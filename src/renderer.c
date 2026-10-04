#include "renderer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <SDL2/SDL_image.h>

bool renderer_load_image(SceneRenderer *scene, SDL_Renderer *renderer,
                         const char *image_path, const char *image_id,
                         size_t vram_budget) {
    if (scene == NULL || renderer == NULL || image_path == NULL) {
        fprintf(stderr, "renderer_load_image: invalid arguments\n");
        return false;
    }
    renderer_destroy(scene);
    tex_cache_init(&scene->cache, renderer, vram_budget);
    if (!tex_cache_load_source(&scene->cache, image_path)) {
        return false;
    }
    snprintf(scene->image_path, sizeof(scene->image_path), "%s", image_path);
    snprintf(scene->image_id, sizeof(scene->image_id), "%s",
             image_id ? image_id : image_path);
    scene->image_w = scene->cache.source_w;
    scene->image_h = scene->cache.source_h;
    scene->loaded = true;
    return true;
}

GPoint renderer_physical_to_logical(int phys_x, int phys_y,
                                    GSize logical, double dpr, Rotation rot) {
    GAffine inv = g_physical_to_logical(logical, dpr, rot);
    return g_affine_apply(&inv, gpoint((double)phys_x, (double)phys_y));
}

static SDL_Rect sdl_rect_from_g(GRect r) {
    SDL_Rect s;
    s.x = (int)lround(r.x);
    s.y = (int)lround(r.y);
    s.w = (int)lround(r.w);
    s.h = (int)lround(r.h);
    return s;
}

static void draw_debug(SDL_Renderer *rd, const LayoutTransform *t,
                       const LayoutConfig *cfg, GSize logical, double dpr,
                       Rotation rot, GPoint ptr, bool ptr_valid) {
    /* 视口边界（黄） */
    SDL_SetRenderDrawColor(rd, 255, 220, 0, 255);
    SDL_Rect vp = {0, 0, (int)logical.w, (int)logical.h};
    SDL_RenderDrawRect(rd, &vp);

    /* 源图裁切线（以"图在视口里的可见区"即视口框，红色理论裁切框用覆盖标识：
       这里在目标矩形超出视口的边缘画红线，表示被裁掉的部分） */
    SDL_SetRenderDrawColor(rd, 255, 40, 60, 255);
    SDL_Rect target = sdl_rect_from_g(t->target);
    SDL_RenderDrawRect(rd, &target);

    /* 焦点十字（青） */
    GPoint f = g_clamp_focus(cfg->focus_x, cfg->focus_y);
    GPoint fl = g_affine_apply(&t->image_to_logical,
                               gpoint(f.x * cfg->image_w, f.y * cfg->image_h));
    SDL_SetRenderDrawColor(rd, 0, 220, 220, 255);
    SDL_RenderDrawLine(rd, (int)fl.x - 10, (int)fl.y, (int)fl.x + 10, (int)fl.y);
    SDL_RenderDrawLine(rd, (int)fl.x, (int)fl.y - 10, (int)fl.x, (int)fl.y + 10);

    /* 命中点（绿=在图像上，灰=留边区） */
    if (ptr_valid) {
        GPoint imgpt;
        bool hit = layout_hit_test(t, ptr, &imgpt);
        SDL_SetRenderDrawColor(rd, hit ? 0 : 180, hit ? 230 : 180,
                               hit ? 0 : 180, 255);
        SDL_Rect m = {(int)ptr.x - 6, (int)ptr.y - 6, 12, 12};
        SDL_RenderDrawRect(rd, &m);
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "rot=%d dpr=%.2f vp=%dx%d ptr(%.0f,%.0f)->img(%.0f,%.0f) %s",
                 (int)rot, dpr, (int)logical.w, (int)logical.h,
                 ptr.x, ptr.y, imgpt.x, imgpt.y, hit ? "HIT" : "MISS");
        (void)buf; /* 文字需要 SDL_ttf，控制台另打印，见 main */
    }
}

