/* 홈 화면 (위쪽 줄 없이 꽉 차게)
 *
 *  ┌ 시계 · 날짜 · 요일 (누르면 메뉴) ──────┐ ┌ 황인환 선생님 오늘 시간표 ┐
 *  ├ 날씨 · 미세먼지 · 5일 ─┬ 내 일정 ──────┤ │ 1교시  공강               │
 *  │                         │               │ │ 2교시  정보        104    │
 *  ├ 점심 ───────────────────┴───────────────┤ │ 점심                      │
 *  └ 저녁 ───────────────────────────────────┘ ├ 학교 일정 (오늘) ─────────┤
 *                                               └───────────────────────────┘
 */
#include <stdlib.h>

#include "ui_priv.h"

#define RIGHT_W 330
#define LEFT_W (SCR_W - PAD * 3 - RIGHT_W)
#define RIGHT_X (PAD + LEFT_W + PAD)
#define CLOCK_H 200
#define ROW2_Y (PAD + CLOCK_H + PAD)
#define ROW2_H 200
#define WEATHER_W 330
#define MEAL_Y (ROW2_Y + ROW2_H + PAD)
#define SCHOOL_H 150
#define TT_H (SCR_H - PAD * 3 - SCHOOL_H)
#define WX_DAYS 5
#define MY_LINES 5
#define TT_ROWS (TT_PERIODS + 1)   /* 교시 + 점심 */
#define SCHOOL_LINES 4
#define LESSON_MIN 50

static lv_obj_t *s_time, *s_ampm, *s_date, *s_status;
static lv_obj_t *s_weather, *s_w_icon, *s_w_temp, *s_w_cond, *s_w_range, *s_w_dust, *s_w_dust_lv;
static lv_obj_t *s_w_day[WX_DAYS], *s_w_day_icon[WX_DAYS], *s_w_day_t[WX_DAYS];
static lv_obj_t *s_my, *s_my_line[MY_LINES];
static lv_obj_t *s_meal, *s_lunch_name, *s_lunch, *s_dinner_row, *s_dinner;
static lv_obj_t *s_tt_card, *s_tt_title, *s_tt_sub, *s_tt_msg;
static lv_obj_t *s_tt_row[TT_ROWS], *s_tt_badge[TT_ROWS], *s_tt_btext[TT_ROWS], *s_tt_subj[TT_ROWS], *s_tt_room[TT_ROWS];
static lv_obj_t *s_school, *s_school_line[SCHOOL_LINES];
static lv_obj_t *s_setup;
static int s_last_min = -1;

static void on_settings(lv_event_t *e)
{
    ui_go(PG_SET);
}

static void on_menu(lv_event_t *e)
{
    ui_menu_open();
}

static void on_page(lv_event_t *e)
{
    ui_go((int)(intptr_t)lv_event_get_user_data(e));
}

static void show_or_hide(lv_obj_t *o, bool show)
{
    if (show) {
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 카드 전체를 누르면 그 화면으로 */
static void card_link(lv_obj_t *card, int page)
{
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, on_page, LV_EVENT_CLICKED, (void *)(intptr_t)page);
}

static lv_obj_t *dot_label(lv_obj_t *parent, int x, int y, int w)
{
    lv_obj_t *l = mk_label(parent, &font_kr_18, C_TEXT, "");
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    return l;
}

static void build_clock(lv_obj_t *page)
{
    lv_obj_t *c = mk_card(page, PAD, PAD, LEFT_W, CLOCK_H);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, on_menu, LV_EVENT_CLICKED, NULL);
    s_status = mk_label(c, &font_kr_18, C_GREEN, "");
    lv_obj_set_pos(s_status, 0, -4);
    lv_obj_t *gear = mk_label(c, &lv_font_montserrat_24, C_DIM, LV_SYMBOL_SETTINGS);
    lv_obj_add_flag(gear, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(gear, 16);
    lv_obj_add_event_cb(gear, on_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, 0, -4);
    lv_obj_t *hint = mk_label(c, &font_kr_18, C_DIM, "시계를 누르면 메뉴");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_RIGHT, 0, 4);
    s_time = mk_label(c, &font_num_130, C_TEXT, "--:--");
    lv_obj_align(s_time, LV_ALIGN_TOP_MID, 0, 0);
    s_ampm = mk_label(c, &font_kr_22, C_DIM, "");
    lv_obj_align_to(s_ampm, s_time, LV_ALIGN_OUT_LEFT_BOTTOM, -6, -24);
    s_date = mk_label(c, &font_kr_36, C_SKY, "");
    lv_obj_align(s_date, LV_ALIGN_BOTTOM_MID, 0, 2);
}

