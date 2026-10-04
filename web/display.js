/*
 * display.js - Web 现场展示终端
 *
 * 与 C 展示端使用同一份 geo.js：
 *  - 物理像素 = canvas backing store（devicePixelRatio × CSS 像素）
 *  - 逻辑窗口 = CSS 像素（window.innerWidth/Height）
 *  - orientationchange / resize 后重新解算并上报裁切
 *  - 配置来自后端，轮询版本；新版本且图变 -> 原子换图后一次重绘
 *  - 真实裁切区域 POST /api/reports/crop（client: web-display）
 */
'use strict';

const cv = document.getElementById('cv');
const ctx = cv.getContext('2d');
const hud = document.getElementById('hud');
const errEl = document.getElementById('err');

const term = {
  deviceId: 'web-' + (navigator.deviceMemory || 'x') + '-' +
            Math.abs(hashStr(location.userAgent + screen.width + screen.height)).toString(36).slice(0, 6),
  record: null,
  image: null,
  imageVersion: -1,
  logical: { w: 0, h: 0 },
  dpr: 1,
  rotation: 0,
  lastReportKey: '',
  resizeRaf: 0,
};
function hashStr(s) { let h = 0; for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) | 0; return h; }

function showError(msg) {
  errEl.textContent = msg;
  errEl.style.display = msg ? 'block' : 'none';
}

function currentRotation() {
  if (typeof screen !== 'undefined' && screen.orientation &&
      typeof screen.orientation.angle === 'number') {
    return ((screen.orientation.angle % 360) + 360) % 360;
  }
  return 0;
}

function measure() {
  term.dpr = Math.max(0.5, Math.min(8, window.devicePixelRatio || 1));
  term.logical.w = window.innerWidth;
  term.logical.h = window.innerHeight;
  term.rotation = currentRotation();
  cv.width = Math.round(term.logical.w * term.dpr);
  cv.height = Math.round(term.logical.h * term.dpr);
}

function draw() {
  if (!term.record) return;
  measure();
  const cfg = term.record.config;
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, cv.width, cv.height);
  if (!term.image) return;

  /*
   * 布局以"当前浏览器视口"为逻辑窗口求解。orientationchange 后
   * innerWidth/Height 已交换，视口本身就是新方向，无需再旋转内容。
   * config.rotation>0 表示强制内容旋转（面板安装方向与逻辑方向不一致），
   * 此时先在面板方向离屏绘制再绕视口中心旋转（与 C renderer 同构）。
   */
  const vp = { w: term.logical.w, h: term.logical.h };
  const forcedRot = ((cfg.rotation | 0) % 360);
  ctx.setTransform(term.dpr, 0, 0, term.dpr, 0, 0);
  ctx.imageSmoothingQuality = 'high';
  let t;
  if (forcedRot === 0) {
    t = resolveLayout(cfg, vp);
    ctx.save(); ctx.beginPath(); ctx.rect(0, 0, vp.w, vp.h); ctx.clip();
    ctx.drawImage(term.image,
      t.crop.x, t.crop.y, t.crop.w, t.crop.h,
      t.target.x, t.target.y, t.target.w, t.target.h);
    ctx.restore();
  } else {
    const pw = (forcedRot === 90 || forcedRot === 270) ? vp.h : vp.w;
    const ph = (forcedRot === 90 || forcedRot === 270) ? vp.w : vp.h;
    const off = document.createElement('canvas');
    off.width = Math.round(pw * term.dpr);
    off.height = Math.round(ph * term.dpr);
    const octx = off.getContext('2d');
    octx.setTransform(term.dpr, 0, 0, term.dpr, 0, 0);
    t = resolveLayout(cfg, { w: pw, h: ph });
    octx.fillStyle = '#000'; octx.fillRect(0, 0, pw, ph);
    octx.drawImage(term.image,
      t.crop.x, t.crop.y, t.crop.w, t.crop.h,
      t.target.x, t.target.y, t.target.w, t.target.h);
    ctx.save();
    ctx.translate(vp.w / 2, vp.h / 2);
    ctx.rotate(forcedRot * Math.PI / 180);
    ctx.drawImage(off, -vp.w / 2, -vp.h / 2, vp.w, vp.h);
    ctx.restore();
  }

  const rc = reportedCrop(t);
  hud.textContent =
    `device ${term.deviceId}\n` +
    `image  ${cfg.image_id} v${term.record.version}\n` +
    `mode   ${cfg.mode}  focus (${cfg.focus_x.toFixed(2)},${cfg.focus_y.toFixed(2)})\n` +
    `logical ${term.logical.w}x${term.logical.h}  dpr ${term.dpr.toFixed(2)}  rot ${term.rotation}\n` +
    `physical ${cv.width}x${cv.height}\n` +
    `crop   x=${rc.x} y=${rc.y} w=${rc.w} h=${rc.h} (source ${cfg.image_w}x${cfg.image_h})`;
  scheduleReport(t, rc, forcedRot);
}

let reportTimer = 0;
function scheduleReport(t, rc, forcedRot) {
  const key = `${term.record.version}|${rc.x},${rc.y},${rc.w},${rc.h}|` +
              `${term.logical.w}x${term.logical.h}|${term.dpr}|${term.rotation}`;
  if (key === term.lastReportKey) return;
  /* 合并连续 resize：静止 400ms 后才发最终裁切，拖动过程中不刷接口 */
  clearTimeout(reportTimer);
  reportTimer = setTimeout(() => {
    term.lastReportKey = key;
    postReport(t, rc);
  }, 400);
}

async function postReport(t, rc) {
  try {
    const res = await fetch('/api/reports/crop', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        device_id: term.deviceId,
        image_id: term.record.config.image_id,
        mode: term.record.config.mode,
        crop: rc,
        viewport_logical: { w: Math.round(term.logical.w), h: Math.round(term.logical.h) },
        dpr: Number(term.dpr.toFixed(3)),
        rotation: forcedRot,
        panel_rotation: term.rotation,
        source: { w: term.record.config.image_w, h: term.record.config.image_h },
        scale: Number(t.scale.toFixed(6)),
        client: 'web-display',
      }),
    });
    if (!res.ok) showError('上报失败 HTTP ' + res.status);
    else showError('');
  } catch (err) {
    showError('上报失败：' + err);
  }
}

async function fetchConfig() {
  try {
    const res = await fetch('/api/layout/latest');
    const data = await res.json();
    const first = term.record === null;
    if (first || data.version !== term.record.version) {
      term.record = data;
      const img = new Image();
      await new Promise((ok, fail) => {
        img.onload = ok; img.onerror = fail;
        img.src = `/assets/${encodeURIComponent(data.config.image_id)}?v=${data.version}`;
      });
      term.image = img;
      term.imageVersion = data.version;
      draw();
    }
  } catch (err) {
    showError('配置拉取失败：' + err);
  }
}

/* resize 合并到 rAF：快速拖动只在每帧绘制一次，上报再做 400ms 静止去抖 */
function onResize() {
  if (term.resizeRaf) return;
  term.resizeRaf = requestAnimationFrame(() => {
    term.resizeRaf = 0;
    draw();
  });
}
window.addEventListener('resize', onResize);
window.addEventListener('orientationchange', () => setTimeout(draw, 120));

fetchConfig().then(draw);
setInterval(fetchConfig, 3000);
setInterval(draw, 1000);
