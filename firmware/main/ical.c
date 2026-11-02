/* 구글 캘린더 iCal(.ics) 을 한 줄씩 읽어서 오늘부터 7일 치 일정을 뽑습니다.
 * 반복 일정은 매일·매주(요일)·매달(날짜 / n번째 요일)·매년을 지원합니다. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "feeds.h"
#include "net.h"

static const char *TAG = "ical";

#define MAX_FOUND 128
#define MAX_EXDATES 16
#define MAX_OVERRIDES 32

/* ---- 날짜 계산 (1970-01-01 부터 며칠째인지) ---- */

static int days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int z, int *y, int *m, int *d)
{
    z += 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = yoe + era * 400 + (*m <= 2);
}

static int weekday(int day) /* 0 = 일요일 */
{
    return ((day % 7) + 11) % 7;
}

/* "20261102" / "20261102T050000Z" / "20261102T140000" -> 한국 날짜와 분 */
static bool parse_dt(const char *v, int *day, int *minute)
{
    int y, mo, d, h = 0, mi = 0;
    if (sscanf(v, "%4d%2d%2d", &y, &mo, &d) != 3) {
        return false;
    }
    if (v[8] != 'T') {
        *day = days_from_civil(y, mo, d);
        *minute = -1;
        return true;
    }
    sscanf(v + 9, "%2d%2d", &h, &mi);
    long total = (long)days_from_civil(y, mo, d) * 1440 + h * 60 + mi;
    if (strchr(v, 'Z')) {
        total += 9 * 60; /* UTC -> 한국 시간 */
    }
    *day = (int)(total / 1440);
    *minute = (int)(total % 1440);
    return true;
}

/* ---- 읽는 중인 일정 하나 ---- */

enum { F_NONE, F_DAILY, F_WEEKLY, F_MONTHLY, F_YEARLY };

typedef struct {
    int start_day, start_min, end_day;
    bool has_end;
    char title[96];
    unsigned uid;
    bool cancelled;
    bool is_override;
    int override_day;
    int freq, interval, count, until_day;
    int byday_mask;  /* 비트 0 = 일요일 */
    int byday_nth;   /* 매달 n번째 요일 (0 = 없음, -1 = 마지막) */
    int exdates[MAX_EXDATES];
    int exdate_count;
} vevent_t;

typedef struct {
    cal_event_t ev;
    int abs_day;
    unsigned uid;
    bool from_rule;
} found_t;

typedef struct {
    int today;
    int window;
    bool in_event;
    vevent_t e;
    char cur[1024];
    size_t cur_len;
    found_t found[MAX_FOUND];
    int found_count;
    struct { unsigned uid; int day; } overrides[MAX_OVERRIDES];
    int override_count;
} parser_t;

static unsigned hash(const char *s)
{
    unsigned h = 5381;
    while (*s) {
        h = h * 33 + (unsigned char)*s++;
    }
    return h;
}

static void copy_text(char *dst, size_t size, const char *src)
{
    size_t o = 0;
    while (*src && o < size - 1) {
        if (*src == '\\' && src[1]) {
            src++;
            dst[o++] = (*src == 'n' || *src == 'N') ? ' ' : *src;
            src++;
        } else {
            dst[o++] = *src++;
        }
    }
    if (*src) {
        /* 잘렸으면 마지막 글자를 통째로 버려서 한글이 깨지지 않게 합니다 */
        while (o > 0 && ((unsigned char)dst[o - 1] & 0xC0) == 0x80) {
            o--;
        }
        if (o > 0 && ((unsigned char)dst[o - 1] & 0xC0) == 0xC0) {
            o--;
        }
    }
    dst[o] = 0;
}

