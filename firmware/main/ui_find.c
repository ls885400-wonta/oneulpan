/* 일주일 시간표 표, 그리고 '찾기' 화면 (교사 고르기)
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
        lv_obj_set_pos(g->head[d], pw + gap + d * (cw + gap), 2);
    }
    for (int p = 0; p < TT_PERIODS; p++) {
        int cy = hh + p * (ch + gap);
        g->plabel[p] = mk_label(g->box, &font_kr_18, C_DIM, "");
        lv_obj_set_width(g->plabel[p], pw);
        lv_obj_set_style_text_align(g->plabel[p], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(g->plabel[p], -2, 0);
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
    }
    for (int d = 0; d < TT_DAYS; d++) {
        int ymd = ymd_add(monday, d);
        bool holiday;
        const acad_t *a = school_day_info(ymd, &holiday);
        bool is_today = ymd == today;
        snprintf(buf, sizeof(buf), "%s %d/%d", WEEKDAYS[d + 1], ymd / 100 % 100, ymd % 100);
        label_set(g->head[d], buf);
        lv_obj_set_style_text_color(g->head[d], lv_color_hex(holiday ? C_RED : is_today ? C_GREEN : C_DIM), 0);

        for (int p = 0; p < TT_PERIODS; p++) {
            lv_obj_t *c = g->cell[d][p];
            bool has = t && t->subj[d][p][0];
            if (has) {
                snprintf(buf, sizeof(buf), "%s\n%s", t->subj[d][p], t->room[d][p]);
            } else if (holiday && p == 0 && a) {
                strlcpy(buf, a->name, sizeof(buf));  /* 휴일 이름은 첫 칸에 */
            } else {
                buf[0] = 0;
            }
            label_set(g->text[d][p], msg ? "" : buf);
            lv_obj_set_style_text_color(g->text[d][p], lv_color_hex(holiday ? C_RED : C_TEXT), 0);

            bool now = is_today && p == cur;
            lv_obj_set_style_bg_opa(c, msg ? LV_OPA_TRANSP : has ? LV_OPA_COVER : LV_OPA_30, 0);
            lv_obj_set_style_bg_color(c, lv_color_hex(now ? 0x24624E : has ? C_CARD2 : C_CARD), 0);
            lv_obj_set_style_border_color(c, lv_color_hex(now ? C_GREEN : is_today && has ? 0x3E8F77 : C_LINE), 0);
            lv_obj_set_style_border_width(c, now ? 3 : 1, 0);
            lv_obj_set_style_opa(c, holiday && !now ? LV_OPA_60 : LV_OPA_COVER, 0);
        }
    }
}

/* ---------------- 찾기 화면 ---------------- */

static lv_obj_t *s_names, *s_title, *s_owner_btn, *s_now;
static tt_grid_t s_grid;
static int s_sel = -1;
static int s_last_min = -1;

static void show_now(void)
{
    const teachers_t *ts = g_data.teachers;
    char buf[128];
    if (!ts || s_sel < 0 || s_sel >= ts->count) {
        label_set(s_now, "");
        return;
    }
    const teacher_t *t = &ts->t[s_sel];
    int today = ymd_today(), wd = ymd_wday(today);
    int next, cur = tt_current_period(&next);
    bool holiday;
    school_day_info(today, &holiday);
    if (wd == 0 || wd == 6 || holiday) {
        strlcpy(buf, "오늘은 수업이 없는 날입니다", sizeof(buf));
    } else {
        int d = wd - 1;
        int left = 0;
        for (int p = cur >= 0 ? cur + 1 : next; p >= 0 && p < TT_PERIODS; p++) {
            left += t->subj[d][p][0] != 0;
        }
        if (cur >= 0 && t->subj[d][cur][0]) {
            snprintf(buf, sizeof(buf), "지금 · %d교시 %s %s   (남은 수업 %d)", cur + 1, t->subj[d][cur],
                     t->room[d][cur], left);
        } else if (cur >= 0) {
            snprintf(buf, sizeof(buf), "지금 · %d교시 공강   (남은 수업 %d)", cur + 1, left);
        } else if (next >= 0 && left) {
            int p = next;
            while (p < TT_PERIODS && !t->subj[d][p][0]) {
                p++;
            }
            snprintf(buf, sizeof(buf), "다음 · %d교시 %s %s   (남은 수업 %d)", p + 1, t->subj[d][p],
                     t->room[d][p], left);
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
    tt_grid_fill(&s_grid, i);
    show_now();
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
    if (m != s_last_min) {
        s_last_min = m;
        tt_grid_fill(&s_grid, s_sel);
        show_now();
    }
}
