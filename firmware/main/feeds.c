#include "feeds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_config.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "ui.h"

static const char *TAG = "feeds";
static TaskHandle_t s_task;

data_t g_data;

const char *WEEKDAYS[7] = { "일", "월", "화", "수", "목", "금", "토" };

/* ---------------- 날짜 ---------------- */

static time_t ymd_to_time(int ymd)
{
    struct tm tm = { .tm_year = ymd / 10000 - 1900, .tm_mon = ymd / 100 % 100 - 1,
                     .tm_mday = ymd % 100, .tm_hour = 12 };
    return mktime(&tm);
}

static int time_to_ymd(time_t t)
{
    struct tm tm;
    localtime_r(&t, &tm);
    return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}

int ymd_today(void)
{
    return time_to_ymd(time(NULL));
}

int ymd_add(int ymd, int days)
{
    return time_to_ymd(ymd_to_time(ymd) + (time_t)days * 86400);
}

int ymd_wday(int ymd)
{
    time_t t = ymd_to_time(ymd);
    struct tm tm;
    localtime_r(&t, &tm);
    return tm.tm_wday;
}

int ymd_diff(int a, int b)
{
    return (int)((ymd_to_time(a) - ymd_to_time(b) + 43200) / 86400);
}

int ymd_week_monday(void)
{
    int today = ymd_today();
    int wd = ymd_wday(today);
    if (wd == 0) {
        return ymd_add(today, 1);
    }
    if (wd == 6) {
        return ymd_add(today, 2);
    }
    return ymd_add(today, 1 - wd);
}

/* ---------------- 자료 바꿔 끼우기 ---------------- */

void feeds_swap(int kind, void *fresh)
{
    void **slot = kind == D_WEATHER ? (void **)&g_data.weather
                : kind == D_MEALS ? (void **)&g_data.meals
                : kind == D_MYCAL ? (void **)&g_data.mycal
                : kind == D_SCHOOL ? (void **)&g_data.school
                : kind == D_CHA ? (void **)&g_data.cha
                : (void **)&g_data.teachers;
    void *old = NULL;
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        old = *slot;
        *slot = fresh;
        ui_refresh(kind);
        esp_lv_adapter_unlock();
    } else {
        old = fresh;
    }
    free(old);
}

const acad_t *school_day_info(int ymd, bool *holiday)
{
    const school_t *s = g_data.school;
    const acad_t *found = NULL;
    if (holiday) {
        *holiday = false;
    }
    for (int i = 0; s && i < s->acad_count; i++) {
        const acad_t *a = &s->acad[i];
        if (a->ymd != ymd) {
            continue;
        }
        bool off = !strcmp(a->type, "공휴일") || !strcmp(a->type, "재량휴업일") || !strcmp(a->type, "방학");
        if (off && holiday) {
            *holiday = true;
        }
        if (!found || off) {
            found = a;
        }
    }
    return found;
}

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
             "&timezone=Asia%%2FSeoul&forecast_days=7",
             g_cfg.lat, g_cfg.lon);
    char *body = http_get_text(url, 16 * 1024);
    if (!body) {
        return false;
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    const cJSON *cur = cJSON_GetObjectItem(root, "current");
    const cJSON *daily = cJSON_GetObjectItem(root, "daily");
    if (cur && daily) {
        w->temp = num_at(cur, "temperature_2m", 0);
        w->humidity = (int)num_at(cur, "relative_humidity_2m", 0);
        w->code = (int)num_at(cur, "weather_code", 0);
        for (int i = 0; i < 7; i++) {
            w->day_code[i] = (int)num_at(daily, "weather_code", i);
            w->day_max[i] = num_at(daily, "temperature_2m_max", i);
            w->day_min[i] = num_at(daily, "temperature_2m_min", i);
            w->day_pop[i] = (int)num_at(daily, "precipitation_probability_max", i);
        }
        w->ok = true;
    }
    cJSON_Delete(root);
    if (!w->ok) {
        return false;
    }

    /* 미세먼지 */
    snprintf(url, sizeof(url),
             "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%s&longitude=%s"
             "&current=pm10,pm2_5&timezone=Asia%%2FSeoul",
             g_cfg.lat, g_cfg.lon);
    body = http_get_text(url, 4 * 1024);
    if (body) {
        root = cJSON_Parse(body);
        free(body);
        cur = cJSON_GetObjectItem(root, "current");
        if (cJSON_IsNumber(cJSON_GetObjectItem(cur, "pm10"))) {
            w->pm10 = (int)(num_at(cur, "pm10", 0) + 0.5);
            w->pm25 = (int)(num_at(cur, "pm2_5", 0) + 0.5);
            w->air_ok = true;
        }
        cJSON_Delete(root);
    }
    return true;
}

