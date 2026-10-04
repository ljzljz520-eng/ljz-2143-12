#include "layout.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void layout_config_init(LayoutConfig *cfg, const char *image_id, int iw, int ih) {
    memset(cfg, 0, sizeof(*cfg));
    if (image_id != NULL) {
        snprintf(cfg->image_id, sizeof(cfg->image_id), "%s", image_id);
    }
    cfg->image_w = iw;
    cfg->image_h = ih;
    cfg->mode = LAYOUT_COVER;
    cfg->focus_x = 0.5;
    cfg->focus_y = 0.5;
    cfg->rotation = ROT_0;
    cfg->dpr_override = 0.0;
}

bool layout_config_valid(const LayoutConfig *cfg, char *err, size_t errsz) {
    if (cfg == NULL) {
        if (err && errsz) snprintf(err, errsz, "config is null");
        return false;
    }
    if (cfg->image_id[0] == '\0') {
        if (err && errsz) snprintf(err, errsz, "image_id empty");
        return false;
    }
    if (cfg->image_w <= 0 || cfg->image_h <= 0) {
        if (err && errsz) snprintf(err, errsz, "image dimensions invalid");
        return false;
    }
    if (cfg->mode != LAYOUT_COVER && cfg->mode != LAYOUT_CONTAIN) {
        if (err && errsz) snprintf(err, errsz, "unknown layout mode");
        return false;
    }
    if (cfg->focus_x < 0.0 || cfg->focus_x > 1.0 ||
        cfg->focus_y < 0.0 || cfg->focus_y > 1.0) {
        if (err && errsz) snprintf(err, errsz, "focus out of [0,1]");
        return false;
    }
    if (cfg->dpr_override < 0.0 || cfg->dpr_override > 8.0) {
        if (err && errsz) snprintf(err, errsz, "dpr out of range");
        return false;
    }
    (void)err;
    return true;
}

const char *layout_mode_str(LayoutMode mode) {
    return mode == LAYOUT_CONTAIN ? "contain" : "cover";
}

bool layout_mode_parse(const char *s, LayoutMode *out) {
    if (s == NULL || out == NULL) return false;
    if (strcmp(s, "cover") == 0) {
        *out = LAYOUT_COVER;
        return true;
    }
    if (strcmp(s, "contain") == 0) {
        *out = LAYOUT_CONTAIN;
        return true;
    }
    return false;
}

LayoutTransform layout_resolve(const LayoutConfig *cfg, GSize vp) {
    LayoutTransform t;
    memset(&t, 0, sizeof(t));
    t.viewport = grect(0, 0, vp.w, vp.h);

    double iw = (double)cfg->image_w;
    double ih = (double)cfg->image_h;
    double scale;
    if (cfg->mode == LAYOUT_CONTAIN) {
        scale = fmin(vp.w / iw, vp.h / ih);
    } else {
        scale = fmax(vp.w / iw, vp.h / ih);
    }
    t.scale = scale;

    double dw = iw * scale;
    double dh = ih * scale;

    if (cfg->mode == LAYOUT_CONTAIN) {
        t.crop = grect(0, 0, iw, ih);
        t.target = grect((vp.w - dw) / 2.0, (vp.h - dh) / 2.0, dw, dh);
        t.fully_visible = true;
    } else {
        double cw = vp.w / scale; /* 视口在源图像上覆盖的宽 */
        double ch = vp.h / scale;
        GPoint f = g_clamp_focus(cfg->focus_x, cfg->focus_y);
        double cx = f.x * (iw - cw);
        double cy = f.y * (ih - ch);
        t.crop = grect(cx, cy, cw, ch);
        /* 把裁切矩形映射到整个视口 */
        t.target = grect(-cx * scale, -cy * scale, dw, dh);
        t.fully_visible = (dw <= vp.w + 1e-6 && dh <= vp.h + 1e-6);
    }

    /* logical = image*scale + target.origin */
    t.image_to_logical = (GAffine){scale, 0, 0, scale, t.target.x, t.target.y};
    t.logical_to_image = g_affine_invert(&t.image_to_logical);
    return t;
}

bool layout_hit_test(const LayoutTransform *t, GPoint logical, GPoint *image_pt) {
    if (t == NULL) return false;
    GPoint ip = g_affine_apply(&t->logical_to_image, logical);
    bool inside = ip.x >= 0.0 && ip.y >= 0.0 &&
                  ip.x <= t->crop.x + t->crop.w + 1e-6 &&
                  ip.y <= t->crop.y + t->crop.h + 1e-6;
    if (image_pt) *image_pt = ip;
    return inside;
}

GRect layout_reported_crop(const LayoutConfig *cfg, const LayoutTransform *t) {
    (void)cfg;
    double x = t->crop.x;
    double y = t->crop.y;
    double w = t->crop.w;
    double h = t->crop.h;
    int ix = (int)lround(x);
    int iy = (int)lround(y);
    int iw = (int)ceil(x + w) - ix;
    int ih = (int)ceil(y + h) - iy;
    return grect(ix, iy, iw, ih);
}

/* ---------- 编辑会话 ----------
 *
 * 线性栈：stack[0] 恒为本次编辑基础 (baseline)，head 为当前条目下标。
 * undo 下限是 0（本次编辑基础），不允许越过到上一次编辑会话。
 * push 时丢弃 head 之后的 redo 分支；栈满时丢弃紧邻 baseline 的旧条目，
 * baseline 语义仍保留为"当前已知最早的本次编辑起点"。
 */

void edit_session_begin(EditSession *s, const LayoutConfig *baseline, int remote_version) {
    memset(s, 0, sizeof(*s));
    s->stack[0] = *baseline;
    s->head = 0;
    s->base = 0;
    s->count = 1;
    s->remote_version = remote_version;
    s->remote_changed = false;
}

void edit_session_push(EditSession *s, const LayoutConfig *cfg) {
    /* 新编辑作废 redo 分支 */
    s->count = s->head + 1;
    int next = s->head + 1;
    if (next >= LAYOUT_HISTORY_CAP) {
        /* 左移一格，base 仍指向下标 0 */
        memmove(&s->stack[0], &s->stack[1],
                (size_t)(LAYOUT_HISTORY_CAP - 1) * sizeof(LayoutConfig));
        next = LAYOUT_HISTORY_CAP - 1;
        s->count = LAYOUT_HISTORY_CAP;
    }
    s->head = next;
    s->stack[s->head] = *cfg;
    s->count = s->head + 1;
}

bool edit_session_can_undo(const EditSession *s) {
    return s->head > s->base;
}

bool edit_session_can_redo(const EditSession *s) {
    return s->head < s->count - 1;
}

bool edit_session_undo(EditSession *s, LayoutConfig *out) {
    if (!edit_session_can_undo(s)) return false;
    s->head--;
    if (out) *out = s->stack[s->head];
    return true;
}

bool edit_session_redo(EditSession *s, LayoutConfig *out) {
    if (!edit_session_can_redo(s)) return false;
    s->head++;
    if (out) *out = s->stack[s->head];
    return true;
}

void edit_session_notify_remote(EditSession *s, int remote_version) {
    if (remote_version > s->remote_version) {
        s->remote_version = remote_version;
        s->remote_changed = true;
    }
}

const LayoutConfig *edit_session_baseline(const EditSession *s) {
    return &s->stack[s->base];
}

const LayoutConfig *edit_session_current(const EditSession *s) {
    return &s->stack[s->head];
}
