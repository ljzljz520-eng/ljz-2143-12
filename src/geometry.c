#include "geometry.h"

#include <math.h>
#include <string.h>

double geo_clamp(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int geo_iround(double v) {
    if (v >= 0.0) return (int)(v + 0.5);
    return -(int)(-v + 0.5);
}

FitMode fit_mode_from_str(const char *s) {
    if (s == NULL) return FIT_COVER;
    if (strcmp(s, "stretch") == 0) return FIT_STRETCH;
    if (strcmp(s, "contain") == 0) return FIT_CONTAIN;
    return FIT_COVER;
}

const char *fit_mode_to_str(FitMode m) {
    switch (m) {
        case FIT_STRETCH: return "stretch";
        case FIT_CONTAIN: return "contain";
        case FIT_COVER:
        default:          return "cover";
    }
}

static double safe_dim(double v) { return (v > 0.0 && isfinite(v)) ? v : 0.0; }

void geometry_compute(Geometry *g) {
    /* 仅归一化输入；输出字段在本函数内全部显式赋值，不能 memset 整个结构。 */
    if (g->dpr <= 0.0 || !isfinite(g->dpr)) g->dpr = 1.0;
    g->focus_x = geo_clamp(isfinite(g->focus_x) ? g->focus_x : 0.5, 0.0, 1.0);
    g->focus_y = geo_clamp(isfinite(g->focus_y) ? g->focus_y : 0.5, 0.0, 1.0);
    g->rotation = ((g->rotation % 360) + 360) % 360;

    double iw = safe_dim(g->src_w), ih = safe_dim(g->src_h);
    double w  = safe_dim(g->win_w), h  = safe_dim(g->win_h);
    /* <=0 或小于 MIN_USABLE 像素的视口视为退化：不产生缩放除法异常，
     * 仍如实上报尺寸并标记 degenerate（极窄窗口验收）。 */
    if (w < 16.0 || h < 16.0) {
        g->degenerate = 1;
        /* 物理尺寸仍如实给出 */
        int pw = geo_iround(safe_dim(g->win_w) * g->dpr);
        int ph = geo_iround(safe_dim(g->win_h) * g->dpr);
        if (g->rotation == 90 || g->rotation == 270) { g->phys_w = ph; g->phys_h = pw; }
        else { g->phys_w = pw; g->phys_h = ph; }
        g->scale = g->scale_x = g->scale_y = 1.0;
        g->crop = (GeoRect){0, 0, iw, ih};
        return;
    }
    if (iw == 0.0 || ih == 0.0) {
        /* 无图：退化为单倍、无内容 */
        g->degenerate = 1;
        int pw = geo_iround(w * g->dpr), ph = geo_iround(h * g->dpr);
        if (g->rotation == 90 || g->rotation == 270) { g->phys_w = ph; g->phys_h = pw; }
        else { g->phys_w = pw; g->phys_h = ph; }
        g->scale = g->scale_x = g->scale_y = 1.0;
        g->crop = (GeoRect){0, 0, 0, 0};
        return;
    }

    double s = 1.0, tx = 0.0, ty = 0.0, cw, ch;
    double sx = 1.0, sy = 1.0;
    double crop_x = 0.0, crop_y = 0.0, crop_w = iw, crop_h = ih;
    double lt = 0.0, ll = 0.0, lb = 0.0, lr = 0.0;

    if (g->fit == FIT_STRETCH) {
        sx = w / iw;
        sy = h / ih;
        s  = (sx + sy) * 0.5;
        cw = w; ch = h;
    } else if (g->fit == FIT_CONTAIN) {
        s = fmin(w / iw, h / ih);
        cw = iw * s; ch = ih * s;
        /* 内容比视口小：保持焦点相对位置 fx，黑边分给对侧。
         * tx∈[0,w-cw]：fx=0 贴左、fx=1 贴右、0.5 居中。 */
        tx = g->focus_x * (w - cw);
        ty = g->focus_y * (h - ch);
        sx = sy = s;
        ll = tx; lt = ty;
        lr = w - cw - ll; lb = h - ch - lt;
    } else { /* FIT_COVER */
        s = fmax(w / iw, h / ih);
        cw = iw * s; ch = ih * s;
        /* 内容比视口大：同一公式 tx=fx*(w-cw)∈[w-cw,0]，
         * 焦点贴边时裁切对侧，居中时两边均分。 */
        tx = g->focus_x * (w - cw);
        ty = g->focus_y * (h - ch);
        sx = sy = s;
        crop_w = w / s; crop_h = h / s;
        crop_x = -tx / s; crop_y = -ty / s;
    }

    g->scale = s; g->scale_x = sx; g->scale_y = sy;
    g->tx = tx; g->ty = ty;
    g->content_w = cw; g->content_h = ch;
    g->crop = (GeoRect){crop_x, crop_y, crop_w, crop_h};
    g->lb_top = lt; g->lb_left = ll; g->lb_bottom = lb; g->lb_right = lr;

    int pw = geo_iround(w * g->dpr), ph = geo_iround(h * g->dpr);
    if (g->rotation == 90 || g->rotation == 270) { g->phys_w = ph; g->phys_h = pw; }
    else { g->phys_w = pw; g->phys_h = ph; }
}

void geo_src_to_logical(const Geometry *g, double sx_, double sy_,
                        double *lx, double *ly) {
    if (lx) *lx = sx_ * g->scale_x + g->tx;
    if (ly) *ly = sy_ * g->scale_y + g->ty;
}

void geo_logical_to_src(const Geometry *g, double lx, double ly,
                        double *sx_, double *sy_) {
    if (sx_) *sx_ = g->scale_x != 0.0 ? (lx - g->tx) / g->scale_x : lx;
    if (sy_) *sy_ = g->scale_y != 0.0 ? (ly - g->ty) / g->scale_y : ly;
}

void geo_logical_to_phys(const Geometry *g, double lx, double ly,
                         double *px, double *py) {
    double x = lx * g->dpr, y = ly * g->dpr;
    double wp = safe_dim(g->win_w) * g->dpr;
    double hp = safe_dim(g->win_h) * g->dpr;
    /* rot 90/270 时帧缓冲尺寸为 (hp,wp)，映射必须用交换后的范围 */
    switch (g->rotation) {
        case 90:  if (px) *px = hp - y; if (py) *py = x; break;
        case 180: if (px) *px = wp - x; if (py) *py = hp - y; break;
        case 270: if (px) *px = y;      if (py) *py = wp - x; break;
        default:  if (px) *px = x;      if (py) *py = y; break;
    }
}

void geo_phys_to_logical(const Geometry *g, double px, double py,
                         double *lx, double *ly) {
    double wp = safe_dim(g->win_w) * g->dpr;
    double hp = safe_dim(g->win_h) * g->dpr;
    double x, y;
    switch (g->rotation) {
        case 90:  x = py;          y = hp - px; break;
        case 180: x = wp - px;     y = hp - py; break;
        case 270: x = wp - py;     y = px; break;
        default:  x = px;          y = py; break;
    }
    if (lx) *lx = x / g->dpr;
    if (ly) *ly = y / g->dpr;
}

void geo_phys_to_src(const Geometry *g, double px, double py,
                     double *sx_, double *sy_) {
    double lx, ly;
    geo_phys_to_logical(g, px, py, &lx, &ly);
    geo_logical_to_src(g, lx, ly, sx_, sy_);
}

int geo_hit_test(const Geometry *g, double px, double py, const GeoRect *z) {
    if (z == NULL) return 0;
    double sx_, sy_;
    geo_phys_to_src(g, px, py, &sx_, &sy_);
    return sx_ >= z->x && sx_ <= z->x + z->w &&
           sy_ >= z->y && sy_ <= z->y + z->h;
}

void geo_crop_normalized(const Geometry *g, double out[4]) {
    double iw = safe_dim(g->src_w), ih = safe_dim(g->src_h);
    if (iw <= 0.0 || ih <= 0.0) {
        out[0] = out[1] = out[2] = out[3] = 0.0;
        return;
    }
    out[0] = geo_clamp(g->crop.x / iw, 0.0, 1.0);
    out[1] = geo_clamp(g->crop.y / ih, 0.0, 1.0);
    out[2] = geo_clamp(g->crop.w / iw, 0.0, 1.0);
    out[3] = geo_clamp(g->crop.h / ih, 0.0, 1.0);
}
