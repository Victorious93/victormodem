import asyncio, json, sys, pathlib
import pytest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from fastapi.testclient import TestClient
from victormodem_hub.at import AtEngine
from victormodem_hub.config import Config, load
from victormodem_hub.service import Hub
from victormodem_hub.uart_link import UartLink
from victormodem_hub.app import create_app
from fake_modem import FakeModem

TOKEN = "t0k"

@pytest.fixture
def cfg(tmp_path):
    return Config(token=TOKEN, state_dir=str(tmp_path), data_backend="rndis", esp_enabled=False)

async def mk(cfg):
    fm = FakeModem(); at = AtEngine(fm); hub = Hub(at, cfg); at.start()
    await hub.init_modem(); events = []; hub.listeners.append(events.append)
    return fm, at, hub, events

def run(coro): return asyncio.run(coro)

def test_init_and_dial_hangup(cfg):
    async def t():
        fm, at, hub, _ = await mk(cfg)
        assert "AT+CNMI=2,1,0,0,0" in fm.sent and "AT+CLIP=1" in fm.sent
        assert (await hub.dial("+15551234567"))["ok"] and hub.call_state == "dialing"
        assert "ATD+15551234567;" in fm.sent
        assert (await hub.hangup())["ok"] and "AT+CHUP" in fm.sent
        assert not (await hub.dial("123;ATZ"))["ok"]   # injection rejected
        fm.dial_result = "BUSY"
        r = await hub.dial("5551234567"); assert not r["ok"] and r["error"] == "BUSY"
        await at.stop()
    run(t())

def test_clcc_response_not_urc(cfg):
    async def t():
        fm, at, hub, ev = await mk(cfg)
        r = await hub.calls(); assert "+CLCC: 1" in r["raw"]; assert not ev
        await at.stop()
    run(t())

def test_sms_send_prompt(cfg):
    async def t():
        fm, at, hub, _ = await mk(cfg)
        r = await hub.sms_send("+15551234567", "hi there")
        assert r["ok"] and r["ref"] == 42 and "SMS:hi there" in fm.sent
        assert not (await hub.sms_send("+1555", "x" * 161))["ok"]
        await at.stop()
    run(t())

def test_sms_list_parse(cfg):
    async def t():
        fm, at, hub, _ = await mk(cfg)
        r = await hub.sms_list("ALL")
        assert [m["index"] for m in r["messages"]] == [1, 2]
        assert r["messages"][1]["text"] == "second\nline2"
        assert not (await hub.sms_list('ALL";AT+X'))["ok"]
        await at.stop()
    run(t())

def test_urcs_ring_and_cmti(cfg, tmp_path):
    async def t():
        fm, at, hub, ev = await mk(cfg)
        fm.urc("RING"); fm.urc('+CLIP: "+15559998888",145,,,,0'); fm.urc('+CMTI: "SM",3')
        for _ in range(100):
            await asyncio.sleep(0.01)
            if any(e["event"] == "sms" for e in ev): break
        names = [e["event"] for e in ev]
        assert names[:3] == ["ring", "incoming_call", "sms_received"]
        assert ev[1]["data"]["number"] == "+15559998888"
        sms = [e for e in ev if e["event"] == "sms"][0]["data"]
        assert sms["from"] == "+15551230000" and sms["text"] == "Hello from Verizon"
        assert (tmp_path / "sms.jsonl").exists()
        fm.urc("NO CARRIER"); await asyncio.sleep(0.05)
        assert ev[-1]["event"] == "call_ended" and hub.call_state == "idle"
        await at.stop()
    run(t())

def test_timeout(cfg):
    async def t():
        fm, at, hub, _ = await mk(cfg)
        r = await at.cmd("AT+HANG", 0.2); assert not r.ok and r.final == "TIMEOUT"
        assert (await at.cmd("AT", 1)).ok   # engine recovers
        await at.stop()
    run(t())

def test_protocol_auth(cfg):
    async def t():
        fm, at, hub, _ = await mk(cfg)
        bad = await hub.handle({"id": 1, "token": "no", "cmd": "status"})
        assert bad["result"]["error"] == "unauthorized"
        ok = await hub.handle({"id": 2, "token": TOKEN, "cmd": "status"})
        assert ok["id"] == 2 and ok["result"]["rssi_dbm"] == -73
        assert (await hub.handle({"token": TOKEN, "cmd": "nope"}))["result"]["error"] == "unknown cmd"
        await at.stop()
    run(t())

class Pipe:
    def __init__(self): self.inq = asyncio.Queue(); self.out = []
    async def read(self): return await self.inq.get()
    async def write(self, d): self.out.append(d)
    async def close(self): pass

def test_uart_link(cfg):
    async def t():
        fm, at, hub, _ = await mk(cfg)
        p = Pipe(); link = UartLink(p, hub); link.start()
        p.inq.put_nowait(b'{"type":"ping"}\n{"id":7,"token":"t0k","cmd":"dial","num')
        p.inq.put_nowait(b'ber":"+15551112222"}\nnot json\n')
        fm.urc("RING")
        for _ in range(100):
            await asyncio.sleep(0.01)
            msgs = [json.loads(x) for x in p.out]
            if any(m.get("id") == 7 for m in msgs): break
        types = [m.get("type") for m in msgs]
        assert "pong" in types and "event" in types
        assert [m for m in msgs if m.get("id") == 7][0]["result"]["ok"]
        assert any(m.get("error") == "bad json" for m in msgs)
        await link.stop(); await at.stop()
    run(t())

def test_rest_and_ws(cfg):
    app = create_app(cfg, at_transport=FakeModem())
    with TestClient(app) as c:
        assert c.get("/api/status").status_code == 401
        h = {"Authorization": f"Bearer {TOKEN}"}
        assert c.get("/api/status", headers=h).json()["csq"] == "+CSQ: 20,99"
        assert c.post("/api/sms", json={"number": "+15551234567", "text": "yo"}, headers=h).json()["ok"]
        assert c.post("/api/call/dial", json={"number": "+15551234567"}, headers=h).json()["ok"]
        assert c.post("/api", json={"id": 3, "token": TOKEN, "cmd": "hangup"}).json()["result"]["ok"]
        with c.websocket_connect(f"/ws?token={TOKEN}") as ws:
            ws.send_json({"id": 9, "cmd": "sms_list"})
            assert ws.receive_json()["id"] == 9
        with pytest.raises(Exception):
            with c.websocket_connect("/ws?token=bad") as ws: ws.receive_json()

def test_config_load(tmp_path):
    p = tmp_path / "c.toml"; p.write_text('token="abc"\nesp_baud=115200\nfoo=1\n')
    c = load(str(p)); assert c.token == "abc" and c.esp_baud == 115200 and c.extra == {"foo": 1}
    assert load(str(tmp_path / "missing.toml")).token == "change-this-token"
