#include "texture_cache.h"

#include <stdio.h>
#include <string.h>

#include <SDL2/SDL_image.h>

static size_t bytes_of(int w, int h) {
    return (size_t)w * (size_t)h * 4u;
}

void tex_cache_init(TextureCache *tc, SDL_Renderer *renderer, size_t budget_bytes) {
    memset(tc, 0, sizeof(*tc));
    tc->renderer = renderer;
    tc->budget_bytes = budget_bytes ? budget_bytes : 96u * 1024u * 1024u;
    static const int default_levels[TEX_CACHE_LEVEL_COUNT] = {
        4096, 2560, 1920, 1280, 960, 640
    };
    memcpy(tc->level_widths, default_levels, sizeof(default_levels));
    tc->level_count = TEX_CACHE_LEVEL_COUNT;
}

void tex_cache_set_levels(TextureCache *tc, const int *widths, int count) {
    if (count > TEX_CACHE_LEVEL_COUNT) count = TEX_CACHE_LEVEL_COUNT;
    for (int i = 0; i < count; i++) tc->level_widths[i] = widths[i];
    tc->level_count = count;
}

static TextureEntry *find_slot(TextureCache *tc) {
    for (int i = 0; i < TEX_CACHE_MAX_ENTRIES; i++) {
        if (!tc->entries[i].valid) return &tc->entries[i];
    }
    return NULL;
}

static TextureEntry *find_size(TextureCache *tc, int w, int h) {
    for (int i = 0; i < TEX_CACHE_MAX_ENTRIES; i++) {
        if (tc->entries[i].valid && tc->entries[i].w == w &&
            tc->entries[i].h == h) {
            return &tc->entries[i];
        }
    }
    return NULL;
}

void evict_one(TextureCache *tc, bool runtime_only) {
    int victim = -1;
    unsigned int oldest = 0;
    for (int i = 0; i < TEX_CACHE_MAX_ENTRIES; i++) {
        TextureEntry *e = &tc->entries[i];
        if (!e->valid || e->kind == TEX_KIND_SOURCE) continue;
        if (runtime_only && e->kind != TEX_KIND_RUNTIME) continue;
        if (victim < 0 || e->last_used < oldest) {
            victim = i;
            oldest = e->last_used;
        }
    }
    if (victim < 0 && runtime_only) {
        evict_one(tc, false);
        return;
    }
    if (victim >= 0) {
        SDL_DestroyTexture(tc->entries[victim].tex);
        memset(&tc->entries[victim], 0, sizeof(TextureEntry));
    }
}

static void make_room(TextureCache *tc, size_t need) {
    /* 运行时条目先淘汰；不够再淘汰预生成档；源纹理始终保留 */
    evict_one(tc, true);
    while (tex_cache_used(tc) + need > tc->budget_bytes) {
        int before = -1;
        for (int i = 0; i < TEX_CACHE_MAX_ENTRIES; i++) {
            if (tc->entries[i].valid && tc->entries[i].kind != TEX_KIND_SOURCE) {
                before = i;
                break;
            }
        }
        if (before < 0) break;
        evict_one(tc, false);
    }
}

static SDL_Texture *scale_from(TextureCache *tc, SDL_Texture *src,
                               int sw, int sh, int dw, int dh,
                               TextureKind kind) {
    size_t need = bytes_of(dw, dh);
    if (need > tc->budget_bytes) {
        /* 单条超预算：不入缓存，直接返回临时纹理由调用方……这里返回 NULL，
         * 调用方回退 RenderCopyEx 直接缩放源纹理 */
        return NULL;
    }
    make_room(tc, need);
    SDL_Texture *dst = SDL_CreateTexture(
        tc->renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET, dw, dh);
    if (!dst) return NULL;
    SDL_Texture *prev = SDL_GetRenderTarget(tc->renderer);
    if (SDL_SetRenderTarget(tc->renderer, dst) != 0) {
        SDL_DestroyTexture(dst);
        return NULL;
    }
    SDL_Rect d = {0, 0, dw, dh};
    SDL_RenderClear(tc->renderer);
    if (SDL_RenderCopy(tc->renderer, src, NULL, &d) != 0) {
        SDL_SetRenderTarget(tc->renderer, prev);
        SDL_DestroyTexture(dst);
        return NULL;
    }
    SDL_SetRenderTarget(tc->renderer, prev);

    TextureEntry *slot = find_slot(tc);
    if (!slot) {
        evict_one(tc, true);
        slot = find_slot(tc);
    }
    if (!slot) {
        SDL_DestroyTexture(dst);
        return NULL;
    }
    slot->tex = dst;
    slot->w = dw;
    slot->h = dh;
    slot->kind = kind;
    slot->last_used = tc->frame_clock;
    slot->valid = true;
    (void)sw; (void)sh;
    return dst;
}

