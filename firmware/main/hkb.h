#pragma once

#include "lvgl.h"

/* 한글 자판 (두벌식). 한 번에 하나만 띄웁니다.
 * on_ok 는 확인 글쇠를 누르면 불립니다. 자판을 지우면 저절로 잊습니다. */
lv_obj_t *hkb_create(lv_obj_t *parent, lv_event_cb_t on_ok);
/* 글자를 넣을 상자 (쓰던 글자는 굳히고 바꿈) */
void hkb_set_textarea(lv_obj_t *ta);
/* 쓰던 글자 굳히기 (상자 글자를 바깥에서 바꿀 때) */
void hkb_commit(void);
void hkb_forget(void);
