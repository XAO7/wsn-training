/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "lcd.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
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

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1


static int s_retry_num = 0;

#define TCP_PORT                        6666
#define UDP_PORT                        6665
#define KEEPALIVE_IDLE              10
#define KEEPALIVE_INTERVAL          2
#define KEEPALIVE_COUNT             3

static const char *TAG_SERVER = "SERVER";
static const char *TAG_CASTIP = "CAST IP";
static const char *TAG_DRAW = "DRAW";

#define BUFFER_SIZE 64
#define QUEUE_SIZE 300

#define BROADCAST_IP "255.255.255.255"

char host_ip[32];

char rx_buffer[BUFFER_SIZE];

char queue[QUEUE_SIZE + 60];
char *queue_end = queue + QUEUE_SIZE;
char *ptr_recv = queue;
char *ptr_draw = queue;
long total_recv = 0;
long total_draw = 0;
char light[3];

static char padding[20]; // eye-blinding lights!


static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG_SERVER, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG_SERVER,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        sprintf(host_ip, IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG_SERVER, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG_SERVER, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_SERVER, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG_SERVER, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG_SERVER, "UNEXPECTED EVENT");
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
            ESP_LOGE(TAG_CASTIP, "Unable to create socket: errno %d", errno);
            goto CLEAN_UP;
        }

        ESP_LOGI(TAG_CASTIP, "Socket created");

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
                ESP_LOGE(TAG_CASTIP, "send failed");
                goto CLEAN_UP;
            } else {
                ESP_LOGI(TAG_CASTIP, "ip sent");
            }

            vTaskDelay(pdMS_TO_TICKS(2000));
        }

        CLEAN_UP:
        close(sock);
        vTaskDelete(NULL);
    }
}

void recv_data(const int sock)
{
    int len;
    

    do {
        len = recv(sock, rx_buffer, BUFFER_SIZE, 0);
        if (len < 0) {
            ESP_LOGE(TAG_SERVER, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG_SERVER, "Connection closed");
        } else {
            rx_buffer[len] = 0; // Null-terminate whatever is received and treat it like a string
            ESP_LOGI(TAG_SERVER, "Received %d bytes: %s", len, rx_buffer);

            strcpy(ptr_recv, rx_buffer);
            ptr_recv += len;
            total_recv += len;
            if (ptr_recv > queue_end) {
                strcpy(queue, queue_end);
                ptr_recv = queue + (ptr_recv - queue_end);
            }

            // vTaskDelay(pdMS_TO_TICKS(50));
        

            // // send() can return less bytes than supplied length.
            // // Walk-around for robust implementation.
            // int to_write = len;
            // while (to_write > 0) {
            //     int written = send(sock, rx_buffer + (len - to_write), to_write, 0);
            //     if (written < 0) {
            //         ESP_LOGE(TAG_SERVER, "Error occurred during sending: errno %d", errno);
            //         // Failed to retransmit, giving up
            //         return;
            //     }
            //     to_write -= written;
            // }
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
            ESP_LOGE(TAG_SERVER, "Unable to create socket: errno %d", errno);
            vTaskDelete(NULL);
            return;
        }
        int opt = 1;
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        ESP_LOGI(TAG_SERVER, "Socket created");

        int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0) {
            ESP_LOGE(TAG_SERVER, "Socket unable to bind: errno %d", errno);
            ESP_LOGE(TAG_SERVER, "IPPROTO: %d", AF_INET);
            goto CLEAN_UP;
        }
        ESP_LOGI(TAG_SERVER, "Socket bound, port %d", TCP_PORT);

        err = listen(listen_sock, 1);
        if (err != 0) {
            ESP_LOGE(TAG_SERVER, "Error occurred during listen: errno %d", errno);
            goto CLEAN_UP;
        }

        while (1) {

            ESP_LOGI(TAG_SERVER, "Socket listening");

            struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
            socklen_t addr_len = sizeof(source_addr);
            int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);

            if (sock < 0) {
                ESP_LOGE(TAG_SERVER, "Unable to accept connection: errno %d", errno);
                break;
            }

            // Set tcp keepalive option
            setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
            setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
            setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
            setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
            // Convert ip address to string
            if (source_addr.ss_family == PF_INET) {
                inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
            }
            ESP_LOGI(TAG_SERVER, "Socket accepted ip address: %s", addr_str);

            recv_data(sock);

            shutdown(sock, 0);
            close(sock);
        }

        CLEAN_UP:
        close(listen_sock);
        vTaskDelete(NULL);
    }
}

void vTaskDraw(void * pvParameters)
{
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(18));


        if (total_draw >= total_recv) {
            ptr_draw -= 3;
            total_draw -= 3;
        }
        strncpy(light, ptr_draw, 3);
        ptr_draw += 3;
        total_draw += 3;
        if (ptr_draw == queue_end) {
            ptr_draw = queue;
        }
        
        ESP_LOGI(TAG_DRAW, "total_recv: %ld, total_draw: %ld, Draw %s", total_recv, total_draw, light);
        lcd_set_progress((int)strtol(light, NULL, 16) * 128 / 0xfff);
        lcd_draw();
    }
}

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG_SERVER, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
    lcd_init();

    xTaskCreate(vTaskCastIP, "cast_ip", 2048, NULL, 1, NULL);
    xTaskCreate(vTaskServer, "tcp_server", 4096, NULL, 5, NULL);
    xTaskCreate(vTaskDraw, "draw", 4096, NULL, 3, NULL);
}
