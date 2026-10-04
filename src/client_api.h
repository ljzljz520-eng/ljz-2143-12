/* client_api.h - C 展示端与后端的交互封装 */
#ifndef CLIENT_API_H
#define CLIENT_API_H

#include <stdbool.h>

#include "http_client.h"
#include "layout_store.h"

/* GET /api/layout/latest；服务器不可达时 ok=false，调用方可沿用本地配置 */
LayoutApiResult client_fetch_layout(const HttpClient *c);

/*
 * PUT /api/layout 乐观锁保存。
 * base_version 必须等于编辑开始时拿到的版本；服务端已更新则返回
 * status=409（冲突），本函数把 ok=false、server_version 填上，绝不静默覆盖。
 */
LayoutApiResult client_save_layout(const HttpClient *c, const LayoutConfig *cfg,
                                   int base_version);

/* POST /api/reports/crop */
bool client_report_crop(const HttpClient *c, const char *device_id,
                        const LayoutConfig *cfg, const GRect *crop,
                        GSize viewport, double dpr, Rotation rotation);

#endif
