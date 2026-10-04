# 验收协议：以展示终端真实像素为准

> 原则：**不能只靠管理端预览断言现场显示正确**。管理端（`web/`）只是编辑工具，
> 它用的几何实现（`web/geometry.js`）与现场 C 端是两套代码，可能各自出错也可能同时错。
> 验收必须看"展示终端真实帧缓冲 + 后端入库的真实上报"。

## 参与的四套独立实现

| 角色 | 文件 | 用途 |
|------|------|------|
| C 展示端真实渲染 | `src/geometry.c`, `src/scene.c` | 现场像素 |
| 浏览器管理端预览 | `web/geometry.js` | 编辑辅助（不作验收依据） |
| 后端参考实现 | `server/geometry.py` | 校验上报、生成理论帧 |
| C/JS 单元测试 | `tests/unit/` | 三套实现数值交叉一致 |

三者同口径但独立编写，任何两套不一致都会被单测/E2E 抓到。

## E2E 场景（`tests/e2e/e2e_runner.py`）

1. **cover/contain**：真实窗口 xwd 抓帧 vs Python 理论帧，RMSE/AE 双指标；
   上报 crop、黑边与理论值比对（contain 像素差异 0）。
2. **横竖屏 90° + 高 DPI**：面板物理尺寸固定、UI 视口交换；
   `physical/logical/dpr/rotation` 上报正确；触摸命中走同一逆变换（中心 HIT、角落 MISS）。
3. **极窄窗口 + 跨显示器移动**：8px 宽窗口标记 `degenerate` 安全上报、无 NaN/崩溃；
   恢复后像素与理论帧一致。
4. **快速拖窗 burst**：200ms 内 40 个尺寸事件合并为最终一次重绘；
   上报条数被防抖合并（≤6），最终上报尺寸=拖动终点。
5. **缩放过程中后台发布新图**：sim burst 中触发 `/api/test/publish`，
   截图与新图 rev2 一致、与旧图显著不同；`image_rev=2` 而 fit 保持 cover。
6. **配置保存的最终选择**：远程在 burst 前保存 cover→contain，
   burst 结束后截图仍是 contain（像素差异 0），最终上报 fit=contain 且尺寸为终点。

## 像素比对方法（`scripts/compare_frames.py`）

1. `xwd -root` 抓 **Xvfb 根窗口**（=展示终端帧缓冲），不是管理端 canvas。
2. 窗口几何取自 C 端 shot marker（`window x y w h`），不靠亮度猜（contain 黑边会误导）。
3. 理论帧由 `scripts/theoretical_frame.py` 逐物理像素 `P→L→S` 最近邻采样生成。
4. ImageMagick `compare -metric RMSE/AE` 输出差异率与差异图 `test-results/*-diff.png`。
5. 测试图（`scripts/make_pattern.py`）用四个大面积纯色象限：色块内部必须 0 差异，
   仅象限交界的 1px 抗锯齿可容忍（阈值 RMSE ≤ 0.04，旋转 ≤ 0.05）。

## 运行

```bash
# 一次性准备无 root 环境（CI 镜像里已有 SDL/Xvfb 可跳过）
make SDL_PREFIX=/tmp/local/usr            # 编译 C 端
make unit SDL_PREFIX=/tmp/local/usr       # C 单测（303 断言）
python3 -m unittest discover -s server    # 后端版本/撤销/上报测试
node tests/unit/geometry.test.js          # JS 镜像测试
python3 tests/e2e/e2e_runner.py           # 真机像素 E2E（需要 /tmp/Xvfb-patched）
```
