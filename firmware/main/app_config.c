#include "app_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "nvs.h"
#include "ui.h"

static const char *TAG = "config";

app_config_t g_cfg;

#define FIELD(name) { #name, g_cfg.name, sizeof(g_cfg.name) }
static const struct {
    const char *key;
    char *dst;
    size_t size;
} FIELDS[] = {
    FIELD(ssid), FIELD(pass), FIELD(title), FIELD(place), FIELD(lat), FIELD(lon),
    FIELD(atpt), FIELD(school), FIELD(school_name), FIELD(level), FIELD(grade),
    FIELD(cls), FIELD(neis_key), FIELD(ical), FIELD(sched_url), FIELD(cha_url),
    FIELD(owner), FIELD(night_from), FIELD(night_to), FIELD(weekend_off), FIELD(h12),
    FIELD(home_ret), FIELD(show),
};
#define FIELD_COUNT (sizeof(FIELDS) / sizeof(FIELDS[0]))

/* 남에게 보이면 안 되는 값: INFO 에는 있는지 없는지만 알려 줍니다 */
static bool is_secret(const char *key)
{
    return !strcmp(key, "pass") || !strcmp(key, "ical") || !strcmp(key, "neis_key") ||
           !strcmp(key, "cha_url");
}

static void merge(const cJSON *root)
{
    for (int i = 0; i < FIELD_COUNT; i++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, FIELDS[i].key);
        if (cJSON_IsString(v)) {
            strlcpy(FIELDS[i].dst, v->valuestring, FIELDS[i].size);
        }
    }
}

static char *to_json(bool include_secrets)
{
    cJSON *root = cJSON_CreateObject();
    for (int i = 0; i < FIELD_COUNT; i++) {
        if (is_secret(FIELDS[i].key) && !include_secrets) {
            cJSON_AddBoolToObject(root, FIELDS[i].key, FIELDS[i].dst[0] != 0);
        } else {
            cJSON_AddStringToObject(root, FIELDS[i].key, FIELDS[i].dst);
        }
    }
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

void config_load(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    strlcpy(g_cfg.title, "오늘판", sizeof(g_cfg.title));
    strlcpy(g_cfg.home_ret, "20", sizeof(g_cfg.home_ret));
    strlcpy(g_cfg.show, "wdmts", sizeof(g_cfg.show));

    nvs_handle_t h;
    if (nvs_open("board", NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW(TAG, "저장된 설정 없음");
        return;
    }
    size_t len = 0;
    if (nvs_get_str(h, "cfg", NULL, &len) == ESP_OK && len > 0) {
        char *buf = malloc(len);
        if (buf && nvs_get_str(h, "cfg", buf, &len) == ESP_OK) {
            cJSON *root = cJSON_Parse(buf);
            if (root) {
                merge(root);
                cJSON_Delete(root);
            }
        }
        free(buf);
    }
    nvs_close(h);
}

bool config_save(void)
{
    char *all = to_json(true);
    nvs_handle_t h;
    esp_err_t err = nvs_open("board", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, "cfg", all);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    cJSON_free(all);
    return err == ESP_OK;
}

bool config_apply_json(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return false;
    }
    merge(root);
    cJSON_Delete(root);
    return config_save();
}

char *config_public_json(void)
{
    return to_json(false);
}

bool config_show(char what)
{
    return strchr(g_cfg.show, what) != NULL;
}

/* ---- 보드 안 파일 ---- */

void storage_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/data",
        .partition_label = "storage",
        .max_files = 4,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "파일 저장소를 열지 못했습니다: %s", esp_err_to_name(err));
    }
}

char *storage_read(const char *name, size_t *len)
{
    char path[48];
    snprintf(path, sizeof(path), "/data/%s.json", name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = size > 0 ? heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM) : NULL;
    if (buf && fread(buf, 1, size, f) == (size_t)size) {
        buf[size] = 0;
        if (len) {
            *len = size;
        }
    } else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

bool storage_write(const char *name, const char *data, size_t len)
{
    char path[48], tmp[52];
    snprintf(path, sizeof(path), "/data/%s.json", name);
    snprintf(tmp, sizeof(tmp), "/data/%s.tmp", name);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return false;
    }
    bool ok = fwrite(data, 1, len, f) == len;
    ok &= fclose(f) == 0;
    if (ok) {
        remove(path);
        ok = rename(tmp, path) == 0;
    }
    return ok;
}

/* ---- USB(UART0) 명령 ----
 *   INFO            -> INFO {"ver":..,"ip":..,"cfg":{..}}
 *   CFG {json}      -> CFG OK  (저장 후 다시 시작)
 *   PUT 이름 길이    -> PUT OK          파일 보내기 시작 (이름: 영문·숫자)
 *   DAT 글자들       -> DAT 받은길이     여러 번 나눠 보냅니다 (줄바꿈 없는 JSON)
 *   END             -> END OK          /data/이름.json 으로 저장
 */
