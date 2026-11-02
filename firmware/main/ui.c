/* 화면 (1024 x 600)
 *
 *  ┌ 이름 ─────────────────────────────── 와이파이 · 갱신 ┐
 *  │ 시계·날짜      │ 일정 (오늘 / 7일)  │ 점심            │
 *  │ 날씨           │                    │ 시간표 (선택)   │
 *  └──────────────────────────────────────────────────────┘
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_lv_adapter.h"
#include "fonts_kr.h"
#include "lvgl.h"

#define C_BG     0x0B1220
#define C_CARD   0x162036
#define C_TEXT   0xE8EDF5
#define C_DIM    0x8A97AD
#define C_BLUE   0x5AA9FF
#define C_WARM   0xFFB547
#define C_GREEN  0x5BD6A0
#define C_PINK   0xFF7A90

#define TOP 60
#define GAP 8
#define COL_W 330
#define BODY_H (600 - TOP - GAP)

static const char *WEEKDAYS[] = { "일", "월", "화", "수", "목", "금", "토" };

static lv_obj_t *s_status, *s_time, *s_date;
static lv_obj_t *s_w_title, *s_w_temp, *s_w_cond, *s_w_detail, *s_w_day[2];
static lv_obj_t *s_cal_list;
static lv_obj_t *s_meal_card, *s_meal_title, *s_meal_menu, *s_meal_dinner;
static lv_obj_t *s_tt_card, *s_tt_title, *s_tt_rows[MAX_PERIODS];
static lv_obj_t *s_setup;
static char s_status_text[64] = "";
static char s_updated_text[32] = "";

/* ---------------- 도우미 ---------------- */

static void on_tap(lv_event_t *e)
{
    feeds_refresh_now();
}

static lv_obj_t *card(int x, int y, int w, int h)
{
    lv_obj_t *c = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_radius(c, 16, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 16, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(c, on_tap, LV_EVENT_CLICKED, NULL);
    return c;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *wrap_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = label(parent, font, color, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));
    return l;
}

static const char *weather_text(int code)
{
    if (code == 0) return "맑음";
    if (code <= 2) return "구름 조금";
    if (code == 3) return "흐림";
    if (code <= 48) return "안개";
    if (code <= 57) return "이슬비";
    if (code <= 67) return "비";
    if (code <= 77) return "눈";
    if (code <= 82) return "소나기";
    if (code <= 86) return "눈";
    return "뇌우";
}

static uint32_t weather_color(int code)
{
    if (code <= 1) return C_WARM;
    if (code <= 48) return C_DIM;
    if (code >= 71 && code <= 77) return C_TEXT;
    return C_BLUE;
}

static void refresh_status(void)
{
    char buf[112];
    snprintf(buf, sizeof(buf), "%s%s%s", s_status_text, s_updated_text[0] ? "  ·  " : "", s_updated_text);
    lv_label_set_text(s_status, buf);
}

/* ---------------- 시계 ---------------- */

static void clock_tick(lv_timer_t *t)
{
    time_t now = time(NULL);
    if (now < 1735689600) {
        lv_label_set_text(s_time, "--:--");
        lv_label_set_text(s_date, "");
        return;
    }
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[48];
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    if (strcmp(buf, lv_label_get_text(s_time))) {
        lv_label_set_text(s_time, buf);
    }
    snprintf(buf, sizeof(buf), "%d월 %d일 %s요일", tm.tm_mon + 1, tm.tm_mday, WEEKDAYS[tm.tm_wday]);
    if (strcmp(buf, lv_label_get_text(s_date))) {
        lv_label_set_text(s_date, buf);
    }
}

/* ---------------- 만들기 ---------------- */

