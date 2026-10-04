#!/usr/bin/env python3
"""
screenshot_verify.py - 现场截图 vs 理论裁切框 的像素级比对。

流程：
  1) 用与 C(layout.c)/JS(geo.js) 同构的 crop_theory 解算理论裁切；
  2) 现场上报裁切必须与理论裁切一致（<=1px 舍入）；
  3) 从源图按理论 crop 裁切并等比缩放到视口，生成 expected.png；
  4) 截图归一化到视口尺寸后，与 expected 做逐像素 MAE/MSE/最大偏差比较；
  5) contain 额外校验留边区为黑。

注意：这是对"现场端真实截图"的断言，不是管理端预览。
"""
import argparse
import subprocess
import sys
import tempfile
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from crop_theory import resolve_layout, reported_crop  # noqa: E402


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"command failed: {' '.join(cmd)}\n{r.stderr}")
    return r.stdout


def img_size(path):
    w, h = run(["identify", "-format", "%w %h", path]).split()
    return int(w), int(h)


def make_expected(src, t, vp, mode, out_path):
    vw, vh = int(round(vp["w"])), int(round(vp["h"]))
    c = t["crop"]
    if mode == "cover":
        cw = max(1, int(round(c["w"])))
        ch = max(1, int(round(c["h"])))
        cx = int(round(c["x"]))
        cy = int(round(c["y"]))
        run(["convert", src,
             "-crop", f"{cw}x{ch}+{cx}+{cy}", "+repage",
             "-resize", f"{vw}x{vh}!", out_path])
    else:
        dw = max(1, int(round(t["target"]["w"])))
        dh = max(1, int(round(t["target"]["h"])))
        run(["convert", "-size", f"{vw}x{vh}", "xc:black",
             "(", src, "-resize", f"{dw}x{dh}!", ")",
             "-gravity", "center", "-composite", out_path])


def load_rgb(path):
    """返回 (w,h, bytes) 原始 RGB（ppm）。"""
    with tempfile.TemporaryDirectory() as td:
        ppm = os.path.join(td, "p.ppm")
        run(["convert", path, "-depth", "8", "rgb:" + ppm.replace("rgb:", "")]) \
            if False else None
        # 用 ppm 带头部更稳妥
        run(["convert", path, "-depth", "8", ppm])
        with open(ppm, "rb") as f:
            data = f.read()
    # P6 header: P6\n w h\n max\n
    idx = 0
    assert data[:2] == b"P6"
    fields = []
    i = 2
    while len(fields) < 3:
        while i < len(data) and data[i] in b" \t\r\n":
            i += 1
        j = i
        while j < len(data) and data[j] not in b" \t\r\n":
            j += 1
        fields.append(int(data[i:j]))
        i = j
    w, h, mx = fields
    while i < len(data) and data[i] not in b"\n\r":
        i += 1
    i += 1
    px = data[i:i + w * h * 3]
    return w, h, px, mx


def pixel_diff(a_path, b_path):
    """缩放到同尺寸后逐像素比较，返回 mae,mse,maxd,bad_ratio。"""
    wa, ha, pa, ma = load_rgb(a_path)
    wb, hb, pb, mb = load_rgb(b_path)
    if (wa, ha) != (wb, hb):
        # 把 b 归一到 a 的尺寸
        with tempfile.TemporaryDirectory() as td:
            norm = os.path.join(td, "n.ppm")
            run(["convert", b_path, "-resize", f"{wa}x{ha}!", norm])
            wb, hb, pb, mb = wa, ha, load_rgb(norm)[2], load_rgb(norm)[3]
    n = wa * ha
    sa = sb = 0
    maxd = 0
    bad = 0
    TOL = 12  # 容差（缩放算法/颜色空间抖动）
    for i in range(0, len(pa), 3):
        dr = pa[i] - pb[i]
        dg = pa[i + 1] - pb[i + 1]
        db = pa[i + 2] - pb[i + 2]
        ad = abs(dr) + abs(dg) + abs(db)
        sa += ad / 3
        sb += (dr * dr + dg * dg + db * db) / 3
        if ad // 3 > maxd:
            maxd = ad // 3
        if ad / 3 > TOL:
            bad += 1
    mae = sa / n
    mse = sb / n
    return mae, mse, maxd, bad / n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", required=True)
    ap.add_argument("--image", required=True)
    ap.add_argument("--mode", required=True, choices=["cover", "contain"])
    ap.add_argument("--vp", required=True)
    ap.add_argument("--focus", default="0.5,0.5")
    ap.add_argument("--rotation", type=int, default=0)
    ap.add_argument("--crop", required=True)
    ap.add_argument("--expected-out", default="/tmp/expected.png")
    ap.add_argument("--mae", type=float, default=4.0, help="平均绝对误差阈值(0-255)")
    ap.add_argument("--bad", type=float, default=0.02, help="超容差像素比例上限")
    args = ap.parse_args()

    iw, ih = img_size(args.image)
    vw, vh = (int(x) for x in args.vp.split("x"))
    fx, fy = (float(x) for x in args.focus.split(","))
    cx, cy, cw, ch = (int(x) for x in args.crop.split(","))

    cfg = {"image_w": iw, "image_h": ih, "mode": args.mode,
           "focus_x": fx, "focus_y": fy, "rotation": args.rotation}
    t = resolve_layout(cfg, {"w": vw, "h": vh})
    rc = reported_crop(t)
    problems = []

    reported = {"x": cx, "y": cy, "w": cw, "h": ch}
    for k in ("x", "y", "w", "h"):
        if abs(rc[k] - reported[k]) > 1:
            problems.append(f"reported crop.{k}={reported[k]} != theory {rc[k]}")
    print(f"theory crop   = {rc}")
    print(f"reported crop = {reported}")

    make_expected(args.image, t, {"w": vw, "h": vh}, args.mode, args.expected_out)

    sw, sh = img_size(args.shot)
    if (sw, sh) != (vw, vh):
        print(f"note: shot {sw}x{sh} != logical {vw}x{vh}（DPR/浏览器栏），归一化后逐像素比较")
    mae, mse, maxd, bad_ratio = pixel_diff(args.expected_out, args.shot)
    print(f"pixel diff: MAE={mae:.3f} MSE={mse:.1f} MAX={maxd} bad(>{12})={bad_ratio*100:.2f}%")
    if mae > args.mae:
        problems.append(f"MAE {mae:.2f} > {args.mae}（截图内容与理论裁切不符）")
    if bad_ratio > args.bad:
        problems.append(f"超容差像素 {bad_ratio*100:.1f}% > {args.bad*100:.0f}%")

    if args.mode == "contain":
        # 留边检查：expected 与 shot 的顶部 6% 行应都为黑
        with tempfile.TemporaryDirectory() as td:
            def row_black(img, frac=0.05):
                rw = os.path.join(td, "row.png")
                run(["convert", img, "-gravity", "north",
                     "-crop", f"100%x{max(2,int(vh*frac))}+0+0", "+repage",
                     "-colorspace", "Gray", "-depth", "8", rw])
                return float(run(["convert", rw, "-format",
                                  "%[fx:mean.r]", "info:"]).strip())
            if row_black(args.expected_out) > 0.05:
                problems.append("理论图顶部应为留边黑边")
            if row_black(args.shot) > 0.12:
                problems.append("现场截图顶部不是 contain 留边黑边")

    if problems:
        print("VERIFY FAIL:")
        for p in problems:
            print("  -", p)
        sys.exit(1)
    print(f"VERIFY OK: 现场截图与理论裁切逐像素一致 (mode={args.mode}, vp={vw}x{vh})")


if __name__ == "__main__":
    main()
