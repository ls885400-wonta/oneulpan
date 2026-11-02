#include "feeds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_config.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "ui.h"

static const char *TAG = "feeds";
static TaskHandle_t s_task;

/* ---------------- 날씨 (Open-Meteo, 무료·키 필요 없음) ---------------- */

static double num_at(const cJSON *obj, const char *key, int idx)
{
    const cJSON *arr = cJSON_GetObjectItem(obj, key);
    const cJSON *v = cJSON_IsArray(arr) ? cJSON_GetArrayItem(arr, idx) : arr;
    return cJSON_IsNumber(v) ? v->valuedouble : 0;
}

static bool fetch_weather(weather_t *w)
{
    char url[400];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
             "&current=temperature_2m,relative_humidity_2m,weather_code"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
             "&timezone=Asia%%2FSeoul&forecast_days=3",
             g_cfg.lat, g_cfg.lon);
    char *body = http_get_text(url, 16 * 1024);
    if (!body) {
        return false;
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return false;
    }
    const cJSON *cur = cJSON_GetObjectItem(root, "current");
    const cJSON *daily = cJSON_GetObjectItem(root, "daily");
    if (cur && daily) {
        w->temp = num_at(cur, "temperature_2m", 0);
        w->humidity = (int)num_at(cur, "relative_humidity_2m", 0);
        w->code = (int)num_at(cur, "weather_code", 0);
        for (int i = 0; i < 3; i++) {
            w->day_code[i] = (int)num_at(daily, "weather_code", i);
            w->day_max[i] = num_at(daily, "temperature_2m_max", i);
            w->day_min[i] = num_at(daily, "temperature_2m_min", i);
            w->day_pop[i] = (int)num_at(daily, "precipitation_probability_max", i);
        }
        w->ok = true;
    }
    cJSON_Delete(root);
    return w->ok;
}

/* ---------------- 나이스 (급식·시간표) ---------------- */

/* 오후 3시가 넘으면 다음 평일을 보여 줍니다. */
static struct tm target_day(bool *is_tomorrow)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    *is_tomorrow = false;
    if (tm.tm_hour >= 15 || tm.tm_wday == 0 || tm.tm_wday == 6) {
        do {
            t += 86400;
            localtime_r(&t, &tm);
        } while (tm.tm_wday == 0 || tm.tm_wday == 6);
        *is_tomorrow = true;
    }
    return tm;
}

static cJSON *neis_rows(const char *service, const char *params, cJSON **root_out)
{
    char url[512];
    int n = snprintf(url, sizeof(url),
                     "https://open.neis.go.kr/hub/%s?Type=json&pIndex=1&pSize=50"
                     "&ATPT_OFCDC_SC_CODE=%s&SD_SCHUL_CODE=%s%s",
                     service, g_cfg.atpt, g_cfg.school, params);
    if (g_cfg.neis_key[0]) {
        snprintf(url + n, sizeof(url) - n, "&KEY=%s", g_cfg.neis_key);
    }
    char *body = http_get_text(url, 48 * 1024);
    if (!body) {
        return NULL;
    }
    *root_out = cJSON_Parse(body);
    free(body);
    /* {"서비스":[{"head":[..]},{"row":[..]}]}  자료가 없으면 {"RESULT":{..}} */
    const cJSON *svc = cJSON_GetObjectItem(*root_out, service);
    const cJSON *second = cJSON_GetArrayItem(svc, 1);
    return cJSON_GetObjectItem(second, "row");
}

/* "밥<br/>국 (5.6.13)" -> "밥\n국" */
static void clean_dish(const char *src, char *dst, size_t size)
{
    size_t o = 0;
    while (*src && o < size - 1) {
        if (!strncmp(src, "<br/>", 5)) {
            while (o > 0 && dst[o - 1] == ' ') {
                o--;
            }
            dst[o++] = '\n';
            src += 5;
            while (*src == ' ') {
                src++;
            }
        } else if (*src == '(') {
            const char *p = src + 1;
            while (*p && (strchr("0123456789. ", *p))) {
                p++;
            }
            if (*p == ')') {
                src = p + 1; /* 알레르기 번호 지우기 */
            } else {
                dst[o++] = *src++;
            }
        } else if (!strncmp(src, "&amp;", 5)) {
            dst[o++] = '&';
            src += 5;
        } else {
            dst[o++] = *src++;
        }
    }
    while (o > 0 && (dst[o - 1] == ' ' || dst[o - 1] == '\n')) {
        o--;
    }
    dst[o] = 0;
}

