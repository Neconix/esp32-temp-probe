/* 
 * Temperature sensor HTTP Server
*/

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "driver/gpio.h"
#include "esp_http_server.h"
#include "wifi.h"
#include "ds18b20.h"
#include "http_content.h"

// Values from menuconfig
#define AP_WIFI_SSID CONFIG_WIFI_SSID
#define AP_WIFI_PASS CONFIG_WIFI_PASSWORD

// Manual config
#define AP_WIFI_CHANNEL 1
#define SENSOR_DATA_PIN 4
#define LED_PIN 14

static const char *TAG = "probe";
static float temperature = 0;
static bool connected = false;
static httpd_handle_t server = NULL;


/* An HTTP GET handler */
static esp_err_t root_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Host: %s", buf);
        }
        free(buf);
    }

    /* Send response with custom headers and body set as the
     * string passed in user context*/
    // const char* resp_str = (const char*) req->user_ctx;
    static char resp_str[150];
    sprintf(resp_str, HTML_PAGE, temperature);
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}

static const httpd_uri_t root_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_handler,
    .user_ctx  = ""
};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // Start the httpd server
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root_uri);
        
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

/**
 * @brief Polling temperature sensor
 * 
 * @param pvParameters 
 */
void temperature_poll(void *pvParameters) 
{
    sensor_t sensor;
    sensor_config_t sensorConfig = {
        .dataPin = SENSOR_DATA_PIN
    };

    bool presense = false;

    for (;;)
    {
        // ESP_LOGI(TAG, "Sensor online = %d", presense);

        if (!presense) {
            presense = sensorInit(&sensor, &sensorConfig);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        sensorSetConfig(&sensor, R_10_BIT, 0, 0);

        sensorGetTempSync(&sensor, &temperature);
        // ESP_LOGI(TAG, "tempC = %.4f", temperature);

        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
}

void led_thread(void *pvParameters) 
{
    gpio_config_t pinConfig = {
        .pin_bit_mask = BIT64(LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&pinConfig);

    for (;;)
    {
        if (connected) {
            gpio_set_level(LED_PIN, 1);
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            continue;
        }

        gpio_set_level(LED_PIN, 1);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        gpio_set_level(LED_PIN, 0);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void wifi_connected(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
    connected = true;
    ESP_LOGI(TAG, "Starting server on %d.%d.%d.%d:80", IP2STR(&event->ip_info.ip));
    server = start_webserver();

}

void wifi_disconnected(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    ESP_LOGI(TAG, "Disconnected from handler");

    connected = false;

    if (server == NULL) {
        return;
    }

    ESP_ERROR_CHECK(
        httpd_stop(server)
    );
}

void app_main(void)
{
    xTaskCreate(led_thread, "LedThread", 1024 * 6, NULL, 2, NULL);
    xTaskCreate(temperature_poll, "TemperaturePollingThread", 1024 * 6, NULL, 2, NULL);

    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());

    wifi_init_station(
        AP_WIFI_SSID,
        AP_WIFI_PASS,
        wifi_connected,
        wifi_disconnected
    );
}
