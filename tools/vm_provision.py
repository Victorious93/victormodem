#!/usr/bin/env python3
"""Host side of the VictorModem fleet USB provisioning contract v1.

Usage: vm_provision.py PORT hello|get|reboot|factory_reset
       vm_provision.py PORT provision key=value [key=value ...]
Needs pyserial only for real ports; the protocol helpers are pure Python (see tests/).
"""
import json, sys

def encode(cmd, **kw):
    return (json.dumps({"v": 1, "cmd": cmd, **kw}, separators=(",", ":")) + "\n").encode()

def read_reply(readline, max_lines=200):
    """Skip debug/log lines (never start with '{') and return the first JSON object."""
    for _ in range(max_lines):
        raw = readline()
        if not raw:
            raise TimeoutError("no reply")
        s = raw.decode(errors="replace").strip()
        if s.startswith("{"):
            obj = json.loads(s)
            if obj.get("v") == 1 and "ok" in obj:
                return obj
    raise TimeoutError("no JSON reply")

def transact(port, cmd, **kw):
    port.write(encode(cmd, **kw)); port.flush()
    return read_reply(port.readline)

def main(argv):
    import serial  # pyserial
    if len(argv) < 3: sys.exit(__doc__)
    with serial.Serial(argv[1], 115200, timeout=3) as p:
        cmd = argv[2]
        if cmd == "provision":
            cfg = {}
            for a in argv[3:]:
                k, v = a.split("=", 1)
                cfg[k] = int(v) if k == "hub_baud" else v
            r = transact(p, cmd, cfg=cfg)
        else:
            r = transact(p, cmd)
        print(json.dumps(r, indent=2))
        sys.exit(0 if r.get("ok") else 1)

if __name__ == "__main__":
    main(sys.argv)
