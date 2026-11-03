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
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "ui.h"

static const char *TAG = "net";

#define SAVED_MAX 5
#define ROAM_WEAK (-70)    /* 이보다 약하면 더 센 곳을 찾아봄 */
#define ROAM_BETTER 8      /* 이만큼(dB) 더 세면 옮겨 감 */

typedef struct {
    char ssid[33];
    char pass[65];
} saved_t;

static volatile bool s_connected;
static char s_ip[16] = "";
static char s_ssid[33] = "";
static saved_t s_saved[SAVED_MAX];
static int s_saved_n;
static TaskHandle_t s_roam;
static SemaphoreHandle_t s_scan_lock; /* 찾기는 한 번에 하나만 */

/* ---- 저장된 와이파이 목록 (NVS "board"/"wifis") ---- */

static void saved_load(void)
{
    nvs_handle_t h;
    size_t len = sizeof(s_saved);
    s_saved_n = 0;
    if (nvs_open("board", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, "wifis", s_saved, &len) == ESP_OK) {
            s_saved_n = len / sizeof(saved_t);
        }
        nvs_close(h);
    }
}

static void saved_store(void)
{
    nvs_handle_t h;
    if (nvs_open("board", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "wifis", s_saved, s_saved_n * sizeof(saved_t));
        nvs_commit(h);
        nvs_close(h);
    }
}

static int saved_find(const char *ssid)
{
    for (int i = 0; i < s_saved_n; i++) {
        if (!strcmp(s_saved[i].ssid, ssid)) {
            return i;
        }
    }
    return -1;
}

/* 맨 앞에 넣기 (이미 있으면 앞으로 옮기고 비밀번호 새로) */
static void saved_add(const char *ssid, const char *pass)
{
    int i = saved_find(ssid);
    if (i < 0) {
        i = s_saved_n < SAVED_MAX ? s_saved_n++ : SAVED_MAX - 1;
    }
    memmove(&s_saved[1], &s_saved[0], i * sizeof(saved_t));
    strlcpy(s_saved[0].ssid, ssid, sizeof(s_saved[0].ssid));
    strlcpy(s_saved[0].pass, pass, sizeof(s_saved[0].pass));
}

void net_forget(const char *ssid)
{
    saved_load();
    int i = saved_find(ssid);
    if (i >= 0) {
        memmove(&s_saved[i], &s_saved[i + 1], (s_saved_n - i - 1) * sizeof(saved_t));
        s_saved_n--;
        saved_store();
    }
}

/* ---- 연결과 로밍 ---- */

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_ip[0] = 0;
        s_ssid[0] = 0;
        if (s_roam) {
            xTaskNotifyGive(s_roam);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            strlcpy(s_ssid, (char *)ap.ssid, sizeof(s_ssid));
        }
        ESP_LOGI(TAG, "연결됨 %s (%s)", s_ip, s_ssid);
        s_connected = true;
        if (s_roam) {
            xTaskNotifyGive(s_roam);
        }
    }
}

/* 주변을 찾아 저장된 와이파이 중 가장 센 곳. 없으면 false */
static bool best_saved(wifi_ap_record_t *best, int *which)
{
    bool found = false;
    xSemaphoreTake(s_scan_lock, portMAX_DELAY);
    wifi_scan_config_t sc = { .show_hidden = false };
    if (esp_wifi_scan_start(&sc, true) == ESP_OK) {
        uint16_t n = 30;
        wifi_ap_record_t *rec = calloc(n, sizeof(*rec));
        if (rec && esp_wifi_scan_get_ap_records(&n, rec) == ESP_OK) {
            for (int i = 0; i < n; i++) {
                int k = saved_find((char *)rec[i].ssid);
                if (k >= 0 && (!found || rec[i].rssi > best->rssi)) {
                    *best = rec[i];
                    *which = k;
                    found = true;
                }
            }
        } else {
            esp_wifi_clear_ap_list();
        }
        free(rec);
    }
    xSemaphoreGive(s_scan_lock);
    return found;
}

static void connect_to(const wifi_ap_record_t *ap, int k)
{
    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, s_saved[k].ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, s_saved[k].pass, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = s_saved[k].pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    memcpy(wc.sta.bssid, ap->bssid, 6);
    wc.sta.bssid_set = true;
    wc.sta.channel = ap->primary;
    wc.sta.rm_enabled = 1;  /* 같은 이름 공유기끼리 옮겨 다니기 도움 */
    wc.sta.btm_enabled = 1;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    ESP_LOGI(TAG, "'%s' 에 연결 (신호 %d)", s_saved[k].ssid, ap->rssi);
    esp_wifi_connect();
}

static void roam_task(void *arg)
{
    for (;;) {
        if (!s_connected) {
            wifi_ap_record_t ap;
            int k;
            esp_wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(200));
            ulTaskNotifyTake(pdTRUE, 0); /* 방금 끊은 알림은 버림 */
            if (best_saved(&ap, &k)) {
                connect_to(&ap, k);
                /* 붙을 때까지 (또는 실패) 기다림 */
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20000));
                if (s_connected) {
                    continue;
                }
            } else {
                ESP_LOGW(TAG, "저장된 와이파이가 보이지 않음");
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));
            continue;
        }
        /* 붙어 있는 동안 30초마다 신호 확인, 약하면 더 센 곳으로 */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000));
        wifi_ap_record_t cur;
        if (!s_connected || esp_wifi_sta_get_ap_info(&cur) != ESP_OK || cur.rssi >= ROAM_WEAK) {
            continue;
        }
        wifi_ap_record_t ap;
        int k;
        if (best_saved(&ap, &k) && ap.rssi >= cur.rssi + ROAM_BETTER && memcmp(ap.bssid, cur.bssid, 6)) {
            ESP_LOGI(TAG, "신호 약함 (%d) → 더 센 곳으로", cur.rssi);
            esp_wifi_disconnect();
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(3000));
            ulTaskNotifyTake(pdTRUE, 0);
            connect_to(&ap, k);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20000));
        }
    }
}

void net_start(const char *ssid, const char *pass)
{
    setenv("TZ", "KST-9", 1);
    tzset();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    s_scan_lock = xSemaphoreCreateMutex();

    /* 설정의 와이파이는 늘 목록 맨 앞에 */
    saved_load();
    if (ssid[0] && (saved_find(ssid) != 0 || strcmp(s_saved[0].pass, pass))) {
        saved_add(ssid, pass);
        saved_store();
    }
    ESP_LOGI(TAG, "저장된 와이파이 %d개", s_saved_n);

    /* 와이파이 설정이 없어도 켜 둡니다 (보드 설정 화면에서 찾기) */
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
    if (s_saved_n == 0) {
        ESP_LOGW(TAG, "와이파이 설정 없음");
        return;
    }
    xTaskCreate(roam_task, "roam", 4096, NULL, 4, &s_roam);

    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp);
}

const char *net_ssid(void)
{
    return s_ssid;
}

/* ---- 와이파이 찾기 (보드 설정 화면) ---- */

static void scan_task(void *arg)
{
    wifi_scan_t *list = calloc(1, sizeof(*list));
    wifi_scan_config_t sc = { .show_hidden = false };
    xSemaphoreTake(s_scan_lock, portMAX_DELAY);
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
    xSemaphoreGive(s_scan_lock);
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