void ui_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, on_tap, LV_EVENT_CLICKED, NULL);

    /* 위쪽 줄 */
    lv_obj_t *title = label(scr, &font_kr_26, C_TEXT, g_cfg.title);
    lv_obj_set_pos(title, 20, 16);
    s_status = label(scr, &font_kr_18, C_DIM, "");
    lv_obj_align(s_status, LV_ALIGN_TOP_RIGHT, -20, 20);

    /* 왼쪽: 시계 */
    int x = GAP;
    lv_obj_t *c = card(x, TOP, COL_W, 250);
    s_time = label(c, &font_num_130, C_TEXT, "--:--");
    lv_obj_align(s_time, LV_ALIGN_TOP_MID, 0, -6);
    s_date = label(c, &font_kr_36, C_BLUE, "");
    lv_obj_align(s_date, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* 왼쪽: 날씨 */
    c = card(x, TOP + 250 + GAP, COL_W, BODY_H - 250 - GAP);
    s_w_title = label(c, &font_kr_18, C_DIM, "날씨");
    s_w_temp = label(c, &font_num_60, C_TEXT, "");
    lv_obj_set_pos(s_w_temp, 0, 28);
    s_w_cond = label(c, &font_kr_26, C_WARM, g_cfg.lat[0] ? "불러오는 중…" : "위치 설정 필요");
    lv_obj_set_pos(s_w_cond, 150, 52);
    s_w_detail = label(c, &font_kr_18, C_TEXT, "");
    lv_obj_set_pos(s_w_detail, 0, 112);
    for (int i = 0; i < 2; i++) {
        s_w_day[i] = label(c, &font_kr_18, C_DIM, "");
        lv_obj_set_pos(s_w_day[i], 0, 156 + i * 34);
    }

    /* 가운데: 일정 */
    x += COL_W + GAP;
    c = card(x, TOP, COL_W, BODY_H);
    label(c, &font_kr_26, C_PINK, "일정");
    s_cal_list = lv_obj_create(c);
    lv_obj_remove_style_all(s_cal_list);
    lv_obj_set_pos(s_cal_list, 0, 44);
    lv_obj_set_size(s_cal_list, lv_pct(100), BODY_H - 32 - 44);
    lv_obj_set_flex_flow(s_cal_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_cal_list, 6, 0);
    lv_obj_clear_flag(s_cal_list, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    wrap_label(s_cal_list, &font_kr_22, C_DIM,
               g_cfg.ical[0] ? "불러오는 중…" : "설치 페이지에서 구글 캘린더 주소를 넣으면 여기에 일정이 나옵니다.");

    /* 오른쪽: 급식 + 시간표 */
    x += COL_W + GAP;
    int right_w = 1024 - x - GAP;
    bool has_tt = g_cfg.grade[0] && g_cfg.cls[0];
    int meal_h = has_tt ? 300 : BODY_H;
    s_meal_card = card(x, TOP, right_w, meal_h);
    s_meal_title = label(s_meal_card, &font_kr_26, C_WARM, "오늘 점심");
    s_meal_menu = wrap_label(s_meal_card, &font_kr_22, C_TEXT,
                             g_cfg.school[0] ? "불러오는 중…" : "설치 페이지에서 학교를 고르면 급식이 나옵니다.");
    lv_obj_set_pos(s_meal_menu, 0, 44);
    lv_obj_set_style_text_line_space(s_meal_menu, 6, 0);
    s_meal_dinner = wrap_label(s_meal_card, &font_kr_18, C_DIM, "");
    lv_obj_align(s_meal_dinner, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    s_tt_card = card(x, TOP + meal_h + GAP, right_w, BODY_H - meal_h - GAP);
    char buf[64];
    snprintf(buf, sizeof(buf), "시간표 · %s학년 %s반", g_cfg.grade, g_cfg.cls);
    s_tt_title = label(s_tt_card, &font_kr_26, C_GREEN, buf);
    for (int i = 0; i < MAX_PERIODS; i++) {
        s_tt_rows[i] = label(s_tt_card, &font_kr_22, C_TEXT, "");
        lv_obj_set_pos(s_tt_rows[i], (i / 4) * 150, 42 + (i % 4) * 36);
        lv_label_set_long_mode(s_tt_rows[i], LV_LABEL_LONG_DOT);
        lv_obj_set_size(s_tt_rows[i], 145, 30);
    }
    if (!has_tt) {
        lv_obj_add_flag(s_tt_card, LV_OBJ_FLAG_HIDDEN);
    }

    /* 처음 설정 안내 */
    if (!g_cfg.ssid[0]) {
        s_setup = card(162, 170, 700, 260);
        lv_obj_set_style_bg_color(s_setup, lv_color_hex(0x22304F), 0);
        lv_obj_set_style_shadow_width(s_setup, 40, 0);
        lv_obj_t *h = label(s_setup, &font_kr_26, C_WARM, "처음 설정이 필요합니다");
        lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 8);
        lv_obj_t *b = wrap_label(s_setup, &font_kr_22, C_TEXT,
                                 "1. 보드 뒷면 스위치를 UART1 쪽으로 두고\n"
                                 "    UART 단자를 컴퓨터에 꽂으세요.\n"
                                 "2. 컴퓨터에서 설치 페이지를 열고\n"
                                 "    와이파이와 학교를 넣은 뒤 '설정 보내기'를 누르세요.");
        lv_obj_set_pos(b, 10, 60);
        lv_obj_set_style_text_line_space(b, 8, 0);
    }

    lv_timer_create(clock_tick, 1000, NULL);
    clock_tick(NULL);
}

/* ---------------- 자료 넣기 ---------------- */

static bool lock(void)
{
    return esp_lv_adapter_lock(-1) == ESP_OK;
}

static void unlock(void)
{
    esp_lv_adapter_unlock();
}

void ui_set_status(bool configured, bool connected, const char *ip)
{
    char buf[64];
    snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI "  %s",
             !configured ? "와이파이 설정 필요" : connected ? "연결됨" : "연결 중…");
    if (!strcmp(buf, s_status_text)) {
        return;
    }
    strlcpy(s_status_text, buf, sizeof(s_status_text));
    if (lock()) {
        refresh_status();
        unlock();
    }
}

