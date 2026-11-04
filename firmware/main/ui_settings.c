/* 설정 화면
 *
 *  ┌ 와이파이        [찾기] ┐ ┌ 화면 끄기 ─────────────────────────────┐
 *  │ 'Multi_2G' 저장됨 · … │ │ 밤 시간   [22:00] ~ [07:00]             │
 *  │ (찾은 와이파이 목록)   │ │ 주말·휴일 [끄기] [켜 두기]              │
 *  │                        │ ├ 홈 화면 ───────────────────────────────┤
 *  │                        │ │ 시간 표시 · 홈 복귀 · 보이기            │
 *  │                        │ ├ 자료 받기 ─────────────────────────────┤
 *  └ [저장한 와이파이 지우기]┘ └ [지금 받기]  08:16 받음                 ┘
 */
#include <stdlib.h>

#include "esp_system.h"
#include "net.h"
#include "ui.h"
#include "ui_priv.h"

#define LEFT_W 400
#define LBL_W 116   /* 오른쪽 칸의 이름 너비 */

static lv_obj_t *s_wifi_info, *s_wifi_list, *s_scan_btn, *s_status;
static lv_obj_t *s_from, *s_to;
static lv_obj_t *s_weekend[2], *s_h12[2], *s_ret[6], *s_show[5];
static lv_obj_t *s_pop, *s_pop_ta, *s_kb;
static char s_pop_ssid[33];

static const int RET_MIN[6] = { 1, 3, 5, 10, 20, 0 };
static const char *RET_NAMES[6] = { "1분", "3분", "5분", "10분", "20분", "안 함" };
static const char SHOW_KEYS[5] = { 'w', 'd', 'm', 't', 's' };
static const char *SHOW_NAMES[5] = { "날씨", "미세먼지", "급식", "시간표", "오늘" };

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
    char buf[320];
    int n = net_saved_count();
    const char *fail = net_fail_text();
    if (n == 0) {
        strlcpy(buf, "저장된 와이파이가 없습니다. [찾기] 를 눌러 고르세요.", sizeof(buf));
    } else if (net_connected()) {
        snprintf(buf, sizeof(buf), "'%s' 에 연결됨 %s · 저장 %d개", net_ssid(), net_ip(), n);
    } else if (net_trying()[0]) {
        snprintf(buf, sizeof(buf), "'%s' 에 연결하는 중…", net_trying());
    } else {
        snprintf(buf, sizeof(buf), "저장 %d개 · 연결 안 됨 (다시 찾는 중)", n);
    }
    if (fail[0]) {
        size_t o = strlen(buf);
        snprintf(buf + o, sizeof(buf) - o, "\n#FF7A90 %s#", fail);
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
        lv_obj_del_async(s_pop); /* 팝업 안 버튼·자판의 이벤트 안에서 불리므로 나중에 지움 */
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
    net_join(g_cfg.ssid, g_cfg.pass); /* 다시 시작하지 않고 바로 붙어 봄 */
    close_pop();
    wifi_info();
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
    lv_obj_t *b = mk_btn(box, "연결", &font_kr_22, on_pop_connect, NULL);
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

/* 찾은 목록을 따로 들고 있다가 번호로 꺼냅니다 (버튼 글자는 길면 잘려 보일 수 있어서) */
static wifi_scan_t s_aps;

static void on_ap(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < s_aps.count) {
        open_pop(s_aps.ap[i].ssid, s_aps.ap[i].open);
    }
}

void set_wifi_list(const void *arg)
{
    const wifi_scan_t *list = arg;
    s_aps.count = 0;
    if (list) {
        s_aps = *list;
    }
    lv_obj_clear_state(s_scan_btn, LV_STATE_DISABLED);
    lv_obj_clean(s_wifi_list);
    if (!list || list->count == 0) {
        mk_label(s_wifi_list, &font_kr_18, C_DIM, "찾은 와이파이가 없습니다. 다시 찾아 보세요.");
        return;
    }
    for (int i = 0; i < list->count; i++) {
        lv_obj_t *b = mk_btn(s_wifi_list, list->ap[i].ssid, &font_kr_18, on_ap, (void *)(intptr_t)i);
        lv_obj_set_size(b, lv_pct(100), 44);
        lv_obj_align(lv_obj_get_child(b, 0), LV_ALIGN_LEFT_MID, 8, 0);
        char sig[24];
        int bars = list->ap[i].rssi > -60 ? 3 : list->ap[i].rssi > -72 ? 2 : 1;
        snprintf(sig, sizeof(sig), "%s%s", list->ap[i].open ? "" : LV_SYMBOL_EYE_CLOSE " ",
                 bars == 3 ? "강함" : bars == 2 ? "보통" : "약함");
        lv_obj_t *s = mk_label(b, &font_kr_18, C_DIM, sig);
        lv_obj_align(s, LV_ALIGN_RIGHT_MID, -8, 0);
        btn_set_on(b, !strcmp(list->ap[i].ssid, net_ssid()));
    }
}

/* 저장된 와이파이를 전부 지움 (하나만 지우면 예전 와이파이로 다시 붙어 버려서) */
static void on_forget(lv_event_t *e)
{
    net_forget_all();
    g_cfg.ssid[0] = g_cfg.pass[0] = 0;
    config_save();
    wifi_info();
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
    lv_obj_t *l = mk_label(parent, &font_kr_18, C_TEXT, text);
    lv_obj_set_pos(l, 4, y + 11);
    return l;
}

static lv_obj_t *opt(lv_obj_t *parent, int x, int y, int w, const char *text, lv_event_cb_t cb, int i)
{
    lv_obj_t *b = mk_btn(parent, text, &font_kr_18, cb, (void *)(intptr_t)i);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, 42);
    return b;
}

