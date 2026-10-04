/*
 * renderer.h - 场景渲染
 *
 * 渲染管线：
 *   源图像(像素) --layout_resolve--> 逻辑窗口矩形(目标矩形)
 *   逻辑窗口 --DPR+旋转--> 物理像素表面
 * 纹理选择按物理目标宽走 TextureCache（预生成分档 / 运行时缩放）。
 */
#ifndef RENDERER_H
#define RENDERER_H

#include <stdbool.h>

#include <SDL2/SDL.h>

#include "geometry.h"
#include "layout.h"
#include "texture_cache.h"

typedef struct {
    TextureCache cache;
    char image_path[512];
    char image_id[128];
    int image_w;
    int image_h;
    bool loaded;
    bool show_debug; /* 显示理论裁切框 / 视口边界 / 焦点 */
} SceneRenderer;

bool renderer_load_image(SceneRenderer *scene, SDL_Renderer *renderer,
                         const char *image_path, const char *image_id,
                         size_t vram_budget);

/*
 * 绘制一帧。
 *   logical_w/h : 逻辑窗口尺寸（DIP）
 *   dpr         : 设备像素比
 *   rot         : 当前应用的屏幕旋转（物理面板方向）
 * 返回当前帧应用的变换，供上报/命中使用（可为 NULL）。
 */
LayoutTransform renderer_draw(SceneRenderer *scene,
                              SDL_Renderer *renderer,
                              int logical_w, int logical_h,
                              double dpr, Rotation rot,
                              const LayoutConfig *cfg,
                              GPoint pointer_logical, bool pointer_valid);

void renderer_destroy(SceneRenderer *scene);

/* 物理触摸/鼠标点 -> 逻辑点（含旋转与 DPI 反算） */
GPoint renderer_physical_to_logical(int phys_x, int phys_y,
                                    GSize logical, double dpr, Rotation rot);

#endif
