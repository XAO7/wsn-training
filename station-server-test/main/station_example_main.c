#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>


#include "lwip/err.h"
#include "lwip/sys.h"


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


static int retrynum = 0;

#define TCP_PORT                        6666
#define UDP_PORT                        6665
#define KEEPALIVE_IDLE              10
#define KEEPALIVE_INTERVAL          2
#define KEEPALIVE_COUNT             3

static const char *TAG_SERVER = "服务器";
static const char *TAG_CASTIP = "发送IP";

#define BUFFER_SIZE 128

#define BROADCAST_IP "255.255.255.255"

char host_ip[32];


static void HandlerEvent(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retrynum < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            retrynum++;
            ESP_LOGI(TAG_SERVER, "重试连接 AP");
        } else {
            xEventGroupSetBits(wifi_evevt_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG_SERVER,"连接 AP 失败");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        sprintf(host_ip, IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG_SERVER, "收到 IP 地址:" IPSTR, IP2STR(&event->ip_info.ip));
        retrynum = 0;
        xEventGroupSetBits(wifi_evevt_group, WIFI_CONNECTED_BIT);
    }
}

void Launch_Wifi(void)
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

    ESP_LOGI(TAG_SERVER, "启动Wifi 已完成。");

  
    EventBits_t bits = xEventGroupWaitBits(wifi_evevt_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

   
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_SERVER, "连接到 ap SSID:%s 密码:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG_SERVER, "连接 SSID 失败:%s, 密码:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG_SERVER, "突发事件");
    }
}

void vTaskCastIP(void * pvParameters)
{
    struct sockaddr_in broadcastAddr;
    int broadcastPermission;
    int sendResult;
    char buffer[BUFFER_SIZE];

    while (1)
    {
        broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;
        broadcastAddr.sin_family = AF_INET;
        broadcastAddr.sin_port = htons(UDP_PORT);

        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG_CASTIP, "无法创建套接字: errno %d", errno);
            goto CLEAN_UP;
        }

        ESP_LOGI(TAG_CASTIP, "套接字已创建");

        broadcastPermission = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastPermission, sizeof(broadcastPermission)) < 0) {
            ESP_LOGE(TAG_CASTIP, "setsockopt() failed");
            goto CLEAN_UP;
        }


        while (1)
        {
            strcpy(buffer, host_ip);

            sendResult = sendto(sock, buffer, strlen(buffer), 0, (struct sockaddr *)&broadcastAddr, sizeof(broadcastAddr));
            if (sendResult < 0) {
                ESP_LOGE(TAG_CASTIP, "发送失败");
                goto CLEAN_UP;
            } else {
                ESP_LOGI(TAG_CASTIP, "IP 已发送");
            }

            vTaskDelay(pdMS_TO_TICKS(2000));
        }

        CLEAN_UP:
        close(sock);
        vTaskDelete(NULL);
    }
}

void do_retransmit(const int sock)
{
    int len;
    char rx_buffer[BUFFER_SIZE];

    do {
        len = recv(sock, rx_buffer, 3, 0);
        if (len < 0) {
            ESP_LOGE(TAG_SERVER, "接收过程中发生错误: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG_SERVER, "连接已关闭");
        } else {
            rx_buffer[len] = 0; 
            ESP_LOGI(TAG_SERVER, "已收到 %d 字节: %s", len, rx_buffer);

           
        }
    } while (len > 0);
}

void vTaskServer(void * pvParameters)
{
    while (1) {
        char addr_str[BUFFER_SIZE];
        int keepAlive = 1;
        int keepIdle = KEEPALIVE_IDLE;
        int keepInterval = KEEPALIVE_INTERVAL;
        int keepCount = KEEPALIVE_COUNT;
        struct sockaddr_storage dest_addr;

        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(TCP_PORT);

        int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (listen_sock < 0) {
            ESP_LOGE(TAG_SERVER, "无法创建套接字: errno %d", errno);
            vTaskDelete(NULL);
            return;
        }
        int opt = 1;
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        ESP_LOGI(TAG_SERVER, "套接字已创建");

        int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0) {
            ESP_LOGE(TAG_SERVER, "套接字无法绑定: errno %d", errno);
            ESP_LOGE(TAG_SERVER, "IPPROTO: %d", AF_INET);
            goto CLEAN_UP;
        }
        ESP_LOGI(TAG_SERVER, "套接字已绑定, 端口 %d", TCP_PORT);

        err = listen(listen_sock, 1);
        if (err != 0) {
            ESP_LOGE(TAG_SERVER, "监听时发生错误: errno %d", errno);
            goto CLEAN_UP;
        }

        while (1) {

            ESP_LOGI(TAG_SERVER, "套接字监听");

            struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
            socklen_t addr_len = sizeof(source_addr);
            int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);

            if (sock < 0) {
                ESP_LOGE(TAG_SERVER, "无法连接: errno %d", errno);
                break;
            }

           
            setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
            setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
            setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
            setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
           
            if (source_addr.ss_family == PF_INET) {
                inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
            }
            ESP_LOGI(TAG_SERVER, "套接字接收的 IP 地址: %s", addr_str);

            do_retransmit(sock);

            shutdown(sock, 0);
            close(sock);
        }

        CLEAN_UP:
        close(listen_sock);
        vTaskDelete(NULL);
    }
}

void app_main(void)
{
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG_SERVER, "ESP_WIFI_MODE_STA");
    Launch_Wifi();

    xTaskCreate(vTaskCastIP, "cast_ip", 2048, NULL, 4, NULL);
    xTaskCreate(vTaskServer, "tcp_server", 4096, NULL, 5, NULL);
}
