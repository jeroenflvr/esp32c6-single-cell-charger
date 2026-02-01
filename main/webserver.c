#include "webserver.h"
#include "sensor.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"

static const char *TAG = "webserver";

static httpd_handle_t server = NULL;

/* External function to get sensor data from main */
extern esp_err_t main_get_sensor_data(sensor_data_t *data);

/* Embedded HTML files */
extern const uint8_t dashboard_html_start[] asm("_binary_dashboard_html_start");
extern const uint8_t dashboard_html_end[]   asm("_binary_dashboard_html_end");

/* Dashboard page handler */
static esp_err_t dashboard_get_handler(httpd_req_t *req)
{
    const size_t html_len = dashboard_html_end - dashboard_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)dashboard_html_start, html_len);
    return ESP_OK;
}

/* API endpoint for current sensor data */
static esp_err_t api_data_handler(httpd_req_t *req)
{
    sensor_data_t data;
    
    if (main_get_sensor_data(&data) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get sensor data");
        return ESP_FAIL;
    }
    
    /* Build JSON response */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "voltage", data.battery_voltage);
    cJSON_AddNumberToObject(root, "percentage", data.battery_percentage);
    cJSON_AddNumberToObject(root, "temperature", data.internal_temp);
    cJSON_AddStringToObject(root, "charge_state", sensor_charge_state_str(data.charge_state));
    cJSON_AddNumberToObject(root, "charge_state_code", (int)data.charge_state);
    cJSON_AddStringToObject(root, "cell_id", data.cell_id[0] ? data.cell_id : "");
    cJSON_AddNumberToObject(root, "charging_time_sec", data.charging_time_sec);
    cJSON_AddBoolToObject(root, "cell_present", data.cell_present);
    cJSON_AddStringToObject(root, "device_id", g_config.device_id);
    
    /* Format charging time as string */
    char time_str[32];
    uint32_t hours = data.charging_time_sec / 3600;
    uint32_t minutes = (data.charging_time_sec % 3600) / 60;
    uint32_t seconds = data.charging_time_sec % 60;
    snprintf(time_str, sizeof(time_str), "%02lu:%02lu:%02lu", hours, minutes, seconds);
    cJSON_AddStringToObject(root, "charging_time_str", time_str);
    
    char *json_str = cJSON_PrintUnformatted(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    cJSON_free(json_str);
    cJSON_Delete(root);
    
    return ESP_OK;
}

/* Favicon handler */
static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t webserver_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;
    
    ESP_LOGI(TAG, "Starting web server on port %d", config.server_port);
    
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }
    
    /* Register URI handlers */
    const httpd_uri_t uri_dashboard = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = dashboard_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_dashboard);
    
    const httpd_uri_t uri_api_data = {
        .uri = "/api/data",
        .method = HTTP_GET,
        .handler = api_data_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_api_data);
    
    const httpd_uri_t uri_favicon = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_favicon);
    
    ESP_LOGI(TAG, "Web server started successfully");
    return ESP_OK;
}

void webserver_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
}
