/* 학교 일정 웹앱과 창체 계획 읽기 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "feeds.h"
#include "net.h"

static const char *TAG = "school";

static int parse_ymd(const char *s)
{
    int y, m, d;
    if (s && sscanf(s, "%d-%d-%d", &y, &m, &d) == 3) {
        return y * 10000 + m * 100 + d;
    }
    return 0;
}

static void put_utf8(char **o, unsigned c)
{
    char *p = *o;
    if (c < 0x80) {
        *p++ = c;
    } else if (c < 0x800) {
        *p++ = 0xC0 | (c >> 6);
        *p++ = 0x80 | (c & 0x3F);
    } else {
        *p++ = 0xE0 | (c >> 12);
        *p++ = 0x80 | ((c >> 6) & 0x3F);
        *p++ = 0x80 | (c & 0x3F);
    }
    *o = p;
}

/* JS 문자열 "..." 의 내용을 풀어 dst 에 씁니다(같은 버퍼도 됨, 결과가 더 짧음).
 * src 는 여는 따옴표 바로 다음. 닫는 따옴표에서 멈춥니다. */
static size_t js_unescape(const char *src, char *dst)
{
    char *o = dst;
    while (*src && *src != '"') {
        if (*src != '\\') {
            *o++ = *src++;
            continue;
        }
        char n = src[1];
        if (n == 'x' && isxdigit((unsigned char)src[2]) && isxdigit((unsigned char)src[3])) {
            char hex[3] = { src[2], src[3], 0 };
            put_utf8(&o, strtoul(hex, NULL, 16));
            src += 4;
        } else if (n == 'u') {
            char hex[5] = { 0 };
            memcpy(hex, src + 2, 4);
            put_utf8(&o, strtoul(hex, NULL, 16));
            src += 6;
        } else if (n) {
            *o++ = n == 'n' ? '\n' : n == 'r' ? '\r' : n == 't' ? '\t' : n;
            src += 2;
        } else {
            break;
        }
    }
    *o = 0;
    return o - dst;
}

static const char *str_of(const cJSON *obj, const char *key)
{
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItem(obj, key));
    return s ? s : "";
}

/* 웹앱 화면(HTML) 안에 들어 있는 <script id="init-data"> JSON 을 꺼냅니다.
 *   goog.script.init("…") 의 JS 문자열 → JSON → userHtml → init-data */
