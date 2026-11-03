#pragma once

#include <stdbool.h>

/* 양력 날짜(yyyymmdd)를 음력 달·날로. 2020~2039 밖이면 false */
bool lunar_from_solar(int ymd, int *month, int *day, bool *leap);
