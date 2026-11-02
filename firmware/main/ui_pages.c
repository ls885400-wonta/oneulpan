/* 목록 화면들: 학교일정 · 창체계획 · 캘린더 · 급식 */
#include <stdlib.h>

#include "ui_priv.h"

#define SCHOOL_DAYS 45

static lv_obj_t *s_school, *s_cha, *s_cha_table, *s_cal, *s_meal_row;

static lv_obj_t *page_card(lv_obj_t *page, const char *title, uint32_t color)
{
    lv_obj_t *c = mk_card(page, PAD, BODY_Y, SCR_W - PAD * 2, BODY_H);
    mk_label(c, &font_kr_22, color, title);
    return c;
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
        lv_obj_set_width(rt, 220);
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

/* ---------------- 창체계획 ---------------- */

void cha_build(lv_obj_t *page)
{
    lv_obj_t *c = page_card(page, "창체 계획", C_GREEN);
    s_cha = mk_list(c, 0, 36, SCR_W - PAD * 2 - 28, BODY_H - 28 - 36);
    cha_refresh();
}

static void cal_list(lv_obj_t *list, const calendar_t *cal, const char *empty)
{
    int last = -1;
    for (int i = 0; cal && i < cal->count; i++) {
        const cal_event_t *ev = &cal->ev[i];
        if (ev->day != last) {
            day_head(list, ymd_add(ymd_today(), ev->day));
            last = ev->day;
        }
        char t[16];
        if (ev->minute < 0) {
            strlcpy(t, "종일", sizeof(t));
        } else {
            snprintf(t, sizeof(t), "%02d:%02d", ev->minute / 60, ev->minute % 60);
        }
        row(list, t, ev->day == 0 ? C_GREEN : C_DIM, ev->title, "");
    }
    if (!cal || cal->count == 0) {
        mk_wrap(list, &font_kr_22, C_DIM, empty);
    }
}

void cha_refresh(void)
{
    lv_obj_clean(s_cha);
    s_cha_table = NULL;
    const cha_t *c = g_data.cha;
    if (!c) {
        mk_wrap(s_cha, &font_kr_22, C_DIM,
                g_cfg.cha_url[0] ? "창체 계획을 받는 중입니다…"
                                 : "설치 페이지에서 창체 계획 주소(구글 시트 또는 캘린더)를 넣어 주세요.");
        return;
    }
    if (c->is_cal) {
        cal_list(s_cha, &c->cal, "앞으로 60일 동안 창체 일정이 없습니다.");
        return;
    }
    /* 표: 이번 주 월요일부터 */
    int from = 0, monday = ymd_week_monday();
    for (int r = 0; r < c->rows; r++) {
        if (c->ymd[r] && c->ymd[r] < ymd_add(monday, -2)) {
            from = r + 1;
        }
    }
    s_cha_table = lv_table_create(s_cha);
    lv_obj_set_width(s_cha_table, lv_pct(100));
    lv_obj_set_style_text_font(s_cha_table, &font_kr_18, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_cha_table, lv_color_hex(C_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_cha_table, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(s_cha_table, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_cha_table, lv_color_hex(C_LINE), LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_cha_table, 0, 0);
    lv_obj_set_style_pad_all(s_cha_table, 6, LV_PART_ITEMS);
    int n = c->rows - from;
    if (n > 60) {
        n = 60;
    }
    lv_table_set_col_cnt(s_cha_table, c->cols);
    lv_table_set_row_cnt(s_cha_table, n + 1);
    int total = SCR_W - PAD * 2 - 40;
    for (int k = 0; k < c->cols; k++) {
        /* 짧은 칸(월·주·일)은 좁게 */
        bool narrow = strlen(c->head[k]) <= 3;
        int narrow_cnt = 0;
        for (int j = 0; j < c->cols; j++) {
            narrow_cnt += strlen(c->head[j]) <= 3;
        }
        int wide = (total - narrow_cnt * 60) / (c->cols - narrow_cnt ? c->cols - narrow_cnt : 1);
        lv_table_set_col_width(s_cha_table, k, narrow ? 60 : wide);
        lv_table_set_cell_value(s_cha_table, 0, k, c->head[k]);
    }
    for (int r = 0; r < n; r++) {
        for (int k = 0; k < c->cols; k++) {
            lv_table_set_cell_value(s_cha_table, r + 1, k, c->cell[from + r][k]);
        }
    }
}

/* ---------------- 캘린더 ---------------- */

void cal_build(lv_obj_t *page)
{
    lv_obj_t *c = page_card(page, "내 캘린더 (2주)", C_SKY);
    s_cal = mk_list(c, 0, 36, SCR_W - PAD * 2 - 28, BODY_H - 28 - 36);
    cal_refresh();
}

void cal_refresh(void)
{
    lv_obj_clean(s_cal);
    if (!g_cfg.ical[0]) {
        mk_wrap(s_cal, &font_kr_22, C_DIM, "설치 페이지에서 구글 캘린더 비공개 주소를 넣으면 여기에 일정이 나옵니다.");
        return;
    }
    cal_list(s_cal, g_data.mycal, g_data.mycal ? "앞으로 2주 동안 일정이 없습니다." : "캘린더를 받는 중입니다…");
}

/* ---------------- 급식 ---------------- */

void meal_build(lv_obj_t *page)
{
    s_meal_row = mk_box(page, PAD, BODY_Y, SCR_W - PAD * 2, BODY_H);
    meal_refresh();
}

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
    int n = m->count < 5 ? m->count : 5;
    int w = (SCR_W - PAD * 2 - PAD * (n - 1)) / n;
    int today = ymd_today();
    for (int i = 0; i < n; i++) {
        const meal_day_t *d = &m->day[i];
        lv_obj_t *c = mk_card(s_meal_row, i * (w + PAD), 0, w, BODY_H);
        if (d->ymd == today) {
            lv_obj_set_style_border_color(c, lv_color_hex(C_GREEN), 0);
            lv_obj_set_style_border_width(c, 2, 0);
        }
        char buf[48];
        date_text(d->ymd, buf, sizeof(buf));
        mk_label(c, &font_kr_22, d->ymd == today ? C_GREEN : C_TEXT, buf);
        lv_obj_t *l = mk_label(c, &font_kr_18, C_WARM, "점심");
        lv_obj_set_pos(l, 0, 38);
        l = mk_wrap(c, &font_kr_18, C_TEXT, d->lunch[0] ? d->lunch : "없음");
        lv_obj_set_pos(l, 0, 62);
        if (d->dinner[0]) {
            l = mk_label(c, &font_kr_18, C_SKY, "저녁");
            lv_obj_set_pos(l, 0, 290);
            l = mk_wrap(c, &font_kr_18, C_DIM, d->dinner);
            lv_obj_set_pos(l, 0, 314);
        }
    }
}
