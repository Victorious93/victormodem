"""Scriptable fake SIM8262A speaking AT over an in-memory transport."""
from __future__ import annotations
import asyncio

class FakeModem:
    def __init__(self):
        self.rx = asyncio.Queue()       # bytes from modem -> host
        self.sent: list[str] = []
        self._buf = b""
        self._sms_target = None
        self.sms = {3: ('"REC UNREAD","+15551230000","","26/10/20,01:00:00-16"', "Hello from Verizon")}
        self.dial_result = "OK"
    # Transport interface
    async def read(self) -> bytes:
        return await self.rx.get()
    async def close(self) -> None: pass
    async def write(self, data: bytes) -> None:
        if self._sms_target is not None:
            if data.endswith(b"\x1a"):
                self.sent.append("SMS:" + data[:-1].decode()); self._sms_target = None
                self._out("+CMGS: 42", "OK")
            return
        self._buf += data
        while b"\r" in self._buf:
            line, self._buf = self._buf.split(b"\r", 1)
            self._handle(line.decode())
    def urc(self, line: str) -> None:
        self.rx.put_nowait(f"\r\n{line}\r\n".encode())
    def _out(self, *lines: str) -> None:
        self.rx.put_nowait(b"".join(f"\r\n{l}\r\n".encode() for l in lines))
    def _handle(self, c: str) -> None:
        self.sent.append(c)
        if c == "ATI": self._out("Manufacturer: SIMCOM INCORPORATED", "Model: SIMCOM_SIM8262A-M2", "OK")
        elif c.startswith("ATD"): self._out(self.dial_result)
        elif c == "AT+CSQ": self._out("+CSQ: 20,99", "OK")
        elif c == "AT+CLCC": self._out('+CLCC: 1,0,0,0,0,"+15551230000",145', "OK")
        elif c.startswith("AT+CMGS="):
            self._sms_target = c; self.rx.put_nowait(b"\r\n> ")
        elif c.startswith("AT+CMGR="):
            i = int(c.split("=")[1])
            if i in self.sms: h, b = self.sms[i]; self._out(f"+CMGR: {h}", b, "OK")
            else: self._out("+CMS ERROR: invalid memory index")
        elif c.startswith("AT+CMGL="):
            self._out('+CMGL: 1,"REC READ","+15550001111","","26/10/19,10:00:00-16"', "first",
                      '+CMGL: 2,"REC UNREAD","+15550002222","","26/10/19,11:00:00-16"', "second", "line2", "OK")
        elif c == "AT+HANG": pass  # never answers -> timeout
        else: self._out("OK")
