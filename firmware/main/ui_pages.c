/* 달력 · 목록 화면들: 학교일정(한 달) · 창체계획 · 캘린더(5주) · 급식
 *
 *  달력 칸에는 일정 몇 줄과 "+n건 더" (학교일정은 건수만).
 *  칸을 누르면 그날 일정을 다 보여 줍니다 (학교일정은 오른쪽 칸에, 나머지는 창으로).
 */
#include <stdlib.h>

#include "ui_priv.h"

#define INNER_W (SCR_W - PAD * 2 - 28)   /* 큰 카드 안쪽 너비 */
#define LIST_H (BODY_H - 28 - 38)

static lv_obj_t *s_page_school, *s_cha, *s_cha_table, *s_cha_hint, *s_cal, *s_cal_title, *s_meal_list;

static lv_obj_t *page_card(lv_obj_t *page, const char *title, uint32_t color)
{
    lv_obj_t *c = mk_card(page, PAD, BODY_Y, SCR_W - PAD * 2, BODY_H);
    mk_label(c, &font_kr_22, color, title);
    return c;
}

/* 카드 제목 오른쪽의 작은 안내 글자 */
static lv_obj_t *page_hint(lv_obj_t *card)
{
    lv_obj_t *l = mk_label(card, &font_kr_18, C_DIM, "");
    lv_obj_align(l, LV_ALIGN_TOP_RIGHT, 0, 2);
    return l;
}

static uint32_t acad_color(const char *type)
{
    if (!strcmp(type, "공휴일") || !strcmp(type, "재량휴업일") || !strcmp(type, "방학")) return C_PINK;
    if (!strcmp(type, "지필") || !strcmp(type, "시험")) return 0xC792EA;
    if (!strcmp(type, "행사")) return C_GREEN;
    return C_WARM;
}

/* 창체 일정으로 볼 낱말 */
static const char *CHA_WORDS[] = { "창체", "동아리", "자율", "진로", "봉사", "창의적" };

static bool is_cha(const char *t)
{
    for (size_t i = 0; i < sizeof(CHA_WORDS) / sizeof(CHA_WORDS[0]); i++) {
        if (strstr(t, CHA_WORDS[i])) {
            return true;
        }
    }
    return false;
}

/* ---------------- 하루 일정 글자 (자세히) ---------------- */

enum { GK_SCHOOL, GK_CHA, GK_CAL };

static size_t add_line(char *buf, size_t o, size_t size, uint32_t color, const char *tag, const char *text)
{
    if (o >= size - 8) {
        return o;
    }
    if (tag && tag[0]) {
        o += snprintf(buf + o, size - o, "%s#%06lX [%s]# %s", o ? "\n" : "", (unsigned long)color, tag, text);
    } else {
        o += snprintf(buf + o, size - o, "%s#%06lX ·# %s", o ? "\n" : "", (unsigned long)color, text);
    }
    return o < size ? o : size - 1;
}

static size_t add_cal(char *buf, size_t o, size_t size, const calendar_t *cal, int ymd, uint32_t color)
{
    int today = ymd_today();
    for (int i = 0; cal && i < cal->count; i++) {
        const cal_event_t *ev = &cal->ev[i];
        if (ymd_add(today, ev->day) != ymd) {
            continue;
        }
        char t[16];
        if (ev->minute < 0) {
            strlcpy(t, "종일", sizeof(t));
        } else {
            snprintf(t, sizeof(t), "%d:%02d", ev->minute / 60, ev->minute % 60);
        }
        o = add_line(buf, o, size, color, t, ev->title);
    }
    return o;
}

/* 학교 일정: 학사 일정은 [종류], 부서 일정은 [부서] 와 시간. cha_only 면 창체 일정만. */
static size_t add_school(char *buf, size_t o, size_t size, int ymd, bool cha_only)
{
    const school_t *s = g_data.school;
    for (int i = 0; s && i < s->acad_count; i++) {
        if (s->acad[i].ymd == ymd && (!cha_only || is_cha(s->acad[i].name))) {
            o = add_line(buf, o, size, acad_color(s->acad[i].type), s->acad[i].type, s->acad[i].name);
        }
    }
    for (int i = 0; s && i < s->sched_count; i++) {
        const sched_t *x = &s->sched[i];
        if (x->start <= ymd && ymd <= x->end && (!cha_only || is_cha(x->title))) {
            char t[200];
            snprintf(t, sizeof(t), "%s%s%s", x->title, x->time[0] ? "  " : "", x->time);
            o = add_line(buf, o, size, C_SKY, x->dept[0] ? x->dept : "업무", t);
        }
    }
    return o;
}

