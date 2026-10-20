"""FastAPI app: REST + WebSocket with bearer-token auth."""
from __future__ import annotations
import asyncio, contextlib, hmac, logging
from fastapi import Depends, FastAPI, Header, HTTPException, WebSocket, WebSocketDisconnect
from pydantic import BaseModel
from . import config as cfgmod
from .at import AtEngine, SerialTransport, probe_at_port
from .service import Hub
from .uart_link import UartLink

log = logging.getLogger(__name__)

class DialReq(BaseModel): number: str
class SmsReq(BaseModel):
    number: str
    text: str
class DataReq(BaseModel): apn: str | None = None

def create_app(cfg=None, at_transport=None, uart_transport=None) -> FastAPI:
    cfg = cfg or cfgmod.load()
    state: dict = {}

    @contextlib.asynccontextmanager
    async def lifespan(app: FastAPI):
        t = at_transport
        if t is None:
            port = probe_at_port(cfg.at_baud) if cfg.at_port == "auto" else cfg.at_port
            if not port: raise RuntimeError("no SIM8262A AT port found on /dev/ttyUSB*")
            t = SerialTransport(port, cfg.at_baud)
        at = AtEngine(t); hub = Hub(at, cfg); at.start()
        await hub.init_modem()
        state["hub"] = hub
        link = None
        if uart_transport is not None or cfg.esp_enabled:
            try:
                link = UartLink(uart_transport or SerialTransport(cfg.esp_uart, cfg.esp_baud), hub); link.start()
            except Exception:  # noqa: BLE001 - hub still useful without the ESP32
                log.exception("ESP32 UART link unavailable")
        yield
        if link: await link.stop()
        await at.stop()

    app = FastAPI(title="VictorModem hub", version="0.1.0", lifespan=lifespan)

    def auth(authorization: str = Header(default="")) -> Hub:
        if not hmac.compare_digest(authorization, f"Bearer {cfg.token}"):
            raise HTTPException(401, "unauthorized")
        return state["hub"]

    @app.get("/healthz")
    async def health(): return {"ok": True}
    @app.get("/api/status")
    async def status(h: Hub = Depends(auth)): return await h.status()
    @app.post("/api/call/dial")
    async def dial(b: DialReq, h: Hub = Depends(auth)): return await h.dial(b.number)
    @app.post("/api/call/answer")
    async def answer(h: Hub = Depends(auth)): return await h.answer()
    @app.post("/api/call/hangup")
    async def hangup(h: Hub = Depends(auth)): return await h.hangup()
    @app.get("/api/calls")
    async def calls(h: Hub = Depends(auth)): return await h.calls()
    @app.post("/api/sms")
    async def sms_send(b: SmsReq, h: Hub = Depends(auth)): return await h.sms_send(b.number, b.text)
    @app.get("/api/sms")
    async def sms_list(filter: str = "ALL", h: Hub = Depends(auth)): return await h.sms_list(filter)
    @app.get("/api/sms/{idx}")
    async def sms_read(idx: int, h: Hub = Depends(auth)): return await h.sms_read(idx)
    @app.delete("/api/sms/{idx}")
    async def sms_del(idx: int, h: Hub = Depends(auth)): return await h.sms_delete(idx)
    @app.post("/api/data/up")
    async def data_up(b: DataReq, h: Hub = Depends(auth)): return await h.data_up(b.apn)
    @app.post("/api/data/down")
    async def data_down(h: Hub = Depends(auth)): return await h.data_down()
    @app.post("/api")   # generic: same JSON protocol as BLE/ESP32 ({"id","token","cmd",...})
    async def generic(req: dict): return await state["hub"].handle(req)

    @app.websocket("/ws")
    async def ws(sock: WebSocket, token: str = ""):
        if not hmac.compare_digest(token, cfg.token):
            await sock.close(code=4401); return
        await sock.accept()
        hub: Hub = state["hub"]; q: asyncio.Queue = asyncio.Queue(maxsize=256)
        listener = lambda m: q.full() or q.put_nowait(m)
        hub.listeners.append(listener)
        async def pump():
            while True: await sock.send_json(await q.get())
        p = asyncio.create_task(pump())
        try:
            while True:
                req = await sock.receive_json()
                req.setdefault("token", token)
                await sock.send_json(await hub.handle(req))
        except WebSocketDisconnect:
            pass
        finally:
            p.cancel(); hub.listeners.remove(listener)
    return app
