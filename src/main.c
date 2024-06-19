/*
Development of  2 d.o.f Robot
 - MCU: STM32F1
 by Tutla
*/

#include <string.h>
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <mcuio.h>
#include <miniprintf.h>
#include <semphr.h>

#include <libopencm3/cm3/cortex.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/rtc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/adc.h>

#define mainECHO_TASK_PRIORITY (tskIDLE_PRIORITY + 1)
#define USE_USB		1		// Set to 1 for USB

/*Global Variable*/
static QueueHandle_t xQueueOut = NULL, xQueueIn = NULL;
static SemaphoreHandle_t h_mutex;

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,signed portCHAR *pcTaskName);

void vApplicationStackOverflowHook(xTaskHandle *pxTask,signed portCHAR *pcTaskName) {
	(void)pxTask;
	(void)pcTaskName;
	for(;;);
}

typedef struct Message {
	uint16_t vtemp;
	TickType_t elapsed;
} Message_t;

/* mutex_lock()/mutex_unlock()
	- prevents competing tasks from printing in the middle of our own line of text
*/
static void mutex_lock(void) {
	xSemaphoreTake(h_mutex,portMAX_DELAY);
}

static void mutex_unlock(void) {
	xSemaphoreGive(h_mutex);
}

static uint16_t read_adc(uint8_t channel) {

	/*Read ADC*/
	adc_set_sample_time(ADC1,channel,ADC_SMPR_SMP_239DOT5CYC);
	adc_set_regular_sequence(ADC1,1,&channel);
	adc_start_conversion_direct(ADC1);
	while ( !adc_eoc(ADC1) )
		taskYIELD();
	return adc_read_regular(ADC1);
}

static void gpio_setup(void) {
	rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);	// Use this for "blue pill"

	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOC);
	gpio_set_mode(GPIOC,GPIO_MODE_OUTPUT_50_MHZ,GPIO_CNF_OUTPUT_PUSHPULL,GPIO13);
	gpio_set_mode(GPIOA,GPIO_MODE_INPUT,GPIO_CNF_INPUT_ANALOG,GPIO0);
	
}

static void adc_setup(void){
	
	/*Setup ADC*/
	rcc_peripheral_enable_clock(&RCC_APB2ENR,RCC_APB2ENR_ADC1EN);
	adc_power_off(ADC1);
	rcc_peripheral_reset(&RCC_APB2RSTR,RCC_APB2RSTR_ADC1RST);
	rcc_peripheral_clear_reset(&RCC_APB2RSTR,RCC_APB2RSTR_ADC1RST);
	rcc_set_adcpre(RCC_CFGR_ADCPRE_PCLK2_DIV6);	// Set. 12MHz, Max. 14MHz
	adc_set_dual_mode(ADC_CR1_DUALMOD_IND);		// Independent mode
	adc_disable_scan_mode(ADC1);
	adc_set_right_aligned(ADC1);
	adc_set_single_conversion_mode(ADC1);
	adc_set_sample_time(ADC1,ADC_CHANNEL_TEMP,ADC_SMPR_SMP_239DOT5CYC);
	adc_set_sample_time(ADC1,ADC_CHANNEL_VREF,ADC_SMPR_SMP_239DOT5CYC);
	adc_enable_temperature_sensor();
	adc_power_on(ADC1);
	adc_reset_calibration(ADC1);
	adc_calibrate_async(ADC1);
	while ( adc_is_calibrating(ADC1) );
}

static void input_task(void *args) {
	(void)args;
	char ch_buff = 0;
	uint32_t val = 0;

	for (;;) {
		if (std_peek() < 1) { /*similar with Serial.available()*/
			vTaskDelay(pdMS_TO_TICKS(1));
			continue;
		}
		else{
			ch_buff = std_getc();
			if(ch_buff == '\n'){
				if (val == 0) continue;
				
				mutex_lock();
				std_printf("Delay Value = %lu\n", val);
				mutex_unlock();
				
				xQueueSend(xQueueIn, &val, 0u);
				val = 0;
			}
				
			if(ch_buff >= '0' && ch_buff <= '9'){
				val = val*10 + ch_buff-'0';
			}
		}
	}
}

static void main_task (void *args) {
	/*It will be changed to ISR*/
	(void)args;
	static const uint16_t v25 = 143;
	TickType_t t0 = 0;
	uint32_t del = 200;
	Message_t send_value;
	send_value.vtemp = 0;
	send_value.elapsed = 0;

	for (;;) {
		t0 = xTaskGetTickCount();
		
		/*Receive delay from USB*/
		xQueueReceive(xQueueIn, &del, 0u);
		
		/*Read Temperature in Degree*/
		send_value.vtemp = (uint16_t)read_adc(ADC_CHANNEL_TEMP) * 3300 / 4095;
		send_value.vtemp = ((v25-send_value.vtemp)/45)+2500;
		
		vTaskDelay(pdMS_TO_TICKS(del));
		send_value.elapsed = xTaskGetTickCount()-t0;
		
		/*Send the time value to USB*/
		xQueueSend(xQueueOut, &send_value, 0u);
		
	}
}

static void output_task (void *args) {
	(void)args;
	TickType_t t0 = 0;
	Message_t received_value;

	for (;;) {
		t0 = xTaskGetTickCount();
		xQueueReceive(xQueueOut, &received_value, portMAX_DELAY);
		gpio_toggle(GPIOC,GPIO13);
		
		mutex_lock();
		std_printf("T %d.%02d main %lu, usb %lu\n", 
					received_value.vtemp/100,
					received_value.vtemp%100,
					received_value.elapsed, 
					xTaskGetTickCount()-t0);
		mutex_unlock();
	}
}

int main(void) {
	
	/*Hardware Setup*/
	gpio_setup();
	adc_setup();
	
	/*Task Setup*/
	xQueueOut	= xQueueCreate(10, sizeof(Message_t));
	xQueueIn	= xQueueCreate(10, sizeof(uint16_t));
	h_mutex		= xSemaphoreCreateMutex();
	
	/*Create Task*/
	xTaskCreate(main_task,"main_task",400,NULL,configMAX_PRIORITIES-1,NULL);
	xTaskCreate(input_task,"input_task",400,NULL,mainECHO_TASK_PRIORITY,NULL);
	xTaskCreate(output_task,"output_task",400,NULL,mainECHO_TASK_PRIORITY,NULL);
	
	/*Communication Setup*/
	#if USE_USB
		usb_start(1,1);
		std_set_device(mcu_usb);							// Use USB for std I/O
	#else
		rcc_periph_clock_enable(RCC_GPIOA);					// TX=A9, RX=A10, CTS=A11, RTS=A12
		rcc_periph_clock_enable(RCC_USART1);
		
		gpio_set_mode(GPIOA,GPIO_MODE_OUTPUT_50_MHZ,
			GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,GPIO9|GPIO11);
		gpio_set_mode(GPIOA,GPIO_MODE_INPUT,
			GPIO_CNF_INPUT_FLOAT,GPIO10|GPIO12);
		open_uart(1,115200,"8N1","rw",1,1);					// UART1 with RTS/CTS flow control
		std_set_device(mcu_uart1);							// Use UART1 for std I/O
	#endif

	/*Scheduling and start the program*/
	vTaskStartScheduler();
	
	for (;;)
		;
	return 0;
}