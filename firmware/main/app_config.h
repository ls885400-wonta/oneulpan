#pragma once

#include <stdbool.h>

#define APP_VERSION "0.1.0"

/* 설치 페이지에서 보내 주는 설정. 보드 안(NVS)에 JSON 으로 저장됩니다. */
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
    char level[4];         /* els / mis / his (시간표용) */
    char grade[4];
    char cls[8];
    char neis_key[48];     /* 나이스 인증키 (없어도 됨) */
    char ical[512];        /* 구글 캘린더 비공개 iCal 주소 */
} app_config_t;

extern app_config_t g_cfg;

void config_load(void);
/* 받은 JSON 을 지금 설정에 덮어쓰고 저장합니다. 없는 항목은 그대로 둡니다. */
bool config_apply_json(const char *json);
/* 비밀번호·캘린더 주소를 뺀 설정을 JSON 으로 돌려줍니다 (free 필요). */
char *config_public_json(void);

/* 설치 페이지와 USB(UART)로 주고받는 작업을 시작합니다. */
void config_serial_start(void);
