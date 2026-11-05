/* 일주일 시간표 표, 그리고 '시간표 찾기' 화면 (교사 고르기)
 *
 *  표에서 수업 칸을 끌어 다른 칸에 놓으면 그 주 수업 교체 (수업이 있는 칸이면 맞바꿈).
 *  빈 칸을 누르면 보강 넣기, 바뀐 칸을 누르면 원래대로. 바뀐 것은 홈 시간표에도 들어갑니다.
 *
 *  ┌ 선생님을 고르세요 ┐ ┌ 황인환 선생님                 [이 보드 주인] ┐
 *  │ 김민식 박민희 …   │ │ 지금 · 3교시 공일 V (307)                    │
 *  │ …                 │ │       월 11/2  화 11/3 …                     │
 *  └───────────────────┘ └ 1교시 [과목/교실] …                          ┘
 */
#include <stdlib.h>

#include "ui_priv.h"

#define LESSON_MIN 50

/* ---------------- 표 ---------------- */

int tt_current_period(int *next)
{
    const teachers_t *t = g_data.teachers;
    int m = now_minute();
    int cur = -1, nx = -1;
    for (int p = 0; t && p < TT_PERIODS; p++) {
        if (m >= t->ptime[p] && m < t->ptime[p] + LESSON_MIN) {
            cur = p;
        }
        if (nx < 0 && m < t->ptime[p]) {
            nx = p;
        }
    }
    if (next) {
        *next = nx;
    }
    return cur;
}

