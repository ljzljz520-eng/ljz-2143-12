#!/usr/bin/env python3
"""生成"理论帧"（物理像素 PNG）：独立于 C/JS 的第四套实现（Python 参考几何）。
逐物理像素：P -> L -> S 最近邻采样源图，视口外/黑边为黑色。
用法: theoretical_frame.py src.png W H dpr rotation fit fx fy out.png
"""
import os, struct, subprocess, sys, zlib
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "server"))
from geometry import Geometry

def read_png(path):
    with open(path, "rb") as f:
        head = f.read(24)
    w, h = struct.unpack(">II", head[16:24])
    raw = subprocess.run(["/usr/bin/convert", path, "rgba:-"],
                         capture_output=True, check=True).stdout
    return w, h, raw

def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)
    def ch(t, d):
        return (struct.pack(">I", len(d)) + t + d +
                struct.pack(">I", zlib.crc32(t + d) & 0xffffffff))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", ihdr) +
                ch(b"IDAT", zlib.compress(raw, 9)) + ch(b"IEND", b""))

def main():
    srcp, W, H, dpr, rot, fit, fx, fy, outp = sys.argv[1:10]
    W, H, dpr, rot = int(W), int(H), float(dpr), int(rot)
    fx, fy = float(fx), float(fy)
    iw, ih, src = read_png(srcp)
    g = Geometry(iw, ih, W, H, dpr, rot, fit, fx, fy).compute()
    PW, PH = g.phys_w, g.phys_h
    canvas = bytearray(PW * PH * 4)  # 全黑

    def sample(sx, sy):
        x = int(sx + 0.5); y = int(sy + 0.5)
        if x < 0: x = 0
        if y < 0: y = 0
        if x > iw - 1: x = iw - 1
        if y > ih - 1: y = ih - 1
        o = (y * iw + x) * 4
        return src[o:o+4]

    for py in range(PH):
        rowbase = py * PW * 4
        for px in range(PW):
            lx, ly = g.phys_to_logical(px + 0.5, py + 0.5)
            if (g.tx <= lx <= g.tx + g.content_w and
                    g.ty <= ly <= g.ty + g.content_h):
                sx = (lx - g.tx) / g.scale_x
                sy = (ly - g.ty) / g.scale_y
                if -0.5 <= sx < iw and -0.5 <= sy < ih:
                    canvas[rowbase + px*4: rowbase + px*4 + 4] = sample(sx, sy)
    rows = [bytes(canvas[y*PW*4:(y+1)*PW*4]) for y in range(PH)]
    write_png(outp, PW, PH, rows)
    print("theoretical %dx%d crop=%s -> %s" %
          (PW, PH, [round(v, 6) for v in g.normalized_crop()], outp))

if __name__ == "__main__":
    main()
