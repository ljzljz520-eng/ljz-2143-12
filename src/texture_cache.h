/*
 * texture_cache.h - 多尺寸纹理缓存 vs 运行时缩放
 *
 * 策略（混合）：
 *  - 源纹理加载后，按"分档"预生成若干降采样纹理（如 4096/2560/1920/1280/768），
 *    分档按目标逻辑宽*dpr 就近选择（向上取一档，保证不放大模糊）。
 *  - 未命中分档或窗口比例极端时，回退到 RenderCopyEx 运行时缩放（GPU 双线性；
 *    软件渲染器走 SDL 内置缩放），并把该尺寸以"运行时条目"入缓存。
 *  - 显存预算（字节，默认 96 MiB）按 RGBA8888 计算：w*h*4。
 *  - 超预算时按 LRU（last_used 帧序号）淘汰，源纹理常驻（pinned），
 *    pinned 条目永不淘汰；运行时条目优先于预生成分档被淘汰。
 *
 *  两种方案的取舍见 docs/design.md。
 */
#ifndef TEXTURE_CACHE_H
#define TEXTURE_CACHE_H

#include <stdbool.h>
#include <SDL2/SDL.h>

#define TEX_CACHE_MAX_ENTRIES 24
#define TEX_CACHE_LEVEL_COUNT 6

typedef enum {
    TEX_KIND_SOURCE = 0,  /* 原图，常驻 */
    TEX_KIND_PRESET = 1,  /* 预生成分档 */
    TEX_KIND_RUNTIME = 2, /* 运行时缩放生成 */
} TextureKind;

typedef struct {
    int w;
    int h;
    TextureKind kind;
    SDL_Texture *tex;
    unsigned int last_used;
    bool valid;
} TextureEntry;

typedef struct {
    SDL_Renderer *renderer;
    TextureEntry entries[TEX_CACHE_MAX_ENTRIES];
    int level_widths[TEX_CACHE_LEVEL_COUNT];
    int level_count;
    size_t budget_bytes;
    unsigned int frame_clock;
    int source_w;
    int source_h;
} TextureCache;

void tex_cache_init(TextureCache *tc, SDL_Renderer *renderer, size_t budget_bytes);
void tex_cache_set_levels(TextureCache *tc, const int *widths, int count);

/* 加载源纹理（pinned）。失败返回 false */
bool tex_cache_load_source(TextureCache *tc, const char *path);

/*
 * 请求最适合目标像素尺寸的纹理：优先 >= target_w 的最小预生成档；
 * 没有合适分档时从源纹理运行时缩放出一条目（受预算约束）。
 * 返回的纹理调用方不持有。out_w/out_h 为纹理实际像素。
 */
SDL_Texture *tex_cache_acquire(TextureCache *tc, int target_w, int target_h,
                               int *out_w, int *out_h);

size_t tex_cache_used(const TextureCache *tc);
void tex_cache_tick(TextureCache *tc);
void tex_cache_destroy(TextureCache *tc);

/* 便于测试/上报 */
int tex_cache_count_kind(const TextureCache *tc, TextureKind kind);

#endif
