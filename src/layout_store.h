/* layout_store.h - 布局配置 <-> JSON，含后端版本校验字段 */
#ifndef LAYOUT_STORE_H
#define LAYOUT_STORE_H

#include <stdbool.h>

#include "json.h"
#include "layout.h"

#define LAYOUT_API_ERR_SZ 160

typedef struct {
    int version;
    char updated_at[32];
    char checksum[64];
    LayoutConfig config;
} LayoutRecord;

typedef struct {
    int status;            /* HTTP 状态码 */
    bool ok;
    int server_version;    /* 响应中的 version（用于乐观锁冲突判定） */
    char error[LAYOUT_API_ERR_SZ];
    LayoutRecord record;
} LayoutApiResult;

void layout_record_init(LayoutRecord *r);

/* 从 JSON 对象解析；任何关键字段非法返回 false 并填写 err */
bool layout_record_from_json(const JsonValue *root, LayoutRecord *out,
                             char *err, size_t errsz);
bool layout_config_from_json(const JsonValue *cfgj, LayoutConfig *out,
                             char *err, size_t errsz);

/* 生成 PUT /api/layout 请求体：{"base_version":N,"config":{...},"client":"..."} */
char *layout_save_body(const LayoutConfig *cfg, int base_version, const char *client);

/* 生成裁切上报体 */
char *layout_report_body(const char *device_id, const LayoutConfig *cfg,
                         const GRect *crop, GSize viewport, double dpr,
                         Rotation applied_rotation);

#endif
