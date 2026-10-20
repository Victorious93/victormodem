"""Newline-delimited JSON link to the ESP32-S3 (Pi GPIO14 TXD -> ESP RX, GPIO15 RXD <- ESP TX, 3.3 V, GND).
ESP32 forwards phone requests verbatim (token included); hub replies and pushes events.
Link control frames: {"type":"hello"} / {"type":"ping"} -> {"type":"pong"}."""
from __future__ import annotations
import asyncio, json, logging
from .at import Transport

log = logging.getLogger(__name__)
MAX_FRAME = 4096

class UartLink:
    def __init__(self, transport: Transport, hub):
        self.t, self.hub = transport, hub
        self._buf = b""
        self._out: asyncio.Queue[bytes] = asyncio.Queue(maxsize=256)
        hub.listeners.append(self.push)
        self._tasks: list[asyncio.Task] = []

    def start(self) -> None:
        self._tasks = [asyncio.create_task(self._rx()), asyncio.create_task(self._tx())]

    async def stop(self) -> None:
        for t in self._tasks: t.cancel()
        await self.t.close()

    def push(self, msg: dict) -> None:
        try:
            self._out.put_nowait(json.dumps(msg, separators=(",", ":")).encode() + b"\n")
        except asyncio.QueueFull:
            log.warning("uart tx queue full; dropping")

    async def _tx(self) -> None:
        while True:
            await self.t.write(await self._out.get())

    async def _rx(self) -> None:
        while True:
            chunk = await self.t.read()
            if not chunk:
                await asyncio.sleep(0.01); continue
            self._buf += chunk
            if len(self._buf) > MAX_FRAME and b"\n" not in self._buf:
                self._buf = b""
            while b"\n" in self._buf:
                raw, self._buf = self._buf.split(b"\n", 1)
                await self.handle_frame(raw.strip())

    async def handle_frame(self, raw: bytes) -> None:
        if not raw: return
        try:
            req = json.loads(raw)
        except ValueError:
            return self.push({"ok": False, "error": "bad json"})
        if req.get("type") in ("ping", "hello"):
            return self.push({"type": "pong", "hub": "victormodem", "call_state": self.hub.call_state})
        asyncio.create_task(self._serve(req))

    async def _serve(self, req: dict) -> None:
        self.push(await self.hub.handle(req))
