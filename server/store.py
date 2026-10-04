"""SQLite 持久化：布局版本入库 + 乐观锁 + 会话基线撤销 + 上报入库。

只用标准库。表结构：
  layouts       每个 layout_id 一行当前指针（version, config_json, image_rev）
  layout_vers   全量版本流水（version, config_json, image_rev, author, ts, kind）
  edit_sessions 编辑会话基线（session, base_version）——撤销只作用于本次编辑基础
  reports       C 端上报的真实裁切区域
  images        当前图片（全局或按 layout），rev 单调；发布新图不改 fit/focus
"""
from __future__ import annotations

import json
import os
import sqlite3
import threading
import time
from typing import Any, Optional

DEFAULT_CONFIG: dict[str, Any] = {
    "layout_id": "default",
    "version": 1,
    "image_rev": 1,
    "image_url": "/api/images/current",
    "fit": "cover",
    "focus": [0.5, 0.5],
    "orientation": -1,
    "texture_strategy": "atlas",
    "budget_bytes_mb": 32,
    "budget_entries": 16,
    "min_bucket": 128,
    "max_bucket": 2048,
    "bg_color": 0xFF000000,
}

VALID_FIT = {"cover", "contain", "stretch"}
VALID_STRATEGY = {"atlas", "runtime"}
VALID_ORIENT = {-1, 0, 90, 180, 270}


class StoreError(Exception):
    pass


class VersionConflict(StoreError):
    """客户端基于旧版本保存（409）。"""


class SessionBoundary(StoreError):
    """撤销越过本次编辑会话基线（409）。"""


