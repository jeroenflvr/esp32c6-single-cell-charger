#include "sensor.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/temperature_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_random.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "sensor";

/* Battery ADC Configuration - GPIO1 on ESP32-C6 */
#define BATTERY_ADC_CHANNEL  ADC_CHANNEL_1  /* GPIO1 */
#define BATTERY_ADC_ATTEN    ADC_ATTEN_DB_11
#define BATTERY_ADC_SAMPLES  16
#define VOLTAGE_DIVIDER      3.33 /* Voltage divider ratio: (R1+R2)/R2, e.g. 200k+100k = 3.0, adjust as needed */

/* Voltage smoothing - exponential moving average */
#define VOLTAGE_EMA_ALPHA    0.1f  /* Lower = more smoothing (0.1 = 10% new, 90% old) */

/* Cell detection threshold */
#define CELL_DETECT_VOLTAGE  2.5  /* Minimum voltage to consider a cell present */
#define CELL_FULL_VOLTAGE    4.15 /* Voltage considered fully charged */

/* Voltage change thresholds for state detection (in mV) */
#define VOLTAGE_RISING_THRESHOLD   20  /* mV increase over period to consider charging */
#define VOLTAGE_FALLING_THRESHOLD  20  /* mV decrease over period to consider discharging */
#define VOLTAGE_STABLE_COUNT       10  /* Number of stable readings to confirm state */
#define VOLTAGE_HISTORY_SIZE       10  /* Number of readings to compare for trend */

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static temperature_sensor_handle_t temp_sensor = NULL;

/* Cell tracking state */
static bool s_cell_was_present = false;
static bool s_new_cell_flag = false;
static char s_cell_id[24] = {0};
static int64_t s_cell_connect_time = 0;
static float s_smoothed_voltage = 0;
static float s_voltage_history[VOLTAGE_HISTORY_SIZE] = {0};
static int s_history_index = 0;
static bool s_history_filled = false;
static int s_stable_count = 0;
static charge_state_t s_last_charge_state = CHARGE_STATE_NO_CELL;

esp_err_t sensor_init(void)
{
    esp_err_t err;
    
    /* Initialize ADC for battery voltage measurement */
    const adc_oneshot_unit_init_cfg_t adc_init_config = {
        .unit_id = ADC_UNIT_1,
    };
    err = adc_oneshot_new_unit(&adc_init_config, &adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC init failed");
        return err;
    }
    
    const adc_oneshot_chan_cfg_t adc_chan_config = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(adc_handle, BATTERY_ADC_CHANNEL, &adc_chan_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed");
        return err;
    }
    
    /* Initialize ADC calibration */
    const adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration failed, readings will be uncalibrated");
        adc_cali_handle = NULL;
    }
    
    ESP_LOGI(TAG, "ADC initialized for battery voltage on GPIO1");
    
    /* Initialize internal temperature sensor */
    temperature_sensor_config_t temp_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    err = temperature_sensor_install(&temp_config, &temp_sensor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Temperature sensor install failed");
        return err;
    }
    
    err = temperature_sensor_enable(temp_sensor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Temperature sensor enable failed");
        return err;
    }
    
    ESP_LOGI(TAG, "Internal temperature sensor initialized");
    
    return ESP_OK;
}

static void generate_cell_id(void)
{
    /* Generate unique ID: timestamp + random suffix */
    uint32_t random_part = esp_random() & 0xFFFF;
    int64_t time_part = esp_timer_get_time() / 1000000; /* seconds */
    snprintf(s_cell_id, sizeof(s_cell_id), "CELL-%08lX%04X", 
             (unsigned long)(time_part & 0xFFFFFFFF), (unsigned int)random_part);
    ESP_LOGI(TAG, "Generated new cell ID: %s", s_cell_id);
}

