/* 교사 시간표 파일(/data/tt.json) 읽기
 *
 * 설치 페이지가 한글(HWP) 파일을 읽어 아래 모양으로 보내 줍니다.
 *   {"v":1,"times":["08:40",…7개],
 *    "t":[{"n":"김민식","c":["고전 Q|301", "", …]}, …]}
 *   c 는 월1교시, 월2교시 … 금7교시 순서의 35칸이고 "과목|교실" 입니다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "feeds.h"

static const char *TAG = "teachers";

static teachers_t *parse(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return NULL;
    }
    teachers_t *t = heap_caps_calloc(1, sizeof(teachers_t), MALLOC_CAP_SPIRAM);
    static const int DEFAULT_TIMES[TT_PERIODS] = { 520, 580, 640, 700, 810, 870, CLEAN_END };
    memcpy(t->ptime, DEFAULT_TIMES, sizeof(t->ptime));

    const cJSON *times = cJSON_GetObjectItem(root, "times");
    for (int p = 0; p < TT_PERIODS; p++) {
        int h, m;
        const char *s = cJSON_GetStringValue(cJSON_GetArrayItem(times, p));
        if (s && sscanf(s, "%d:%d", &h, &m) == 2) {
            t->ptime[p] = h * 60 + m;
        }
    }
    /* 7교시는 청소 시간이 끝난 뒤에 시작합니다 (한글 파일에 15:30 으로 적혀 있어도) */
    if (t->ptime[TT_PERIODS - 1] < CLEAN_END) {
        t->ptime[TT_PERIODS - 1] = CLEAN_END;
    }
    const cJSON *item;
    cJSON_ArrayForEach(item, cJSON_GetObjectItem(root, "t")) {
        if (t->count >= MAX_TEACHERS) {
            break;
        }
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(item, "n"));
        if (!name || !name[0]) {
            continue;
        }
        teacher_t *x = &t->t[t->count++];
        strlcpy(x->name, name, sizeof(x->name));
        const cJSON *cells = cJSON_GetObjectItem(item, "c");
        for (int i = 0; i < TT_DAYS * TT_PERIODS; i++) {
            const char *c = cJSON_GetStringValue(cJSON_GetArrayItem(cells, i));
            if (!c || !c[0]) {
                continue;
            }
            int d = i / TT_PERIODS, p = i % TT_PERIODS;
            const char *bar = strchr(c, '|');
            size_t n = bar ? (size_t)(bar - c) : strlen(c);
            if (n >= sizeof(x->subj[0][0])) {
                n = sizeof(x->subj[0][0]) - 1;
            }
            memcpy(x->subj[d][p], c, n);
            x->subj[d][p][n] = 0;
            if (bar) {
                strlcpy(x->room[d][p], bar + 1, sizeof(x->room[0][0]));
            }
        }
    }
    cJSON_Delete(root);
    return t;
}

void teachers_load(void)
{
    char *json = storage_read("tt", NULL);
    if (!json) {
        ESP_LOGI(TAG, "교사 시간표 파일 없음");
        return;
    }
    teachers_t *t = parse(json);
    free(json);
    if (t) {
        ESP_LOGI(TAG, "교사 %d명", t->count);
        feeds_swap(D_TEACHERS, t);
    }
}

void teachers_reload(void)
{
    teachers_load();
}

int teachers_find(const char *name)
{
    const teachers_t *t = g_data.teachers;
    if (!t || !name || !name[0]) {
        return -1;
    }
    for (int i = 0; i < t->count; i++) {
        if (!strcmp(t->t[i].name, name)) {
            return i;
        }
    }
    return -1;
}
