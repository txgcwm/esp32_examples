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
#include "esp_partition.h"
#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "apps/sntp/sntp.h"

#include "ota.h"

#define DR_REG_RNG_BASE   0x3ff75144

#define ESP_INTR_FLAG_DEFAULT 0
#define TAG	"app_main"


static EventGroupHandle_t event_group;  // Event group for inter-task communication
nvs_handle wifi_handle;  // NVS handler
const int WIFI_CONNECTED_BIT = BIT0;
const int BUTTON_PRESSED_BIT = BIT1;

esp_err_t miot_nvs_init()
{
	esp_err_t err = nvs_flash_init();  // initialize NVS flash
	
	// if it is invalid, try to erase it
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
		printf("Got NO_FREE_PAGES error, trying to erase the partition...\n");
		
		// find the NVS partition
        const esp_partition_t* nvs_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);      
		if(!nvs_partition) {
			printf("FATAL ERROR: No NVS partition found\n");
		}
		
		// erase the partition
        err = (esp_partition_erase_range(nvs_partition, 0, nvs_partition->size));
		if(err != ESP_OK) {
			printf("FATAL ERROR: Unable to erase the partition\n");
		}
		printf("Partition erased!\n");
		
		// now try to initialize it again
		err = nvs_flash_init();
		if(err != ESP_OK) {
			printf("FATAL ERROR: Unable to initialize NVS\n");
		}
	}

	return err;
}

esp_err_t miot_nvs_save_ssid_passwd(char* ssid, char* passwd)
{
	printf("ssid(%s), passwd(%s)\n", ssid, passwd);

	esp_err_t err = nvs_set_str(wifi_handle, "ssid", ssid);
	if(err != ESP_OK) {
		printf("Error in nvs_set_str ssid! (%04X)\n", err);
		return err;
	}

	err = nvs_commit(wifi_handle);
	if(err != ESP_OK) {
		printf("\nError in commit! (%04X)\n", err);
		return err;
	}

	err = nvs_set_str(wifi_handle, "passwd", passwd);
	if(err != ESP_OK) {
		printf("\nError in nvs_set_str passwd! (%04X)\n", err);
		return err;
	}

	err = nvs_commit(wifi_handle);
	if(err != ESP_OK) {
		printf("\nError in commit! (%04X)\n", err);
		return err;
	}

	return err;
}

esp_err_t miot_nvs_read_ssid_passwd(char* ssid, size_t* ssid_size, char* passwd, size_t* passwd_size)
{
	esp_err_t err = nvs_get_str(wifi_handle, "ssid", ssid, ssid_size);
	if(err != ESP_OK) {
		if(err == ESP_ERR_NVS_NOT_FOUND) {
			printf("Key not found\n");
		}

		printf("Error in nvs_get_str to get string! (%04X)\n", err);
		return err;
	}

	err = nvs_get_str(wifi_handle, "passwd", passwd, passwd_size);
	if(err != ESP_OK) {
		if(err == ESP_ERR_NVS_NOT_FOUND) {
			printf("Key not found\n");
		}

		printf("Error in nvs_get_str to get string! (%04X)\n", err);
		return err;
	}

	return err;
}

void IRAM_ATTR button_isr_handler(void* arg)
{	
	xEventGroupSetBitsFromISR(event_group, BUTTON_PRESSED_BIT, NULL);

	return;
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {		
    case SYSTEM_EVENT_STA_START:
    	printf("%s, %d ~~~~~~~~~~~~~~~~\n", __FUNCTION__, __LINE__);
        esp_wifi_connect();
        break;

	case SYSTEM_EVENT_STA_GOT_IP:
		printf("%s, %d ~~~~~~~~~~~~~~~~\n", __FUNCTION__, __LINE__);
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
		} else if (*type == SC_TYPE_ESPTOUCH_AIRKISS) {
			ESP_LOGI(TAG, "SC_TYPE:SC_TYPE_AIRKISS");
		}
		break;

	case SC_STATUS_LINK:
		ESP_LOGI(TAG, "SC_STATUS_LINK");
		wifi_config_t wifi_config;
		wifi_config.sta = *((wifi_sta_config_t *)pdata);
		esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

		miot_nvs_save_ssid_passwd((char *)wifi_config.sta.ssid, (char *)wifi_config.sta.password);

		esp_wifi_disconnect();
        vTaskDelay(1000 / portTICK_RATE_MS);
		esp_wifi_connect();
		break;

	case SC_STATUS_LINK_OVER:
		ESP_LOGI(TAG, "SC_STATUS_LINK_OVER");
		if (pdata != NULL) {
			uint8_t phone_ip[4] = {0};

			memcpy(phone_ip, (uint8_t *) pdata, 4);
			ESP_LOGI(TAG, "Phone ip: %d.%d.%d.%d", phone_ip[0],
			       phone_ip[1], phone_ip[2], phone_ip[3]);
		}
		esp_smartconfig_stop();
		break;
	}

	return;
}

