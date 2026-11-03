#pragma once
/* 화면 파일들끼리 함께 쓰는 것 */

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "feeds.h"
#include "fonts_kr.h"
#include "lvgl.h"

#define C_BG     0x101826
#define C_CARD   0x1A2539
#define C_CARD2  0x253350
#define C_LINE   0x364767
#define C_TEXT   0xF5F7FA
#define C_DIM    0xA3AFC4
#define C_BLUE   0x6CB6FF
#define C_ACCENT 0x2F74E8
#define C_WARM   0xFFB547
#define C_GREEN  0x5BD6A0
#define C_PINK   0xFF7A90
#define C_RED    0xE5484D
#define C_SKY    0x7DD3FC
#define C_BTN    0x2C3B5B   /* 고르지 않은 단추 */
#define C_BTN_LINE 0x4A5D84

#define SCR_W 1024
#define SCR_H 600
#define BAR_H 64
#define PAD 8
/* 위쪽 줄 아래 내용 칸 */
#define BODY_Y (BAR_H + PAD)
#define BODY_H (SCR_H - BODY_Y - PAD)

enum { PG_HOME, PG_SCHOOL, PG_CHA, PG_FIND, PG_CAL, PG_MEAL, PG_SET, PG_COUNT };

void ui_go(int page);
int ui_page(void);

/* 만들기 도우미 */
lv_obj_t *mk_box(lv_obj_t *parent, int x, int y, int w, int h);  /* 투명한 칸 */
lv_obj_t *mk_card(lv_obj_t *parent, int x, int y, int w, int h);
lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
lv_obj_t *mk_wrap(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
lv_obj_t *mk_btn(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_event_cb_t cb, void *user);
void btn_set_on(lv_obj_t *btn, bool on);
/* "급식 >" 처럼 누르면 다른 화면으로 가는 글자 */
lv_obj_t *mk_link(lv_obj_t *parent, const char *text, int page);
/* 세로로 쌓이는 스크롤 목록 */
lv_obj_t *mk_list(lv_obj_t *parent, int x, int y, int w, int h);
void label_set(lv_obj_t *label, const char *text);  /* 같으면 그대로 */

/* 날씨 그림 (code = WMO 날씨 코드) */
lv_obj_t *mk_wx_icon(lv_obj_t *parent, int size);
void wx_icon_set(lv_obj_t *icon, int code);
/* 시계를 누르면 나오는 메뉴, 달력 칸을 누르면 나오는 자세히 */
void ui_menu_open(void);
void ui_popup(const char *title, const char *body);

/* 글자 만들기 */
const char *weather_text(int code);
uint32_t weather_color(int code);
void dust_text(int pm10, int pm25, char *buf, size_t size, uint32_t *color);
/* "밥\n국\n김치" -> "밥 · 국 · 김치" */
void join_menu(const char *menu, const char *sep, char *buf, size_t size);
void date_text(int ymd, char *buf, size_t size);   /* "11월 3일 (화)" */
int now_minute(void);
/* 수업하는 날인지 (주말·휴일이 아니면) */
bool is_school_day(int ymd);

/* 일주일 시간표 표 */
typedef struct {
    lv_obj_t *box, *msg;
    lv_obj_t *head[TT_DAYS];
    lv_obj_t *plabel[TT_PERIODS];
    lv_obj_t *cell[TT_DAYS][TT_PERIODS];
    lv_obj_t *text[TT_DAYS][TT_PERIODS];
} tt_grid_t;
void tt_grid_create(tt_grid_t *g, lv_obj_t *parent, int x, int y, int w, int h);
void tt_grid_fill(tt_grid_t *g, int teacher);
/* 지금 몇 교시인지 (-1 = 수업 시간 아님), next 에는 다음 교시 */
int tt_current_period(int *next);

/* 화면별 */
void home_build(lv_obj_t *page);
void home_refresh(int kind);
void home_tick(void);

void find_build(lv_obj_t *page);
void find_refresh(void);
void find_tick(void);

void school_build(lv_obj_t *page);
void school_refresh(void);
void cha_build(lv_obj_t *page);
void cha_refresh(void);
void cal_build(lv_obj_t *page);
void cal_refresh(void);
void meal_build(lv_obj_t *page);
void meal_refresh(void);

void set_build(lv_obj_t *page);
void set_tick(void);
void set_wifi_list(const void *list);

extern char g_status_text[64];
extern char g_batt_text[24];   /* "배터리그림 97%" (없으면 빈 글자) */