static void day_text(int kind, int ymd, char *buf, size_t size)
{
    size_t o = 0;
    buf[0] = 0;
    if (kind == GK_SCHOOL) {
        o = add_school(buf, o, size, ymd, false);
    } else if (kind == GK_CAL) {
        o = add_cal(buf, o, size, g_data.mycal, ymd, C_SKY);
    } else {
        o = add_school(buf, o, size, ymd, true); /* 창체: 학교 일정 가운데 창체 일정만 (캘린더는 넣지 않음) */
    }
    if (!o) {
        strlcpy(buf, "#AEBBD2 일정이 없습니다.#", size);
    }
}

/* ---------------- 달력 표 ----------------
 *  weeks 주 × days 요일(월요일부터). 칸마다 날짜와 일정 몇 줄, 넘치면 "+n건 더". */

#define GRID_MAX 42
#define GRID_LINE_H 21
typedef struct {
    int first, weeks, days, lines, month, kind;
    int cw;
    bool count_only;                /* 칸에 일정 대신 "n건" 만 */
    lv_obj_t *cell[GRID_MAX];
    lv_obj_t *last[GRID_MAX];
    int used[GRID_MAX], extra[GRID_MAX];
} week_grid_t;

static int s_school_sel;   /* 학교일정에서 고른 날 */
void school_refresh(void);

static void on_cell(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    int kind = v / 100000000, ymd = v % 100000000;
    if (kind == GK_SCHOOL) {
        s_school_sel = ymd;
        school_refresh();
        return;
    }
    static char body[2048];
    char title[48];
    date_text(ymd, title, sizeof(title));
    day_text(kind, ymd, body, sizeof(body));
    ui_popup(title, body);
}

