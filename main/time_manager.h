#pragma once

#include "esp_err.h"
#include "sensor.h"
#include <stdint.h>

/**
 * Initialize SNTP and synchronize time
 * @return ESP_OK on success
 */
esp_err_t time_manager_init(void);

/**
 * Check if NTP sync is needed and perform sync if required
 * Syncs at boot and every 20th update
 * @return ESP_OK on success
 */
esp_err_t time_manager_check_sync(void);

/**
 * Get current timestamp in nanoseconds
 * @return timestamp in nanoseconds (UTC)
 */
int64_t time_manager_get_timestamp_ns(void);

/**
 * Store failed sensor data for retry
 * Uses circular buffer with max 50 entries
 * @param data Sensor data to store
 * @return ESP_OK on success
 */
esp_err_t time_manager_store_failed_data(const sensor_data_t *data);

/**
 * Get count of pending failed data entries
 * @return number of pending entries
 */
uint32_t time_manager_get_pending_count(void);

/**
 * Get next pending data entry
 * @param data Pointer to store retrieved data
 * @return ESP_OK if data available, ESP_ERR_NOT_FOUND if empty
 */
esp_err_t time_manager_get_next_pending(sensor_data_t *data);

/**
 * Remove oldest pending data entry (after successful send)
 * @return ESP_OK on success
 */
esp_err_t time_manager_remove_pending(void);

/**
 * Clear all pending data
 * @return ESP_OK on success
 */
esp_err_t time_manager_clear_pending(void);
