#!/usr/bin/env bash
# Start several simulated boards on the laptop: one espnow_probe.py link peer
# each, with its own name and MAC. They show up in the real boards' lobbies
# and accept invites. Ctrl+C stops them all.
#
#   sudo tools/espnow_mon.sh up              # once: Wi-Fi card -> monitor mode
#   sudo tools/sim_boards.sh                 # 3 boards (LAP, BOB, EVE) for 10 minutes
#   sudo tools/sim_boards.sh 5 60            # 5 boards for 60 s
#   sudo tools/sim_boards.sh 2 600 wlan0     # on another interface
#   sudo tools/espnow_mon.sh down            # back to normal Wi-Fi
#
# Each board's output goes to tools/sim-logs/<NAME>.log
# (watch one with: tail -f tools/sim-logs/BOB.log).
set -euo pipefail

COUNT=${1:-3}
DURATION=${2:-600}
IFACE=${3:-wlp2s0}
NAMES=(LAP BOB EVE MAX ZOE KIM TOM ANA)

DIR=$(cd "$(dirname "$0")" && pwd)
LOGS="$DIR/sim-logs"

if [[ $EUID -ne 0 ]]; then
    echo "run with sudo" >&2
    exit 1
fi
if ((COUNT < 1 || COUNT > ${#NAMES[@]})); then
    echo "number of boards must be 1..${#NAMES[@]}" >&2
    exit 1
fi
# Link type 803 = radiotap, which is what monitor mode looks like.
if [[ $(cat "/sys/class/net/$IFACE/type" 2>/dev/null) != 803 ]]; then
    echo "$IFACE isn't in monitor mode. Run first:  sudo $DIR/espnow_mon.sh up $IFACE" >&2
    exit 1
fi

mkdir -p "$LOGS"
pids=()
stop() {
    trap - INT TERM EXIT
    kill "${pids[@]}" 2>/dev/null || true
    wait 2>/dev/null || true
    echo "all simulated boards stopped"
}
trap stop INT TERM EXIT

for ((i = 0; i < COUNT; i++)); do
    name=${NAMES[$i]}
    src=$(printf '02:00:00:c5:00:%02x' $((i + 1)))  # locally administered, one per board
    python3 -u "$DIR/espnow_probe.py" --iface "$IFACE" --seconds "$DURATION" \
        --name "$name" --src "$src" >"$LOGS/$name.log" 2>&1 &
    pids+=($!)
    echo "$name  $src  -> $LOGS/$name.log"
done

echo "running for ${DURATION}s on $IFACE, Ctrl+C to stop"
wait
