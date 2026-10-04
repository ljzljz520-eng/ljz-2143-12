#!/usr/bin/env python3
"""对比"展示终端真实截图(xwd 根窗口)"与"理论帧"。
自动定位应用窗口（非纯黑的最大矩形），比较其内部像素：
  - RMSE 差异率、AE 不匹配像素
  - 叠加可视化 diff PNG
"""
import argparse, struct, subprocess, sys, tempfile, os

def png_size(head):
    return struct.unpack(">II", head[16:24])

def load_rgba(path):
    if path.endswith(".xwd"):
        src = "xwd:" + path
    else:
        src = path
    r = subprocess.run(["/usr/bin/convert", src, "rgba:-"], capture_output=True)
    raw = r.stdout
    if not path.endswith(".xwd"):
        with open(path, "rb") as f:
            head = f.read(24)
        w, h = struct.unpack(">II", head[16:24])
    if path.endswith(".xwd"):
        # XWDFileHeader 大端：pixmap_width=header[4] @offset16, height=[5] @offset20
        with open(path, "rb") as f:
            hdr = f.read(24)
        w = struct.unpack(">I", hdr[16:20])[0]
        h = struct.unpack(">I", hdr[20:24])[0]
    return w, h, raw

def find_bounds(w, h, raw):
    minx,miny,maxx,maxy=w,h,-1,-1
    for y in range(0,h,2):
        base=y*w*4
        for x in range(0,w,2):
            o=base+x*4
            if max(raw[o],raw[o+1],raw[o+2])>40:
                if x<minx:minx=x
                if x>maxx:maxx=x
                if y<miny:miny=y
                if y>maxy:maxy=y
    if maxx<0: return None
    return minx,miny,maxx-minx+1,maxy-miny+1

def rgba_png(path,w,h,raw,crop=None):
    if crop:
        x,y,cw,ch=crop
        buf=bytearray(cw*ch*4)
        for ry in range(ch):
            s=((y+ry)*w+x)*4
            buf[ry*cw*4:(ry+1)*cw*4]=raw[s:s+cw*4]
        raw,w,h=bytes(buf),cw,ch
    def ch(t,d):
        import zlib
        return struct.pack(">I",len(d))+t+d+struct.pack(">I",zlib.crc32(t+d)&0xffffffff)
    ihdr=struct.pack(">IIBBBBB",w,h,8,6,0,0,0)
    rows=b"".join(b"\x00"+raw[y*w*4:(y+1)*w*4] for y in range(h))
    import zlib
    open(path,"wb").write(b"\x89PNG\r\n\x1a\n"+ch(b"IHDR",ihdr)+
                          ch(b"IDAT",zlib.compress(rows,6))+ch(b"IEND",b""))

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("shot"); ap.add_argument("theoretical")
    ap.add_argument("--max-diff",type=float,default=0.04)
    ap.add_argument("--vis",default="")
    ap.add_argument("--marker",default="",
                    help="shot marker 文件，内含 'window x y w h'；给定时用它精确定位窗口")
    args=ap.parse_args()
    sw,sh,sraw=load_rgba(args.shot)
    tw,th,traw=load_rgba(args.theoretical)
    print(f"framebuffer {sw}x{sh}, theoretical {tw}x{th}")
    if args.marker and os.path.exists(args.marker):
        b=None
        for line in open(args.marker):
            if line.startswith("window "):
                _,mx,my,mw,mh=line.split()
                b=(int(mx),int(my),int(mw),int(mh)); break
        if not b:
            print("ERROR: marker 中没有 window 行"); sys.exit(2)
        print(f"window bounds (marker): x={b[0]} y={b[1]} {b[2]}x{b[3]}")
    else:
        b=find_bounds(sw,sh,sraw)
        if not b:
            print("ERROR: 无法定位展示窗口"); sys.exit(2)
        print(f"window bounds (luma): x={b[0]} y={b[1]} {b[2]}x{b[3]}")
    x,y,ww,hh=b
    if abs(ww-tw)>3 or abs(hh-th)>3:
        print(f"ERROR: 窗口 {ww}x{hh} 与理论帧 {tw}x{th} 尺寸不一致"); sys.exit(2)
    winpng=os.path.join(tempfile.gettempdir(),"e2e-win.png")
    rgba_png(winpng,sw,sh,sraw,crop=(x,y,ww,hh))
    # 统一尺寸（窗口含 1px 装饰边）：理论帧缩放到窗口内尺寸
    norm=os.path.join(tempfile.gettempdir(),"e2e-norm.png")
    subprocess.run(["/usr/bin/convert",args.theoretical,"-resize",f"{ww}x{hh}!",norm],check=True)
    vis=args.vis or os.path.join(tempfile.gettempdir(),"e2e-diff.png")
    r=subprocess.run(["/usr/bin/compare","-metric","RMSE",winpng,norm,vis],
                     capture_output=True,text=True)
    metric=(r.stderr or r.stdout).strip()
    print("RMSE:",metric)
    try: rate=float(metric.split("(")[1].rstrip(")"))
    except Exception: rate=1.0
    r2=subprocess.run(["/usr/bin/compare","-metric","AE","-fuzz","10%",winpng,norm,"null:"],
                      capture_output=True,text=True)
    print("AE(fuzz10%):",(r2.stderr or r2.stdout).strip())
    ok=rate<=args.max_diff
    print("RESULT:", "PASS" if ok else f"FAIL diff {rate:.4f} > {args.max_diff}")
    print("diff map:",vis)
    sys.exit(0 if ok else 1)

if __name__=="__main__":
    main()
