#ifndef WINDOW_H
#define WINDOW_H

#include <stdbool.h>

#include <SDL2/SDL.h>

#include "geometry.h"

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    /* 物理像素（framebuffer） */
    int phys_w;
    int phys_h;
    /* 逻辑像素 DIP（= 物理 / dpr，并按旋转交换） */
    int logical_w;
    int logical_h;
    double dpr;
    Rotation rotation;
} AppWindow;

bool window_init(AppWindow *app, const char *title, int width, int height);

/* 根据当前物理尺寸、dpr、旋转刷新 logical 尺寸与 SDL 逻辑视口 */
void window_refresh_metrics(AppWindow *app);

/* 调试用：模拟跨显示器 DPI 变化 / 横竖屏切换 */
void window_set_dpr(AppWindow *app, double dpr);
void window_set_rotation(AppWindow *app, Rotation rot);
void window_cycle_rotation(AppWindow *app);

void window_destroy(AppWindow *app);

#endif
