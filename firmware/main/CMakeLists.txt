file(GLOB FONT_SRCS "fonts/*.c")

idf_component_register(
    SRCS "main.c" "app_config.c" "net.c" "feeds.c" "ical.c" "ui.c" ${FONT_SRCS}
    INCLUDE_DIRS "." "fonts"
    REQUIRES rgb_lcd_port esp_wifi esp_netif esp_event nvs_flash esp_http_client
             esp-tls mbedtls json driver esp_driver_uart
)

idf_build_get_property(build_components BUILD_COMPONENTS)
if("lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl COMPONENT_LIB)
elseif("lvgl__lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
endif()

if(lvgl_lib)
    target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
endif()
