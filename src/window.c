#include "window.h"

#include <stdio.h>

bool window_init(AppWindow *app, const char *title,
                 int logical_w, int logical_h, int sim_dpr, int x, int y) {
    app->window = NULL;
    app->renderer = NULL;
    app->logical_w = logical_w;
    app->logical_h = logical_h;
    app->dpr = 1.0;
    app->rotation = 0;
    app->display_index = 0;
    app->sim_dpr = sim_dpr;
    app->sim_event_type = SDL_RegisterEvents(1);

    /* 窗口在屏幕上占用的物理尺寸：模拟 DPR 时创建为 逻辑*dpr */
    int pw = logical_w, ph = logical_h;
    if (sim_dpr > 1) { pw = logical_w * sim_dpr; ph = logical_h * sim_dpr; app->dpr = sim_dpr; }
    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (x >= 0 && y >= 0)
        app->window = SDL_CreateWindow(title, x, y, pw, ph, flags);
    else
        app->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, pw, ph, flags);
    if (!app->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    int idx = SDL_GetWindowDisplayIndex(app->window);
    app->display_index = idx >= 0 ? idx : 0;
    if (sim_dpr <= 1) {
        float vd = 1.0f;
        if (SDL_GetDisplayDPI(app->display_index, NULL, NULL, &vd) == 0 && vd > 1.0f)
            app->dpr = (double)vd; /* 真实 DPI 仅记录；Xvfb 下通常 96 */
    }

    app->renderer = SDL_CreateRenderer(
        app->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!app->renderer)
        app->renderer = SDL_CreateRenderer(app->window, -1, SDL_RENDERER_SOFTWARE);
    if (!app->renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        window_destroy(app);
        return false;
    }
    window_set_render_scale(app);
    return true;
}

void window_set_render_scale(AppWindow *app) {
    if (app->sim_dpr > 1) {
        /* drawable 是逻辑*dpr；让上层以"逻辑尺寸+dpr"直接画物理像素，
           故 scale 固定 1，几何里 dpr 承担倍率。这里仅记录。 */
        SDL_RenderSetLogicalSize(app->renderer, 0, 0);
        SDL_RenderSetScale(app->renderer, 1.0f, 1.0f);
    } else {
        SDL_RenderSetScale(app->renderer, 1.0f, 1.0f);
    }
}

void window_get_physical_size(AppWindow *app, int *pw, int *ph) {
    int w, h;
    SDL_GetWindowSize(app->window, &w, &h);
    /* 模拟 DPR 下窗口像素即物理；真实 X11 dpr=1 */
    if (pw) *pw = w;
    if (ph) *ph = h;
}

void window_apply_rotation(AppWindow *app, int rotation) {
    /* 面板（窗口/物理屏）尺寸不随旋转改变；旋转只改变 UI 逻辑视口到面板的
     * 映射（90/270 时逻辑视口宽高互换，由 geometry 层完成）。 */
    app->rotation = ((rotation % 360) + 360) % 360;
}

void window_destroy(AppWindow *app) {
    if (!app) return;
    if (app->renderer) { SDL_DestroyRenderer(app->renderer); app->renderer = NULL; }
    if (app->window) { SDL_DestroyWindow(app->window); app->window = NULL; }
}
