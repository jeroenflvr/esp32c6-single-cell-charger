#include "config.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "config";

/* NVS Configuration Keys */
static const char NVS_NAMESPACE[] = "thermo_cfg";
static const char NVS_KEY_PROVISIONED[] = "provisioned";
static const char NVS_KEY_WIFI_SSID[] = "wifi_ssid";
static const char NVS_KEY_WIFI_PASS[] = "wifi_pass";
static const char NVS_KEY_INFLUX_URL[] = "influx_url";
static const char NVS_KEY_INFLUX_ORG[] = "influx_org";
static const char NVS_KEY_INFLUX_BUCKET[] = "influx_bucket";
static const char NVS_KEY_INFLUX_TOKEN[] = "influx_token";
static const char NVS_KEY_DEVICE_ID[] = "device_id";
static const char NVS_KEY_TIMEZONE[] = "timezone";

config_t g_config;

void config_init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");
}

bool config_load_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace not found, device not provisioned");
        return false;
    }

    /* Check if provisioned */
    uint8_t provisioned = 0;
    err = nvs_get_u8(nvs_handle, NVS_KEY_PROVISIONED, &provisioned);
    if (err != ESP_OK || provisioned == 0) {
        ESP_LOGW(TAG, "Device not provisioned");
        nvs_close(nvs_handle);
        return false;
    }

    g_config.provisioned = true;

    /* Load all configuration values */
    size_t len;
    
    len = sizeof(g_config.wifi_ssid);
    nvs_get_str(nvs_handle, NVS_KEY_WIFI_SSID, g_config.wifi_ssid, &len);
    
    len = sizeof(g_config.wifi_password);
    nvs_get_str(nvs_handle, NVS_KEY_WIFI_PASS, g_config.wifi_password, &len);
    
    len = sizeof(g_config.influx_url);
    nvs_get_str(nvs_handle, NVS_KEY_INFLUX_URL, g_config.influx_url, &len);
    
    len = sizeof(g_config.influx_org);
    nvs_get_str(nvs_handle, NVS_KEY_INFLUX_ORG, g_config.influx_org, &len);
    
    len = sizeof(g_config.influx_bucket);
    nvs_get_str(nvs_handle, NVS_KEY_INFLUX_BUCKET, g_config.influx_bucket, &len);
    
    len = sizeof(g_config.influx_token);
    nvs_get_str(nvs_handle, NVS_KEY_INFLUX_TOKEN, g_config.influx_token, &len);
    
    len = sizeof(g_config.device_id);
    nvs_get_str(nvs_handle, NVS_KEY_DEVICE_ID, g_config.device_id, &len);
    
    len = sizeof(g_config.timezone);
    if (nvs_get_str(nvs_handle, NVS_KEY_TIMEZONE, g_config.timezone, &len) != ESP_OK) {
        /* Default to UTC if not set */
        strncpy(g_config.timezone, "UTC", sizeof(g_config.timezone) - 1);
    }

    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "Configuration loaded from NVS");
    ESP_LOGI(TAG, "  WiFi SSID: %s", g_config.wifi_ssid);
    ESP_LOGI(TAG, "  Device ID: %s", g_config.device_id);
    
    return true;
}

esp_err_t config_save_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing");
        return err;
    }

    nvs_set_u8(nvs_handle, NVS_KEY_PROVISIONED, 1);
    nvs_set_str(nvs_handle, NVS_KEY_WIFI_SSID, g_config.wifi_ssid);
    nvs_set_str(nvs_handle, NVS_KEY_WIFI_PASS, g_config.wifi_password);
    nvs_set_str(nvs_handle, NVS_KEY_INFLUX_URL, g_config.influx_url);
    nvs_set_str(nvs_handle, NVS_KEY_INFLUX_ORG, g_config.influx_org);
    nvs_set_str(nvs_handle, NVS_KEY_INFLUX_BUCKET, g_config.influx_bucket);
    nvs_set_str(nvs_handle, NVS_KEY_INFLUX_TOKEN, g_config.influx_token);
    nvs_set_str(nvs_handle, NVS_KEY_DEVICE_ID, g_config.device_id);
    nvs_set_str(nvs_handle, NVS_KEY_TIMEZONE, g_config.timezone);

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Configuration saved to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to commit NVS");
    }

    return err;
}

