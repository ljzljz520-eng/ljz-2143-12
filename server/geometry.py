"""参考几何：与 src/geometry.c、web/geometry.js 三套独立实现保持同口径。
后端用它校验 C 端上报的裁切框，E2E 用它生成"理论帧"与终端截图比对。"""
from __future__ import annotations

from dataclasses import dataclass, field


def clamp(v, lo=0.0, hi=1.0):
    return max(lo, min(hi, v))


@dataclass
class Geometry:
    src_w: float; src_h: float
    win_w: float; win_h: float
    dpr: float = 1.0
    rotation: int = 0
    fit: str = "cover"
    focus_x: float = 0.5
    focus_y: float = 0.5
    # outputs
    scale: float = 1.0; scale_x: float = 1.0; scale_y: float = 1.0
    tx: float = 0.0; ty: float = 0.0
    content_w: float = 0.0; content_h: float = 0.0
    crop: tuple = (0.0, 0.0, 0.0, 0.0)
    lb_top: float = 0.0; lb_left: float = 0.0
    lb_bottom: float = 0.0; lb_right: float = 0.0
    degenerate: bool = False
    phys_w: int = 0; phys_h: int = 0

    def compute(self):
        iw, ih, w, h = self.src_w, self.src_h, self.win_w, self.win_h
        self.dpr = self.dpr if self.dpr > 0 else 1.0
        self.focus_x = clamp(self.focus_x); self.focus_y = clamp(self.focus_y)
        self.rotation %= 360
        pw, ph = round(w * self.dpr), round(h * self.dpr)
        if self.rotation in (90, 270):
            self.phys_w, self.phys_h = ph, pw
        else:
            self.phys_w, self.phys_h = pw, ph
        if w < 16 or h < 16 or iw <= 0 or ih <= 0:
            self.degenerate = True
            self.content_w = self.content_h = 0
            self.crop = (0, 0, iw, ih)
            return self
        sx = sy = s = 1.0; tx = ty = 0.0; cw = ch = 0.0
        crop = (0.0, 0.0, iw, ih)
        lt = ll = lb = lr = 0.0
        if self.fit == "stretch":
            sx, sy = w / iw, h / ih
            cw, ch = w, h
        elif self.fit == "contain":
            s = min(w / iw, h / ih)
            cw, ch = iw * s, ih * s
            tx = self.focus_x * (w - cw); ty = self.focus_y * (h - ch)
            sx = sy = s; ll, lt = tx, ty
            lr, lb = w - cw - ll, h - ch - lt
        else:
            s = max(w / iw, h / ih)
            cw, ch = iw * s, ih * s
            tx = self.focus_x * (w - cw); ty = self.focus_y * (h - ch)
            sx = sy = s
            crop = (-tx / s, -ty / s, w / s, h / s)
        self.scale=s; self.scale_x=sx; self.scale_y=sy
        self.tx=tx; self.ty=ty; self.content_w=cw; self.content_h=ch
        self.crop=crop
        self.lb_top=lt; self.lb_left=ll; self.lb_bottom=lb; self.lb_right=lr
        return self

    def src_to_logical(self, sx, sy):
        return sx*self.scale_x+self.tx, sy*self.scale_y+self.ty

    def logical_to_phys(self, lx, ly):
        x, y = lx*self.dpr, ly*self.dpr
        wp, hp = self.win_w*self.dpr, self.win_h*self.dpr
        if self.rotation == 90:   return hp-y, x
        if self.rotation == 180:  return wp-x, hp-y
        if self.rotation == 270:  return y, wp-x
        return x, y

    def phys_to_logical(self, px, py):
        wp, hp = self.win_w*self.dpr, self.win_h*self.dpr
        if self.rotation == 90:   x, y = py, hp-px
        elif self.rotation == 180: x, y = wp-px, hp-py
        elif self.rotation == 270: x, y = wp-py, px
        else: x, y = px, py
        return x/self.dpr, y/self.dpr

    def phys_to_src(self, px, py):
        lx, ly = self.phys_to_logical(px, py)
        return (lx-self.tx)/self.scale_x, (ly-self.ty)/self.scale_y

    def normalized_crop(self):
        if self.src_w <= 0 or self.src_h <= 0:
            return [0.0, 0.0, 0.0, 0.0]
        x, y, w, h = self.crop
        return [clamp(x/self.src_w), clamp(y/self.src_h),
                clamp(w/self.src_w), clamp(h/self.src_h)]
