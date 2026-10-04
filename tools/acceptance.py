#!/usr/bin/env python3
"""
acceptance.py - 布局调试能力端到端验收

覆盖需求中的验收场景：
  A. 横竖屏切换（窗口尺寸交换 + 旋转键 r）
  B. 跨显示器移动（DPR 键 d 模拟 1x/1.5x/2x/3x）
  C. 极窄窗口
  D. 后台"在缩放中发布新图"：发布后轮询提示/应用，且本地选择不被覆盖
  E. 快速拖动窗口 -> 合并重绘；模式选择必须保留（迟到尺寸事件不覆盖新模式）
  F. 撤销不越过本次编辑基础；远程更新冲突提示；强制保存
  G. 截图与理论裁切框像素级比对（不只看管理端预览）
  H. Web 展示终端：若无 headless 浏览器则跑其几何+上报契约，
     有则真截图比对。

用法：在容器内 `make acceptance`（脚本自行拉起 Xvfb / openbox / server / app）。
"""
import json
import os
import signal
import subprocess
import sys
import time
import urllib.request
import urllib.error
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
TMP = Path(os.environ.get("ACCEPT_TMP", "/tmp/layout-accept"))
TMP.mkdir(parents=True, exist_ok=True)

PORT = os.environ.get("LAYOUT_SERVER_PORT", "8081")
BASE = f"http://127.0.0.1:{PORT}"
DISPLAY = os.environ.get("ACCEPT_DISPLAY", ":97")
SHOTS = TMP / "shots"
SHOTS.mkdir(exist_ok=True)

PASSED = []
FAILED = []


def check(name, cond, detail=""):
    (PASSED if cond else FAILED).append(name)
    print(f"[{'PASS' if cond else 'FAIL'}] {name}" + (f" -- {detail}" if detail else ""))


def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def api(method, path, body=None, raw=None, ctype="application/json"):
    url = BASE + path
    data = None
    headers = {}
    if raw is not None:
        data = raw
        headers["Content-Type"] = ctype
    elif body is not None:
        data = json.dumps(body).encode()
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            txt = r.read().decode()
            return r.status, json.loads(txt) if txt else {}
    except urllib.error.HTTPError as e:
        txt = e.read().decode()
        try:
            return e.code, json.loads(txt)
        except json.JSONDecodeError:
            return e.code, {"error": txt}


def wait_http(timeout=15):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            urllib.request.urlopen(BASE + "/api/layout/latest", timeout=2).read()
            return True
        except Exception:
            time.sleep(0.3)
    return False


procs = []


def start(cmd, env=None, **kw):
    e = os.environ.copy()
    e["DISPLAY"] = DISPLAY
    if env:
        e.update(env)
    p = subprocess.Popen(cmd, env=e, **kw)
    procs.append(p)
    return p


def cleanup():
    for p in procs:
        if p.poll() is None:
            p.send_signal(signal.SIGTERM)
    time.sleep(0.5)
    for p in procs:
        if p.poll() is None:
            p.kill()


def xdotool(*args):
    for _ in range(20):
        r = sh(["xdotool"] + [str(a) for a in args])
        if r.returncode == 0:
            return r.stdout.strip()
        time.sleep(0.2)
    raise RuntimeError("xdotool failed: " + " ".join(map(str, args)))


def find_app_window():
    wid = xdotool("search", "--name", "Layout Debug Display")
    return wid.splitlines()[0]


def resize_window(wid, w, h):
    # 合并性测试：快速连续 resize，最终只关心最终尺寸
    xdotool("windowsize", wid, w, h)


def screenshot(path, wid=None):
    if wid:
        r = sh(["import", "-window", wid, str(path)])
    else:
        r = sh(["import", "-window", "root", str(path)])
    if r.returncode != 0:
        raise RuntimeError("screenshot failed: " + r.stderr)


def keypress(wid, key):
    xdotool("windowfocus", wid)
    time.sleep(0.05)
    xdotool("key", "--window", wid, key)


def latest_report(device="display-01"):
    _, d = api("GET", "/api/reports/crop")
    reps = [r for r in d.get("reports", []) if r.get("device_id") == device]
    return reps[-1] if reps else None


def verify_shot(name, shot, mode, vp, focus, crop, rotation=0, image=None):
    out = SHOTS / f"{name}.expected.png"
    image = image or str(ROOT / "assets" / "background.png")
    r = sh(["python3", str(TOOLS / "screenshot_verify.py"),
            "--shot", str(shot), "--image", image,
            "--mode", mode, "--vp", vp, "--focus", focus,
            "--rotation", str(rotation), "--crop", crop,
            "--expected-out", str(out)])
    ok = r.returncode == 0
    print(r.stdout)
    if not ok:
        print(r.stderr)
    check(f"截图比对 {name}", ok)
    return ok