LayoutTransform renderer_draw(SceneRenderer *scene,
                              SDL_Renderer *renderer,
                              int logical_w, int logical_h,
                              double dpr, Rotation rot,
                              const LayoutConfig *cfg,
                              GPoint pointer_logical, bool pointer_valid) {
    GSize logical = gsize((double)logical_w, (double)logical_h);
    GSize physical = gsize(logical.w * dpr, logical.h * dpr);
    (void)physical;

    LayoutConfig local;
    if (cfg == NULL) {
        layout_config_init(&local, scene->image_id, scene->image_w, scene->image_h);
        cfg = &local;
    }

    LayoutTransform t = layout_resolve(cfg, logical);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    if (scene->loaded) {
        /*
         * 旋转的正确做法（避免直接旋转矩形导致的宽高交换失真）：
         *  1) 在"面板方向"离屏表面上按未旋转布局绘制（panel 视口）；
         *     rot=90/270 时面板视口尺寸 = (logical.h, logical.w)。
         *  2) 把离屏表面绕视口中心旋转到主视口。
         * 离屏四角旋转后即 g_logical_to_physical 仿射结果；
         * 触摸点走同一仿射的逆，命中位置与画面严格一致。
         */
        GSize panel = logical;
        if (rot == ROT_90 || rot == ROT_270) {
            panel = gsize(logical.h, logical.w);
        }
        LayoutTransform pt = layout_resolve(cfg, panel);
        int bw = (int)lround(panel.w);
        int bh = (int)lround(panel.h);

        int phys_target_w = (int)lround(pt.target.w * dpr);
        int tex_w = 0, tex_h = 0;
        SDL_Texture *tex = tex_cache_acquire(&scene->cache, phys_target_w,
                                             (int)lround(pt.target.h * dpr),
                                             &tex_w, &tex_h);
        double kx = (double)tex_w / (double)scene->image_w;
        double ky = (double)tex_h / (double)scene->image_h;
        SDL_Rect src_rect;
        if (cfg->mode == LAYOUT_CONTAIN) {
            src_rect = (SDL_Rect){0, 0, tex_w, tex_h};
        } else {
            src_rect = (SDL_Rect){
                (int)lround(pt.crop.x * kx), (int)lround(pt.crop.y * ky),
                (int)lround(pt.crop.w * kx), (int)lround(pt.crop.h * ky)
            };
        }

        SDL_Texture *scene_tex = NULL;
        SDL_Renderer *rd = renderer;
        SDL_Texture *prev_target = SDL_GetRenderTarget(rd);
        if (rot != ROT_0) {
            scene_tex = SDL_CreateTexture(
                rd, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, bw, bh);
            SDL_SetTextureBlendMode(scene_tex, SDL_BLENDMODE_BLEND);
            SDL_SetRenderTarget(rd, scene_tex);
        }
        SDL_SetRenderDrawColor(rd, 0, 0, 0, 255);
        SDL_RenderClear(rd);
        if (tex) {
            SDL_Rect pdst = sdl_rect_from_g(pt.target);
            SDL_RenderCopy(rd, tex, &src_rect, &pdst);
        }
        if (rot != ROT_0) {
            SDL_SetRenderTarget(rd, prev_target);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            /* 离屏面板同尺寸铺满视口地旋转：dst=逻辑视口，轴心=视口中心 */
            SDL_Rect full = {0, 0, (int)lround(logical.w),
                             (int)lround(logical.h)};
            SDL_Point ctr = {(int)lround(logical.w / 2.0),
                             (int)lround(logical.h / 2.0)};
            double angle = rot == ROT_90 ? 90.0 :
                           rot == ROT_180 ? 180.0 : 270.0;
            SDL_RenderCopyEx(renderer, scene_tex, NULL, &full, angle,
                             &ctr, SDL_FLIP_NONE);
            SDL_DestroyTexture(scene_tex);
        }
    }

    if (scene->show_debug) {
        draw_debug(renderer, &t, cfg, logical, dpr, rot,
                   pointer_logical, pointer_valid);
    }

    SDL_RenderPresent(renderer);
    return t;
}

void renderer_destroy(SceneRenderer *scene) {
    if (scene == NULL) return;
    if (scene->loaded) {
        tex_cache_destroy(&scene->cache);
    }
    memset(scene, 0, sizeof(*scene));
}
