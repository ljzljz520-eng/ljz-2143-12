# 布局调试能力 — 设计说明

## 1. 三套坐标系

| 坐标系 | 单位 | 原点 | 尺寸来源 | 用途 |
|---|---|---|---|---|
| 源图像 `image` | 图像像素 | 图左上 | 图片真实像素（后端读取文件头校正） | 裁切区域、纹理坐标 |
| 逻辑窗口 `logical` | DIP（与设备无关像素） | 视口左上 | 物理像素 / DPR（旋转不交换定义） | 布局求解、UI、命中测试 |
| 物理像素 `physical` | framebuffer 像素 | 表面左上 | SDL output size / canvas backing store | 实际绘制、触摸原始坐标 |

定义方向：x 向右、y 向下。

### 1.1 正变换（逻辑 → 物理）

`GSize logical`、`dpr`、`rotation ∈ {0,90,180,270}`：

```
0   : (x,y)        -> (dpr·x, dpr·y)
90  : (x,y)        -> (dpr·(Hl-y), dpr·x)
180 : (x,y)        -> (dpr·(Wl-x), dpr·(Hl-y))
270 : (x,y)        -> (dpr·y, dpr·(Wl-x))
```

实现为 2×3 仿射 `GAffine`（`geometry.c`、`geo.js` 同构），物理→逻辑是其矩阵求逆。
所有正/逆变换在单元测试里逐角度×逐 DPR 做往返断言，保证：

> 视觉位置（内容画在哪）与触摸命中（点回映到源图像）严格闭合。

### 1.2 布局求解（图像 → 逻辑窗口）

设源图 `iw×ih`，视口 `vw×vh`：

- 铺满 `cover`：`scale = max(vw/iw, vh/ih)`，图像完全覆盖视口，超出部分裁切。
  视口在源图上覆盖 `cw = vw/scale`、`ch = vh/scale`，裁切起点由焦点决定：
  `cx = focus_x·(iw-cw)`，`cy = focus_y·(ih-ch)`。
  目标矩形 `target = (-cx·scale, -cy·scale, iw·scale, ih·scale)`。
- 完整 `contain`：`scale = min(...)`，整图可见，视口内留黑边（letterbox），
  目标矩形居中；裁切矩形即整张源图。

`image_to_logical = scale·p + target.tl`；命中测试用其逆矩阵，落源图外即 MISS
（cover 下只有视口外，contain 下包括留边区）。

### 1.3 高 DPI

- SDL：`SDL_RenderSetLogicalSize(logical_w, logical_h)` 统一把逻辑坐标缩放到
  output surface（`SDL_GetRendererOutputSize`）。
- Web：`canvas.width = CSS·devicePixelRatio`，`ctx.setTransform(dpr,…)`。
- 纹理按**物理目标宽**选档（逻辑宽 × dpr），因此高 DPI 下拿的是更清晰的一档，
  但 cover/contain 逻辑裁切只由逻辑视口决定——DPR 变化不改变上报的源图裁切。

### 1.4 旋转

- C 端渲染：`RenderCopyEx` 的 `dstrect` 始终是**未旋转**的目标矩形，旋转轴心为
  `视口中心 − 目标矩形左上`。旋转后四角等于 `logical_to_physical` 仿射结果。
- 事件：鼠标/触摸点先归算到物理表面，再用逆仿射回逻辑点做命中。
- Web：`screen.orientation.angle` + 画布旋转；横竖屏切换后 innerWidth/Height 已交换。

## 2. 纹理方案：预生成多尺寸 vs 运行时缩放

| 维度 | 预生成多尺寸纹理 | 运行时缩放（直接 draw 源纹理） |
|---|---|---|
| 每帧 GPU 成本 | 几乎只做一次拷贝（1:1 或轻微缩放） | 每帧全图双线性/ mip 采样，4K 源 + 多屏时成本高 |
| 显存 | 占用固定、可预估 | 最低（仅源纹理） |
| 质量 | 选"≥目标宽的最小档"，避免放大糊 | 持续缩小走 GPU 双线性，质量取决于实现 |
| 首次加载 | 启动时一次性分档生成（有尖峰） | 无尖峰 |
| 极端比例/突发尺寸 | 可能没有合适档 | 永远可用 |

**采用混合策略**（`texture_cache.c`）：

1. 源纹理常驻（pinned，永不淘汰）。
2. 预生成分档（默认宽 4096/2560/1920/1280/960/640，不超过源宽才生成）。
3. 请求时取 ≥ 物理目标宽的最小档；没有合适档则从源纹理运行时缩放一条
   `TEX_KIND_RUNTIME` 入缓存；单条超预算或无空间时回退到直接缩放源纹理（保证可用）。

