#pragma once

#include <stdbool.h>

/* 인터넷에서 받아 온 자료들 */

typedef struct {
    bool ok;
    float temp;
    int code;           /* WMO 날씨 코드 */
    int humidity;
    int day_code[3];    /* 오늘, 내일, 모레 */
    float day_max[3];
    float day_min[3];
    int day_pop[3];     /* 강수 확률 % */
} weather_t;

typedef struct {
    bool ok;
    bool tomorrow;      /* 오후에는 내일 급식을 보여 줍니다 */
    char lunch[600];
    char dinner[600];
} meal_t;

#define MAX_PERIODS 8
typedef struct {
    bool ok;
    int count;
    char subject[MAX_PERIODS][48];
} timetable_t;

#define MAX_EVENTS 24
typedef struct {
    int day;            /* 오늘부터 며칠 뒤 (0 = 오늘) */
    int minute;         /* 하루 중 시작 시각(분), 종일 일정은 -1 */
    char title[96];
} cal_event_t;

typedef struct {
    bool ok;
    int count;
    cal_event_t ev[MAX_EVENTS];
} calendar_t;

/* 자료를 주기적으로 받아 화면에 넣는 작업을 시작합니다. */
void feeds_start(void);
/* 화면을 누르면 바로 새로 받습니다. */
void feeds_refresh_now(void);

/* ical.c */
bool ical_fetch(const char *url, calendar_t *out);
