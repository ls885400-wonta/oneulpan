file(GLOB FONT_SRCS "fonts/*.c")

idf_component_register(
    SRCS "main.c" "app_config.c" "net.c" "feeds.c" "ical.c" "school.c" "teachers.c" "lunar.c"
         "ui.c" "ui_home.c" "ui_find.c" "ui_pages.c" "ui_settings.c" ${FONT_SRCS}
    INCLUDE_DIRS "." "fonts"
    REQUIRES rgb_lcd_port esp_wifi esp_netif esp_event nvs_flash esp_http_client
             esp-tls mbedtls json driver esp_driver_uart spiffs io_extension
)

idf_build_get_property(build_components BUILD_COMPONENTS)
if("lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl COMPONENT_LIB)
elseif("lvgl__lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
endif()

if(lvgl_lib)
    target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
    # LVGL 메모리를 PSRAM 에서 (lv_psram.h)
    target_include_directories(${lvgl_lib} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${lvgl_lib} PRIVATE
        LV_MEM_CUSTOM_INCLUDE="lv_psram.h"
        LV_MEM_CUSTOM_ALLOC=lv_psram_malloc
        LV_MEM_CUSTOM_FREE=free
        LV_MEM_CUSTOM_REALLOC=lv_psram_realloc)
endif()
