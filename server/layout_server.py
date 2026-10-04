#!/usr/bin/env python3
"""
布局调试后端（标准库实现）：
  GET  /api/layout/latest        读取当前生效配置
  PUT  /api/layout               乐观锁保存（base_version 不匹配 -> 409）
  GET  /api/layout/history       版本历史
  POST /api/reports/crop         C 展示端 / Web 展示端上报真实裁切区域
  GET  /api/reports/crop         读取上报（验收比对）
  POST /api/images/publish       发布新图（multipart/form-data，缩放中也安全）
  GET  /assets/<file>            图片静态服务
  /  /admin.html  /display.html  页面

入库：data/state.json（原子写：临时文件 + fsync + rename），
校验：config 字段/焦点范围/图片必须存在且尺寸匹配。
"""
import hashlib
import json
import os
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

from image_meta import image_size

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.join(ROOT, "data")
ASSETS_DIR = os.path.join(ROOT, "assets")
STATE_PATH = os.path.join(DATA_DIR, "state.json")
REPORTS_PATH = os.path.join(DATA_DIR, "reports.jsonl")

VALID_MODES = {"cover", "contain"}
VALID_ROTATIONS = {0, 90, 180, 270}

_lock = threading.Lock()


def now_iso():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def checksum_of(config: dict) -> str:
    canon = json.dumps(config, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canon.encode("utf-8")).hexdigest()[:16]


def default_state():
    return {
        "version": 0,
        "updated_at": now_iso(),
        "config": {
            "image_id": "background.png",
            "image_w": 1920,
            "image_h": 1080,
            "mode": "cover",
            "focus_x": 0.5,
            "focus_y": 0.5,
            "rotation": 0,
            "dpr_override": 0.0,
        },
        "checksum": "",
        "history": [],
    }


