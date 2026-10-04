/*
 * app.js - 管理调试台
 *
 * 关键规则（与 C 端一致）：
 *  - choiceSeq：用户显式选择（模式/焦点）代际；resize 只重排预览，
 *    绝不把视口尺寸/迟到事件写回配置（"不能让迟到的尺寸事件覆盖新模式"）。
 *  - 保存基于编辑开始时拿到的 baseVersion（乐观锁）；409 时弹出冲突条，
 *    用户选择"加载远程"或"强制保存"，不自动合并、不静默覆盖。
 *  - undo/redo 仅在本次编辑会话内，下限为 baseline。
 */
'use strict';

const $ = (id) => document.getElementById(id);
const state = {
  record: null,            // 服务端当前记录
  baseVersion: -1,         // 本次编辑基础版本
  config: null,            // 当前编辑配置（用户选择）
  screen: { w: 393, h: 852, dpr: 3, rotation: 0 },
  image: null,             // HTMLImageElement
  imgUrl: null,
  history: [],             // {cfg} 线性栈
  histHead: 0,
  histBase: 0,
  histCount: 0,
  choiceSeq: 0,
  pointer: null,
  conflict: false,
  dragFocus: false,
};
const HISTORY_CAP = 64;

function toast(msg, ms = 2200) {
  const t = $('toast');
  t.textContent = msg;
  t.classList.add('show');
  clearTimeout(toast._t);
  toast._t = setTimeout(() => t.classList.remove('show'), ms);
}

async function api(method, path, body) {
  const opt = { method, headers: {} };
  if (body !== undefined) {
    opt.headers['Content-Type'] = 'application/json';
    opt.body = JSON.stringify(body);
  }
  const res = await fetch(path, opt);
  let data = null;
  try { data = await res.json(); } catch { /* ignore */ }
  return { status: res.status, data };
}

/* ---------- 编辑会话（与 layout.c 的线性栈语义一致） ---------- */
function beginSession(cfg, version) {
  state.history = [structuredClone(cfg)];
  state.histHead = 0; state.histBase = 0; state.histCount = 1;
  state.baseVersion = version;
  state.config = structuredClone(cfg);
}
function pushHistory(cfg) {
  state.histCount = state.histHead + 1;
  let next = state.histHead + 1;
  if (next >= HISTORY_CAP) {
    state.history.shift();
    next = HISTORY_CAP - 1;
    state.histCount = HISTORY_CAP;
  } else {
    state.history.length = next;
  }
  state.history[next] = structuredClone(cfg);
  state.histHead = next;
  state.histCount = next + 1;
}
const canUndo = () => state.histHead > state.histBase;
const canRedo = () => state.histHead < state.histCount - 1;
function undo() {
  if (!canUndo()) return;
  state.histHead--;
  state.config = structuredClone(state.history[state.histHead]);
  state.choiceSeq++;
  syncControls(); render();
}
function redo() {
  if (!canRedo()) return;
  state.histHead++;
  state.config = structuredClone(state.history[state.histHead]);
  state.choiceSeq++;
  syncControls(); render();
}
function userChoice(mutator) {
  const draft = structuredClone(state.config);
  mutator(draft);
  state.config = draft;
  state.choiceSeq++;          // 用户选择代际 +1
  pushHistory(draft);
  syncControls(); render();
}

/* ---------- 加载 ---------- */
async function loadRecord({ keepSession = false } = {}) {
  const { status, data } = await api('GET', '/api/layout/latest');
  if (status !== 200) { toast('拉取配置失败'); return; }
  state.record = data;
  if (!keepSession) {
    beginSession(data.config, data.version);
  }
  await loadImage(data.config.image_id);
  syncControls(); render();
  loadHistory();
}
async function loadImage(imageId) {
  const url = `/assets/${encodeURIComponent(imageId)}?v=${Date.now()}`;
  state.imgUrl = url;
  const img = new Image();
  await new Promise((res, rej) => { img.onload = res; img.onerror = rej; img.src = url; });
  state.image = img;
}

