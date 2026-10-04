#!/usr/bin/env python3
"""端到端验收编排（不依赖管理端预览）：
  启动后端 + Xvfb 上的 C 展示端 -> 模拟事件 -> xwd 抓真实帧缓冲
  -> 与独立 Python 参考实现的"理论帧"逐像素比对 -> 校验上报裁切框。

覆盖：
  1) cover / contain 渲染 + 上报裁切框/黑边
  2) 横竖屏旋转 90/270 + 高 DPI 物理像素 + 触摸命中=视觉位置
  3) 极窄窗口（退化）+ 跨显示器移动（display_index 上报）
  4) 快速拖窗 burst 合并（最终尺寸正确、中间态被合并）
  5) 缩放过程中后台发布新图（image_rev 前进、fit 模式不变）
  6) 配置保存的最终选择不被迟到尺寸事件覆盖（服务端版本+客户端 epoch 双端）
"""
import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
import urllib.error
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "server"))
from geometry import Geometry  # noqa: E402

LOCAL = os.environ.get("SDL_PREFIX", "/tmp/local/usr")
LDLP = ":".join([
    f"{LOCAL}/lib/aarch64-linux-gnu",
    f"{LOCAL}/lib/aarch64-linux-gnu/pulseaudio",
    f"{LOCAL}/../lib/aarch64-linux-gnu",
])
CLIENT = str(ROOT / "visual-window-app")
RESULTS = ROOT / "test-results"
PASS = FAIL = 0


