#pragma once

#include <stdbool.h>
#include <time.h>
#include "feeds.h"

/* LVGL 잠금 안에서 한 번 부릅니다. */
void ui_init(void);

/* 아래 함수들은 다른 작업에서 불러도 됩니다 (안에서 LVGL 을 잠급니다). */
void ui_set_status(bool configured, bool connected, const char *ip);
void ui_set_updated(time_t when);
void ui_set_weather(const weather_t *w);
void ui_set_meal(const meal_t *m);
void ui_set_timetable(const timetable_t *tt, bool tomorrow);
void ui_set_calendar(const calendar_t *cal);