static void parse_rrule(vevent_t *e, char *v)
{
    e->interval = 1;
    char *save;
    for (char *tok = strtok_r(v, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
        if (!strncmp(tok, "FREQ=", 5)) {
            const char *f = tok + 5;
            e->freq = !strcmp(f, "DAILY") ? F_DAILY : !strcmp(f, "WEEKLY") ? F_WEEKLY
                    : !strcmp(f, "MONTHLY") ? F_MONTHLY : !strcmp(f, "YEARLY") ? F_YEARLY : F_NONE;
        } else if (!strncmp(tok, "INTERVAL=", 9)) {
            e->interval = atoi(tok + 9) > 0 ? atoi(tok + 9) : 1;
        } else if (!strncmp(tok, "COUNT=", 6)) {
            e->count = atoi(tok + 6);
        } else if (!strncmp(tok, "UNTIL=", 6)) {
            int d, m;
            if (parse_dt(tok + 6, &d, &m)) {
                e->until_day = d;
            }
        } else if (!strncmp(tok, "BYDAY=", 6)) {
            static const char *names[] = { "SU", "MO", "TU", "WE", "TH", "FR", "SA" };
            for (char *p = tok + 6; *p;) {
                int nth = strtol(p, &p, 10);
                for (int i = 0; i < 7; i++) {
                    if (!strncmp(p, names[i], 2)) {
                        e->byday_mask |= 1 << i;
                    }
                }
                if (nth) {
                    e->byday_nth = nth;
                }
                while (*p && *p != ',') {
                    p++;
                }
                if (*p == ',') {
                    p++;
                }
            }
        }
    }
}

static void add_found(parser_t *p, int day, const vevent_t *e, bool from_rule)
{
    if (day < p->today || day >= p->today + p->window || p->found_count >= MAX_FOUND) {
        return;
    }
    found_t *f = &p->found[p->found_count++];
    f->abs_day = day;
    f->uid = e->uid;
    f->from_rule = from_rule;
    f->ev.day = day - p->today;
    f->ev.minute = e->start_min;
    strlcpy(f->ev.title, e->title[0] ? e->title : "(제목 없음)", sizeof(f->ev.title));
}

static int count_bits(int v)
{
    int n = 0;
    for (; v; v >>= 1) {
        n += v & 1;
    }
    return n;
}

/* day 가 반복 규칙에 맞으면 몇 번째 반복인지(0부터), 아니면 -1 */
static int occurrence_index(const vevent_t *e, int day)
{
    int diff = day - e->start_day;
    if (diff < 0) {
        return -1;
    }
    int y, m, d, sy, sm, sd;
    civil_from_days(day, &y, &m, &d);
    civil_from_days(e->start_day, &sy, &sm, &sd);

    switch (e->freq) {
    case F_DAILY:
        return diff % e->interval == 0 ? diff / e->interval : -1;
    case F_WEEKLY: {
        int mask = e->byday_mask ? e->byday_mask : 1 << weekday(e->start_day);
        int wd = weekday(day);
        if (!(mask & (1 << wd))) {
            return -1;
        }
        /* 주는 월요일부터 */
        int monday = day - (wd + 6) % 7;
        int start_monday = e->start_day - (weekday(e->start_day) + 6) % 7;
        int weeks = (monday - start_monday) / 7;
        if (weeks % e->interval) {
            return -1;
        }
        int before = 0;
        for (int i = 0; i < (wd + 6) % 7; i++) {
            before += (mask >> ((i + 1) % 7)) & 1;
        }
        int skipped_first = 0;
        for (int i = 0; i < (weekday(e->start_day) + 6) % 7; i++) {
            skipped_first += (mask >> ((i + 1) % 7)) & 1;
        }
        return (weeks / e->interval) * count_bits(mask) + before - skipped_first;
    }
    case F_MONTHLY: {
        int months = (y * 12 + m) - (sy * 12 + sm);
        if (months % e->interval) {
            return -1;
        }
        if (e->byday_nth && e->byday_mask) {
            if (!(e->byday_mask & (1 << weekday(day)))) {
                return -1;
            }
            int nth = e->byday_nth > 0 ? (d - 1) / 7 + 1 : 0;
            if (e->byday_nth < 0) {
                int ny, nm, nd;
                civil_from_days(day + 7, &ny, &nm, &nd);
                nth = nm != m ? -1 : 0;
            }
            return nth == e->byday_nth ? months / e->interval : -1;
        }
        return d == sd ? months / e->interval : -1;
    }
    case F_YEARLY:
        return (m == sm && d == sd && (y - sy) % e->interval == 0) ? (y - sy) / e->interval : -1;
    }
    return -1;
}

static void finish_event(parser_t *p)
{
    vevent_t *e = &p->e;
    if (e->cancelled || e->start_day == INT_MIN) {
        return;
    }
    if (e->is_override && p->override_count < MAX_OVERRIDES) {
        p->overrides[p->override_count].uid = e->uid;
        p->overrides[p->override_count].day = e->override_day;
        p->override_count++;
    }
    if (e->freq == F_NONE) {
        /* 여러 날 이어지는 종일 일정은 이어지는 날마다 보여 줍니다 */
        int last = (e->start_min < 0 && e->has_end && e->end_day > e->start_day) ? e->end_day - 1 : e->start_day;
        for (int d = e->start_day; d <= last; d++) {
            add_found(p, d, e, false);
        }
        return;
    }
    for (int d = p->today; d < p->today + p->window; d++) {
        if (d > e->until_day) {
            break;
        }
        int idx = occurrence_index(e, d);
        if (idx < 0 || (e->count && idx >= e->count)) {
            continue;
        }
        bool excluded = false;
        for (int i = 0; i < e->exdate_count; i++) {
            excluded |= e->exdates[i] == d;
        }
        if (!excluded) {
            add_found(p, d, e, true);
        }
    }
}

static void handle_property(parser_t *p, char *line)
{
    if (!strcmp(line, "BEGIN:VEVENT")) {
        p->in_event = true;
        memset(&p->e, 0, sizeof(p->e));
        p->e.start_day = INT_MIN;
        p->e.until_day = INT_MAX;
        return;
    }
    if (!p->in_event) {
        return;
    }
    if (!strcmp(line, "END:VEVENT")) {
        finish_event(p);
        p->in_event = false;
        return;
    }
    char *colon = strchr(line, ':');
    if (!colon) {
        return;
    }
    *colon = 0;
    char *value = colon + 1;
    char *semi = strchr(line, ';');
    if (semi) {
        *semi = 0; /* 이름 뒤의 매개변수(TZID 등)는 버림: 시간대는 한국으로 봅니다 */
    }
    vevent_t *e = &p->e;
    if (!strcmp(line, "DTSTART")) {
        parse_dt(value, &e->start_day, &e->start_min);
    } else if (!strcmp(line, "DTEND")) {
        int m;
        e->has_end = parse_dt(value, &e->end_day, &m);
    } else if (!strcmp(line, "SUMMARY")) {
        copy_text(e->title, sizeof(e->title), value);
    } else if (!strcmp(line, "UID")) {
        e->uid = hash(value);
    } else if (!strcmp(line, "STATUS")) {
        e->cancelled = !strcmp(value, "CANCELLED");
    } else if (!strcmp(line, "RRULE")) {
        parse_rrule(e, value);
    } else if (!strcmp(line, "RECURRENCE-ID")) {
        int m;
        e->is_override = parse_dt(value, &e->override_day, &m);
    } else if (!strcmp(line, "EXDATE")) {
        char *save;
        for (char *tok = strtok_r(value, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            int d, m;
            if (e->exdate_count < MAX_EXDATES && parse_dt(tok, &d, &m)) {
                e->exdates[e->exdate_count++] = d;
            }
        }
    }
}

static bool on_line(const char *line, void *ctx)
{
    parser_t *p = ctx;
    if (line[0] == ' ' || line[0] == '\t') {
        /* 앞 줄에 이어지는 줄 */
        size_t n = strlen(line + 1);
        if (p->cur_len + n < sizeof(p->cur)) {
            memcpy(p->cur + p->cur_len, line + 1, n + 1);
            p->cur_len += n;
        }
        return true;
    }
    if (p->cur_len) {
        handle_property(p, p->cur);
    }
    strlcpy(p->cur, line, sizeof(p->cur));
    p->cur_len = strlen(p->cur);
    return true;
}

static int compare_found(const void *a, const void *b)
{
    const found_t *x = a, *y = b;
    if (x->abs_day != y->abs_day) {
        return x->abs_day - y->abs_day;
    }
    return x->ev.minute - y->ev.minute;
}

bool ical_fetch(const char *url, calendar_t *out, int days)
{
    parser_t *p = heap_caps_calloc(1, sizeof(parser_t), MALLOC_CAP_SPIRAM);
    if (!p) {
        return false;
    }
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    p->today = days_from_civil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    p->window = days;

    /* webcal:// 로 시작하면 https:// 로 바꿉니다 */
    char *fixed = NULL;
    if (!strncmp(url, "webcal://", 9)) {
        asprintf(&fixed, "https://%s", url + 9);
        url = fixed;
    }
    esp_err_t err = http_get_lines(url, on_line, p);
    free(fixed);
    if (p->cur_len) {
        handle_property(p, p->cur);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "캘린더를 받지 못했습니다");
        free(p);
        return false;
    }

    /* 따로 고친 반복 일정은 원래 자리의 반복을 지웁니다 */
    int kept = 0;
    for (int i = 0; i < p->found_count; i++) {
        bool drop = false;
        for (int j = 0; j < p->override_count && p->found[i].from_rule; j++) {
            drop |= p->overrides[j].uid == p->found[i].uid && p->overrides[j].day == p->found[i].abs_day;
        }
        if (!drop) {
            p->found[kept++] = p->found[i];
        }
    }
    qsort(p->found, kept, sizeof(found_t), compare_found);

    out->count = kept < MAX_EVENTS ? kept : MAX_EVENTS;
    for (int i = 0; i < out->count; i++) {
        out->ev[i] = p->found[i].ev;
    }
    out->ok = true;
    ESP_LOGI(TAG, "일정 %d개", kept);
    free(p);
    return true;
}