bool school_fetch(const char *url, school_t *out)
{
    char *page = http_get_text(url, 512 * 1024);   /* 보통 200KB 쯤 */
    if (!page) {
        ESP_LOGW(TAG, "학교 일정을 받지 못했습니다");
        return false;
    }
    bool ok = false;
    cJSON *outer = NULL, *data = NULL;
    char *p = strstr(page, "goog.script.init(\"");
    if (!p) {
        ESP_LOGW(TAG, "학교 일정 웹앱 모양이 아닙니다");
        goto done;
    }
    js_unescape(p + strlen("goog.script.init(\""), page);
    outer = cJSON_Parse(page);
    const char *html = cJSON_GetStringValue(cJSON_GetObjectItem(outer, "userHtml"));
    const char *a = html ? strstr(html, "id=\"init-data\"") : NULL;
    a = a ? strchr(a, '>') : NULL;
    const char *b = a ? strstr(a, "</script") : NULL;
    if (!b) {
        ESP_LOGW(TAG, "학교 일정 자료를 찾지 못했습니다 (받은 %u바이트, JSON %s, 큰 빈칸 %u)", (unsigned)strlen(p),
                 outer ? "풀림" : "못 풂", (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        goto done;
    }
    data = cJSON_ParseWithLength(a + 1, b - a - 1);
    if (!data) {
        goto done;
    }

    memset(out, 0, sizeof(*out));
    const cJSON *row;
    cJSON_ArrayForEach(row, cJSON_GetObjectItem(data, "academic")) {
        if (out->acad_count >= MAX_ACAD) {
            break;
        }
        acad_t *x = &out->acad[out->acad_count];
        x->ymd = parse_ymd(str_of(row, "date"));
        if (!x->ymd) {
            continue;
        }
        strlcpy(x->name, str_of(row, "name"), sizeof(x->name));
        const char *type = str_of(row, "type");
        strlcpy(x->type, !strcmp(type, "토요") ? "재량휴업일" : type[0] ? type : "행사", sizeof(x->type));
        out->acad_count++;
    }
    cJSON_ArrayForEach(row, cJSON_GetObjectItem(data, "schedules")) {
        if (out->sched_count >= MAX_SCHED) {
            break;
        }
        sched_t *x = &out->sched[out->sched_count];
        x->start = parse_ymd(str_of(row, "startDate"));
        x->end = parse_ymd(str_of(row, "endDate"));
        if (!x->start) {
            continue;
        }
        if (x->end < x->start) {
            x->end = x->start;
        }
        strlcpy(x->dept, str_of(row, "dept"), sizeof(x->dept));
        strlcpy(x->title, str_of(row, "title"), sizeof(x->title));
        strlcpy(x->time, str_of(row, "time"), sizeof(x->time));
        out->sched_count++;
    }
    out->ok = true;
    ok = true;
    ESP_LOGI(TAG, "학사 일정 %d, 부서 일정 %d", out->acad_count, out->sched_count);
done:
    cJSON_Delete(data);
    cJSON_Delete(outer);
    free(page);
    return ok;
}

/* ---------------- 창체 계획 ---------------- */

/* 구글 시트 주소를 CSV 내려받기 주소로 바꿉니다. */
static char *sheet_csv_url(const char *url)
{
    const char *d = strstr(url, "/spreadsheets/d/");
    if (!d) {
        return strdup(url);
    }
    d += strlen("/spreadsheets/d/");
    size_t id_len = strcspn(d, "/?#");
    const char *gid = strstr(url, "gid=");
    char *out;
    asprintf(&out, "https://docs.google.com/spreadsheets/d/%.*s/export?format=csv%s%.*s", (int)id_len, d,
             gid ? "&gid=" : "", gid ? (int)strcspn(gid + 4, "&#") : 0, gid ? gid + 4 : "");
    return out;
}

static void trim_copy(char *dst, size_t size, const char *s, size_t n)
{
    while (n && isspace((unsigned char)*s)) {
        s++, n--;
    }
    while (n && isspace((unsigned char)s[n - 1])) {
        n--;
    }
    size_t k = n < size - 1 ? n : size - 1;
    memcpy(dst, s, k);
    dst[k] = 0;
    /* 잘린 한글 조각 지우기 */
    if (k < n) {
        while (k > 0 && ((unsigned char)dst[k - 1] & 0xC0) == 0x80) {
            k--;
        }
        if (k > 0 && ((unsigned char)dst[k - 1] & 0xC0) == 0xC0) {
            k--;
        }
        dst[k] = 0;
    }
}

/* 따옴표를 지키는 CSV 읽기. 칸 안의 줄바꿈은 빈칸으로 바꿉니다. */
static void parse_csv(char *csv, cha_t *out)
{
    char *p = csv;
    int row = -1; /* -1 = 머리줄 */
    char field[256];
    while (*p && out->rows < CHA_ROWS) {
        int col = 0;
        bool any = false;
        char *cells[CHA_COLS];
        static char buf[CHA_COLS][256];
        for (;;) {
            size_t n = 0;
            if (*p == '"') {
                p++;
                while (*p) {
                    if (*p == '"' && p[1] == '"') {
                        p += 2;
                        if (n < sizeof(field) - 1) field[n++] = '"';
                    } else if (*p == '"') {
                        p++;
                        break;
                    } else {
                        if (n < sizeof(field) - 1) field[n++] = (*p == '\n' || *p == '\r') ? ' ' : *p;
                        p++;
                    }
                }
                while (*p && *p != ',' && *p != '\n') p++;
            } else {
                while (*p && *p != ',' && *p != '\n' && *p != '\r') {
                    if (n < sizeof(field) - 1) field[n++] = *p;
                    p++;
                }
            }
            field[n] = 0;
            if (col < CHA_COLS) {
                trim_copy(buf[col], sizeof(buf[col]), field, n);
                cells[col] = buf[col];
                any |= buf[col][0] != 0;
            }
            col++;
            if (*p == '\r') p++;
            if (*p == ',') {
                p++;
                continue;
            }
            if (*p == '\n') p++;
            break;
        }
        int cols = col < CHA_COLS ? col : CHA_COLS;
        if (!any) {
            continue;
        }
        if (row < 0) {
            out->cols = cols;
            for (int c = 0; c < cols; c++) {
                strlcpy(out->head[c], cells[c], sizeof(out->head[c]));
            }
        } else {
            for (int c = 0; c < out->cols && c < cols; c++) {
                strlcpy(out->cell[out->rows][c], cells[c], sizeof(out->cell[0][0]));
            }
            out->rows++;
        }
        row++;
    }
}

/* '월'·'일' 칸이 있으면 각 줄의 날짜를 셉니다 (3월~다음 해 2월을 한 학년도로). */
static void cha_dates(cha_t *out)
{
    int cm = -1, cd = -1;
    for (int c = 0; c < out->cols; c++) {
        if (!strcmp(out->head[c], "월")) cm = c;
        if (!strcmp(out->head[c], "일")) cd = c;
    }
    if (cm < 0 || cd < 0) {
        return;
    }
    int today = ymd_today();
    int year = today / 10000, month = today / 100 % 100;
    int school_year = month >= 3 ? year : year - 1;
    int last_m = 0;
    for (int r = 0; r < out->rows; r++) {
        int m = atoi(out->cell[r][cm]), d = atoi(out->cell[r][cd]);
        if (m == 0) {
            m = last_m; /* 월 칸을 합쳐 둔 시트: 위 줄의 월을 씁니다 */
        }
        last_m = m;
        if (m >= 1 && m <= 12 && d >= 1 && d <= 31) {
            out->ymd[r] = (m >= 3 ? school_year : school_year + 1) * 10000 + m * 100 + d;
        }
    }
}

bool cha_fetch(const char *url, cha_t *out)
{
    memset(out, 0, sizeof(*out));
    if (strstr(url, "/ical/") || strstr(url, ".ics") || !strncmp(url, "webcal://", 9)) {
        out->is_cal = true;
        out->ok = ical_fetch(url, &out->cal, 60);
        return out->ok;
    }
    char *csv_url = sheet_csv_url(url);
    char *csv = http_get_text(csv_url, 256 * 1024);
    free(csv_url);
    if (!csv) {
        ESP_LOGW(TAG, "창체 계획을 받지 못했습니다 (시트 공유를 '링크가 있는 모든 사용자'로 했는지 확인)");
        return false;
    }
    if (strstr(csv, "<html") || strstr(csv, "<!DOCTYPE")) {
        ESP_LOGW(TAG, "창체 시트가 공개되어 있지 않습니다");
        free(csv);
        return false;
    }
    parse_csv(csv, out);
    free(csv);
    cha_dates(out);
    out->ok = out->cols > 0;
    ESP_LOGI(TAG, "창체 %d줄 %d칸", out->rows, out->cols);
    return out->ok;
}