class Store:
    def __init__(self, db_path: str, image_dir: str):
        self.db_path = db_path
        self.image_dir = image_dir
        os.makedirs(os.path.dirname(db_path) or ".", exist_ok=True)
        os.makedirs(image_dir, exist_ok=True)
        self.lock = threading.RLock()
        self._init_db()

    def _conn(self):
        c = sqlite3.connect(self.db_path)
        c.row_factory = sqlite3.Row
        return c

    def _init_db(self):
        with self.lock, self._conn() as c:
            c.executescript(
                """
                CREATE TABLE IF NOT EXISTS layouts(
                    layout_id TEXT PRIMARY KEY,
                    version INTEGER NOT NULL,
                    image_rev INTEGER NOT NULL,
                    config_json TEXT NOT NULL
                );
                CREATE TABLE IF NOT EXISTS layout_vers(
                    layout_id TEXT NOT NULL,
                    version INTEGER NOT NULL,
                    config_json TEXT NOT NULL,
                    image_rev INTEGER NOT NULL,
                    author TEXT,
                    kind TEXT NOT NULL,
                    ts REAL NOT NULL,
                    PRIMARY KEY(layout_id, version)
                );
                CREATE TABLE IF NOT EXISTS edit_sessions(
                    session TEXT PRIMARY KEY,
                    layout_id TEXT NOT NULL,
                    base_version INTEGER NOT NULL,
                    undo_cursor INTEGER,
                    created REAL NOT NULL
                );
                CREATE TABLE IF NOT EXISTS reports(
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    device_id TEXT NOT NULL,
                    layout_version INTEGER,
                    image_rev INTEGER,
                    body_json TEXT NOT NULL,
                    ts REAL NOT NULL
                );
                CREATE TABLE IF NOT EXISTS images(
                    layout_id TEXT PRIMARY KEY,
                    rev INTEGER NOT NULL,
                    ctype TEXT NOT NULL,
                    updated REAL NOT NULL
                );
                """
            )
            row = c.execute("SELECT layout_id FROM layouts WHERE layout_id='default'").fetchone()
            if not row:
                cfg = dict(DEFAULT_CONFIG)
                js = json.dumps(cfg)
                c.execute(
                    "INSERT INTO layouts(layout_id,version,image_rev,config_json) VALUES('default',1,1,?)",
                    (js,),
                )
                c.execute(
                    "INSERT INTO layout_vers(layout_id,version,config_json,image_rev,author,kind,ts)"
                    " VALUES('default',1,?,1,'system','init',?)",
                    (js, time.time()),
                )

    # ---------- 读取 ----------
    def get_layout(self, layout_id: str = "default", since: int = 0) -> Optional[dict]:
        with self.lock, self._conn() as c:
            row = c.execute(
                "SELECT version, image_rev, config_json FROM layouts WHERE layout_id=?",
                (layout_id,),
            ).fetchone()
            if not row:
                return None
            cfg = json.loads(row["config_json"])
            cfg["version"] = row["version"]
            cfg["image_rev"] = row["image_rev"]
            if since and row["version"] <= since:
                return {"version": row["version"], "unchanged": True, "data": cfg}
            return {"version": row["version"], "data": cfg}

    def history(self, layout_id: str = "default", limit: int = 50):
        with self.lock, self._conn() as c:
            rows = c.execute(
                "SELECT version,image_rev,config_json,author,kind,ts FROM layout_vers"
                " WHERE layout_id=? ORDER BY version DESC LIMIT ?",
                (layout_id, limit),
            ).fetchall()
            out = []
            for r in rows:
                d = json.loads(r["config_json"])
                out.append({
                    "version": r["version"],
                    "image_rev": r["image_rev"],
                    "author": r["author"],
                    "kind": r["kind"],
                    "ts": r["ts"],
                    "fit": d.get("fit"),
                    "focus": d.get("focus"),
                })
            return out

    # ---------- 校验 ----------
    @staticmethod
    def validate(patch: dict) -> dict:
        cleaned: dict[str, Any] = {}
        if "fit" in patch:
            if patch["fit"] not in VALID_FIT:
                raise StoreError(f"invalid fit {patch['fit']!r}")
            cleaned["fit"] = patch["fit"]
        if "texture_strategy" in patch:
            if patch["texture_strategy"] not in VALID_STRATEGY:
                raise StoreError(f"invalid texture_strategy")
            cleaned["texture_strategy"] = patch["texture_strategy"]
        if "orientation" in patch:
            o = int(patch["orientation"])
            if o not in VALID_ORIENT:
                raise StoreError("orientation must be -1/0/90/180/270")
            cleaned["orientation"] = o
        if "focus" in patch:
            f = patch["focus"]
            if (not isinstance(f, (list, tuple)) or len(f) != 2 or
                    not all(isinstance(v, (int, float)) and -0.001 <= v <= 1.001
                            for v in f)):
                raise StoreError("focus must be [x,y] within [0,1]")
            cleaned["focus"] = [max(0.0, min(1.0, float(v))) for v in f]
        for k in ("budget_bytes_mb", "budget_entries", "min_bucket", "max_bucket"):
            if k in patch:
                v = int(patch[k])
                if v <= 0:
                    raise StoreError(f"{k} must be positive")
                cleaned[k] = v
        if "bg_color" in patch:
            cleaned["bg_color"] = int(patch["bg_color"]) & 0xFFFFFFFF
        return cleaned

    # ---------- 写（乐观锁） ----------
    def save_layout(self, layout_id: str, expected_version: int, patch: dict,
                    author: str = "admin", kind: str = "save") -> dict:
        cleaned = self.validate(patch)
        with self.lock, self._conn() as c:
            row = c.execute(
                "SELECT version,image_rev,config_json FROM layouts WHERE layout_id=?",
                (layout_id,),
            ).fetchone()
            if not row:
                raise StoreError("unknown layout")
            if int(expected_version) != row["version"]:
                raise VersionConflict(
                    f"expected {row['version']} got {expected_version}")
            cfg = json.loads(row["config_json"])
            cfg.update(cleaned)
            new_version = row["version"] + 1
            cfg["version"] = new_version
            cfg["layout_id"] = layout_id
            js = json.dumps(cfg)
            c.execute(
                "UPDATE layouts SET version=?, image_rev=?, config_json=? WHERE layout_id=?",
                (new_version, row["image_rev"], js, layout_id),
            )
            c.execute(
                "INSERT INTO layout_vers(layout_id,version,config_json,image_rev,author,kind,ts)"
                " VALUES(?,?,?,?,?,?,?)",
                (layout_id, new_version, js, row["image_rev"], author, kind, time.time()),
            )
            if kind == "save":
                # 新的用户编辑成为当前撤销栈顶：清掉本 layout 会话的撤销指针
                c.execute(
                    "UPDATE edit_sessions SET undo_cursor=NULL WHERE layout_id=?",
                    (layout_id,),
                )
            return {"version": new_version, "data": cfg}

    # ---------- 编辑会话与撤销 ----------
    def open_session(self, session: str, layout_id: str = "default") -> dict:
        """撤销的"本次编辑基础"：打开会话时的版本就是不可越过的基线。"""
        with self.lock, self._conn() as c:
            row = c.execute(
                "SELECT version FROM layouts WHERE layout_id=?", (layout_id,)
            ).fetchone()
            if not row:
                raise StoreError("unknown layout")
            c.execute(
                "INSERT OR REPLACE INTO edit_sessions(session,layout_id,base_version,undo_cursor,created)"
                " VALUES(?,?,?,?,?)",
                (session, layout_id, row["version"], None, time.time()),
            )
            return {"session": session, "base_version": row["version"]}

    def undo(self, session: str, layout_id: str = "default") -> dict:
        """回退到上一版本，但绝不越过会话基线。"""
        with self.lock, self._conn() as c:
            srow = c.execute(
                "SELECT base_version,undo_cursor FROM edit_sessions WHERE session=?",
                (session,),
            ).fetchone()
            if not srow:
                raise SessionBoundary("unknown session; open one first")
            base = srow["base_version"]
            cursor = srow["undo_cursor"]  # 上次撤销恢复到的用户版本；None=尚未撤销
            row = c.execute(
                "SELECT version,image_rev,config_json FROM layouts WHERE layout_id=?",
                (layout_id,),
            ).fetchone()
            cur = row["version"]
            # 搜索上界：有 undo_cursor 时从它往前；否则从当前版本往前
            start = cursor if cursor is not None else cur
            if start <= base:
                raise SessionBoundary(
                    f"at session baseline v{base}; undo cannot go beyond this edit")
            # 找上一个用户编辑版本（跳过 undo/publish-image 等非编辑流水）
            prev = None
            v = start - 1
            while v >= 1:
                r = c.execute(
                    "SELECT version,config_json,image_rev,kind FROM layout_vers"
                    " WHERE layout_id=? AND version=?",
                    (layout_id, v),
                ).fetchone()
                if r is None:
                    break
                if r["kind"] in ("undo", "publish-image"):
                    v -= 1
                    continue
                prev = r
                break
            # 目标若低于基线，则回退到基线内容，且这是本会话最后一次可撤销
            if prev is None or prev["version"] < base:
                r = c.execute(
                    "SELECT version,config_json,image_rev FROM layout_vers"
                    " WHERE layout_id=? AND version=?",
                    (layout_id, base),
                ).fetchone()
                if r is None:
                    raise SessionBoundary("baseline missing")
                prev = r
            cfg = json.loads(prev["config_json"])
            new_version = cur + 1  # 撤销本身是一个新版本，不删历史
            cfg["version"] = new_version
            js = json.dumps(cfg)
            c.execute(
                "UPDATE layouts SET version=?,config_json=? WHERE layout_id=?",
                (new_version, js, layout_id),
            )
            c.execute(
                "INSERT INTO layout_vers(layout_id,version,config_json,image_rev,author,kind,ts)"
                " VALUES(?,?,?,?, 'self', 'undo',?)",
                (layout_id, new_version, js, prev["image_rev"], time.time()),
            )
            c.execute("UPDATE edit_sessions SET undo_cursor=? WHERE session=?",
                      (prev["version"], session))
            return {"version": new_version, "base_version": base,
                    "restored_version": prev["version"], "data": cfg}

    # ---------- 新图发布：image_rev 单调，模式不动 ----------
    def publish_image(self, layout_id: str, ctype: str = "image/png") -> int:
        with self.lock, self._conn() as c:
            row = c.execute(
                "SELECT image_rev FROM layouts WHERE layout_id=?", (layout_id,)
            ).fetchone()
            if not row:
                raise StoreError("unknown layout")
            new_rev = row["image_rev"] + 1
            c.execute(
                "UPDATE layouts SET image_rev=? WHERE layout_id=?",
                (new_rev, layout_id),
            )
            c.execute(
                "INSERT OR REPLACE INTO images(layout_id,rev,ctype,updated) VALUES(?,?,?,?)",
                (layout_id, new_rev, ctype, time.time()),
            )
            # 配置版本也递增（内容版本），使轮询端立刻发现"新图但模式不变"
            crow = c.execute(
                "SELECT version,config_json FROM layouts WHERE layout_id=?",
                (layout_id,),
            ).fetchone()
            nv = crow["version"] + 1
            cfg = json.loads(crow["config_json"])
            cfg["version"] = nv
            js = json.dumps(cfg)
            c.execute("UPDATE layouts SET version=?, config_json=? WHERE layout_id=?",
                      (nv, js, layout_id))
            c.execute(
                "INSERT INTO layout_vers(layout_id,version,config_json,image_rev,author,kind,ts)"
                " VALUES(?,?,?,?, 'ops', 'publish-image',?)",
                (layout_id, nv, js, new_rev, time.time()),
            )
            return new_rev

    def image_path(self, layout_id: str, rev: Optional[int] = None):
        with self.lock, self._conn() as c:
            row = c.execute(
                "SELECT rev,ctype FROM images WHERE layout_id=?", (layout_id,)
            ).fetchone()
            cur_rev = row["rev"] if row else 1
            ctype = row["ctype"] if row else "image/png"
        rev = rev or cur_rev
        path = os.path.join(self.image_dir, f"{layout_id}-{rev}.png")
        return path, rev, ctype

    def store_image_bytes(self, layout_id: str, rev: int, data: bytes):
        path = os.path.join(self.image_dir, f"{layout_id}-{rev}.png")
        with open(path, "wb") as f:
            f.write(data)

    # ---------- 上报 ----------
    def add_report(self, body: dict) -> int:
        required = {"device_id", "fit", "crop"}
        missing = required - set(body)
        if missing:
            raise StoreError(f"report missing fields: {sorted(missing)}")
        crop = body.get("crop")
        if (not isinstance(crop, list) or len(crop) != 4 or
                not all(isinstance(v, (int, float)) for v in crop)):
            raise StoreError("crop must be [x,y,w,h]")
        with self.lock, self._conn() as c:
            cur = c.execute(
                "INSERT INTO reports(device_id,layout_version,image_rev,body_json,ts)"
                " VALUES(?,?,?,?,?)",
                (body["device_id"], body.get("layout_version"),
                 body.get("image_rev"), json.dumps(body), time.time()),
            )
            return cur.lastrowid

    def latest_reports(self, limit: int = 20):
        with self.lock, self._conn() as c:
            rows = c.execute(
                "SELECT device_id,layout_version,image_rev,body_json,ts FROM reports"
                " ORDER BY id DESC LIMIT ?", (limit,)
            ).fetchall()
            return [{
                "device_id": r["device_id"],
                "layout_version": r["layout_version"],
                "image_rev": r["image_rev"],
                "body": json.loads(r["body_json"]),
                "ts": r["ts"],
            } for r in rows]
