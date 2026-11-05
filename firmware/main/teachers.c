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

/* ---------------- 시간표 바꾸기 (교체 · 보강) ----------------
 *  {"c":[{"n":"이상민","d":20261105,"p":2,"k":2,"s":"국어","r":"301"}, …]}
 *  k: 1 = 옮겨 가서 빈 칸, 2 = 옮겨 온 수업, 3 = 보강 */

#define CHG_MAX 64
#define CHG_KEEP_DAYS 14   /* 이보다 오래된 것은 저장할 때 버림 */

typedef struct {
    char name[32];
    int ymd;
    int8_t p, kind;
    char subj[28];
    char room[12];
} tt_change_t;

static tt_change_t s_chg[CHG_MAX];
static int s_chg_n = -1;   /* -1 = 아직 안 읽음 */

static void chg_load(void)
{
    if (s_chg_n >= 0) {
        return;
    }
    s_chg_n = 0;
    char *json = storage_read("ttchg", NULL);
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    free(json);
    const cJSON *item;
    cJSON_ArrayForEach(item, cJSON_GetObjectItem(root, "c")) {
        if (s_chg_n >= CHG_MAX) {
            break;
        }
        tt_change_t *c = &s_chg[s_chg_n];
        const char *n = cJSON_GetStringValue(cJSON_GetObjectItem(item, "n"));
        const char *s = cJSON_GetStringValue(cJSON_GetObjectItem(item, "s"));
        const char *r = cJSON_GetStringValue(cJSON_GetObjectItem(item, "r"));
        c->ymd = cJSON_GetNumberValue(cJSON_GetObjectItem(item, "d"));
        c->p = cJSON_GetNumberValue(cJSON_GetObjectItem(item, "p"));
        c->kind = cJSON_GetNumberValue(cJSON_GetObjectItem(item, "k"));
        if (!n || c->p < 0 || c->p >= TT_PERIODS || c->kind < TT_MOVED_OUT || c->kind > TT_EXTRA) {
            continue;
        }
        strlcpy(c->name, n, sizeof(c->name));
        strlcpy(c->subj, s ? s : "", sizeof(c->subj));
        strlcpy(c->room, r ? r : "", sizeof(c->room));
        s_chg_n++;
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "시간표 바꾼 칸 %d개", s_chg_n);
}

static void chg_save(void)
{
    int keep_from = ymd_add(ymd_today(), -CHG_KEEP_DAYS);
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "c");
    for (int i = 0; i < s_chg_n; i++) {
        const tt_change_t *c = &s_chg[i];
        if (c->ymd < keep_from && ymd_today() > 20250101) {
            continue;
        }
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "n", c->name);
        cJSON_AddNumberToObject(o, "d", c->ymd);
        cJSON_AddNumberToObject(o, "p", c->p);
        cJSON_AddNumberToObject(o, "k", c->kind);
        cJSON_AddStringToObject(o, "s", c->subj);
        cJSON_AddStringToObject(o, "r", c->room);
        cJSON_AddItemToArray(arr, o);
    }
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (out) {
        storage_write("ttchg", out, strlen(out));
        cJSON_free(out);
    }
}

static const teacher_t *chg_teacher(int ti)
{
    const teachers_t *ts = g_data.teachers;
    return ts && ti >= 0 && ti < ts->count ? &ts->t[ti] : NULL;
}

static tt_change_t *chg_find(const char *name, int ymd, int p)
{
    chg_load();
    for (int i = 0; i < s_chg_n; i++) {
        if (s_chg[i].ymd == ymd && s_chg[i].p == p && !strcmp(s_chg[i].name, name)) {
            return &s_chg[i];
        }
    }
    return NULL;
}

static void chg_remove(const char *name, int ymd, int p)
{
    tt_change_t *c = chg_find(name, ymd, p);
    if (c) {
        *c = s_chg[--s_chg_n];
    }
}

/* 원래 시간표의 그 칸 (주말이면 없음) */
static bool base_lesson(const teacher_t *t, int ymd, int p, const char **subj, const char **room)
{
    int d = ymd_wday(ymd) - 1;
    if (d < 0 || d >= TT_DAYS || p < 0 || p >= TT_PERIODS) {
        *subj = *room = "";
        return false;
    }
    *subj = t->subj[d][p];
    *room = t->room[d][p];
    return t->subj[d][p][0] != 0;
}