static void grid_build(week_grid_t *g, lv_obj_t *box, int w, int h, int first, int weeks, int days, int month,
                       int kind, int sel)
{
    /* 7칸이면 일요일부터, 5칸(창체)이면 월~금 */
    static const char *HEAD[7] = { "일", "월", "화", "수", "목", "금", "토" };
    const int wd0 = days == 7 ? 0 : 1;
    const int hh = 28, gap = 5;
    memset(g, 0, sizeof(*g));
    g->first = first;
    g->weeks = weeks;
    g->days = days;
    g->month = month;
    g->kind = kind;
    g->count_only = kind == GK_SCHOOL;
    g->cw = (w - gap * (days - 1)) / days;
    int ch = (h - hh - gap * (weeks - 1)) / weeks;
    g->lines = (ch - 28) / GRID_LINE_H;
    int today = ymd_today();
    for (int d = 0; d < days; d++) {
        int wd = wd0 + d;
        lv_obj_t *l = mk_label(box, &font_kr_18, wd == 0 ? C_PINK : wd == 6 ? C_BLUE : C_DIM, HEAD[wd]);
        lv_obj_set_width(l, g->cw);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(l, d * (g->cw + gap), 2);
    }
    for (int wk = 0; wk < weeks; wk++) {
        for (int d = 0; d < days; d++) {
            int ymd = ymd_add(first, wk * 7 + d);
            bool holiday;
            const acad_t *a = school_day_info(ymd, &holiday);
            bool out = month && ymd / 100 != month;
            lv_obj_t *c = mk_box(box, d * (g->cw + gap), hh + wk * (ch + gap), g->cw, ch);
            lv_obj_set_style_radius(c, 8, 0);
            lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(c, lv_color_hex(ymd == sel ? 0x3A5288 : C_CARD2), 0);
            lv_obj_set_style_border_width(c, ymd == today ? 3 : ymd == sel ? 2 : 1, 0);
            lv_obj_set_style_border_color(c, lv_color_hex(ymd == today ? C_RED : ymd == sel ? C_BLUE : C_LINE), 0);
            lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(c, on_cell, LV_EVENT_CLICKED, (void *)(intptr_t)(kind * 100000000 + ymd));
            if (out) {
                lv_obj_set_style_opa(c, LV_OPA_30, 0);
            } else if (ymd < today && !month) {
                lv_obj_set_style_opa(c, LV_OPA_50, 0);
            }
            char buf[24];
            int day = ymd % 100;
            if (!month && ((wk == 0 && d == 0) || day == 1)) {
                snprintf(buf, sizeof(buf), "%d/%d", ymd / 100 % 100, day);
            } else {
                snprintf(buf, sizeof(buf), "%d", day);
            }
            int wd = wd0 + d;
            uint32_t color = (wd == 0 || holiday) ? C_PINK : wd == 6 ? C_BLUE : C_TEXT;
            lv_obj_t *dl = mk_label(c, &font_kr_18, color, buf);
            lv_obj_set_pos(dl, 6, 2);
            if (ymd == today) {
                lv_obj_align_to(mk_label(c, &font_kr_18, C_RED, "오늘"), dl, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
            }
            int i = wk * days + d;
            g->cell[i] = c;
            /* 학교일정 달력은 휴일도 일정으로 들어가니 여기서는 빼고 */
            if (holiday && a && kind != GK_SCHOOL && !out && g->lines > 0) {
                lv_obj_t *hl = mk_label(c, &font_kr_18, C_PINK, a->name);
                lv_obj_set_width(hl, g->cw - 12);
                lv_label_set_long_mode(hl, LV_LABEL_LONG_DOT);
                lv_obj_set_pos(hl, 6, 26);
                g->used[i] = 1;
            }
        }
    }
}

static void grid_add(week_grid_t *g, int ymd, const char *text, uint32_t color)
{
    int k = ymd_diff(ymd, g->first);
    if (k < 0 || k >= g->weeks * 7 || k % 7 >= g->days || (g->month && ymd / 100 != g->month)) {
        return;
    }
    int i = k / 7 * g->days + k % 7;
    if (g->count_only || g->used[i] >= g->lines) {
        g->extra[i]++;
        return;
    }
    lv_obj_t *l = mk_label(g->cell[i], &font_kr_18, color, text);
    lv_obj_set_width(l, g->cw - 12);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(l, 6, 26 + g->used[i] * GRID_LINE_H);
    g->last[i] = l;
    g->used[i]++;
}

/* 넘친 칸은 마지막 줄을 "+n건 더" 로. 넣은 일정 수를 돌려줍니다. */
static int grid_finish(week_grid_t *g)
{
    int total = 0;
    for (int i = 0; i < g->weeks * g->days; i++) {
        total += g->used[i] + g->extra[i];
        if (g->count_only && g->extra[i]) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d건", g->extra[i]);
            lv_obj_t *l = mk_label(g->cell[i], &font_kr_22, C_SKY, buf);
            lv_obj_align(l, LV_ALIGN_CENTER, 0, 10);
        } else if (g->extra[i] && g->last[i]) {
            char buf[24];
            snprintf(buf, sizeof(buf), "+%d건 더", g->extra[i] + 1);
            lv_label_set_text(g->last[i], buf);
            lv_obj_set_style_text_color(g->last[i], lv_color_hex(C_DIM), 0);
        }
    }
    return total;
}

static void grid_add_cal(week_grid_t *g, const calendar_t *cal, uint32_t color)
{
    int today = ymd_today();
    for (int i = 0; cal && i < cal->count; i++) {
        const cal_event_t *ev = &cal->ev[i];
        char buf[112];
        if (ev->minute < 0) {
            strlcpy(buf, ev->title, sizeof(buf));
        } else {
            snprintf(buf, sizeof(buf), "%d:%02d %s", ev->minute / 60, ev->minute % 60, ev->title);
        }
        grid_add(g, ymd_add(today, ev->day), buf, color);
    }
}

/* 학교 일정을 달력에 (여러 날 이어지는 일정은 날마다) */
static void grid_add_school(week_grid_t *g, bool cha_only)
{
    const school_t *s = g_data.school;
    int last = ymd_add(g->first, g->weeks * 7 - 1);
    for (int i = 0; s && i < s->acad_count; i++) {
        if (!cha_only || is_cha(s->acad[i].name)) {
            grid_add(g, s->acad[i].ymd, s->acad[i].name, acad_color(s->acad[i].type));
        }
    }
    for (int i = 0; s && i < s->sched_count; i++) {
        const sched_t *x = &s->sched[i];
        if (cha_only && !is_cha(x->title)) {
            continue;
        }
        int y = x->start > g->first ? x->start : g->first;
        for (int n = 0; y <= x->end && y <= last && n < GRID_MAX; n++, y = ymd_add(y, 1)) {
            grid_add(g, y, x->title, C_TEXT);
        }
    }
}

