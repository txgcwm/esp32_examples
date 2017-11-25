#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mdns.h"


// Event group
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;


// Wifi event handler
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    
	case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    
	case SYSTEM_EVENT_STA_DISCONNECTED:
		xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    
	default:
        break;
    }
   
	return ESP_OK;
}

static void query_mdns_service(mdns_server_t * mdns, const char * service, const char * proto)
{
    if(!mdns) {
        return;
    }

    uint32_t res;

    if (!proto) {
        printf("Host Lookup: %s\n", service);
        res = mdns_query(mdns, service, 0, 1000);
        if (res) {
            size_t i;
            for(i=0; i<res; i++) {
                const mdns_result_t * r = mdns_result_get(mdns, i);
                if (r) {
                    printf("  %u: " IPSTR " " IPV6STR, i+1, 
                        IP2STR(&r->addr), IPV62STR(r->addrv6));
                }
            }
            mdns_result_free(mdns);
        } else {
            printf("  Not Found\n");
        }
    } else {
        printf("Service Lookup: %s.%s \n", service, proto);
        res = mdns_query(mdns, service, proto, 1000);
        if (res) {
            size_t i;
            for(i=0; i<res; i++) {
                const mdns_result_t * r = mdns_result_get(mdns, i);
                if (r) {
                    printf("  %u: %s \"%s\" " IPSTR " " IPV6STR " %u %s\n", i+1, 
                        (r->host)?r->host:"", (r->instance)?r->instance:"", 
                        IP2STR(&r->addr), IPV62STR(r->addrv6),
                        r->port, (r->txt)?r->txt:"");
                }
            }
            mdns_result_free(mdns);
        }
    }

    return;
}

void main_task(void *pvParameter)
{
	// wait for connection
	printf("Main task: waiting for connection to the wifi network...\n");
	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
	printf("connected!\n");
	
	// print the local IP address
	tcpip_adapter_ip_info_t ip_info;
	ESP_ERROR_CHECK(tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info));
	printf("IP Address:  %s\n", ip4addr_ntoa(&ip_info.ip));
	printf("Subnet mask: %s\n", ip4addr_ntoa(&ip_info.netmask));
	printf("Gateway:     %s\n", ip4addr_ntoa(&ip_info.gw));
	
	mdns_server_t* mdns = NULL; // mDNS server instance

    while(1) {
        if (!mdns) {
            esp_err_t err = mdns_init(TCPIP_ADAPTER_IF_STA, &mdns);
            if (err) {
                printf("Failed starting MDNS: %u\n", err);
                continue;
            }

            ESP_ERROR_CHECK( mdns_set_hostname(mdns, CONFIG_MDNS_HOSTNAME) );
            ESP_ERROR_CHECK( mdns_set_instance(mdns, CONFIG_MDNS_INSTANCE) );

            const char * arduTxtData[4] = {
                "board=esp32",
                "tcp_check=no",
                "ssh_upload=no",
                "auth_upload=no"
            };

            ESP_ERROR_CHECK( mdns_service_add(mdns, "_arduino", "_tcp", 3232) );
            ESP_ERROR_CHECK( mdns_service_txt_set(mdns, "_arduino", "_tcp", 4, arduTxtData) );
            ESP_ERROR_CHECK( mdns_service_add(mdns, "_http", "_tcp", 80) );
            ESP_ERROR_CHECK( mdns_service_instance_set(mdns, "_http", "_tcp", "ESP32 WebServer") );
            ESP_ERROR_CHECK( mdns_service_add(mdns, "_smb", "_tcp", 445) );
        } else {
            query_mdns_service(mdns, "esp32", NULL);
            query_mdns_service(mdns, "_arduino", "_tcp");
            query_mdns_service(mdns, "_http", "_tcp");
            query_mdns_service(mdns, "_printer", "_tcp");
            query_mdns_service(mdns, "_ipp", "_tcp");
            query_mdns_service(mdns, "_afpovertcp", "_tcp");
            query_mdns_service(mdns, "_smb", "_tcp");
            query_mdns_service(mdns, "_ftp", "_tcp");
            query_mdns_service(mdns, "_nfs", "_tcp");
        }

        printf("Restarting in 60 seconds!\n");
        vTaskDelay(60000 / portTICK_PERIOD_MS);
        printf("Starting again!\n");
    }

	return;
}

void app_main()
{	
	// initialize NVS
	ESP_ERROR_CHECK(nvs_flash_init());
	
	// create the event group to handle wifi events
	wifi_event_group = xEventGroupCreate();
		
	// initialize the tcp stack
	tcpip_adapter_init();

	// initialize the wifi event handler
	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
	
	// initialize the wifi stack in STAtion mode with config in RAM
	wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

	// configure the wifi connection and start the interface
	wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
	printf("Connecting to %s\n", CONFIG_WIFI_SSID);

    xTaskCreate(&main_task, "main_task", 2048, NULL, 5, NULL);

    return;
}
