/* 홈 화면 (위쪽 줄 없이 꽉 차게)
 *
 *  ┌ 시계 · 날짜 · 요일 ────┐ ┌ 황인환 선생님 · 이번 주 시간표 ┐
 *  ├ 날씨 · 미세먼지 · 주간 ┤ │  월 화 수 목 금 × 1~7교시        │
 *  ├ 오늘 점심 · 저녁 ──────┤ ├ 오늘 학교 일정 ─────────────────┤
 *  └────────────────────────┘ └─────────────────────────────────┘
 */
#include <stdlib.h>

#include "ui_priv.h"

#define LEFT_W 440
#define RIGHT_X (PAD + LEFT_W + PAD)
#define RIGHT_W (SCR_W - RIGHT_X - PAD)
#define CLOCK_H 206
#define WEATHER_H 178
#define MEAL_Y (PAD + CLOCK_H + PAD + WEATHER_H + PAD)
#define TODAY_H 84

static lv_obj_t *s_time, *s_ampm, *s_date, *s_status;
static lv_obj_t *s_weather, *s_w_temp, *s_w_cond, *s_w_range, *s_w_dust, *s_w_dust_lv, *s_w_place;
static lv_obj_t *s_w_day[6], *s_w_day_t[6];
static lv_obj_t *s_meal, *s_meal_title, *s_meal_lunch, *s_meal_dinner;
static lv_obj_t *s_tt_card, *s_tt_title, *s_tt_week;
static tt_grid_t s_grid;
static lv_obj_t *s_today, *s_today_text;
static lv_obj_t *s_setup;
static int s_last_min = -1;

static void on_settings(lv_event_t *e)
{
    ui_go(PG_SET);
}