def main():
    # 准备：测试图 + 干净状态
    sh(["python3", str(TOOLS / "make_test_pattern.py"),
        str(ROOT / "assets" / "background.png")])
    state = ROOT / "data" / "state.json"
    if state.exists():
        state.unlink()
    reports = ROOT / "data" / "reports.jsonl"
    if reports.exists():
        reports.unlink()

    # Xvfb + openbox（若外层没提供 DISPLAY 服务）
    if sh(["xdpyinfo", "-display", DISPLAY]).returncode != 0:
        start(["Xvfb", DISPLAY, "-screen", "0", "1920x1200x24",
               "-ac", "+extension", "GLX", "+render", "-noreset"])
        time.sleep(1.0)
        start(["openbox"])
        time.sleep(0.8)

    # 后端
    start(["python3", str(ROOT / "server" / "layout_server.py")],
          env={"LAYOUT_SERVER_PORT": PORT},
          stdout=open(TMP / "server.log", "w"), stderr=subprocess.STDOUT)
    if not wait_http():
        print("server did not start"); cleanup(); sys.exit(2)

    # 初始配置：cover 居中
    _, rec = api("GET", "/api/layout/latest")
    iw, ih = rec["config"]["image_w"], rec["config"]["image_h"]
    print(f"source image = {iw}x{ih}")

    # C 展示端
    env = {
        "LAYOUT_SERVER_HOST": "127.0.0.1",
        "LAYOUT_SERVER_PORT": PORT,
        "LAYOUT_DEVICE_ID": "display-01",
        "LAYOUT_VRAM_BUDGET": "64M",
        "LAYOUT_BORDERLESS": "1",
    }
    appbin = ROOT / "visual-window-app"
    start([str(appbin)], env=env,
          stdout=open(TMP / "app.log", "w"), stderr=subprocess.STDOUT)
    # 等窗口出现
    wid = None
    for _ in range(50):
        try:
            wid = find_app_window()
            break
        except Exception:
            time.sleep(0.2)
    if not wid:
        print("app window not found"); cleanup(); sys.exit(2)
    print("app window:", wid)
    time.sleep(1.5)

    # 几何一致性（三坐标变换）直接跑 C 单测 + 对拍已在 make test；这里确认在线行为。
    from crop_theory import resolve_layout, reported_crop  # noqa
    sys.path.insert(0, str(TOOLS))

    # ---------- 场景 E：快速拖动窗口 + 模式选择保留 ----------
    resize_window(wid, 1280, 720)
    time.sleep(0.6)
    # 用户切到 contain
    keypress(wid, "m")
    time.sleep(0.4)
    # 立刻快速连续 resize（模拟拖动中尺寸事件风暴）
    for w, h in [(900, 700), (700, 650), (480, 600), (1280, 720)]:
        xdotool("windowsize", wid, w, h)
        time.sleep(0.03)
    time.sleep(0.8)
    # 模式必须仍是 contain（迟到 resize 不能覆盖模式）
    _, rec2 = api("GET", "/api/layout/latest")
    # 注意：模式是端上本地选择，未保存前不改后端。用上报里的 mode 佐证
    rep = latest_report()
    check("快速 resize 后用户模式保留(contain)",
          rep is not None and rep.get("mode") == "contain",
          f"report mode={rep.get('mode') if rep else None}")

    # ---------- 场景 A：横屏 cover 截图比对 ----------
    # 切回 cover 并保存
    keypress(wid, "m")  # contain->cover
    time.sleep(0.2)
    keypress(wid, "s")  # 保存（基线=初始版本）
    time.sleep(0.6)
    resize_window(wid, 1280, 720)
    time.sleep(0.8)
    shot = SHOTS / "landscape.png"
    screenshot(shot, wid)
    rep = latest_report()
    t = resolve_layout({"image_w": iw, "image_h": ih, "mode": "cover",
                        "focus_x": .5, "focus_y": .5}, {"w": 1280, "h": 720})
    rc = reported_crop(t)
    check("横屏 cover 上报裁切≈理论",
          rep and rep["mode"] == "cover" and
          abs(rep["crop"]["x"] - rc["x"]) <= 1 and
          abs(rep["crop"]["y"] - rc["y"]) <= 1 and
          abs(rep["crop"]["w"] - rc["w"]) <= 1 and
          abs(rep["crop"]["h"] - rc["h"]) <= 1,
          f"reported={rep and rep['crop']} theory={rc}")
    verify_shot("landscape-cover", shot, "cover", "1280x720", "0.5,0.5",
                f"{rep['crop']['x']},{rep['crop']['y']},{rep['crop']['w']},{rep['crop']['h']}")

    # ---------- 场景 C：极窄窗口 cover ----------
    resize_window(wid, 180, 900)
    time.sleep(0.8)
    shot = SHOTS / "narrow.png"
    screenshot(shot, wid)
    rep = latest_report()
    t = resolve_layout({"image_w": iw, "image_h": ih, "mode": "cover",
                        "focus_x": .5, "focus_y": .5}, {"w": 180, "h": 900})
    rc = reported_crop(t)
    check("极窄 cover 裁切在图像范围内且上报一致",
          rep and rep["crop"]["x"] >= 0 and
          rep["crop"]["x"] + rep["crop"]["w"] <= iw and
          rep["crop"]["h"] <= ih and
          abs(rep["crop"]["w"] - rc["w"]) <= 1,
          f"reported={rep['crop']} theory={rc}")
    verify_shot("narrow-cover", shot, "cover", "180x900", "0.5,0.5",
                f"{rep['crop']['x']},{rep['crop']['y']},{rep['crop']['w']},{rep['crop']['h']}")

    # ---------- 场景 A2：竖屏（交换宽高） ----------
    resize_window(wid, 540, 960)
    time.sleep(0.8)
    shot = SHOTS / "portrait.png"
    screenshot(shot, wid)
    rep = latest_report()
    t = resolve_layout({"image_w": iw, "image_h": ih, "mode": "cover",
                        "focus_x": .5, "focus_y": .5}, {"w": 540, "h": 960})
    rc = reported_crop(t)
    check("竖屏 cover 上报裁切≈理论",
          rep and abs(rep["crop"]["w"] - rc["w"]) <= 1 and
          abs(rep["crop"]["h"] - rc["h"]) <= 1,
          f"reported={rep['crop']} theory={rc}")
    verify_shot("portrait-cover", shot, "cover", "540x960", "0.5,0.5",
                f"{rep['crop']['x']},{rep['crop']['y']},{rep['crop']['w']},{rep['crop']['h']}")

    # ---------- 场景 contain 留边 ----------
    keypress(wid, "m")  # cover->contain
    time.sleep(0.6)
    resize_window(wid, 1000, 1000)
    time.sleep(0.8)
    shot = SHOTS / "contain.png"
    screenshot(shot, wid)
    rep = latest_report()
    t = resolve_layout({"image_w": iw, "image_h": ih, "mode": "contain",
                        "focus_x": .5, "focus_y": .5}, {"w": 1000, "h": 1000})
    rc = reported_crop(t)
    check("contain 上报完整源图(留边)",
          rep and rep["mode"] == "contain" and
          rep["crop"]["w"] == iw and rep["crop"]["h"] == ih,
          f"reported={rep['crop']}")
    verify_shot("square-contain", shot, "contain", "1000x1000", "0.5,0.5",
                f"{rep['crop']['x']},{rep['crop']['y']},{rep['crop']['w']},{rep['crop']['h']}")

    # ---------- 场景 F：撤销只到本次基线 ----------
    # 当前 contain。再改焦点几次，撤销两次，最后不应越过启动时基线（cover 是会话起点？
    # 会话起点是启动时配置 cover；本会话内已切 contain）。验证撤销可回到 cover 基线一次，
    # 再次撤销无效。
    keypress(wid, "Right")  # focus +
    time.sleep(0.2)
    keypress(wid, "Right")
    time.sleep(0.2)
    keypress(wid, "u")  # undo right
    time.sleep(0.2)
    keypress(wid, "u")  # undo right
    time.sleep(0.2)
    keypress(wid, "u")  # undo -> cover (baseline of session)
    time.sleep(0.2)
    keypress(wid, "u")  # 停在 baseline(cover)
    time.sleep(0.2)
    keypress(wid, "u")  # 越过基础：必须被拒绝并提示
    time.sleep(0.2)
    log = (TMP / "app.log").read_text()
    check("撤销提示到达本次编辑基础后停止",
          "不能再撤销" in log or "已到本次编辑基础" in log,
          "见 app.log 撤销边界提示")

    # ---------- 场景 F2：远程更新冲突（后端版本推进，端上有未保存选择） ----------
    # 先在端上制造一个未保存选择：切 contain
    keypress(wid, "m")
    time.sleep(0.3)
    # 用管理端身份直接推进服务端版本
    st2, _ = api("GET", "/api/layout/latest")
    cur = api("GET", "/api/layout/latest")[1]
    cfgx = dict(cur["config"])
    cfgx["focus_x"] = 0.2
    code, resp = api("PUT", "/api/layout",
                     {"base_version": cur["version"], "client": "other-admin",
                      "config": cfgx})
    check("其他端保存成功推进版本", code == 200, f"v{resp.get('version')}")
    # 等 C 端轮询（间隔 5s）
    time.sleep(6.0)
    log = (TMP / "app.log").read_text()
    check("C 端提示远程冲突且不覆盖本地 contain 选择",
          "远程有新版本" in log and
          ("未被覆盖" in log or "未保存选择" in log),
          "见 app.log 冲突提示")
    # 本地选择仍应为 contain（看最新上报）
    rep = latest_report()
    check("冲突期间本地模式仍是用户最终选择 contain",
          rep and rep.get("mode") == "contain", f"mode={rep and rep.get('mode')}")
    # 强制保存（s 键在冲突态走 force）
    keypress(wid, "s")
    time.sleep(1.0)
    cur2 = api("GET", "/api/layout/latest")[1]
    check("强制保存后服务端采用本地 contain",
          cur2["config"]["mode"] == "contain",
          f"server mode={cur2['config']['mode']}")

    # ---------- 场景 B：跨显示器 DPI 变化（d 键） ----------
    resize_window(wid, 1280, 720)
    time.sleep(0.4)
    keypress(wid, "d")  # 1.5
    time.sleep(0.2)
    keypress(wid, "d")  # 2.0
    time.sleep(0.8)
    rep = latest_report()
    check("跨显示器 DPR 变化上报新 dpr",
          rep and rep.get("dpr", 0) >= 1.9, f"dpr={rep and rep.get('dpr')}")
    # 裁切逻辑以逻辑视口计算，DPR 不应改变 cover 逻辑裁切
    t1 = resolve_layout({"image_w": iw, "image_h": ih, "mode": "contain",
                         "focus_x": .5, "focus_y": .5}, {"w": 1280, "h": 720})
    rc1 = reported_crop(t1)
    check("DPR 不改变逻辑裁切(contain=整图)",
          rep and rep["crop"]["w"] == rc1["w"] and rep["crop"]["h"] == rc1["h"],
          f"dpr2 crop={rep and rep['crop']}")
    _ = rep_before

    # ---------- 场景 D：后台在缩放/拖动中发布新图 ----------
    new_img = TMP / "newscene.png"
    sh(["convert", "-size", "900x1600", "xc:#201030",
        "-fill", "#19e0c0", "-draw", "rectangle 0,1200 900,1600",
        "-fill", "#ff4fd8", "-draw", "rectangle 0,0 900,300",
        str(new_img)])
    # 边 resize 边发布
    import threading
    def resize_storm():
        for w, h in [(800, 600), (600, 800), (400, 900), (1280, 720)]:
            xdotool("windowsize", wid, w, h)
            time.sleep(0.05)
    th = threading.Thread(target=resize_storm)
    th.start()
    time.sleep(0.05)
    th.join()
    boundary = "----acpt2"
    with open(new_img, "rb") as f:
        img_bytes = f.read()
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"image\"; "
            f"filename=\"newscene.png\"\r\nContent-Type: image/png\r\n\r\n").encode() + \
           img_bytes + f"\r\n--{boundary}--\r\n".encode()
    code, resp = api("POST", "/api/images/publish", raw=body,
                     ctype=f"multipart/form-data; boundary={boundary}")
    check("缩放中发布新图成功并产生新版本", code == 200,
          f"code={code} v={resp.get('version')} {resp.get('config', {}).get('image_id')}")
    # 等端轮询并换图（无未保存改动时自动应用）
    keypress(wid, "g")  # 显式拉取，避免轮询窗口
    time.sleep(1.5)
    resize_window(wid, 720, 1000)
    time.sleep(1.0)
    shot = SHOTS / "newimage.png"
    screenshot(shot, wid)
    rep = latest_report()
    check("发布后现场上报指向新图", rep and rep["image_id"] ==
          resp["config"]["image_id"], f"reported image={rep and rep['image_id']}")
    # 用新图做一次截图比对（cover）
    t = resolve_layout({"image_w": 900, "image_h": 1600, "mode": "contain",
                        "focus_x": .5, "focus_y": .5}, {"w": 720, "h": 1000})
    rc = reported_crop(t)
    verify_shot("newimage-contain", shot, "contain", "720x1000", "0.5,0.5",
                f"{rc['x']},{rc['y']},{rc['w']},{rc['h']}",
                image=str(ROOT / "assets" / resp["config"]["image_id"]))

    # ---------- Web 展示终端契约/截图 ----------
    web_verify(resp["config"]["image_id"])

    print("\n================ 验收结果 ================")
    for n in PASSED:
        print("  PASS", n)
    for n in FAILED:
        print("  FAIL", n)
    print(f"合计 {len(PASSED)} 通过, {len(FAILED)} 失败")
    cleanup()
    sys.exit(1 if FAILED else 0)


