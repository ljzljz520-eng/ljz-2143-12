#ifndef WINDOW_H
#define WINDOW_H

#include <stdbool.h>
#include <SDL2/SDL.h>

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    /* 逻辑（设备无关）尺寸；物理尺寸 = 逻辑 * dpr（模拟 DPR 时用渲染缩放实现） */
    int logical_w, logical_h;
    double dpr;
    int rotation;          /* 0/90/180/270 */
    int display_index;
    int sim_dpr;           /* 模拟 dpr（0/1=真实） */
    Uint32 sim_event_type; /* SDL_RegisterEvents 分配的脚本事件类型 */
} AppWindow;

bool window_init(AppWindow *app, const char *title,
                 int logical_w, int logical_h, int sim_dpr, int x, int y);
void window_get_physical_size(AppWindow *app, int *pw, int *ph);
void window_apply_rotation(AppWindow *app, int rotation); /* 可能交换逻辑宽高 */
void window_destroy(AppWindow *app);

/* SDL 渲染缩放：模拟高 DPI（drawable 保持物理像素，渲染器按 dpr 缩放）。 */
void window_set_render_scale(AppWindow *app);

#endif
