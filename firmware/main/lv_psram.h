#pragma once
/* LVGL 이 쓰는 메모리를 모두 PSRAM 에서 받습니다.
 * (작은 조각이 내부 RAM 을 다 쓰면 와이파이가 켜지지 못합니다)
 * CMakeLists.txt 에서 LVGL 에 LV_MEM_CUSTOM_* 로 알려 줍니다. */
#include <stdlib.h>
#include "esp_heap_caps.h"

static inline void *lv_psram_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

static inline void *lv_psram_realloc(void *p, size_t size)
{
    return heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM);
}
