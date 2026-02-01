#pragma once

#include "esp_err.h"
#include "sensor.h"

/**
 * Send battery charging data to InfluxDB
 * @param data Sensor/battery readings to send
 * @return ESP_OK on successful transmission (HTTP 2xx)
 */
esp_err_t influxdb_send(const sensor_data_t *data);
