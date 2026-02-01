#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* Charging state enumeration */
typedef enum {
    CHARGE_STATE_NO_CELL,      /* No cell detected (voltage < 2.5V) */
    CHARGE_STATE_CHARGING,     /* Cell is charging (voltage rising) */
    CHARGE_STATE_FULL,         /* Cell is fully charged (~4.2V stable) */
    CHARGE_STATE_DISCHARGING,  /* Cell is discharging (voltage dropping) */
    CHARGE_STATE_IDLE          /* Cell present but stable */
} charge_state_t;

/* Battery/charging data structure */
typedef struct {
    float battery_voltage;        /* V */
    float battery_percentage;     /* % */
    float internal_temp;          /* °C - ESP32 internal temperature */
    charge_state_t charge_state;  /* Current charging state */
    char cell_id[24];             /* Unique ID for current cell session */
    uint32_t charging_time_sec;   /* Seconds since cell was connected */
    int64_t timestamp_ns;         /* Timestamp in nanoseconds (UTC) */
    bool cell_present;            /* Whether a cell is detected */
} sensor_data_t;

/**
 * Initialize ADC for battery voltage and internal temperature sensor
 * @return ESP_OK on success
 */
esp_err_t sensor_init(void);

/**
 * Read current battery voltage and temperature
 * @param data Pointer to store sensor readings
 * @return ESP_OK on success
 */
esp_err_t sensor_read(sensor_data_t *data);

/**
 * Update charging state based on voltage history
 * Call this periodically to track state changes
 * @param data Current sensor data (will update charge_state)
 */
void sensor_update_charge_state(sensor_data_t *data);

/**
 * Check if a new cell was just connected
 * @return true if new cell detected since last check
 */
bool sensor_is_new_cell(void);

/**
 * Get the current cell ID (generated when cell is connected)
 * @return pointer to cell_id string
 */
const char* sensor_get_cell_id(void);

/**
 * Get charging time in seconds for current cell
 * @return seconds since cell was connected
 */
uint32_t sensor_get_charging_time(void);

/**
 * Get string representation of charge state
 * @param state The charge state
 * @return Human-readable string
 */
const char* sensor_charge_state_str(charge_state_t state);