async function save(force = false) {
  let base = state.baseVersion;
  if (force) {
    const { data } = await api('GET', '/api/layout/latest');
    base = data.version;
  }
  const { status, data } = await api('PUT', '/api/layout', {
    base_version: base,
    client: 'web-admin',
    config: state.config,
  });
  if (status === 200) {
    state.record = data;
    beginSession(data.config, data.version);  // 保存后以结果为新基线
    state.conflict = false; updateConflictUI();
    syncControls(); render(); loadHistory();
    toast(`已保存 v${data.version}（选择代际 ${state.choiceSeq} 保留）`);
  } else if (status === 409) {
    state.conflict = true;
    $('conflictText').textContent =
      `保存冲突：服务端已到 v${data.current_version}，本地基线 v${state.baseVersion}。` +
      `你的选择（代际 ${state.choiceSeq}）未被覆盖。`;
    updateConflictUI();
    toast('版本冲突', 3000);
  } else {
    toast('保存失败：' + (data?.error || status));
  }
}

function updateConflictUI() {
  $('conflict').classList.toggle('show', state.conflict);
}

/* ---------- 预览渲染 ---------- */
function currentTransform() {
  const vp = { w: state.screen.w, h: state.screen.h };
  const t = resolveLayout(state.config, vp);
  return { vp, t };
}

