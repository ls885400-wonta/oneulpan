#include "net.h"

#include "app_config.h"

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
#define FIRST_OK (-80)     /* 맨 앞(가장 최근에 고른) 와이파이가 이만큼 보이면 그곳부터 */

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
static SemaphoreHandle_t s_list_lock; /* 저장 목록은 화면과 roam 이 같이 씀 */
static char s_join[33];               /* 방금 고른 와이파이: 이것부터 붙어 봄 */
static char s_try[33];                /* 지금 붙어 보는 와이파이 */
static char s_fail[128];              /* 마지막 실패 ("'이름' 이유") */
static volatile int s_reason;         /* 마지막으로 끊긴 이유 코드 */

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

static void saved_store_job(void *arg);

/* 화면 작업에서도 불리므로 플래시 작업에 맡김 */
static void saved_store(void)
{
    flash_run(saved_store_job, NULL);
}

static void saved_store_job(void *arg)
{
    nvs_handle_t h;
    if (nvs_open("board", NVS_READWRITE, &h) == ESP_OK) {
        if (s_saved_n > 0) {
            nvs_set_blob(h, "wifis", s_saved, s_saved_n * sizeof(saved_t));
        } else {
            nvs_erase_key(h, "wifis");
        }
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

void net_join(const char *ssid, const char *pass)
{
    xSemaphoreTake(s_list_lock, portMAX_DELAY);
    saved_add(ssid, pass);
    saved_store();
    xSemaphoreGive(s_list_lock);
    strlcpy(s_join, ssid, sizeof(s_join));
    s_fail[0] = 0;
    ESP_LOGI(TAG, "새 와이파이 '%s' 저장, 연결해 봄", ssid);
    if (s_connected) {
        esp_wifi_disconnect(); /* 끊기면 roam 이 새 와이파이부터 붙어 봄 */
    } else if (s_roam) {
        xTaskNotifyGive(s_roam);
    }
}

void net_forget_all(void)
{
    xSemaphoreTake(s_list_lock, portMAX_DELAY);
    s_saved_n = 0;
    saved_store();
    xSemaphoreGive(s_list_lock);
    s_join[0] = s_fail[0] = 0;
    ESP_LOGI(TAG, "저장된 와이파이 모두 지움");
    esp_wifi_disconnect();
}

int net_saved_count(void)
{
    return s_saved_n;
}

const char *net_trying(void)
{
    return s_try;
}

const char *net_fail_text(void)
{
    return s_fail;
}

static const char *reason_text(int r)
{
    switch (r) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_802_1X_AUTH_FAILED:
        return "비밀번호가 맞지 않습니다";
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "와이파이를 찾지 못했습니다";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "보안 방식이 맞지 않습니다";
    case 0:
        return "공유기가 대답하지 않습니다";
    default:
        return NULL;
    }
}

static void set_fail(const char *ssid, int reason)
{
    const char *why = reason_text(reason);
    if (why) {
        snprintf(s_fail, sizeof(s_fail), "'%s' %s", ssid, why);
    } else {
        snprintf(s_fail, sizeof(s_fail), "'%s' 연결 실패 (%d)", ssid, reason);
    }
    ESP_LOGW(TAG, "%s", s_fail);
}

/* ---- 연결과 로밍 ---- */

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_reason = ((wifi_event_sta_disconnected_t *)data)->reason;
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

/* 주변을 찾아 붙을 저장된 와이파이를 고릅니다. 없으면 false
 *  - 방금 고른 와이파이(s_join)가 있으면 그것만
 *  - 아니면 맨 앞(가장 최근에 고른 것)이 쓸 만하게 보이면 그것, 아니면 가장 센 곳 */
static bool best_saved(wifi_ap_record_t *best, saved_t *out)
{
    bool found = false;
    int best_score = 0;
    xSemaphoreTake(s_scan_lock, portMAX_DELAY);
    wifi_scan_config_t sc = { .show_hidden = false };
    if (esp_wifi_scan_start(&sc, true) == ESP_OK) {
        uint16_t n = 30;
        wifi_ap_record_t *rec = calloc(n, sizeof(*rec));
        if (rec && esp_wifi_scan_get_ap_records(&n, rec) == ESP_OK) {
            xSemaphoreTake(s_list_lock, portMAX_DELAY);
            for (int i = 0; i < n; i++) {
                const char *ssid = (char *)rec[i].ssid;
                int k = saved_find(ssid);
                if (k < 0 || (s_join[0] && strcmp(ssid, s_join))) {
                    continue;
                }
                int score = rec[i].rssi + (k == 0 && rec[i].rssi >= FIRST_OK ? 100 : 0);
                if (!found || score > best_score) {
                    *best = rec[i];
                    *out = s_saved[k];
                    best_score = score;
                    found = true;
                }
            }
            xSemaphoreGive(s_list_lock);
        } else {
            esp_wifi_clear_ap_list();
        }
        free(rec);
    }
    xSemaphoreGive(s_scan_lock);
    return found;
}

static void connect_to(const wifi_ap_record_t *ap, const saved_t *w)
{
    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, w->ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, w->pass, sizeof(wc.sta.password));
    /* 비밀번호가 있으면 WEP~WPA3 어느 것이든 (공유기마다 달라서) */
    wc.sta.threshold.authmode = w->pass[0] ? WIFI_AUTH_WEP : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    memcpy(wc.sta.bssid, ap->bssid, 6);
    wc.sta.bssid_set = true;
    wc.sta.channel = ap->primary;
    wc.sta.rm_enabled = 1;  /* 같은 이름 공유기끼리 옮겨 다니기 도움 */
    wc.sta.btm_enabled = 1;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    ESP_LOGI(TAG, "'%s' 에 연결 (신호 %d)", w->ssid, ap->rssi);
    strlcpy(s_try, w->ssid, sizeof(s_try));
    s_reason = 0;
    esp_wifi_connect();
}