esp_err_t sensor_read(sensor_data_t *data)
{
    esp_err_t err;
    
    /* Measure battery voltage with oversampling */
    int adc_samples[BATTERY_ADC_SAMPLES];
    for (int i = 0; i < BATTERY_ADC_SAMPLES; i++) {
        err = adc_oneshot_read(adc_handle, BATTERY_ADC_CHANNEL, &adc_samples[i]);
        if (err != ESP_OK) {
            adc_samples[i] = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    
    /* Sort samples and take median of middle 8 (discard 4 lowest and 4 highest) */
    for (int i = 0; i < BATTERY_ADC_SAMPLES - 1; i++) {
        for (int j = i + 1; j < BATTERY_ADC_SAMPLES; j++) {
            if (adc_samples[i] > adc_samples[j]) {
                int temp = adc_samples[i];
                adc_samples[i] = adc_samples[j];
                adc_samples[j] = temp;
            }
        }
    }
    
    /* Average the middle 8 samples */
    int adc_raw_sum = 0;
    for (int i = 4; i < BATTERY_ADC_SAMPLES - 4; i++) {
        adc_raw_sum += adc_samples[i];
    }
    int adc_raw_avg = adc_raw_sum / 8;
    
    /* Convert to voltage */
    int voltage_mv;
    if (adc_cali_handle) {
        adc_cali_raw_to_voltage(adc_cali_handle, adc_raw_avg, &voltage_mv);
    } else {
        /* Rough approximation if calibration failed */
        voltage_mv = (adc_raw_avg * 3100) / 4095;
    }
    
    /* Apply voltage divider ratio to get actual battery voltage */
    float raw_voltage = (voltage_mv * VOLTAGE_DIVIDER) / 1000.0f;
    
    /* Apply exponential moving average for smoothing */
    if (s_smoothed_voltage == 0) {
        s_smoothed_voltage = raw_voltage;  /* Initialize on first read */
    } else {
        s_smoothed_voltage = (VOLTAGE_EMA_ALPHA * raw_voltage) + ((1.0f - VOLTAGE_EMA_ALPHA) * s_smoothed_voltage);
    }
    data->battery_voltage = s_smoothed_voltage;
    
    /* Check if cell is present */
    data->cell_present = (data->battery_voltage >= CELL_DETECT_VOLTAGE);
    
    /* Handle cell connection/disconnection */
    if (data->cell_present && !s_cell_was_present) {
        /* New cell just connected */
        generate_cell_id();
        s_cell_connect_time = esp_timer_get_time();
        s_new_cell_flag = true;
        s_smoothed_voltage = data->battery_voltage;
        /* Reset voltage history */
        for (int i = 0; i < VOLTAGE_HISTORY_SIZE; i++) {
            s_voltage_history[i] = data->battery_voltage;
        }
        s_history_index = 0;
        s_history_filled = true;
        s_stable_count = 0;
        s_last_charge_state = CHARGE_STATE_IDLE;
        ESP_LOGI(TAG, "Cell connected! Voltage: %.2fV", data->battery_voltage);
    } else if (!data->cell_present && s_cell_was_present) {
        /* Cell was removed */
        ESP_LOGI(TAG, "Cell removed");
        s_cell_id[0] = '\0';
        s_cell_connect_time = 0;
        s_smoothed_voltage = 0;
        s_history_filled = false;
        s_last_charge_state = CHARGE_STATE_NO_CELL;
    }
    s_cell_was_present = data->cell_present;
    
    /* Copy cell ID and calculate charging time */
    strncpy(data->cell_id, s_cell_id, sizeof(data->cell_id) - 1);
    if (data->cell_present && s_cell_connect_time > 0) {
        data->charging_time_sec = (uint32_t)((esp_timer_get_time() - s_cell_connect_time) / 1000000);
    } else {
        data->charging_time_sec = 0;
    }
    
    /* Calculate battery percentage based on voltage curve */
    const float v = data->battery_voltage;
    float percentage;
    
    if (!data->cell_present) {
        percentage = 0.0f;
    } else if (v >= 4.10f) {
        percentage = 100.0f;
    } else if (v >= 4.0f) {
        percentage = 95.0f + (v - 4.0f) * 25.0f;
    } else if (v >= 3.9f) {
        percentage = 85.0f + (v - 3.9f) * 100.0f;
    } else if (v >= 3.8f) {
        percentage = 70.0f + (v - 3.8f) * 150.0f;
    } else if (v >= 3.7f) {
        percentage = 50.0f + (v - 3.7f) * 200.0f;
    } else if (v >= 3.6f) {
        percentage = 30.0f + (v - 3.6f) * 200.0f;
    } else if (v >= 3.5f) {
        percentage = 15.0f + (v - 3.5f) * 150.0f;
    } else if (v >= 3.3f) {
        percentage = 5.0f + (v - 3.3f) * 50.0f;
    } else if (v >= 3.2f) {
        percentage = 0.0f + (v - 3.2f) * 50.0f;
    } else {
        percentage = 0.0f;
    }
    data->battery_percentage = percentage;
    
    /* Read internal temperature */
    float temp = -999.0f;  /* Obvious invalid value for debugging */
    err = temperature_sensor_get_celsius(temp_sensor, &temp);
    if (err == ESP_OK) {
        data->internal_temp = temp;
        ESP_LOGD(TAG, "Temperature sensor read: %.2f°C", temp);
    } else {
        data->internal_temp = 0;
        ESP_LOGW(TAG, "Failed to read internal temperature: %s", esp_err_to_name(err));
    }
    
    /* Update charge state */
    sensor_update_charge_state(data);
    
    ESP_LOGI(TAG, "Battery: %.2fV (%.0f%%), Temp: %.1f°C, State: %s", 
             data->battery_voltage, data->battery_percentage, 
             data->internal_temp, sensor_charge_state_str(data->charge_state));
    
    return ESP_OK;
}

void sensor_update_charge_state(sensor_data_t *data)
{
    if (!data->cell_present) {
        data->charge_state = CHARGE_STATE_NO_CELL;
        s_last_charge_state = CHARGE_STATE_NO_CELL;
        return;
    }
    
    /* Store current voltage in history ring buffer */
    s_voltage_history[s_history_index] = data->battery_voltage;
    s_history_index = (s_history_index + 1) % VOLTAGE_HISTORY_SIZE;
    if (s_history_index == 0) {
        s_history_filled = true;
    }
    
    /* Need at least a full history before detecting trends */
    if (!s_history_filled) {
        data->charge_state = CHARGE_STATE_IDLE;
        s_last_charge_state = CHARGE_STATE_IDLE;
        return;
    }
    
    /* Calculate oldest voltage in history */
    int oldest_index = s_history_index;  /* This is where next write goes, so it's the oldest */
    float oldest_voltage = s_voltage_history[oldest_index];
    float voltage_diff_mv = (data->battery_voltage - oldest_voltage) * 1000.0f;
    
    /* Determine trend with hysteresis */
    charge_state_t detected_state = s_last_charge_state;
    
    if (voltage_diff_mv > VOLTAGE_RISING_THRESHOLD) {
        /* Voltage trending up - charging */
        if (s_last_charge_state != CHARGE_STATE_CHARGING) {
            s_stable_count++;
            if (s_stable_count >= 3) {
                detected_state = CHARGE_STATE_CHARGING;
                s_stable_count = 0;
            }
        } else {
            s_stable_count = 0;
        }
    } else if (voltage_diff_mv < -VOLTAGE_FALLING_THRESHOLD) {
        /* Voltage trending down - discharging */
        if (s_last_charge_state != CHARGE_STATE_DISCHARGING) {
            s_stable_count++;
            if (s_stable_count >= 3) {
                detected_state = CHARGE_STATE_DISCHARGING;
                s_stable_count = 0;
            }
        } else {
            s_stable_count = 0;
        }
    } else {
        /* Voltage stable */
        s_stable_count++;
        if (s_stable_count >= VOLTAGE_STABLE_COUNT) {
            if (data->battery_voltage >= CELL_FULL_VOLTAGE) {
                detected_state = CHARGE_STATE_FULL;
            } else {
                detected_state = CHARGE_STATE_IDLE;
            }
            s_stable_count = VOLTAGE_STABLE_COUNT; /* Cap to avoid overflow */
        }
    }
    
    s_last_charge_state = detected_state;
    data->charge_state = detected_state;
}

bool sensor_is_new_cell(void)
{
    bool result = s_new_cell_flag;
    s_new_cell_flag = false;
    return result;
}

const char* sensor_get_cell_id(void)
{
    return s_cell_id;
}

uint32_t sensor_get_charging_time(void)
{
    if (s_cell_connect_time > 0) {
        return (uint32_t)((esp_timer_get_time() - s_cell_connect_time) / 1000000);
    }
    return 0;
}

const char* sensor_charge_state_str(charge_state_t state)
{
    switch (state) {
        case CHARGE_STATE_NO_CELL:     return "No Cell";
        case CHARGE_STATE_CHARGING:    return "Charging";
        case CHARGE_STATE_FULL:        return "Full";
        case CHARGE_STATE_DISCHARGING: return "Discharging";
        case CHARGE_STATE_IDLE:        return "Idle";
        default:                       return "Unknown";
    }
}