function render() {
  if (!state.config || !state.image) return;
  const { vp, t } = currentTransform();
  const cv = $('preview');
  cv.width = Math.max(1, Math.round(vp.w * state.screen.dpr));
  cv.height = Math.max(1, Math.round(vp.h * state.screen.dpr));
  const ctx = cv.getContext('2d');
  ctx.setTransform(state.screen.dpr, 0, 0, state.screen.dpr, 0, 0);
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, vp.w, vp.h);

  // 源图上的裁切矩形 -> 纹理坐标（这里直接从源 image 裁切绘制）
  const rot = state.screen.rotation;
  ctx.imageSmoothingQuality = 'high';
  if (rot === 0) {
    // 快路径：直接在逻辑视口解布局绘制
    const t0 = resolveLayout(state.config, { w: vp.w, h: vp.h });
    ctx.save(); ctx.beginPath(); ctx.rect(0, 0, vp.w, vp.h); ctx.clip();
    ctx.drawImage(state.image,
      t0.crop.x, t0.crop.y, t0.crop.w, t0.crop.h,
      t0.target.x, t0.target.y, t0.target.w, t0.target.h);
    ctx.restore();
  } else {
    // 先在"面板方向"离屏画布按未旋转布局绘制，再绕视口中心旋转，
    // 避免直接旋转矩形产生的宽高交换失真（与 C renderer 一致）。
    const pw = rot === 90 || rot === 270 ? vp.h : vp.w;
    const ph = rot === 90 || rot === 270 ? vp.w : vp.h;
    const off = document.createElement('canvas');
    off.width = Math.round(pw * state.screen.dpr);
    off.height = Math.round(ph * state.screen.dpr);
    const octx = off.getContext('2d');
    octx.setTransform(state.screen.dpr, 0, 0, state.screen.dpr, 0, 0);
    const tp = resolveLayout(state.config, { w: pw, h: ph });
    octx.fillStyle = '#000'; octx.fillRect(0, 0, pw, ph);
    octx.drawImage(state.image,
      tp.crop.x, tp.crop.y, tp.crop.w, tp.crop.h,
      tp.target.x, tp.target.y, tp.target.w, tp.target.h);
    ctx.save();
    ctx.translate(vp.w / 2, vp.h / 2);
    ctx.rotate(rot * Math.PI / 180);
    // off 是 pw×ph（物理像素），转回逻辑坐标绘制
    ctx.drawImage(off, -vp.w / 2, -vp.h / 2, vp.w, vp.h);
    ctx.restore();
  }
  const panelVp = (() => {
    const r = state.screen.rotation;
    return { w: r === 90 || r === 270 ? vp.h : vp.w,
             h: r === 90 || r === 270 ? vp.w : vp.h };
  })();
  const t2 = resolveLayout(state.config, panelVp);

  // 理论裁切框叠加：在屏幕物理显示上，cover 时内容铺满故框=视口边缘；
  // 真正的"源图裁切框"在下方数据面板给出，并在画面上用红边标出图像边界。
  const box = $('cropBox');
  box.style.display = 'block';
  box.style.borderColor = state.config.mode === 'cover' ? '#ff2d40' : '#39d98a';
  box.style.left = '0px'; box.style.top = '0px';
  box.style.width = '100%'; box.style.height = '100%';

  // 焦点标记：把源图焦点映到逻辑坐标
  const f = clampFocus(state.config.focus_x, state.config.focus_y);
  const fp = affineApply(t2.imageToLogical,
    { x: f.x * state.config.image_w, y: f.y * state.config.image_h });
  const mark = $('focusMark');
  mark.style.left = (fp.x / panelVp.w * 100) + '%';
  mark.style.top = (fp.y / panelVp.h * 100) + '%';

  // 数据面板
  const rc = reportedCrop(t2);
  $('theoryLine').innerHTML =
    `mode=<b>${state.config.mode}</b> source=${state.config.image_w}×${state.config.image_h} ` +
    `viewport(logical)=<b>${vp.w}×${vp.h}</b> dpr=<b>${state.screen.dpr}</b> rot=<b>${rot}</b><br/>` +
    `scale=<b>${t2.scale.toFixed(4)}</b> 源图裁切 x=${rc.x} y=${rc.y} w=${rc.w} h=${rc.h} ` +
    `完整可见=${t2.fullyVisible ? '是' : '否'}`;
  $('vpBadge').textContent = `${vp.w}×${vp.h} @${state.screen.dpr}x rot${rot}`;
  $('cropBadge').textContent = `crop ${rc.w}×${rc.h} @(${rc.x},${rc.y})`;
  $('physSize').textContent = `${cv.width} × ${cv.height} px`;

  // 命中反算（鼠标）
  if (state.pointer) {
    const hit = hitTest(t2, state.pointer);
    $('hitLine').innerHTML =
      `逻辑点(${state.pointer.x.toFixed(1)},${state.pointer.y.toFixed(1)}) -> ` +
      `源图像(${hit.imagePoint.x.toFixed(1)},${hit.imagePoint.y.toFixed(1)}) ` +
      (hit.inside ? '<b style="color:#39d98a">HIT</b>' : '<b style="color:#ff8a80">MISS/留边</b>');
  }

  // 状态
  $('versionTag').textContent = state.record ? `v${state.record.version}` : '-';
  $('checksumTag').textContent = state.record ? state.record.checksum : '-';
  $('baseTag').textContent = 'v' + state.baseVersion;
  $('genTag').textContent = state.choiceSeq;
  const dirty = JSON.stringify(state.config) !==
    JSON.stringify(state.record?.config);
  $('dirtyTag').textContent = dirty ? '是' : '否';

  // 按钮可用性
  $('btnUndo').disabled = !canUndo();
  $('btnRedo').disabled = !canRedo();
}

/* 屏幕盒 CSS 尺寸与逻辑尺寸的比（用于叠加标记换算） */
function boxScaleFactor() {
  const r = $('screenBox').getBoundingClientRect();
  return { sx: r.width / state.screen.w, sy: r.height / state.screen.h };
}

function syncControls() {
  $('vpW').value = state.screen.w;
  $('vpH').value = state.screen.h;
  $('dpr').value = state.screen.dpr;
  $('focusX').value = state.config.focus_x.toFixed(2);
  $('focusY').value = state.config.focus_y.toFixed(2);
  $('modeCover').classList.toggle('active', state.config.mode === 'cover');
  $('modeContain').classList.toggle('active', state.config.mode === 'contain');
  $('rot0').classList.toggle('active', state.screen.rotation === 0);
  $('rot90').classList.toggle('active', state.screen.rotation === 90);
}

