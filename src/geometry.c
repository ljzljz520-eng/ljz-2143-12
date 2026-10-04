#include "geometry.h"

#include <math.h>
#include <stddef.h>

bool grect_contains(const GRect *r, GPoint p) {
    return r != NULL && p.x >= r->x && p.x <= r->x + r->w &&
           p.y >= r->y && p.y <= r->y + r->h;
}

GRect grect_intersect(const GRect *a, const GRect *b) {
    double x0 = fmax(a->x, b->x);
    double y0 = fmax(a->y, b->y);
    double x1 = fmin(a->x + a->w, b->x + b->w);
    double y1 = fmin(a->y + a->h, b->y + b->h);
    GRect r = grect(x0, y0, fmax(0.0, x1 - x0), fmax(0.0, y1 - y0));
    return r;
}

double grect_area(const GRect *r) {
    return r->w * r->h;
}

GPoint g_affine_apply(const GAffine *m, GPoint p) {
    return gpoint(m->a * p.x + m->c * p.y + m->tx,
                  m->b * p.x + m->d * p.y + m->ty);
}

GAffine g_affine_invert(const GAffine *m) {
    double det = m->a * m->d - m->b * m->c;
    GAffine inv = {0};
    if (det == 0.0) {
        return inv;
    }
    double id = 1.0 / det;
    inv.a = m->d * id;
    inv.b = -m->b * id;
    inv.c = -m->c * id;
    inv.d = m->a * id;
    inv.tx = -(inv.a * m->tx + inv.c * m->ty);
    inv.ty = -(inv.b * m->tx + inv.d * m->ty);
    return inv;
}

GAffine g_affine_compose(const GAffine *outer, const GAffine *inner) {
    /* outer(inner(p)) */
    GAffine r;
    r.a = outer->a * inner->a + outer->c * inner->b;
    r.b = outer->b * inner->a + outer->d * inner->b;
    r.c = outer->a * inner->c + outer->c * inner->d;
    r.d = outer->b * inner->c + outer->d * inner->d;
    r.tx = outer->a * inner->tx + outer->c * inner->ty + outer->tx;
    r.ty = outer->b * inner->tx + outer->d * inner->ty + outer->ty;
    return r;
}

/*
 * 旋转映射定义（逻辑 -> 物理），物理表面是旋转后的面板：
 *   0   : (x, y)
 *   90  : (Hl - y, x)       物理宽 = 逻辑高, 物理高 = 逻辑宽
 *   180 : (Wl - x, Hl - y)
 *   270 : (y, Wl - x)
 * 再乘以 dpr 得到物理像素。
 */
GAffine g_logical_to_physical(GSize logical, double dpr, Rotation rot) {
    GAffine r;
    switch (rot) {
        case ROT_90:
            r = (GAffine){0, dpr, -dpr, 0, dpr * logical.h, 0};
            break;
        case ROT_180:
            r = (GAffine){-dpr, 0, 0, -dpr, dpr * logical.w, dpr * logical.h};
            break;
        case ROT_270:
            r = (GAffine){0, -dpr, dpr, 0, 0, dpr * logical.w};
            break;
        case ROT_0:
        default:
            r = (GAffine){dpr, 0, 0, dpr, 0, 0};
            break;
    }
    return r;
}

GAffine g_physical_to_logical(GSize logical, double dpr, Rotation rot) {
    GAffine fwd = g_logical_to_physical(logical, dpr, rot);
    return g_affine_invert(&fwd);
}

GSize g_logical_size(GSize physical, double dpr, Rotation rot) {
    if (dpr <= 0.0) {
        dpr = 1.0;
    }
    if (rot == ROT_90 || rot == ROT_270) {
        /* physical.w = logical.h * dpr, physical.h = logical.w * dpr */
        return gsize(physical.h / dpr, physical.w / dpr);
    }
    return gsize(physical.w / dpr, physical.h / dpr);
}

GPoint g_clamp_focus(double fx, double fy) {
    double eps = 1e-4;
    if (!(fx >= 0.0)) fx = 0.5;
    if (!(fy >= 0.0)) fy = 0.5;
    if (fx < eps) fx = eps;
    if (fx > 1.0 - eps) fx = 1.0 - eps;
    if (fy < eps) fy = eps;
    if (fy > 1.0 - eps) fy = 1.0 - eps;
    return gpoint(fx, fy);
}
