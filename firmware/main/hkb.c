/* 한글 자판 (두벌식) — 보강 과목·교실처럼 짧은 글자를 넣을 때
 *
 *  LVGL 자판에는 한글이 없어서 btnmatrix 로 만들고, 낱자를 모아 글자를 짓습니다.
 *  쓰는 중인 글자는 글 상자의 맨 끝 한 글자이고, 다른 글쇠를 누르거나 상자를 바꾸면 굳습니다.
 */
#include "hkb.h"

#include <string.h>

#include "ui_priv.h"

/* 초성 19 · 중성 21 · 종성 28 (0 = 받침 없음), 모두 호환 낱자 코드로 */
static const uint16_t CHO[19] = { 0x3131, 0x3132, 0x3134, 0x3137, 0x3138, 0x3139, 0x3141, 0x3142, 0x3143, 0x3145,
                                  0x3146, 0x3147, 0x3148, 0x3149, 0x314A, 0x314B, 0x314C, 0x314D, 0x314E };
static const uint16_t JONG[28] = { 0,      0x3131, 0x3132, 0x3133, 0x3134, 0x3135, 0x3136, 0x3137, 0x3139, 0x313A,
                                   0x313B, 0x313C, 0x313D, 0x313E, 0x313F, 0x3140, 0x3141, 0x3142, 0x3144, 0x3145,
                                   0x3146, 0x3147, 0x3148, 0x314A, 0x314B, 0x314C, 0x314D, 0x314E };
#define JUNG_FIRST 0x314F   /* ㅏ … ㅣ 가 차례로 21개 */

/* 겹모음: {앞, 뒤, 합친 것} (중성 번호) */
static const int8_t VV[][3] = { { 8, 0, 9 }, { 8, 1, 10 }, { 8, 20, 11 }, { 13, 4, 14 },
                                { 13, 5, 15 }, { 13, 20, 16 }, { 18, 20, 19 } };
/* 겹받침: {앞, 뒤, 합친 것} (종성 번호) */
static const int8_t CC[][3] = { { 1, 19, 3 }, { 4, 22, 5 }, { 4, 27, 6 }, { 8, 1, 9 }, { 8, 16, 10 }, { 8, 17, 11 },
                                { 8, 19, 12 }, { 8, 25, 13 }, { 8, 26, 14 }, { 8, 27, 15 }, { 17, 19, 18 } };

static const char *MAP_LOW[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "ㅂ", "ㅈ", "ㄷ", "ㄱ", "ㅅ", "ㅛ", "ㅕ", "ㅑ", "ㅐ", "ㅔ", "\n",
    "ㅁ", "ㄴ", "ㅇ", "ㄹ", "ㅎ", "ㅗ", "ㅓ", "ㅏ", "ㅣ", "\n",
    LV_SYMBOL_UP, "ㅋ", "ㅌ", "ㅊ", "ㅍ", "ㅠ", "ㅜ", "ㅡ", LV_SYMBOL_BACKSPACE, "\n",
    "-", "띄어쓰기", ".", LV_SYMBOL_OK, "",
};
static const char *MAP_UP[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "ㅃ", "ㅉ", "ㄸ", "ㄲ", "ㅆ", "ㅛ", "ㅕ", "ㅑ", "ㅒ", "ㅖ", "\n",
    "ㅁ", "ㄴ", "ㅇ", "ㄹ", "ㅎ", "ㅗ", "ㅓ", "ㅏ", "ㅣ", "\n",
    LV_SYMBOL_UP, "ㅋ", "ㅌ", "ㅊ", "ㅍ", "ㅠ", "ㅜ", "ㅡ", LV_SYMBOL_BACKSPACE, "\n",
    "-", "띄어쓰기", ".", LV_SYMBOL_OK, "",
};
static const lv_btnmatrix_ctrl_t CTRL[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 3,
    2, 6, 2, 3,
};