void tt_grid_create(tt_grid_t *g, lv_obj_t *parent, int x, int y, int w, int h)
{
    memset(g, 0, sizeof(*g));
    g->box = mk_box(parent, x, y, w, h);
    const int pw = 62, hh = 28, gap = 4;
    int cw = (w - pw - gap * TT_DAYS) / TT_DAYS;
    int ch = (h - hh - gap * TT_PERIODS) / TT_PERIODS;
    for (int d = 0; d < TT_DAYS; d++) {
        g->head[d] = mk_label(g->box, &font_kr_18, C_DIM, "");
        lv_obj_set_width(g->head[d], cw);
        lv_obj_set_style_text_align(g->head[d], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(g->head[d], pw + gap + d * (cw + gap), 0);
        lv_obj_set_style_radius(g->head[d], 6, 0);
        lv_obj_set_style_pad_ver(g->head[d], 2, 0);
    }
    for (int p = 0; p < TT_PERIODS; p++) {
        int cy = hh + p * (ch + gap);
        g->plabel[p] = mk_label(g->box, &font_kr_18, C_DIM, "");
        lv_obj_set_width(g->plabel[p], pw);
        lv_obj_set_style_text_align(g->plabel[p], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(g->plabel[p], -2, 0);
        lv_obj_set_style_radius(g->plabel[p], 8, 0);
        lv_obj_set_style_pad_ver(g->plabel[p], 2, 0);
        lv_obj_align_to(g->plabel[p], g->box, LV_ALIGN_TOP_LEFT, 0, cy + ch / 2 - 21);
        for (int d = 0; d < TT_DAYS; d++) {
            lv_obj_t *c = mk_box(g->box, pw + gap + d * (cw + gap), cy, cw, ch);
            lv_obj_set_style_radius(c, 8, 0);
            lv_obj_set_style_border_width(c, 1, 0);
            lv_obj_set_style_border_color(c, lv_color_hex(C_LINE), 0);
            lv_obj_set_style_bg_color(c, lv_color_hex(C_CARD2), 0);
            g->cell[d][p] = c;
            g->text[d][p] = mk_label(c, &font_kr_18, C_TEXT, "");
            lv_obj_set_width(g->text[d][p], cw - 4);
            lv_obj_set_style_text_align(g->text[d][p], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_line_space(g->text[d][p], -2, 0);
            lv_label_set_long_mode(g->text[d][p], LV_LABEL_LONG_CLIP);
            lv_obj_set_height(g->text[d][p], ch - 6);
            lv_obj_center(g->text[d][p]);
        }
    }
    g->msg = mk_wrap(g->box, &font_kr_22, C_DIM, "");
    lv_obj_set_width(g->msg, w - 40);
    lv_obj_align(g->msg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(g->msg, LV_OBJ_FLAG_HIDDEN);
}

void tt_grid_fill(tt_grid_t *g, int teacher)
{
    const teachers_t *ts = g_data.teachers;
    const teacher_t *t = ts && teacher >= 0 && teacher < ts->count ? &ts->t[teacher] : NULL;
    const char *msg = !ts ? "설치 페이지에서 교사 시간표 한글 파일을 넣어 주세요."
                    : !t ? "선생님을 고르세요." : NULL;
    if (msg) {
        label_set(g->msg, msg);
        lv_obj_clear_flag(g->msg, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g->msg, LV_OBJ_FLAG_HIDDEN);
    }

    int monday = ymd_week_monday();
    int today = ymd_today();
    int next;
    int cur = tt_current_period(&next);
    char buf[64];

    for (int p = 0; p < TT_PERIODS; p++) {
        int st = ts ? ts->ptime[p] : 0;
        snprintf(buf, sizeof(buf), "%d교시\n%02d:%02d", p + 1, st / 60, st % 60);
        label_set(g->plabel[p], ts ? buf : "");
        bool now_p = p == cur && is_school_day(today);
        lv_obj_set_style_bg_opa(g->plabel[p], now_p ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(g->plabel[p], lv_color_hex(C_RED), 0);
        lv_obj_set_style_text_color(g->plabel[p], lv_color_hex(now_p ? 0xFFFFFF : C_DIM), 0);
    }
    for (int d = 0; d < TT_DAYS; d++) {
        int ymd = ymd_add(monday, d);
        bool holiday;
        const acad_t *a = school_day_info(ymd, &holiday);
        bool is_today = ymd == today;
        snprintf(buf, sizeof(buf), "%s %d/%d", WEEKDAYS[d + 1], ymd / 100 % 100, ymd % 100);
        label_set(g->head[d], buf);
        lv_obj_set_style_text_color(g->head[d], lv_color_hex(is_today ? 0xFFFFFF : holiday ? C_PINK : C_DIM), 0);
        lv_obj_set_style_bg_opa(g->head[d], is_today ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(g->head[d], lv_color_hex(C_RED), 0);

        for (int p = 0; p < TT_PERIODS; p++) {
            lv_obj_t *c = g->cell[d][p];
            const char *subj, *room;
            int kind;
            bool has = t && tt_lesson(teacher, ymd, p, &subj, &room, &kind);
            uint32_t tc = holiday ? C_RED : C_TEXT;
            if (has) {
                snprintf(buf, sizeof(buf), "%s%s\n%s", kind == TT_EXTRA ? "보강 " : "", subj, room);
            } else if (t && kind == TT_MOVED_OUT) {
                snprintf(buf, sizeof(buf), "%s\n(옮김)", subj);
                tc = C_DIM;
            } else if (holiday && p == 0 && a) {
                strlcpy(buf, a->name, sizeof(buf));  /* 휴일 이름은 첫 칸에 */
            } else {
                buf[0] = 0;
            }
            label_set(g->text[d][p], msg ? "" : buf);
            lv_obj_set_style_text_color(g->text[d][p], lv_color_hex(tc), 0);

            bool now = is_today && p == cur;
            /* 바뀐 칸 테두리: 보강 = 초록, 교체·옮겨 간 빈 칸 = 주황 */
            uint32_t bc = now ? C_GREEN : kind == TT_EXTRA ? C_GREEN : kind != TT_NORMAL ? C_WARM
                        : is_today && has ? 0x3E8F77 : C_LINE;
            lv_obj_set_style_bg_opa(c, msg ? LV_OPA_TRANSP : has ? LV_OPA_COVER : LV_OPA_30, 0);
            lv_obj_set_style_bg_color(c, lv_color_hex(now ? 0x24624E : has ? C_CARD2 : C_CARD), 0);
            lv_obj_set_style_border_color(c, lv_color_hex(bc), 0);
            lv_obj_set_style_border_width(c, now ? 3 : kind != TT_NORMAL ? 2 : 1, 0);
            lv_obj_set_style_opa(c, holiday && !now ? LV_OPA_60 : LV_OPA_COVER, 0);
        }
    }
}

/* ---------------- 찾기 화면 ---------------- */

static lv_obj_t *s_names, *s_title, *s_owner_btn, *s_now;
static tt_grid_t s_grid;
static int s_sel = -1;
static int s_last_min = -1;

/* ---------------- 시간표 바꾸기: 끌어서 교체 · 눌러서 보강 ---------------- */

static lv_obj_t *s_chg_hint, *s_undo_btn;
static lv_obj_t *s_ghost;              /* 끄는 동안 손가락을 따라다니는 칸 */
static lv_obj_t *s_hover;              /* 놓을 칸 */
static int s_drag_d = -1, s_drag_p;
static bool s_dragging, s_dragged;
static lv_point_t s_press_pt;
static lv_obj_t *s_pop, *s_pop_ta, *s_pop_subj_btns;
static int s_pop_ymd, s_pop_p;
static char s_pop_subj[28];

static void show_now(void);

static int week_day(int d)
{
    return ymd_add(ymd_week_monday(), d);
}

static void changed(void)
{
    tt_grid_fill(&s_grid, s_sel);
    show_now();
    int n = tt_week_changes(s_sel, ymd_week_monday());
    if (n) {
        char buf[32];
        snprintf(buf, sizeof(buf), LV_SYMBOL_REFRESH " 되돌리기 %d", n);
        label_set(lv_obj_get_child(s_undo_btn, 0), buf);
        lv_obj_clear_flag(s_undo_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_chg_hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_undo_btn, LV_OBJ_FLAG_HIDDEN);
        if (s_sel >= 0) {
            lv_obj_clear_flag(s_chg_hint, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_chg_hint, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void after_change(void)
{
    changed();
    home_refresh(D_TEACHERS);
}

static void on_undo_week(lv_event_t *e)
{
    tt_restore_week(s_sel, ymd_week_monday());
    after_change();
}

static bool cell_at(const lv_point_t *pt, int *d, int *p)
{
    for (int i = 0; i < TT_DAYS; i++) {
        for (int j = 0; j < TT_PERIODS; j++) {
            lv_area_t a;
            lv_obj_get_coords(s_grid.cell[i][j], &a);
            if (_lv_area_is_point_on(&a, pt, 0)) {
                *d = i;
                *p = j;
                return true;
            }
        }
    }
    return false;
}

static void set_hover(lv_obj_t *c)
{
    if (s_hover == c) {
        return;
    }
    if (s_hover) {
        lv_obj_set_style_outline_width(s_hover, 0, 0);
    }
    s_hover = c;
    if (c) {
        lv_obj_set_style_outline_color(c, lv_color_hex(C_WARM), 0);
        lv_obj_set_style_outline_width(c, 3, 0);
        lv_obj_set_style_outline_pad(c, 1, 0);
    }
}

static void drag_end(void)
{
    set_hover(NULL);
    if (s_ghost) {
        lv_obj_del(s_ghost);
        s_ghost = NULL;
    }
    if (s_drag_d >= 0) {
        lv_obj_set_style_opa(s_grid.cell[s_drag_d][s_drag_p], LV_OPA_COVER, 0);
    }
    s_dragging = false;
    s_drag_d = -1;
}

/* ---- 작은 창 (보강 넣기 · 원래대로) ---- */

static void pop_close(void)
{
    if (s_pop) {
        lv_obj_del_async(s_pop);   /* 창 안 단추의 이벤트에서 불리므로 나중에 지움 */
        s_pop = NULL;
    }
}

static void on_pop_cancel(lv_event_t *e)
{
    pop_close();
}

static void on_pop_restore(lv_event_t *e)
{
    tt_restore(s_sel, s_pop_ymd, s_pop_p);
    pop_close();
    after_change();
}

static void on_pop_add(lv_event_t *e)
{
    if (!s_pop_subj[0]) {
        return;
    }
    tt_add_extra(s_sel, s_pop_ymd, s_pop_p, s_pop_subj, lv_textarea_get_text(s_pop_ta));
    pop_close();
    after_change();
}

static void pick_subj(lv_obj_t *b)
{
    strlcpy(s_pop_subj, lv_label_get_text(lv_obj_get_child(b, 0)), sizeof(s_pop_subj));
    uint32_t n = lv_obj_get_child_cnt(s_pop_subj_btns);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *x = lv_obj_get_child(s_pop_subj_btns, i);
        btn_set_on(x, x == b);
    }
}

static void on_pop_subj(lv_event_t *e)
{
    pick_subj(lv_event_get_target(e));
}

static void on_pop_room(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_target(e);
    lv_textarea_set_text(s_pop_ta, lv_label_get_text(lv_obj_get_child(b, 0)));
}

static void on_pop_kb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        on_pop_add(e);
    } else if (code == LV_EVENT_CANCEL) {
        pop_close();
    }
}

static lv_obj_t *pop_box(int h, const char *title)
{
    pop_close();
    s_pop = mk_box(lv_layer_top(), 0, 0, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(s_pop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_pop, LV_OPA_70, 0);
    lv_obj_add_flag(s_pop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *box = mk_card(s_pop, 112, 12, SCR_W - 224, h);
    mk_label(box, &font_kr_22, C_TEXT, title);
    return box;
}

static void pop_title(int ymd, int p, const char *what, char *buf, size_t size)
{
    snprintf(buf, size, "%d/%d(%s) %d교시 %s", ymd / 100 % 100, ymd % 100, WEEKDAYS[ymd_wday(ymd)], p + 1, what);
}

/* 빈 칸: 보강 넣기. 과목은 그 선생님 시간표에 있는 과목 중에서, 교실은 숫자로 */
static void open_extra(int ymd, int p)
{
    const teachers_t *ts = g_data.teachers;
    const teacher_t *t = &ts->t[s_sel];
    char title[64];
    pop_title(ymd, p, "보강 넣기", title, sizeof(title));
    lv_obj_t *box = pop_box(250, title);
    s_pop_ymd = ymd;
    s_pop_p = p;
    s_pop_subj[0] = 0;

    lv_obj_t *l = mk_label(box, &font_kr_18, C_DIM, "과목");
    lv_obj_set_pos(l, 0, 44);
    s_pop_subj_btns = mk_box(box, 56, 36, SCR_W - 224 - 28 - 56, 44);
    lv_obj_set_flex_flow(s_pop_subj_btns, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_pop_subj_btns, 6, 0);
    lv_obj_add_flag(s_pop_subj_btns, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_pop_subj_btns, LV_DIR_HOR);
    lv_obj_t *first = mk_btn(s_pop_subj_btns, "보강", &font_kr_18, on_pop_subj, NULL);
    lv_obj_set_height(first, 40);
    const char *seen[12];
    int ns = 0;
    const char *rooms[6];
    int nr = 0;
    for (int d = 0; d < TT_DAYS; d++) {
        for (int q = 0; q < TT_PERIODS; q++) {
            const char *sj = t->subj[d][q], *rm = t->room[d][q];
            bool dup = !sj[0];
            for (int k = 0; k < ns && !dup; k++) {
                dup = !strcmp(seen[k], sj);
            }
            if (!dup && ns < 12) {
                seen[ns++] = sj;
                lv_obj_t *b = mk_btn(s_pop_subj_btns, sj, &font_kr_18, on_pop_subj, NULL);
                lv_obj_set_height(b, 40);
            }
            dup = !rm[0];
            for (int k = 0; k < nr && !dup; k++) {
                dup = !strcmp(rooms[k], rm);
            }
            if (!dup && nr < 6) {
                rooms[nr++] = rm;
            }
        }
    }
    pick_subj(first);

    l = mk_label(box, &font_kr_18, C_DIM, "교실");
    lv_obj_set_pos(l, 0, 104);
    s_pop_ta = lv_textarea_create(box);
    lv_textarea_set_one_line(s_pop_ta, true);
    lv_textarea_set_placeholder_text(s_pop_ta, "(없어도 됨)");
    lv_obj_set_style_text_font(s_pop_ta, &font_kr_22, 0);
    lv_obj_set_size(s_pop_ta, 160, 48);
    lv_obj_set_pos(s_pop_ta, 56, 92);
    for (int k = 0; k < nr; k++) {
        lv_obj_t *b = mk_btn(box, rooms[k], &font_kr_18, on_pop_room, NULL);
        lv_obj_set_size(b, 80, 40);
        lv_obj_set_pos(b, 228 + k * 86, 96);
    }

    lv_obj_t *b = mk_btn(box, "넣기", &font_kr_22, on_pop_add, NULL);
    btn_set_on(b, true);
    lv_obj_set_size(b, 160, 48);
    lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    b = mk_btn(box, "취소", &font_kr_22, on_pop_cancel, NULL);
    lv_obj_set_size(b, 120, 48);
    lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, -172, 0);

    lv_obj_t *kb = lv_keyboard_create(s_pop);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(kb, SCR_W, 320);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_24, 0);
    lv_keyboard_set_textarea(kb, s_pop_ta);
    lv_obj_add_event_cb(kb, on_pop_kb, LV_EVENT_ALL, NULL);
}

/* 바뀐 칸: 원래대로 (옮겨 간 빈 칸이면 보강도 넣을 수 있게) */
static void open_restore(int ymd, int p, int kind)
{
    char title[64], body[96];
    pop_title(ymd, p, kind == TT_EXTRA ? "보강" : "바뀐 수업", title, sizeof(title));
    lv_obj_t *box = pop_box(170, title);
    s_pop_ymd = ymd;
    s_pop_p = p;
    const char *subj, *room;
    tt_lesson(s_sel, ymd, p, &subj, &room, NULL);
    snprintf(body, sizeof(body), kind == TT_MOVED_OUT ? "%s 수업이 다른 시간으로 옮겨 갔습니다." : "%s %s", subj, room);
    lv_obj_t *l = mk_label(box, &font_kr_22, C_DIM, body);
    lv_obj_set_pos(l, 0, 44);

    lv_obj_t *b = mk_btn(box, kind == TT_EXTRA ? "보강 지우기" : "원래대로", &font_kr_22, on_pop_restore, NULL);
    btn_set_on(b, true);
    lv_obj_set_size(b, 180, 48);
    lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    b = mk_btn(box, "취소", &font_kr_22, on_pop_cancel, NULL);
    lv_obj_set_size(b, 120, 48);
    lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, -192, 0);
}

static void on_tap(int d, int p)
{
    const teachers_t *ts = g_data.teachers;
    if (!ts || s_sel < 0 || s_sel >= ts->count) {
        return;
    }
    int ymd = week_day(d), kind;
    bool has = tt_lesson(s_sel, ymd, p, NULL, NULL, &kind);
    if (kind != TT_NORMAL) {
        open_restore(ymd, p, kind);
    } else if (!has) {
        open_extra(ymd, p);
    }
}

static void on_grid_cell(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    int d = v / 10, p = v % 10;
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t pt = { 0 };
    if (indev) {
        lv_indev_get_point(indev, &pt);
    }
    const teachers_t *ts = g_data.teachers;
    bool ok = ts && s_sel >= 0 && s_sel < ts->count;

    if (code == LV_EVENT_PRESSED) {
        s_drag_d = d;
        s_drag_p = p;
        s_dragging = s_dragged = false;
        s_press_pt = pt;
    } else if (code == LV_EVENT_PRESSING && s_drag_d >= 0 && ok) {
        if (!s_dragging && (LV_ABS(pt.x - s_press_pt.x) > 12 || LV_ABS(pt.y - s_press_pt.y) > 12)) {
            const char *subj, *room;
            if (!tt_lesson(s_sel, week_day(s_drag_d), s_drag_p, &subj, &room, NULL)) {
                s_drag_d = -1;   /* 빈 칸은 끌 수 없음 */
                return;
            }
            s_dragging = s_dragged = true;
            lv_obj_t *c = s_grid.cell[s_drag_d][s_drag_p];
            lv_obj_set_style_opa(c, LV_OPA_40, 0);
            s_ghost = mk_box(lv_layer_top(), 0, 0, lv_obj_get_width(c), lv_obj_get_height(c));
            lv_obj_set_style_radius(s_ghost, 8, 0);
            lv_obj_set_style_bg_opa(s_ghost, LV_OPA_90, 0);
            lv_obj_set_style_bg_color(s_ghost, lv_color_hex(C_ACCENT), 0);
            lv_obj_set_style_border_color(s_ghost, lv_color_hex(C_WARM), 0);
            lv_obj_set_style_border_width(s_ghost, 2, 0);
            char buf[48];
            snprintf(buf, sizeof(buf), "%s\n%s", subj, room);
            lv_obj_t *l = mk_label(s_ghost, &font_kr_18, C_TEXT, buf);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_center(l);
        }
        if (s_dragging) {
            lv_obj_set_pos(s_ghost, pt.x - lv_obj_get_width(s_ghost) / 2, pt.y - lv_obj_get_height(s_ghost) / 2);
            int td, tp;
            set_hover(cell_at(&pt, &td, &tp) && (td != s_drag_d || tp != s_drag_p) ? s_grid.cell[td][tp] : NULL);
        }
    } else if (code == LV_EVENT_RELEASED) {
        int td, tp;
        bool move = s_dragging && cell_at(&pt, &td, &tp) && (td != s_drag_d || tp != s_drag_p);
        int fd = s_drag_d, fp = s_drag_p;
        drag_end();
        if (move) {
            tt_move(s_sel, week_day(fd), fp, week_day(td), tp);
            after_change();
        }
    } else if (code == LV_EVENT_PRESS_LOST) {
        drag_end();
    } else if (code == LV_EVENT_CLICKED && !s_dragged && ok) {
        on_tap(d, p);
    }
}

static void grid_interactive(void)
{
    for (int d = 0; d < TT_DAYS; d++) {
        for (int p = 0; p < TT_PERIODS; p++) {
            lv_obj_t *c = s_grid.cell[d][p];
            lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
            lv_obj_clear_flag(c, LV_OBJ_FLAG_GESTURE_BUBBLE);
            lv_obj_add_event_cb(c, on_grid_cell, LV_EVENT_ALL, (void *)(intptr_t)(d * 10 + p));
        }
    }
}

static void show_now(void)
{
    const teachers_t *ts = g_data.teachers;
    char buf[128];
    if (!ts || s_sel < 0 || s_sel >= ts->count) {
        label_set(s_now, "");
        return;
    }
    int today = ymd_today(), wd = ymd_wday(today);
    int next, cur = tt_current_period(&next);
    bool holiday;
    school_day_info(today, &holiday);
    if (wd == 0 || wd == 6 || holiday) {
        strlcpy(buf, "오늘은 수업이 없는 날입니다", sizeof(buf));
    } else {
        int left = 0;
        const char *subj, *room;
        for (int p = cur >= 0 ? cur + 1 : next; p >= 0 && p < TT_PERIODS; p++) {
            left += tt_lesson(s_sel, today, p, NULL, NULL, NULL);
        }
        if (cur >= 0 && tt_lesson(s_sel, today, cur, &subj, &room, NULL)) {
            snprintf(buf, sizeof(buf), "지금 · %d교시 %s %s   (남은 수업 %d)", cur + 1, subj, room, left);
        } else if (cur >= 0) {
            snprintf(buf, sizeof(buf), "지금 · %d교시 공강   (남은 수업 %d)", cur + 1, left);
        } else if (next >= 0 && left) {
            int p = next;
            while (p < TT_PERIODS && !tt_lesson(s_sel, today, p, &subj, &room, NULL)) {
                p++;
            }
            snprintf(buf, sizeof(buf), "다음 · %d교시 %s %s   (남은 수업 %d)", p + 1, subj, room, left);
        } else {
            strlcpy(buf, "지금 · 오늘 수업 끝", sizeof(buf));
        }
    }
    label_set(s_now, buf);
}

static void select_teacher(int i)
{
    const teachers_t *ts = g_data.teachers;
    s_sel = i;
    char buf[64];
    if (ts && i >= 0 && i < ts->count) {
        snprintf(buf, sizeof(buf), "%s 선생님", ts->t[i].name);
    } else {
        strlcpy(buf, "선생님을 고르세요", sizeof(buf));
    }
    label_set(s_title, buf);
    bool is_owner = ts && i >= 0 && !strcmp(ts->t[i].name, g_cfg.owner);
    label_set(lv_obj_get_child(s_owner_btn, 0), is_owner ? "이 보드 주인 " LV_SYMBOL_OK : "이 보드 주인으로");
    btn_set_on(s_owner_btn, is_owner);
    if (i >= 0) {
        lv_obj_clear_flag(s_owner_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_owner_btn, LV_OBJ_FLAG_HIDDEN);
    }
    uint32_t n = lv_obj_get_child_cnt(s_names);
    for (uint32_t k = 0; k < n; k++) {
        btn_set_on(lv_obj_get_child(s_names, k), (int)k == i);
    }
    changed();
}

static void on_name(lv_event_t *e)
{
    select_teacher((int)(intptr_t)lv_event_get_user_data(e));
}

static void on_owner(lv_event_t *e)
{
    const teachers_t *ts = g_data.teachers;
    if (!ts || s_sel < 0 || s_sel >= ts->count) {
        return;
    }
    strlcpy(g_cfg.owner, ts->t[s_sel].name, sizeof(g_cfg.owner));
    config_save();
    select_teacher(s_sel);
    home_refresh(D_TEACHERS);
}

void find_build(lv_obj_t *page)
{
    lv_obj_t *left = mk_card(page, PAD, BODY_Y, 330, BODY_H);
    mk_label(left, &font_kr_18, C_DIM, "선생님을 고르세요");
    s_names = lv_obj_create(left);
    lv_obj_remove_style_all(s_names);
    lv_obj_set_pos(s_names, 0, 30);
    lv_obj_set_size(s_names, 300, BODY_H - 28 - 30);
    lv_obj_set_flex_flow(s_names, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(s_names, 6, 0);
    lv_obj_set_style_pad_column(s_names, 6, 0);
    lv_obj_set_scroll_dir(s_names, LV_DIR_VER);

    lv_obj_t *right = mk_card(page, PAD + 330 + PAD, BODY_Y, SCR_W - 330 - PAD * 3, BODY_H);
    s_title = mk_label(right, &font_kr_26, C_TEXT, "");
    s_owner_btn = mk_btn(right, "", &font_kr_18, on_owner, NULL);
    lv_obj_set_size(s_owner_btn, 170, 38);
    lv_obj_align(s_owner_btn, LV_ALIGN_TOP_RIGHT, 0, -4);
    s_chg_hint = mk_label(right, &font_kr_18, C_DIM, "끌어서 교체 · 빈 칸은 보강");
    lv_obj_align(s_chg_hint, LV_ALIGN_TOP_RIGHT, -182, 6);
    s_undo_btn = mk_btn(right, "", &font_kr_18, on_undo_week, NULL);
    lv_obj_set_size(s_undo_btn, 150, 38);
    lv_obj_align(s_undo_btn, LV_ALIGN_TOP_RIGHT, -182, -4);
    lv_obj_add_flag(s_undo_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *bar = mk_box(right, 0, 40, lv_pct(100), 36);
    lv_obj_set_style_radius(bar, 10, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_RED), 0);
    lv_obj_set_style_bg_grad_color(bar, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, 0);
    s_now = mk_label(bar, &font_kr_22, C_TEXT, "");
    lv_obj_align(s_now, LV_ALIGN_LEFT_MID, 12, 0);

    int gw = SCR_W - 330 - PAD * 3 - 30;
    tt_grid_create(&s_grid, right, 0, 84, gw, BODY_H - 28 - 84);
    grid_interactive();
    find_refresh();
}

void find_refresh(void)
{
    const teachers_t *ts = g_data.teachers;
    lv_obj_clean(s_names);
    for (int i = 0; ts && i < ts->count; i++) {
        lv_obj_t *b = mk_btn(s_names, ts->t[i].name, &font_kr_18, on_name, (void *)(intptr_t)i);
        lv_obj_set_size(b, 94, 44);
    }
    int keep = s_sel;
    if (!ts || keep >= ts->count) {
        keep = -1;
    }
    if (keep < 0) {
        keep = teachers_find(g_cfg.owner);
    }
    select_teacher(keep);
}

void find_tick(void)
{
    int m = now_minute();
    if (m != s_last_min && !s_dragging) {
        s_last_min = m;
        tt_grid_fill(&s_grid, s_sel);
        show_now();
    }
}
