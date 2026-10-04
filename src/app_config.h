/* app_config.h - 运行参数（环境变量） */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stddef.h>

typedef struct {
    char host[128];
    char port[16];
    char device_id[64];
    char default_image[512];
    size_t vram_budget;
    int http_timeout_ms;
} AppConfig;

void app_config_load(AppConfig *c);

#endif
