/* Single Cell Battery Charger Monitor
 * 
 * Features:
 * - Battery voltage monitoring via ADC (GPIO1)
 * - Internal temperature sensor
 * - Automatic cell detection and unique ID generation
 * - Charging state detection (charging, full, idle, discharging)
 * - WiFi connectivity
 * - Data logging to InfluxDB every minute
 * - Web dashboard with real-time graph
 * - Web-based provisioning for first-time setup
 * 
 * Operation:
 * 1. Check if provisioned (config exists in NVS or .env file)
 *    - If not: Start AP mode + web server for configuration
 * 2. Connect to WiFi
 * 3. Start web server for dashboard
 * 4. Continuously monitor battery:
 *    - Read voltage and temperature every second
 *    - Detect cell connection/disconnection
 *    - Generate unique cell ID on new cell
 *    - Track charging state and time
 *    - Send data to InfluxDB every 60 seconds
 */

#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "config.h"
#include "wifi_manager.h"
#include "sensor.h"
#include "influxdb.h"
#include "provisioning.h"
#include "time_manager.h"
#include "webserver.h"

static const char *TAG = "main";

/* Update interval for InfluxDB (60 seconds) */
#define INFLUXDB_UPDATE_INTERVAL_SEC  60

/* Sensor read interval (1 second) */
#define SENSOR_READ_INTERVAL_MS       1000

/* Global sensor data for web dashboard access */
static sensor_data_t g_sensor_data;
static SemaphoreHandle_t g_sensor_mutex = NULL;

/* Get current sensor data (thread-safe) */
esp_err_t main_get_sensor_data(sensor_data_t *data)
{
    if (g_sensor_mutex == NULL || data == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(g_sensor_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(data, &g_sensor_data, sizeof(sensor_data_t));
        xSemaphoreGive(g_sensor_mutex);
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

void app_main(void)
{
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "Single Cell Charger Monitor Starting");
    ESP_LOGI(TAG, "====================================");

    /* Create mutex for sensor data access */
    g_sensor_mutex = xSemaphoreCreateMutex();
    if (g_sensor_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor mutex");
        esp_restart();
    }

    /* Initialize NVS */
    config_init_nvs();

    /* Load configuration */
    memset(&g_config, 0, sizeof(g_config));
    
    /* First try to load from .env file (development mode) */
    bool is_provisioned = config_load_from_env();
    
    /* If .env not found or incomplete, try NVS */
    if (!is_provisioned) {
        is_provisioned = config_load_from_nvs();
    }

    if (!is_provisioned) {
        ESP_LOGW(TAG, "Device not provisioned - entering setup mode");
        provisioning_start();
        /* Never returns */
    }

    ESP_LOGI(TAG, "Device is provisioned, starting normal operation");
    ESP_LOGI(TAG, "Device ID: %s", g_config.device_id);

    /* Initialize sensor (ADC + temperature) */
    if (sensor_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize sensor");
        esp_restart();
    }

    /* Connect to WiFi */
    if (wifi_connect() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
        esp_restart();
    }

    /* Initialize NTP and sync time */
    if (time_manager_init() != ESP_OK) {
        ESP_LOGW(TAG, "NTP sync failed, timestamps may be inaccurate");
    }

    /* Start web server for dashboard */
    if (webserver_start() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start web server");
    }

    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "System ready - monitoring battery");
    ESP_LOGI(TAG, "Dashboard: http://%s/", wifi_get_ip());
    ESP_LOGI(TAG, "====================================");

    /* Main monitoring loop */
    int64_t last_influx_send = 0;
    
    while (1) {
        /* Read sensor data */
        sensor_data_t sensor_data;
        if (sensor_read(&sensor_data) == ESP_OK) {
            /* Get current timestamp */
            sensor_data.timestamp_ns = time_manager_get_timestamp_ns();
            
            /* Update global sensor data (thread-safe) */
            if (xSemaphoreTake(g_sensor_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                memcpy(&g_sensor_data, &sensor_data, sizeof(sensor_data_t));
                xSemaphoreGive(g_sensor_mutex);
            }
            
            /* Check if new cell was just connected */
            if (sensor_is_new_cell()) {
                ESP_LOGI(TAG, "New cell detected: %s (%.2fV)", 
                         sensor_data.cell_id, sensor_data.battery_voltage);
                /* Send immediately on new cell */
                influxdb_send(&sensor_data);
                last_influx_send = esp_timer_get_time();
            }
            
            /* Send to InfluxDB every 60 seconds if cell is present */
            int64_t now = esp_timer_get_time();
            if (sensor_data.cell_present && 
                (now - last_influx_send) >= (INFLUXDB_UPDATE_INTERVAL_SEC * 1000000LL)) {
                
                ESP_LOGI(TAG, "Sending update: %.2fV (%.0f%%), %s, %lus", 
                         sensor_data.battery_voltage,
                         sensor_data.battery_percentage,
                         sensor_charge_state_str(sensor_data.charge_state),
                         sensor_data.charging_time_sec);
                
                if (influxdb_send(&sensor_data) == ESP_OK) {
                    last_influx_send = now;
                } else {
                    ESP_LOGW(TAG, "Failed to send to InfluxDB");
                }
            }
        }
        
        /* Wait before next reading */
        vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
    }
}