def atomic_write(path, text):
    tmp = f"{path}.tmp.{os.getpid()}.{uuid.uuid4().hex[:6]}"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(text)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def load_state():
    if not os.path.exists(STATE_PATH):
        st = default_state()
        st["checksum"] = checksum_of(st["config"])
        save_state(st)
        return st
    with open(STATE_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def save_state(st):
    atomic_write(STATE_PATH, json.dumps(st, ensure_ascii=False, indent=2))


def validate_config(cfg):
    if not isinstance(cfg, dict):
        return "config must be object"
    image_id = cfg.get("image_id")
    if not isinstance(image_id, str) or not image_id or "/" in image_id or ".." in image_id:
        return "invalid image_id"
    path = os.path.join(ASSETS_DIR, image_id)
    if not os.path.isfile(path):
        return f"image not found: {image_id}"
    try:
        w, h = image_size(path)
    except Exception as exc:  # noqa: BLE001
        return f"unreadable image: {exc}"
    if int(cfg.get("image_w", -1)) != w or int(cfg.get("image_h", -1)) != h:
        # 以磁盘真实尺寸为准并纠正，防止伪造/过期配置入库
        cfg["image_w"] = w
        cfg["image_h"] = h
    if cfg.get("mode") not in VALID_MODES:
        return "mode must be cover|contain"
    for axis in ("focus_x", "focus_y"):
        v = cfg.get(axis)
        if not isinstance(v, (int, float)) or not (0.0 <= float(v) <= 1.0):
            return f"{axis} must be in [0,1]"
    if cfg.get("rotation") not in VALID_ROTATIONS:
        return "rotation must be 0/90/180/270"
    dpr = cfg.get("dpr_override", 0.0)
    if not isinstance(dpr, (int, float)) or not (0.0 <= float(dpr) <= 8.0):
        return "dpr_override must be in [0,8]"
    cfg["dpr_override"] = float(dpr)
    return None


def public_record(st):
    return {
        "version": st["version"],
        "updated_at": st["updated_at"],
        "checksum": st["checksum"],
        "config": st["config"],
    }


def append_report(obj):
    obj["received_at"] = now_iso()
    with open(REPORTS_PATH, "a", encoding="utf-8") as f:
        f.write(json.dumps(obj, ensure_ascii=False) + "\n")


def read_reports(limit=100):
    if not os.path.exists(REPORTS_PATH):
        return []
    with open(REPORTS_PATH, "r", encoding="utf-8") as f:
        lines = f.readlines()[-limit:]
    out = []
    for line in lines:
        line = line.strip()
        if line:
            try:
                out.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return out


class Handler(BaseHTTPRequestHandler):
    server_version = "LayoutDebug/1.0"

    def log_message(self, fmt, *args):  # 安静些
        pass

    def _send_json(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self):
        length = int(self.headers.get("Content-Length", "0") or 0)
        raw = self.rfile.read(length) if length else b""
        return json.loads(raw.decode("utf-8")) if raw else {}

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path
        if path == "/api/layout/latest":
            with _lock:
                return self._send_json(200, public_record(load_state()))
        if path == "/api/layout/history":
            with _lock:
                st = load_state()
            return self._send_json(200, {"history": st.get("history", [])[-50:]})
        if path == "/api/reports/crop":
            return self._send_json(200, {"reports": read_reports()})
        if path.startswith("/assets/"):
            return self._serve_asset(path[len("/assets/"):])
        if path in ("/", "/admin.html"):
            return self._serve_static("admin.html", "text/html; charset=utf-8")
        if path == "/display.html":
            return self._serve_static("display.html", "text/html; charset=utf-8")
        if path == "/geo.js":
            return self._serve_static("geo.js", "application/javascript; charset=utf-8")
        if path == "/app.js":
            return self._serve_static("app.js", "application/javascript; charset=utf-8")
        if path == "/display.js":
            return self._serve_static("display.js", "application/javascript; charset=utf-8")
        if path == "/style.css":
            return self._serve_static("style.css", "text/css; charset=utf-8")
        self._send_json(404, {"error": "not found"})

    def _serve_asset(self, name):
        if "/" in name or ".." in name:
            return self._send_json(400, {"error": "bad name"})
        p = os.path.join(ASSETS_DIR, name)
        if not os.path.isfile(p):
            return self._send_json(404, {"error": "asset missing"})
        ctype = "image/png" if name.lower().endswith(".png") else "image/jpeg"
        with open(p, "rb") as f:
            data = f.read()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(data)

    def _serve_static(self, name, ctype):
        p = os.path.join(ROOT, "web", name)
        if not os.path.isfile(p):
            return self._send_json(404, {"error": f"{name} missing"})
        with open(p, "rb") as f:
            data = f.read()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_PUT(self):
        if urlparse(self.path).path != "/api/layout":
            return self._send_json(404, {"error": "not found"})
        try:
            payload = self._read_json()
        except json.JSONDecodeError:
            return self._send_json(400, {"error": "invalid json"})
        cfg = payload.get("config")
        try:
            base_version = int(payload.get("base_version", -1))
        except (TypeError, ValueError):
            return self._send_json(400, {"error": "bad base_version"})
        with _lock:
            st = load_state()
            if base_version != st["version"]:
                return self._send_json(409, {
                    "error": f"version conflict: base {base_version} != current {st['version']}",
                    "current_version": st["version"],
                    "current": public_record(st),
                })
            err = validate_config(cfg)
            if err:
                return self._send_json(400, {"error": err})
            st["config"] = cfg
            st["version"] += 1
            st["updated_at"] = now_iso()
            st["checksum"] = checksum_of(cfg)
            st.setdefault("history", []).append({
                "version": st["version"],
                "updated_at": st["updated_at"],
                "checksum": st["checksum"],
                "client": str(payload.get("client", "unknown"))[:64],
                "config": cfg,
            })
            st["history"] = st["history"][-100:]
            save_state(st)
            return self._send_json(200, public_record(st))

    def do_POST(self):
        path = urlparse(self.path).path
        ctype = self.headers.get("Content-Type", "")
        if path == "/api/reports/crop":
            try:
                payload = self._read_json()
            except json.JSONDecodeError:
                return self._send_json(400, {"error": "invalid json"})
            crop = payload.get("crop")
            if not isinstance(crop, dict) or not all(
                isinstance(crop.get(k), (int, float)) for k in ("x", "y", "w", "h")
            ):
                return self._send_json(400, {"error": "bad crop"})
            with _lock:
                append_report(payload)
            return self._send_json(200, {"ok": True})
        if path == "/api/images/publish":
            return self._publish_image(ctype)
        self._send_json(404, {"error": "not found"})

    def _publish_image(self, ctype):
        """multipart/form-data: field 'image'。发布新版本配置指向新图。
        用临时名落盘 -> fsync -> rename，保证展示端在缩放中拿到的要么旧图要么新图。"""
        length = int(self.headers.get("Content-Length", "0") or 0)
        raw = self.rfile.read(length) if length else b""
        if "multipart/form-data" not in ctype or "boundary=" not in ctype:
            return self._send_json(400, {"error": "expected multipart"})
        boundary = ctype.split("boundary=", 1)[1].strip().encode()
        delimiter = b"--" + boundary
        parts = raw.split(delimiter)
        image_data = None
        image_name = None
        for part in parts:
            if b"Content-Disposition" not in part:
                continue
            head, _, body = part.partition(b"\r\n\r\n")
            body = body.rstrip(b"\r\n")
            if body.endswith(b"--"):
                body = body[:-2]
            text = head.decode("latin-1", "ignore")
            # 只取 Content-Disposition 那一行，避免把 Content-Type 头并入 filename
            disp = ""
            for line in text.splitlines():
                if "Content-Disposition" in line:
                    disp = line
                    break
            if 'name="image"' in disp:
                image_data = body
                if "filename=" in disp:
                    fn = disp.split("filename=", 1)[1].split(";", 1)[0]
                    fn = fn.strip().strip('"').splitlines()[0]
                    image_name = os.path.basename(fn)
        if not image_data:
            return self._send_json(400, {"error": "no image part"})
        ext = os.path.splitext(image_name or "")[1].lower()
        if ext not in (".png", ".jpg", ".jpeg"):
            return self._send_json(400, {"error": "only png/jpeg"})
        image_id = f"img-{uuid.uuid4().hex[:10]}{ext}"
        final_path = os.path.join(ASSETS_DIR, image_id)
        tmp_path = final_path + ".part"
        with open(tmp_path, "wb") as f:
            f.write(image_data)
            f.flush()
            os.fsync(f.fileno())
        try:
            w, h = image_size(tmp_path)
        except Exception as exc:  # noqa: BLE001
            os.unlink(tmp_path)
            return self._send_json(400, {"error": f"bad image: {exc}"})
        os.replace(tmp_path, final_path)
        with _lock:
            st = load_state()
            new_cfg = dict(st["config"])
            new_cfg.update({
                "image_id": image_id,
                "image_w": w,
                "image_h": h,
            })
            st["config"] = new_cfg
            st["version"] += 1
            st["updated_at"] = now_iso()
            st["checksum"] = checksum_of(new_cfg)
            st.setdefault("history", []).append({
                "version": st["version"],
                "updated_at": st["updated_at"],
                "checksum": st["checksum"],
                "client": "image-publish",
                "config": new_cfg,
            })
            save_state(st)
            rec = public_record(st)
        return self._send_json(200, rec)


def main():
    os.makedirs(DATA_DIR, exist_ok=True)
    os.makedirs(ASSETS_DIR, exist_ok=True)
    with _lock:
        st = load_state()
        # 用真实背景图尺寸修正默认记录
        bg = os.path.join(ASSETS_DIR, "background.png")
        if os.path.isfile(bg):
            try:
                w, h = image_size(bg)
                if (st["config"].get("image_w"), st["config"].get("image_h")) != (w, h):
                    st["config"]["image_w"] = w
                    st["config"]["image_h"] = h
                    st["checksum"] = checksum_of(st["config"])
                    save_state(st)
            except Exception:
                pass
    port = int(os.environ.get("LAYOUT_SERVER_PORT", "8080"))
    srv = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    print(f"layout server listening on :{port}")
    srv.serve_forever()


if __name__ == "__main__":
    main()
