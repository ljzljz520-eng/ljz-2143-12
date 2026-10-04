#include "texcache.h"

#include <stdio.h>
#include <stdlib.h>

static size_t pitch_bytes(int w) {
    /* ARGB8888: 4 字节/像素；SDL 的 pitch 通常 4 字节对齐，w 已是像素 */
    return (size_t)w * 4u;
}

size_t texcache_texture_bytes(SDL_Texture *t, int *w, int *h) {
    if (!t) return 0;
    unsigned int pw = 0, ph = 0;
    if (SDL_QueryTexture(t, NULL, NULL, (int *)&pw, (int *)&ph) != 0) return 0;
    if (w) *w = (int)pw;
    if (h) *h = (int)ph;
    return pitch_bytes((int)pw) * ph;
}

void texcache_init(TexCache *c, SDL_Renderer *r, TexStrategy strategy,
                   size_t budget_bytes, int budget_entries,
                   int min_bucket, int max_bucket) {
    c->renderer = r;
    c->head = NULL;
    c->bytes = 0;
    c->budget_bytes = budget_bytes;
    c->budget_entries = budget_entries;
    c->strategy = strategy;
    c->min_bucket = min_bucket > 0 ? min_bucket : 128;
    c->max_bucket = max_bucket > 0 ? max_bucket : 2048;
    c->ticks = 1;
    c->hits = c->misses = c->evictions = 0;
}

void texcache_destroy(TexCache *c) {
    texcache_invalidate_all(c);
}

static TexEntry *find_entry(TexCache *c, int bucket) {
    for (TexEntry *e = c->head; e; e = e->next)
        if (e->bucket == bucket) return e;
    return NULL;
}

static void touch(TexCache *c, TexEntry *e) {
    e->last_used_tick = ++c->ticks;
}

static int entry_count(TexCache *c) {
    int n = 0;
    for (TexEntry *e = c->head; e; e = e->next) n++;
    return n;
}

static void evict_one(TexCache *c) {
    /* 选 LRU 且未 pin 的条目 */
    TexEntry *best = NULL, *prev = NULL, *bestprev = NULL;
    for (TexEntry *e = c->head, *p = NULL; e; p = e, e = e->next) {
        if (e->pin) continue;
        if (!best || e->last_used_tick < best->last_used_tick) {
            best = e;
            bestprev = p;
        }
        prev = p;
    }
    (void)prev;
    if (!best) return;
    if (bestprev) bestprev->next = best->next;
    else c->head = best->next;
    int w = 0, h = 0;
    size_t b = texcache_texture_bytes(best->tex, &w, &h);
    SDL_DestroyTexture(best->tex);
    if (b <= c->bytes) c->bytes -= b; else c->bytes = 0;
    free(best);
    c->evictions++;
}

void texcache_pin_begin(TexCache *c) {
    for (TexEntry *e = c->head; e; e = e->next) e->pin = 0;
}

void texcache_pin_end(TexCache *c) {
    (void)c; /* pin 在 evict 中检查；渲染完成后下轮 begin 时清零 */
}

static void enforce_budget(TexCache *c) {
    while ((c->bytes > c->budget_bytes ||
            entry_count(c) > c->budget_entries)) {
        size_t before = c->bytes;
        evict_one(c);
        if (c->bytes == before) break; /* 全部 pinned，无法淘汰 */
    }
}

static int pick_bucket(TexCache *c, int target) {
    int b = c->min_bucket;
    while (b < target && b < c->max_bucket) b <<= 1;
    return b;
}

/* 用软渲染把 master 缩放到 w×h，再上传为纹理。 */
static SDL_Texture *make_scaled(TexCache *c, SDL_Surface *master, int w, int h) {
    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(
        0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!dst) return NULL;
    SDL_Rect dr = {0, 0, w, h};
    if (SDL_BlitScaled(master, NULL, dst, &dr) != 0) {
        SDL_FreeSurface(dst);
        return NULL;
    }
    SDL_Texture *t = SDL_CreateTextureFromSurface(c->renderer, dst);
    SDL_FreeSurface(dst);
    if (t) SDL_SetTextureScaleMode(t, SDL_ScaleModeBest);
    return t;
}

