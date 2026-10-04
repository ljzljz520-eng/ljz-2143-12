# 布局调试台（Layout Debug Workbench）

在原有"背景图等比缩放"基础上改造成的一套完整布局调试能力：

- **浏览器管理调试台**：模拟手机/平板/桌面/极窄屏、DPR、横竖屏，切换
  **铺满 cover / 完整 contain**，拖拽焦点，预览理论裁切框。
- **后端版本服务**（Python 标准库）：配置校验、**乐观锁入库**、版本历史、
  裁切上报收集、图片原子发布。
- **C(SDL2) 展示端**：按后端配置渲染，高 DPI / 旋转 / 焦点区域，
  纹理缓存（预生成分档 + 运行时缩放），上报真实裁切。
- **Web 展示终端**：同一套几何（`web/geo.js`），按配置渲染并上报。
- **三套坐标**（源图像 / 逻辑窗口 / 物理像素）严格互逆，触摸命中与视觉一致。
- **验收**：截"现场端"屏幕，与理论裁切框**逐像素比对**——不靠管理端预览自证。

## 目录

```text
src/                C 展示端
  geometry.[ch]     三套坐标 / 仿射 / 旋转 / DPR（纯逻辑，可脱离 SDL 单测）
  layout.[ch]       cover/contain 求解、焦点、命中、编辑会话(撤销基线)
  layout_store.[ch] 配置<->JSON、保存/上报报文
  json.[ch]         零依赖 JSON
  http_client.[ch]  POSIX HTTP/1.1（超时）
  texture_cache.[ch] 分档纹理 + LRU + 显存预算
  renderer.[ch]     逻辑视口->物理渲染、旋转、调试覆盖层
  window.[ch]       DPR / 旋转 / 逻辑视口 / 可缩放窗口
  client_api.[ch]   拉取/乐观锁保存/上报
  app_config.[ch]   环境变量
  test_core.c       纯逻辑单元测试
server/             Python 后端（无第三方依赖）
  layout_server.py  版本/校验/发布/上报/静态服务
  image_meta.py     PNG/JPEG 尺寸读取
web/
  admin.html app.js     管理调试台
  display.html display.js Web 现场展示终端
  geo.js                与 C 严格同构的几何
  style.css
tools/
  make_test_pattern.py  生成定位测试图
  crop_theory.py        第三套独立几何（Python），用于验收
  screenshot_verify.py  现场截图 vs 理论裁切 逐像素比对
  acceptance.py         端到端验收（Xvfb+xdotool）
  geo_parity.js         JS 几何对拍
docs/design.md      坐标变换/显存预算/冲突协议/方案比较
```

## 一键启动（容器）

```bash
docker compose up --build
# C 展示端(noVNC) : http://localhost:6080/vnc.html
# 管理调试台      : http://localhost:8080/admin.html
# Web 展示终端    : http://localhost:8080/display.html
```

> noVNC 只是远程显示容器里真实的 C/SDL 窗口；布局后端是独立 HTTP 服务（8080）。

## C 展示端快捷键

| 键 | 作用 |
|---|---|
| `m` | 切换 cover / contain（用户选择，带代际保护） |
| `r` | 旋转 0/90/180/270 |
| `d` | 模拟跨显示器 DPR：1 → 1.5 → 2 → 3 |
| 方向键 / `f` | 调焦点 / 焦点归中 |
| `s` | 保存（冲突时变为"强制保存"） |
| `u` / `i` | 撤销 / 重做（不越过本次编辑基础） |
| `g` | 放弃本地改动，加载远程新版本 |
| `q` | 忽略远程更新，继续用本地选择 |
| `F1` | 调试覆盖层（视口框/目标矩形/焦点/命中点） |

环境变量：`LAYOUT_SERVER_HOST/PORT`、`LAYOUT_DEVICE_ID`、`LAYOUT_VRAM_BUDGET`（如 `64M`）、
`LAYOUT_BORDERLESS=1`、`LAYOUT_DEFAULT_IMAGE`。

## 本地开发

```bash
make test          # 纯 C 逻辑单测（不需要 SDL）+ JS 几何对拍
make               # 需要 libsdl2-dev / libsdl2-image-dev
python3 server/layout_server.py
```

## 验收

```bash
# 容器内（或装好 Xvfb/xdotool/imagemagick/SDL 的机器）
make acceptance
```

验收覆盖并逐条断言：

1. 横屏 / 竖屏切换后的 cover 裁切；
2. 跨显示器移动的 DPR 变化（逻辑裁切不变、dpr 上报变化）；
3. 极窄窗口裁切不越界；
4. contain 留黑边；
5. **快速拖动窗口合并重绘，用户模式选择不被迟到尺寸事件覆盖**；
6. 撤销到本次编辑基础后拒绝再撤销；
7. 远程更新与本地未保存选择的 409 冲突提示、加载远程/强制保存；
8. 后台在"缩放/拖动中"发布新图，现场端原子换图并上报新图；
9. **现场窗口截图与理论裁切框逐像素一致**（不是管理端预览）；
10. Web 展示终端按配置渲染并上报（有 chromium 时同样真截图比对）。

## 后端 API

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/layout/latest` | 当前版本+配置 |
| PUT | `/api/layout` | body 带 `base_version`；冲突返回 `409` |
| GET | `/api/layout/history` | 最近 50 个版本 |
| POST | `/api/reports/crop` | 现场端上报真实裁切 |
| GET | `/api/reports/crop` | 读取上报（验收用） |
| POST | `/api/images/publish` | multipart 发布新背景图（原子 rename） |
| GET | `/assets/<id>` | 图片 |

详见 `docs/design.md`。
