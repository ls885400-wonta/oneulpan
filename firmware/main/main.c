/* 오늘판 — ESP32-S3-Touch-LCD-7B 용 사무실 알림판
 * 시계·날씨·급식·교사 시간표·학교 일정·창체 계획·캘린더를 보여 줍니다. */
#include <assert.h>
#include <stdlib.h>

#include "app_config.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_lv_adapter.h"
#include "feeds.h"
#include "net.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "rgb_lcd_port.h"
#include "ui.h"

static const char *TAG = "main";

/* JSON 은 크기가 클 수 있어 PSRAM 에 둡니다 */
static void *json_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

/* 화면 뒤집기 (180도). 바꾸면 다시 켜야 적용됩니다. */
bool screen_flipped(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open("board", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "flip", &v);
        nvs_close(h);
    }
    return v != 0;
}

static void flip_job(void *arg)
{
    nvs_handle_t h;
    if (nvs_open("board", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "flip", screen_flipped() ? 0 : 1);
        nvs_commit(h);
        nvs_close(h);
    }
}

void screen_flip_toggle(void)
{
    flash_run(flip_job, NULL);   /* 화면 단추에서 불림: 플래시 작업에서 씀 */
    esp_restart();
}

static void display_start(void)
{
    const bool flip = screen_flipped();
    const esp_lv_adapter_rotation_t rotation = flip ? ESP_LV_ADAPTER_ROTATE_180 : ESP_LV_ADAPTER_ROTATE_0;
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_touch_handle_t touch = NULL;
    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init(ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB,
                                                    rotation, &panel, &touch));
    wavesahre_rgb_lcd_bl_on();

    esp_lv_adapter_config_t cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    cfg.task_stack_size = 12 * 1024;
    cfg.stack_in_psram = true;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&cfg));

    esp_lv_adapter_display_config_t disp_cfg = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel, NULL, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, rotation);
    disp_cfg.profile.use_psram = true;
    lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);
    assert(disp != NULL);

    if (touch) {
        /* 그림만 돌아가므로 터치 좌표도 같이 뒤집습니다 */
        esp_lcd_touch_set_mirror_x(touch, flip);
        esp_lcd_touch_set_mirror_y(touch, flip);
        esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch);
        esp_lv_adapter_register_touch(&touch_cfg);
    }
    ESP_ERROR_CHECK(esp_lv_adapter_start());
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    cJSON_Hooks hooks = { .malloc_fn = json_malloc, .free_fn = free };
    cJSON_InitHooks(&hooks);

    config_load();
    storage_init();
    config_serial_start();
    ESP_LOGI(TAG, "오늘판 %s 시작 (%s)", APP_VERSION, g_cfg.title);

    display_start();
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        ui_init();
        esp_lv_adapter_unlock();
    }

    teachers_load();
    net_start(g_cfg.ssid, g_cfg.pass);
    feeds_start();
    ESP_LOGI(TAG, "남은 메모리: 내부 %u, PSRAM %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