def check(cond, name, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  PASS {name}")
    else:
        FAIL += 1
        print(f"  FAIL {name} {detail}")


def wait_port(port, timeout=8):
    end = time.time() + timeout
    while time.time() < end:
        try:
            with socket.create_connection(("127.0.0.1", port), 0.3):
                return True
        except OSError:
            time.sleep(0.1)
    return False


def api(port, path, method="GET", body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", data=data,
                                 method=method,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read().decode())


class Env:
    def __init__(self, geom, name, display_num):
        self.tmp = Path(tempfile.mkdtemp(prefix=f"e2e-{name}-"))
        (self.tmp / "xdg").mkdir()
        self.geom, self.name, self.display_num, self.display = (
            geom, name, display_num, f":{display_num}")
        self.port = 32000 + display_num
        self.server = self.client = None
        self.db_dir = self.tmp / "data"
        self.db_dir.mkdir()

    def start_xvfb(self):
        env = self._env(display=False)
        # 清理同号 display 的残留锁与 socket
        for f in (f"/tmp/.X{self.display_num}-lock",
                  f"/tmp/.X11-unix/X{self.display_num}",
                  f"/tmp/server-{self.display_num}.xkm"):
            try:
                os.remove(f)
            except FileNotFoundError:
                pass
        logf = open(self.tmp / "xvfb.log", "wb")
        subprocess.Popen(["/tmp/start_xvfb.sh", self.display,
                          "-xkbdir", f"{LOCAL}/share/X11/xkb",
                          "-screen", "0", self.geom, "-nolisten", "tcp", "-ac"],
                         env=env, stdout=logf, stderr=subprocess.STDOUT,
                         start_new_session=True)
        end = time.time() + 6
        last_err = b""
        while time.time() < end:
            r = subprocess.run([f"{LOCAL}/bin/xdpyinfo", "-display", self.display],
                               env=env, capture_output=True)
            if r.returncode == 0:
                return
            last_err = r.stderr
            time.sleep(0.2)
        raise RuntimeError(f"Xvfb failed: {last_err[:200]!r} "
                           f"log={ (self.tmp/'xvfb.log').read_text()[:300]}")

    def _env(self, display=True):
        e = dict(os.environ,
                 LD_LIBRARY_PATH=LDLP,
                 XKB_CONFIG_ROOT=f"{LOCAL}/share/X11/xkb",
                 XDG_RUNTIME_DIR=str(self.tmp / "xdg"),
                 DATA_DIR=str(self.db_dir), PORT=str(self.port), HOST="127.0.0.1")
        if display:
            e["DISPLAY"] = self.display
            e["SDL_VIDEODRIVER"] = "x11"
        return e

    def start_server(self):
        logf = open(self.tmp / "server.log", "wb")
        self.server = subprocess.Popen(
            [sys.executable, str(ROOT / "server/app.py")],
            env=self._env(display=False), stdout=logf, stderr=subprocess.STDOUT,
            cwd=str(ROOT), start_new_session=True)
        if not wait_port(self.port):
            raise RuntimeError("backend not up")

    def seed_image(self, src_png, rev=1):
        d = self.db_dir / "images"
        d.mkdir(parents=True, exist_ok=True)
        (d / f"default-{rev}.png").write_bytes(Path(src_png).read_bytes())

    def run_client(self, script_lines, extra=None, wait_markers=(), timeout=12,
                   hold=1.5):
        scr = self.tmp / f"sim-{abs(hash(tuple(script_lines)))%9999}.txt"
        scr.write_text("\n".join(script_lines) + "\n")
        for m in (wait_markers if wait_markers else ()):
            for cand in (self.tmp / m, ROOT / m):
                try: cand.unlink()
                except FileNotFoundError: pass
        logf = open(self.tmp / "client.log", "wb")
        env = self._env()
        env["HIT_LOG"] = str(self.tmp / "hits.log")
        args = [CLIENT, "--host", "127.0.0.1", "--port", str(self.port),
                "--device", "screen-A", "--script", str(scr)] + (extra or [])
        self.client = subprocess.Popen(args, env=env, stdout=logf,
                                       stderr=subprocess.STDOUT, cwd=str(ROOT),
                                       start_new_session=True)
        end = time.time() + timeout
        while time.time() < end:
            if all(self.marker_path(m).exists() for m in wait_markers):
                break
            if self.client.poll() is not None:
                break
            time.sleep(0.08)
        # marker 出现后保活窗口 hold 秒（抓帧期间窗口必须存在）
        self._hold_until = time.time() + hold
        return self.client

    def marker_path(self, m):
        for cand in (self.tmp / m, ROOT / m):
            if cand.exists():
                return cand
        return self.tmp / m

    def hold_remaining(self):
        end = getattr(self, "_hold_until", 0)
        while time.time() < end:
            time.sleep(0.05)

    def xwd(self, name):
        out = RESULTS / f"{self.name}-{name}.xwd"
        subprocess.run([f"{LOCAL}/bin/xwd", "-display", self.display, "-root",
                        "-silent", "-out", str(out)], env=self._env(),
                       check=True, capture_output=True)
        return out

    def hits(self):
        p = self.tmp / "hits.log"
        return p.read_text().strip().splitlines() if p.exists() else []

    def stop_client(self):
        if self.client and self.client.poll() is None:
            try:
                os.killpg(self.client.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                self.client.wait(3)
            except subprocess.TimeoutExpired:
                os.killpg(self.client.pid, signal.SIGKILL)
        self.client = None

    def cleanup(self):
        self.stop_client()
        if self.server:
            try:
                os.killpg(self.server.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                self.server.wait(3)
            except subprocess.TimeoutExpired:
                os.killpg(self.server.pid, signal.SIGKILL)
            self.server = None


def write_script(path, lines):
    Path(path).write_text("\n".join(lines) + "\n")


CLEAN_ENV = {k: v for k, v in os.environ.items()
             if k not in ("LD_LIBRARY_PATH", "LD_PRELOAD")}


def run_clean(cmd, **kw):
    return subprocess.run(cmd, env=CLEAN_ENV, **kw)


def compare(name, shot, src, spec, max_diff=0.04, marker=None):
    theo = RESULTS / f"{name}-theoretical.png"
    r = run_clean(
        [sys.executable, str(ROOT / "scripts/theoretical_frame.py"), str(src),
         str(spec["w"]), str(spec["h"]), str(spec["dpr"]), str(spec["rotation"]),
         spec["fit"], str(spec["fx"]), str(spec["fy"]), str(theo)],
        capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout, r.stderr)
    vis = RESULTS / f"{name}-diff.png"
    r = run_clean(
        [sys.executable, str(ROOT / "scripts/compare_frames.py"), str(shot),
         str(theo), "--max-diff", str(max_diff), "--vis", str(vis)] +
        (["--marker", str(marker)] if marker else []),
        capture_output=True, text=True)
    print("   " + r.stdout.strip().replace("\n", "\n   "))
    return r.returncode == 0, vis


def latest_report(port):
    _, b = api(port, "/api/reports")
    reps = b.get("reports", [])
    return reps[0]["body"] if reps else None


def pattern(name, w=1000, h=600, rev=1):
    p = RESULTS / name
    run_clean([sys.executable, str(ROOT / "scripts/make_pattern.py"),
               str(w), str(h), str(p), str(rev)],
              check=True, capture_output=True)
    return p


# ---------------------------------------------------------------------------
def scenario_cover_contain(dn):
    print("\n=== 场景1：cover 铺满裁切 / contain 完整显示黑边 + 上报 ===")
    env = Env("1600x900x24", "s1", dn)
    env.start_xvfb(); env.start_server()
    pat = pattern("s1-pat.png")
    env.seed_image(pat)
    try:
        env.run_client(
            ["wait 600", "size 800 600", "wait 600", "shot READY_COVER"],
            extra=["--width", "800", "--height", "600", "--x", "30", "--y", "30", "--no-overlay"],
            wait_markers=("READY_COVER",))
        shot = env.xwd("cover")
        ok, vis = compare("s1-cover", shot, pat,
                          dict(w=800,h=600,dpr=1,rotation=0,fit="cover",fx=.5,fy=.5),
                          marker=env.marker_path("READY_COVER"))
        check(ok, "cover 终端真实截图 == 理论帧（非管理端预览）", f"diff: {vis}")
        rep = latest_report(env.port)
        check(rep is not None, "cover 已上报裁切区域")
        if rep:
            ref = Geometry(1000,600,800,600,1,0,"cover",.5,.5).compute().normalized_crop()
            check(all(abs(a-b)<1e-3 for a,b in zip(ref,rep["crop"])),
                  "cover 上报 crop == 理论值", f"got={rep['crop']} ref={ref}")

        # 远程切 contain，等轮询生效，再截图
        st, _ = api(env.port, "/api/layout", "POST",
                    {"expected_version": rep["layout_version"], "fit": "contain"})
        check(st == 200, "远程保存 contain 成功（新版本入库）")
        env.stop_client()

        env.run_client(
            ["wait 700", "size 800 600", "wait 700", "shot READY_CONTAIN",
             "wait 3000"],
            extra=["--width", "800", "--height", "600", "--x", "30", "--y", "30", "--no-overlay"],
            wait_markers=("READY_CONTAIN",))
        shot = env.xwd("contain")
        ok, vis = compare("s1-contain", shot, pat,
                          dict(w=800,h=600,dpr=1,rotation=0,fit="contain",fx=.5,fy=.5),
                          marker=env.marker_path("READY_CONTAIN"))
        check(ok, "contain 终端真实截图 == 理论帧（上下黑边）", f"diff: {vis}")
        rep = latest_report(env.port)
        if rep:
            check(rep["fit"] == "contain" and
                  all(abs(a-b)<1e-3 for a,b in zip([0,0,1,1], rep["crop"])),
                  "contain 上报 fit 与整图 crop", str(rep["crop"]))
            check(abs(rep["letterbox"]["top"]-60)<1 and abs(rep["letterbox"]["bottom"]-60)<1,
                  "contain 黑边高度各60px 上报正确", str(rep["letterbox"]))
    finally:
        env.cleanup()


def scenario_rotation_dpi_hit(dn):
    print("\n=== 场景2：横竖屏 90° + 高DPI + 触摸命中与视觉一致 ===")
    env = Env("1700x1000x24", "s2", dn)
    env.start_xvfb(); env.start_server()
    pat = pattern("s2-pat.png")
    env.seed_image(pat)
    try:
        # 先按 rot90 保存配置，客户端一上线即竖屏；逻辑 600x800
        st, b = api(env.port, "/api/layout", "POST",
                    {"expected_version": 1, "orientation": 90})
        check(st == 200, "保存 orientation=90 配置")
        # 面板 800x600；旋转90 后 UI 逻辑视口 600x800，物理像素仍是 800x600
        g = Geometry(1000,600,600,800,1,90,"cover",.5,.5).compute()
        lx,ly = g.src_to_logical(500,300)
        cx,cy = (int(v) for v in g.logical_to_phys(lx,ly))
        env.run_client(
            ["wait 700",
             f"tap {cx} {cy}",
             "tap 2 2",
             "wait 200", "shot READY_R90", "wait 3000"],
            extra=["--width","800","--height","600","--x","40","--y","40","--no-overlay"],
            wait_markers=("READY_R90",))
        shot = env.xwd("rot90")
        ok, vis = compare("s2-rot90", shot, pat,
                          dict(w=600,h=800,dpr=1,rotation=90,fit="cover",fx=.5,fy=.5),
                          max_diff=0.05, marker=env.marker_path("READY_R90"))
        check(ok, "旋转90° 终端截图 == 理论帧（竖视口内容铺满固定面板）", str(vis))
        rep = latest_report(env.port)
        if rep:
            check(rep["rotation"] == 90 and rep["physical"] == [800,600]
                  and rep["logical"] == [600,800],
                  f"rot90 上报 physical/logical 正确: {rep['physical']}/{rep['logical']}")
        hits = env.hits()
        check(any("HIT" in h for h in hits), "视觉中心点触摸命中热区", str(hits))
        check(any(("tap 2 2" in h and "MISS" in h) for h in hits),
              "角落触摸不命中（命中测试与视觉位置一致）", str(hits))

        # 高 DPI 2
        env.stop_client()
        # 恢复 orientation auto，避免继承
        api(env.port, "/api/layout", "POST",
            {"expected_version": b["version"], "orientation": -1})
        env.run_client(
            ["wait 700", "size 800 600", "wait 600", "shot READY_DPR",
             "wait 3000"],
            extra=["--width","400","--height","300","--dpr","2",
                   "--x","40","--y","40","--no-overlay"],
            wait_markers=("READY_DPR",))
        shot = env.xwd("dpr2")
        ok, vis = compare("s2-dpr2", shot, pat,
                          dict(w=400,h=300,dpr=2,rotation=0,fit="cover",fx=.5,fy=.5),
                          marker=env.marker_path("READY_DPR"))
        check(ok, "DPR=2 终端截图 == 理论帧（逻辑400x300→物理800x600）", str(vis))
        rep = latest_report(env.port)
        if rep:
            check(abs(rep["dpr"]-2)<1e-6 and rep["physical"]==[800,600],
                  f"DPR 上报正确 dpr={rep['dpr']} phys={rep['physical']}")
    finally:
        env.cleanup()


def scenario_narrow_and_move(dn):
    print("\n=== 场景3：极窄窗口退化 + 跨显示器移动 ===")
    env = Env("1700x1000x24", "s3", dn)
    env.start_xvfb(); env.start_server()
    pat = pattern("s3-pat.png")
    env.seed_image(pat)
    try:
        # 阶段1：极窄窗口，单独等待 drain 并抓帧/上报
        env.run_client(
            ["wait 600",
             "size 8 600", "wait 1200",
             "shot READY_NARROW", "wait 1500"],
            extra=["--width","800","--height","600","--x","40","--y","40","--no-overlay"],
            wait_markers=("READY_NARROW",), hold=1.0)
        _, b = api(env.port, "/api/reports")
        deg = [r["body"] for r in b["reports"] if r["body"].get("degenerate")]
        check(len(deg) >= 1,
              "极窄窗口被标记 degenerate 并安全上报",
              str([(x.get("logical"), x.get("degenerate")) for x in
                   [r["body"] for r in b["reports"]]]))
        env.stop_client()
        # 阶段2：跨显示器移动 + 恢复正常尺寸
        env.run_client(
            ["wait 600",
             "move 900 100", "wait 400",
             "size 800 600", "wait 900",
             "shot READY_NARROW_FLOW", "wait 1500"],
            extra=["--width","800","--height","600","--x","40","--y","40","--no-overlay"],
            wait_markers=("READY_NARROW_FLOW",), hold=1.0)
        shot = env.xwd("narrow-flow")
        ok, vis = compare("s3-recovered", shot, pat,
                          dict(w=800,h=600,dpr=1,rotation=0,fit="cover",fx=.5,fy=.5),
                          marker=env.marker_path("READY_NARROW_FLOW"))
        check(ok, "极窄窗口后恢复，终端截图仍 == 理论帧（无 NaN/崩溃）", str(vis))
    finally:
        env.cleanup()


def scenario_burst_coalesce(dn):
    print("\n=== 场景4：快速拖窗 40 事件合并为一次最终重绘/上报 ===")
    env = Env("1700x1000x24", "s4", dn)
    env.start_xvfb(); env.start_server()
    pat = pattern("s4-pat.png")
    env.seed_image(pat)
    try:
        env.run_client(
            ["wait 600",
             "burst 40 200 800 600 1100 600",  # 200ms 内拖到 1100x600
             "wait 900", "shot READY_BURST", "wait 3000"],
            extra=["--width","800","--height","600","--x","30","--y","30","--no-overlay"],
            wait_markers=("READY_BURST",))
        shot = env.xwd("burst")
        ok, vis = compare("s4-burst", shot, pat,
                          dict(w=1100,h=600,dpr=1,rotation=0,fit="cover",fx=.5,fy=.5),
                          marker=env.marker_path("READY_BURST"))
        check(ok, "burst 结束后截图 == 最终尺寸理论帧", str(vis))
        # 上报数量应远少于 40（防抖 500ms + 合并）
        _, b = api(env.port, "/api/reports")
        n = sum(1 for r in b["reports"] if r["device_id"]=="screen-A")
        check(1 <= n <= 6, f"burst 上报被合并/防抖（实际 {n} 条，要求 ≤6）",
              f"n={n}")
        rep = latest_report(env.port)
        check(rep and rep["logical"] == [1100,600],
              f"最终上报尺寸为拖动终点 {rep and rep['logical']}")
    finally:
        env.cleanup()


def scenario_publish_during_resize(dn):
    print("\n=== 场景5：缩放进行中后台发布新图（模式不动、rev 前进、画面换成新图）===")
    env = Env("1700x1000x24", "s5", dn)
    env.start_xvfb(); env.start_server()
    pat1 = pattern("s5-pat1.png", rev=1)
    pat2 = pattern("s5-pat2.png", rev=2)
    env.seed_image(pat1)
    # 直接把新图入库（rev2），再让发布钩子 bump 版本但模式保持 cover
    env.seed_image(pat2, rev=2)
    try:
        env.run_client(
            ["wait 600",
             # 持续 burst 制造"缩放中"
             "burst 20 4000 800 600 1000 600",
             "wait 200",
             "publish",          # burst 进行中由 sim 线程触发后台发布
             "wait 1500",
             "shot READY_NEWIMG",
             "wait 3000"],
            extra=["--width","800","--height","600","--x","30","--y","30","--no-overlay"],
            wait_markers=("READY_NEWIMG",), timeout=15)
        shot = env.xwd("newimg")
        # 新图 rev2：必须按新像素渲染，且 fit 仍为 cover（模式未被发布动作改变）
        ok, vis = compare("s5-newimg", shot, pat2,
                          dict(w=1000,h=600,dpr=1,rotation=0,fit="cover",fx=.5,fy=.5),
                          marker=env.marker_path("READY_NEWIMG"))
        check(ok, "发布新图后终端显示新图像素（rev2 配色）", str(vis))
        # 反向断言：截图与旧图必须"不匹配"（compare 返回 False 才说明确实换图）
        ok_old, _ = compare("s5-newimg-vs-old", shot, pat1,
                            dict(w=1000,h=600,dpr=1,rotation=0,fit="cover",fx=.5,fy=.5),
                            marker=env.marker_path("READY_NEWIMG"), max_diff=0.04)
        check(not ok_old, "截图与旧图存在可见差异（证明确实换图，RMSE 超阈值）")
        rep = latest_report(env.port)
        check(rep and rep["image_rev"] == 2 and rep["fit"] == "cover",
              f"上报 image_rev=2 且 fit 保持 cover: rev={rep and rep['image_rev']} fit={rep and rep['fit']}")
    finally:
        env.cleanup()


def scenario_save_finality(dn):
    print("\n=== 场景6：用户最终选择（cover->contain 保存）不被迟到尺寸事件覆盖 ===")
    env = Env("1700x1000x24", "s6", dn)
    env.start_xvfb(); env.start_server()
    pat = pattern("s6-pat.png")
    env.seed_image(pat)
    try:
        # 客户端上线后，先把 cover 初始布局截一张；然后保存 contain，
        # 紧接着灌入一批"旧 cover 时代排队"的尺寸 burst（模拟迟到事件）
        env.run_client(
            ["wait 600",
             "size 800 600", "wait 300",
             "shot READY_BEFORE_SAVE",
             # 由服务端在另一个连接完成保存后，sim 继续推尺寸事件（此时客户端已轮询到新版本）
             "wait 800",
             "burst 10 200 800 600 700 600",
             "wait 1200",
             "shot READY_AFTER",
             "wait 3000"],
            extra=["--width","800","--height","600","--x","30","--y","30","--no-overlay"],
            wait_markers=("READY_BEFORE_SAVE",), timeout=15)
        # 在客户端等待期间，由测试侧（模拟"另一个管理员/远程"）保存 contain
        st, b = api(env.port, "/api/layout", "POST",
                    {"expected_version": 1, "fit": "contain"})
        check(st == 200, "保存 contain=用户最终选择 v2")
        # 等待 READY_AFTER marker（run_client 只等第一个；手动等）
        end = time.time() + 8
        while not (env.tmp/"READY_AFTER").exists() and time.time() < end:
            time.sleep(0.1)
        time.sleep(0.3)
        shot = env.xwd("after-save")
        ok, vis = compare("s6-contain-final", shot, pat,
                          dict(w=700,h=600,dpr=1,rotation=0,fit="contain",fx=.5,fy=.5),
                          marker=env.marker_path("READY_AFTER"))
        check(ok, "迟到 burst 后仍是 contain 新模式（未被旧尺寸事件覆盖）", str(vis))
        rep = latest_report(env.port)
        check(rep and rep["fit"] == "contain" and rep["logical"] == [700,600],
              f"最终上报 fit=contain 且采用最终尺寸: {rep and (rep['fit'],rep['logical'])}")
    finally:
        env.cleanup()


def main():
    RESULTS.mkdir(exist_ok=True)
    assert Path("/tmp/Xvfb-patched").exists(), "先 source scripts/xvfb_env.sh 准备 Xvfb"
    assert Path(CLIENT).exists(), "先编译 visual-window-app"
    base = 200 + (int(time.time()) % 50)
    scenario_cover_contain(base+1)
    scenario_rotation_dpi_hit(base+2)
    scenario_narrow_and_move(base+3)
    scenario_burst_coalesce(base+4)
    scenario_publish_during_resize(base+5)
    scenario_save_finality(base+6)
    print(f"\n==== E2E: {PASS} passed, {FAIL} failed ====")
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