bool config_load_from_env(void)
{
    ESP_LOGI(TAG, "Checking for .env file...");
    
    /* Mount SPIFFS */
    const esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = false
    };
    
    const esp_err_t ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGD(TAG, "SPIFFS not formatted or partition not found");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGD(TAG, "SPIFFS partition not found");
        } else {
            ESP_LOGD(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        }
        return false;
    }
    
    /* Try to open .env file */
    FILE *const f = fopen("/spiffs/.env", "r");
    if (f == NULL) {
        ESP_LOGI(TAG, ".env file not found, will use NVS or provisioning");
        esp_vfs_spiffs_unregister("storage");
        return false;
    }
    
    ESP_LOGI(TAG, "Loading configuration from .env file");
    
    /* Parse .env file */
    char line[256];
    bool has_wifi_ssid = false;
    bool has_wifi_pass = false;
    bool has_influx_url = false;
    bool has_influx_org = false;
    bool has_influx_bucket = false;
    bool has_influx_token = false;
    bool has_device_id = false;
    
    /* Set defaults */
    strncpy(g_config.timezone, "UTC", sizeof(g_config.timezone) - 1);
    
    while (fgets(line, sizeof(line), f) != NULL) {
        /* Remove newline */
        line[strcspn(line, "\r\n")] = 0;
        
        /* Skip empty lines and comments */
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        
        /* Parse KEY=VALUE */
        char *equals = strchr(line, '=');
        if (equals == NULL) {
            continue;
        }
        
        *equals = '\0';
        char *key = line;
        char *value = equals + 1;
        
        /* Remove quotes if present */
        if (value[0] == '"' || value[0] == '\'') {
            value++;
            size_t len = strlen(value);
            if (len > 0 && (value[len-1] == '"' || value[len-1] == '\'')) {
                value[len-1] = '\0';
            }
        }
        
        // Map to config structure
        if (strcmp(key, "WIFI_SSID") == 0) {
            strncpy(g_config.wifi_ssid, value, sizeof(g_config.wifi_ssid) - 1);
            has_wifi_ssid = true;
        } else if (strcmp(key, "WIFI_PASSWORD") == 0) {
            strncpy(g_config.wifi_password, value, sizeof(g_config.wifi_password) - 1);
            has_wifi_pass = true;
        } else if (strcmp(key, "INFLUXDB_URL") == 0) {
            strncpy(g_config.influx_url, value, sizeof(g_config.influx_url) - 1);
            has_influx_url = true;
        } else if (strcmp(key, "INFLUXDB_ORG") == 0) {
            strncpy(g_config.influx_org, value, sizeof(g_config.influx_org) - 1);
            has_influx_org = true;
        } else if (strcmp(key, "INFLUXDB_BUCKET") == 0) {
            strncpy(g_config.influx_bucket, value, sizeof(g_config.influx_bucket) - 1);
            has_influx_bucket = true;
        } else if (strcmp(key, "INFLUXDB_TOKEN") == 0) {
            strncpy(g_config.influx_token, value, sizeof(g_config.influx_token) - 1);
            has_influx_token = true;
        } else if (strcmp(key, "DEVICE_ID") == 0) {
            strncpy(g_config.device_id, value, sizeof(g_config.device_id) - 1);
            has_device_id = true;
        } else if (strcmp(key, "TIMEZONE") == 0) {
            strncpy(g_config.timezone, value, sizeof(g_config.timezone) - 1);
        }
    }
    
    fclose(f);
    esp_vfs_spiffs_unregister("storage");
    
    /* Validate all required fields are present */
    const bool config_complete = has_wifi_ssid && has_wifi_pass && has_influx_url && 
                                  has_influx_org && has_influx_bucket && has_influx_token && has_device_id;
    
    if (config_complete) {
        g_config.provisioned = true;
        ESP_LOGI(TAG, "Configuration loaded from .env file");
        ESP_LOGI(TAG, "  WiFi SSID: %s", g_config.wifi_ssid);
        ESP_LOGI(TAG, "  Device ID: %s", g_config.device_id);
        return true;
    } else {
        ESP_LOGW(TAG, ".env file incomplete, missing required fields");
        return false;
    }
}