#define LINE_MAX 2048
#define FILE_MAX (256 * 1024)

static struct {
    char name[16];
    char *buf;
    size_t want, got;
} s_put;

static void handle_put(char *line)
{
    if (!strncmp(line, "PUT ", 4)) {
        free(s_put.buf);
        memset(&s_put, 0, sizeof(s_put));
        long want = 0;
        if (sscanf(line + 4, "%15s %ld", s_put.name, &want) == 2 && want > 0 && want <= FILE_MAX &&
            strspn(s_put.name, "abcdefghijklmnopqrstuvwxyz0123456789_") == strlen(s_put.name)) {
            s_put.buf = heap_caps_malloc(want + 1, MALLOC_CAP_SPIRAM);
            s_put.want = want;
        }
        printf("\n%s\n", s_put.buf ? "PUT OK" : "PUT ERR");
    } else if (!strncmp(line, "DAT ", 4)) {
        size_t n = strlen(line + 4);
        if (s_put.buf && s_put.got + n <= s_put.want) {
            memcpy(s_put.buf + s_put.got, line + 4, n);
            s_put.got += n;
            printf("\nDAT %u\n", (unsigned)s_put.got);
        } else {
            printf("\nDAT ERR\n");
        }
    } else { /* END */
        bool ok = s_put.buf && s_put.got == s_put.want && storage_write(s_put.name, s_put.buf, s_put.got);
        printf("\n%s\n", ok ? "END OK" : "END ERR");
        fflush(stdout);
        if (ok && !strcmp(s_put.name, "tt")) {
            teachers_reload();
        }
        free(s_put.buf);
        memset(&s_put, 0, sizeof(s_put));
    }
}

static void handle_line(char *line)
{
    if (!strncmp(line, "PUT ", 4) || !strncmp(line, "DAT ", 4) || !strcmp(line, "END")) {
        handle_put(line);
        fflush(stdout);
        return;
    }

    /* 켜지는 동안 받은 깨진 조각이 앞에 붙을 수 있어, 마지막 명령부터 읽습니다 */
    for (char *p; (p = strstr(line + 1, "CFG ")) != NULL;) {
        line = p;
    }
    char *info = strstr(line, "INFO");
    if (strncmp(line, "CFG ", 4) && info && !strcmp(info, "INFO")) {
        line = info;
    }

    if (!strncmp(line, "CFG ", 4)) {
        if (config_apply_json(line + 4)) {
            printf("\nCFG OK\n");
            fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        } else {
            printf("\nCFG ERR\n");
        }
    } else if (strstr(line, "BATT")) {
        batt_dump();
    } else if (!strcmp(line, "INFO")) {
        char *pub = config_public_json();
        printf("\nINFO {\"ver\":\"%s\",\"ip\":\"%s\",\"cfg\":%s}\n", APP_VERSION, net_ip(), pub);
        cJSON_free(pub);
    }
    fflush(stdout);
}

/* 한 글자씩 모아 줄 단위로 명령 처리 */
typedef struct {
    char *line;
    size_t n;
} line_buf_t;

static void feed(line_buf_t *lb, const uint8_t *buf, int got)
{
    for (int i = 0; i < got; i++) {
        char c = buf[i];
        if (c == '\n' || c == '\r') {
            if (lb->n > 0) {
                lb->line[lb->n] = 0;
                handle_line(lb->line);
                lb->n = 0;
            }
        } else if (lb->n < LINE_MAX - 1) {
            lb->line[lb->n++] = c;
        }
    }
}

/* UART 단자(CH343)와 USB 단자(ESP32-S3 내장 USB) 어느 쪽으로 와도 받습니다.
 * 대답(printf)은 두 쪽 모두로 나갑니다. */
static void serial_task(void *arg)
{
    line_buf_t uart = { malloc(LINE_MAX), 0 }, usb = { malloc(LINE_MAX), 0 };
    uint8_t buf[128];
    for (;;) {
        int got = uart_read_bytes(UART_NUM_0, buf, sizeof(buf), pdMS_TO_TICKS(20));
        feed(&uart, buf, got);
        got = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(20));
        feed(&usb, buf, got);
    }
}

void config_serial_start(void)
{
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 4096, 0, 0, NULL, 0));
    uart_flush_input(UART_NUM_0);
    usb_serial_jtag_driver_config_t usj = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usj.rx_buffer_size = 4096;
    if (usb_serial_jtag_driver_install(&usj) == ESP_OK) {
        usb_serial_jtag_vfs_use_driver();
    }
    xTaskCreate(serial_task, "serial", 6144, NULL, 3, NULL);
}