/* 묶음 제목 + 위쪽 가는 줄 */
static void section(lv_obj_t *parent, int y, const char *text, int w)
{
    if (y > 0) {
        lv_obj_t *line = mk_box(parent, 0, y - 12, w, 1);
        lv_obj_set_style_bg_color(line, lv_color_hex(C_LINE), 0);
        lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    }
    lv_obj_t *h = mk_label(parent, &font_kr_22, C_SKY, text);
    lv_obj_set_pos(h, 0, y);
}

static lv_obj_t *dropdown(lv_obj_t *parent, int x, int y, const char *opts)
{
    lv_obj_t *d = lv_dropdown_create(parent);
    lv_dropdown_set_options(d, opts);
    lv_obj_set_pos(d, x, y);
    lv_obj_set_size(d, 150, 42);
    lv_obj_set_style_radius(d, 10, 0);
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
    mk_label(c, &font_kr_22, C_SKY, "와이파이");
    s_scan_btn = mk_btn(c, "찾기", &font_kr_18, on_scan, NULL);
    lv_obj_set_size(s_scan_btn, 100, 40);
    btn_set_on(s_scan_btn, true);
    lv_obj_align(s_scan_btn, LV_ALIGN_TOP_RIGHT, 0, -6);
    s_wifi_info = mk_wrap(c, &font_kr_18, C_DIM, "");
    lv_label_set_recolor(s_wifi_info, true);
    lv_obj_set_pos(s_wifi_info, 0, 40);
    lv_obj_set_width(s_wifi_info, LEFT_W - 28);
    s_wifi_list = mk_list(c, 0, 112, LEFT_W - 28, BODY_H - 28 - 112 - 52);
    lv_obj_t *b = mk_btn(c, "저장한 와이파이 모두 지우기", &font_kr_18, on_forget, NULL);
    lv_obj_set_size(b, 262, 40);
    lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *v = mk_label(c, &font_kr_18, C_DIM, "오늘판 " APP_VERSION);
    lv_obj_align(v, LV_ALIGN_BOTTOM_RIGHT, 0, -8);

    /* 화면 끄기 · 홈 화면 · 자료 */
    int x = PAD + LEFT_W + PAD;
    int cw = SCR_W - x - PAD;
    int iw = cw - 28;           /* 안쪽 너비 */
    c = mk_card(page, x, BODY_Y, cw, BODY_H);

    static char from_opts[200], to_opts[160];
    size_t o = strlcpy(from_opts, "안 끔", sizeof(from_opts));
    size_t p = 0;
    for (int h = 0; h < 24; h++) {
        o += snprintf(from_opts + o, sizeof(from_opts) - o, "\n%02d:00", h);
        p += snprintf(to_opts + p, sizeof(to_opts) - p, "%s%02d:00", h ? "\n" : "", h);
    }
    section(c, 0, "화면 끄기", iw);
    row_label(c, 38, "밤 시간");
    s_from = dropdown(c, LBL_W, 38, from_opts);
    lv_obj_t *tilde = mk_label(c, &font_kr_22, C_DIM, "~");
    lv_obj_set_pos(tilde, LBL_W + 160, 46);
    s_to = dropdown(c, LBL_W + 184, 38, to_opts);
    lv_dropdown_set_selected(s_from, g_cfg.night_from[0] ? atoi(g_cfg.night_from) + 1 : 0);
    lv_dropdown_set_selected(s_to, g_cfg.night_to[0] ? atoi(g_cfg.night_to) : 7);
    row_label(c, 88, "주말·휴일");
    s_weekend[0] = opt(c, LBL_W, 88, 150, "끄기", on_weekend, 1);
    s_weekend[1] = opt(c, LBL_W + 160, 88, 150, "켜 두기", on_weekend, 0);

    section(c, 158, "홈 화면", iw);
    row_label(c, 196, "시간 표시");
    s_h12[0] = opt(c, LBL_W, 196, 150, "24시간", on_h12, 0);
    s_h12[1] = opt(c, LBL_W + 160, 196, 150, "12시간", on_h12, 1);
    row_label(c, 246, "홈 복귀");
    int bw = (iw - LBL_W - 5 * 6) / 6;
    for (int i = 0; i < 6; i++) {
        s_ret[i] = opt(c, LBL_W + i * (bw + 6), 246, bw, RET_NAMES[i], on_ret, i);
    }
    row_label(c, 296, "보이기");
    bw = (iw - LBL_W - 4 * 6) / 5;
    for (int i = 0; i < 5; i++) {
        s_show[i] = opt(c, LBL_W + i * (bw + 6), 296, bw, SHOW_NAMES[i], on_show, i);
    }

    section(c, 366, "자료 받기", iw);
    b = mk_btn(c, "지금 받기", &font_kr_18, on_refresh, NULL);
    lv_obj_set_pos(b, LBL_W, 404);
    lv_obj_set_size(b, 150, 42);
    btn_set_on(b, true);
    s_status = mk_label(c, &font_kr_18, C_GREEN, "");
    lv_obj_set_pos(s_status, LBL_W + 166, 415);
    lv_obj_t *note = mk_label(c, &font_kr_18, C_DIM, "날씨 30분 · 급식 3시간 · 일정 1시간마다");
    lv_obj_set_pos(note, 4, 460);
    mark();
    wifi_info();
}

void set_tick(void)
{
    static int n;
    wifi_info(); /* 연결하는 중 → 연결됨 / 실패 를 바로 보이게 매초 */
    if (++n % 5) {
        return;
    }
    if (strcmp(lv_label_get_text(s_status), "새로 받는 중…") || n % 20 == 0) {
        label_set(s_status, g_status_text);
    }
}
