#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui.h"

static const char *TAG = "net";

static volatile bool s_connected;
static bool s_has_ssid;
static char s_ip[16] = "";

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_has_ssid) {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_ip[0] = 0;
        if (s_has_ssid) {
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        ESP_LOGI(TAG, "연결됨 %s", s_ip);
        s_connected = true;
    }
}

void net_start(const char *ssid, const char *pass)
{
    setenv("TZ", "KST-9", 1);
    tzset();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    /* 와이파이 설정이 없어도 켜 둡니다 (보드 설정 화면에서 찾기) */
    s_has_ssid = ssid[0] != 0;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL));

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
    if (!s_has_ssid) {
        ESP_LOGW(TAG, "와이파이 설정 없음");
        return;
    }

    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp);
}

/* ---- 와이파이 찾기 (보드 설정 화면) ---- */

static void scan_task(void *arg)
{
    wifi_scan_t *list = calloc(1, sizeof(*list));
    wifi_scan_config_t sc = { .show_hidden = false };
    if (list && esp_wifi_scan_start(&sc, true) == ESP_OK) {
        uint16_t n = WIFI_SCAN_MAX;
        wifi_ap_record_t *rec = calloc(n, sizeof(*rec));
        if (rec && esp_wifi_scan_get_ap_records(&n, rec) == ESP_OK) {
            for (int i = 0; i < n; i++) {
                if (!rec[i].ssid[0]) {
                    continue;
                }
                bool dup = false;
                for (int j = 0; j < list->count; j++) {
                    dup |= !strcmp(list->ap[j].ssid, (char *)rec[i].ssid);
                }
                if (dup) {
                    continue;
                }
                strlcpy(list->ap[list->count].ssid, (char *)rec[i].ssid, sizeof(list->ap[0].ssid));
                list->ap[list->count].rssi = rec[i].rssi;
                list->ap[list->count].open = rec[i].authmode == WIFI_AUTH_OPEN;
                list->count++;
            }
        }
        free(rec);
    }
    ui_wifi_scanned(list);
    free(list);
    vTaskDelete(NULL);
}

void net_scan(void)
{
    xTaskCreate(scan_task, "scan", 4096, NULL, 3, NULL);
}

bool net_connected(void)
{
    return s_connected;
}

bool net_time_ok(void)
{
    return time(NULL) > 1735689600; /* 2025-01-01 이후면 시간이 맞춰진 것 */
}

const char *net_ip(void)
{
    return s_ip;
}

/* ---- HTTP ---- */

typedef void (*chunk_cb_t)(const char *buf, int len, void *ctx);

static esp_err_t http_stream(const char *url, chunk_cb_t cb, void *ctx)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
        .user_agent = "oneulpan/" "0.1",
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_FAIL;
    }

    esp_err_t err = ESP_FAIL;
    char *buf = malloc(2048);
    for (int redirects = 0; redirects < 5; redirects++) {
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            break;
        }
        esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            esp_http_client_set_redirection(client);
            esp_http_client_close(client);
            err = ESP_FAIL;
            continue;
        }
        if (status != 200) {
            ESP_LOGW(TAG, "HTTP %d", status);
            err = ESP_FAIL;
            break;
        }
        int n;
        while ((n = esp_http_client_read(client, buf, 2048)) > 0) {
            cb(buf, n, ctx);
        }
        err = n < 0 ? ESP_FAIL : ESP_OK;
        break;
    }
    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

typedef struct {
    char *data;
    size_t len, max;
} text_ctx_t;

static void text_chunk(const char *buf, int len, void *arg)
{
    text_ctx_t *t = arg;
    if (t->len + len > t->max) {
        len = t->max - t->len;
    }
    memcpy(t->data + t->len, buf, len);
    t->len += len;
}

char *http_get_text(const char *url, size_t max_len)
{
    text_ctx_t t = { .data = heap_caps_malloc(max_len + 1, MALLOC_CAP_SPIRAM), .max = max_len };
    if (!t.data) {
        return NULL;
    }
    if (http_stream(url, text_chunk, &t) != ESP_OK) {
        free(t.data);
        return NULL;
    }
    t.data[t.len] = 0;
    /* 받은 만큼만 남기고 나머지 PSRAM 은 돌려줍니다 */
    char *fit = heap_caps_realloc(t.data, t.len + 1, MALLOC_CAP_SPIRAM);
    return fit ? fit : t.data;
}

typedef struct {
    http_line_cb_t cb;
    void *ctx;
    bool stop;
    char line[1024];
    size_t n;
} lines_ctx_t;

static void lines_chunk(const char *buf, int len, void *arg)
{
    lines_ctx_t *l = arg;
    for (int i = 0; i < len && !l->stop; i++) {
        char c = buf[i];
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            l->line[l->n] = 0;
            l->stop = !l->cb(l->line, l->ctx);
            l->n = 0;
        } else if (l->n < sizeof(l->line) - 1) {
            l->line[l->n++] = c;
        }
    }
}

esp_err_t http_get_lines(const char *url, http_line_cb_t cb, void *ctx)
{
    lines_ctx_t *l = heap_caps_calloc(1, sizeof(*l), MALLOC_CAP_SPIRAM);
    if (!l) {
        return ESP_ERR_NO_MEM;
    }
    l->cb = cb;
    l->ctx = ctx;
    esp_err_t err = http_stream(url, lines_chunk, l);
    if (l->n > 0 && !l->stop) {
        l->line[l->n] = 0;
        cb(l->line, ctx);
    }
    free(l);
    return err;
}
