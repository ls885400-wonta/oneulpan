/* 화면 뼈대: 위쪽 줄(메뉴), 화면 바꾸기, 밤에 화면 끄기, 홈으로 돌아가기
 *
 *  홈 화면은 위쪽 줄 없이 꽉 차게, 다른 화면은 위쪽 줄 + 내용입니다.
 *  ┌ 교 황인환 선생님 │ 홈 학교일정 창체계획 찾기 캘린더 급식 설정 │ 와이파이  19:48 ┐
 */
#include "ui.h"

#include <stdlib.h>

#include "esp_lv_adapter.h"
#include "rgb_lcd_port.h"
#include "ui_priv.h"

static lv_obj_t *s_pages[PG_COUNT];
static lv_obj_t *s_bar, *s_tabs[PG_COUNT], *s_badge, *s_name, *s_status, *s_clock, *s_date;
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
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_CARD2), 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_pad_all(b, 6, 0);
    lv_obj_t *l = mk_label(b, font, C_TEXT, text);
    lv_obj_center(l);
    if (cb) {
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    }
    return b;
}

void btn_set_on(lv_obj_t *btn, bool on)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(on ? C_ACCENT : C_CARD2), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(on ? C_BLUE : C_LINE), 0);
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
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x0F1A2E), 0);
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
    lv_obj_set_pos(s_name, 66, 19);
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_name, 190);

    lv_obj_t *tabs = mk_box(s_bar, 262, 8, 7 * 72 + 8, 48);
    lv_obj_set_style_bg_color(tabs, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tabs, 12, 0);
    for (int i = 0; i < PG_COUNT; i++) {
        lv_obj_t *t = mk_box(tabs, 4 + i * 72, 4, 72, 40);
        lv_obj_set_style_radius(t, 10, 0);
        lv_obj_set_style_bg_color(t, lv_color_hex(C_ACCENT), 0);
        lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(t, on_tab, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_center(mk_label(t, &font_kr_18, C_TEXT, TAB_NAMES[i]));
        s_tabs[i] = t;
    }

    s_status = mk_label(s_bar, &font_kr_18, C_GREEN, "");
    lv_obj_set_pos(s_status, 782, 22);
    s_clock = mk_label(s_bar, &font_kr_26, C_TEXT, "");
    lv_obj_align(s_clock, LV_ALIGN_TOP_RIGHT, -14, 6);
    s_date = mk_label(s_bar, &font_kr_18, C_DIM, "");
    lv_obj_align(s_date, LV_ALIGN_TOP_RIGHT, -14, 38);
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
        strlcpy(clock, "--:--", cs);
        date[0] = 0;
        return;
    }
    int h = tm.tm_hour;
    if (g_cfg.h12[0] == '1') {
        h = h % 12 == 0 ? 12 : h % 12;
    }
    snprintf(clock, cs, "%d:%02d", h, tm.tm_min);
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

static void tick(lv_timer_t *t)
{
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
        label_set(s_status, buf);
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
