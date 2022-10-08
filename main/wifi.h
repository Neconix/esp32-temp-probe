/**
 * @brief Wi-Fi client module
 */

#include "freertos/event_groups.h"

void wifi_init_station(
    char* wifi_ssid, 
    char* wifi_password, 
    esp_event_handler_t connected_callback, 
    esp_event_handler_t disconnected_callback
);