#!/usr/bin/env bash
set -euo pipefail

# Opens the LAN ports used by Local Call. Run only on trusted private networks.
TCP_PORTS=(50010 50120)
UDP_PORTS=(50005 50100 50200)

if command -v firewall-cmd >/dev/null 2>&1; then
  for p in "${TCP_PORTS[@]}"; do sudo firewall-cmd --permanent --add-port="${p}/tcp"; done
  for p in "${UDP_PORTS[@]}"; do sudo firewall-cmd --permanent --add-port="${p}/udp"; done
  sudo firewall-cmd --reload
  echo "firewalld rules added."
elif command -v ufw >/dev/null 2>&1; then
  for p in "${TCP_PORTS[@]}"; do sudo ufw allow "${p}/tcp"; done
  for p in "${UDP_PORTS[@]}"; do sudo ufw allow "${p}/udp"; done
  echo "ufw rules added."
else
  cat >&2 <<MSG
No firewalld or ufw command was found.
Open these ports manually on trusted LANs only:
  TCP: ${TCP_PORTS[*]}
  UDP: ${UDP_PORTS[*]}
MSG
fi
