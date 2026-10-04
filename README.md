# 布局调试与多屏展示系统（C/SDL2 + Python 后端 + 浏览器调试台）

由原"背景图等比缩放窗口"改造而来的完整布局调试链路：

- **浏览器调试台**模拟多种屏幕（尺寸/DPI/旋转），设置铺满（cover）/完整显示（contain）/拉伸；
- **后端**校验布局版本（乐观锁）、全量版本入库、按编辑会话基线撤销、接收真实裁切上报；
- **C 展示端**按版本配置渲染、轮询热更、上报实际裁切区域；
- 三套坐标（**源图像 S → 逻辑窗口 L → 物理像素 P**）变换统一处理高 DPI、屏幕旋转、焦点区域，
  触摸命中与视觉位置走同一条逆映射；
- 快速拖动窗口**合并重绘**，但**配置保存保留用户最终选择**，迟到尺寸事件不能覆盖新模式；
- 撤销**只作用于本次编辑会话基线**，与远程更新冲突时显式提示；
- 验收用**展示终端真实帧缓冲截图**与独立参考实现的理论裁切框逐像素比对，
  **不靠管理端预览自证**。

## 架构

```
浏览器调试台 web/                 后端 server/                    C 展示端 src/
  多屏/DPI/旋转模拟   ──POST──►  乐观锁版本校验 SQLite  ──轮询──► 按版本渲染
  cover/contain/焦点              全量版本流水 layout_vers          几何 S/L/P
  冲突提示/会话撤销  ──undo──►    会话基线 edit_sessions           事件合并+epoch
  查看现场上报       ◄──reports──  真实裁切入库 reports   ◄─上报──  旋转/DPI/命中
                                       ▲
                                理论帧/校验 geometry.py（第四套独立实现）
```

## 三套坐标（详见 docs/geometry.md）

- **S 源图像像素**（原图 IW×IH）
- **L 逻辑窗口**（设备无关 dp，W×H；触摸事件统一在 L 命中）
- **P 物理帧缓冲像素**（Wp=W·dpr；rot 90/270 时宽高交换）

`fit + focus` 统一公式：`tx = fx*(W - content_w)`，cover 裁切、contain 黑边都由它给出；
逆变换 `P→L→S` 用于触摸命中和上报，保证"看到的点"就是"点中的点"。

## 目录

```
src/            C 展示端
  main.c          事件循环/轮询/配置代际
  geometry.c      三套坐标变换（cover/contain/stretch、焦点、旋转、DPI、退化）
  events.c        尺寸事件合并 + 配置 epoch 防护
  texcache.c      多尺寸纹理缓存（LRU + 字节/条数预算 + pin）
  rotate_blit.c   软件渲染器下像素级 90/270 旋转
  scene.c         逻辑视口 target → 旋转 → 物理帧缓冲
  http_client.c   零依赖 POSIX HTTP（chunked）
  reporter.c      布局轮询/图片下载/裁切上报（防抖+退化强报）
  image_png.c     libpng 解码为 ARGB
  sim.c           E2E 事件注入（尺寸 burst/旋转/点击/发布钩子）
server/         纯标准库 Python HTTP + SQLite
  app.py store.py geometry.py  + test_*.py
web/            浏览器调试台（index.html/app.js/geometry.js/events.js）
scripts/        测试图生成、理论帧、截图比对、Xvfb 环境
tests/unit      C / JS 单元测试；tests/e2e 真机像素验收
docs/           geometry.md / texture-strategy.md / acceptance.md
```

## API

| 方法 | 路径 | 说明 |
|------|------|------|
| GET  | `/api/layout?device_id=...&since=N` | 拉取当前版本（N 之后才返回 data） |
| POST | `/api/layout` | 保存，body 带 `expected_version`；冲突返回 409 + server_version |
| POST | `/api/session` | 打开编辑会话，记录撤销基线 base_version |
| POST | `/api/undo` | 回到会话内上一用户版本；越过基线返回 409 |
| POST | `/api/images` / `/api/test/publish` | 上传/（测试钩子）发布新图，image_rev++ 但模式不动 |
| GET  | `/api/images/current` | 当前图片（带 X-Image-Rev） |
| POST | `/api/reports` | C 端上报真实裁切（归一化 crop、letterbox、dpr、rotation…） |
| GET  | `/api/reports` / `/api/history` | 现场上报/版本流水 |

## 构建与运行

```bash
# 标准环境（Debian + libsdl2-dev + libpng）
make
./visual-window-app --host 127.0.0.1 --port 8080
PORT=8080 python3 server/app.py          # 后端
# 浏览器开 http://localhost:8080

# 离线（无后端，本地配置+图片+上报 JSONL）
./visual-window-app --config layout.json --image assets/background.png \
                    --report-file reports.jsonl --script sim.txt

# 无 root 沙箱/CI：本地前缀
make SDL_PREFIX=/tmp/local/usr
```

## 测试

```bash
make unit SDL_PREFIX=/tmp/local/usr      # 303 C 断言（几何/事件/JSON/布局/纹理缓存）
python3 -m unittest discover -s server   # 12 后端测试（乐观锁/会话撤销/发布/HTTP）
node tests/unit/geometry.test.js         # JS 几何镜像 40 断言
python3 tests/e2e/e2e_runner.py          # 6 场景 24 项真机像素验收
```

## 关键设计约束（防回归）

1. 几何只有一个事实来源，渲染与命中**必须**共用 `geometry.c`；JS/Python 为镜像实现。
2. 尺寸事件携带 epoch；`commit_config` 后在途旧事件一律丢弃（C 单测 + 场景6 覆盖）。
3. 撤销记录每会话 `undo_cursor`，连续撤销不重复作用于 undo 记录，且不越过 base。
4. 发布新图只动 `image_rev` 与纹理缓存，不改 fit/focus/strategy。
5. 像素验收阈值：纯色块内部必须 0 差异，仅允许象限交界 1px 抗锯齿。
