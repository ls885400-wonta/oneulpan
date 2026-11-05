#pragma once

#include <stdbool.h>
#include <time.h>

/* 인터넷·파일에서 받아 온 자료들. 모두 PSRAM 에 두고,
 * 새로 받으면 화면 잠금(LVGL lock) 안에서 통째로 바꿔 끼웁니다(feeds_swap).
 * 화면 코드는 LVGL 잠금 안에서만 읽으므로 따로 잠그지 않아도 됩니다. */

/* ---- 날짜 도우미: 날짜는 20261102 같은 정수(ymd)로 다룹니다 ---- */
int ymd_today(void);
int ymd_add(int ymd, int days);
int ymd_wday(int ymd);                  /* 0 = 일요일 */
int ymd_diff(int a, int b);             /* a - b (일) */
int ymd_week_monday(void);              /* 이번 주 월요일 (주말이면 다음 주 월요일) */
extern const char *WEEKDAYS[7];         /* "일".."토" */

typedef struct {
    bool ok;
    float temp;
    int code;           /* WMO 날씨 코드 */
    int humidity;
    int day_code[7];    /* 오늘부터 7일 */
    float day_max[7];
    float day_min[7];
    int day_pop[7];     /* 강수 확률 % */
    bool air_ok;
    int pm10, pm25;     /* 미세먼지·초미세먼지 ㎍/㎥ */
} weather_t;

#define MEAL_DAYS 7
typedef struct {
    int ymd;
    char lunch[600];
    char dinner[600];
} meal_day_t;

typedef struct {
    bool ok;
    int count;
    meal_day_t day[MEAL_DAYS];   /* 오늘부터 급식이 있는 날 */
} meals_t;

#define MAX_EVENTS 60
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

/* 학교 일정 웹앱: 학사 일정(휴일·시험·행사)과 부서 일정 */
#define MAX_ACAD 240
#define MAX_SCHED 300
typedef struct {
    int ymd;
    char name[64];
    char type[16];      /* 공휴일 방학 지필 시험 행사 재량휴업일 기타 */
} acad_t;

typedef struct {
    int start, end;     /* ymd */
    char dept[32];
    char title[128];
    char time[48];
} sched_t;

typedef struct {
    bool ok;
    int acad_count, sched_count;
    acad_t acad[MAX_ACAD];
    sched_t sched[MAX_SCHED];
} school_t;

/* 창체 계획: 구글 시트(CSV) 표, 또는 iCal 캘린더 */
#define CHA_COLS 8
#define CHA_ROWS 120
typedef struct {
    bool ok;
    bool is_cal;
    int cols, rows;
    char head[CHA_COLS][32];
    char cell[CHA_ROWS][CHA_COLS][80];
    int ymd[CHA_ROWS];  /* 월·일 칸으로 계산한 날짜 (없으면 0) */
    calendar_t cal;
} cha_t;

/* 교사 시간표 (설치 페이지가 한글 파일을 읽어 보내 준 것) */
#define MAX_TEACHERS 100
#define TT_DAYS 5
#define TT_PERIODS 7
#define CLEAN_START (15 * 60 + 20) /* 청소 15:20 ~ 15:40, 끝나면 7교시 */
#define CLEAN_END (15 * 60 + 40)
typedef struct {
    char name[32];
    char subj[TT_DAYS][TT_PERIODS][28];
    char room[TT_DAYS][TT_PERIODS][12];
} teacher_t;

typedef struct {
    int count;
    int ptime[TT_PERIODS];      /* 교시 시작 시각(분) */
    teacher_t t[MAX_TEACHERS];
} teachers_t;

typedef struct {
    weather_t *weather;
    meals_t *meals;
    calendar_t *mycal;
    school_t *school;
    cha_t *cha;
    teachers_t *teachers;
} data_t;

extern data_t g_data;

enum { D_WEATHER, D_MEALS, D_MYCAL, D_SCHOOL, D_CHA, D_TEACHERS };

/* 새 자료를 끼우고 화면을 고칩니다 (예전 자료는 풀어 줍니다). */
void feeds_swap(int kind, void *fresh);

/* 그날의 학사 일정 (없으면 NULL). 휴일이면 *holiday = true */
const acad_t *school_day_info(int ymd, bool *holiday);

/* 자료를 주기적으로 받아 화면에 넣는 작업을 시작합니다. */
void feeds_start(void);
/* 바로 새로 받습니다. */
void feeds_refresh_now(void);

/* ical.c: 오늘부터 days 일 동안의 일정 */
bool ical_fetch(const char *url, calendar_t *out, int days);
/* school.c */
bool school_fetch(const char *url, school_t *out);
bool cha_fetch(const char *url, cha_t *out);
/* teachers.c */
void teachers_load(void);
int teachers_find(const char *name);

/* 시간표 바꾸기 (수업 교체 · 보강). 날짜마다 칸을 덮어써서 /data/ttchg.json 에 둡니다.
 *  ti 는 g_data.teachers 안의 번호, p 는 0부터 교시. */
enum { TT_NORMAL, TT_MOVED_OUT, TT_MOVED_IN, TT_EXTRA };
/* 그날 그 교시 수업 (바뀐 것 반영). 수업이 있으면 true.
 * TT_MOVED_OUT(옮겨 가서 빈 칸)이면 false 이고 subj·room 은 원래 수업. */
bool tt_lesson(int ti, int ymd, int p, const char **subj, const char **room, int *kind);
/* from 칸 수업을 to 칸으로 (to 에 수업이 있으면 서로 맞바꿈) */
void tt_move(int ti, int from_ymd, int from_p, int to_ymd, int to_p);
/* 빈 칸에 보강 넣기 */
void tt_add_extra(int ti, int ymd, int p, const char *subj, const char *room);
/* 그 칸을 원래 시간표대로 */
void tt_restore(int ti, int ymd, int p);
/* monday 부터 5일 동안 바뀐 칸 수 / 모두 원래대로 */
int tt_week_changes(int ti, int monday);
void tt_restore_week(int ti, int monday);
