"""Host test: client helpers against a pyserial-free fake device mirroring src/fleet_config.cpp rules.
Run: python3 -m unittest tests/test_provision.py"""
import io, json, os, sys, unittest
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import vm_provision as vp

KEYS = {"ap_ssid": ("s", 1, 32, False), "ap_pass": ("s", 8, 63, True), "api_token": ("s", 16, 128, True),
        "apn": ("s", 0, 63, False), "apn_user": ("s", 0, 63, False), "apn_pass": ("s", 0, 63, True)}

class FakeDevice:
    def __init__(self): self.nvs, self.out = {}, io.BytesIO(); self.log("[I] boot log")
    def log(self, s): self.out.write((s + "\n").encode())
    def reply(self, o): self.out.write((json.dumps(o) + "\n").encode())
    def write(self, b):
        self.out = io.BytesIO(); self.log("event: noise")
        r = json.loads(b.decode()); c = r.get("cmd")
        if r.get("v") != 1: return self.reply({"v": 1, "ok": False, "err": "unsupported version"})
        if c == "hello": return self.reply({"v": 1, "ok": True, "project": "victormodem", "fw": "t", "env": "x", "chip": "esp32", "keys": list(KEYS)})
        if c == "get": return self.reply({"v": 1, "ok": True, "cfg": {k: "***" if KEYS[k][3] else self.nvs.get(k, "") for k in KEYS}})
        if c == "factory_reset": self.nvs.clear(); return self.reply({"v": 1, "ok": True})
        if c == "provision":
            cfg = r.get("cfg")
            for k, v in cfg.items():
                if k not in KEYS: return self.reply({"v": 1, "ok": False, "err": "unknown key: " + k})
                _, lo, hi, _ = KEYS[k]
                if not isinstance(v, str) or not lo <= len(v) <= hi:
                    return self.reply({"v": 1, "ok": False, "err": f"{k}: length must be {lo}-{hi}"})
            self.nvs.update(cfg); return self.reply({"v": 1, "ok": True})
        self.reply({"v": 1, "ok": False, "err": "unknown cmd"})
    def flush(self): self.out.seek(0)
    def readline(self): return self.out.readline()

class T(unittest.TestCase):
    def setUp(self): self.d = FakeDevice()
    def test_encode(self): self.assertEqual(vp.encode("hello"), b'{"v":1,"cmd":"hello"}\n')
    def test_hello_skips_logs(self):
        r = vp.transact(self.d, "hello"); self.assertTrue(r["ok"]); self.assertIn("api_token", r["keys"])
    def test_provision_and_get_masks(self):
        self.assertTrue(vp.transact(self.d, "provision", cfg={"api_token": "x" * 16, "apn": "internet"})["ok"])
        g = vp.transact(self.d, "get")["cfg"]; self.assertEqual(g["api_token"], "***"); self.assertEqual(g["apn"], "internet")
    def test_rejects(self):
        self.assertFalse(vp.transact(self.d, "provision", cfg={"bogus": "1"})["ok"])
        self.assertFalse(vp.transact(self.d, "provision", cfg={"ap_pass": "short"})["ok"])
        self.assertFalse(vp.transact(self.d, "provision", cfg={"api_token": "x" * 129})["ok"])
    def test_no_reply(self):
        with self.assertRaises(TimeoutError): vp.read_reply(io.BytesIO(b"log only\n").readline)

if __name__ == "__main__": unittest.main()
