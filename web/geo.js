/*
 * geo.js - 与 C 端 geometry.c/layout.c 严格同构的实现。
 * 坐标系统：
 *   image   源图像像素
 *   logical 逻辑窗口 DIP（UI/命中测试坐标）
 *   physical物理像素 = logical * dpr，再经旋转
 */
'use strict';

function p(x, y) { return {x, y}; }

function affineApply(m, q) {
  return { x: m.a * q.x + m.c * q.y + m.tx,
           y: m.b * q.x + m.d * q.y + m.ty };
}

function affineInvert(m) {
  const det = m.a * m.d - m.b * m.c;
  return {
    a: m.d / det, b: -m.b / det, c: -m.c / det, d: m.a / det,
    tx: -(m.d / det * m.tx - m.c / det * m.ty),
    ty: -(-m.b / det * m.tx + m.a / det * m.ty),
  };
}

/* 逻辑 -> 物理（旋转 + dpr），与 C 端 g_logical_to_physical 一致 */
function logicalToPhysical(logical, dpr, rot) {
  switch (rot) {
    case 90:  return { a: 0, b: dpr, c: -dpr, d: 0, tx: dpr * logical.h, ty: 0 };
    case 180: return { a: -dpr, b: 0, c: 0, d: -dpr, tx: dpr * logical.w, ty: dpr * logical.h };
    case 270: return { a: 0, b: -dpr, c: dpr, d: 0, tx: 0, ty: dpr * logical.w };
    default:  return { a: dpr, b: 0, c: 0, d: dpr, tx: 0, ty: 0 };
  }
}
function physicalToLogical(logical, dpr, rot) {
  return affineInvert(logicalToPhysical(logical, dpr, rot));
}

function clampFocus(fx, fy) {
  const eps = 1e-4;
  fx = Math.min(Math.max(fx, eps), 1 - eps);
  fy = Math.min(Math.max(fy, eps), 1 - eps);
  return { x: fx, y: fy };
}

/*
 * 布局求解。mode: 'cover' | 'contain'
 * 返回 { target, crop, scale, imageToLogical, logicalToImage, fullyVisible }
 */
function resolveLayout(cfg, vp) {
  const iw = cfg.image_w, ih = cfg.image_h;
  const scale = cfg.mode === 'contain'
    ? Math.min(vp.w / iw, vp.h / ih)
    : Math.max(vp.w / iw, vp.h / ih);
  const dw = iw * scale, dh = ih * scale;
  let crop, target, fullyVisible = false;
  if (cfg.mode === 'contain') {
    crop = { x: 0, y: 0, w: iw, h: ih };
    target = { x: (vp.w - dw) / 2, y: (vp.h - dh) / 2, w: dw, h: dh };
    fullyVisible = true;
  } else {
    const cw = vp.w / scale, ch = vp.h / scale;
    const f = clampFocus(cfg.focus_x ?? 0.5, cfg.focus_y ?? 0.5);
    const cx = f.x * (iw - cw), cy = f.y * (ih - ch);
    crop = { x: cx, y: cy, w: cw, h: ch };
    target = { x: -cx * scale, y: -cy * scale, w: dw, h: dh };
  }
  const imageToLogical = { a: scale, b: 0, c: 0, d: scale, tx: target.x, ty: target.y };
  return { crop, target, scale, imageToLogical,
           logicalToImage: affineInvert(imageToLogical), fullyVisible,
           viewport: { x: 0, y: 0, w: vp.w, h: vp.h } };
}

function hitTest(t, logicalPt) {
  const ip = affineApply(t.logicalToImage, logicalPt);
  const inside = ip.x >= 0 && ip.y >= 0 &&
    ip.x <= t.crop.x + t.crop.w + 1e-6 &&
    ip.y <= t.crop.y + t.crop.h + 1e-6;
  return { inside, imagePoint: ip };
}

function reportedCrop(t) {
  const x = Math.round(t.crop.x), y = Math.round(t.crop.y);
  return {
    x, y,
    w: Math.ceil(t.crop.x + t.crop.w) - x,
    h: Math.ceil(t.crop.y + t.crop.h) - y,
  };
}

/* 模拟设备预设：逻辑尺寸(px 基准)、DPR、默认方向 */
const DEVICE_PRESETS = [
  { id: 'phone-se',   name: '手机 4.7"',   w: 375, h: 667, dpr: 2.0 },
  { id: 'phone-pro',  name: '手机 6.1"',   w: 393, h: 852, dpr: 3.0 },
  { id: 'tablet',     name: '平板 10.9"',  w: 820, h: 1180, dpr: 2.0 },
  { id: 'foldable',   name: '折叠屏外屏',  w: 280, h: 654, dpr: 2.0 },
  { id: 'desktop-hd', name: '桌面 1080p',  w: 1920, h: 1080, dpr: 1.0 },
  { id: 'desktop-4k', name: '桌面 4K',     w: 1920, h: 1080, dpr: 2.0 },
  { id: 'ultranarrow',name: '极窄竖条',    w: 120, h: 900, dpr: 2.0 },
];

if (typeof module !== 'undefined') {
  module.exports = { affineApply, affineInvert, logicalToPhysical,
    physicalToLogical, clampFocus, resolveLayout, hitTest, reportedCrop,
    DEVICE_PRESETS };
}
