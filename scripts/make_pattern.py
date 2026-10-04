#!/usr/bin/env python3
"""生成可像素验证的测试图：四象限高对比色 + 中线 + 四角标记。
用法: make_pattern.py W H out.png [rev]
rev>=2 时换一套配色（"后台在缩放中发布新图"验收用）。"""
import sys, struct, zlib

def png(w,h,rgba_rows):
    raw=b''.join(b'\x00'+row for row in rgba_rows)
    def chunk(t,d):
        return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    ihdr=struct.pack('>IIBBBBB',w,h,8,6,0,0,0)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',ihdr)+chunk(b'IDAT',zlib.compress(raw,9))+chunk(b'IEND',b'')

w,h=int(sys.argv[1]),int(sys.argv[2])
out=sys.argv[3]
rev=int(sys.argv[4]) if len(sys.argv)>4 else 1
# rev1 鲜明色；rev2 换色板，确保截图能区分新旧图
palette1=[(230,40,40,255),(40,200,60,255),(40,90,230,255),(240,220,40,255)]
palette2=[(230,80,220,255),(40,220,220,255),(240,150,40,255),(120,80,240,255)]
pal=palette1 if rev==1 else palette2
rows=[]
for y in range(h):
    row=bytearray(w*4)
    for x in range(w):
        qi=(1 if x>=w/2 else 0)+(2 if y>=h/2 else 0)
        r,g,b,a=pal[qi]
        o=x*4
        row[o:o+4]=bytes((r,g,b,a))
    rows.append(bytes(row))
open(out,'wb').write(png(w,h,rows))
print(f"wrote {out} {w}x{h} rev{rev}")
