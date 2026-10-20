from __future__ import annotations
import os, tomllib
from dataclasses import dataclass, field

DEFAULT_PATH = "/etc/victormodem/config.toml"

@dataclass
class Config:
    token: str = "change-this-token"
    at_port: str = "auto"            # "auto" = probe /dev/ttyUSB* for an AT responder
    at_baud: int = 115200
    esp_uart: str = "/dev/ttyAMA0"   # Pi 5 GPIO14/15 (enable_uart=1, dtoverlay=uart0)
    esp_baud: int = 921600
    esp_enabled: bool = True
    apn: str = "vzwinternet"         # Visible/Total Wireless on Verizon; check your plan
    data_backend: str = "modemmanager"  # modemmanager | rndis | none
    state_dir: str = "/var/lib/victormodem"
    host: str = "0.0.0.0"
    port: int = 8080
    extra: dict = field(default_factory=dict)

def load(path: str | None = None) -> Config:
    path = path or os.environ.get("VICTORMODEM_CONFIG", DEFAULT_PATH)
    if not os.path.exists(path):
        return Config()
    with open(path, "rb") as f:
        raw = tomllib.load(f)
    known = {k: raw.pop(k) for k in list(raw) if k in Config.__dataclass_fields__}
    return Config(**known, extra=raw)
