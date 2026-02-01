#include "provisioning.h"
#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "provisioning";

#define AP_SSID       "charger-setup"
#define AP_PASSWORD   ""
#define AP_MAX_CONN   1

static httpd_handle_t server = NULL;

/* Embedded HTML files */
extern const uint8_t provisioning_html_start[] asm("_binary_provisioning_html_start");
extern const uint8_t provisioning_html_end[]   asm("_binary_provisioning_html_end");
extern const uint8_t success_html_start[] asm("_binary_success_html_start");
extern const uint8_t success_html_end[]   asm("_binary_success_html_end");

/* URL decode helper function */
static void url_decode(char *dst, const char *src)
{
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16*a+b;
            src+=3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';
}

static esp_err_t provisioning_get_handler(httpd_req_t *req)
{
    /* Copy template to buffer */
    const size_t template_len = provisioning_html_end - provisioning_html_start;
    char *html = malloc(template_len + 1024); // Extra space for substitutions
    if (!html) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    
    /* Allocate temp buffer on heap instead of stack */
    char *temp = malloc(4096);
    if (!temp) {
        free(html);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    
    memcpy(html, provisioning_html_start, template_len);
    html[template_len] = '\0';
    
    /* Replace placeholders with actual values */
    char *p;
    
    /* Helper macro for replacements */
    #define REPLACE(placeholder, value) \
        if ((p = strstr(html, placeholder)) != NULL) { \
            snprintf(temp, 4096, "%.*s%s%s", \
                (int)(p - html), html, value, p + strlen(placeholder)); \
            strcpy(html, temp); \
        }
    
    REPLACE("{{WIFI_SSID}}", g_config.wifi_ssid);
    REPLACE("{{WIFI_PASSWORD}}", g_config.wifi_password);
    REPLACE("{{INFLUX_URL}}", g_config.influx_url);
    REPLACE("{{INFLUX_ORG}}", g_config.influx_org);
    REPLACE("{{INFLUX_BUCKET}}", g_config.influx_bucket);
    REPLACE("{{INFLUX_TOKEN}}", g_config.influx_token);
    REPLACE("{{DEVICE_ID}}", g_config.device_id);
    REPLACE("{{TIMEZONE}}", g_config.timezone);
    
    #undef REPLACE
    
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    free(temp);
    free(html);
    return ESP_OK;
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t provisioning_post_handler(httpd_req_t *req)
{
    char buf[1024];
    int ret, remaining = req->content_len;

    if (remaining > sizeof(buf) - 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too large");
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Request timeout");
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    ESP_LOGI(TAG, "Received form data: %s", buf);

    /* Parse form data */
    char value[128];
    char decoded[128];
    
    if (httpd_query_key_value(buf, "wifi_ssid", value, sizeof(value)) == ESP_OK) {
        url_decode(decoded, value);
        strncpy(g_config.wifi_ssid, decoded, sizeof(g_config.wifi_ssid) - 1);
        ESP_LOGI(TAG, "WiFi SSID: %s", g_config.wifi_ssid);
    }
    if (httpd_query_key_value(buf, "wifi_pass", value, sizeof(value)) == ESP_OK) {
        url_decode(decoded, value);
        strncpy(g_config.wifi_password, decoded, sizeof(g_config.wifi_password) - 1);
        ESP_LOGI(TAG, "WiFi password length: %d", strlen(g_config.wifi_password));
    }
    if (httpd_query_key_value(buf, "influx_url", value, sizeof(value)) == ESP_OK) {
        url_decode(decoded, value);
        strncpy(g_config.influx_url, decoded, sizeof(g_config.influx_url) - 1);
    }
    if (httpd_query_key_value(buf, "influx_org", value, sizeof(value)) == ESP_OK) {
        url_decode(decoded, value);
        strncpy(g_config.influx_org, decoded, sizeof(g_config.influx_org) - 1);
    }
    if (httpd_query_key_value(buf, "influx_bucket", value, sizeof(value)) == ESP_OK) {
        url_decode(decoded, value);
        strncpy(g_config.influx_bucket, decoded, sizeof(g_config.influx_bucket) - 1);
    }
    if (httpd_query_key_value(buf, "influx_token", value, sizeof(value)) == ESP_OK) {
        url_decode(decoded, value);
        strncpy(g_config.influx_token, decoded, sizeof(g_config.influx_token) - 1);
    }
    if (httpd_query_key_value(buf, "device_id", value, sizeof(value)) == ESP_OK) {
        url_decode(decoded, value);
        strncpy(g_config.device_id, decoded, sizeof(g_config.device_id) - 1);
    }
    if (httpd_query_key_value(buf, "timezone", value, sizeof(value)) == ESP_OK) {
        url_decode(decoded, value);
        strncpy(g_config.timezone, decoded, sizeof(g_config.timezone) - 1);
    } else {
        /* Default to UTC if not provided */
        strncpy(g_config.timezone, "UTC", sizeof(g_config.timezone) - 1);
    }

    /* Save configuration */
    g_config.provisioned = true;
    config_save_to_nvs();
    
    ESP_LOGI(TAG, "Configuration saved, rebooting in 3 seconds...");

    const size_t success_html_len = success_html_end - success_html_start;
    httpd_resp_send(req, (const char *)success_html_start, success_html_len);

    /* Schedule reboot */
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();

    return ESP_OK;
}

static httpd_handle_t start_server(void)
{
    httpd_config_t config_server = HTTPD_DEFAULT_CONFIG();
    config_server.stack_size = 8192;  /* Increase from default 4096 */
    config_server.lru_purge_enable = true;
    config_server.max_uri_handlers = 8;
    config_server.uri_match_fn = httpd_uri_match_wildcard;
    config_server.recv_wait_timeout = 10;
    config_server.send_wait_timeout = 10;

    ESP_LOGI(TAG, "Starting HTTP server");

    if (httpd_start(&server, &config_server) == ESP_OK) {
        const httpd_uri_t uri_get = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = provisioning_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_get);

        const httpd_uri_t uri_favicon = {
            .uri = "/favicon.ico",
            .method = HTTP_GET,
            .handler = favicon_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_favicon);

        const httpd_uri_t uri_post = {
            .uri = "/save",
            .method = HTTP_POST,
            .handler = provisioning_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_post);

        return server;
    }

    ESP_LOGE(TAG, "Failed to start HTTP server");
    return NULL;
}

void provisioning_start(void)
{
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "Starting provisioning mode");
    ESP_LOGI(TAG, "====================================");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = 1,
            .password = AP_PASSWORD,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "Access Point started");
    ESP_LOGI(TAG, "  SSID: %s", AP_SSID);
    ESP_LOGI(TAG, "  IP: 192.168.4.1");
    ESP_LOGI(TAG, "  Open http://192.168.4.1 to configure");
    ESP_LOGI(TAG, "====================================");

    start_server();

    /* Keep running in provisioning mode */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
