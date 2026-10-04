#ifndef GEOMETRY_H
#define GEOMETRY_H

/*
 * 三套坐标变换的唯一实现：
 *   S 源图像像素 -> L 逻辑窗口点(dp) -> P 物理帧缓冲像素
 * 详见 docs/geometry.md。渲染与命中测试必须共用本头文件的函数。
 */

typedef enum {
    FIT_STRETCH = 0,
    FIT_COVER   = 1,   /* 铺满：裁切溢出 */
    FIT_CONTAIN = 2    /* 完整显示：黑边 */
} FitMode;

typedef struct {
    double x, y, w, h;
} GeoRect;

typedef struct {
    /* 输入 */
    double src_w, src_h;       /* 源图像像素 */
    double win_w, win_h;       /* 逻辑窗口 dp（已按旋转交换） */
    double dpr;                /* 逻辑->物理倍率 */
    int    rotation;           /* 0/90/180/270 顺时针 */
    FitMode fit;
    double focus_x, focus_y;   /* 源图归一化焦点 [0,1] */
    /* 输出：S->L 的仿射参数 l = s*src + t */
    double scale;              /* 统一缩放比（stretch 用 sx/sy） */
    double scale_x, scale_y;   /* stretch 时两者不同 */
    double tx, ty;
    double content_w, content_h; /* 内容在 L 中的绘制尺寸 */
    GeoRect crop;              /* 视口露出的源图像素区域 */
    double lb_top, lb_left, lb_bottom, lb_right; /* contain 黑边（L 单位） */
    int    degenerate;
    /* 物理帧缓冲尺寸（旋转后） */
    int    phys_w, phys_h;
} Geometry;

double geo_clamp(double v, double lo, double hi);

/* 根据输入填充 geometry；非法输入安全回退（degenerate=1）。 */
void geometry_compute(Geometry *g);

/* 坐标互转（所有方向成对出现，渲染与命中必须走这里） */
void geo_src_to_logical(const Geometry *g, double sx, double sy, double *lx, double *ly);
void geo_logical_to_src(const Geometry *g, double lx, double ly, double *sx, double *sy);
void geo_logical_to_phys(const Geometry *g, double lx, double ly, double *px, double *py);
void geo_phys_to_logical(const Geometry *g, double px, double py, double *lx, double *ly);
void geo_phys_to_src(const Geometry *g, double px, double py, double *sx, double *sy);

/* 命中测试：物理触摸点是否落在源坐标热区内。返回 1 命中。 */
int geo_hit_test(const Geometry *g, double px, double py, const GeoRect *src_hotzone);

/* round-half-up（正数即 floor(v+0.5)），几何 -> SDL 整数像素统一走这里 */
int geo_iround(double v);

/* 源像素裁切框 -> 归一化 [0,1] 报告 */
void geo_crop_normalized(const Geometry *g, double out[4]);

FitMode fit_mode_from_str(const char *s);
const char *fit_mode_to_str(FitMode m);

#endif
