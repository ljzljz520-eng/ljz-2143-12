/*
 * geometry.h - 三套坐标系与变换
 *
 * 坐标系统：
 *   1. 源图像坐标 (image)   : 像素坐标，原点在图像左上角，x 向右、y 向下，
 *                            范围 [0, image_w] x [0, image_h]，不受 DPI/旋转影响。
 *   2. 逻辑窗口坐标 (logical): 与设备无关像素 (DIP)。原点在"逻辑视口"左上角，
 *                            宽高为窗口逻辑尺寸 (native_px / dpr)，
 *                            UI 布局、命中测试都以此坐标为准。
 *   3. 物理像素坐标 (physical): SDL 可绘制表面 / framebuffer 像素，
 *                            尺寸 = logical_size * dpr；触摸事件坐标先换算到此。
 *
 * 屏幕旋转 (rotation 0/90/180/270) 描述物理面板相对逻辑视口的旋转：
 * 逻辑点 (x,y) 经 rot_map 映射到物理点，触摸点经 rot_unmap 回到逻辑点。
 * 所有正变换与逆变换必须严格互逆，保证触摸命中与视觉位置一致。
 */
#ifndef GEOMETRY_H
#define GEOMETRY_H

#include <stdbool.h>

typedef struct {
    double x;
    double y;
} GPoint;

typedef struct {
    double w;
    double h;
} GSize;

typedef struct {
    double x;
    double y;
    double w;
    double h;
} GRect;

/* 2x3 仿射：dst = A*src + t（列主） */
typedef struct {
    double a; /* xx */
    double b; /* yx */
    double c; /* xy */
    double d; /* yy */
    double tx;
    double ty;
} GAffine;

typedef enum {
    ROT_0 = 0,
    ROT_90 = 90,
    ROT_180 = 180,
    ROT_270 = 270,
} Rotation;

static inline GPoint gpoint(double x, double y) {
    GPoint p = {x, y};
    return p;
}
static inline GSize gsize(double w, double h) {
    GSize s = {w, h};
    return s;
}
static inline GRect grect(double x, double y, double w, double h) {
    GRect r = {x, y, w, h};
    return r;
}

bool grect_contains(const GRect *r, GPoint p);
GRect grect_intersect(const GRect *a, const GRect *b);
double grect_area(const GRect *r);

GPoint g_affine_apply(const GAffine *m, GPoint p);
GAffine g_affine_invert(const GAffine *m);
GAffine g_affine_compose(const GAffine *outer, const GAffine *inner);

/* 逻辑视口 <-> 物理表面 */
GAffine g_logical_to_physical(GSize logical, double dpr, Rotation rot);
GAffine g_physical_to_logical(GSize logical, double dpr, Rotation rot);

/* 由物理像素尺寸与旋转反推逻辑尺寸 */
GSize g_logical_size(GSize physical, double dpr, Rotation rot);

/* 归一化焦点 [0,1] 裁剪到安全范围 */
GPoint g_clamp_focus(double fx, double fy);

#endif
