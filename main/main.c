/* 
 * Probe HTTP Server
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

// Values from menuconfig
#define AP_WIFI_SSID CONFIG_WIFI_SSID
#define AP_WIFI_PASS CONFIG_WIFI_PASSWORD

// Manual config
#define AP_WIFI_CHANNEL 1
#define SENSOR_DATA_PIN 4
#define LED_PIN 14

static const char *TAG = "probe";
static float temperature = 0;
static bool wifi_connected = false;

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
    sprintf(resp_str, "Temp = <b>%3.2f</b> *C", temperature);
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
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root_uri);
        
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        stop_webserver(*server);
        *server = NULL;
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
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
        if (wifi_connected) {
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

void connected_handler(void)
{
    ESP_LOGI(TAG, "Connected from handler");
}

void app_main(void)
{
    static httpd_handle_t server = NULL;

    xTaskCreate(led_thread, "LedThread", 1024 * 6, NULL, 2, NULL);
    xTaskCreate(temperature_poll, "TemperaturePollingThread", 1024 * 6, NULL, 2, NULL);

    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());

    wifi_connected = wifi_init_station(
        AP_WIFI_SSID,
        AP_WIFI_PASS
    );

    ESP_LOGI(TAG, "wifi_init_sta finished. Connected = %d", wifi_connected);

    if (wifi_connected) {
        /* Start the server for the first time */
        server = start_webserver();
    }
}