/* ---------------- 학교일정: 한 달 달력 + 오른쪽에 고른 날 ----------------
 *  2026년 11월  ●휴일 ●시험 ●행사 ●기타 ●업무      [< 이전달] [오늘] [다음달 >] */

#define DET_W 400
#define SB_W (INNER_W - DET_W - 12)
#define SB_H (BODY_H - 28 - 42)
static week_grid_t s_school_grid;
static lv_obj_t *s_month_title, *s_school_box, *s_det_title, *s_det_text;
static int s_month;   /* yyyymm */

static void on_month(lv_event_t *e)
{
    int step = (int)(intptr_t)lv_event_get_user_data(e);
    if (step == 0) {
        s_month = ymd_today() / 100;
        s_school_sel = ymd_today();
    } else {
        int y = s_month / 100, m = s_month % 100 + step;
        if (m < 1) {
            m = 12;
            y--;
        } else if (m > 12) {
            m = 1;
            y++;
        }
        s_month = y * 100 + m;
    }
    school_refresh();
}

void school_build(lv_obj_t *page)
{
    s_page_school = mk_card(page, PAD, BODY_Y, SCR_W - PAD * 2, BODY_H);
    lv_obj_t *c = s_page_school;
    s_month_title = mk_label(c, &font_kr_26, C_TEXT, "");
    lv_obj_set_pos(s_month_title, 0, -2);
    static const char *LEG[] = { "휴일", "시험", "행사", "기타", "업무" };
    static const uint32_t LEG_C[] = { C_PINK, 0xC792EA, C_GREEN, C_WARM, C_SKY };
    for (int i = 0; i < 5; i++) {
        char buf[24];
        snprintf(buf, sizeof(buf), "● %s", LEG[i]);
        lv_obj_t *l = mk_label(c, &font_kr_18, LEG_C[i], buf);
        lv_obj_set_pos(l, 170 + i * 70, 4);
    }
    static const char *NAV[] = { LV_SYMBOL_LEFT " 이전달", "오늘", "다음달 " LV_SYMBOL_RIGHT };
    static const int STEP[] = { -1, 0, 1 }, NX[] = { 344, 216, 128 }, NW[] = { 120, 80, 128 };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = mk_btn(c, NAV[i], &font_kr_18, on_month, (void *)(intptr_t)STEP[i]);
        lv_obj_set_pos(b, INNER_W - NX[i], -6);
        lv_obj_set_size(b, NW[i], 38);
    }
    s_school_box = mk_box(c, 0, 42, SB_W, SB_H);

    lv_obj_t *det = mk_box(c, INNER_W - DET_W, 42, DET_W, BODY_H - 28 - 42);
    lv_obj_set_style_radius(det, 10, 0);
    lv_obj_set_style_bg_opa(det, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(det, lv_color_hex(C_CARD2), 0);
    lv_obj_set_style_pad_all(det, 12, 0);
    s_det_title = mk_label(det, &font_kr_22, C_TEXT, "");
    lv_obj_t *list = mk_list(det, 0, 36, DET_W - 24, BODY_H - 28 - 42 - 24 - 36);
    s_det_text = mk_wrap(list, &font_kr_18, C_TEXT, "");
    lv_label_set_recolor(s_det_text, true);
    lv_obj_set_style_text_line_space(s_det_text, 8, 0);
    school_refresh();
}

