#pragma once

#include <stdbool.h>
#include <stddef.h>

#define APP_VERSION "0.3.0"

/* 설치 페이지·보드 설정 화면에서 정하는 값. 보드 안(NVS)에 JSON 으로 저장됩니다.
 * 모두 글자로 저장하고, 숫자는 쓸 때 atoi 로 읽습니다. */
typedef struct {
    char ssid[33];
    char pass[65];
    char title[64];        /* 화면 위쪽 이름, 예: "교무실" */
    char place[48];        /* 날씨 위치 이름 */
    char lat[16];
    char lon[16];
    char atpt[8];          /* 나이스 시도교육청 코드 */
    char school[12];       /* 나이스 학교 코드 */
    char school_name[64];
    char level[4];         /* els / mis / his */
    char grade[4];         /* (예전 판 호환용) */
    char cls[8];
    char neis_key[48];     /* 나이스 인증키 (없어도 됨) */
    char ical[512];        /* 내 구글 캘린더 비공개 iCal 주소 */
    char sched_url[256];   /* 학교 일정 웹앱 주소 */
    char cha_url[512];     /* 창체 계획: 구글 시트 또는 iCal 주소 */
    char owner[32];        /* 이 보드 주인 선생님 (홈 시간표) */
    /* 보드 설정 화면 */
    char night_from[4];    /* 화면 끄는 시각(시), 비우면 안 끔 */
    char night_to[4];      /* 화면 켜는 시각(시) */
    char weekend_off[2];   /* "1" 이면 주말·휴일에 화면 끔 */
    char h12[2];           /* "1" 이면 12시간 표시 */
    char home_ret[4];      /* 몇 분 동안 손대지 않으면 홈으로 (0 = 안 함) */
    char show[16];         /* 홈에 보일 칸: w 날씨, d 미세먼지, m 급식, t 시간표, s 오늘 일정 */
} app_config_t;

extern app_config_t g_cfg;

void config_load(void);
/* 받은 JSON 을 지금 설정에 덮어쓰고 저장합니다. 없는 항목은 그대로 둡니다. */
bool config_apply_json(const char *json);
/* 지금 설정(g_cfg)을 그대로 저장합니다. */
bool config_save(void);
/* 비밀번호·캘린더 주소를 뺀 설정을 JSON 으로 돌려줍니다 (free 필요). */
char *config_public_json(void);
/* 홈에 그 칸을 보일지 (show 에 글자가 있는지) */
bool config_show(char what);

/* 설치 페이지와 USB(UART)로 주고받는 작업을 시작합니다. */
void config_serial_start(void);

/* ---- 보드 안 파일 (SPIFFS) ---- */
void storage_init(void);
/* /data/<name>.json 을 읽어 돌려줍니다 (PSRAM, free 필요). 없으면 NULL */
char *storage_read(const char *name, size_t *len);
bool storage_write(const char *name, const char *data, size_t len);

/* 파일을 새로 받으면 불립니다 (teachers.c) */
void teachers_reload(void);
