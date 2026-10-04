import json
import os
import sys
import tempfile
import threading
import unittest
from http.server import ThreadingHTTPServer
from urllib import request

sys.path.insert(0, os.path.dirname(__file__))


def _make_server():
    tmp = tempfile.mkdtemp()
    os.environ["DATA_DIR"] = tmp
    import app as appmod
    # 确保每个用例独立 DB
    appmod.STORE = appmod.Store(os.path.join(tmp, "layout.db"),
                                os.path.join(tmp, "images"))
    appmod._seed_default_image()
    httpd = ThreadingHTTPServer(("127.0.0.1", 0), appmod.Handler)
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    return httpd, f"http://127.0.0.1:{httpd.server_address[1]}"


def call(method, url, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = request.Request(url, data=data, method=method,
                          headers={"Content-Type": "application/json"})
    try:
        with request.urlopen(req) as r:
            return r.status, json.loads(r.read().decode())
    except Exception as e:
        return e.code, json.loads(e.read().decode())


class HttpTests(unittest.TestCase):
    httpd = None
    base = None

    @classmethod
    def setUpClass(cls):
        # 只起一次服务；每个用例在 setUp 里给全局 STORE 换一个全新数据库
        tmp = tempfile.mkdtemp()
        os.environ["DATA_DIR"] = tmp
        import app as appmod
        cls.appmod = appmod
        cls.httpd = ThreadingHTTPServer(("127.0.0.1", 0), appmod.Handler)
        cls.base = f"http://127.0.0.1:{cls.httpd.server_address[1]}"
        t = threading.Thread(target=cls.httpd.serve_forever, daemon=True)
        t.start()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()

    def setUp(self):
        tmp = tempfile.mkdtemp()
        self.appmod.STORE = self.appmod.Store(
            os.path.join(tmp, "layout.db"), os.path.join(tmp, "images"))
        self.appmod._seed_default_image()

    def test_health(self):
        st, b = call("GET", self.base + "/api/health")
        self.assertEqual(st, 200)
        self.assertTrue(b["ok"])

    def test_save_conflict_returns_server_version(self):
        st, b = call("POST", self.base + "/api/layout",
                     {"expected_version": 1, "fit": "contain"})
        self.assertEqual(st, 200)
        st, b = call("POST", self.base + "/api/layout",
                     {"expected_version": 1, "fit": "cover"})
        self.assertEqual(st, 409)
        self.assertEqual(b["error"], "version_conflict")
        self.assertEqual(b["server_version"], 2)
        self.assertEqual(b["server_data"]["fit"], "contain")

    def test_session_undo_flow(self):
        call("POST", self.base + "/api/layout",
             {"expected_version": 1, "fit": "contain"})
        st, b = call("POST", self.base + "/api/session", {"session": "ws1"})
        self.assertEqual(st, 200)
        self.assertEqual(b["base_version"], 2)
        call("POST", self.base + "/api/layout",
             {"expected_version": 2, "fit": "cover"})
        st, b = call("POST", self.base + "/api/undo", {"session": "ws1"})
        self.assertEqual(st, 200)
        self.assertEqual(b["data"]["fit"], "contain")
        st, b = call("POST", self.base + "/api/undo", {"session": "ws1"})
        self.assertEqual(st, 409)
        self.assertEqual(b["error"], "session_boundary")

    def test_publish_hook_and_device_resolution(self):
        st, b = call("POST", self.base + "/api/test/publish?device_id=screen-A")
        self.assertEqual(st, 200)
        self.assertIn("image_rev", b)
        st, b = call("GET", self.base + "/api/layout?device_id=screen-A")
        self.assertEqual(st, 200)

    def test_report_accepted_and_listed(self):
        st, b = call("POST", self.base + "/api/reports",
                     {"device_id": "term-1", "fit": "cover",
                      "crop": [0.1, 0.2, 0.3, 0.4], "layout_version": 1})
        self.assertEqual(st, 201)
        st, b = call("GET", self.base + "/api/reports")
        self.assertEqual(st, 200)
        self.assertTrue(any(r["device_id"] == "term-1" for r in b["reports"]))


if __name__ == "__main__":
    unittest.main()
