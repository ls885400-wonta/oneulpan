/* 목록 화면들: 학교일정 · 창체계획 · 캘린더 · 급식 */
#include <stdlib.h>

#include "ui_priv.h"

#define SCHOOL_DAYS 45

#define INNER_W (SCR_W - PAD * 2 - 28)   /* 큰 카드 안쪽 너비 */
#define LIST_H (BODY_H - 28 - 38)

static lv_obj_t *s_school, *s_cha, *s_cha_table, *s_cha_hint, *s_cal, *s_cal_hint, *s_meal_row;

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

/* ---------------- 달력 표 ----------------
 *  weeks 주 × days 요일(월요일부터). 칸마다 날짜와 일정 몇 줄, 넘치면 "외 n개". */

#define GRID_MAX 35
#define GRID_LINE_H 22
typedef struct {
    int first, weeks, days, lines;
    int cw;
    lv_obj_t *cell[GRID_MAX];
    lv_obj_t *last[GRID_MAX];
    int used[GRID_MAX], extra[GRID_MAX];
} week_grid_t;

static void grid_build(week_grid_t *g, lv_obj_t *box, int w, int h, int weeks, int days)
{
    static const char *HEAD[7] = { "월", "화", "수", "목", "금", "토", "일" };
    const int hh = 28, gap = 6;
    memset(g, 0, sizeof(*g));
    g->first = ymd_week_monday();
    g->weeks = weeks;
    g->days = days;
    g->cw = (w - gap * (days - 1)) / days;
    int ch = (h - hh - gap * (weeks - 1)) / weeks;
    g->lines = (ch - 34) / GRID_LINE_H;
    int today = ymd_today();
    for (int d = 0; d < days; d++) {
        lv_obj_t *l = mk_label(box, &font_kr_18, d == 6 ? C_PINK : d == 5 ? C_BLUE : C_DIM, HEAD[d]);
        lv_obj_set_width(l, g->cw);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(l, d * (g->cw + gap), 2);
    }
    for (int wk = 0; wk < weeks; wk++) {
        for (int d = 0; d < days; d++) {
            int ymd = ymd_add(g->first, wk * 7 + d);
            bool holiday;
            const acad_t *a = school_day_info(ymd, &holiday);
            lv_obj_t *c = mk_box(box, d * (g->cw + gap), hh + wk * (ch + gap), g->cw, ch);
            lv_obj_set_style_radius(c, 10, 0);
            lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(c, lv_color_hex(ymd == today ? 0x24624E : C_CARD2), 0);
            lv_obj_set_style_border_width(c, ymd == today ? 2 : 1, 0);
            lv_obj_set_style_border_color(c, lv_color_hex(ymd == today ? C_GREEN : C_LINE), 0);
            if (ymd < today) {
                lv_obj_set_style_opa(c, LV_OPA_50, 0);
            }
            char buf[24];
            int day = ymd % 100;
            if ((wk == 0 && d == 0) || day == 1) {
                snprintf(buf, sizeof(buf), "%d/%d", ymd / 100 % 100, day);
            } else {
                snprintf(buf, sizeof(buf), "%d", day);
            }
            uint32_t color = ymd == today ? C_GREEN : (d == 6 || holiday) ? C_PINK : d == 5 ? C_BLUE : C_TEXT;
            lv_obj_t *dl = mk_label(c, &font_kr_18, color, buf);
            lv_obj_set_pos(dl, 8, 4);
            if (ymd == today) {
                lv_obj_align_to(mk_label(c, &font_kr_18, C_GREEN, "오늘"), dl, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
            }
            int i = wk * days + d;
            g->cell[i] = c;
            if (holiday && a) {
                lv_obj_t *hl = mk_label(c, &font_kr_18, C_PINK, a->name);
                lv_obj_set_width(hl, g->cw - 16);
                lv_label_set_long_mode(hl, LV_LABEL_LONG_DOT);
                lv_obj_set_pos(hl, 8, 30);
                g->used[i] = 1;
            }
        }
    }
}

static void grid_add(week_grid_t *g, int ymd, const char *text, uint32_t color)
{
    int k = ymd_diff(ymd, g->first);
    if (k < 0 || k >= g->weeks * 7 || k % 7 >= g->days) {
        return;
    }
    int i = k / 7 * g->days + k % 7;
    if (g->used[i] >= g->lines) {
        g->extra[i]++;
        return;
    }
    lv_obj_t *l = mk_label(g->cell[i], &font_kr_18, color, text);
    lv_obj_set_width(l, g->cw - 16);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(l, 8, 30 + g->used[i] * GRID_LINE_H);
    g->last[i] = l;
    g->used[i]++;
}

/* 넘친 칸은 마지막 줄을 "외 n개" 로. 넣은 일정 수를 돌려줍니다. */
static int grid_finish(week_grid_t *g)
{
    int total = 0;
    for (int i = 0; i < g->weeks * g->days; i++) {
        total += g->used[i] + g->extra[i];
        if (g->extra[i] && g->last[i]) {
            char buf[24];
            snprintf(buf, sizeof(buf), "외 %d개", g->extra[i] + 1);
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

static lv_obj_t *day_head(lv_obj_t *list, int ymd)
{
    char buf[48];
    int diff = ymd_diff(ymd, ymd_today());
    date_text(ymd, buf, sizeof(buf));
    if (diff == 0 || diff == 1) {
        char t[64];
        snprintf(t, sizeof(t), "%s · %s", diff == 0 ? "오늘" : "내일", buf);
        strlcpy(buf, t, sizeof(buf));
    }
    lv_obj_t *h = mk_label(list, &font_kr_18, diff == 0 ? C_GREEN : C_BLUE, buf);
    lv_obj_set_style_pad_top(h, 8, 0);
    return h;
}

/* 한 줄: [배지] 글자 */
static void row(lv_obj_t *list, const char *badge, uint32_t badge_color, const char *text, const char *right)
{
    lv_obj_t *r = lv_obj_create(list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    int x = 0;
    if (badge && badge[0]) {
        lv_obj_t *b = mk_label(r, &font_kr_18, C_BG, badge);
        lv_obj_set_style_bg_color(b, lv_color_hex(badge_color), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_set_style_pad_hor(b, 6, 0);
        lv_obj_set_width(b, 116);
        lv_label_set_long_mode(b, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
        x = 128;
    }
    lv_obj_t *t = mk_label(r, &font_kr_22, C_TEXT, text);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(t, x, 0);
    lv_obj_set_width(t, (SCR_W - PAD * 2 - 28) - x - (right && right[0] ? 230 : 0));
    if (right && right[0]) {
        lv_obj_t *rt = mk_label(r, &font_kr_18, C_DIM, right);
        lv_label_set_long_mode(rt, LV_LABEL_LONG_DOT);
        lv_obj_set_size(rt, 220, 24);   /* 한 줄, 넘치면 … */
        lv_obj_set_style_text_align(rt, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(rt, LV_ALIGN_TOP_RIGHT, 0, 2);
    }
}

static uint32_t acad_color(const char *type)
{
    if (!strcmp(type, "공휴일") || !strcmp(type, "재량휴업일") || !strcmp(type, "방학")) return C_PINK;
    if (!strcmp(type, "지필") || !strcmp(type, "시험")) return C_WARM;
    if (!strcmp(type, "행사")) return C_GREEN;
    return C_DIM;
}

/* ---------------- 학교일정 ---------------- */

void school_build(lv_obj_t *page)
{
    lv_obj_t *c = page_card(page, "학교 일정", C_PINK);
    s_school = mk_list(c, 0, 36, SCR_W - PAD * 2 - 28, BODY_H - 28 - 36);
    school_refresh();
}

void school_refresh(void)
{
    lv_obj_clean(s_school);
    const school_t *s = g_data.school;
    if (!s) {
        mk_wrap(s_school, &font_kr_22, C_DIM,
                g_cfg.sched_url[0] ? "학교 일정을 받는 중입니다…" : "설치 페이지에서 '학교 일정' 웹앱 주소를 넣어 주세요.");
        return;
    }
    int today = ymd_today();
    int shown = 0;
    for (int k = 0; k < SCHOOL_DAYS && shown < 160; k++) {
        int ymd = ymd_add(today, k);
        bool head = false;
        for (int i = 0; i < s->acad_count; i++) {
            if (s->acad[i].ymd != ymd) continue;
            if (!head) { day_head(s_school, ymd); head = true; }
            row(s_school, s->acad[i].type, acad_color(s->acad[i].type), s->acad[i].name, "");
            shown++;
        }
        for (int i = 0; i < s->sched_count; i++) {
            const sched_t *x = &s->sched[i];
            /* 여러 날 이어지는 일정은 시작하는 날(또는 오늘)에만 */
            if (!(x->start == ymd || (k == 0 && x->start < ymd && ymd <= x->end))) continue;
            if (!head) { day_head(s_school, ymd); head = true; }
            char right[96];
            if (x->end > x->start) {
                snprintf(right, sizeof(right), "~%d/%d %s", x->end / 100 % 100, x->end % 100, x->time);
            } else {
                strlcpy(right, x->time, sizeof(right));
            }
            row(s_school, x->dept, C_SKY, x->title, right);
            shown++;
        }
    }
    if (!shown) {
        mk_wrap(s_school, &font_kr_22, C_DIM, "앞으로 45일 동안 일정이 없습니다.");
    }
}

/* ---------------- 창체계획 ----------------
 *  구글 시트면 그 표를 그대로, 캘린더면 4주 달력 표.
 *  학교 일정 가운데 동아리·자율·진로·봉사 같은 창체 일정도 달력에 함께 넣습니다. */

#define CHA_WEEKS 4
static week_grid_t s_cha_grid;
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
    grid_build(g, s_cha, INNER_W, LIST_H, CHA_WEEKS, 5);
    if (c) {
        grid_add_cal(g, &c->cal, C_TEXT);
    }
    const school_t *s = g_data.school;
    for (int i = 0; s && i < s->acad_count; i++) {
        if (is_cha(s->acad[i].name)) {
            grid_add(g, s->acad[i].ymd, s->acad[i].name, C_GREEN);
        }
    }
    for (int i = 0; s && i < s->sched_count; i++) {
        if (is_cha(s->sched[i].title)) {
            grid_add(g, s->sched[i].start, s->sched[i].title, C_SKY);
        }
    }
    int n = grid_finish(g);
    const char *hint = "";
    if (!g_cfg.cha_url[0] && !n) {
        hint = "설치 페이지에서 창체 계획 주소를 넣어 주세요";
    } else if (g_cfg.cha_url[0] && !c) {
        hint = "창체 계획을 받는 중…";
    } else if (!n) {
        hint = "앞으로 4주 동안 창체 일정이 없습니다";
    }
    label_set(s_cha_hint, hint);
}

/* ---------------- 캘린더 ---------------- */

static week_grid_t s_cal_grid;

void cal_build(lv_obj_t *page)
{
    lv_obj_t *c = page_card(page, "내 캘린더 (2주)", C_SKY);
    s_cal_hint = page_hint(c);
    s_cal = mk_box(c, 0, 38, INNER_W, LIST_H);
    cal_refresh();
}

void cal_refresh(void)
{
    lv_obj_clean(s_cal);
    week_grid_t *g = &s_cal_grid;
    grid_build(g, s_cal, INNER_W, LIST_H, 2, 7);
    grid_add_cal(g, g_data.mycal, C_TEXT);
    int n = grid_finish(g);
    label_set(s_cal_hint, !g_cfg.ical[0] ? "설치 페이지에서 구글 캘린더 비공개 주소를 넣어 주세요"
                          : !g_data.mycal ? "캘린더를 받는 중…"
                          : !n ? "앞으로 2주 동안 일정이 없습니다" : "");
}

/* ---------------- 급식 ---------------- */

void meal_build(lv_obj_t *page)
{
    s_meal_row = mk_box(page, PAD, BODY_Y, SCR_W - PAD * 2, BODY_H);
    meal_refresh();
}

static void meal_part(lv_obj_t *card, const char *name, uint32_t color, const char *menu,
                      const lv_font_t *font, uint32_t text_color)
{
    lv_obj_t *l = mk_label(card, &font_kr_18, color, name);
    lv_obj_set_style_pad_top(l, 8, 0);
    l = mk_wrap(card, font, text_color, menu);
    lv_obj_set_style_text_line_space(l, 5, 0);
}

/* 4일치 카드: 날짜, 점심(크게), 저녁 */
void meal_refresh(void)
{
    lv_obj_clean(s_meal_row);
    const meals_t *m = g_data.meals;
    if (!m || m->count == 0) {
        lv_obj_t *c = mk_card(s_meal_row, 0, 0, SCR_W - PAD * 2, BODY_H);
        mk_wrap(c, &font_kr_22, C_DIM,
                !g_cfg.school[0] ? "설치 페이지에서 학교를 고르면 급식이 나옵니다."
                : m ? "앞으로 열흘 동안 급식 정보가 없습니다." : "급식을 받는 중입니다…");
        return;
    }
    int n = m->count < 4 ? m->count : 4;
    int w = (SCR_W - PAD * 2 - PAD * 3) / 4;
    int today = ymd_today();
    for (int i = 0; i < n; i++) {
        const meal_day_t *d = &m->day[i];
        lv_obj_t *c = mk_card(s_meal_row, i * (w + PAD), 0, w, BODY_H);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(c, 4, 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(c, LV_DIR_VER);
        if (d->ymd == today) {
            lv_obj_set_style_border_color(c, lv_color_hex(C_GREEN), 0);
            lv_obj_set_style_border_width(c, 2, 0);
        }
        char buf[48];
        int diff = ymd_diff(d->ymd, today);
        date_text(d->ymd, buf, sizeof(buf));
        if (diff == 0 || diff == 1) {
            char t[64];
            snprintf(t, sizeof(t), "%s  %s", diff ? "내일" : "오늘", buf);
            strlcpy(buf, t, sizeof(buf));
        }
        mk_label(c, &font_kr_22, d->ymd == today ? C_GREEN : C_TEXT, buf);
        meal_part(c, "점심", C_WARM, d->lunch[0] ? d->lunch : "없음", &font_kr_22, C_TEXT);
        if (d->dinner[0]) {
            meal_part(c, "저녁", C_SKY, d->dinner, &font_kr_18, C_DIM);
        }
    }
}
