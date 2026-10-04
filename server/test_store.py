import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from store import Store, StoreError, VersionConflict, SessionBoundary


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.s = Store(os.path.join(self.d, "t.db"), os.path.join(self.d, "img"))

    def test_optimistic_lock_and_version_bump(self):
        d = self.s.get_layout("default")
        self.assertEqual(d["version"], 1)
        r = self.s.save_layout("default", 1, {"fit": "contain", "focus": [0.3, 0.7]})
        self.assertEqual(r["version"], 2)
        self.assertEqual(r["data"]["fit"], "contain")
        with self.assertRaises(VersionConflict):
            self.s.save_layout("default", 1, {"fit": "cover"})  # 迟到的旧版本

    def test_validation(self):
        with self.assertRaises(StoreError):
            self.s.save_layout("default", 1, {"fit": "nope"})
        with self.assertRaises(StoreError):
            self.s.save_layout("default", 1, {"focus": [2.0, 0.5]})
        with self.assertRaises(StoreError):
            self.s.save_layout("default", 1, {"orientation": 45})

    def test_undo_only_within_session_baseline(self):
        self.s.save_layout("default", 1, {"fit": "contain"})   # v2 远程/历史
        self.s.open_session("s1", "default")                   # 基线 v2
        self.s.save_layout("default", 2, {"fit": "cover"})     # v3 本次编辑
        self.s.save_layout("default", 3, {"fit": "stretch"})   # v4 本次编辑
        r = self.s.undo("s1")
        self.assertEqual(r["restored_version"], 3)
        self.assertEqual(r["data"]["fit"], "cover")
        r = self.s.undo("s1")
        self.assertEqual(r["restored_version"], 2)
        self.assertEqual(r["data"]["fit"], "contain")
        with self.assertRaises(SessionBoundary):
            self.s.undo("s1")  # 撤销不能越过本次编辑基础

    def test_undo_unknown_session_rejected(self):
        with self.assertRaises(SessionBoundary):
            self.s.undo("ghost")

    def test_publish_image_keeps_mode_and_bumps_rev(self):
        self.s.save_layout("default", 1, {"fit": "contain", "focus": [0.1, 0.9]})
        rev = self.s.publish_image("default")
        self.assertEqual(rev, 2)
        d = self.s.get_layout("default")
        self.assertEqual(d["data"]["image_rev"], 2)
        self.assertEqual(d["data"]["fit"], "contain")   # 新图不改模式
        self.assertEqual(d["data"]["focus"], [0.1, 0.9])

    def test_reports_stored(self):
        rid = self.s.add_report({"device_id": "A", "fit": "cover",
                                 "crop": [0, 0, 1, 1]})
        self.assertTrue(rid > 0)
        with self.assertRaises(StoreError):
            self.s.add_report({"device_id": "A"})  # 缺 crop
        rows = self.s.latest_reports()
        self.assertEqual(rows[0]["body"]["device_id"], "A")

    def test_history_includes_kinds(self):
        self.s.save_layout("default", 1, {"fit": "contain"}, kind="save")
        h = self.s.history()
        self.assertIn("save", {r["kind"] for r in h})


if __name__ == "__main__":
    unittest.main()