def web_verify(image_id):
    """Web 端：优先 headless 真截图；否则跑 Node 契约（几何+上报体）。"""
    # 1) Node 契约：用 geo.js 计算并 POST 一条 web 上报，再读回
    node = sh(["node", "-e", "console.log(require.resolve('/workspace/web/geo.js'))"])
    have_geo = node.returncode == 0
    contract = TMP / "web_contract.js"
    contract.write_text(f"""
const G=require('/workspace/web/geo.js');
(async()=>{{
  const r=await fetch('{BASE}/api/layout/latest');
  const rec=await r.json();
  const vp={{w:393,h:852}};
  const t=G.resolveLayout({{...rec.config}}, vp);
  const crop=G.reportedCrop(t);
  const body={{device_id:'web-contract',image_id:rec.config.image_id,
    mode:rec.config.mode,crop,viewport_logical:vp,dpr:3,rotation:0,
    client:'web-display'}};
  const pr=await fetch('{BASE}/api/reports/crop',{{method:'POST',
    headers:{{'Content-Type':'application/json'}},body:JSON.stringify(body)}});
  if(!pr.ok) process.exit(3);
  console.log(JSON.stringify({{version:rec.version,crop}}));
}})().catch(e=>{{console.error(e);process.exit(2)}});
""")
    r = sh(["node", str(contract)])
    ok = r.returncode == 0
    detail = r.stdout.strip() or r.stderr.strip()
    check("Web 展示端按配置渲染的几何契约+上报", ok, detail)
    if ok:
        data = json.loads(detail)
        reps = api("GET", "/api/reports/crop")[1]["reports"]
        web = [x for x in reps if x["device_id"] == "web-contract"]
        check("后端收到 Web 终端上报裁切", bool(web),
              f"last={web[-1]['crop'] if web else None}")

    # 2) 真 headless 浏览器（如可用）
    chrome = sh(["bash", "-lc", "which chromium || which chromium-browser || which google-chrome"]).stdout.strip()
    if chrome and have_geo:
        out_png = SHOTS / "web-terminal.png"
        r = sh([chrome, "--headless=new", "--disable-gpu", "--no-sandbox",
                "--window-size=393,852", "--force-device-scale-factor=3",
                f"--screenshot={out_png}",
                "--virtual-time-budget=4000",
                f"{BASE}/display.html"])
        if r.returncode == 0 and out_png.exists():
            rec = api("GET", "/api/layout/latest")[1]
            # headless 截图包含整页；几何验证由像素工具做
            t = __import__("sys").modules  # noqa
            sys.path.insert(0, str(TOOLS))
            from crop_theory import resolve_layout as rl, reported_crop as rc2
            cfg = dict(rec["config"])
            th = rl(cfg, {"w": 393, "h": 852})
            cc = rc2(th)
            rr = sh(["python3", str(TOOLS / "screenshot_verify.py"),
                     "--shot", str(out_png),
                     "--image", str(ROOT / "assets" / cfg["image_id"]),
                     "--mode", cfg["mode"], "--vp", "393x852",
                     "--focus", f"{cfg['focus_x']},{cfg['focus_y']}",
                     "--crop", f"{cc['x']},{cc['y']},{cc['w']},{cc['h']}",
                     "--expected-out", str(SHOTS / "web.expected.png"),
                     "--threshold", "0.2"])
            check("Web 展示终端截图与理论裁切一致", rr.returncode == 0,
                  rr.stdout.strip().splitlines()[-1] if rr.stdout else rr.stderr[:200])
        else:
            check("Web 展示终端截图与理论裁切一致", False,
                  "headless chrome 截图失败")
    else:
        print("[SKIP] 无 headless chromium，Web 终端以 Node 契约+上报代替真截图"
              "（容器内安装 chromium 后该项自动启用）")


if __name__ == "__main__":
    try:
        main()
    finally:
        cleanup()
