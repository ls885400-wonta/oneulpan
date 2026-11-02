/* 설정 화면
 *
 *  ┌ 와이파이            [찾기] ┐ ┌ 화면 끄기                 [지금 받기] ┐
 *  │ 'Multi_2G' 저장됨 · 연결됨 │ │ 밤 시간  [22:00] ~ [07:00]            │
 *  │ (찾은 와이파이 목록)       │ │ 주말·휴일 [끄기] [켜 두기]            │
 *  │                            │ │ 홈 화면                               │
 *  │                            │ │ 시간 표시 · 홈 복귀 · 보이기           │
 *  └ [저장한 와이파이 지우기]   ┘ └───────────────────────────────────────┘
 */
#include <stdlib.h>

#include "esp_system.h"
#include "net.h"
#include "ui.h"
#include "ui_priv.h"

#define LEFT_W 470

static lv_obj_t *s_wifi_info, *s_wifi_list, *s_scan_btn, *s_status;
static lv_obj_t *s_from, *s_to;
static lv_obj_t *s_weekend[2], *s_h12[2], *s_ret[6], *s_show[5];
static lv_obj_t *s_pop, *s_pop_ta, *s_kb;
static char s_pop_ssid[33];

static const int RET_MIN[6] = { 1, 3, 5, 10, 20, 0 };
static const char *RET_NAMES[6] = { "1분", "3분", "5분", "10분", "20분", "안 함" };
static const char SHOW_KEYS[5] = { 'w', 'd', 'm', 't', 's' };
static const char *SHOW_NAMES[5] = { "날씨", "미세먼지", "급식", "시간표", "오늘 일정" };

static void save_and_apply(void)
{
    config_save();
    for (int k = D_WEATHER; k <= D_TEACHERS; k++) {
        ui_refresh(k);
    }
}

/* ---------------- 와이파이 ---------------- */

static void wifi_info(void)
{
    char buf[160];
    if (g_cfg.ssid[0]) {
        snprintf(buf, sizeof(buf), "'%s' 저장됨 · %s%s", g_cfg.ssid,
                 net_connected() ? "연결됨 " : "연결 안 됨", net_connected() ? net_ip() : "");
    } else {
        strlcpy(buf, "저장된 와이파이가 없습니다. [찾기] 를 눌러 고르세요.", sizeof(buf));
    }
    label_set(s_wifi_info, buf);
}

static void on_scan(lv_event_t *e)
{
    lv_obj_clean(s_wifi_list);
    mk_label(s_wifi_list, &font_kr_18, C_DIM, "찾는 중…");
    lv_obj_add_state(s_scan_btn, LV_STATE_DISABLED);
    net_scan();
}

static void close_pop(void)
{
    if (s_pop) {
        lv_obj_del(s_pop);
        s_pop = NULL;
        s_kb = NULL;
    }
}

static void on_pop_cancel(lv_event_t *e)
{
    close_pop();
}

static void on_pop_connect(lv_event_t *e)
{
    strlcpy(g_cfg.ssid, s_pop_ssid, sizeof(g_cfg.ssid));
    strlcpy(g_cfg.pass, lv_textarea_get_text(s_pop_ta), sizeof(g_cfg.pass));
    config_save();
    esp_restart(); /* 새 와이파이로 다시 시작 */
}

static void on_kb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        on_pop_connect(e);
    } else if (code == LV_EVENT_CANCEL) {
        close_pop();
    }
}

