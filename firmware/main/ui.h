#pragma once

#include <stdbool.h>
#include <time.h>

#include "feeds.h"

/* 모든 화면을 만듭니다. LVGL 잠금 안에서 부르세요. */
void ui_init(void);

/* 화면 180도 뒤집기 (main.c). 바꾸면 보드가 다시 켜집니다. */
bool screen_flipped(void);
void screen_flip_toggle(void);

/* 자료가 바뀌었을 때 (LVGL 잠금 안에서 불림, feeds_swap) */
void ui_refresh(int kind);

/* 아래 둘은 잠금 밖에서 부릅니다 */
void ui_set_status(bool configured, bool connected, const char *ip);
void ui_set_updated(time_t when);

/* 와이파이 찾기 결과 (net.c 가 잠금 밖에서 부름) */
#define WIFI_SCAN_MAX 20
typedef struct {
    int count;
    struct {
        char ssid[33];
        int rssi;
        bool open;
    } ap[WIFI_SCAN_MAX];
} wifi_scan_t;
void ui_wifi_scanned(const wifi_scan_t *list);
