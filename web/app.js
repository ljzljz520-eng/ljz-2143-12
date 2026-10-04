// 布局调试台：屏幕模拟 + 配置编辑 + 乐观锁保存 + 远程冲突提示 + 现场上报查看。
'use strict';
import {computeGeometry, srcToLogical, logicalToPhys, cropNormalized, clamp} from './geometry.js';
import {EventQueue} from './events.js';

const $ = (id) => document.getElementById(id);
const sessionId = 'web-' + Math.random().toString(36).slice(2, 10);

// ---- 状态 ----
let serverCfg = null;       // 服务端最新配置
let savedVersion = 0;       // 最后同步/保存的版本（保存的最终选择）
let editBaseVersion = null; // 编辑开始时的版本
const queue = new EventQueue();

const IMG_W = 1276, IMG_H = 838;   // 与 assets/background.png 一致；实际由 /api/images/current 头校验
let img = new Image();
img.src = '/api/images/current?t=' + Date.now();

const sim = { w: 390, h: 844, dpr: 3, rotation: 0 };

function toast(msg, ms = 1800) {
  const t = $('toast');
  t.textContent = msg;
  t.classList.add('show');
  clearTimeout(toast._t);
  toast._t = setTimeout(() => t.classList.remove('show'), ms);
}

// ---- 屏幕模拟 ----
$('preset').addEventListener('change', () => {
  const v = $('preset').value;
  if (!v) return;
  const [w, h, d] = v.split(',').map(Number);
  $('lw').value = w; $('lh').value = h; $('dpr').value = d;
  pushSize();
});
$('lw').addEventListener('input', () => pushSize());
$('lh').addEventListener('input', () => pushSize());
$('dpr').addEventListener('input', () => { sim.dpr = +$('dpr').value || 1; render(); });
$('rot').addEventListener('change', () => {
  sim.rotation = +$('rot').value;
  pushSize();
});

function effectiveWH() {
  // 旋转 90/270 逻辑宽高互换
  let w = Math.max(1, +$('lw').value || 1);
  let h = Math.max(1, +$('lh').value || 1);
  if (sim.rotation === 90 || sim.rotation === 270) [w, h] = [h, w];
  return {w, h};
}

function pushSize() {
  const {w, h} = effectiveWH();
  sim.w = w; sim.h = h;
  queue.pushSize(w, h, 0);
  scheduleDrain();
}

let drainTimer = null;
function scheduleDrain() {
  clearTimeout(drainTimer);
  drainTimer = setTimeout(() => {
    const e = queue.drain();
    if (e) {
      $('coalesce-info').textContent =
        `已合并 ${queue.coalesced} 个尺寸事件，应用最新 ${e.w}×${e.h}（epoch=${e.epoch}）`;
      render();
    }
  }, 60);
}

document.querySelectorAll('button[data-burst]').forEach((b) => {
  b.addEventListener('click', () => {
    const [n, ms] = b.dataset.burst.split(',').map(Number);
    const baseW = 390, baseH = 844;
    for (let i = 0; i < n; i++) {
      const w = baseW + Math.round(600 * (i + 1) / n);
      const h = baseH;
      setTimeout(() => {
        queue.pushSize(w, h, 0);
        scheduleDrain();
      }, (ms / n) * i);
    }
  });
});

// ---- 配置编辑（未保存选择 = 草稿） ----
$('fit').addEventListener('change', markDirty);
$('strategy').addEventListener('change', markDirty);
$('budget-mb').addEventListener('change', markDirty);
$('budget-n').addEventListener('change', markDirty);
$('min-b').addEventListener('change', markDirty);
$('max-b').addEventListener('change', markDirty);

let focus = [0.5, 0.5];
function markDirty() {
  $('save-state').textContent = '● 未保存修改（不会被远程尺寸事件覆盖）';
  $('save-state').style.color = '#e0b34a';
  render();
}

// ---- 焦点拖拽 ----
$('stage').addEventListener('click', (ev) => {
  const g = currentGeometry();
  const rect = $('stage').getBoundingClientRect();
  const cw = $('stage').width, ch = $('stage').height;
  const sx = (ev.clientX - rect.left) / (rect.width) * cw;
  const sy = (ev.clientY - rect.top) / (rect.height) * ch;
  // 画布上点的是"视口内可见区域"：用视口屏幕坐标反推源图焦点（简化：直接按可见框比例）
  const [lsx, lsy] = viewToSrc(g, sx, sy);
  focus = [clamp(lsx / IMG_W, 0, 1), clamp(lsy / IMG_H, 0, 1)];
  markDirty();
});