bool tex_cache_load_source(TextureCache *tc, const char *path) {
    SDL_Texture *src = IMG_LoadTexture(tc->renderer, path);
    if (!src) {
        fprintf(stderr, "load source texture failed: %s\n", IMG_GetError());
        return false;
    }
    int w = 0, h = 0;
    SDL_QueryTexture(src, NULL, NULL, &w, &h);
    tc->source_w = w;
    tc->source_h = h;
    TextureEntry *e = &tc->entries[0];
    e->tex = src;
    e->w = w;
    e->h = h;
    e->kind = TEX_KIND_SOURCE;
    e->last_used = tc->frame_clock;
    e->valid = true;

    /* 预生成分档：仅生成不大于原图的档，按预算逐个生成 */
    for (int i = 0; i < tc->level_count; i++) {
        int lw = tc->level_widths[i];
        if (lw >= w) continue;
        int lh = (int)((long long)h * lw / w);
        if (lh <= 0) continue;
        if (find_size(tc, lw, lh)) continue;
        size_t need = bytes_of(lw, lh);
        if (tex_cache_used(tc) + need > tc->budget_bytes) break;
        if (!scale_from(tc, src, w, h, lw, lh, TEX_KIND_PRESET)) break;
    }
    return true;
}

SDL_Texture *tex_cache_acquire(TextureCache *tc, int target_w, int target_h,
                               int *out_w, int *out_h) {
    tc->frame_clock++;
    TextureEntry *src = &tc->entries[0];
    if (!src->valid) return NULL;

    /* 目标超过原图：直接用源纹理运行时放大（避免缓存巨大纹理） */
    if (target_w >= src->w) {
        src->last_used = tc->frame_clock;
        if (out_w) *out_w = src->w;
        if (out_h) *out_h = src->h;
        return src->tex;
    }

    /* 选 >= target_w 的最小预生成档 */
    int best = -1;
    for (int i = 0; i < tc->level_count; i++) {
        int lw = tc->level_widths[i];
        if (lw < target_w) continue;
        if (lw > src->w) continue;
        if (best < 0 || lw < tc->level_widths[best]) best = i;
    }
    if (best >= 0) {
        int lw = tc->level_widths[best];
        int lh = (int)((long long)src->h * lw / src->w);
        TextureEntry *e = find_size(tc, lw, lh);
        if (e) {
            e->last_used = tc->frame_clock;
            if (out_w) *out_w = e->w;
            if (out_h) *out_h = e->h;
            return e->tex;
        }
    }

    /* 极端比例/无分档：运行时按目标宽生成条目（宽高比跟源图一致） */
    int dw = target_w;
    int dh = (int)((long long)src->h * dw / src->w);
    TextureEntry *e = find_size(tc, dw, dh);
    if (!e) {
        SDL_Texture *t = scale_from(tc, src->tex, src->w, src->h, dw, dh,
                                    TEX_KIND_RUNTIME);
        if (!t) {
            /* 预算放不下：直接用源纹理，由 GPU/SDL 运行时缩放 */
            src->last_used = tc->frame_clock;
            if (out_w) *out_w = src->w;
            if (out_h) *out_h = src->h;
            return src->tex;
        }
        e = find_size(tc, dw, dh);
    }
    if (e) {
        e->last_used = tc->frame_clock;
        if (out_w) *out_w = e->w;
        if (out_h) *out_h = e->h;
        return e->tex;
    }
    (void)target_h;
    src->last_used = tc->frame_clock;
    if (out_w) *out_w = src->w;
    if (out_h) *out_h = src->h;
    return src->tex;
}

size_t tex_cache_used(const TextureCache *tc) {
    size_t total = 0;
    for (int i = 0; i < TEX_CACHE_MAX_ENTRIES; i++) {
        if (tc->entries[i].valid) {
            total += bytes_of(tc->entries[i].w, tc->entries[i].h);
        }
    }
    return total;
}

void tex_cache_tick(TextureCache *tc) {
    /* 预算在生成时即约束；tick 预留给定时/低水位回收，可按需要调用 */
    (void)tc;
}

int tex_cache_count_kind(const TextureCache *tc, TextureKind kind) {
    int n = 0;
    for (int i = 0; i < TEX_CACHE_MAX_ENTRIES; i++) {
        if (tc->entries[i].valid && tc->entries[i].kind == kind) n++;
    }
    return n;
}

void tex_cache_destroy(TextureCache *tc) {
    for (int i = 0; i < TEX_CACHE_MAX_ENTRIES; i++) {
        if (tc->entries[i].valid && tc->entries[i].tex) {
            SDL_DestroyTexture(tc->entries[i].tex);
        }
    }
    memset(tc, 0, sizeof(*tc));
}
