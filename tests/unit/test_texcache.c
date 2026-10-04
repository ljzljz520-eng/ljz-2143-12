#include "test.h"
#include <SDL2/SDL.h>
#include "texcache.h"

static SDL_Surface *mk_surface(int w, int h) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
                                                    SDL_PIXELFORMAT_ARGB8888);
    SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, 10, 120, 200, 255));
    return s;
}

static void test_runtime_single_texture(void) {
    SDL_Renderer *r = SDL_CreateRenderer(
        SDL_CreateWindow("t", 0, 0, 100, 100, SDL_WINDOW_HIDDEN), -1, 0);
    TexCache c;
    texcache_init(&c, r, TEX_RUNTIME, 64u*1024*1024, 8, 64, 1024);
    SDL_Surface *m = mk_surface(512, 512);
    int w=0,h=0;
    SDL_Texture *t1 = texcache_get(&c, m, 512, 512, 100, 100, &w, &h);
    SDL_Texture *t2 = texcache_get(&c, m, 512, 512, 400, 400, &w, &h);
    CHECK(t1 == t2);            /* runtime 永远只有原图一张 */
    CHECK(c.hits >= 1);
    CHECK(c.bytes == (size_t)512*512*4);
    SDL_FreeSurface(m);
    texcache_destroy(&c);
    SDL_DestroyRenderer(r);
}

static void test_atlas_buckets_and_lru(void) {
    SDL_Renderer *r = SDL_CreateRenderer(
        SDL_CreateWindow("t2", 0, 0, 100, 100, SDL_WINDOW_HIDDEN), -1, 0);
    /* 预算只够 2 个小档位 */
    TexCache c;
    texcache_init(&c, r, TEX_ATLAS, 128u*128*4, 4, 64, 1024);
    SDL_Surface *m = mk_surface(512, 512);
    int w=0,h=0;
    /* 取 4 个不同档位；超出条数/字节预算后 LRU 淘汰 */
    SDL_Texture *a = texcache_get(&c, m, 512,512, 64, 64, &w,&h);
    texcache_pin_begin(&c);
    SDL_Texture *b = texcache_get(&c, m, 512,512, 128,128,&w,&h);
    texcache_pin_begin(&c);
    SDL_Texture *cc = texcache_get(&c, m, 512,512, 256,256,&w,&h);
    (void)a;(void)b;(void)cc;
    CHECK(c.evictions >= 1);    /* 预算被触发过淘汰 */
    CHECK(c.bytes <= 128u*128*4 + 256*256*4);
    /* 统计 hit/miss 有增长 */
    CHECK(c.misses >= 2);
    SDL_FreeSurface(m);
    texcache_destroy(&c);
    SDL_DestroyRenderer(r);
}

static void test_budget_zero_eviction_keeps_pinned(void) {
    SDL_Renderer *r = SDL_CreateRenderer(
        SDL_CreateWindow("t3", 0, 0, 100, 100, SDL_WINDOW_HIDDEN), -1, 0);
    TexCache c;
    texcache_init(&c, r, TEX_ATLAS, 256*256*4, 2, 64, 512);
    SDL_Surface *m = mk_surface(512, 512);
    int w=0,h=0;
    /* 第一个条目不 pin 开始？texcache_get 内部 pin=1；再取第二档时第一个仍 pin，
     * 无法淘汰 -> 字节超预算但不崩溃。 */
    SDL_Texture *a = texcache_get(&c, m, 512,512, 64,64,&w,&h);
    SDL_Texture *b = texcache_get(&c, m, 512,512, 128,128,&w,&h);
    CHECK(a && b);
    texcache_pin_begin(&c); /* 下一轮全部解除 pin，再次触发淘汰 */
    SDL_Texture *d = texcache_get(&c, m, 512,512, 256,256,&w,&h);
    CHECK(d);
    SDL_FreeSurface(m);
    texcache_destroy(&c);
    SDL_DestroyRenderer(r);
}

void test_texcache_all(void) {
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    SDL_Init(SDL_INIT_VIDEO);
    RUN(test_runtime_single_texture);
    RUN(test_atlas_buckets_and_lru);
    RUN(test_budget_zero_eviction_keeps_pinned);
    SDL_Quit();
}
