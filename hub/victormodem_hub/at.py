"""Async AT engine: one command in flight, URC dispatch, '> ' prompts (CMGS)."""
from __future__ import annotations
import asyncio, glob, logging
from dataclasses import dataclass
from typing import Awaitable, Callable, Protocol

log = logging.getLogger(__name__)
URC_PREFIXES = ("RING", "+CLIP:", "+CMTI:", "NO CARRIER", "BUSY", "NO ANSWER", "+CMT:",
                "VOICE CALL:", "+CREG:", "+CEREG:", "+C5GREG:", "+CPIN:", "MISSED_CALL:",
                "+CLCC:", "+CDS:", "PB DONE", "SMS DONE")
FINAL_OK = ("OK",)
FINAL_ERR = ("ERROR", "+CME ERROR", "+CMS ERROR", "NO CARRIER", "BUSY", "NO ANSWER", "NO DIALTONE")

@dataclass
class AtResult:
    ok: bool
    lines: list[str]
    final: str = ""

class Transport(Protocol):
    async def read(self) -> bytes: ...
    async def write(self, data: bytes) -> None: ...
    async def close(self) -> None: ...

class SerialTransport:
    """pyserial wrapped in a thread executor (works on any tty)."""
    def __init__(self, port: str, baud: int = 115200):
        import serial
        self.s = serial.Serial(port, baud, timeout=0.2)
    async def read(self) -> bytes:
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(None, lambda: self.s.read(self.s.in_waiting or 1))
    async def write(self, data: bytes) -> None:
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, self.s.write, data)
    async def close(self) -> None:
        self.s.close()

def probe_at_port(baud: int = 115200) -> str | None:
    """SIM8262A exposes several ttyUSB ports (DIAG, NMEA, AT, AT/modem). Find one answering AT."""
    import serial
    for p in sorted(glob.glob("/dev/ttyUSB*")):
        try:
            with serial.Serial(p, baud, timeout=0.5) as s:
                s.reset_input_buffer(); s.write(b"AT\r")
                if b"OK" in s.read(64):
                    return p
        except Exception:  # noqa: BLE001 - busy/permission: skip
            continue
    return None

UrcHandler = Callable[[str], Awaitable[None] | None]

class AtEngine:
    def __init__(self, transport: Transport, on_urc: UrcHandler | None = None):
        self.t = transport
        self.on_urc = on_urc
        self._lock = asyncio.Lock()
        self._lines: asyncio.Queue[str] = asyncio.Queue()
        self._prompt = asyncio.Event()
        self._buf = b""
        self._busy = False
        self._reader: asyncio.Task | None = None

    def start(self) -> None:
        self._reader = asyncio.create_task(self._read_loop())

    async def stop(self) -> None:
        if self._reader:
            self._reader.cancel()
        await self.t.close()

    async def _read_loop(self) -> None:
        while True:
            chunk = await self.t.read()
            if not chunk:
                await asyncio.sleep(0.01); continue
            self._buf += chunk
            if self._buf.lstrip(b"\r\n").startswith(b">"):
                self._prompt.set(); self._buf = self._buf.lstrip(b"\r\n")[1:].lstrip(b" ")
            while b"\n" in self._buf:
                raw, self._buf = self._buf.split(b"\n", 1)
                line = raw.decode(errors="replace").strip()
                if line:
                    await self._dispatch(line)

    async def _dispatch(self, line: str) -> None:
        # While a command is in flight, NO CARRIER/BUSY are final codes (ATD); otherwise URCs.
        is_urc = line.startswith(URC_PREFIXES)
        if self._busy and line.startswith(("NO CARRIER", "BUSY", "NO ANSWER")) and self._expect_call:
            is_urc = False
        if is_urc and not (self._busy and self._echo_prefix and line.startswith(self._echo_prefix)):
            if self.on_urc:
                r = self.on_urc(line)
                if asyncio.iscoroutine(r):
                    await r
            return
        if self._busy:
            await self._lines.put(line)

    _expect_call = False
    _echo_prefix = ""

    async def cmd(self, command: str, timeout: float = 5.0, payload: str | None = None) -> AtResult:
        async with self._lock:
            while not self._lines.empty():
                self._lines.get_nowait()
            self._prompt.clear()
            self._busy = True
            self._expect_call = command.upper().startswith(("ATD", "ATA"))
            # responses like "+CLCC:" to AT+CLCC must not be treated as URCs
            self._echo_prefix = "+" + command[3:].split("=")[0].split("?")[0] + ":" if command.startswith("AT+") else ""
            try:
                await self.t.write(command.encode() + b"\r")
                if payload is not None:
                    await asyncio.wait_for(self._prompt.wait(), timeout)
                    await self.t.write(payload.encode() + b"\x1a")
                return await asyncio.wait_for(self._collect(command), timeout)
            except asyncio.TimeoutError:
                if payload is not None:
                    await self.t.write(b"\x1b")  # abort SMS entry
                return AtResult(False, [], "TIMEOUT")
            finally:
                self._busy = False

    async def _collect(self, command: str) -> AtResult:
        out: list[str] = []
        while True:
            line = await self._lines.get()
            if line == command:  # echo
                continue
            if line in FINAL_OK:
                return AtResult(True, out, line)
            if line.startswith(FINAL_ERR):
                return AtResult(False, out, line)
            out.append(line)
