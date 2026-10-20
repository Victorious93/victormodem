"""Phone-line services on top of AtEngine; shared JSON protocol (same as ESP32 firmware v0)."""
from __future__ import annotations
import asyncio, json, logging, re, shutil, subprocess, time
from pathlib import Path
from typing import Any, Callable
from .at import AtEngine, AtResult

log = logging.getLogger(__name__)
NUM_RE = re.compile(r"^\+?[0-9*#]{3,20}$")
CMGL_RE = re.compile(r'^\+CMGL:\s*(\d+),"([^"]*)","([^"]*)",[^,]*,"([^"]*)"')
CMGR_RE = re.compile(r'^\+CMGR:\s*"([^"]*)","([^"]*)",[^,]*,"([^"]*)"')
EventFn = Callable[[dict], None]

def gsm_ok(text: str) -> bool:
    return 0 < len(text) <= 160 and all(32 <= ord(c) < 127 or c in "\r\n" for c in text)

class Hub:
    def __init__(self, at: AtEngine, cfg, run=subprocess.run):
        self.at, self.cfg, self._run = at, cfg, run
        self.listeners: list[EventFn] = []
        self.call_state = "idle"
        self.state = Path(cfg.state_dir)
        self.state.mkdir(parents=True, exist_ok=True)
        at.on_urc = self.on_urc
        self._bg: set[asyncio.Task] = set()

    # ---- events ----
    def emit(self, event: str, data: dict) -> None:
        msg = {"type": "event", "event": event, "data": data}
        for fn in list(self.listeners):
            try:
                fn(msg)
            except Exception:  # noqa: BLE001
                log.exception("listener failed")

    async def on_urc(self, line: str) -> None:
        if line == "RING":
            self.call_state = "ringing"; self.emit("ring", {})
        elif line.startswith("+CLIP:"):
            m = re.search(r'"([^"]*)"', line)
            self.emit("incoming_call", {"number": m.group(1) if m else ""})
        elif line.startswith(("NO CARRIER", "VOICE CALL: END", "MISSED_CALL")):
            self.call_state = "idle"; self.emit("call_ended", {"raw": line})
        elif line.startswith("VOICE CALL: BEGIN"):
            self.call_state = "active"; self.emit("call_active", {})
        elif line.startswith("+CMTI:"):
            idx = int(line.rsplit(",", 1)[1])
            self.emit("sms_received", {"index": idx})
            t = asyncio.create_task(self._fetch_sms(idx)); self._bg.add(t); t.add_done_callback(self._bg.discard)
        else:
            self.emit("urc", {"raw": line})

    async def _fetch_sms(self, idx: int) -> None:
        r = await self.sms_read(idx)
        if r.get("ok"):
            with open(self.state / "sms.jsonl", "a") as f:
                f.write(json.dumps({**r, "ts": time.time()}) + "\n")
            self.emit("sms", r)

    # ---- init ----
    async def init_modem(self) -> dict:
        info = await self.at.cmd("ATI", 3)
        for c in ("ATE0", "AT+CMEE=2", "AT+CLIP=1", "AT+CMGF=1", 'AT+CSCS="GSM"',
                  "AT+CNMI=2,1,0,0,0", "AT+CEREG=2"):
            await self.at.cmd(c, 3)
        return {"ok": info.ok, "info": info.lines}

    # ---- voice ----
    async def dial(self, number: str) -> dict:
        if not NUM_RE.match(number or ""):
            return {"ok": False, "error": "bad number"}
        r = await self.at.cmd(f"ATD{number};", 30)
        if r.ok: self.call_state = "dialing"
        return _res(r)

    async def answer(self) -> dict:
        r = await self.at.cmd("ATA", 20)
        if r.ok: self.call_state = "active"
        return _res(r)

    async def hangup(self) -> dict:
        r = await self.at.cmd("AT+CHUP", 10)
        if r.ok: self.call_state = "idle"
        return _res(r)

    async def calls(self) -> dict:
        return _res(await self.at.cmd("AT+CLCC", 5))

    # ---- SMS ----
    async def sms_send(self, number: str, text: str) -> dict:
        if not NUM_RE.match(number or "") or not gsm_ok(text or ""):
            return {"ok": False, "error": "bad number or text (1-160 GSM chars)"}
        r = await self.at.cmd(f'AT+CMGS="{number}"', 60, payload=text)
        out = _res(r)
        m = [l for l in r.lines if l.startswith("+CMGS:")]
        if m: out["ref"] = int(m[0].split(":")[1])
        return out

    async def sms_read(self, idx: int) -> dict:
        r = await self.at.cmd(f"AT+CMGR={int(idx)}", 10)
        if not r.ok or not r.lines:
            return _res(r)
        m = CMGR_RE.match(r.lines[0])
        return {"ok": True, "index": idx, "status": m.group(1) if m else "",
                "from": m.group(2) if m else "", "time": m.group(3) if m else "",
                "text": "\n".join(r.lines[1:])}

    async def sms_list(self, filt: str = "ALL") -> dict:
        if filt not in ("ALL", "REC UNREAD", "REC READ", "STO UNSENT", "STO SENT"):
            return {"ok": False, "error": "bad filter"}
        r = await self.at.cmd(f'AT+CMGL="{filt}"', 20)
        msgs, cur = [], None
        for l in r.lines:
            m = CMGL_RE.match(l)
            if m:
                cur = {"index": int(m.group(1)), "status": m.group(2), "from": m.group(3),
                       "time": m.group(4), "text": ""}
                msgs.append(cur)
            elif cur is not None:
                cur["text"] = (cur["text"] + "\n" + l) if cur["text"] else l
        return {"ok": r.ok, "messages": msgs}

    async def sms_delete(self, idx: int) -> dict:
        return _res(await self.at.cmd(f"AT+CMGD={int(idx)}", 10))

    # ---- data (OS-level: ModemManager over QMI/MBIM, or RNDIS via usbnetmode) ----
    async def data_up(self, apn: str | None = None) -> dict:
        apn = apn or self.cfg.apn
        if self.cfg.data_backend == "modemmanager":
            if not shutil.which("mmcli"):
                return {"ok": False, "error": "mmcli not installed"}
            p = self._run(["mmcli", "-m", "any", f"--simple-connect=apn={apn},ip-type=ipv4v6"],
                          capture_output=True, text=True, timeout=90)
            return {"ok": p.returncode == 0, "raw": (p.stdout + p.stderr).strip()}
        if self.cfg.data_backend == "rndis":
            await self.at.cmd(f'AT+CGDCONT=1,"IPV4V6","{apn}"', 5)
            r = await self.at.cmd("AT$QCRMCALL=1,1", 30)  # SIMCom: start RNDIS/ECM data call
            return _res(r)
        return {"ok": False, "error": "data backend disabled"}

    async def data_down(self) -> dict:
        if self.cfg.data_backend == "modemmanager" and shutil.which("mmcli"):
            p = self._run(["mmcli", "-m", "any", "--simple-disconnect"], capture_output=True, text=True, timeout=30)
            return {"ok": p.returncode == 0}
        return _res(await self.at.cmd("AT$QCRMCALL=0,1", 15))

    async def status(self) -> dict:
        out: dict[str, Any] = {"ok": True, "model": "SIM8262A", "call_state": self.call_state}
        for k, c in (("csq", "AT+CSQ"), ("cereg", "AT+CEREG?"), ("c5greg", "AT+C5GREG?"),
                     ("cops", "AT+COPS?"), ("cpsi", "AT+CPSI?")):
            r = await self.at.cmd(c, 5)
            out[k] = r.lines[0] if r.ok and r.lines else None
        if out["csq"]:
            rssi = int(out["csq"].split(":")[1].split(",")[0])
            out["rssi_dbm"] = None if rssi == 99 else -113 + 2 * rssi
        return out

    # ---- protocol router (shared by REST /api, WebSocket and the ESP32 UART link) ----
    async def handle(self, req: dict) -> dict:
        rid = req.get("id", 0)
        if req.get("token") != self.cfg.token:
            return {"type": "response", "id": rid, "result": {"ok": False, "error": "unauthorized"}}
        c = req.get("cmd", "")
        table = {
            "dial": lambda: self.dial(str(req.get("number", ""))),
            "answer": self.answer, "hangup": self.hangup, "calls": self.calls,
            "sms_send": lambda: self.sms_send(str(req.get("number", "")), str(req.get("text", ""))),
            "sms_read": lambda: self.sms_read(int(req.get("index", 0))),
            "sms_list": lambda: self.sms_list(str(req.get("filter", "ALL"))),
            "sms_delete": lambda: self.sms_delete(int(req.get("index", 0))),
            "data_up": lambda: self.data_up(req.get("apn") or None),
            "data_down": self.data_down, "status": self.status,
        }
        fn = table.get(c)
        try:
            res = await fn() if fn else {"ok": False, "error": "unknown cmd"}
        except (ValueError, TypeError) as e:
            res = {"ok": False, "error": str(e)}
        return {"type": "response", "id": rid, "result": res}

def _res(r: AtResult) -> dict:
    d: dict[str, Any] = {"ok": r.ok, "raw": "\n".join(r.lines)}
    if not r.ok: d["error"] = r.final
    return d
