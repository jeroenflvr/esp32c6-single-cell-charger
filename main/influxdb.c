#include "influxdb.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"

static const char *TAG = "influxdb";

esp_err_t influxdb_send(const sensor_data_t *data)
{
    char url[384];
    char post_data[512];
    char auth_header[256];

    /* Build InfluxDB URL with nanosecond precision */
    snprintf(url, sizeof(url), "%s/api/v2/write?org=%s&bucket=%s&precision=ns",
             g_config.influx_url, g_config.influx_org, g_config.influx_bucket);

    /* Build Line Protocol data for battery charging
     * Measurement: battery_charging
     * Tags: device (charger name), cell_id (unique per cell session)
     * Fields: voltage, percentage, temp, charge_state, charging_time
     */
    const char *state_str = sensor_charge_state_str(data->charge_state);
    
    snprintf(post_data, sizeof(post_data),
             "battery_charging,device=%s,cell_id=%s "
             "voltage=%.3f,percentage=%.1f,temp=%.1f,charge_state=\"%s\","
             "charging_time_sec=%lui,cell_present=%s "
             "%lld",
             g_config.device_id,
             data->cell_id[0] ? data->cell_id : "none",
             data->battery_voltage,
             data->battery_percentage,
             data->internal_temp,
             state_str,
             data->charging_time_sec,
             data->cell_present ? "true" : "false",
             data->timestamp_ns);

    /* Build authorization header */
    snprintf(auth_header, sizeof(auth_header), "Token %s", g_config.influx_token);

    ESP_LOGI(TAG, "Sending to InfluxDB: %s", post_data);

    const esp_http_client_config_t http_config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "text/plain");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "InfluxDB HTTP Status = %d", status);
        
        if (status >= 200 && status < 300) {
            ESP_LOGI(TAG, "Data sent to InfluxDB successfully");
            err = ESP_OK;
        } else {
            ESP_LOGE(TAG, "InfluxDB returned error status: %d", status);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}
