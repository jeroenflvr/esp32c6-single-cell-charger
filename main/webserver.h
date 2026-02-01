#pragma once

#include "esp_err.h"

/**
 * Start the web server for the dashboard
 * @return ESP_OK on success
 */
esp_err_t webserver_start(void);

/**
 * Stop the web server
 */
void webserver_stop(void);
