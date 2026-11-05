/* 화면 뼈대: 위쪽 줄(메뉴), 화면 바꾸기, 밤에 화면 끄기, 홈으로 돌아가기
 *
 *  홈 화면은 위쪽 줄 없이 꽉 차게, 다른 화면은 위쪽 줄 + 내용입니다.
 *  ┌ 교 황인환 선생님 │ 홈 학교일정 창체계획 찾기 캘린더 급식 설정 │ 와이파이  19:48 ┐
 */
#include "ui.h"

#include <stdlib.h>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "io_extension.h"
#include "rgb_lcd_port.h"
#include "ui_priv.h"

static lv_obj_t *s_pages[PG_COUNT];
static lv_obj_t *s_bar, *s_tabs[PG_COUNT], *s_badge, *s_name, *s_status, *s_clock, *s_date, *s_batt;
char g_batt_text[24] = "";
static int s_page = PG_HOME;
static bool s_screen_off;
static int s_last_day;
char g_status_text[64] = "";
static time_t s_updated;
static bool s_connected, s_configured;

static const char *TAB_NAMES[PG_COUNT] = { "홈", "학교일정", "창체계획", "찾기", "캘린더", "급식", "설정" };

/* ---------------- 만들기 도우미 ---------------- */

lv_obj_t *mk_box(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    return b;
}

lv_obj_t *mk_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_radius(c, 16, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_pad_all(c, 14, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t *mk_wrap(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = mk_label(parent, font, color, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));
    return l;
}

lv_obj_t *mk_btn(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_event_cb_t cb, void *user)
{
    /* 기본 테마의 눌림·초점 색(보라빛)을 빼고 우리 색만 씁니다 */
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_pad_all(b, 6, 0);
    lv_obj_set_style_opa(b, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_t *l = mk_label(b, font, C_TEXT, text);
    lv_obj_center(l);
    btn_set_on(b, false);
    if (cb) {
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    }
    return b;
}

void btn_set_on(lv_obj_t *btn, bool on)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(on ? C_ACCENT : C_BTN), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(on ? 0x7FB0FF : C_BTN_LINE), 0);
    lv_obj_t *l = lv_obj_get_child(btn, 0);
    if (l) {
        lv_obj_set_style_text_color(l, lv_color_hex(on ? 0xFFFFFF : 0xDCE3EE), 0);
    }
}

static void on_link(lv_event_t *e)
{
    ui_go((int)(intptr_t)lv_event_get_user_data(e));
}

lv_obj_t *mk_link(lv_obj_t *parent, const char *text, int page)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "%s " LV_SYMBOL_RIGHT, text);
    lv_obj_t *l = mk_label(parent, &font_kr_18, C_BLUE, buf);
    lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(l, 12);
    lv_obj_add_event_cb(l, on_link, LV_EVENT_CLICKED, (void *)(intptr_t)page);
    return l;
}

lv_obj_t *mk_list(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *l = lv_obj_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, h);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(l, 6, 0);
    lv_obj_set_scroll_dir(l, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_ACTIVE);
    return l;
}

void label_set(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text)) {
        lv_label_set_text(label, text);
    }
}

/* ---------------- 글자 만들기 ---------------- */

const char *weather_text(int code)
{
    if (code == 0) return "맑음";
    if (code <= 2) return "구름";
    if (code == 3) return "흐림";
    if (code <= 48) return "안개";
    if (code <= 57) return "이슬비";
    if (code <= 67) return "비";
    if (code <= 77) return "눈";
    if (code <= 82) return "소나기";
    if (code <= 86) return "눈";
    return "뇌우";
}

uint32_t weather_color(int code)
{
    if (code <= 1) return C_WARM;
    if (code <= 48) return C_DIM;
    if (code >= 71 && code <= 77) return C_TEXT;
    return C_BLUE;
}

/* 환경부 기준: 미세 30/80/150, 초미세 15/35/75 */
void dust_text(int pm10, int pm25, char *buf, size_t size, uint32_t *color)
{
    int a = pm10 <= 30 ? 0 : pm10 <= 80 ? 1 : pm10 <= 150 ? 2 : 3;
    int b = pm25 <= 15 ? 0 : pm25 <= 35 ? 1 : pm25 <= 75 ? 2 : 3;
    int lv = a > b ? a : b;
    static const char *NAMES[] = { "좋음", "보통", "나쁨", "매우 나쁨" };
    static const uint32_t COLORS[] = { C_BLUE, C_GREEN, C_WARM, C_RED };
    snprintf(buf, size, "%s", NAMES[lv]);
    *color = COLORS[lv];
}