function viewToSrc(g, px, py) {
  // 画布把逻辑视口等比放进画布；先转逻辑再转源
  const pad = 20;
  const scale = Math.min(($('stage').width - pad * 2) / g.win_w,
                         ($('stage').height - pad * 2) / g.win_h);
  const lx = (px - pad) / scale;
  const ly = (py - pad) / scale;
  const sx = (lx - g.tx) / g.scale_x;
  const sy = (ly - g.ty) / g.scale_y;
  return [sx, sy];
}

function currentGeometry() {
  return computeGeometry({
    src_w: IMG_W, src_h: IMG_H,
    win_w: sim.w, win_h: sim.h, dpr: sim.dpr, rotation: sim.rotation,
    fit: $('fit').value, focus_x: focus[0], focus_y: focus[1],
  });
}

// ---- 渲染预览 ----
function render() {
  const g = currentGeometry();
  $('focus-val').textContent = `[${focus[0].toFixed(3)}, ${focus[1].toFixed(3)}]`;
  $('phys-label').textContent =
    `物理像素 ${g.phys_w}×${g.phys_h} · 逻辑 ${g.win_w}×${g.win_h}dp · s=${g.scale.toFixed(3)}`;
  const cv = $('stage'); const ctx = cv.getContext('2d');
  ctx.fillStyle = '#000'; ctx.fillRect(0, 0, cv.width, cv.height);
  const pad = 20;
  const scale = Math.min((cv.width - pad * 2) / g.win_w, (cv.height - pad * 2) / g.win_h);
  ctx.save();
  ctx.translate(pad, pad);
  ctx.scale(scale, scale);
  // 视口
  ctx.strokeStyle = '#fff'; ctx.lineWidth = 1 / scale;
  ctx.strokeRect(0, 0, g.win_w, g.win_h);
  // 图片内容
  if (img.complete && img.naturalWidth) {
    ctx.drawImage(img, g.tx, g.ty, g.content_w, g.content_h);
  }
  // contain 黑边标注
  if (g.fit === 'contain') {
    ctx.fillStyle = 'rgba(255,255,255,.08)';
    if (g.lb_top > 0) ctx.fillRect(0, 0, g.win_w, g.lb_top);
    if (g.lb_bottom > 0) ctx.fillRect(0, g.win_h - g.lb_bottom, g.win_w, g.lb_bottom);
  }
  // 理论裁切框（cover：与视口边缘重合）
  const [cx1, cy1] = srcToLogical(g, g.crop.x, g.crop.y);
  const [cx2, cy2] = srcToLogical(g, g.crop.x + g.crop.w, g.crop.y + g.crop.h);
  ctx.strokeStyle = '#36e07a'; ctx.lineWidth = 2 / scale;
  ctx.strokeRect(cx1, cy1, cx2 - cx1, cy2 - cy1);
  // 焦点
  const [fx, fy] = srcToLogical(g, focus[0] * IMG_W, focus[1] * IMG_H);
  ctx.strokeStyle = '#ff4444'; ctx.lineWidth = 2 / scale;
  ctx.beginPath(); ctx.moveTo(fx - 14, fy); ctx.lineTo(fx + 14, fy);
  ctx.moveTo(fx, fy - 14); ctx.lineTo(fx, fy + 14); ctx.stroke();
  ctx.restore();
  const c = cropNormalized(g);
  ctx.fillStyle = '#9fc3ff'; ctx.font = '12px monospace';
  ctx.fillText(`理论裁切(归一化) [${c.map(v=>v.toFixed(3)).join(', ')}]`, 10, cv.height - 10);
}
img.onload = render;
setInterval(render, 200); // 低成本保持画面新鲜（焦点拖拽外的输入兜底）

// ---- 后端交互 ----
async function api(path, opts) {
  const r = await fetch(path, opts);
  const j = await r.json().catch(() => ({}));
  return {status: r.status, body: j};
}

async function loadLayout() {
  const {body} = await api('/api/layout');
  applyServerCfg(body);
}

function applyServerCfg(bundle) {
  serverCfg = bundle.data;
  savedVersion = bundle.version;
  if (editBaseVersion == null) editBaseVersion = savedVersion;
  $('fit').value = serverCfg.fit;
  $('strategy').value = serverCfg.texture_strategy || 'atlas';
  $('budget-mb').value = serverCfg.budget_bytes_mb ?? 32;
  $('budget-n').value = serverCfg.budget_entries ?? 16;
  $('min-b').value = serverCfg.min_bucket ?? 128;
  $('max-b').value = serverCfg.max_bucket ?? 2048;
  focus = serverCfg.focus && serverCfg.focus.length === 2
    ? serverCfg.focus : [0.5, 0.5];
  $('save-state').textContent = `已同步 v${savedVersion}（图 rev=${serverCfg.image_rev}）`;
  $('save-state').style.color = '#79d49a';
  $('conflict').classList.add('hidden');
  render();
}

