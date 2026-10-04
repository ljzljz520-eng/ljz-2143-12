#ifndef TEXCACHE_H
#define TEXCACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <SDL2/SDL.h>

#include "texstrategy.h"

/*
 * 纹理缓存：预生成多尺寸纹理 vs 运行时缩放。
 *
 * atlas/runtime 两种策略（由后端配置 texture_strategy）：
 *  - "atlas"：为每张图预生成 2 的幂次档位纹理，命中即用、零运行时缩放；
 *             代价是显存（预算内 LRU 淘汰）。
 *  - "runtime"：只保留原图一张纹理，每帧 GPU 缩放；显存最省，缩放成本在每帧。
 *
 * 预算：bytes <= budget_bytes，且条数 <= budget_entries；超预算按 LRU 淘汰
 * （当前正在使用的条目标记 pinned，本轮渲染结束前不淘汰）。
 */

typedef struct TexEntry {
    SDL_Texture *tex;
    int w, h;
    int bucket;                 /* 2 的幂档（边长），runtime 策略为 0 */
    uint64_t last_used_tick;
    int pin;
    struct TexEntry *next;
} TexEntry;

typedef struct {
    SDL_Renderer *renderer;
    TexEntry *head;             /* LRU：头=最近使用 */
    size_t bytes;
    size_t budget_bytes;
    int budget_entries;
    TexStrategy strategy;
    int min_bucket;             /* 例如 128 */
    int max_bucket;             /* 例如 2048 */
    uint64_t ticks;
    /* 统计 */
    uint64_t hits, misses, evictions;
} TexCache;

void texcache_init(TexCache *c, SDL_Renderer *r, TexStrategy strategy,
                   size_t budget_bytes, int budget_entries,
                   int min_bucket, int max_bucket);
void texcache_destroy(TexCache *c);

void texcache_pin_begin(TexCache *c);
void texcache_pin_end(TexCache *c);

/*
 * 取得用于把 src_w×src_h 的图绘制到目标 logical 尺寸 dst_w×dst_h 的纹理：
 *  - atlas：选 >= 目标边长的最小 2 幂档，缺失则用 master 缩放到该档生成；
 *  - runtime：直接返回 master（按需由调用方缩放到目标）。
 * 返回的纹理尺寸写回 out_w/out_h；调用方用它做 src/dst 映射。
 * master 必须是 ARGB8888 的 SDL_Surface（函数内部不再转换）。
 */
SDL_Texture *texcache_get(TexCache *c, SDL_Surface *master,
                          int src_w, int src_h, int dst_w, int dst_h,
                          int *out_w, int *out_h);

/* 清空某张图相关的全部档位（发布新图 image_rev 变化时调用）。 */
void texcache_invalidate_all(TexCache *c);

size_t texcache_texture_bytes(SDL_Texture *t, int *w, int *h);

#endif