/* ---------- 设备 ---------- */
function fillDevices() {
  const sel = $('device');
  sel.innerHTML = '';
  for (const d of DEVICE_PRESETS) {
    const o = document.createElement('option');
    o.value = d.id; o.textContent = `${d.name} ${d.w}×${d.h}@${d.dpr}x`;
    sel.appendChild(o);
  }
  sel.value = 'phone-pro';
}
function applyDevice(id) {
  const d = DEVICE_PRESETS.find((x) => x.id === id);
  if (!d) return;
  state.screen.w = d.w; state.screen.h = d.h; state.screen.dpr = d.dpr;
  fitScreenBox();
  syncControls(); render();
}
function fitScreenBox() {
  const stage = $('stage').getBoundingClientRect();
  const maxH = stage.height - 80;
  const maxW = stage.width - 120;
  const s = Math.min(maxH / state.screen.h, maxW / state.screen.w, 1);
  const box = $('screenBox');
  box.style.width = state.screen.w * s + 'px';
  box.style.height = state.screen.h * s + 'px';
}

/* ---------- 历史 / 上报 ---------- */
async function loadHistory() {
  const { data } = await api('GET', '/api/layout/history');
  const el = $('history');
  el.innerHTML = '';
  (data?.history || []).slice().reverse().forEach((h) => {
    const div = document.createElement('div');
    div.className = 'history-item';
    div.innerHTML = `<span><code>v${h.version}</code>
      <span class="tag ${h.config.mode}">${h.config.mode}</span>
      ${h.config.image_id}</span>
      <span>${h.updated_at.replace('T', ' ').replace('Z', '')} · ${h.client}</span>`;
    el.appendChild(div);
  });
}
async function loadReports() {
  const { data } = await api('GET', '/api/reports/crop');
  const el = $('reports');
  el.innerHTML = '';
  (data?.reports || []).slice().reverse().slice(0, 8).forEach((r) => {
    const div = document.createElement('div');
    div.className = 'history-item';
    div.innerHTML = `<span><code>${r.device_id}</code> ${r.mode}
      crop(${r.crop.x},${r.crop.y},${r.crop.w},${r.crop.h})
      vp ${r.viewport_logical?.w}×${r.viewport_logical?.h} @${r.dpr}x rot${r.rotation}</span>
      <span>${r.received_at.replace('T', ' ').replace('Z', '')}</span>`;
    el.appendChild(div);
  });
}

/* ---------- 事件 ---------- */
function bind() {
  $('device').addEventListener('change', (e) => applyDevice(e.target.value));
  $('vpW').addEventListener('change', () => {
    state.screen.w = clampInt($('vpW').value, 80, 7680);
    fitScreenBox(); render();
  });
  $('vpH').addEventListener('change', () => {
    state.screen.h = clampInt($('vpH').value, 80, 4320);
    fitScreenBox(); render();
  });
  $('dpr').addEventListener('change', () => {
    state.screen.dpr = Math.min(8, Math.max(0.5, +$('dpr').value || 1));
    render();
  });
  $('rot0').addEventListener('click', () => { state.screen.rotation = 0; fitScreenBox(); syncControls(); render(); });
  $('rot90').addEventListener('click', () => { state.screen.rotation = 90; fitScreenBox(); syncControls(); render(); });

  $('modeCover').addEventListener('click', () => userChoice((c) => { c.mode = 'cover'; }));
  $('modeContain').addEventListener('click', () => userChoice((c) => { c.mode = 'contain'; }));
  $('focusX').addEventListener('change', () => userChoice((c) => {
    c.focus_x = Math.min(0.98, Math.max(0.02, +$('focusX').value));
  }));
  $('focusY').addEventListener('change', () => userChoice((c) => {
    c.focus_y = Math.min(0.98, Math.max(0.02, +$('focusY').value));
  }));

  $('btnUndo').addEventListener('click', undo);
  $('btnRedo').addEventListener('click', redo);
  $('btnSave').addEventListener('click', () => save(false));
  $('btnRefresh').addEventListener('click', () => loadRecord());
  $('btnTakeRemote').addEventListener('click', async () => {
    state.conflict = false; updateConflictUI();
    await loadRecord(); toast('已加载远程版本');
  });
  $('btnForceSave').addEventListener('click', () => save(true));
  $('btnLoadReports').addEventListener('click', loadReports);
  $('btnPublish').addEventListener('click', publishImage);

  // 焦点拖拽
  const box = $('screenBox');
  box.addEventListener('mousedown', (e) => {
    state.dragFocus = true;
    updateFocusFromEvent(e);
  });
  window.addEventListener('mouseup', () => { state.dragFocus = false; });
  box.addEventListener('mousemove', (e) => {
    const rect = box.getBoundingClientRect();
    const rot = state.screen.rotation;
    const drawW = rot === 90 || rot === 270 ? state.screen.h : state.screen.w;
    const drawH = rot === 90 || rot === 270 ? state.screen.w : state.screen.h;
    state.pointer = {
      x: (e.clientX - rect.left) / rect.width * drawW,
      y: (e.clientY - rect.top) / rect.height * drawH,
    };
    if (state.dragFocus) updateFocusFromEvent(e);
    render();
  });

  window.addEventListener('resize', () => { fitScreenBox(); render(); });
  // 后台轮询远程：检测到新版本且本地有未保存改动 -> 冲突条，不覆盖
  setInterval(pollRemote, 4000);
}

