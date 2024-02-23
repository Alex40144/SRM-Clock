#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "config.h"

#include "esp_wifi.h"
#include <esp_http_server.h>
#include "nvs_flash.h"

const char *ssid = "espAP";
const char *password = "1234";

#define CONNECT_WIFI_SSID "WSC-Mgmt"
#define CONNECT_WIFI_PASS "R6CYhWr9&v6B!fR$"
#define CONNECT_WIFI_MAXIMUM_RETRY 4
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

static const char *TAG = "Main";

int counting = 0;
int up = 0;
int timer = 300;

extern const uint8_t start_html_start[] asm("_binary_start_html_start");
extern const uint8_t start_html_end[] asm("_binary_start_html_end");
extern const uint8_t stop_html_start[] asm("_binary_stop_html_start");
extern const uint8_t stop_html_end[] asm("_binary_stop_html_end");
extern const uint8_t reset_html_start[] asm("_binary_reset_html_start");
extern const uint8_t reset_html_end[] asm("_binary_reset_html_end");

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

static void init_nvs()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static void init_AP()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *wifiAP = esp_netif_create_default_wifi_ap();

    esp_netif_ip_info_t ipInfo;
    esp_netif_set_ip4_addr(&ipInfo.ip, 192, 168, 1, 1);
    esp_netif_set_ip4_addr(&ipInfo.gw, 192, 168, 1, 1);
    esp_netif_set_ip4_addr(&ipInfo.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(wifiAP);
    esp_netif_set_ip_info(wifiAP, &ipInfo);
    esp_netif_dhcps_start(wifiAP);

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = CONFIG_SSID,
            .password = "",
            .max_connection = 3},
    };

    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    ESP_ERROR_CHECK(esp_wifi_start());
}

static int s_retry_num = 0;
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < CONNECT_WIFI_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void connect_AP()
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
            .ssid = CONNECT_WIFI_SSID,
            .password = CONNECT_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 CONNECT_WIFI_SSID, CONNECT_WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 CONNECT_WIFI_SSID, CONNECT_WIFI_PASS);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

static void shiftOut(uint32_t display)
{
    gpio_set_level(CONFIG_RCLK, 0);
    for (int i = 31; i >= 0; i--)
    {
        gpio_set_level(CONFIG_SRCLK, 0);
        gpio_set_level(CONFIG_SER, display & (1 << i));
        gpio_set_level(CONFIG_SRCLK, 1);
    }
    gpio_set_level(CONFIG_RCLK, 1);
}

esp_err_t start_handler(httpd_req_t *req)
{
    counting = 1;

    // const char resp[] = "<a href=\"/stop\"><button>STOP</button></a>";
    // httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send(req, (const char *)start_html_start, start_html_end - start_html_start);
    return ESP_OK;
}

esp_err_t stop_handler(httpd_req_t *req)
{
    counting = false;
    // const char resp[] = "<a href=\"/start\"><button>START</button></a><a href=\"/reset\"><button>RESET</button</a>";
    // httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send(req, (const char *)stop_html_start, stop_html_end - stop_html_start);
    return ESP_OK;
}

esp_err_t reset_handler(httpd_req_t *req)
{
    timer = 300;
    up = 0;
    // const char resp[] = "<a href=\"/start\"><button>START</button></a>";
    // httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send(req, (const char *)reset_html_start, reset_html_end - reset_html_start);
    return ESP_OK;
}

void app_main(void)
{
    vTaskDelay(500 / portTICK_PERIOD_MS);

    init_nvs();

    connect_AP();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    httpd_start(&server, &config);

    httpd_uri_t uri_start = {
        .uri = "/start",
        .method = HTTP_GET,
        .handler = start_handler,
        .user_ctx = NULL};

    httpd_uri_t uri_stop = {
        .uri = "/stop",
        .method = HTTP_GET,
        .handler = stop_handler,
        .user_ctx = NULL};

    httpd_uri_t uri_reset = {
        .uri = "/reset",
        .method = HTTP_GET,
        .handler = reset_handler,
        .user_ctx = NULL};

    httpd_uri_t uri_home = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = stop_handler,
        .user_ctx = NULL};

    httpd_register_uri_handler(server, &uri_start);
    httpd_register_uri_handler(server, &uri_stop);
    httpd_register_uri_handler(server, &uri_reset);
    httpd_register_uri_handler(server, &uri_home);

    gpio_reset_pin(CONFIG_SRCLK);
    gpio_reset_pin(CONFIG_RCLK);
    gpio_reset_pin(CONFIG_SER);
    gpio_set_direction(CONFIG_SRCLK, GPIO_MODE_OUTPUT);
    gpio_set_direction(CONFIG_RCLK, GPIO_MODE_OUTPUT);
    gpio_set_direction(CONFIG_SER, GPIO_MODE_OUTPUT);

    int display = 0;

    int Digit1[] = {SEG10, SEG11, SEG12, SEG13, SEG14, SEG15, SEG16, SEG17, SEG18, SEG19};
    int Digit2[] = {SEG20, SEG21, SEG22, SEG23, SEG24, SEG25, SEG26, SEG27, SEG28, SEG29};
    int Digit3[] = {SEG30, SEG31, SEG32, SEG33, SEG34, SEG35, SEG36, SEG37, SEG38, SEG39};
    int Digit4[] = {SEG40, SEG41, SEG42, SEG43, SEG44, SEG45, SEG46, SEG47, SEG48, SEG49};

    shiftOut(SEG10 | SEG21 | SEG32 | SEG43);

    while (1)
    {
        int seconds = timer % 60;
        int minutes = timer / 60;
        display = Digit4[(seconds % 10)] | Digit3[(seconds % 100) / 10] | Digit2[minutes % 10] | Digit1[(minutes % 100) / 10];
        int dot = 0;
        if (counting && !up)
        {
            dot = BtmDP;
            timer -= 1;
            if (timer <= 0)
            {
                up = 1;
            }
        }
        else if (counting && up)
        {
            dot = TopDP;
            timer += 1;
        }
        else
        {
            dot = TopDP | BtmDP;
        }
        display = display | dot;
        shiftOut(display);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        if (counting)
        {
            display = display & ~dot;
            shiftOut(display);
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}
