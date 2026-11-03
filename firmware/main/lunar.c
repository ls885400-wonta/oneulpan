/* 양력 → 음력 (2020~2039, 한국 기준)
 *
 *  해마다 한 칸: 0x8000 >> (달-1) 이 켜져 있으면 그 달은 30일, 아니면 29일.
 *  아래 4비트는 윤달 (0 이면 없음), 0x10000 이 켜져 있으면 윤달이 30일.
 *  2026·2027년은 중국 음력과 하루 다른 한국 음력에 맞췄습니다 (2027 설날 2/7). */
#include "lunar.h"

#include "feeds.h"

#define FIRST_YEAR 2020
#define BASE_YMD 20200125 /* 2020년 음력 1월 1일 */

static const unsigned TABLE[] = {
    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4f0, 0x05260, 0x0ea65, 0x0d530,
    0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0, 0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45,
};
#define YEARS (int)(sizeof(TABLE) / sizeof(TABLE[0]))

static int month_days(unsigned v, int m)
{
    return v & (0x8000 >> (m - 1)) ? 30 : 29;
}

static int leap_days(unsigned v)
{
    return (v & 0xf) == 0 ? 0 : v & 0x10000 ? 30 : 29;
}

bool lunar_from_solar(int ymd, int *month, int *day, bool *leap)
{
    int off = ymd_diff(ymd, BASE_YMD);
    if (off < 0) {
        return false;
    }
    for (int i = 0; i < YEARS; i++) {
        unsigned v = TABLE[i];
        int lm = v & 0xf;
        for (int m = 1; m <= 12; m++) {
            int n = month_days(v, m);
            if (off < n) {
                *month = m, *day = off + 1, *leap = false;
                return true;
            }
            off -= n;
            if (m == lm) {
                n = leap_days(v);
                if (off < n) {
                    *month = m, *day = off + 1, *leap = true;
                    return true;
                }
                off -= n;
            }
        }
    }
    return false;
}
