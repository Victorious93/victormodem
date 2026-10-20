#!/usr/bin/env bash
# Run on the Pi 5 (Raspberry Pi OS Bookworm) as root from hub/.
set -euo pipefail
apt-get install -y python3-venv modemmanager libqmi-utils
id victormodem >/dev/null 2>&1 || useradd -r -s /usr/sbin/nologin -G dialout victormodem
python3 -m venv /opt/victormodem/venv
/opt/victormodem/venv/bin/pip install .
install -d -m 750 -o victormodem /etc/victormodem
[ -f /etc/victormodem/config.toml ] || install -m 600 -o victormodem deploy/config.toml.example /etc/victormodem/config.toml
install -m 644 deploy/victormodem-hub.service /etc/systemd/system/
systemctl daemon-reload && systemctl enable --now victormodem-hub
echo "Edit /etc/victormodem/config.toml (token!) then: systemctl restart victormodem-hub"