static lv_obj_t *s_kb, *s_ta;
static lv_event_cb_t s_on_ok;
static bool s_shift;
static int s_cho = -1, s_jung = -1, s_jong = 0; /* 쓰는 중인 글자 (모두 비면 없음) */

static bool composing(void)
{
    return s_cho >= 0 || s_jung >= 0;
}

static void reset(void)
{
    s_cho = s_jung = -1;
    s_jong = 0;
}

static int find16(const uint16_t *t, int n, uint32_t cp)
{
    for (int i = 0; i < n; i++) {
        if (t[i] == cp) {
            return i;
        }
    }
    return -1;
}

static int combine(const int8_t (*t)[3], int n, int a, int b)
{
    for (int i = 0; i < n; i++) {
        if (t[i][0] == a && t[i][1] == b) {
            return t[i][2];
        }
    }
    return -1;
}

static void put_utf8(char *out, uint32_t cp)
{
    if (cp < 0x80) {
        out[0] = cp, out[1] = 0;
    } else if (cp < 0x800) {
        out[0] = 0xC0 | (cp >> 6), out[1] = 0x80 | (cp & 0x3F), out[2] = 0;
    } else {
        out[0] = 0xE0 | (cp >> 12), out[1] = 0x80 | ((cp >> 6) & 0x3F), out[2] = 0x80 | (cp & 0x3F), out[3] = 0;
    }
}

static uint32_t get_utf8(const char *s)
{
    const uint8_t *u = (const uint8_t *)s;
    if (u[0] < 0x80) {
        return u[0];
    }
    if ((u[0] & 0xE0) == 0xC0) {
        return ((u[0] & 0x1F) << 6) | (u[1] & 0x3F);
    }
    return ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
}

/* 쓰는 중인 글자를 상자 맨 끝에 다시 그림 (had = 이미 한 글자 그려 둔 것이 있음) */
static void draw(bool had)
{
    if (had) {
        lv_textarea_del_char(s_ta);
    }
    if (!composing()) {
        return;
    }
    uint32_t cp;
    if (s_cho >= 0 && s_jung >= 0) {
        cp = 0xAC00 + (s_cho * 21 + s_jung) * 28 + s_jong;
    } else if (s_cho >= 0) {
        cp = CHO[s_cho];
    } else {
        cp = JUNG_FIRST + s_jung;
    }
    char u[4];
    put_utf8(u, cp);
    lv_textarea_add_text(s_ta, u);
}

static void consonant(uint32_t cp)
{
    bool had = composing();
    int jong = find16(JONG, 28, cp);
    if (s_cho >= 0 && s_jung >= 0 && s_jong == 0 && jong > 0) {
        s_jong = jong;
    } else if (s_cho >= 0 && s_jung >= 0 && s_jong > 0 && jong > 0 && combine(CC, 11, s_jong, jong) > 0) {
        s_jong = combine(CC, 11, s_jong, jong);
    } else {
        reset();   /* 앞 글자는 굳히고 새 글자 */
        had = false;
        s_cho = find16(CHO, 19, cp);
    }
    draw(had);
}

static void vowel(int v)
{
    bool had = composing();
    if (s_jong > 0) {
        /* 받침을 다음 글자 첫소리로: 겹받침이면 뒤쪽만 */
        int keep = 0, move = s_jong;
        for (int i = 0; i < 11; i++) {
            if (CC[i][2] == s_jong) {
                keep = CC[i][0], move = CC[i][1];
            }
        }
        s_jong = keep;
        draw(had);
        reset();
        s_cho = find16(CHO, 19, JONG[move]);
        s_jung = v;
        draw(false);
        return;
    }
    if (s_jung >= 0 && combine(VV, 7, s_jung, v) >= 0) {
        s_jung = combine(VV, 7, s_jung, v);
    } else if (s_cho >= 0 && s_jung < 0) {
        s_jung = v;
    } else {
        reset();
        had = false;
        s_jung = v;
    }
    draw(had);
}