static void open_pop(const char *ssid, bool open)
{
    close_pop();
    strlcpy(s_pop_ssid, ssid, sizeof(s_pop_ssid));
    s_pop = mk_box(lv_layer_top(), 0, 0, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(s_pop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_pop, LV_OPA_70, 0);
    lv_obj_add_flag(s_pop, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *box = mk_card(s_pop, 162, 20, 700, 230);
    char buf[80];
    snprintf(buf, sizeof(buf), "'%s' 에 연결", ssid);
    mk_label(box, &font_kr_22, C_TEXT, buf);
    s_pop_ta = lv_textarea_create(box);
    lv_textarea_set_one_line(s_pop_ta, true);
    lv_textarea_set_placeholder_text(s_pop_ta, open ? "(비밀번호 없음)" : "password");
    lv_obj_set_style_text_font(s_pop_ta, &lv_font_montserrat_24, 0);
    lv_obj_set_size(s_pop_ta, 660, 54);
    lv_obj_set_pos(s_pop_ta, 0, 44);
    lv_obj_t *b = mk_btn(box, "연결하고 다시 시작", &font_kr_22, on_pop_connect, NULL);
    btn_set_on(b, true);
    lv_obj_set_size(b, 260, 50);
    lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    b = mk_btn(box, "취소", &font_kr_22, on_pop_cancel, NULL);
    lv_obj_set_size(b, 120, 50);
    lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, -272, 0);

    s_kb = lv_keyboard_create(s_pop);
    lv_obj_set_size(s_kb, SCR_W, 330);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(s_kb, &lv_font_montserrat_24, 0);
    lv_keyboard_set_textarea(s_kb, s_pop_ta);
    lv_obj_add_event_cb(s_kb, on_kb, LV_EVENT_ALL, NULL);
}

static void on_ap(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *ssid = lv_label_get_text(lv_obj_get_child(btn, 0));
    bool open = (bool)(intptr_t)lv_event_get_user_data(e);
    open_pop(ssid, open);
}

void set_wifi_list(const void *arg)
{
    const wifi_scan_t *list = arg;
    lv_obj_clear_state(s_scan_btn, LV_STATE_DISABLED);
    lv_obj_clean(s_wifi_list);
    if (!list || list->count == 0) {
        mk_label(s_wifi_list, &font_kr_18, C_DIM, "찾은 와이파이가 없습니다. 다시 찾아 보세요.");
        return;
    }
    for (int i = 0; i < list->count; i++) {
        lv_obj_t *b = mk_btn(s_wifi_list, list->ap[i].ssid, &font_kr_18, on_ap, (void *)(intptr_t)list->ap[i].open);
        lv_obj_set_size(b, lv_pct(100), 44);
        lv_obj_align(lv_obj_get_child(b, 0), LV_ALIGN_LEFT_MID, 8, 0);
        char sig[24];
        int bars = list->ap[i].rssi > -60 ? 3 : list->ap[i].rssi > -72 ? 2 : 1;
        snprintf(sig, sizeof(sig), "%s%s", list->ap[i].open ? "" : LV_SYMBOL_EYE_CLOSE " ",
                 bars == 3 ? "강함" : bars == 2 ? "보통" : "약함");
        lv_obj_t *s = mk_label(b, &font_kr_18, C_DIM, sig);
        lv_obj_align(s, LV_ALIGN_RIGHT_MID, -8, 0);
        btn_set_on(b, !strcmp(list->ap[i].ssid, g_cfg.ssid));
    }
}

static void on_forget(lv_event_t *e)
{
    g_cfg.ssid[0] = g_cfg.pass[0] = 0;
    config_save();
    esp_restart();
}

/* ---------------- 오른쪽 ---------------- */

static void on_refresh(lv_event_t *e)
{
    feeds_refresh_now();
    label_set(s_status, "새로 받는 중…");
}

static void mark(void)
{
    btn_set_on(s_weekend[0], g_cfg.weekend_off[0] == '1');
    btn_set_on(s_weekend[1], g_cfg.weekend_off[0] != '1');
    btn_set_on(s_h12[0], g_cfg.h12[0] != '1');
    btn_set_on(s_h12[1], g_cfg.h12[0] == '1');
    int ret = atoi(g_cfg.home_ret);
    for (int i = 0; i < 6; i++) {
        btn_set_on(s_ret[i], RET_MIN[i] == ret);
    }
    for (int i = 0; i < 5; i++) {
        btn_set_on(s_show[i], config_show(SHOW_KEYS[i]));
    }
}

static void on_weekend(lv_event_t *e)
{
    strlcpy(g_cfg.weekend_off, lv_event_get_user_data(e) ? "1" : "", sizeof(g_cfg.weekend_off));
    mark();
    config_save();
}

static void on_h12(lv_event_t *e)
{
    strlcpy(g_cfg.h12, lv_event_get_user_data(e) ? "1" : "", sizeof(g_cfg.h12));
    mark();
    save_and_apply();
}

static void on_ret(lv_event_t *e)
{
    snprintf(g_cfg.home_ret, sizeof(g_cfg.home_ret), "%d", RET_MIN[(int)(intptr_t)lv_event_get_user_data(e)]);
    mark();
    config_save();
}

static void on_show(lv_event_t *e)
{
    char key = SHOW_KEYS[(int)(intptr_t)lv_event_get_user_data(e)];
    char out[16] = "";
    size_t o = 0;
    bool had = config_show(key);
    for (const char *p = g_cfg.show; *p && o < sizeof(out) - 2; p++) {
        if (*p != key) {
            out[o++] = *p;
        }
    }
    if (!had) {
        out[o++] = key;
    }
    out[o] = 0;
    strlcpy(g_cfg.show, out, sizeof(g_cfg.show));
    mark();
    save_and_apply();
}

static void on_night(lv_event_t *e)
{
    int f = lv_dropdown_get_selected(s_from); /* 0 = 안 끔 */
    int t = lv_dropdown_get_selected(s_to);
    if (f == 0) {
        g_cfg.night_from[0] = g_cfg.night_to[0] = 0;
    } else {
        snprintf(g_cfg.night_from, sizeof(g_cfg.night_from), "%d", (int)((f - 1) % 24));
        snprintf(g_cfg.night_to, sizeof(g_cfg.night_to), "%d", (int)(t % 24));
    }
    config_save();
}

static lv_obj_t *row_label(lv_obj_t *parent, int y, const char *text)
{
    lv_obj_t *l = mk_label(parent, &font_kr_18, C_DIM, text);
    lv_obj_set_pos(l, 0, y + 10);
    return l;
}

static lv_obj_t *opt(lv_obj_t *parent, int x, int y, const char *text, lv_event_cb_t cb, int i)
{
    lv_obj_t *b = mk_btn(parent, text, &font_kr_18, cb, (void *)(intptr_t)i);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, 120, 40);
    return b;
}

