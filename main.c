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

static QueueHandle_t xQueue = NULL;
static SemaphoreHandle_t h_mutex;

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,signed portCHAR *pcTaskName);

void vApplicationStackOverflowHook(xTaskHandle *pxTask,signed portCHAR *pcTaskName) {
	(void)pxTask;
	(void)pcTaskName;
	for(;;);
}

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

static void main_task (void *args) {
	(void)args;
	TickType_t valueToSend = 0;
	uint16_t del = 100;

	for (;;) {
		TickType_t t0 = xTaskGetTickCount();
		gpio_toggle(GPIOC,GPIO13);
		vTaskDelay(pdMS_TO_TICKS(del));
		valueToSend = xTaskGetTickCount()-t0;
		
		xQueueSend(xQueue, &valueToSend, 0U);
	}
}

static void usb_task (void *args) {
	(void)args;
	TickType_t valueToReceived = 0, t0 = 0;

	for (;;) {
		t0 = xTaskGetTickCount();
		xQueueReceive(xQueue, &valueToReceived, portMAX_DELAY);
		std_printf("Main: %lu, usb_task: %lu\n", valueToReceived, xTaskGetTickCount()-t0);
	}
}

int main(void) {

	gpio_setup();
	xQueue = xQueueCreate(1, sizeof(TickType_t));
	h_mutex = xSemaphoreCreateMutex();
	
	xTaskCreate(main_task,"Blink",400,NULL,configMAX_PRIORITIES-1,NULL);
	xTaskCreate(usb_task,"USB",400,NULL,mainECHO_TASK_PRIORITY,NULL);
	
	std_set_device(mcu_usb);
	usb_start(1,1);
	
	vTaskStartScheduler();
	
	for (;;)
		;
	return 0;
}

