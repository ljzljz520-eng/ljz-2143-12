#ifndef REPORTER_H
#define REPORTER_H

#include <stdbool.h>
#include <time.h>

#include "geometry.h"
#include "http_client.h"
#include "layout.h"

/*
 * 布局轮询 + 裁切上报。
 *  - poll：GET /api/layout?since=N，版本变化时输出新 cfg（调用方 commit 配置）。
 *  - report：debounce（快速拖动只上报最终几何），失败静默重试不影响渲染。
 */

typedef struct {
    HttpClient http;
    char device_id[64];
    int last_version;
    int last_image_rev;
    struct timespec last_report;
    int debounce_ms;
    int online;
} Reporter;

void reporter_init(Reporter *r, const char *host, int port,
                   const char *device_id, int debounce_ms);

/* 轮询布局；有新版本返回 true 并填充 cfg（cfg 先由调用方置默认）。 */
bool reporter_poll_layout(Reporter *r, LayoutConfig *cfg);

/* 下载当前图片 PNG（调用方 free）。失败返回 NULL。 */
unsigned char *reporter_fetch_image(Reporter *r, const LayoutConfig *cfg,
                                    int *out_len);

/* 排队/立即发送一次几何上报；内部按 debounce_ms 合并。force=true 立即发送。 */
void reporter_report(Reporter *r, const Geometry *g, const LayoutConfig *cfg,
                     int logical_w, int logical_h, bool force);

/* 退出前 flush。 */
void reporter_flush(Reporter *r, const Geometry *g, const LayoutConfig *cfg,
                    int logical_w, int logical_h);

#endif
