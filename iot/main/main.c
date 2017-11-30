#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_smartconfig.h"
#include "esp_system.h"

#include "nvs_flash.h"
#include "driver/gpio.h"

#include "lwip/netdb.h"
#include "lwip/sockets.h"

#define ESP_INTR_FLAG_DEFAULT 0
#define TAG	"app_main"

// Event group for inter-task communication
static EventGroupHandle_t event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int BUTTON_PRESSED_BIT = BIT1;

void IRAM_ATTR button_isr_handler(void* arg)
{	
	xEventGroupSetBitsFromISR(event_group, BUTTON_PRESSED_BIT, NULL);

	return;
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {		
    case SYSTEM_EVENT_STA_START:
    	xEventGroupWaitBits(event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
        esp_wifi_connect();
        break;
    
	case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(event_group, WIFI_CONNECTED_BIT);
        break;
    
	case SYSTEM_EVENT_STA_DISCONNECTED:
		xEventGroupClearBits(event_group, WIFI_CONNECTED_BIT);
        break;
    
	default:
        break;
    }

	return ESP_OK;
}

static void smartconfig_handler(smartconfig_status_t status, void *pdata)
{
	switch (status) {
	case SC_STATUS_WAIT:
		ESP_LOGI(TAG, "SC_STATUS_WAIT");
		break;

	case SC_STATUS_FIND_CHANNEL:
		ESP_LOGI(TAG, "SC_STATUS_FIND_CHANNEL");
		break;

	case SC_STATUS_GETTING_SSID_PSWD:
		ESP_LOGI(TAG, "SC_STATUS_GETTING_SSID_PSWD");
		smartconfig_type_t *type = pdata;
		if (*type == SC_TYPE_ESPTOUCH) {
			ESP_LOGI(TAG, "SC_TYPE:SC_TYPE_ESPTOUCH");
		} else {
			ESP_LOGI(TAG, "SC_TYPE:SC_TYPE_AIRKISS");
		}
		break;

	case SC_STATUS_LINK:
		ESP_LOGI(TAG, "SC_STATUS_LINK");
		wifi_config_t wifi_config;
		wifi_config.sta = *((wifi_sta_config_t *)pdata);
		esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

		esp_wifi_disconnect();
        xEventGroupSetBits(event_group, WIFI_CONNECTED_BIT);
		//esp_wifi_connect();
		break;

	case SC_STATUS_LINK_OVER:
		ESP_LOGI(TAG, "SC_STATUS_LINK_OVER");
		if (pdata != NULL) {
			uint8_t phone_ip[4] = { 0 };

			memcpy(phone_ip, (uint8_t *) pdata, 4);
			ESP_LOGI(TAG, "Phone ip: %d.%d.%d.%d", phone_ip[0],
			       phone_ip[1], phone_ip[2], phone_ip[3]);
		}
		esp_smartconfig_stop();
        // xEventGroupSetBits(event_group, CONNECTED_BIT);
		break;
	}

	return;
}

void smartconfig_init()
{
	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS));
	ESP_ERROR_CHECK(esp_smartconfig_start(smartconfig_handler));
}

void button_setup()
{	
	gpio_pad_select_gpio(CONFIG_BUTTON_PIN);
	gpio_set_direction(CONFIG_BUTTON_PIN, GPIO_MODE_INPUT);
	gpio_set_intr_type(CONFIG_BUTTON_PIN, GPIO_INTR_NEGEDGE);
	
	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
	gpio_isr_handler_add(CONFIG_BUTTON_PIN, button_isr_handler, NULL);

	return;
}

void wifi_setup()
{	
	tcpip_adapter_init();

	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

	wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

	wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    return;
}

void main_task(void *pvParameter)
{
	for(;;) {
		// waiting for button press
		xEventGroupWaitBits(event_group, BUTTON_PRESSED_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
		printf("Button pressed, sending SMS...\n");

		smartconfig_init();

		// wait for connection
		printf("Waiting for connection to the wifi network...\n ");
		xEventGroupWaitBits(event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
		printf("Connected to %s\n\n", CONFIG_WIFI_SSID);
	}

	return;
}

void app_main()
{
	printf("Application started\n\n");
	
	nvs_flash_init();

	event_group = xEventGroupCreate();

	// wifi_setup();
	button_setup();

    xTaskCreate(&main_task, "main_task", 2048, NULL, 5, NULL);

    return;
}