static void build_weather(lv_obj_t *page)
{
    lv_obj_t *c = s_weather = mk_card(page, PAD, ROW2_Y, WEATHER_W, ROW2_H);
    s_w_icon = mk_wx_icon(c, 64);
    lv_obj_set_pos(s_w_icon, 0, 10);
    s_w_range = mk_label(c, &font_kr_18, C_DIM, "");
    lv_obj_set_pos(s_w_range, 76, -2);
    s_w_temp = mk_label(c, &font_num_60, C_TEXT, "");
    lv_obj_set_pos(s_w_temp, 74, 16);
    s_w_cond = mk_label(c, &font_kr_18, C_DIM, g_cfg.lat[0] ? "날씨 받는 중…" : "위치 설정 필요");
    lv_obj_set_pos(s_w_cond, 76, 78);
    s_w_dust_lv = mk_label(c, &font_kr_22, C_GREEN, "");
    lv_obj_align(s_w_dust_lv, LV_ALIGN_TOP_RIGHT, 0, 10);
    s_w_dust = mk_label(c, &font_kr_18, C_DIM, "");
    lv_obj_set_style_text_align(s_w_dust, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_w_dust, LV_ALIGN_TOP_RIGHT, 0, 42);
    int cw = (WEATHER_W - 28) / WX_DAYS;
    for (int i = 0; i < WX_DAYS; i++) {
        lv_obj_t *col = mk_box(c, i * cw, 104, cw, 72);
        s_w_day[i] = mk_label(col, &font_kr_18, C_DIM, "");
        lv_obj_align(s_w_day[i], LV_ALIGN_TOP_MID, 0, 0);
        s_w_day_icon[i] = mk_wx_icon(col, 24);
        lv_obj_align(s_w_day_icon[i], LV_ALIGN_TOP_MID, 0, 22);
        s_w_day_t[i] = mk_label(col, &font_kr_18, C_TEXT, "");
        lv_obj_align(s_w_day_t[i], LV_ALIGN_TOP_MID, 0, 48);
    }
}

static void build_my(lv_obj_t *page)
{
    int x = PAD + WEATHER_W + PAD;
    int w = LEFT_W - WEATHER_W - PAD;
    lv_obj_t *c = s_my = mk_card(page, x, ROW2_Y, w, ROW2_H);
    card_link(c, PG_CAL);
    mk_label(c, &font_kr_22, C_SKY, "내 일정");
    lv_obj_align(mk_link(c, "캘린더", PG_CAL), LV_ALIGN_TOP_RIGHT, 0, 2);
    for (int i = 0; i < MY_LINES; i++) {
        s_my_line[i] = dot_label(c, 0, 38 + i * 27, w - 28);
    }
}

static lv_obj_t *meal_row(lv_obj_t *card, const char *name, uint32_t color, lv_obj_t **name_label, lv_obj_t **text)
{
    lv_obj_t *r = lv_obj_create(card);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *n = mk_label(r, &font_kr_22, color, name);
    lv_obj_set_width(n, 80);
    lv_obj_t *t = mk_label(r, &font_kr_22, C_TEXT, "");
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(t, 86, 0);
    lv_obj_set_width(t, LEFT_W - 28 - 86);
    lv_obj_set_style_text_line_space(t, 4, 0);
    if (name_label) {
        *name_label = n;
    }
    *text = t;
    return r;
}