void join_menu(const char *menu, const char *sep, char *buf, size_t size)
{
    size_t o = 0, sl = strlen(sep);
    for (const char *p = menu; *p && o < size - 1 - sl; p++) {
        if (*p == '\n') {
            memcpy(buf + o, sep, sl);
            o += sl;
        } else {
            buf[o++] = *p;
        }
    }
    buf[o] = 0;
}

void date_text(int ymd, char *buf, size_t size)
{
    snprintf(buf, size, "%d월 %d일 (%s)", ymd / 100 % 100, ymd % 100, WEEKDAYS[ymd_wday(ymd)]);
}

int now_minute(void)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    return tm.tm_hour * 60 + tm.tm_min;
}

bool is_school_day(int ymd)
{
    int wd = ymd_wday(ymd);
    bool holiday;
    school_day_info(ymd, &holiday);
    return wd != 0 && wd != 6 && !holiday;
}

/* ---------------- 날씨 그림 ----------------
 *  해 · 구름 · 비 · 눈 · 번개를 동그라미와 막대로 그립니다 (그림 파일 없이). */

static lv_obj_t *dot(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *o = mk_box(parent, x, y, w, h);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

lv_obj_t *mk_wx_icon(lv_obj_t *parent, int size)
{
    lv_obj_t *b = mk_box(parent, 0, 0, size, size);
    lv_obj_set_style_clip_corner(b, false, 0);
    return b;
}

void wx_icon_set(lv_obj_t *icon, int code)
{
    lv_obj_clean(icon);
    int s = lv_obj_get_width(icon);
    bool sun = code <= 2;
    bool cloud = code >= 1;
    bool rain = (code >= 51 && code <= 67) || (code >= 80 && code <= 82);
    bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
    bool storm = code >= 95;
    if (sun) {
        int d = cloud ? s * 46 / 100 : s * 60 / 100;
        int x = cloud ? s * 8 / 100 : (s - d) / 2;
        int y = cloud ? s * 6 / 100 : (s - d) / 2;
        lv_obj_t *o = dot(icon, x, y, d, d, C_WARM);
        lv_obj_set_style_shadow_width(o, s / 4, 0);
        lv_obj_set_style_shadow_color(o, lv_color_hex(C_WARM), 0);
        lv_obj_set_style_shadow_opa(o, LV_OPA_40, 0);
    }
    if (!cloud) {
        return;
    }
    uint32_t cc = code >= 45 && !sun ? 0xC9D3E3 : 0xE6ECF5;
    int up = (rain || snow || storm) ? s * 14 / 100 : 0;
    dot(icon, s * 26 / 100, s * 22 / 100 - up, s * 40 / 100, s * 40 / 100, cc);
    dot(icon, s * 48 / 100, s * 32 / 100 - up, s * 32 / 100, s * 32 / 100, cc);
    dot(icon, s * 8 / 100, s * 44 / 100 - up, s * 84 / 100, s * 28 / 100, cc);
    int y = s * 76 / 100 - up;
    for (int i = 0; i < 3; i++) {
        int x = s * (26 + i * 20) / 100;
        if (rain) {
            dot(icon, x, y, s / 14 + 1, s * 22 / 100, C_BLUE);
        } else if (snow) {
            dot(icon, x, y + s / 20, s / 8, s / 8, 0xFFFFFF);
        } else if (storm && i == 1) {
            dot(icon, x, y, s / 10, s * 24 / 100, C_WARM);
        }
    }
}

/* ---------------- 메뉴 (홈에서 시계를 누르면) ---------------- */

static lv_obj_t *s_menu;

static void menu_close(void)
{
    if (s_menu) {
        lv_obj_del(s_menu);
        s_menu = NULL;
    }
}

static void on_menu_bg(lv_event_t *e)
{
    if (lv_event_get_target(e) == s_menu) {
        menu_close();
    }
}

static void on_menu_item(lv_event_t *e)
{
    int page = (int)(intptr_t)lv_event_get_user_data(e);
    menu_close();
    ui_go(page);
}

void ui_menu_open(void)
{
    static const int PAGES[6] = { PG_SCHOOL, PG_CHA, PG_FIND, PG_CAL, PG_MEAL, PG_SET };
    static const char *SUB[6] = { "이달 학교 일정", "창의적 체험활동", "선생님 시간표", "내 구글 캘린더", "식단표", "와이파이 · 화면" };
    static const uint32_t COLORS[6] = { C_PINK, C_GREEN, C_BLUE, C_SKY, C_WARM, C_DIM };
    menu_close();
    s_menu = mk_box(lv_layer_top(), 0, 0, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(s_menu, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_menu, LV_OPA_60, 0);
    lv_obj_add_flag(s_menu, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_menu, on_menu_bg, LV_EVENT_CLICKED, NULL);

    const int bw = 236, bh = 150, gap = 16;
    int w = bw * 3 + gap * 2 + 40, h = bh * 2 + gap + 96;
    lv_obj_t *box = mk_card(s_menu, (SCR_W - w) / 2, (SCR_H - h) / 2, w, h);
    lv_obj_set_style_pad_all(box, 20, 0);
    mk_label(box, &font_kr_26, C_TEXT, "메뉴");
    lv_obj_t *x = mk_label(box, &font_kr_22, C_DIM, "닫기 " LV_SYMBOL_CLOSE);
    lv_obj_align(x, LV_ALIGN_TOP_RIGHT, 0, 2);
    lv_obj_add_flag(x, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(x, 16);
    lv_obj_add_event_cb(x, on_menu_item, LV_EVENT_CLICKED, (void *)(intptr_t)PG_HOME);
    for (int i = 0; i < 6; i++) {
        lv_obj_t *b = lv_btn_create(box);
        lv_obj_set_pos(b, (i % 3) * (bw + gap), 52 + (i / 3) * (bh + gap));
        lv_obj_set_size(b, bw, bh);
        lv_obj_set_style_bg_color(b, lv_color_hex(C_CARD2), 0);
        lv_obj_set_style_radius(b, 16, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(COLORS[i]), 0);
        lv_obj_add_event_cb(b, on_menu_item, LV_EVENT_CLICKED, (void *)(intptr_t)PAGES[i]);
        lv_obj_t *t = mk_label(b, &font_kr_26, COLORS[i], TAB_NAMES[PAGES[i]]);
        lv_obj_align(t, LV_ALIGN_CENTER, 0, -16);
        lv_obj_t *s = mk_label(b, &font_kr_18, C_DIM, SUB[i]);
        lv_obj_align(s, LV_ALIGN_CENTER, 0, 22);
    }
}

/* ---------------- 하루 자세히 (달력 칸을 누르면) ---------------- */

static lv_obj_t *s_pop;

static void on_pop_close(lv_event_t *e)
{
    if (s_pop) {
        lv_obj_del_async(s_pop);
        s_pop = NULL;
    }
}

void ui_popup(const char *title, const char *body)
{
    if (s_pop) {
        lv_obj_del(s_pop);
    }
    s_pop = mk_box(lv_layer_top(), 0, 0, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(s_pop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_pop, LV_OPA_60, 0);
    lv_obj_add_flag(s_pop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_pop, on_pop_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *box = mk_card(s_pop, 172, 90, 680, 420);
    lv_obj_set_style_pad_all(box, 20, 0);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, on_pop_close, LV_EVENT_CLICKED, NULL);
    mk_label(box, &font_kr_26, C_TEXT, title);
    lv_obj_t *x = mk_label(box, &font_kr_18, C_DIM, "누르면 닫힘");
    lv_obj_align(x, LV_ALIGN_TOP_RIGHT, 0, 4);
    lv_obj_t *list = mk_list(box, 0, 46, 640, 420 - 40 - 46);
    lv_obj_t *l = mk_wrap(list, &font_kr_22, C_TEXT, body);
    lv_label_set_recolor(l, true);
    lv_obj_set_style_text_line_space(l, 10, 0);
}

/* ---------------- 위쪽 줄 ---------------- */

static void on_tab(lv_event_t *e)
{
    ui_go((int)(intptr_t)lv_event_get_user_data(e));
}

static void bar_name(void)
{
    char buf[64];
    if (g_cfg.owner[0]) {
        snprintf(buf, sizeof(buf), "%s 선생님", g_cfg.owner);
    } else {
        strlcpy(buf, g_cfg.title, sizeof(buf));
    }
    label_set(s_name, buf);
    char first[8] = "";
    const char *src = g_cfg.title[0] ? g_cfg.title : "오";
    int n = ((unsigned char)src[0] & 0xE0) == 0xE0 ? 3 : 1; /* 첫 글자 */
    memcpy(first, src, n);
    first[n] = 0;
    label_set(lv_obj_get_child(s_badge, 0), first);
}

static void bar_build(lv_obj_t *scr)
{
    s_bar = mk_box(scr, 0, 0, SCR_W, BAR_H);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x0B1220), 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(s_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(s_bar, 1, 0);
    lv_obj_set_style_border_color(s_bar, lv_color_hex(C_LINE), 0);

    s_badge = mk_box(s_bar, 12, 10, 44, 44);
    lv_obj_set_style_radius(s_badge, 12, 0);
    lv_obj_set_style_bg_opa(s_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_badge, lv_color_hex(C_RED), 0);
    lv_obj_set_style_bg_grad_color(s_badge, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_bg_grad_dir(s_badge, LV_GRAD_DIR_HOR, 0);
    lv_obj_center(mk_label(s_badge, &font_kr_22, C_TEXT, ""));

    s_name = mk_label(s_bar, &font_kr_22, C_TEXT, "");
    lv_obj_set_pos(s_name, 66, 20);
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_DOT);
    lv_obj_set_size(s_name, 168, 26);   /* 한 줄 */

    /* 메뉴: 칸마다 글자 너비 + 좌우 여백이 같게 */
    const int tab_pad = 24, tab_gap = 2;
    int tab_w[PG_COUNT], tw = 8 - tab_gap;
    for (int i = 0; i < PG_COUNT; i++) {
        lv_point_t sz;
        lv_txt_get_size(&sz, TAB_NAMES[i], &font_kr_18, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        tab_w[i] = sz.x + tab_pad;
        tw += tab_w[i] + tab_gap;
    }
    lv_obj_t *tabs = mk_box(s_bar, 244, 8, tw, 48);
    lv_obj_set_style_bg_color(tabs, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tabs, 12, 0);
    lv_obj_set_style_border_width(tabs, 1, 0);
    lv_obj_set_style_border_color(tabs, lv_color_hex(C_LINE), 0);
    for (int i = 0, x = 4; i < PG_COUNT; x += tab_w[i] + tab_gap, i++) {
        lv_obj_t *t = mk_box(tabs, x, 4, tab_w[i], 40);
        lv_obj_set_style_radius(t, 9, 0);
        lv_obj_set_style_bg_color(t, lv_color_hex(C_ACCENT), 0);
        lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(t, on_tab, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_center(mk_label(t, &font_kr_18, C_TEXT, TAB_NAMES[i]));
        s_tabs[i] = t;
    }

    /* 오른쪽:  [와이파이 배터리]  [시계 / 날짜] */
    s_clock = mk_label(s_bar, &font_kr_36, C_TEXT, "");
    lv_obj_align(s_clock, LV_ALIGN_TOP_RIGHT, -14, -2);
    s_date = mk_label(s_bar, &font_kr_18, C_DIM, "");
    lv_obj_align(s_date, LV_ALIGN_TOP_RIGHT, -14, 40);
    s_batt = mk_label(s_bar, &font_kr_18, C_TEXT, "");
    lv_obj_set_width(s_batt, 96);
    lv_obj_set_style_text_align(s_batt, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_batt, LV_ALIGN_RIGHT_MID, -136, 0);
    s_status = mk_label(s_bar, &font_kr_18, C_GREEN, LV_SYMBOL_WIFI);
    lv_obj_align(s_status, LV_ALIGN_RIGHT_MID, -238, 0);
    bar_name();
}

void ui_go(int page)
{
    if (page < 0 || page >= PG_COUNT) {
        return;
    }
    for (int i = 0; i < PG_COUNT; i++) {
        if (i == page) {
            lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_bg_opa(s_tabs[i], i == page ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    if (page == PG_HOME) {
        lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
        bar_name();
    }
    s_page = page;
}

int ui_page(void)
{
    return s_page;
}

/* ---------------- 1초마다 ---------------- */

static void clock_texts(char *clock, size_t cs, char *date, size_t ds)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (now < 1735689600) {
        clock[0] = 0;  /* 큰 글꼴에 "-" 가 없습니다 */
        date[0] = 0;
        return;
    }
    int h = tm.tm_hour;
    if (g_cfg.h12[0] == '1') {
        h = h % 12 == 0 ? 12 : h % 12;
    }
    snprintf(clock, cs, "%02d:%02d", h, tm.tm_min);
    snprintf(date, ds, "%d월 %d일 (%s)", tm.tm_mon + 1, tm.tm_mday, WEEKDAYS[tm.tm_wday]);
}

static void screen_power(void)
{
    if (time(NULL) < 1735689600) {
        return;
    }
    int m = now_minute();
    bool off = false;
    if (g_cfg.night_from[0] && g_cfg.night_to[0]) {
        int from = atoi(g_cfg.night_from) * 60, to = atoi(g_cfg.night_to) * 60;
        off = from < to ? (m >= from && m < to) : (m >= from || m < to);
    }
    if (g_cfg.weekend_off[0] == '1') {
        int today = ymd_today();
        int wd = ymd_wday(today);
        bool holiday;
        school_day_info(today, &holiday);
        off |= wd == 0 || wd == 6 || holiday;
    }
    /* 끄는 시간이라도 누르면 1분 동안 켭니다 */
    bool want_off = off && lv_disp_get_inactive_time(NULL) > 60000;
    if (want_off != s_screen_off) {
        s_screen_off = want_off;
        if (want_off) {
            wavesahre_rgb_lcd_bl_off();
            ui_go(PG_HOME);
        } else {
            wavesahre_rgb_lcd_bl_on();
        }
    }
}

/* ---------------- 배터리 ----------------
 *  IO 확장 칩의 ADC(10비트, 3.3V)에 배터리 전압이 1/3 로 나뉘어 들어옵니다.
 *  2.5V 아래면 배터리가 없는 것으로 보고 숨깁니다.
 *
 *  충전 여부를 알려 주는 핀이 없어서 전압으로 짐작합니다:
 *   - 충전기를 꽂으면 전압이 한 번에 오르고, 빼면 한 번에 내려갑니다
 *   - 다 차 가면 충전기가 4.17V 넘게 붙잡고 있습니다
 *   - 켤 때부터 꽂혀 있었으면 5분 사이 전압이 오르는지로
 *   - 컴퓨터 USB(내장 USB 단자)에 붙어 있으면 충전 중 */

#define BATT_JUMP 0.06f   /* 10초 사이 이만큼 오르면 꽂음, 내려가면 뺌 */
#define BATT_HOLD 4.17f   /* 이 위면 충전기가 붙잡고 있는 것 */
#define BATT_HIST 30      /* 10초 × 30 = 5분 전과 비교 */
#define BATT_TREND 0.03f  /* 5분 사이 이만큼 오르면 충전 중, 내려가면 아님 */

static float batt_volts(void)
{
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) {
        sum += IO_EXTENSION_Adc_Input();
    }
    return sum / 8.0f * 3.3f * 3.0f / 1023.0f;
}

static void battery(void)
{
    static int n;
    static float last;
    static bool charging, was;
    if (n++ % 10) {
        return;
    }
    float v = batt_volts();
    if (n == 1) {
        ESP_LOGI("ui", "배터리 %.2fV", v);
    } else if (last > 0) {
        if (v - last >= BATT_JUMP) {
            charging = true;
        } else if (last - v >= BATT_JUMP) {
            charging = false;
        }
    }
    if (v >= BATT_HOLD) {
        charging = true;
    }
    /* 켤 때부터 꽂혀 있었으면 오르는 낌새로: 5분 전보다 오르면 충전 중, 내리면 아님 */
    static float hist[BATT_HIST];
    static int hn;
    float old = hist[hn % BATT_HIST];
    if (hn >= BATT_HIST) {
        if (v - old >= BATT_TREND) {
            charging = true;
        } else if (old - v >= BATT_TREND && v < BATT_HOLD) {
            charging = false;
        }
    }
    hist[hn++ % BATT_HIST] = v;
    bool usb = usb_serial_jtag_is_connected();
    if (charging != was) {
        ESP_LOGI("ui", "배터리 %.2fV → %s", v, charging ? "충전 중" : "충전 끝");
        was = charging;
    }
    last = v;

    char buf[24] = "";
    uint32_t color = C_TEXT;
    if (v >= 2.5f && v < 5.5f) {
        int pct = (int)((v - 3.3f) * 100.0f / (4.15f - 3.3f));
        pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
        const char *sym = pct > 80 ? LV_SYMBOL_BATTERY_FULL : pct > 55 ? LV_SYMBOL_BATTERY_3
                        : pct > 30 ? LV_SYMBOL_BATTERY_2 : pct > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
        bool chg = charging || usb;
        snprintf(buf, sizeof(buf), "%s%s %d%%", chg ? LV_SYMBOL_CHARGE " " : "", sym, pct);
        color = chg ? C_GREEN : pct <= 20 ? C_RED : C_GREEN;
    }
    if (strcmp(buf, g_batt_text)) {
        strlcpy(g_batt_text, buf, sizeof(g_batt_text));
        label_set(s_batt, buf);
        lv_obj_set_style_text_color(s_batt, lv_color_hex(color), 0);
        home_refresh(-1);
    }
}

static void tick(lv_timer_t *t)
{
    battery();
    char clock[16], date[32];
    clock_texts(clock, sizeof(clock), date, sizeof(date));
    label_set(s_clock, clock);
    label_set(s_date, date);

    int today = ymd_today();
    if (today != s_last_day && time(NULL) > 1735689600) {
        s_last_day = today;
        for (int k = D_WEATHER; k <= D_TEACHERS; k++) {
            ui_refresh(k);
        }
    }

    int ret = atoi(g_cfg.home_ret);
    if (s_page != PG_HOME && ret > 0 && lv_disp_get_inactive_time(NULL) > (uint32_t)ret * 60000) {
        ui_go(PG_HOME);
    }
    screen_power();

    home_tick();
    find_tick();
    set_tick();
}

/* ---------------- 바깥에서 부르는 것 ---------------- */

static void status_text(void)
{
    char buf[64];
    if (!s_configured) {
        strlcpy(buf, LV_SYMBOL_WIFI " 와이파이 설정 필요", sizeof(buf));
    } else if (!s_connected) {
        strlcpy(buf, LV_SYMBOL_WIFI " 연결 중…", sizeof(buf));
    } else if (s_updated > 0) {
        struct tm tm;
        localtime_r(&s_updated, &tm);
        snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %02d:%02d 받음", tm.tm_hour, tm.tm_min);
    } else {
        strlcpy(buf, LV_SYMBOL_WIFI " 연결됨", sizeof(buf));
    }
    if (strcmp(buf, g_status_text)) {
        strlcpy(g_status_text, buf, sizeof(g_status_text));
        /* 위쪽 줄에는 와이파이 그림만 (색으로 상태) */
        lv_obj_set_style_text_color(s_status, lv_color_hex(s_connected ? C_GREEN : C_WARM), 0);
        home_refresh(-1);
    }
}

void ui_set_status(bool configured, bool connected, const char *ip)
{
    if (configured == s_configured && connected == s_connected && g_status_text[0]) {
        return;
    }
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        s_configured = configured;
        s_connected = connected;
        status_text();
        esp_lv_adapter_unlock();
    }
}

void ui_set_updated(time_t when)
{
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        s_updated = when;
        status_text();
        esp_lv_adapter_unlock();
    }
}

void ui_wifi_scanned(const wifi_scan_t *list)
{
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        set_wifi_list(list);
        esp_lv_adapter_unlock();
    }
}

void ui_refresh(int kind)
{
    switch (kind) {
    case D_WEATHER:
        home_refresh(kind);
        break;
    case D_MEALS:
        home_refresh(kind);
        meal_refresh();
        break;
    case D_MYCAL:
        home_refresh(kind);
        cal_refresh();
        break;
    case D_SCHOOL:
        home_refresh(kind);
        school_refresh();
        find_refresh();
        cha_refresh();  /* 휴일 · 동아리 같은 창체 일정 */
        cal_refresh();
        break;
    case D_CHA:
        cha_refresh();
        break;
    case D_TEACHERS:
        home_refresh(kind);
        find_refresh();
        break;
    }
}

void ui_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    void (*build[PG_COUNT])(lv_obj_t *) = { home_build, school_build, cha_build, find_build,
                                            cal_build, meal_build, set_build };
    for (int i = 0; i < PG_COUNT; i++) {
        s_pages[i] = mk_box(scr, 0, 0, SCR_W, SCR_H);
        build[i](s_pages[i]);
    }
    bar_build(scr);
    ui_go(PG_HOME);

    s_configured = g_cfg.ssid[0] != 0;
    status_text();
    lv_timer_create(tick, 1000, NULL);
    tick(NULL);
}