static lv_obj_t *dropdown(lv_obj_t *parent, int x, int y, const char *opts)
{
    lv_obj_t *d = lv_dropdown_create(parent);
    lv_dropdown_set_options(d, opts);
    lv_obj_set_pos(d, x, y);
    lv_obj_set_size(d, 150, 42);
    lv_obj_set_style_text_font(d, &font_kr_18, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(C_CARD2), 0);
    lv_obj_set_style_text_color(d, lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_border_color(d, lv_color_hex(C_LINE), 0);
    lv_obj_t *list = lv_dropdown_get_list(d);
    lv_obj_set_style_text_font(list, &font_kr_18, 0);
    lv_obj_set_style_max_height(list, 300, 0);
    lv_obj_add_event_cb(d, on_night, LV_EVENT_VALUE_CHANGED, NULL);
    return d;
}

void set_build(lv_obj_t *page)
{
    /* 와이파이 */
    lv_obj_t *c = mk_card(page, PAD, BODY_Y, LEFT_W, BODY_H);
    mk_label(c, &font_kr_22, C_TEXT, "와이파이");
    s_scan_btn = mk_btn(c, "찾기", &font_kr_18, on_scan, NULL);
    lv_obj_set_size(s_scan_btn, 100, 40);
    btn_set_on(s_scan_btn, true);
    lv_obj_align(s_scan_btn, LV_ALIGN_TOP_RIGHT, 0, -6);
    s_wifi_info = mk_wrap(c, &font_kr_18, C_DIM, "");
    lv_obj_set_pos(s_wifi_info, 0, 40);
    lv_obj_set_width(s_wifi_info, LEFT_W - 28);
    s_wifi_list = mk_list(c, 0, 92, LEFT_W - 28, BODY_H - 28 - 92 - 52);
    lv_obj_t *b = mk_btn(c, "저장한 와이파이 지우기", &font_kr_18, on_forget, NULL);
    lv_obj_set_size(b, 230, 40);
    lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *v = mk_label(c, &font_kr_18, C_DIM, "오늘판 " APP_VERSION);
    lv_obj_align(v, LV_ALIGN_BOTTOM_RIGHT, 0, -8);

    /* 화면 끄기 · 홈 화면 */
    int x = PAD + LEFT_W + PAD;
    c = mk_card(page, x, BODY_Y, SCR_W - x - PAD, BODY_H);
    mk_label(c, &font_kr_22, C_TEXT, "화면 끄기");
    b = mk_btn(c, "지금 받기", &font_kr_18, on_refresh, NULL);
    lv_obj_set_size(b, 120, 40);
    btn_set_on(b, true);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, 0, -6);

    static char from_opts[200], to_opts[160];
    size_t o = strlcpy(from_opts, "안 끔", sizeof(from_opts));
    size_t p = 0;
    for (int h = 0; h < 24; h++) {
        o += snprintf(from_opts + o, sizeof(from_opts) - o, "\n%02d:00", h);
        p += snprintf(to_opts + p, sizeof(to_opts) - p, "%s%02d:00", h ? "\n" : "", h);
    }
    row_label(c, 40, "밤 시간");
    s_from = dropdown(c, 110, 40, from_opts);
    lv_obj_t *tilde = mk_label(c, &font_kr_18, C_DIM, "~");
    lv_obj_set_pos(tilde, 270, 50);
    s_to = dropdown(c, 290, 40, to_opts);
    lv_dropdown_set_selected(s_from, g_cfg.night_from[0] ? atoi(g_cfg.night_from) + 1 : 0);
    lv_dropdown_set_selected(s_to, g_cfg.night_to[0] ? atoi(g_cfg.night_to) : 7);

    row_label(c, 92, "주말·휴일");
    s_weekend[0] = opt(c, 110, 92, "끄기", on_weekend, 1);
    s_weekend[1] = opt(c, 240, 92, "켜 두기", on_weekend, 0);
    s_status = mk_label(c, &font_kr_18, C_GREEN, "");
    lv_obj_set_pos(s_status, 0, 142);

    lv_obj_t *h = mk_label(c, &font_kr_22, C_TEXT, "홈 화면");
    lv_obj_set_pos(h, 0, 178);
    row_label(c, 214, "시간 표시");
    s_h12[0] = opt(c, 110, 214, "24시간", on_h12, 0);
    s_h12[1] = opt(c, 240, 214, "12시간", on_h12, 1);
    row_label(c, 262, "홈 복귀");
    for (int i = 0; i < 6; i++) {
        s_ret[i] = opt(c, 110 + (i % 3) * 130, 262 + (i / 3) * 46, RET_NAMES[i], on_ret, i);
    }
    row_label(c, 360, "보이기");
    for (int i = 0; i < 5; i++) {
        s_show[i] = opt(c, 110 + (i % 3) * 130, 360 + (i / 3) * 46, SHOW_NAMES[i], on_show, i);
    }
    mark();
    wifi_info();
}

void set_tick(void)
{
    static int n;
    if (++n % 5) {
        return;
    }
    wifi_info();
    if (strcmp(lv_label_get_text(s_status), "새로 받는 중…") || n % 20 == 0) {
        label_set(s_status, g_status_text);
    }
}