static void show_or_hide(lv_obj_t *o, bool show)
{
    if (show) {
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

void home_build(lv_obj_t *page)
{
    /* 시계 */
    lv_obj_t *c = mk_card(page, PAD, PAD, LEFT_W, CLOCK_H);
    lv_obj_t *gear = mk_label(c, &lv_font_montserrat_24, C_DIM, LV_SYMBOL_SETTINGS);
    lv_obj_add_flag(gear, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(gear, 16);
    lv_obj_add_event_cb(gear, on_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_set_pos(gear, -2, -4);
    s_status = mk_label(c, &font_kr_18, C_GREEN, "");
    lv_obj_align(s_status, LV_ALIGN_TOP_RIGHT, 0, -4);
    s_time = mk_label(c, &font_num_130, C_TEXT, "--:--");
    lv_obj_align(s_time, LV_ALIGN_TOP_MID, 0, 6);
    s_ampm = mk_label(c, &font_kr_22, C_DIM, "");
    lv_obj_align_to(s_ampm, s_time, LV_ALIGN_OUT_LEFT_BOTTOM, -6, -24);
    s_date = mk_label(c, &font_kr_36, C_BLUE, "");
    lv_obj_align(s_date, LV_ALIGN_BOTTOM_MID, 0, 2);

    /* 날씨 */
    s_weather = c = mk_card(page, PAD, PAD + CLOCK_H + PAD, LEFT_W, WEATHER_H);
    s_w_temp = mk_label(c, &font_num_60, C_TEXT, "");
    lv_obj_set_pos(s_w_temp, 0, -6);
    s_w_cond = mk_label(c, &font_kr_22, C_WARM, g_cfg.lat[0] ? "날씨 받는 중…" : "위치 설정 필요");
    lv_obj_set_pos(s_w_cond, 130, 0);
    s_w_range = mk_label(c, &font_kr_18, C_DIM, "");
    lv_obj_set_pos(s_w_range, 130, 30);
    s_w_place = mk_label(c, &font_kr_18, C_DIM, "");
    lv_obj_align(s_w_place, LV_ALIGN_TOP_RIGHT, 0, 0);
    s_w_dust_lv = mk_label(c, &font_kr_22, C_GREEN, "");
    lv_obj_align(s_w_dust_lv, LV_ALIGN_TOP_RIGHT, 0, 24);
    s_w_dust = mk_label(c, &font_kr_18, C_DIM, "");
    lv_obj_align(s_w_dust, LV_ALIGN_TOP_RIGHT, 0, 52);
    for (int i = 0; i < 6; i++) {
        lv_obj_t *col = mk_box(c, i * 69, 84, 66, 66);
        s_w_day[i] = mk_label(col, &font_kr_18, C_DIM, "");
        lv_obj_align(s_w_day[i], LV_ALIGN_TOP_MID, 0, 0);
        s_w_day_t[i] = mk_label(col, &font_kr_18, C_TEXT, "");
        lv_obj_set_style_text_align(s_w_day_t[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_w_day_t[i], LV_ALIGN_TOP_MID, 0, 22);
    }

    /* 급식 */
    s_meal = c = mk_card(page, PAD, MEAL_Y, LEFT_W, SCR_H - MEAL_Y - PAD);
    s_meal_title = mk_label(c, &font_kr_22, C_WARM, "오늘 점심");
    lv_obj_align(mk_link(c, "급식", PG_MEAL), LV_ALIGN_TOP_RIGHT, 0, 0);
    s_meal_lunch = mk_wrap(c, &font_kr_22, C_TEXT, g_cfg.school[0] ? "급식 받는 중…" : "설치 페이지에서 학교를 고르면 급식이 나옵니다.");
    lv_obj_set_pos(s_meal_lunch, 0, 34);
    lv_obj_set_style_text_line_space(s_meal_lunch, 4, 0);
    s_meal_dinner = mk_wrap(c, &font_kr_18, C_DIM, "");
    lv_obj_align(s_meal_dinner, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* 시간표 */
    s_tt_card = c = mk_card(page, RIGHT_X, PAD, RIGHT_W, SCR_H - PAD * 3 - TODAY_H);
    s_tt_title = mk_label(c, &font_kr_26, C_TEXT, "");
    s_tt_week = mk_label(c, &font_kr_18, C_DIM, "");
    lv_obj_align_to(s_tt_week, s_tt_title, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -2);
    lv_obj_align(mk_link(c, "찾기", PG_FIND), LV_ALIGN_TOP_RIGHT, 0, 2);
    tt_grid_create(&s_grid, c, 0, 40, RIGHT_W - 30, SCR_H - PAD * 3 - TODAY_H - 30 - 40);

    /* 오늘 학교 일정 */
    s_today = c = mk_card(page, RIGHT_X, SCR_H - PAD - TODAY_H, RIGHT_W, TODAY_H);
    lv_obj_set_style_pad_ver(c, 8, 0);
    lv_obj_t *h = mk_label(c, &font_kr_18, C_PINK, "오늘");
    lv_obj_set_pos(h, 0, 0);
    lv_obj_align(mk_link(c, "학교일정", PG_SCHOOL), LV_ALIGN_TOP_RIGHT, 0, 0);
    s_today_text = mk_label(c, &font_kr_18, C_TEXT, "");
    lv_obj_set_pos(s_today_text, 52, 0);
    lv_obj_set_size(s_today_text, RIGHT_W - 30 - 52 - 96, TODAY_H - 16);
    lv_label_set_long_mode(s_today_text, LV_LABEL_LONG_CLIP);

    /* 처음 설정 안내 */
    s_setup = mk_card(page, 162, 170, 700, 260);
    lv_obj_set_style_bg_color(s_setup, lv_color_hex(0x22304F), 0);
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

static void refresh_weather(void)
{
    const weather_t *w = g_data.weather;
    show_or_hide(s_weather, config_show('w'));
    if (!w || !w->ok) {
        return;
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "%.0f°", w->temp);
    label_set(s_w_temp, buf);
    label_set(s_w_cond, weather_text(w->code));
    lv_obj_set_style_text_color(s_w_cond, lv_color_hex(weather_color(w->code)), 0);
    snprintf(buf, sizeof(buf), "오늘 %.0f° / %.0f°  ·  강수 %d%%", w->day_max[0], w->day_min[0], w->day_pop[0]);
    label_set(s_w_range, buf);
    label_set(s_w_place, g_cfg.place);

    bool dust = w->air_ok && config_show('d');
    if (dust) {
        uint32_t color;
        dust_text(w->pm10, w->pm25, buf, sizeof(buf), &color);
        label_set(s_w_dust_lv, buf);
        lv_obj_set_style_text_color(s_w_dust_lv, lv_color_hex(color), 0);
        snprintf(buf, sizeof(buf), "미세 %d · 초미세 %d", w->pm10, w->pm25);
        label_set(s_w_dust, buf);
    }
    show_or_hide(s_w_dust_lv, dust);
    show_or_hide(s_w_dust, dust);
    show_or_hide(s_w_place, !dust);

    int today = ymd_today();
    for (int i = 0; i < 6; i++) {
        int ymd = ymd_add(today, i + 1);
        int wd = ymd_wday(ymd);
        bool holiday;
        school_day_info(ymd, &holiday);
        snprintf(buf, sizeof(buf), "%d일(%s)", ymd % 100, WEEKDAYS[wd]);
        label_set(s_w_day[i], buf);
        lv_obj_set_style_text_color(s_w_day[i], lv_color_hex(wd == 0 || holiday ? C_PINK : wd == 6 ? C_BLUE : C_DIM), 0);
        snprintf(buf, sizeof(buf), "%s\n%.0f°/%.0f°", weather_text(w->day_code[i + 1]), w->day_max[i + 1],
                 w->day_min[i + 1]);
        label_set(s_w_day_t[i], buf);
        lv_obj_set_style_text_color(s_w_day_t[i], lv_color_hex(weather_color(w->day_code[i + 1])), 0);
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
    char title[48], buf[700];
    if (!d) {
        label_set(s_meal_title, "점심");
        label_set(s_meal_lunch, "급식 정보가 없습니다.");
        label_set(s_meal_dinner, "");
        return;
    }
    int diff = ymd_diff(d->ymd, today);
    if (diff == 0) {
        strlcpy(title, "오늘 점심", sizeof(title));
    } else if (diff == 1) {
        strlcpy(title, "내일 점심", sizeof(title));
    } else {
        snprintf(title, sizeof(title), "%d일(%s) 점심", d->ymd % 100, WEEKDAYS[ymd_wday(d->ymd)]);
    }
    label_set(s_meal_title, title);
    join_menu(d->lunch[0] ? d->lunch : "점심 없음", " · ", buf, sizeof(buf));
    label_set(s_meal_lunch, buf);
    if (d->dinner[0]) {
        char menu[640];
        join_menu(d->dinner, " · ", menu, sizeof(menu));
        snprintf(buf, sizeof(buf), "저녁  %s", menu);
    } else {
        buf[0] = 0;
    }
    label_set(s_meal_dinner, buf);
}

static void refresh_tt(void)
{
    show_or_hide(s_tt_card, config_show('t'));
    char buf[64];
    if (g_cfg.owner[0]) {
        snprintf(buf, sizeof(buf), "%s 선생님", g_cfg.owner);
    } else {
        strlcpy(buf, "이번 주 시간표", sizeof(buf));
    }
    label_set(s_tt_title, buf);
    int mon = ymd_week_monday(), fri = ymd_add(mon, 4);
    snprintf(buf, sizeof(buf), "%d/%d ~ %d/%d", mon / 100 % 100, mon % 100, fri / 100 % 100, fri % 100);
    label_set(s_tt_week, buf);
    lv_obj_align_to(s_tt_week, s_tt_title, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, -2);
    int i = teachers_find(g_cfg.owner);
    tt_grid_fill(&s_grid, i);
    if (g_data.teachers && i < 0) {
        label_set(s_grid.msg, "찾기 화면에서 선생님을 고른 뒤\n'이 보드 주인으로'를 누르세요.");
    }
}

/* 오늘의 학사 일정 · 부서 일정 · 내 일정을 한 줄씩 */
static void refresh_today(void)
{
    show_or_hide(s_today, config_show('s'));
    char buf[512];
    size_t o = 0;
    buf[0] = 0;
    int today = ymd_today();
    const school_t *s = g_data.school;
    const calendar_t *c = g_data.mycal;
    for (int i = 0; s && i < s->acad_count && o < sizeof(buf) - 80; i++) {
        if (s->acad[i].ymd == today) {
            o += snprintf(buf + o, sizeof(buf) - o, "%s[%s] %s", o ? "\n" : "", s->acad[i].type, s->acad[i].name);
        }
    }
    for (int i = 0; c && i < c->count && o < sizeof(buf) - 80; i++) {
        if (c->ev[i].day == 0) {
            o += snprintf(buf + o, sizeof(buf) - o, "%s%s", o ? "\n" : "", c->ev[i].title);
        }
    }
    for (int i = 0; s && i < s->sched_count && o < sizeof(buf) - 80; i++) {
        if (s->sched[i].start <= today && today <= s->sched[i].end) {
            o += snprintf(buf + o, sizeof(buf) - o, "%s%s", o ? "\n" : "", s->sched[i].title);
        }
    }
    label_set(s_today_text, o ? buf : (s || c) ? "오늘은 일정이 없습니다." : "학교 일정 주소를 넣으면 여기에 나옵니다.");
}

void home_refresh(int kind)
{
    if (kind == -1) {
        label_set(s_status, g_status_text);
        lv_obj_set_style_text_color(s_status, lv_color_hex(strstr(g_status_text, "받음") || strstr(g_status_text, "연결됨") ? C_GREEN : C_WARM), 0);
        lv_obj_align(s_status, LV_ALIGN_TOP_RIGHT, 0, -4);
        return;
    }
    if (kind == D_WEATHER) {
        refresh_weather();
    }
    if (kind == D_MEALS) {
        refresh_meal();
    }
    if (kind == D_TEACHERS || kind == D_SCHOOL) {
        refresh_tt();
    }
    if (kind == D_SCHOOL || kind == D_MYCAL || kind == D_TEACHERS) {
        refresh_today();
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
        refresh_today();
    }
}
