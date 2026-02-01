#include "time_manager.h"
#include "config.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "time_mgr";

/* NVS keys */
static const char NVS_NAMESPACE[] = "time_mgr";
static const char NVS_KEY_UPDATE_COUNT[] = "update_cnt";
static const char NVS_KEY_BUFFER_HEAD[] = "buf_head";
static const char NVS_KEY_BUFFER_TAIL[] = "buf_tail";
static const char NVS_KEY_BUFFER_COUNT[] = "buf_count";
static const char NVS_KEY_DATA_PREFIX[] = "data_";

#define MAX_PENDING_DATA 50
#define UPDATE_SYNC_INTERVAL 20
#define SNTP_SERVER "pool.ntp.org"
#define SNTP_TIMEOUT_MS 15000

static bool sntp_initialized = false;

/* Convert timezone name to POSIX TZ string */
static const char* get_posix_tz(const char *tz_name)
{
    /* Common timezone mappings to POSIX format */
    if (strcmp(tz_name, "Europe/Brussels") == 0 || strcmp(tz_name, "Europe/Paris") == 0 ||
        strcmp(tz_name, "Europe/Amsterdam") == 0 || strcmp(tz_name, "Europe/Berlin") == 0) {
        return "CET-1CEST,M3.5.0,M10.5.0/3";  /* Central European Time */
    } else if (strcmp(tz_name, "Europe/London") == 0) {
        return "GMT0BST,M3.5.0/1,M10.5.0";  /* British Time */
    } else if (strcmp(tz_name, "America/New_York") == 0) {
        return "EST5EDT,M3.2.0,M11.1.0";  /* Eastern Time */
    } else if (strcmp(tz_name, "America/Los_Angeles") == 0) {
        return "PST8PDT,M3.2.0,M11.1.0";  /* Pacific Time */
    } else if (strcmp(tz_name, "UTC") == 0) {
        return "UTC0";
    }
    
    /* If it already looks like POSIX format or unknown, return as-is */
    return tz_name;
}

/* SNTP sync callback */
static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "NTP time synchronized");
}

esp_err_t time_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    
    /* Initialize SNTP */
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, SNTP_SERVER);
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
    sntp_initialized = true;
    
    /* Wait for time to be set */
    int retry = 0;
    const int max_retry = SNTP_TIMEOUT_MS / 500;  /* Check every 500ms */
    
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < max_retry) {
        ESP_LOGI(TAG, "Waiting for NTP sync... (%d/%d)", retry, max_retry);
        vTaskDelay(pdMS_TO_TICKS(500));  /* Wait 500ms between checks */
    }
    
    if (retry >= max_retry) {
        ESP_LOGW(TAG, "NTP sync timeout, using system time");
        return ESP_ERR_TIMEOUT;
    }
    
    /* Set timezone - convert to POSIX format if needed */
    const char *posix_tz = get_posix_tz(g_config.timezone);
    setenv("TZ", posix_tz, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to: %s (POSIX: %s)", g_config.timezone, posix_tz);
    
    /* Log synchronized time */
    time_t now;
    char strftime_buf[64];
    struct tm timeinfo;
    
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "Current local time: %s", strftime_buf);
    
    return ESP_OK;
}

esp_err_t time_manager_check_sync(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    uint32_t update_count = 0;
    
    /* Open NVS */
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    /* Get current update count */
    nvs_get_u32(nvs_handle, NVS_KEY_UPDATE_COUNT, &update_count);
    
    /* Increment counter */
    update_count++;
    ESP_LOGI(TAG, "Update count: %lu", update_count);
    
    /* Check if we need to sync */
    if (update_count >= UPDATE_SYNC_INTERVAL) {
        ESP_LOGI(TAG, "Reached %d updates, re-syncing NTP", UPDATE_SYNC_INTERVAL);
        
        /* Reset counter */
        update_count = 0;
        
        /* Re-sync time if SNTP is initialized */
        if (sntp_initialized) {
            esp_sntp_stop();
            esp_sntp_init();
            
            /* Wait for sync */
            int retry = 0;
            const int max_retry = SNTP_TIMEOUT_MS / 500;  /* Check every 500ms */
            
            while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < max_retry) {
                vTaskDelay(pdMS_TO_TICKS(500));  /* Wait 500ms between checks */
            }
            
            if (retry >= max_retry) {
                ESP_LOGW(TAG, "NTP re-sync timeout");
            } else {
                ESP_LOGI(TAG, "NTP re-synchronized successfully");
            }
        }
    }
    
    /* Save updated counter */
    err = nvs_set_u32(nvs_handle, NVS_KEY_UPDATE_COUNT, update_count);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    
    nvs_close(nvs_handle);
    return err;
}