void school_refresh(void)
{
    int today = ymd_today();
    /* 켜자마자(인터넷 시간 맞추기 전)는 1970년이라 그 달에 붙박이지 않게
     * 2025년 이전 값이면 오늘로 다시 잡습니다 */
    if (s_month < 202501) {
        s_month = today / 100;
    }
    if (s_school_sel < 20250101) {
        s_school_sel = today;
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "%d년 %d월", s_month / 100, s_month % 100);
    label_set(s_month_title, buf);

    lv_obj_clean(s_school_box);
    int first = s_month * 100 + 1;
    int ndays = 0;
    while (ndays < 31 && ymd_add(first, ndays) / 100 == s_month) {
        ndays++;
    }
    int off = ymd_wday(first);   /* 일요일부터 */
    int weeks = (off + ndays + 6) / 7;
    week_grid_t *g = &s_school_grid;
    grid_build(g, s_school_box, SB_W, SB_H,
               ymd_add(first, -off), weeks, 7, s_month, GK_SCHOOL, s_school_sel);
    grid_add_school(g, false);
    grid_finish(g);

    date_text(s_school_sel, buf, sizeof(buf));
    label_set(s_det_title, buf);
    static char body[2048];
    if (!g_data.school) {
        strlcpy(body, g_cfg.sched_url[0] ? "학교 일정을 받는 중입니다…" : "설치 페이지에서 '학교 일정' 웹앱 주소를 넣어 주세요.",
                sizeof(body));
    } else {
        day_text(GK_SCHOOL, s_school_sel, body, sizeof(body));
    }
    lv_label_set_text(s_det_text, body);
}

/* ---------------- 창체계획 ----------------
 *  구글 시트면 그 표를 그대로, 아니면 4주 달력 표에
 *  학교 일정 가운데 동아리·자율·진로·봉사 같은 창체 일정만 (캘린더 일정은 넣지 않음). */

#define CHA_WEEKS 4
static week_grid_t s_cha_grid;

void cha_build(lv_obj_t *page)
{
    lv_obj_t *c = page_card(page, "창체 계획", C_GREEN);
    s_cha_hint = page_hint(c);
    s_cha = mk_box(c, 0, 38, INNER_W, LIST_H);
    cha_refresh();
}