function draftPatch() {
  return {
    fit: $('fit').value,
    focus: [+focus[0].toFixed(6), +focus[1].toFixed(6)],
    texture_strategy: $('strategy').value,
    budget_bytes_mb: +$('budget-mb').value,
    budget_entries: +$('budget-n').value,
    min_bucket: +$('min-b').value,
    max_bucket: +$('max-b').value,
  };
}

$('btn-open-session').addEventListener('click', async () => {
  editBaseVersion = savedVersion;
  const {body} = await api('/api/session', {
    method: 'POST', headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({session: sessionId}),
  });
  toast(`新编辑会话，基线 v${body.base_version}；撤销不会越过它`);
});

$('btn-save').addEventListener('click', () => save(false));

async function save(forceKeep) {
  // 保存=用户最终选择：推进本地 epoch，使所有在途尺寸事件立刻失效，不能覆盖新模式
  queue.commitConfig();
  const patch = draftPatch();
  const base = forceKeep ? null : (editBaseVersion ?? savedVersion);
  const payload = {...patch, expected_version: forceKeep ? savedVersion : base,
                   author: 'web-debugger'};
  const {status, body} = await api('/api/layout', {
    method: 'POST', headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(payload),
  });
  if (status === 409) {
    // 远程更新冲突提示：不让远程覆盖用户选择
    $('conf-base').textContent = payload.expected_version;
    $('conf-server').textContent = body.server_version;
    $('conf-fit').textContent = body.server_data?.fit;
    $('conflict').classList.remove('hidden');
    toast('版本冲突：远程已更新');
    return;
  }
  applyServerCfg(body);
  toast(`已保存 v${body.version}`);
}

$('conf-keep').addEventListener('click', async () => {
  // 基于服务端最新版本重放用户选择
  const {body} = await api('/api/layout');
  savedVersion = body.version;
  await save(true);
});
$('conf-discard').addEventListener('click', async () => {
  const {body} = await api('/api/layout');
  applyServerCfg(body);
});

$('btn-undo').addEventListener('click', async () => {
  const {status, body} = await api('/api/undo', {
    method: 'POST', headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({session: sessionId}),
  });
  if (status === 409) {
    toast('已到本次编辑会话基线，撤销只作用于本次编辑');
    return;
  }
  queue.commitConfig();
  applyServerCfg(body);
  toast(`撤销 → v${body.version}（恢复自 v${body.restored_version}）`);
});

$('btn-refresh-reports').addEventListener('click', loadReports);
async function loadReports() {
  const {body} = await api('/api/reports');
  const tb = document.querySelector('#reports tbody');
  tb.innerHTML = '';
  for (const r of (body.reports || [])) {
    const b = r.body;
    const tr = document.createElement('tr');
    tr.innerHTML = `<td>${b.device_id}</td><td>${b.layout_version}</td><td>${b.image_rev}</td>
      <td>${(b.physical||[]).join('×')} / ${(b.logical||[]).join('×')}</td>
      <td>${b.dpr}</td><td>${b.rotation}°</td><td>${b.fit}</td>
      <td>${(b.crop||[]).map(v=>(+v).toFixed(3)).join(', ')}</td>`;
    tb.appendChild(tr);
  }
}

// 轮询远程更新：若本地有未保存编辑则弹冲突，不静默覆盖草稿
let lastSeen = savedVersion;
setInterval(async () => {
  const {body} = await api('/api/layout');
  if (!body || body.version == null) return;
  if (body.version > lastSeen) {
    lastSeen = body.version;
    const dirty = $('save-state').textContent.startsWith('●');
    if (dirty && $('conflict').classList.contains('hidden')) {
      $('conf-base').textContent = savedVersion;
      $('conf-server').textContent = body.version;
      $('conf-fit').textContent = body.data.fit;
      $('conflict').classList.remove('hidden');
    } else {
      applyServerCfg(body);
    }
  }
}, 4000);

// 初始化：先开会话再拉配置
(async function init() {
  await api('/api/session', {
    method: 'POST', headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({session: sessionId}),
  });
  await loadLayout();
  lastSeen = savedVersion;
  await loadReports();
  pushSize();
})();
