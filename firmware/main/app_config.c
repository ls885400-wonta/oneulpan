#include "app_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "nvs.h"

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
    FIELD(cls), FIELD(neis_key), FIELD(ical),
};
#define FIELD_COUNT (sizeof(FIELDS) / sizeof(FIELDS[0]))

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
        bool secret = !strcmp(FIELDS[i].key, "pass") || !strcmp(FIELDS[i].key, "ical") ||
                      !strcmp(FIELDS[i].key, "neis_key");
        if (secret && !include_secrets) {
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

bool config_apply_json(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return false;
    }
    merge(root);
    cJSON_Delete(root);

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

char *config_public_json(void)
{
    return to_json(false);
}

/* ---- USB(UART0) 명령 ----
 *   INFO            -> INFO {"ver":..,"ip":..,"cfg":{..}}
 *   CFG {json}      -> CFG OK  (저장 후 다시 시작)
 */
#define LINE_MAX 2048

static void handle_line(char *line)
{
    if (!strncmp(line, "CFG ", 4)) {
        if (config_apply_json(line + 4)) {
            printf("\nCFG OK\n");
            fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        } else {
            printf("\nCFG ERR\n");
        }
    } else if (!strcmp(line, "INFO")) {
        char *pub = config_public_json();
        printf("\nINFO {\"ver\":\"%s\",\"ip\":\"%s\",\"cfg\":%s}\n", APP_VERSION, net_ip(), pub);
        cJSON_free(pub);
    }
    fflush(stdout);
}

static void serial_task(void *arg)
{
    char *line = malloc(LINE_MAX);
    size_t n = 0;
    uint8_t buf[128];
    for (;;) {
        int got = uart_read_bytes(UART_NUM_0, buf, sizeof(buf), pdMS_TO_TICKS(100));
        for (int i = 0; i < got; i++) {
            char c = buf[i];
            if (c == '\n' || c == '\r') {
                if (n > 0) {
                    line[n] = 0;
                    handle_line(line);
                    n = 0;
                }
            } else if (n < LINE_MAX - 1) {
                line[n++] = c;
            }
        }
    }
}

void config_serial_start(void)
{
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 4096, 0, 0, NULL, 0));
    xTaskCreate(serial_task, "serial", 6144, NULL, 3, NULL);
}