static void cha_sheet(const cha_t *c)
{
    /* 표: 이번 주 월요일부터 */
    int from = 0, monday = ymd_week_monday();
    for (int r = 0; r < c->rows; r++) {
        if (c->ymd[r] && c->ymd[r] < ymd_add(monday, -2)) {
            from = r + 1;
        }
    }
    lv_obj_t *list = mk_list(s_cha, 0, 0, INNER_W, LIST_H);
    s_cha_table = lv_table_create(list);
    lv_obj_set_width(s_cha_table, lv_pct(100));
    lv_obj_set_style_text_font(s_cha_table, &font_kr_18, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_cha_table, lv_color_hex(C_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_cha_table, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(s_cha_table, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_cha_table, lv_color_hex(C_LINE), LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_cha_table, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(s_cha_table, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_cha_table, 0, 0);
    lv_obj_set_style_pad_all(s_cha_table, 8, LV_PART_ITEMS);
    int n = c->rows - from;
    if (n > 60) {
        n = 60;
    }
    lv_table_set_col_cnt(s_cha_table, c->cols);
    lv_table_set_row_cnt(s_cha_table, n + 1);
    int total = INNER_W - 12;
    int narrow_cnt = 0;
    for (int j = 0; j < c->cols; j++) {
        narrow_cnt += strlen(c->head[j]) <= 3;
    }
    int wide = (total - narrow_cnt * 60) / (c->cols - narrow_cnt ? c->cols - narrow_cnt : 1);
    for (int k = 0; k < c->cols; k++) {
        /* 짧은 칸(월·주·일)은 좁게 */
        lv_table_set_col_width(s_cha_table, k, strlen(c->head[k]) <= 3 ? 60 : wide);
        lv_table_set_cell_value(s_cha_table, 0, k, c->head[k]);
    }
    for (int r = 0; r < n; r++) {
        for (int k = 0; k < c->cols; k++) {
            lv_table_set_cell_value(s_cha_table, r + 1, k, c->cell[from + r][k]);
        }
    }
    label_set(s_cha_hint, n ? "" : "이번 주부터 창체 계획이 없습니다");
}

void cha_refresh(void)
{
    lv_obj_clean(s_cha);
    s_cha_table = NULL;
    const cha_t *c = g_data.cha;
    if (c && !c->is_cal) {
        cha_sheet(c);
        return;
    }
    week_grid_t *g = &s_cha_grid;
    grid_build(g, s_cha, INNER_W, LIST_H, ymd_week_monday(), CHA_WEEKS, 5, 0, GK_CHA, 0);
    grid_add_school(g, true);
    int n = grid_finish(g);
    const char *hint = "칸을 누르면 자세히";
    if (!g_data.school) {
        hint = "학교 일정을 받는 중…";
    } else if (!n) {
        hint = "앞으로 4주 동안 창체 일정이 없습니다";
    }
    label_set(s_cha_hint, hint);
}

/* ---------------- 캘린더: 이번 주부터 5주 ---------------- */

#define CAL_WEEKS 5
static week_grid_t s_cal_grid;
static lv_obj_t *s_cal_hint;

void cal_build(lv_obj_t *page)
{
    lv_obj_t *c = mk_card(page, PAD, BODY_Y, SCR_W - PAD * 2, BODY_H);
    s_cal_title = mk_label(c, &font_kr_22, C_SKY, "");
    s_cal_hint = page_hint(c);
    s_cal = mk_box(c, 0, 38, INNER_W, LIST_H);
    cal_refresh();
}

void cal_refresh(void)
{
    lv_obj_clean(s_cal);
    int today = ymd_today();
    int first = ymd_add(today, -ymd_wday(today)), last = ymd_add(first, CAL_WEEKS * 7 - 1); /* 이번 주 일요일부터 */
    char buf[80];
    snprintf(buf, sizeof(buf), "내 구글 캘린더  %d/%d ~ %d/%d", first / 100 % 100, first % 100, last / 100 % 100,
             last % 100);
    label_set(s_cal_title, buf);
    week_grid_t *g = &s_cal_grid;
    grid_build(g, s_cal, INNER_W, LIST_H, first, CAL_WEEKS, 7, 0, GK_CAL, 0);
    grid_add_cal(g, g_data.mycal, C_SKY);
    int n = grid_finish(g);
    label_set(s_cal_hint, !g_cfg.ical[0] ? "설치 페이지에서 구글 캘린더 비공개 주소를 넣어 주세요"
                          : !g_data.mycal ? "캘린더를 받는 중…"
                          : !n ? "앞으로 일정이 없습니다" : "칸을 누르면 자세히");
}

/* ---------------- 급식: 하루 한 줄씩 (오늘이 맨 위) ---------------- */

void meal_build(lv_obj_t *page)
{
    lv_obj_t *c = page_card(page, "급식 · 식단표 (오늘이 맨 위)", C_WARM);
    s_meal_list = mk_list(c, 0, 38, INNER_W, LIST_H);
    lv_obj_set_style_pad_row(s_meal_list, 8, 0);
    meal_refresh();
}

static void meal_line(lv_obj_t *box, const char *name, uint32_t color, const char *menu)
{
    lv_obj_t *r = lv_obj_create(box);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    mk_label(r, &font_kr_22, color, name);
    char buf[700];
    join_menu(menu, " · ", buf, sizeof(buf));
    lv_obj_t *t = mk_label(r, &font_kr_22, C_TEXT, buf);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(t, 64, 0);
    lv_obj_set_width(t, INNER_W - 28 - 64);
}

void meal_refresh(void)
{
    lv_obj_clean(s_meal_list);
    const meals_t *m = g_data.meals;
    if (!m || m->count == 0) {
        mk_wrap(s_meal_list, &font_kr_22, C_DIM,
                !g_cfg.school[0] ? "설치 페이지에서 학교를 고르면 급식이 나옵니다."
                : m ? "앞으로 열흘 동안 급식 정보가 없습니다." : "급식을 받는 중입니다…");
        return;
    }
    int today = ymd_today();
    for (int i = 0; i < m->count; i++) {
        const meal_day_t *d = &m->day[i];
        bool is_today = d->ymd == today;
        lv_obj_t *c = lv_obj_create(s_meal_list);
        lv_obj_set_size(c, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(c, lv_color_hex(is_today ? 0x2B4A7A : C_CARD2), 0);
        lv_obj_set_style_radius(c, 10, 0);
        lv_obj_set_style_border_width(c, is_today ? 2 : 0, 0);
        lv_obj_set_style_border_color(c, lv_color_hex(C_WARM), 0);
        lv_obj_set_style_pad_all(c, 12, 0);
        lv_obj_set_style_pad_row(c, 6, 0);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        char buf[64];
        date_text(d->ymd, buf, sizeof(buf));
        if (is_today) {
            strlcat(buf, "  오늘", sizeof(buf));
        }
        mk_label(c, &font_kr_22, C_WARM, buf);
        meal_line(c, "점심", C_WARM, d->lunch[0] ? d->lunch : "없음");
        if (d->dinner[0]) {
            meal_line(c, "저녁", C_SKY, d->dinner);
        }
    }
}