static void backspace(void)
{
    if (!composing()) {
        lv_textarea_del_char(s_ta);
        return;
    }
    if (s_jong > 0) {
        int back = 0;
        for (int i = 0; i < 11; i++) {
            if (CC[i][2] == s_jong) {
                back = CC[i][0];
            }
        }
        s_jong = back;
    } else if (s_jung >= 0) {
        int back = -1;
        for (int i = 0; i < 7; i++) {
            if (VV[i][2] == s_jung) {
                back = VV[i][0];
            }
        }
        s_jung = back;
        if (s_jung < 0 && s_cho < 0) {
            lv_textarea_del_char(s_ta);
            return;
        }
    } else {
        s_cho = -1;
    }
    draw(true);
}

static void on_key(lv_event_t *e)
{
    if (!s_ta) {
        return;
    }
    uint32_t id = lv_btnmatrix_get_selected_btn(s_kb);
    const char *txt = lv_btnmatrix_get_btn_text(s_kb, id);
    if (!txt) {
        return;
    }
    lv_textarea_set_cursor_pos(s_ta, LV_TEXTAREA_CURSOR_LAST);
    if (!strcmp(txt, LV_SYMBOL_UP)) {
        s_shift = !s_shift;
        lv_btnmatrix_set_map(s_kb, s_shift ? MAP_UP : MAP_LOW);
        lv_btnmatrix_set_ctrl_map(s_kb, CTRL);
        if (s_shift) {
            lv_btnmatrix_set_btn_ctrl(s_kb, 29, LV_BTNMATRIX_CTRL_CHECKED);
        }
        return;
    }
    if (!strcmp(txt, LV_SYMBOL_BACKSPACE)) {
        backspace();
        return;
    }
    if (!strcmp(txt, LV_SYMBOL_OK)) {
        reset();
        if (s_on_ok) {
            s_on_ok(e);
        }
        return;
    }
    uint32_t cp = get_utf8(txt);
    if (cp >= 0x3131 && cp < JUNG_FIRST) {
        consonant(cp);
    } else if (cp >= JUNG_FIRST && cp <= 0x3163) {
        vowel(cp - JUNG_FIRST);
    } else {
        reset();
        lv_textarea_add_text(s_ta, !strcmp(txt, "띄어쓰기") ? " " : txt);
    }
    if (s_shift) {   /* 쌍자음·ㅒㅖ 는 한 번만 */
        s_shift = false;
        lv_btnmatrix_set_map(s_kb, MAP_LOW);
        lv_btnmatrix_set_ctrl_map(s_kb, CTRL);
    }
}

static void on_del(lv_event_t *e)
{
    hkb_forget();
}

lv_obj_t *hkb_create(lv_obj_t *parent, lv_event_cb_t on_ok)
{
    s_kb = lv_btnmatrix_create(parent);
    s_on_ok = on_ok;
    s_ta = NULL;
    s_shift = false;
    reset();
    lv_btnmatrix_set_map(s_kb, MAP_LOW);
    lv_btnmatrix_set_ctrl_map(s_kb, CTRL);
    lv_obj_set_style_text_font(s_kb, &font_kr_22, 0);
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_kb, 0, 0);
    lv_obj_set_style_radius(s_kb, 0, 0);
    lv_obj_set_style_pad_all(s_kb, 6, 0);
    lv_obj_set_style_pad_gap(s_kb, 6, 0);
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(C_BTN), LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_kb, lv_color_hex(C_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_radius(s_kb, 8, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(C_ACCENT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_kb, on_key, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_kb, on_del, LV_EVENT_DELETE, NULL);
    return s_kb;
}

void hkb_set_textarea(lv_obj_t *ta)
{
    reset();   /* 다른 상자로 가면 쓰던 글자는 굳음 */
    s_ta = ta;
    if (ta) {
        lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
    }
}

void hkb_commit(void)
{
    reset();
}

void hkb_forget(void)
{
    s_kb = s_ta = NULL;
    reset();
}
