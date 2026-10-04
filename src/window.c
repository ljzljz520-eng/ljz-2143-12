#include "window.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

bool window_init(AppWindow *app, const char *title, int width, int height) {
    if (app == NULL) {
        fprintf(stderr, "window_init: app is NULL\n");
        return false;
    }
    app->window = NULL;
    app->renderer = NULL;
    app->phys_w = width;
    app->phys_h = height;
    app->dpr = 1.0;
    app->rotation = ROT_0;

    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    const char *borderless = getenv("LAYOUT_BORDERLESS");
    if (borderless && borderless[0] == '1') {
        flags |= SDL_WINDOW_BORDERLESS;
    }
    app->window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        flags
    );
    if (app->window == NULL) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    app->renderer = SDL_CreateRenderer(
        app->window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (app->renderer == NULL) {
        app->renderer = SDL_CreateRenderer(app->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (app->renderer == NULL) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        window_destroy(app);
        return false;
    }

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    /* 读真实 DPR（高 DPI 屏）。SDL2 不自动每屏缩放：窗口尺寸是逻辑的，
       drawable 尺寸才是物理的；Xvfb 下恒为 1.0，可用 window_set_dpr 模拟。 */
    int dw = 0, dh = 0;
    SDL_GetRendererOutputSize(app->renderer, &dw, &dh);
    int ww = 0, wh = 0;
    SDL_GetWindowSize(app->window, &ww, &wh);
    if (ww > 0 && wh > 0) {
        double sx = (double)dw / (double)ww;
        double sy = (double)dh / (double)wh;
        app->dpr = sx > 0 ? sx : 1.0;
        (void)sy;
        app->phys_w = dw;
        app->phys_h = dh;
    }
    window_refresh_metrics(app);
    return true;
}

void window_refresh_metrics(AppWindow *app) {
    int dw = 0, dh = 0;
    if (app->renderer) {
        SDL_GetRendererOutputSize(app->renderer, &dw, &dh);
    }
    if (dw > 0 && dh > 0) {
        app->phys_w = dw;
        app->phys_h = dh;
    }
    /*
     * 逻辑视口固定按"用户方向"定义：仅除以 DPR，不做轴交换。
     * 旋转是渲染层（RenderCopyEx）与触摸事件反算层的事，
     * 这样命中测试和视觉位置都在同一逻辑坐标系里闭合。
     */
    app->logical_w = (int)lround((double)app->phys_w / app->dpr);
    app->logical_h = (int)lround((double)app->phys_h / app->dpr);
    if (app->renderer) {
        SDL_RenderSetLogicalSize(app->renderer, app->logical_w, app->logical_h);
    }
}

void window_set_dpr(AppWindow *app, double dpr) {
    if (dpr < 0.5) dpr = 0.5;
    if (dpr > 8.0) dpr = 8.0;
    app->dpr = dpr;
    window_refresh_metrics(app);
}

void window_set_rotation(AppWindow *app, Rotation rot) {
    app->rotation = rot;
    window_refresh_metrics(app);
}

void window_cycle_rotation(AppWindow *app) {
    switch (app->rotation) {
        case ROT_0: app->rotation = ROT_90; break;
        case ROT_90: app->rotation = ROT_180; break;
        case ROT_180: app->rotation = ROT_270; break;
        default: app->rotation = ROT_0; break;
    }
    window_refresh_metrics(app);
}

void window_destroy(AppWindow *app) {
    if (app == NULL) return;
    if (app->renderer != NULL) {
        SDL_DestroyRenderer(app->renderer);
        app->renderer = NULL;
    }
    if (app->window != NULL) {
        SDL_DestroyWindow(app->window);
        app->window = NULL;
    }
}