/* ---------------- 나이스 급식 ---------------- */

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
    char *body = http_get_text(url, 64 * 1024);
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

static meal_day_t *meal_slot(meals_t *m, int ymd)
{
    for (int i = 0; i < m->count; i++) {
        if (m->day[i].ymd == ymd) {
            return &m->day[i];
        }
    }
    if (m->count >= MEAL_DAYS) {
        return NULL;
    }
    meal_day_t *d = &m->day[m->count++];
    d->ymd = ymd;
    return d;
}

static int compare_meal(const void *a, const void *b)
{
    return ((const meal_day_t *)a)->ymd - ((const meal_day_t *)b)->ymd;
}

static bool fetch_meals(meals_t *m)
{
    int from = ymd_today(), to = ymd_add(from, 10);
    char params[64];
    snprintf(params, sizeof(params), "&MLSV_FROM_YMD=%d&MLSV_TO_YMD=%d", from, to);
    cJSON *root = NULL;
    cJSON *rows = neis_rows("mealServiceDietInfo", params, &root);
    if (!root) {
        return false;
    }
    cJSON *row;
    cJSON_ArrayForEach(row, rows) {
        const char *kind = cJSON_GetStringValue(cJSON_GetObjectItem(row, "MMEAL_SC_NM"));
        const char *dish = cJSON_GetStringValue(cJSON_GetObjectItem(row, "DDISH_NM"));
        const char *ymd = cJSON_GetStringValue(cJSON_GetObjectItem(row, "MLSV_YMD"));
        if (!kind || !dish || !ymd) {
            continue;
        }
        meal_day_t *d = meal_slot(m, atoi(ymd));
        if (!d) {
            continue;
        }
        if (!strcmp(kind, "중식")) {
            clean_dish(dish, d->lunch, sizeof(d->lunch));
        } else if (!strcmp(kind, "석식")) {
            clean_dish(dish, d->dinner, sizeof(d->dinner));
        }
    }
    cJSON_Delete(root);
    qsort(m->day, m->count, sizeof(meal_day_t), compare_meal);
    m->ok = true; /* 자료가 없으면 '급식 없음' 으로 보입니다 */
    return true;
}

/* ---------------- 일정 관리 ---------------- */

#define MIN (60)

static void *psram(size_t size)
{
    return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM);
}

static void feeds_task(void *arg)
{
    time_t last_weather = 0, last_meal = 0, last_cal = 0, last_school = 0, last_cha = 0;
    int last_day = 0;
    bool force = true;

    for (;;) {
        if (net_connected() && net_time_ok()) {
            time_t now = time(NULL);
            int today = ymd_today();
            bool new_day = today != last_day;
            last_day = today;

            if (g_cfg.lat[0] && (force || now - last_weather > 30 * MIN)) {
                weather_t *w = psram(sizeof(*w));
                if (w && fetch_weather(w)) {
                    last_weather = now;
                    feeds_swap(D_WEATHER, w);
                } else {
                    free(w);
                }
            }
            if (g_cfg.school[0] && (force || new_day || now - last_meal > 3 * 3600)) {
                meals_t *m = psram(sizeof(*m));
                if (m && fetch_meals(m)) {
                    last_meal = now;
                    feeds_swap(D_MEALS, m);
                } else {
                    free(m);
                }
            }
            if (g_cfg.ical[0] && (force || new_day || now - last_cal > 15 * MIN)) {
                calendar_t *c = psram(sizeof(*c));
                if (c && ical_fetch(g_cfg.ical, c, 14)) {
                    last_cal = now;
                    feeds_swap(D_MYCAL, c);
                } else {
                    free(c);
                }
            }
            if (g_cfg.sched_url[0] && (force || new_day || now - last_school > 60 * MIN)) {
                school_t *s = psram(sizeof(*s));
                if (s && school_fetch(g_cfg.sched_url, s)) {
                    last_school = now;
                    feeds_swap(D_SCHOOL, s);
                } else {
                    free(s);
                }
            }
            if (g_cfg.cha_url[0] && (force || new_day || now - last_cha > 60 * MIN)) {
                cha_t *c = psram(sizeof(*c));
                if (c && cha_fetch(g_cfg.cha_url, c)) {
                    last_cha = now;
                    feeds_swap(D_CHA, c);
                } else {
                    free(c);
                }
            }
            if (force) {
                ESP_LOGI(TAG, "모두 받음, 남은 메모리 %u / PSRAM %u",
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
                ui_set_updated(now);
            }
            force = false;
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
