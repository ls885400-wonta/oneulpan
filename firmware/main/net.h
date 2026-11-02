#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/* 와이파이에 연결하고 인터넷 시간(한국 시간)을 맞춥니다. */
void net_start(const char *ssid, const char *pass);
bool net_connected(void);
bool net_time_ok(void);
const char *net_ip(void);
/* 주변 와이파이를 찾아 ui_wifi_scanned 로 알려 줍니다. */
void net_scan(void);

/* 주소의 내용을 통째로 받아 옵니다. 받은 글자는 free() 로 지워야 합니다. */
char *http_get_text(const char *url, size_t max_len);

/* 주소의 내용을 한 줄씩 cb 로 넘깁니다 (긴 파일용). cb 가 false 를 돌려주면 멈춥니다. */
typedef bool (*http_line_cb_t)(const char *line, void *ctx);
esp_err_t http_get_lines(const char *url, http_line_cb_t cb, void *ctx);