void smartconfig_init()
{
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
	wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

	size_t ssid_size = 30;
	char ssid[30] = {0};
	size_t passwd_size = 60;
	char passwd[60] = {0};

	esp_err_t err = miot_nvs_read_ssid_passwd(ssid, &ssid_size, passwd, &passwd_size);
	if(err == ESP_OK) {
		wifi_config_t wifi_config = {0};

		printf("ssid(%s), passwd(%s)\n", ssid, passwd);

		memcpy(wifi_config.sta.ssid, ssid, ssid_size);
		memcpy(wifi_config.sta.password, passwd, passwd_size);

		ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
	    ESP_ERROR_CHECK(esp_wifi_start());
	}

    return;
}

void miot_sntp_gettime()
{
	// initialize the SNTP service
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, CONFIG_SNTP_SERVER);
	sntp_init();
	
	// wait for the service to set the time
	time_t now;
	struct tm timeinfo;
	time(&now);
	localtime_r(&now, &timeinfo);

	while(timeinfo.tm_year < (2016 - 1900)) {
		printf("Time not set, waiting...\n");
		vTaskDelay(5000 / portTICK_PERIOD_MS);
		time(&now);
        localtime_r(&now, &timeinfo);
	}
	
	// print the actual time with different formats
	char buffer[100];
	printf("Actual UTC time:\n");
	strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", &timeinfo);
	printf("- %s\n", buffer);
	strftime(buffer, sizeof(buffer), "%A, %d %B %Y", &timeinfo);
	printf("- %s\n", buffer);
	strftime(buffer, sizeof(buffer), "Today is day %j of year %Y", &timeinfo);
	printf("- %s\n", buffer);
	printf("\n");

	// change the timezone to Italy
	// setenv("TZ", "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00", 1);
	setenv("TZ", "GMT-8", 1);
	tzset();

	localtime_r(&now, &timeinfo);
	strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", &timeinfo);
	printf("Actual time: %s\n", buffer);

	return;
}

void main_task(void *pvParameter)
{
	for(;;) {
		// waiting for button press
		xEventGroupWaitBits(event_group, BUTTON_PRESSED_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
		printf("Button pressed...\n");

		smartconfig_init();

		// wait for connection
		printf("Waiting for connection to the wifi network...\n ");
		xEventGroupWaitBits(event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
		printf("Connected...\n");

		// print the local IP address
		tcpip_adapter_ip_info_t ip_info;
		ESP_ERROR_CHECK(tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info));
		printf("IP Address:  %s\n", ip4addr_ntoa(&ip_info.ip));
		printf("Subnet mask: %s\n", ip4addr_ntoa(&ip_info.netmask));
		printf("Gateway:     %s\n", ip4addr_ntoa(&ip_info.gw));

		miot_sntp_gettime();

		xTaskCreate(&miot_ota_task, "ota_task", 8192, NULL, 5, NULL);

		while(1) {
			vTaskDelay(1000 / portTICK_RATE_MS);

			// get a new random number and print it
			uint32_t randomNumber = READ_PERI_REG(DR_REG_RNG_BASE);
			printf("New random number: %u\n", randomNumber);
		}
	}

	return;
}

void app_main()
{
	printf("Application started\n\n");

	event_group = xEventGroupCreate();

	miot_nvs_init();
	tcpip_adapter_init();

	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

	// open the partition in RW mode
	esp_err_t err = nvs_open("storage", NVS_READWRITE, &wifi_handle);
    if (err != ESP_OK) {
		printf("FATAL ERROR: Unable to open NVS\n");
	}
	printf("NVS open OK\n");

	button_setup();
	wifi_setup();

    xTaskCreate(&main_task, "main_task", 2048, NULL, 5, NULL);

    return;
}