int64_t time_manager_get_timestamp_ns(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    /* Convert to nanoseconds */
    const int64_t timestamp_ns = (int64_t)tv.tv_sec * 1000000000LL + (int64_t)tv.tv_usec * 1000LL;
    
    return timestamp_ns;
}

esp_err_t time_manager_store_failed_data(const sensor_data_t *data)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    uint32_t head = 0;
    uint32_t tail = 0;
    uint32_t count = 0;
    
    /* Open NVS */
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    /* Get buffer pointers */
    nvs_get_u32(nvs_handle, NVS_KEY_BUFFER_HEAD, &head);
    nvs_get_u32(nvs_handle, NVS_KEY_BUFFER_TAIL, &tail);
    nvs_get_u32(nvs_handle, NVS_KEY_BUFFER_COUNT, &count);
    
    /* Check if buffer is full */
    if (count >= MAX_PENDING_DATA) {
        ESP_LOGW(TAG, "Pending data buffer full, overwriting oldest entry");
        /* Move tail forward (circular buffer) */
        tail = (tail + 1) % MAX_PENDING_DATA;
    } else {
        count++;
    }
    
    /* Store data at head */
    char key[16];
    snprintf(key, sizeof(key), "%s%lu", NVS_KEY_DATA_PREFIX, head);
    
    err = nvs_set_blob(nvs_handle, key, data, sizeof(sensor_data_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store data: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    /* Move head forward */
    head = (head + 1) % MAX_PENDING_DATA;
    
    /* Update pointers */
    nvs_set_u32(nvs_handle, NVS_KEY_BUFFER_HEAD, head);
    nvs_set_u32(nvs_handle, NVS_KEY_BUFFER_TAIL, tail);
    nvs_set_u32(nvs_handle, NVS_KEY_BUFFER_COUNT, count);
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "Stored failed data, pending count: %lu", count);
    
    return err;
}

uint32_t time_manager_get_pending_count(void)
{
    nvs_handle_t nvs_handle;
    uint32_t count = 0;
    
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle) == ESP_OK) {
        nvs_get_u32(nvs_handle, NVS_KEY_BUFFER_COUNT, &count);
        nvs_close(nvs_handle);
    }
    
    return count;
}

esp_err_t time_manager_get_next_pending(sensor_data_t *data)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    uint32_t tail = 0;
    uint32_t count = 0;
    
    /* Open NVS */
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    
    /* Get buffer pointers */
    nvs_get_u32(nvs_handle, NVS_KEY_BUFFER_TAIL, &tail);
    nvs_get_u32(nvs_handle, NVS_KEY_BUFFER_COUNT, &count);
    
    /* Check if buffer is empty */
    if (count == 0) {
        nvs_close(nvs_handle);
        return ESP_ERR_NOT_FOUND;
    }
    
    /* Get data at tail */
    char key[16];
    snprintf(key, sizeof(key), "%s%lu", NVS_KEY_DATA_PREFIX, tail);
    
    size_t size = sizeof(sensor_data_t);
    err = nvs_get_blob(nvs_handle, key, data, &size);
    
    nvs_close(nvs_handle);
    
    return err;
}

esp_err_t time_manager_remove_pending(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    uint32_t tail = 0;
    uint32_t count = 0;
    
    /* Open NVS */
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    
    /* Get buffer pointers */
    nvs_get_u32(nvs_handle, NVS_KEY_BUFFER_TAIL, &tail);
    nvs_get_u32(nvs_handle, NVS_KEY_BUFFER_COUNT, &count);
    
    /* Check if buffer is empty */
    if (count == 0) {
        nvs_close(nvs_handle);
        return ESP_ERR_NOT_FOUND;
    }
    
    /* Move tail forward */
    tail = (tail + 1) % MAX_PENDING_DATA;
    count--;
    
    /* Update pointers */
    nvs_set_u32(nvs_handle, NVS_KEY_BUFFER_TAIL, tail);
    nvs_set_u32(nvs_handle, NVS_KEY_BUFFER_COUNT, count);
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "Removed pending data, remaining: %lu", count);
    
    return err;
}

esp_err_t time_manager_clear_pending(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    /* Open NVS */
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    
    /* Reset all pointers */
    nvs_set_u32(nvs_handle, NVS_KEY_BUFFER_HEAD, 0);
    nvs_set_u32(nvs_handle, NVS_KEY_BUFFER_TAIL, 0);
    nvs_set_u32(nvs_handle, NVS_KEY_BUFFER_COUNT, 0);
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "Cleared all pending data");
    
    return err;
}