void ui_set_updated(time_t when)
{
    if (when <= 0) {
        return;
    }
    struct tm tm;
    localtime_r(&when, &tm);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d 갱신", tm.tm_hour, tm.tm_min);
    if (!strcmp(buf, s_updated_text)) {
        return;
    }
    strlcpy(s_updated_text, buf, sizeof(s_updated_text));
    if (lock()) {
        refresh_status();
        unlock();
    }
}

void ui_set_weather(const weather_t *w)
{
    char temp[16], detail[96], day[2][96], title[80];
    snprintf(title, sizeof(title), "날씨 · %s", g_cfg.place[0] ? g_cfg.place : "현재 위치");
    snprintf(temp, sizeof(temp), "%.0f°", w->temp);
    snprintf(detail, sizeof(detail), "최고 %.0f°  ·  최저 %.0f°  ·  습도 %d%%",
             w->day_max[0], w->day_min[0], w->humidity);
    static const char *names[] = { "내일", "모레" };
    for (int i = 0; i < 2; i++) {
        snprintf(day[i], sizeof(day[i]), "%s  %s  %.0f° / %.0f°  강수 %d%%", names[i],
                 weather_text(w->day_code[i + 1]), w->day_max[i + 1], w->day_min[i + 1], w->day_pop[i + 1]);
    }
    if (!lock()) {
        return;
    }
    lv_label_set_text(s_w_title, title);
    lv_label_set_text(s_w_temp, temp);
    lv_label_set_text(s_w_cond, weather_text(w->code));
    lv_obj_set_style_text_color(s_w_cond, lv_color_hex(weather_color(w->code)), 0);
    lv_label_set_text(s_w_detail, detail);
    lv_label_set_text(s_w_day[0], day[0]);
    lv_label_set_text(s_w_day[1], day[1]);
    unlock();
}

void ui_set_meal(const meal_t *m)
{
    char dinner[640] = "";
    if (m->dinner[0]) {
        /* 저녁은 한 줄로: "저녁  밥, 국, ..." */
        size_t o = strlcpy(dinner, "저녁  ", sizeof(dinner));
        for (const char *p = m->dinner; *p && o < sizeof(dinner) - 3; p++) {
            if (*p == '\n') {
                dinner[o++] = ',';
                dinner[o++] = ' ';
            } else {
                dinner[o++] = *p;
            }
        }
        dinner[o] = 0;
    }
    if (!lock()) {
        return;
    }
    lv_label_set_text(s_meal_title, m->tomorrow ? "내일 점심" : "오늘 점심");
    lv_label_set_text(s_meal_menu, m->lunch[0] ? m->lunch : "급식이 없는 날입니다.");
    lv_label_set_text(s_meal_dinner, dinner);
    unlock();
}

void ui_set_timetable(const timetable_t *tt, bool tomorrow)
{
    if (!lock()) {
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%s시간표 · %s학년 %s반", tomorrow ? "내일 " : "", g_cfg.grade, g_cfg.cls);
    lv_label_set_text(s_tt_title, buf);
    for (int i = 0; i < MAX_PERIODS; i++) {
        if (i < tt->count) {
            snprintf(buf, sizeof(buf), "%d  %s", i + 1, tt->subject[i][0] ? tt->subject[i] : "-");
        } else {
            buf[0] = 0;
        }
        lv_label_set_text(s_tt_rows[i], i == 0 && tt->count == 0 ? "수업 없음" : buf);
    }
    unlock();
}

static void time_text(int minute, char *buf, size_t size)
{
    if (minute < 0) {
        strlcpy(buf, "종일", size);
    } else {
        snprintf(buf, size, "%02d:%02d", minute / 60, minute % 60);
    }
}

void ui_set_calendar(const calendar_t *cal)
{
    if (!lock()) {
        return;
    }
    lv_obj_clean(s_cal_list);
    int today = 0;
    for (int i = 0; i < cal->count; i++) {
        today += cal->ev[i].day == 0;
    }
    if (today == 0) {
        label(s_cal_list, &font_kr_22, C_DIM, "오늘은 일정이 없습니다.");
    }
    int last_day = 0;
    time_t now = time(NULL);
    for (int i = 0; i < cal->count; i++) {
        const cal_event_t *ev = &cal->ev[i];
        char t[16], buf[140];
        time_text(ev->minute, t, sizeof(t));
        if (ev->day != last_day) {
            /* 날짜 머리글: "11월 3일 (화)" */
            time_t d = now + ev->day * 86400;
            struct tm tm;
            localtime_r(&d, &tm);
            snprintf(buf, sizeof(buf), "%s%d월 %d일 (%s)", ev->day == 1 ? "내일 · " : "",
                     tm.tm_mon + 1, tm.tm_mday, WEEKDAYS[tm.tm_wday]);
            lv_obj_t *h = label(s_cal_list, &font_kr_18, C_BLUE, buf);
            lv_obj_set_style_pad_top(h, 10, 0);
            last_day = ev->day;
        }
        snprintf(buf, sizeof(buf), "%s   %s", t, ev->title);
        wrap_label(s_cal_list, ev->day == 0 ? &font_kr_22 : &font_kr_18,
                   ev->day == 0 ? C_TEXT : C_DIM, buf);
    }
    unlock();
}
