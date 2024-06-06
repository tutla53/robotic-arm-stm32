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

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/nvic.h>

#define mainECHO_TASK_PRIORITY (tskIDLE_PRIORITY + 1)

static QueueHandle_t xQueueOut = NULL, xQueueIn = NULL;
static SemaphoreHandle_t h_mutex;

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,signed portCHAR *pcTaskName);

void vApplicationStackOverflowHook(xTaskHandle *pxTask,signed portCHAR *pcTaskName) {
	(void)pxTask;
	(void)pcTaskName;
	for(;;);
}

typedef struct Message {
	bool start;
	uint16_t del;
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

static void gpio_setup(void) {
	rcc_clock_setup_in_hse_8mhz_out_72mhz();	// Use this for "blue pill"
	rcc_periph_clock_enable(RCC_GPIOC);
	gpio_set_mode(GPIOC,GPIO_MODE_OUTPUT_2_MHZ,GPIO_CNF_OUTPUT_PUSHPULL,GPIO13);
}

static void input_task(void *args) {
	(void)args;
	char ch_buff = 0;
	uint32_t val = 0;

	for (;;) {
		ch_buff = std_getc();
		if(ch_buff == '\n'){
			std_printf("Delay Value = %lu\n", val);
			xQueueSend(xQueueIn, &val, 0U);
			val = 0;
		}
			
		if(ch_buff >= '0' && ch_buff <= '9'){
			val = val*10 + ch_buff-'0';
		}
	}
}

static void main_task (void *args) {
	/*It will be changed to ISR*/
	(void)args;
	TickType_t valueToSend = 0;
	uint32_t del = 500;

	for (;;) {
		TickType_t t0 = xTaskGetTickCount();
		
		/*Receive delay from USB*/
		xQueueReceive(xQueueIn, &del, 0U);
		
		gpio_toggle(GPIOC,GPIO13);
		vTaskDelay(pdMS_TO_TICKS(del));
		valueToSend = xTaskGetTickCount()-t0;
		
		/*Send the time value to USB*/
		xQueueSend(xQueueOut, &valueToSend, 0U);
		
	}
}

static void output_task (void *args) {
	(void)args;
	TickType_t valueToReceived = 0, t0 = 0;

	for (;;) {
		t0 = xTaskGetTickCount();
		xQueueReceive(xQueueOut, &valueToReceived, portMAX_DELAY);
		
		mutex_lock();
		std_printf("Elapsed time: main_task = %lu, usb_task = %lu\n", valueToReceived, xTaskGetTickCount()-t0);
		mutex_unlock();
	}
}

int main(void) {

	gpio_setup();
	xQueueOut 	= xQueueCreate(10, sizeof(TickType_t));
	xQueueIn  	= xQueueCreate(10, sizeof(uint32_t));
	h_mutex 	= xSemaphoreCreateMutex();
	
	xTaskCreate(main_task,"main_task",400,NULL,configMAX_PRIORITIES-2,NULL);
	xTaskCreate(input_task,"input_task",400,NULL,mainECHO_TASK_PRIORITY,NULL);
	xTaskCreate(output_task,"output_task",400,NULL,mainECHO_TASK_PRIORITY,NULL);
	
	std_set_device(mcu_usb);
	usb_start(1,1);
	
	vTaskStartScheduler();
	
	for (;;)
		;
	return 0;
}

