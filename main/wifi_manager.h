#pragma once

#include "esp_err.h"

/**
 * Initialize WiFi in station mode and connect
 * @return ESP_OK on successful connection
 */
esp_err_t wifi_connect(void);

/**
 * Get the device IP address as a string
 * @return Pointer to static IP string (e.g., "192.168.0.227")
 */
const char* wifi_get_ip(void);

/**
 * Disconnect and deinitialize WiFi to save power
 */
void wifi_disconnect(void);