function clampInt(v, lo, hi) {
  v = parseInt(v, 10);
  if (isNaN(v)) return lo;
  return Math.min(hi, Math.max(lo, v));
}

function updateFocusFromEvent(e) {
  const rect = $('screenBox').getBoundingClientRect();
  const rot = state.screen.rotation;
  const drawW = rot === 90 || rot === 270 ? state.screen.h : state.screen.w;
  const drawH = rot === 90 || rot === 270 ? state.screen.w : state.screen.h;
  const t2 = resolveLayout(state.config, { w: drawW, h: drawH });
  const lx = (e.clientX - rect.left) / rect.width * drawW;
  const ly = (e.clientY - rect.top) / rect.height * drawH;
  const ip = affineApply(t2.logicalToImage, { x: lx, y: ly });
  const fx = Math.min(0.98, Math.max(0.02, ip.x / state.config.image_w));
  const fy = Math.min(0.98, Math.max(0.02, ip.y / state.config.image_h));
  userChoice((c) => { c.focus_x = fx; c.focus_y = fy; });
}

async function pollRemote() {
  const { status, data } = await api('GET', '/api/layout/latest');
  if (status !== 200 || !state.record) return;
  if (data.version > state.record.version) {
    const localDirty = JSON.stringify(state.config) !==
      JSON.stringify(state.record.config);
    if (localDirty || state.histHead > state.histBase) {
      state.conflict = true;
      $('conflictText').textContent =
        `远程有新版本 v${data.version}（本地基线 v${state.baseVersion}，有未保存选择）。` +
        `你的最终选择不会被迟到事件覆盖。`;
      updateConflictUI();
    } else {
      await loadRecord();
      toast(`已自动应用远程 v${data.version}`);
    }
  }
}

async function publishImage() {
  const input = $('imgFile');
  if (!input.files.length) { toast('先选择图片'); return; }
  const fd = new FormData();
  fd.append('image', input.files[0]);
  $('btnPublish').disabled = true;
  try {
    const res = await fetch('/api/images/publish', { method: 'POST', body: fd });
    const data = await res.json();
    if (res.ok) {
      toast(`图片已发布 v${data.version}`);
      await loadRecord();
    } else {
      toast('发布失败：' + data.error);
    }
  } catch (err) {
    toast('发布失败：' + err);
  } finally {
    $('btnPublish').disabled = false;
  }
}

(async function init() {
  fillDevices();
  bind();
  await loadRecord();
  applyScreenSizeFromPreset();
  loadReports();
  fitScreenBox();
  render();
})();
function applyScreenSizeFromPreset() { applyDevice($('device').value); }
