#!/usr/bin/env bash
# Put the laptop's Wi-Fi card into monitor mode on a fixed channel so
# espnow_probe.py can sniff and inject ESP-NOW frames, and put it back after.
#
#   sudo tools/espnow_mon.sh up   [iface] [channel]   # default wlp2s0, channel 1
#   sudo tools/espnow_mon.sh down [iface]
#
# "up" refuses to run while the card carries the default route, because monitor
# mode drops its Wi-Fi connection. Bring up another link first (USB tethering,
# Ethernet) or pass FORCE=1 if you really want to go offline.

set -euo pipefail

IW=/usr/sbin/iw
CMD=${1:-}
IFACE=${2:-wlp2s0}
CHANNEL=${3:-1}

if [[ $EUID -ne 0 ]]; then
    echo "run with sudo" >&2
    exit 1
fi

case "$CMD" in
up)
    # Ask which interface traffic actually leaves through (lowest metric wins),
    # since NetworkManager keeps a default route on every connected interface.
    if ip route get 1.1.1.1 | grep -q "dev $IFACE " && [[ "${FORCE:-0}" != 1 ]]; then
        echo "$IFACE carries the default route; monitor mode would take you offline." >&2
        echo "Turn on USB tethering / Ethernet first (or FORCE=1)." >&2
        ip route show default >&2
        exit 1
    fi
    nmcli device set "$IFACE" managed no 2>/dev/null || true
    ip link set "$IFACE" down
    $IW dev "$IFACE" set type monitor
    ip link set "$IFACE" up
    $IW dev "$IFACE" set channel "$CHANNEL"
    $IW dev "$IFACE" info
    ;;
down)
    ip link set "$IFACE" down
    $IW dev "$IFACE" set type managed
    ip link set "$IFACE" up
    nmcli device set "$IFACE" managed yes 2>/dev/null || true
    echo "$IFACE back in managed mode; NetworkManager will reconnect."
    ;;
*)
    echo "usage: sudo $0 up|down [iface] [channel]" >&2
    exit 1
    ;;
esac
