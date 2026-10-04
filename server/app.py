"""布局调试后端：版本校验入库、会话撤销、新图发布、裁切上报。纯标准库 HTTP。"""
from __future__ import annotations

import json
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

sys.path.insert(0, os.path.dirname(__file__))
from store import Store, StoreError, VersionConflict, SessionBoundary, DEFAULT_CONFIG

WEB_DIR = os.path.join(os.path.dirname(__file__), "..", "web")
DATA_DIR = os.environ.get("DATA_DIR", os.path.join(os.path.dirname(__file__), "..", "data"))
STORE = Store(os.path.join(DATA_DIR, "layout.db"),
              os.path.join(DATA_DIR, "images"))


def _seed_default_image():
    """首次启动把仓库 assets/background.png 登记为 rev=1。"""
    path, rev, _ = STORE.image_path("default")
    if not os.path.exists(path):
        fb = os.path.join(os.path.dirname(__file__), "..", "assets", "background.png")
        if os.path.exists(fb):
            with open(fb, "rb") as f:
                data = f.read()
            STORE.store_image_bytes("default", 1, data)


_seed_default_image()

DEVICE_LAYOUT: dict[str, str] = {}  # 可扩展：设备注册时登记 device_id -> layout_id


def resolve_layout_id(q: dict) -> str:
    lid = q.get("layout_id")
    if lid:
        return lid
    dev = q.get("device_id")
    if dev:
        return DEVICE_LAYOUT.get(dev, "default")
    return "default"


_CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".png": "image/png",
    ".svg": "image/svg+xml",
    ".json": "application/json",
}


class Handler(BaseHTTPRequestHandler):
    server_version = "layoutd/1.0"

    def log_message(self, fmt, *args):
        if os.environ.get("HTTP_LOG"):
            sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    # ---------- 基础收发 ----------
    def _json(self, obj, status: int = 200):
        body = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _error(self, status: int, code: str, msg: str):
        self._json({"error": code, "message": msg}, status)

    def _read_json(self):
        n = int(self.headers.get("Content-Length", 0) or 0)
        raw = self.rfile.read(n) if n else b"{}"
        try:
            return json.loads(raw.decode() or "{}")
        except Exception as e:
            raise StoreError(f"invalid json body: {e}")

    def _qs(self):
        return {k: v[0] for k, v in parse_qs(urlparse(self.path).query).items()}

    # ---------- 路由 ----------
    def do_GET(self):
        u = urlparse(self.path)
        p, q = u.path, self._qs()
        try:
            if p == "/api/health":
                return self._json({"ok": True})
            if p == "/api/layout":
                layout_id = resolve_layout_id(q)
                since = int(q.get("since", 0) or 0)
                d = STORE.get_layout(layout_id, since)
                if not d:
                    return self._error(404, "no_layout", "unknown layout id")
                return self._json(d)
            if p == "/api/history":
                return self._json({"history": STORE.history(q.get("layout_id", "default"))})
            if p == "/api/reports":
                return self._json({"reports": STORE.latest_reports(50)})
            if p == "/api/images/current":
                layout_id = resolve_layout_id(q)
                path, rev, ctype = STORE.image_path(layout_id)
                if not os.path.exists(path):
                    # 回退仓库自带背景
                    fb = os.path.join(os.path.dirname(__file__), "..", "assets", "background.png")
                    if os.path.exists(fb):
                        path = fb
                    else:
                        return self._error(404, "no_image", "no image published")
                with open(path, "rb") as f:
                    data = f.read()
                self.send_response(200)
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Length", str(len(data)))
                self.send_header("X-Image-Rev", str(rev))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                return self.wfile.write(data)
            if p in ("/", "/index.html"):
                return self._static("index.html")
            if p.startswith("/") and ".." not in p:
                return self._static(p.lstrip("/"))
            self._error(404, "not_found", p)
        except StoreError as e:
            self._error(400, "bad_request", str(e))

    def _static(self, rel: str):
        path = os.path.normpath(os.path.join(WEB_DIR, rel))
        if not path.startswith(os.path.abspath(WEB_DIR)) or not os.path.isfile(path):
            return self._error(404, "not_found", rel)
        ext = os.path.splitext(path)[1]
        data = open(path, "rb").read()
        self.send_response(200)
        self.send_header("Content-Type", _CONTENT_TYPES.get(ext, "application/octet-stream"))
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        u = urlparse(self.path)
        p, q = u.path, self._qs()
        try:
            if p == "/api/layout":
                body = self._read_json()
                layout_id = body.get("layout_id", q.get("layout_id", "default"))
                expected = int(body.get("expected_version", 0))
                try:
                    d = STORE.save_layout(layout_id, expected, body,
                                          author=body.get("author", "admin"))
                except VersionConflict as vc:
                    cur = STORE.get_layout(layout_id)
                    return self._json({
                        "error": "version_conflict",
                        "message": str(vc),
                        "server_version": cur["version"],
                        "server_data": cur["data"],
                    }, 409)
                return self._json({"ok": True, **d})

            if p == "/api/session":
                body = self._read_json()
                sid = body.get("session") or q.get("session") or f"sess-{os.getpid()}-{id(self)}"
                d = STORE.open_session(sid, body.get("layout_id", "default"))
                return self._json({"ok": True, **d})

            if p == "/api/undo":
                body = self._read_json()
                sid = body.get("session") or q.get("session")
                if not sid:
                    return self._error(400, "no_session", "session required")
                try:
                    d = STORE.undo(sid, body.get("layout_id", "default"))
                except SessionBoundary as sb:
                    return self._json({
                        "error": "session_boundary",
                        "message": str(sb),
                    }, 409)
                return self._json({"ok": True, **d})

            if p == "/api/test/publish":
                layout_id = resolve_layout_id(q)
                rev = STORE.publish_image(layout_id)
                return self._json({"ok": True, "image_rev": rev})

            if p == "/api/images":
                layout_id = resolve_layout_id(q)
                return self._save_upload(layout_id)

            if p == "/api/reports":
                body = self._read_json()
                rid = STORE.add_report(body)
                return self._json({"ok": True, "id": rid}, 201)

            self._error(404, "not_found", p)
        except StoreError as e:
            self._error(400, "bad_request", str(e))

    def _save_upload(self, layout_id: str):
        ctype = self.headers.get("Content-Type", "image/png")
        n = int(self.headers.get("Content-Length", 0) or 0)
        data = self.rfile.read(n) if n else b""
        if not data.startswith(b"\x89PNG"):
            return self._error(400, "not_png", "only PNG uploads are accepted")
        rev = STORE.publish_image(layout_id, ctype)
        STORE.store_image_bytes(layout_id, rev, data)
        return self._json({"ok": True, "image_rev": rev})


def main():
    port = int(os.environ.get("PORT", "8080"))
    host = os.environ.get("HOST", "127.0.0.1")
    httpd = ThreadingHTTPServer((host, port), Handler)
    print(f"layoutd listening on http://{host}:{port}", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