static void build_meal(lv_obj_t *page)
{
    lv_obj_t *c = s_meal = mk_card(page, PAD, MEAL_Y, LEFT_W, SCR_H - MEAL_Y - PAD);
    card_link(c, PG_MEAL);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 10, 0);
    meal_row(c, "점심", C_WARM, &s_lunch_name, &s_lunch);
    s_dinner_row = meal_row(c, "저녁", C_SKY, NULL, &s_dinner);
    label_set(s_lunch, g_cfg.school[0] ? "급식 받는 중…" : "설치 페이지에서 학교를 고르면 급식이 나옵니다.");
}

static void build_tt(lv_obj_t *page)
{
    lv_obj_t *c = s_tt_card = mk_card(page, RIGHT_X, PAD, RIGHT_W, TT_H);
    card_link(c, PG_FIND);
    lv_obj_set_style_pad_all(c, 12, 0);
    s_tt_title = mk_label(c, &font_kr_22, C_TEXT, "");
    lv_obj_set_width(s_tt_title, RIGHT_W - 24 - 70);
    lv_label_set_long_mode(s_tt_title, LV_LABEL_LONG_DOT);
    lv_obj_align(mk_link(c, "이번 주", PG_FIND), LV_ALIGN_TOP_RIGHT, 0, 2);
    s_tt_sub = mk_label(c, &font_kr_18, C_DIM, "");
    lv_obj_set_pos(s_tt_sub, 0, 28);
    const int top = 56, iw = RIGHT_W - 24;
    int rh = (TT_H - 24 - top) / TT_ROWS;
    for (int i = 0; i < TT_ROWS; i++) {
        lv_obj_t *r = mk_box(c, 0, top + i * rh, iw, rh - 4);
        lv_obj_set_style_radius(r, 8, 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(r, lv_color_hex(C_CARD2), 0);
        s_tt_row[i] = r;
        lv_obj_t *b = mk_box(r, 0, 0, 72, rh - 4);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        s_tt_badge[i] = b;
        s_tt_btext[i] = mk_label(b, &font_kr_18, C_TEXT, "");
        lv_obj_set_style_text_align(s_tt_btext[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(s_tt_btext[i], -4, 0);
        lv_obj_center(s_tt_btext[i]);
        s_tt_subj[i] = mk_label(r, &font_kr_22, C_TEXT, "");
        lv_obj_set_width(s_tt_subj[i], iw - 72 - 14 - 70);
        lv_label_set_long_mode(s_tt_subj[i], LV_LABEL_LONG_DOT);
        lv_obj_align(s_tt_subj[i], LV_ALIGN_LEFT_MID, 84, 0);
        s_tt_room[i] = mk_label(r, &font_kr_18, C_TEXT, "");
        lv_obj_align(s_tt_room[i], LV_ALIGN_RIGHT_MID, -10, 0);
    }
    s_tt_msg = mk_wrap(c, &font_kr_22, C_DIM, "");
    lv_obj_set_width(s_tt_msg, iw - 20);
    lv_obj_align(s_tt_msg, LV_ALIGN_CENTER, 0, 20);
}

static void build_school(lv_obj_t *page)
{
    lv_obj_t *c = s_school = mk_card(page, RIGHT_X, SCR_H - PAD - SCHOOL_H, RIGHT_W, SCHOOL_H);
    card_link(c, PG_SCHOOL);
    lv_obj_set_style_pad_all(c, 10, 0);
    lv_obj_t *b = mk_box(c, 0, 0, 50, SCHOOL_H - 20);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_ACCENT), 0);
    lv_obj_t *t = mk_label(b, &font_kr_22, C_TEXT, "학교\n일정");
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(t);
    for (int i = 0; i < SCHOOL_LINES; i++) {
        s_school_line[i] = dot_label(c, 60, 4 + i * 32, RIGHT_W - 20 - 60);
    }
}

void home_build(lv_obj_t *page)
{
    build_clock(page);
    build_weather(page);
    build_my(page);
    build_meal(page);
    build_tt(page);
    build_school(page);

    /* 처음 설정 안내 */
    s_setup = mk_card(page, 162, 170, 700, 260);
    lv_obj_set_style_bg_color(s_setup, lv_color_hex(0x2C3E66), 0);
    lv_obj_set_style_shadow_width(s_setup, 40, 0);
    lv_obj_t *t = mk_label(s_setup, &font_kr_26, C_WARM, "와이파이 설정이 필요합니다");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *b = mk_wrap(s_setup, &font_kr_22, C_TEXT,
                          "· 아래 단추를 눌러 보드에서 와이파이를 고르거나\n"
                          "· 컴퓨터 설치 페이지에서 '설정 보내기'를 누르세요.");
    lv_obj_set_pos(b, 10, 60);
    lv_obj_set_style_text_line_space(b, 8, 0);
    lv_obj_t *go = mk_btn(s_setup, "와이파이 고르기", &font_kr_22, on_settings, NULL);
    lv_obj_set_size(go, 220, 48);
    btn_set_on(go, true);
    lv_obj_align(go, LV_ALIGN_BOTTOM_MID, 0, 0);
    show_or_hide(s_setup, !g_cfg.ssid[0]);

    home_refresh(-1);
    home_refresh(D_TEACHERS);
}

/* ---------------- 채우기 ---------------- */

static void refresh_weather(void)
{
    const weather_t *w = g_data.weather;
    show_or_hide(s_weather, config_show('w'));
    if (!w || !w->ok) {
        return;
    }
    char buf[96];
    wx_icon_set(s_w_icon, w->code);
    snprintf(buf, sizeof(buf), "%.0f°", w->temp);
    label_set(s_w_temp, buf);
    snprintf(buf, sizeof(buf), "오늘 %.0f° / %.0f°", w->day_max[0], w->day_min[0]);
    label_set(s_w_range, buf);
    snprintf(buf, sizeof(buf), "%s · 비 %d%%", weather_text(w->code), w->day_pop[0]);
    label_set(s_w_cond, buf);

    bool dust = w->air_ok && config_show('d');
    if (dust) {
        uint32_t color;
        dust_text(w->pm10, w->pm25, buf, sizeof(buf), &color);
        label_set(s_w_dust_lv, buf);
        lv_obj_set_style_text_color(s_w_dust_lv, lv_color_hex(color), 0);
        lv_obj_align(s_w_dust_lv, LV_ALIGN_TOP_RIGHT, 0, 10);
        snprintf(buf, sizeof(buf), "미세 %d\n초미세 %d", w->pm10, w->pm25);
        label_set(s_w_dust, buf);
        lv_obj_align(s_w_dust, LV_ALIGN_TOP_RIGHT, 0, 42);
    }
    show_or_hide(s_w_dust_lv, dust);
    show_or_hide(s_w_dust, dust);

    int today = ymd_today();
    for (int i = 0; i < WX_DAYS; i++) {
        int ymd = ymd_add(today, i + 1);
        int wd = ymd_wday(ymd);
        bool holiday;
        school_day_info(ymd, &holiday);
        snprintf(buf, sizeof(buf), "%d(%s)", ymd % 100, WEEKDAYS[wd]);
        label_set(s_w_day[i], buf);
        lv_obj_set_style_text_color(s_w_day[i], lv_color_hex(wd == 0 || holiday ? C_PINK : wd == 6 ? C_BLUE : C_DIM), 0);
        wx_icon_set(s_w_day_icon[i], w->day_code[i + 1]);
        snprintf(buf, sizeof(buf), "%.0f/%.0f", w->day_max[i + 1], w->day_min[i + 1]);
        label_set(s_w_day_t[i], buf);
    }
}

static void refresh_my(void)
{
    const calendar_t *c = g_data.mycal;
    int n = 0;
    int today = ymd_today();
    for (int i = 0; c && i < c->count && n < MY_LINES; i++, n++) {
        const cal_event_t *ev = &c->ev[i];
        char when[24], buf[160];
        int ymd = ymd_add(today, ev->day);
        if (ev->day == 0) {
            strlcpy(when, "오늘", sizeof(when));
        } else if (ev->day == 1) {
            strlcpy(when, "내일", sizeof(when));
        } else {
            snprintf(when, sizeof(when), "%d/%d(%s)", ymd / 100 % 100, ymd % 100, WEEKDAYS[ymd_wday(ymd)]);
        }
        if (ev->minute >= 0) {
            snprintf(buf, sizeof(buf), "%s %d:%02d  %s", when, ev->minute / 60, ev->minute % 60, ev->title);
        } else {
            snprintf(buf, sizeof(buf), "%s  %s", when, ev->title);
        }
        label_set(s_my_line[n], buf);
        lv_obj_set_style_text_color(s_my_line[n], lv_color_hex(ev->day == 0 ? C_SKY : C_TEXT), 0);
    }
    if (n == 0) {
        label_set(s_my_line[0], !g_cfg.ical[0] ? "구글 캘린더 주소를 넣으면 나옵니다"
                                : !c ? "받는 중…" : "앞으로 일정이 없습니다");
        lv_obj_set_style_text_color(s_my_line[0], lv_color_hex(C_DIM), 0);
        n = 1;
    }
    for (; n < MY_LINES; n++) {
        label_set(s_my_line[n], "");
    }
}

/* 저녁 7시까지는 오늘, 그 뒤로는 다음 급식 날 */
static void refresh_meal(void)
{
    const meals_t *m = g_data.meals;
    show_or_hide(s_meal, config_show('m'));
    if (!m || !m->ok) {
        return;
    }
    int today = ymd_today();
    int from = now_minute() >= 19 * 60 ? ymd_add(today, 1) : today;
    const meal_day_t *d = NULL;
    for (int i = 0; i < m->count && !d; i++) {
        if (m->day[i].ymd >= from) {
            d = &m->day[i];
        }
    }
    char name[32], buf[700];
    if (!d) {
        label_set(s_lunch_name, "점심");
        label_set(s_lunch, "급식 정보가 없습니다.");
        show_or_hide(s_dinner_row, false);
        return;
    }
    int diff = ymd_diff(d->ymd, today);
    if (diff == 0) {
        strlcpy(name, "점심", sizeof(name));
    } else if (diff == 1) {
        strlcpy(name, "내일\n점심", sizeof(name));
    } else {
        snprintf(name, sizeof(name), "%d일\n점심", d->ymd % 100);
    }
    label_set(s_lunch_name, name);
    join_menu(d->lunch[0] ? d->lunch : "점심 없음", " · ", buf, sizeof(buf));
    label_set(s_lunch, buf);
    if (d->dinner[0]) {
        join_menu(d->dinner, " · ", buf, sizeof(buf));
        label_set(s_dinner, buf);
    }
    show_or_hide(s_dinner_row, d->dinner[0] != 0);
}

static void tt_row(int i, const char *badge, uint32_t badge_bg, const char *subj, const char *room, bool now)
{
    show_or_hide(s_tt_row[i], true);
    label_set(s_tt_btext[i], badge);
    lv_obj_set_style_bg_color(s_tt_badge[i], lv_color_hex(badge_bg), 0);
    label_set(s_tt_subj[i], subj);
    label_set(s_tt_room[i], room);
    uint32_t fg = now ? 0x10213F : C_TEXT;
    lv_obj_set_style_bg_color(s_tt_row[i], lv_color_hex(now ? 0xDCEBFF : C_CARD2), 0);
    lv_obj_set_style_text_color(s_tt_subj[i], lv_color_hex(subj[0] == 0 || strcmp(subj, "공강") ? fg : C_DIM), 0);
    lv_obj_set_style_text_color(s_tt_room[i], lv_color_hex(now ? fg : C_DIM), 0);
    lv_obj_set_style_border_width(s_tt_row[i], 0, 0);
    lv_obj_set_style_opa(s_tt_row[i], LV_OPA_COVER, 0);
}

/* 오늘(수업 없는 날이면 다음 수업일) 시간표를 교시마다 한 줄로. 점심시간도 한 줄. */
static void refresh_tt(void)
{
    show_or_hide(s_tt_card, config_show('t'));
    const teachers_t *ts = g_data.teachers;
    int ti = teachers_find(g_cfg.owner);
    char buf[64];
    if (g_cfg.owner[0]) {
        snprintf(buf, sizeof(buf), "%s 선생님", g_cfg.owner);
    } else {
        strlcpy(buf, "오늘 시간표", sizeof(buf));
    }
    label_set(s_tt_title, buf);

    const char *msg = !ts ? "설치 페이지에서 교사 시간표 한글 파일을 넣어 주세요."
                    : ti < 0 ? "찾기 화면에서 선생님을 고른 뒤 '이 보드 주인으로'를 누르세요." : NULL;
    label_set(s_tt_msg, msg ? msg : "");
    show_or_hide(s_tt_msg, msg != NULL);
    if (msg) {
        label_set(s_tt_sub, "");
        for (int i = 0; i < TT_ROWS; i++) {
            show_or_hide(s_tt_row[i], false);
        }
        return;
    }

    int today = ymd_today();
    int day = today;
    for (int k = 0; k < 10 && !is_school_day(day); k++) {
        day = ymd_add(day, 1);
    }
    bool is_today = day == today;
    int wd = ymd_wday(day);
    if (is_today) {
        label_set(s_tt_sub, "오늘 시간표");
    } else {
        snprintf(buf, sizeof(buf), "다음 수업일 · %d/%d(%s)", day / 100 % 100, day % 100, WEEKDAYS[wd]);
        label_set(s_tt_sub, buf);
    }
    const teacher_t *t = &ts->t[ti];
    int d = wd - 1;
    int m = now_minute();
    int row = 0;
    for (int p = 0; p < TT_PERIODS && row < TT_ROWS; p++) {
        int st = ts->ptime[p];
        if (p > 0 && row < TT_ROWS) {
            /* 앞 교시가 끝나고 40분 넘게 비면 점심시간 */
            int prev_end = ts->ptime[p - 1] + LESSON_MIN;
            if (st - prev_end >= 40) {
                bool now = is_today && m >= prev_end && m < st;
                snprintf(buf, sizeof(buf), "%d:%02d ~ %d:%02d", prev_end / 60, prev_end % 60, st / 60, st % 60);
                tt_row(row++, "점심", now ? C_RED : 0x3A4F7E, "점심시간", buf, now);
            }
        }
        if (row >= TT_ROWS) {
            break;
        }
        bool has = t->subj[d][p][0] != 0;
        bool now = is_today && m >= st && m < st + LESSON_MIN;
        char badge[32];
        if (now) {
            snprintf(badge, sizeof(badge), "%d교시\n%d분 남음", p + 1, st + LESSON_MIN - m);
        } else {
            snprintf(badge, sizeof(badge), "%d교시\n%02d:%02d", p + 1, st / 60, st % 60);
        }
        tt_row(row++, badge, now ? C_RED : has ? C_ACCENT : 0x3A4F7E, has ? t->subj[d][p] : "공강",
               has ? t->room[d][p] : "", now);
        lv_obj_set_style_border_width(s_tt_row[row - 1], has && !now ? 2 : 0, 0);
        lv_obj_set_style_border_color(s_tt_row[row - 1], lv_color_hex(0x3E8F77), 0);
        lv_obj_set_style_opa(s_tt_row[row - 1], is_today && m >= st + LESSON_MIN ? LV_OPA_60 : LV_OPA_COVER, 0);
    }
    for (; row < TT_ROWS; row++) {
        show_or_hide(s_tt_row[row], false);
    }
}

/* 오늘의 학사 일정 · 부서 일정을 한 줄씩 */
static void refresh_school(void)
{
    show_or_hide(s_school, config_show('s'));
    const school_t *s = g_data.school;
    int today = ymd_today();
    int n = 0;
    char buf[200];
    for (int i = 0; s && i < s->acad_count && n < SCHOOL_LINES; i++) {
        if (s->acad[i].ymd == today) {
            snprintf(buf, sizeof(buf), "· %s", s->acad[i].name);
            label_set(s_school_line[n], buf);
            lv_obj_set_style_text_color(s_school_line[n++], lv_color_hex(C_WARM), 0);
        }
    }
    for (int i = 0; s && i < s->sched_count && n < SCHOOL_LINES; i++) {
        if (s->sched[i].start <= today && today <= s->sched[i].end) {
            snprintf(buf, sizeof(buf), "· %s", s->sched[i].title);
            label_set(s_school_line[n], buf);
            lv_obj_set_style_text_color(s_school_line[n++], lv_color_hex(C_TEXT), 0);
        }
    }
    if (n == 0) {
        label_set(s_school_line[0], s ? "오늘은 학교 일정이 없습니다" : g_cfg.sched_url[0] ? "받는 중…" : "학교 일정 주소를 넣어 주세요");
        lv_obj_set_style_text_color(s_school_line[0], lv_color_hex(C_DIM), 0);
        n = 1;
    }
    for (; n < SCHOOL_LINES; n++) {
        label_set(s_school_line[n], "");
    }
}

void home_refresh(int kind)
{
    if (kind == -1) {
        label_set(s_status, g_status_text);
        lv_obj_set_style_text_color(s_status, lv_color_hex(strstr(g_status_text, "받음") || strstr(g_status_text, "연결됨") ? C_GREEN : C_WARM), 0);
        return;
    }
    if (kind == D_WEATHER) {
        refresh_weather();
    }
    if (kind == D_MEALS) {
        refresh_meal();
    }
    if (kind == D_MYCAL) {
        refresh_my();
    }
    if (kind == D_TEACHERS || kind == D_SCHOOL) {
        refresh_tt();
    }
    if (kind == D_SCHOOL || kind == D_TEACHERS) {
        refresh_school();
    }
    if (kind == D_SCHOOL) {
        refresh_weather(); /* 휴일 색 */
    }
}

void home_tick(void)
{
    time_t now = time(NULL);
    if (now < 1735689600) {
        return;
    }
    struct tm tm;
    localtime_r(&now, &tm);
    int m = tm.tm_hour * 60 + tm.tm_min;
    if (m == s_last_min) {
        return;
    }
    bool first = s_last_min < 0;
    s_last_min = m;

    char buf[48];
    bool h12 = g_cfg.h12[0] == '1';
    int h = h12 ? (tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12) : tm.tm_hour;
    snprintf(buf, sizeof(buf), h12 ? "%d:%02d" : "%02d:%02d", h, tm.tm_min);
    label_set(s_time, buf);
    label_set(s_ampm, h12 ? (tm.tm_hour < 12 ? "오전" : "오후") : "");
    lv_obj_align_to(s_ampm, s_time, LV_ALIGN_OUT_LEFT_BOTTOM, -6, -24);
    snprintf(buf, sizeof(buf), "%d월 %d일 %s요일", tm.tm_mon + 1, tm.tm_mday, WEEKDAYS[tm.tm_wday]);
    label_set(s_date, buf);

    show_or_hide(s_setup, !g_cfg.ssid[0]);
    /* 매분: 지금 교시 강조, 저녁 7시 급식 넘기기 */
    refresh_tt();
    if (first || m == 19 * 60) {
        refresh_meal();
        refresh_weather();
        refresh_my();
        refresh_school();
    }
}
