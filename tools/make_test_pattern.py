#!/usr/bin/env python3
"""生成带定位标记的测试图，便于从截图反查源图裁切坐标。
四角颜色不同，网格每 100px 一条，坐标文字烧录在图上。"""
import subprocess
import sys
import os

W, H = 1600, 1000
OUT = sys.argv[1] if len(sys.argv) > 1 else "assets/background.png"

cmds = ["convert", "-size", f"{W}x{H}", "xc:#101830"]
# 四角色块（各 220px）
corners = [
    (0, 0, 220, 220, "#e23b3b"),
    (W - 220, 0, W, 220, "#3bd24a"),
    (0, H - 220, 220, H, "#3b7ae2"),
    (W - 220, H - 220, W, H, "#e2c53b"),
]
for x0, y0, x1, y1, c in corners:
    cmds += ["-fill", c, "-draw", f"rectangle {x0},{y0} {x1},{y1}"]
# 网格
for x in range(0, W + 1, 100):
    cmds += ["-stroke", "#ffffff40", "-draw", f"line {x},0 {x},{H}"]
for y in range(0, H + 1, 100):
    cmds += ["-stroke", "#ffffff40", "-draw", f"line 0,{y} {W},{y}"]
# 十字中心线（亮白，最易定位）
cmds += ["-stroke", "#ffffff", "-strokewidth", "4",
         "-draw", f"line {W//2},0 {W//2},{H}",
         "-draw", f"line 0,{H//2} {W},{H//2}"]
# 用醒目的数字刻度色块代替文字（避免容器无字体）
for i, x in enumerate(range(200, W, 200)):
    col = "#ffffff" if i % 2 == 0 else "#9ad0ff"
    cmds += ["-fill", col, "-draw", f"rectangle {x-3},{H//2-18} {x+3},{H//2+18}"]
for i, y in enumerate(range(200, H, 200)):
    col = "#ffffff" if i % 2 == 0 else "#ffd09a"
    cmds += ["-fill", col, "-draw", f"rectangle {W//2-18},{y-3} {W//2+18},{y+3}"]
cmds.append(OUT)
os.makedirs(os.path.dirname(OUT), exist_ok=True)
subprocess.run(cmds, check=True)
print(f"wrote {OUT} ({W}x{H})")
