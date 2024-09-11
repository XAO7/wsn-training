
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "continuous_adc_read.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/udp.h"

#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "esp_netif.h"



#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif


static EventGroupHandle_t wifi_evevt_group;


#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define TCP_PORT 6666
#define UDP_PORT 6665

static const char *TAG_CLI = "客户端";
static const char *TAG_GETIP = "获取IP";
static const char *TAG_ADC = "数模转换";

static int retrynum = 0;
static TaskHandle_t AdcTaskHandle = NULL;
static TaskHandle_t ClientTaskHandle = NULL;
adc_channel_t channel[2] = {ADC_CHANNEL_0};

uint32_t sensordata = 0xabc;

char host_ip[32];
bool getip = false;

static void HandlerEvent(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retrynum < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            retrynum++;
            ESP_LOGI(TAG_CLI, "重试连接 AP");
        } else {
            xEventGroupSetBits(wifi_evevt_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG_CLI,"连接 AP 失败");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG_CLI, "获取 IP:" IPSTR, IP2STR(&event->ip_info.ip));
        retrynum = 0;
        xEventGroupSetBits(wifi_evevt_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    wifi_evevt_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &HandlerEvent,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &HandlerEvent,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
          
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG_CLI, "WIFI启动完成");

    
    EventBits_t bits = xEventGroupWaitBits(wifi_evevt_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_CLI, "连接到 ap SSID:%s 密码:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG_CLI, "连接 SSID 失败:%s, 密码:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG_CLI, "UNEXPECTED EVENT");
    }
}

void vTaskGetIP(void *pvParameters)
{
    #define BUFFER_SIZE 256
    char buffer[BUFFER_SIZE];
    struct sockaddr_in client_addr;
    int addr_len;

    while (1)
    {
        struct sockaddr_in server_addr;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(UDP_PORT);

        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG_GETIP, "无法创建套接字: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG_GETIP, "创建 IP 插口，从端口接收 IP %d", UDP_PORT);

        if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            ESP_LOGE(TAG_GETIP, "绑定套接字失败 : errno %d", errno);
            close(sock);
            break;
        }

        ESP_LOGI(TAG_GETIP, "UDP 服务器正在端口上运行 %d\n", UDP_PORT);

        while (1)
        {
            memset(buffer, 0, BUFFER_SIZE);
            int recv_len = recvfrom(sock, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, NULL);
            if (recv_len < 0)
            {
                ESP_LOGE(TAG_GETIP, "接收失败: %d", errno);
                break;
            }
            strcpy(host_ip, inet_ntoa(client_addr.sin_addr));
            getip = true;
            ESP_LOGI(TAG_GETIP, "Received UDP packet from %s:%d : %s", host_ip, ntohs(client_addr.sin_port), buffer);

            strcpy(host_ip, buffer);
            
            shutdown(sock, 0);
            close(sock);
            vTaskDelete(NULL);
        }

        shutdown(sock, 0);
        close(sock);
    }
    vTaskDelete(NULL);
}

void vTaskClient(void *pvParamters)
{
    while (getip == false)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    while (1)
    {
        struct sockaddr_in dest_addr;
        inet_pton(AF_INET, (const char *)host_ip, &dest_addr.sin_addr);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(TCP_PORT);

        char payload[128];
        // char rx_buffer[128];

        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG_CLI, "无法创建套接字: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG_CLI, "创建了套接字，连接到 %s:%d", host_ip, TCP_PORT);

        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0) {
            ESP_LOGE(TAG_CLI, "套接字无法连接: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            goto CLEAR;
        }
        ESP_LOGI(TAG_CLI, "插座连接成功");

        while (1)
        {
            xTaskNotifyWait(0xffffffff, 0x0, &sensordata, pdMS_TO_TICKS(10000));
            sprintf(payload, "%lx", sensordata);
            
            int err = send(sock, payload, strlen(payload), 0);

            if (err < 0) {
                ESP_LOGE(TAG_CLI, "发送过程中发生错误: errno %d", errno);
                break;
            }
            ESP_LOGI(TAG_CLI, "发送: %s", payload);

           
        }

        CLEAR:
        if (sock != -1) {
            ESP_LOGE(TAG_CLI, "关闭套接字并重新启动...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    BaseType_t mustYield = pdFALSE;
    
    vTaskNotifyGiveFromISR(AdcTaskHandle, &mustYield);

    return (mustYield == pdTRUE);
}

void vTaskADCRead(void * pvParameters)
{
    esp_err_t ret;
    uint32_t ret_num = 0;
    uint8_t result[EXAMPLE_READ_LEN] = {0};
    memset(result, 0xcc, EXAMPLE_READ_LEN);
    adc_continuous_handle_t handle = NULL;

    continuous_adc_init(channel, sizeof(channel) / sizeof(adc_channel_t), &handle);
    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = s_conv_done_cb,
    };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(handle, &cbs, NULL));
    ESP_ERROR_CHECK(adc_continuous_start(handle));

    char unit[] = EXAMPLE_ADC_UNIT_STR(EXAMPLE_ADC_UNIT);
    
    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ret = adc_continuous_read(handle, result, EXAMPLE_READ_LEN, &ret_num, 0);
        if (ret == ESP_OK) {
            
            for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
                adc_digi_output_data_t *p = (adc_digi_output_data_t*)&result[i];
                uint32_t chan_num = EXAMPLE_ADC_GET_CHANNEL(p);
                uint32_t data = EXAMPLE_ADC_GET_DATA(p);
            
                if (chan_num < SOC_ADC_CHANNEL_NUM(EXAMPLE_ADC_UNIT)) {
                    if (getip)
                        xTaskNotify(ClientTaskHandle, data, eSetValueWithOverwrite);
                    else
                        ESP_LOGI(TAG_ADC, "Unit: %s, Channel: %"PRIu32", Value: 0x%x", unit, chan_num, (unsigned int)data);
                } else {
                    ESP_LOGW(TAG_ADC, "无效数据 [%s_%"PRIu32"_%"PRIx32"]", unit, chan_num, data);
                }
            }
           
            vTaskDelay(pdMS_TO_TICKS(50));
        } else if (ret == ESP_ERR_TIMEOUT) {
        
            break;
        }
    }

    ESP_ERROR_CHECK(adc_continuous_stop(handle));
    ESP_ERROR_CHECK(adc_continuous_deinit(handle));
}

void app_main(void)
{
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG_CLI, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    xTaskCreate(vTaskGetIP, "get_ip", 2048, NULL, 5, NULL);
    xTaskCreate(vTaskADCRead, "continuous_adc_read", 2048, NULL, 2, &AdcTaskHandle);
    xTaskCreate(vTaskClient, "tcp_client", 2048, NULL, 5, &ClientTaskHandle);
}
