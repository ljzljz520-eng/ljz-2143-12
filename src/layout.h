#ifndef LAYOUT_H
#define LAYOUT_H

#include <stdbool.h>

#include "json_mini.h"
#include "texstrategy.h"

typedef struct {
    char layout_id[64];
    int  version;          /* 乐观锁版本 */
    int  image_rev;
    char image_url[512];
    char fit[16];          /* cover|contain|stretch */
    double focus_x, focus_y;
    int  orientation;      /* 0 | 90 | 180 | 270 | -1=auto */
    char texture_strategy[16]; /* atlas|runtime */
    int  budget_bytes_mb;
    int  budget_entries;
    int  min_bucket, max_bucket;
    unsigned int bg_rgba;  /* letterbox 底色 */
} LayoutConfig;

void layout_set_defaults(LayoutConfig *c);

/*
 * 解析后端 GET /api/layout 的响应 JSON。
 * 期望形如 {"data": {...}, "version": n}；也兼容直接给对象。
 * 字段缺失时保留 cfg 中已有默认值。返回 false 表示格式非法。
 */
bool layout_parse(LayoutConfig *cfg, const char *json_body, char *err, int err_sz);

TexStrategy layout_strategy(const LayoutConfig *c);

#endif
