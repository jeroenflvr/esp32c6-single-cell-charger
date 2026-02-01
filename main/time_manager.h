#pragma once

#include "esp_err.h"
#include <stdint.h>

/**
 * Initialize SNTP and synchronize time
 * @return ESP_OK on success
 */
esp_err_t time_manager_init(void);

/**
 * Get current timestamp in nanoseconds
 * @return timestamp in nanoseconds (UTC)
 */
int64_t time_manager_get_timestamp_ns(void);