bool tt_lesson(int ti, int ymd, int p, const char **subj, const char **room, int *kind)
{
    const char *s, *r;
    int k = TT_NORMAL;
    const teacher_t *t = chg_teacher(ti);
    if (!t) {
        s = r = "";
        if (subj) *subj = s;
        if (room) *room = r;
        if (kind) *kind = k;
        return false;
    }
    bool has = base_lesson(t, ymd, p, &s, &r);
    const tt_change_t *c = chg_find(t->name, ymd, p);
    if (c) {
        k = c->kind;
        if (k == TT_MOVED_OUT) {
            has = false;
        } else {
            s = c->subj;
            r = c->room;
            has = true;
        }
    }
    if (subj) *subj = s;
    if (room) *room = r;
    if (kind) *kind = k;
    return has;
}

/* 칸을 has·subj·room 으로 (원래와 같으면 덮어쓴 것을 지움) */
static void set_cell(const teacher_t *t, int ymd, int p, bool has, const char *subj, const char *room, int kind)
{
    const char *bs, *br;
    bool base = base_lesson(t, ymd, p, &bs, &br);
    chg_remove(t->name, ymd, p);
    if (has ? base && !strcmp(bs, subj) && !strcmp(br, room) : !base) {
        return;   /* 원래대로 */
    }
    if (s_chg_n >= CHG_MAX) {
        /* 가장 오래된 날짜 하나를 버림 */
        int old = 0;
        for (int i = 1; i < s_chg_n; i++) {
            old = s_chg[i].ymd < s_chg[old].ymd ? i : old;
        }
        s_chg[old] = s_chg[--s_chg_n];
    }
    tt_change_t *c = &s_chg[s_chg_n++];
    memset(c, 0, sizeof(*c));
    strlcpy(c->name, t->name, sizeof(c->name));
    c->ymd = ymd;
    c->p = p;
    c->kind = has ? kind : TT_MOVED_OUT;
    if (has) {
        strlcpy(c->subj, subj, sizeof(c->subj));
        strlcpy(c->room, room, sizeof(c->room));
    }
}

void tt_move(int ti, int from_ymd, int from_p, int to_ymd, int to_p)
{
    const teacher_t *t = chg_teacher(ti);
    if (!t || (from_ymd == to_ymd && from_p == to_p)) {
        return;
    }
    const char *s, *r;
    int ka, kb;
    char as[28], ar[12], bs[28], br[12];
    bool a = tt_lesson(ti, from_ymd, from_p, &s, &r, &ka);
    strlcpy(as, s, sizeof(as));
    strlcpy(ar, r, sizeof(ar));
    if (!a) {
        return;
    }
    bool b = tt_lesson(ti, to_ymd, to_p, &s, &r, &kb);
    strlcpy(bs, s, sizeof(bs));
    strlcpy(br, r, sizeof(br));
    /* 보강은 옮겨도 보강, 나머지는 '옮겨 온 수업' */
    set_cell(t, to_ymd, to_p, true, as, ar, ka == TT_EXTRA ? TT_EXTRA : TT_MOVED_IN);
    set_cell(t, from_ymd, from_p, b, bs, br, kb == TT_EXTRA ? TT_EXTRA : TT_MOVED_IN);
    ESP_LOGI(TAG, "%s: %d %d교시 → %d %d교시%s", t->name, from_ymd, from_p + 1, to_ymd, to_p + 1, b ? " (맞바꿈)" : "");
    chg_save();
}

void tt_add_extra(int ti, int ymd, int p, const char *subj, const char *room)
{
    const teacher_t *t = chg_teacher(ti);
    if (!t) {
        return;
    }
    set_cell(t, ymd, p, true, subj, room, TT_EXTRA);
    ESP_LOGI(TAG, "%s: %d %d교시 보강 %s %s", t->name, ymd, p + 1, subj, room);
    chg_save();
}

void tt_restore(int ti, int ymd, int p)
{
    const teacher_t *t = chg_teacher(ti);
    if (t && chg_find(t->name, ymd, p)) {
        chg_remove(t->name, ymd, p);
        chg_save();
    }
}

int tt_week_changes(int ti, int monday)
{
    const teacher_t *t = chg_teacher(ti);
    int n = 0;
    chg_load();
    for (int i = 0; t && i < s_chg_n; i++) {
        int k = ymd_diff(s_chg[i].ymd, monday);
        n += k >= 0 && k < TT_DAYS && !strcmp(s_chg[i].name, t->name);
    }
    return n;
}

void tt_restore_week(int ti, int monday)
{
    const teacher_t *t = chg_teacher(ti);
    chg_load();
    for (int i = 0; t && i < s_chg_n;) {
        int k = ymd_diff(s_chg[i].ymd, monday);
        if (k >= 0 && k < TT_DAYS && !strcmp(s_chg[i].name, t->name)) {
            s_chg[i] = s_chg[--s_chg_n];
        } else {
            i++;
        }
    }
    chg_save();
}
