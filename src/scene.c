#include "scene.h"

#include <stdio.h>

#include "image_png.h"
#include "rotate_blit.h"

void scene_init(Scene *s, SDL_Renderer *r, const LayoutConfig *cfg) {
    s->master = NULL;
    s->master_rev = -1;
    s->logical = s->rotated = NULL;
    s->logical_w = s->logical_h = s->rotated_w = s->rotated_h = 0;
    texcache_init(&s->cache, r, layout_strategy(cfg),
                  (size_t)cfg->budget_bytes_mb * 1024u * 1024u,
                  cfg->budget_entries, cfg->min_bucket, cfg->max_bucket);
}

void scene_destroy(Scene *s) {
    texcache_destroy(&s->cache);
    if (s->logical) SDL_DestroyTexture(s->logical);
    if (s->rotated) SDL_DestroyTexture(s->rotated);
    if (s->master) SDL_FreeSurface(s->master);
    s->logical = s->rotated = NULL;
    s->master = NULL;
}

int scene_set_image(Scene *s, SDL_Renderer *r, const LayoutConfig *cfg,
                    const unsigned char *png, int len) {
    SDL_Surface *m = png_load_from_memory(png, len);
    if (!m) return -1;
    if (s->master) SDL_FreeSurface(s->master);
    s->master = m;
    s->master_rev = cfg->image_rev;
    texcache_invalidate_all(&s->cache);
    s->cache.strategy = layout_strategy(cfg);
    s->cache.budget_bytes = (size_t)cfg->budget_bytes_mb * 1024u * 1024u;
    s->cache.budget_entries = cfg->budget_entries;
    s->cache.min_bucket = cfg->min_bucket;
    s->cache.max_bucket = cfg->max_bucket;
    (void)r;
    return 0;
}

static SDL_Rect lrect(double x, double y, double w, double h) {
    SDL_Rect rc;
    rc.x = geo_iround(x);
    rc.y = geo_iround(y);
    rc.w = geo_iround(w);
    rc.h = geo_iround(h);
    if (rc.w < 1) rc.w = 1;
    if (rc.h < 1) rc.h = 1;
    return rc;
}

static void draw_logical(Scene *s, SDL_Renderer *r, const Geometry *g,
                         const GeoRect *hotzones, int n_hotzones, int overlay) {
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    if (s->master) {
        int tw = 0, th = 0;
        texcache_pin_begin(&s->cache);
        SDL_Texture *t = texcache_get(&s->cache, s->master,
                                      s->master->w, s->master->h,
                                      geo_iround(g->content_w),
                                      geo_iround(g->content_h), &tw, &th);
        if (t) {
            SDL_Rect dst = lrect(g->tx, g->ty, g->content_w, g->content_h);
            SDL_RenderCopy(r, t, NULL, &dst);
        }
        texcache_pin_end(&s->cache);
    }
    if (!overlay) return;
    /* 视口白框 */
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_Rect vp = lrect(0, 0, g->win_w, g->win_h);
    SDL_RenderDrawRect(r, &vp);
    /* 焦点红十字 */
    double flx, fly;
    geo_src_to_logical(g, g->focus_x * g->src_w, g->focus_y * g->src_h,
                       &flx, &fly);
    SDL_SetRenderDrawColor(r, 255, 0, 0, 255);
    int fx = geo_iround(flx), fy = geo_iround(fly), R = 12;
    SDL_RenderDrawLine(r, fx - R, fy, fx + R, fy);
    SDL_RenderDrawLine(r, fx, fy - R, fx, fy + R);
    /* 理论裁切框（绿） */
    SDL_SetRenderDrawColor(r, 0, 255, 0, 255);
    double x1, y1, x2, y2;
    geo_src_to_logical(g, g->crop.x, g->crop.y, &x1, &y1);
    geo_src_to_logical(g, g->crop.x + g->crop.w, g->crop.y + g->crop.h, &x2, &y2);
    if (x2 > x1 && y2 > y1) {
        SDL_Rect crc = lrect(x1, y1, x2 - x1, y2 - y1);
        SDL_RenderDrawRect(r, &crc);
    }
    /* 热区品红 */
    SDL_SetRenderDrawColor(r, 255, 0, 255, 255);
    for (int i = 0; i < n_hotzones; i++) {
        double a_lx, a_ly, b_lx, b_ly;
        geo_src_to_logical(g, hotzones[i].x, hotzones[i].y, &a_lx, &a_ly);
        geo_src_to_logical(g, hotzones[i].x + hotzones[i].w,
                           hotzones[i].y + hotzones[i].h, &b_lx, &b_ly);
        if (b_lx > a_lx && b_ly > a_ly) {
            SDL_Rect hr = lrect(a_lx, a_ly, b_lx - a_lx, b_ly - a_ly);
            SDL_RenderDrawRect(r, &hr);
        }
    }
}

void scene_render(Scene *s, SDL_Renderer *r, const Geometry *g,
                  const GeoRect *hotzones, int n_hotzones, int show_overlay) {
    int lw = geo_iround(g->win_w), lh = geo_iround(g->win_h);
    if (lw <= 0 || lh <= 0) {
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        return;
    }
    int pw = geo_iround(g->win_w * g->dpr);
    int ph = geo_iround(g->win_h * g->dpr);
    int rot_swap = (g->rotation == 90 || g->rotation == 270);

    if (!s->logical || s->logical_w != pw || s->logical_h != ph) {
        if (s->logical) SDL_DestroyTexture(s->logical);
        s->logical = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_TARGET, pw, ph);
        s->logical_w = pw; s->logical_h = ph;
    }
    /* 1) 逻辑视口绘制到 logical target */
    SDL_SetRenderTarget(r, s->logical);
    SDL_RenderSetScale(r, (float)g->dpr, (float)g->dpr);
    draw_logical(s, r, g, hotzones, n_hotzones, show_overlay);
    SDL_RenderSetScale(r, 1.0f, 1.0f);
    /* 先把渲染目标切回窗口，再清窗口帧缓冲 */
    SDL_SetRenderTarget(r, NULL);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);

    if (rot_swap) {
        int ow = ph, oh = pw; /* 旋转后物理尺寸 */
        if (!s->rotated || s->rotated_w != ow || s->rotated_h != oh) {
            if (s->rotated) SDL_DestroyTexture(s->rotated);
            s->rotated = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_STREAMING, ow, oh);
            s->rotated_w = ow; s->rotated_h = oh;
        }
        if (s->rotated &&
            rotate_blit_90_270(r, s->logical, pw, ph, s->rotated,
                               g->rotation) == 0) {
            SDL_Rect full = {0, 0, g->phys_w, g->phys_h};
            SDL_RenderCopy(r, s->rotated, NULL, &full);
        }
    } else {
        SDL_Rect full = {0, 0, g->phys_w, g->phys_h};
        SDL_RenderCopy(r, s->logical, NULL, &full);
    }
}

const GeoRect *scene_pick(Scene *s, const Geometry *g, double px, double py,
                          const GeoRect *hotzones, int n) {
    (void)s;
    for (int i = 0; i < n; i++)
        if (geo_hit_test(g, px, py, &hotzones[i])) return &hotzones[i];
    return NULL;
}
