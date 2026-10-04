# 三套坐标系与变换（本系统唯一事实来源）

像素管线：

```
源图像像素 S (sx, sy) ──fit+focus──► 逻辑窗口 L (lx, ly) ──dpr+rotation──► 物理像素 P (px, py)
        原图分辨率 IW×IH                 设备无关 W×H 点(dp)                实际帧缓冲 Wp×Hp px
```

1. **源图像坐标 S**：PNG 像素，原点左上，范围 `0..IW, 0..IH`。
2. **逻辑窗口坐标 L**：设备无关点（dp / CSS 点），`W×H`，触摸事件统一用它做命中。
3. **物理像素坐标 P**：SDL 帧缓冲（drawable）像素，`Wp=W·dpr`、`Hp=H·dbr`（X11 下 dpr 一般 1；
   SDL 模拟值通过渲染缩放实现），再叠加屏幕旋转 `rot ∈ {0,90,180,270}`（顺时针）。

## 1. fit + focus：S→L

输入：源尺寸 `IW×IH`、逻辑视口 `W×H`、fit 模式、焦点 `fx,fy ∈ [0,1]`（源图归一化，默认 0.5）。

**stretch（旧"背景等比缩放"之外的兼容模式，会变形，仅显式选择）**
`scale = (W/IW, H/IH)`，`L = S ⊙ scale`，铺满无裁切。

**cover（铺满 / 充满视口，裁掉溢出）**
```
s  = max(W/IW, H/IH)
dw = IW*s, dh = IH*s                 # 内容在 L 中的绘制尺寸（dw>=W, dh>=H）
tx = fx*(W - dw)   （dw>W 时为负，焦点贴边时另一边缘被裁）
ty = fy*(H - dh)
```
- 内容矩形 `content = (tx,ty,dw,dh)`；露出（裁切）的源区域 = 视口反投影：
  `crop = SrcRect(-tx/s, -ty/s, W/s, H/s)`，与 `fx,fy` 联动并在边缘钳制。
- S→L：`lx = sx*s + tx`；L→S：`sx = (lx - tx)/s`。

**contain（完整显示，留黑边 letterbox）**
```
s = min(W/IW, H/IH)
dw = IW*s, dh = IH*s
# 焦点决定黑边如何分配；默认居中
offx = clamp((fx*IW*s - W/2)/(dw - W)) 当 W>dw(恒成立除非比例相等) —— 注意分母为负！
```
contain 下内容比视口小，对齐量应当让焦点一侧的空间尽可能小：内容应尽量放在**焦点对侧**。
`tx = fx*(W-dw)`，`ty = fy*(H-dh)`（与 cover 同一公式，符号自然正确）。
fx=fy=0.5 时 tx=(W-dw)/2、ty=(H-dh)/2 居中黑边；fx=0 时 tx=0 贴左。

- S→L：`lx = sx*s + tx`；可见源区域恒为整图 `(0,0,IW,IH)`；
  上报字段另含 `letterbox = {top:-ty, left:-tx, bottom:H-dh-ty, right:W-dw-tx}`。

## 2. dpr + rotation：L→P

```
L' = (lx*dpr, ly*dpr)   尺寸 (Wp, Hp) = (W*dpr, H*dpr)
```
旋转中心 `(Wp/2, Hp/2)`，顺时针 90° 步进：
```
rot=0:   P = L'
rot=90:  px = Hp - ly*dpr , py = lx*dpr        (帧缓冲为 Hp×Wp，交换 Wp/Hp)
rot=180: px = Wp - lx*dpr, py = Hp - ly*dpr
rot=270: px = ly*dpr      , py = Wp - lx*dpr
```
> 实现约定：帧缓冲按"旋转后物理尺寸"分配。`rot∈{90,270}` 时 drawable 尺寸为 `(Hp, Wp)`，
> 即逻辑宽高互换（横竖屏切换）。视口的逻辑 W,H 在切换时随之交换，使 L 坐标系始终自然正向。

## 3. 触摸命中

输入永远先取物理事件 `(px,py)`，走**同一条几何函数的逆变换** P→L→S，再与热区做包含测试。
禁止在渲染坐标与命中坐标处各写一份近似映射。热区用 S 坐标声明（与分辨率无关），
因此命中判定 = `map_L_to_S(map_P_to_L(p)) ∈ hotzone`。

## 4. 数值约定

- 几何全部 double 运算，仅在最终 `SDL_Rect` 处 round（`.5 取整方向固定为 floor(x+0.5)`）。
- 退化视口（W<=0 或 H<=0，极窄窗口）不产生 NaN：分母为 0 的项按"沿该轴 scale=1, offset=0"处理，
  并在上报中标记 `degenerate=true`。
- 上报裁切框统一使用**源图像归一化坐标** `[0,1]²`，避免不同设备像素口径不一致。

## 5. 报告（C 端 → 后端）

```json
{
  "device_id": "screen-A",
  "layout_version": 7,
  "image_rev": 3,
  "physical": [1080, 1920],
  "logical": [390, 844],
  "dpr": 2.769,
  "rotation": 0,
  "fit": "cover",
  "crop": {"x": 0.12, "y": 0.0, "w": 0.76, "h": 1.0},
  "focus": [0.5, 0.5],
  "letterbox": {"top": 0, "left": 0, "bottom": 0, "right": 0},
  "degenerate": false
}
```
`crop` 为当前视口在源图上实际露出的区域（contain 恒为整图）；
验收时以该报告 + 后端几何服务两种实现交叉核对，并与真实帧缓冲截图比对。