### 2.1 显存预算与淘汰

- 预算：`LAYOUT_VRAM_BUDGET`，默认 **96 MiB**（RGBA8888：`w·h·4`），可按设备下调（验收用 64M）。
- 淘汰：LRU（`last_used` 帧时钟）。需要空间时**先淘汰运行时条目，再淘汰预生成分档**；
  源纹理 pinned 永不淘汰。条目数有硬上限（24）。
- 预算约束发生在"生成前"，不会先超再回收。

## 3. 版本化、乐观锁与入库

- 后端 `data/state.json`：`{version, config, checksum, updated_at, history[]}`。
  保存时**整文件原子写**（临时文件 + fsync + os.replace）。
- `PUT /api/layout` 必须带 `base_version`：
  - 与当前一致 → 校验 config → version+1 → 入 history；
  - 不一致 → `409` + `current_version`，**绝不覆盖**。
- 校验：mode/rotation/focus/dpr 合法；图片必须存在；`image_w/h` 后端读取
  PNG/JPEG 文件头为准并自动纠正，防止伪造或过期尺寸入库。
- 图片发布 `POST /api/images/publish`：落盘用 `.part → fsync → rename`，
  展示端在缩放/拖动中拿到的要么旧图要么新图，不会读到半成品。

## 4. 事件合并与"最终选择不被覆盖"

两条独立通道，不能混：

1. **尺寸通道**：`SIZE_CHANGED/RESIZED`（或浏览器 resize）只置 `dirty`/rAF 调度，
   快速拖动的事件风暴在一帧内抽干，合并成一次重绘；上报再做 ~400ms 静止去抖。
   尺寸事件**永远不写 config.mode/focus**。
2. **选择通道**：用户切换 cover/contain、拖焦点等显式动作带单调递增 `choice_seq`，
   压入本次编辑会话栈；保存序列化的是当前用户配置。

> 任何迟到的尺寸事件都无法把新模式改回去——这是代码结构保证，而不是时序巧合。

## 5. 撤销/重做与远程更新冲突

- 编辑会话是线性栈，`stack[0]` 恒为**本次编辑基础 baseline**（打开编辑器/拉取/
  一次成功保存之后建立）。
- undo 下限是 baseline，**不跨编辑会话**；redo 分支在新动作后立即作废。
- 后台轮询到更高 `version`：
  - 本地无未保存改动 → 自动应用并换图；
  - 本地有未保存选择 → 仅弹冲突条/打日志，本地选择保持不变。用户显式二选一：
    **加载远程（放弃本地）** 或 **强制保存（以服务端最新版本为基线重试，提交本地内容）**。
- C 端按键：`m/r/d/方向键/s/u/i/g/q/F1`；Web 端按钮一致。

## 6. 裁切上报

展示端（C 与 Web）在配置/视口/DPR/旋转变化后上报：

```json
{ "device_id","image_id","mode",
  "crop": {"x","y","w","h"},          // 源图像整数像素
  "viewport_logical": {"w","h"}, "dpr", "rotation" }
```

C 端退出前强制再报一次最终裁切。后端追加写 `data/reports.jsonl`。

## 7. 验收方法（为什么不能只看管理端预览）

管理端预览用的是浏览器内同构几何，只能证明"管理端怎么想"；现场端真实渲染受
SDL 渲染器、DPR、窗口管理器、旋转、纹理缓存路径影响。因此验收是：

1. `make test`：C 纯逻辑单测 + JS 几何对拍（三端同构）。
2. `make acceptance`（容器内）：拉起 Xvfb/openbox/后端/C 端，用 xdotool 驱动
   横竖屏切换、跨显示器 DPR、极窄窗口、缩放中发布、快速拖动；
   用 ImageMagick `import -window` 截**现场端窗口**。
3. `tools/screenshot_verify.py`：用独立的 Python 同构几何算理论裁切，
   从源图裁切+缩放生成 expected，再与现场截图**逐像素 MAE / 超容差像素比例**比对，
   同时核对"上报裁切 == 理论裁切（≤1px）"。
4. Web 展示终端：有 headless chromium 时走真截图同一套比对；否则跑 Node 契约
   （geo.js 渲染几何 + 真实 POST 上报 + 读回校验）。

场景覆盖：横屏 cover、竖屏 cover、极窄 cover、方屏 contain 留边、DPR 1.5/2/3、
旋转、后台缩放中发布新图、快速 resize 保模式、撤销越界拒绝、远程 409 冲突与强制保存。
