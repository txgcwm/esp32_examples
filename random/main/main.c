#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DR_REG_RNG_BASE   0x3ff75144


void main_task(void *pvParameter)
{
	while(1) {
		vTaskDelay(5000 / portTICK_RATE_MS); // wait 5 second
		
		// get a new random number and print it
		uint32_t randomNumber = READ_PERI_REG(DR_REG_RNG_BASE);
		printf("New random number: %u\n", randomNumber);
	}

	return;
}

void app_main()
{
    xTaskCreate(&main_task, "main_task", 2048, NULL, 5, NULL);

    return;
}