static bool fetch_meal(meal_t *m)
{
    struct tm d = target_day(&m->tomorrow);
    char params[40];
    snprintf(params, sizeof(params), "&MLSV_YMD=%04d%02d%02d", d.tm_year + 1900, d.tm_mon + 1, d.tm_mday);

    cJSON *root = NULL;
    cJSON *rows = neis_rows("mealServiceDietInfo", params, &root);
    if (!root) {
        return false;
    }
    m->lunch[0] = m->dinner[0] = 0;
    cJSON *row;
    cJSON_ArrayForEach(row, rows) {
        const char *kind = cJSON_GetStringValue(cJSON_GetObjectItem(row, "MMEAL_SC_NM"));
        const char *dish = cJSON_GetStringValue(cJSON_GetObjectItem(row, "DDISH_NM"));
        if (!kind || !dish) {
            continue;
        }
        if (!strcmp(kind, "중식")) {
            clean_dish(dish, m->lunch, sizeof(m->lunch));
        } else if (!strcmp(kind, "석식")) {
            clean_dish(dish, m->dinner, sizeof(m->dinner));
        }
    }
    cJSON_Delete(root);
    m->ok = true; /* 자료가 없으면 '급식 없음' 으로 보입니다 */
    return true;
}

static bool fetch_timetable(timetable_t *tt)
{
    const char *service = !strcmp(g_cfg.level, "els") ? "elsTimetable"
                        : !strcmp(g_cfg.level, "mis") ? "misTimetable" : "hisTimetable";
    bool tomorrow;
    struct tm d = target_day(&tomorrow);
    char params[80];
    snprintf(params, sizeof(params), "&ALL_TI_YMD=%04d%02d%02d&GRADE=%s&CLASS_NM=%s",
             d.tm_year + 1900, d.tm_mon + 1, d.tm_mday, g_cfg.grade, g_cfg.cls);

    cJSON *root = NULL;
    cJSON *rows = neis_rows(service, params, &root);
    if (!root) {
        return false;
    }
    memset(tt, 0, sizeof(*tt));
    cJSON *row;
    cJSON_ArrayForEach(row, rows) {
        const char *perio = cJSON_GetStringValue(cJSON_GetObjectItem(row, "PERIO"));
        const char *subj = cJSON_GetStringValue(cJSON_GetObjectItem(row, "ITRT_CNTNT"));
        int p = perio ? atoi(perio) : 0;
        if (p >= 1 && p <= MAX_PERIODS && subj) {
            strlcpy(tt->subject[p - 1], subj, sizeof(tt->subject[0]));
            if (p > tt->count) {
                tt->count = p;
            }
        }
    }
    cJSON_Delete(root);
    tt->ok = true;
    return true;
}

/* ---------------- 일정 관리 ---------------- */

#define MIN_30 (30 * 60)

static void feeds_task(void *arg)
{
    weather_t *w = heap_caps_calloc(1, sizeof(weather_t), MALLOC_CAP_SPIRAM);
    meal_t *m = heap_caps_calloc(1, sizeof(meal_t), MALLOC_CAP_SPIRAM);
    timetable_t *tt = heap_caps_calloc(1, sizeof(timetable_t), MALLOC_CAP_SPIRAM);
    calendar_t *cal = heap_caps_calloc(1, sizeof(calendar_t), MALLOC_CAP_SPIRAM);

    time_t last_weather = 0, last_school = 0, last_cal = 0;
    int last_hour = -1;
    bool force = true;

    for (;;) {
        if (net_connected() && net_time_ok()) {
            time_t now = time(NULL);
            struct tm tm;
            localtime_r(&now, &tm);
            /* 날짜가 바뀌거나 오후 3시가 되면 급식·시간표를 다시 받습니다 */
            bool hour_mark = last_hour != tm.tm_hour && (tm.tm_hour == 0 || tm.tm_hour == 15);
            last_hour = tm.tm_hour;

            if (g_cfg.lat[0] && (force || now - last_weather > MIN_30)) {
                if (fetch_weather(w)) {
                    last_weather = now;
                    ui_set_weather(w);
                }
            }
            if (g_cfg.school[0] && (force || hour_mark || now - last_school > 6 * 3600)) {
                bool ok = fetch_meal(m);
                if (ok) {
                    ui_set_meal(m);
                }
                if (g_cfg.grade[0] && g_cfg.cls[0] && fetch_timetable(tt)) {
                    ui_set_timetable(tt, m->tomorrow);
                }
                if (ok) {
                    last_school = now;
                }
            }
            if (g_cfg.ical[0] && (force || hour_mark || now - last_cal > 15 * 60)) {
                if (ical_fetch(g_cfg.ical, cal)) {
                    last_cal = now;
                    ui_set_calendar(cal);
                }
            }
            if (force) {
                ESP_LOGI(TAG, "모두 받음, 남은 메모리 %u / PSRAM %u",
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            }
            force = false;
            time_t latest = last_weather > last_school ? last_weather : last_school;
            ui_set_updated(latest > last_cal ? latest : last_cal);
        }
        ui_set_status(g_cfg.ssid[0] != 0, net_connected(), net_ip());
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20000))) {
            force = true;
        }
    }
}

void feeds_start(void)
{
    xTaskCreate(feeds_task, "feeds", 12 * 1024, NULL, 4, &s_task);
}

void feeds_refresh_now(void)
{
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}