static TexEntry *add_entry(TexCache *c, SDL_Texture *tex, int bucket) {
    TexEntry *e = calloc(1, sizeof(*e));
    e->tex = tex;
    e->bucket = bucket;
    unsigned int pw = 0, ph = 0;
    SDL_QueryTexture(tex, NULL, NULL, (int *)&pw, (int *)&ph);
    e->w = (int)pw;
    e->h = (int)ph;
    e->last_used_tick = ++c->ticks;
    e->next = c->head;
    c->head = e;
    c->bytes += pitch_bytes(e->w) * (size_t)e->h;
    enforce_budget(c);
    return e;
}

SDL_Texture *texcache_get(TexCache *c, SDL_Surface *master,
                          int src_w, int src_h, int dst_w, int dst_h,
                          int *out_w, int *out_h) {
    (void)src_w; (void)src_h;
    if (c->strategy == TEX_RUNTIME) {
        /* bucket=0 代表原图（运行时缩放） */
        TexEntry *e = find_entry(c, 0);
        if (!e) {
            SDL_Texture *t = SDL_CreateTextureFromSurface(c->renderer, master);
            if (!t) return NULL;
            SDL_SetTextureScaleMode(t, SDL_ScaleModeBest);
            e = add_entry(c, t, 0);
            c->misses++;
        } else {
            touch(c, e);
            c->hits++;
        }
        e->pin = 1;
        if (out_w) *out_w = e->w;
        if (out_h) *out_h = e->h;
        enforce_budget(c);
        return e->tex;
    }

    /* ATLAS：按"目标在 L 中最大边"选档。档位纹理保持源图宽高比缩放到 bucket 内。 */
    int target = dst_w > dst_h ? dst_w : dst_h;
    int bucket = pick_bucket(c, target);
    TexEntry *e = find_entry(c, bucket);
    if (!e) {
        double s = (double)bucket /
                   (master->w > master->h ? master->w : master->h);
        int w = master->w, h = master->h;
        if (s >= 1.0) {
            /* 不放大超过原图：直接用原图档位 */
            w = master->w;
            h = master->h;
            e = find_entry(c, -1); /* -1 表示原图档（atlas 下的天花板） */
            if (e) {
                touch(c, e);
                e->pin = 1;
                c->hits++;
                if (out_w) *out_w = e->w;
                if (out_h) *out_h = e->h;
                return e->tex;
            }
            SDL_Texture *t = SDL_CreateTextureFromSurface(c->renderer, master);
            if (!t) return NULL;
            SDL_SetTextureScaleMode(t, SDL_ScaleModeBest);
            e = add_entry(c, t, -1);
            e->pin = 1;
            c->misses++;
            if (out_w) *out_w = e->w;
            if (out_h) *out_h = e->h;
            return e->tex;
        }
        w = (int)(master->w * s + 0.5);
        h = (int)(master->h * s + 0.5);
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        SDL_Texture *t = make_scaled(c, master, w, h);
        if (!t) return NULL;
        e = add_entry(c, t, bucket);
        c->misses++;
    } else {
        c->hits++;
        touch(c, e);
    }
    e->pin = 1;
    enforce_budget(c);
    if (out_w) *out_w = e->w;
    if (out_h) *out_h = e->h;
    return e->tex;
}

void texcache_invalidate_all(TexCache *c) {
    TexEntry *e = c->head;
    while (e) {
        TexEntry *n = e->next;
        SDL_DestroyTexture(e->tex);
        free(e);
        e = n;
    }
    c->head = NULL;
    c->bytes = 0;
}
