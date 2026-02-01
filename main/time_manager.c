#include "time_manager.h"
#include "config.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "time_mgr";

#define SNTP_SERVER "pool.ntp.org"
#define SNTP_TIMEOUT_MS 15000

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

int64_t time_manager_get_timestamp_ns(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    /* Convert to nanoseconds */
    const int64_t timestamp_ns = (int64_t)tv.tv_sec * 1000000000LL + (int64_t)tv.tv_usec * 1000LL;
    
    return timestamp_ns;
}