static void roam_task(void *arg)
{
    for (;;) {
        if (!s_connected) {
            if (s_saved_n == 0) {
                s_try[0] = 0;
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY); /* 새로 고를 때까지 */
                continue;
            }
            wifi_ap_record_t ap;
            saved_t w;
            char join[33];
            strlcpy(join, s_join, sizeof(join));
            esp_wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(200));
            ulTaskNotifyTake(pdTRUE, 0); /* 방금 끊은 알림은 버림 */
            if (best_saved(&ap, &w)) {
                connect_to(&ap, &w);
                /* 붙을 때까지 (또는 실패) 기다림 */
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20000));
                if (s_connected) {
                    s_try[0] = 0;
                    if (!strcmp(join, w.ssid)) {
                        s_join[0] = 0;
                    }
                    continue;
                }
                set_fail(w.ssid, s_reason);
            } else if (join[0]) {
                set_fail(join, WIFI_REASON_NO_AP_FOUND);
            } else {
                ESP_LOGW(TAG, "저장된 와이파이가 보이지 않음");
            }
            s_try[0] = 0;
            /* 방금 고른 곳은 한 번 해 보고, 안 되면 다른 저장된 곳도 봄 */
            if (join[0] && !strcmp(join, s_join)) {
                s_join[0] = 0;
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
        saved_t w;
        if (best_saved(&ap, &w) && ap.rssi >= cur.rssi + ROAM_BETTER && memcmp(ap.bssid, cur.bssid, 6)) {
            ESP_LOGI(TAG, "신호 약함 (%d) → 더 센 곳으로", cur.rssi);
            esp_wifi_disconnect();
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(3000));
            ulTaskNotifyTake(pdTRUE, 0);
            connect_to(&ap, &w);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20000));
            s_try[0] = 0;
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
    s_list_lock = xSemaphoreCreateMutex();

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
    }
    /* 저장된 것이 없어도 띄워 둡니다: 설정 화면에서 고르면 바로 붙음 */
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
