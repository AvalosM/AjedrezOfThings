#!/usr/bin/env python3
"""Sniff and inject ESP-NOW frames from a laptop Wi-Fi card in monitor mode.

Two modes:

  --mode link (default)  a full peer of our link protocol (firmware/components/link):
                         HELLO every second, answers JOIN, keepalive PING, BYE, DATA.
  --mode example         talks ESP-IDF's examples/wifi/espnow (example_espnow_data_t)
                         as a broadcast peer.

    sudo tools/espnow_mon.sh up                                   # card -> monitor, channel 1
    sudo python3 tools/espnow_probe.py                            # link peer "LAP", accepts JOINs
    sudo python3 tools/espnow_probe.py --connect any              # laptop pairs with nearest free board
    sudo python3 tools/espnow_probe.py --connect aa:bb:cc:dd:ee:ff --send-text hi
    sudo python3 tools/espnow_probe.py --listen                   # decode only, transmit nothing
    sudo python3 tools/espnow_probe.py --mode example --send      # espnow example peer
    sudo tools/espnow_mon.sh down                                 # back to normal Wi-Fi
    python3 tools/espnow_probe.py --selftest                      # no root, no radio

What proves what:
  - receive works   -> HELLO lines from boards (or other teams' frames with --all)
  - injection works -> a board shows "LAP" in its nearby list / logs
                       "link: connected to LAP 02:00:00:c5:00:01", and here we
                       print "*** CONNECTED" after its PING arrives
    (seeing our own frames in this sniffer does NOT prove they went on air)

The laptop in monitor mode never sends 802.11 ACKs. A board's unicast to us
still arrives, but its radio retries each frame several times, so expect
duplicate copies of JOIN_ACK/PING/DATA here. Nothing is de-duplicated at this
layer, and the board's own send status will say "fail" for frames to us.

Example mode limits: the example's unicast phase is encrypted with its LMK, so
the laptop only takes part in the broadcast phase. We send magic=0xffffffff so
the board never switches to unicast towards us.
"""

import argparse
import collections
import os
import random
import struct
import sys
import threading
import time

ESPRESSIF_OUI = b"\x18\xfe\x34"
CATEGORY_VENDOR = 0x7F
ESPNOW_TYPE = 0x04
ESPNOW_VERSION = 0x01
BROADCAST = "ff:ff:ff:ff:ff:ff"

# --- espnow example format ---------------------------------------------------

# example_espnow_data_t: type, state, seq_num, crc, magic (packed, little endian)
EXAMPLE_HDR = struct.Struct("<BBHHI")
EXAMPLE_BROADCAST, EXAMPLE_UNICAST = 0, 1


def crc16_le(data, init=0xFFFF):
    """esp_crc16_le(): reflected CRC-16, poly 0x8408, with ROM's ~ in and out."""
    crc = ~init & 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return ~crc & 0xFFFF


def example_build(state, seq, magic, payload=b""):
    raw = bytearray(EXAMPLE_HDR.pack(EXAMPLE_BROADCAST, state, seq, 0, magic) + payload)
    struct.pack_into("<H", raw, 4, crc16_le(raw))
    return bytes(raw)


def example_parse(data):
    """Return a dict if data is a valid example_espnow_data_t, else None."""
    if len(data) < EXAMPLE_HDR.size:
        return None
    typ, state, seq, crc, magic = EXAMPLE_HDR.unpack_from(data)
    zeroed = bytearray(data)
    struct.pack_into("<H", zeroed, 4, 0)
    if crc16_le(zeroed) != crc:
        return None
    return {"type": "bcast" if typ == EXAMPLE_BROADCAST else "ucast", "state": state,
            "seq": seq, "magic": magic}

# --- link (AOT) format, mirrors firmware/components/link/link.c ----------------

AOT_MAGIC = b"AOT"
AOT_VERSION = 1
AOT_HDR = struct.Struct("<3sBBB3s6s")  # magic version type flags name[3] mac[6] = 15 bytes
AOT_MAX_DATA = 200
FLAG_BUSY = 0x01
T_HELLO, T_JOIN, T_JOIN_ACK, T_PING, T_DATA, T_BYE = range(1, 7)
TYPE_NAMES = {T_HELLO: "HELLO", T_JOIN: "JOIN", T_JOIN_ACK: "JOIN_ACK", T_PING: "PING",
              T_DATA: "DATA", T_BYE: "BYE"}

HELLO_PERIOD = 1.0
HELLO_JITTER = 0.2
PING_PERIOD = 1.0
JOIN_RETRY = 0.3
HANDSHAKE_TIMEOUT = 2.0
PEER_TIMEOUT = 3.0
NEARBY_TIMEOUT = 3.0
RETRY_JITTER = 0.3


def mac_bytes(mac):
    return bytes(int(x, 16) for x in mac.split(":"))


def mac_str(raw):
    return ":".join(f"{b:02x}" for b in raw)


def aot_build(typ, name, mac, flags=0, data=b""):
    if len(data) > AOT_MAX_DATA:
        raise ValueError(f"link data is at most {AOT_MAX_DATA} bytes")
    name = name.upper().encode()[:3].ljust(3, b"_")
    return AOT_HDR.pack(AOT_MAGIC, AOT_VERSION, typ, flags, name, mac_bytes(mac)) + data


def aot_parse(payload):
    """Return a dict for a valid AOT frame, else None (same checks as link.c on_recv)."""
    if not AOT_HDR.size <= len(payload) <= AOT_HDR.size + AOT_MAX_DATA:
        return None
    magic, version, typ, flags, name, mac = AOT_HDR.unpack_from(payload)
    if magic != AOT_MAGIC or version != AOT_VERSION:
        return None
    return {"type": typ, "type_name": TYPE_NAMES.get(typ, f"?{typ}"), "busy": bool(flags & FLAG_BUSY),
            "name": name.decode(errors="replace"), "mac": mac_str(mac), "data": payload[AOT_HDR.size:]}


def show_data(data):
    return repr(data.decode()) if data and all(32 <= b < 127 for b in data) else data.hex()

# --- ESP-NOW framing -------------------------------------------------------------


def espnow_body(payload):
    """802.11 action frame body: category, OUI, 4 random bytes, vendor element."""
    if len(payload) > 250:
        raise ValueError("ESP-NOW v1 payload is at most 250 bytes")
    element = bytes([0xDD, 5 + len(payload)]) + ESPRESSIF_OUI + bytes([ESPNOW_TYPE, ESPNOW_VERSION])
    return bytes([CATEGORY_VENDOR]) + ESPRESSIF_OUI + os.urandom(4) + element + payload


def espnow_payload(body):
    """Inverse of espnow_body(); None if body isn't ESP-NOW."""
    if len(body) < 15 or body[0] != CATEGORY_VENDOR or body[1:4] != ESPRESSIF_OUI:
        return None
    el = body[8:]
    if el[0] != 0xDD or el[2:5] != ESPRESSIF_OUI or el[5] != ESPNOW_TYPE:
        return None
    return el[7:2 + el[1]]


class Radio:
    """Injects ESP-NOW frames as one source MAC; safe to call from any thread."""

    def __init__(self, iface, src):
        from scapy.all import conf
        self.src = src
        self.sock = conf.L2socket(iface=iface)
        self.lock = threading.Lock()
        self.seq = 0
        self.sent = 0

    def send(self, dest, payload):
        from scapy.all import Dot11, RadioTap, Raw
        with self.lock:
            frame = (RadioTap() /
                     Dot11(type=0, subtype=13, addr1=dest, addr2=self.src, addr3=BROADCAST,
                           SC=(self.seq & 0xFFF) << 4) /
                     Raw(espnow_body(payload)))
            self.sock.send(frame)
            self.seq += 1
            self.sent += 1

# --- link peer state machine (same rules as link.c) ------------------------------


class LinkPeer:
    def __init__(self, name, radio, want=None):
        self.name = name
        self.radio = radio
        self.want = want            # None (accept incoming), "any", or a MAC string
        self.lock = threading.Lock()
        self.state = "idle"
        self.initiator = False
        self.peer = None            # MAC we are connecting/connected to
        self.peer_name = "???"
        self.peer_rssi = None
        self.state_since = self.last_rx = self.last_join = self.last_ping = 0.0
        self.next_hello = self.retry_at = 0.0
        self.nearby = {}            # mac -> (name, rssi, busy, last_seen)
        self.connects = self.disconnects = 0

    def _send(self, dest, typ, data=b""):
        flags = FLAG_BUSY if self.state == "connected" else 0
        self.radio.send(dest, aot_build(typ, self.name, self.radio.src, flags, data))

    def _event(self, text):
        print(f"{time.strftime('%H:%M:%S')} *** {text}")

    def _go_idle(self, reason):
        if self.state == "connected":
            self.disconnects += 1
            self._event(f"DISCONNECTED from {self.peer_name} {self.peer} ({reason})")
        self.state = "idle"
        self.state_since = time.time()
        self.retry_at = self.state_since + random.random() * RETRY_JITTER

    def _start_connecting(self, mac, name, initiator):
        self.peer, self.peer_name, self.peer_rssi = mac, name or "???", None
        self.state = "connecting"
        self.initiator = initiator
        self.state_since = time.time()
        self.last_join = 0.0

    def _become_connected(self):
        self.state = "connected"
        self.connects += 1
        self.last_rx = time.time()
        self.last_ping = 0.0
        self._event(f"CONNECTED to {self.peer_name} {self.peer} ({self.peer_rssi} dBm)")

    def _accepts_join_from(self, mac):
        return self.want in (None, "any") or self.want == mac

    def on_frame(self, src, rssi, f):
        """Handle an AOT frame addressed to us (unicast to our MAC or broadcast)."""
        with self.lock:
            now = time.time()
            from_peer = self.state != "idle" and src == self.peer
            self.nearby[src] = (f["name"], rssi, f["busy"], now)
            t = f["type"]
            # Same rule as link.c: HELLO isn't a keepalive, the peer keeps
            # broadcasting it after dropping us.
            if from_peer and t != T_HELLO:
                self.peer_name, self.peer_rssi, self.last_rx = f["name"], rssi, now

            if t == T_HELLO:
                if self.state == "connected" and from_peer and not f["busy"]:
                    self._go_idle("peer says it's free")
            elif t == T_JOIN:
                if self.state == "connected":
                    if not from_peer:
                        return
                    self._go_idle("peer restarted")
                if not self._accepts_join_from(src):
                    return
                if not (self.state == "connecting" and from_peer):
                    self._start_connecting(src, f["name"], initiator=False)
                    self.peer_rssi = rssi
                self._send(src, T_JOIN_ACK)
            elif t in (T_JOIN_ACK, T_PING):
                if self.state == "connecting" and from_peer:
                    self._become_connected()
            elif t == T_BYE:
                if from_peer:
                    self.want = None
                    self._go_idle("BYE")

    def _nearest_free(self, now):
        free = [(rssi, mac, name) for mac, (name, rssi, busy, seen) in self.nearby.items()
                if now - seen < NEARBY_TIMEOUT and not busy]
        return max(free, default=None, key=lambda x: x[0] if x[0] is not None else -999)

    def tick(self):
        with self.lock:
            now = time.time()
            if now >= self.next_hello:
                self._send(BROADCAST, T_HELLO)
                self.next_hello = now + HELLO_PERIOD + random.random() * HELLO_JITTER

            if self.state == "connected":
                if now - self.last_rx > PEER_TIMEOUT:
                    self._go_idle("timeout")
                elif not self.last_ping or now - self.last_ping >= PING_PERIOD:
                    self._send(self.peer, T_PING)
                    self.last_ping = now
            elif self.state == "connecting":
                if now - self.state_since > HANDSHAKE_TIMEOUT:
                    self._go_idle("handshake timeout")
                elif self.initiator and (not self.last_join or now - self.last_join >= JOIN_RETRY):
                    self._send(self.peer, T_JOIN)
                    self.last_join = now
            elif now >= self.retry_at:
                if self.want == "any":
                    best = self._nearest_free(now)
                    if best:
                        self._start_connecting(best[1], best[2], initiator=True)
                elif self.want:
                    self._start_connecting(self.want, None, initiator=True)

    def send_data(self, data):
        with self.lock:
            if self.state != "connected":
                return False
            self._send(self.peer, T_DATA, data)
            return True

    def hang_up(self):
        with self.lock:
            if self.state != "idle":
                self._send(self.peer, T_BYE)
                self._go_idle("we hung up")

# --- selftest --------------------------------------------------------------------


def selftest():
    frame = example_build(state=1, seq=7, magic=0xFFFFFFFF)
    # Same values as the ROM table in esp_rom/linux/esp_rom_crc.c (crc16_le_table[1] == 0x1189).
    assert (~crc16_le(b"\x01", init=0xFFFF) & 0xFFFF) == 0x1189
    parsed = example_parse(frame)
    assert parsed == {"type": "bcast", "state": 1, "seq": 7, "magic": 0xFFFFFFFF}, parsed
    assert example_parse(frame[:-1] + bytes([frame[-1] ^ 1])) is None
    assert espnow_payload(espnow_body(frame)) == frame

    assert AOT_HDR.size == 15, AOT_HDR.size  # sizeof(frame_t) in link.c
    aot = aot_build(T_DATA, "lap", "02:00:00:c5:00:01", FLAG_BUSY, b"hi")
    assert aot[:6] == b"AOT\x01\x05\x01" and len(aot) == 17, aot.hex()
    p = aot_parse(espnow_payload(espnow_body(aot)))
    assert p == {"type": T_DATA, "type_name": "DATA", "busy": True, "name": "LAP",
                 "mac": "02:00:00:c5:00:01", "data": b"hi"}, p
    assert aot_build(T_HELLO, "x", "02:00:00:00:00:00")[6:9] == b"X__"
    assert aot_parse(b"AOT\x02" + aot[4:]) is None   # wrong version
    assert aot_parse(aot[:14]) is None                # too short
    print("selftest ok:", frame.hex(), aot.hex())

# --- main --------------------------------------------------------------------------


def parse_args():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mode", choices=["link", "example"], default="link")
    ap.add_argument("--iface", default="wlp2s0")
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--src", default="02:00:00:c5:00:01", help="source MAC we inject with")
    ap.add_argument("--all", action="store_true", help="also print frames that aren't ours")
    ap.add_argument("--selftest", action="store_true")
    g = ap.add_argument_group("link mode")
    g.add_argument("--name", default="LAP", help="our 3-letter name")
    g.add_argument("--connect", metavar="MAC|any", help="initiate a connection (default: only accept)")
    g.add_argument("--send-text", metavar="TEXT", help="send TEXT as DATA every --period s while connected")
    g.add_argument("--listen", action="store_true", help="decode only, transmit nothing")
    g.add_argument("--peer-timeout", type=float, default=PEER_TIMEOUT,
                   help="seconds of silence before we drop the peer; raise it if the card "
                        "misses frames (the RTL8852AE does: 10 works)")
    g = ap.add_argument_group("example mode")
    g.add_argument("--send", action="store_true", help="broadcast example frames every --period s")
    g.add_argument("--magic", type=lambda s: int(s, 0), default=0xFFFFFFFF)
    ap.add_argument("--period", type=float, default=1.0)
    return ap.parse_args()


def run_example(args, radio, stats, senders):
    def on_payload(d, payload, rssi, protected):
        ex = None if protected else example_parse(payload)
        t = time.strftime("%H:%M:%S")
        if ex:
            stats["example"] += 1
            print(f"{t} {d.addr2} -> {d.addr1} rssi={rssi} example {ex['type']} seq={ex['seq']} "
                  f"state={ex['state']} magic={ex['magic']:#010x}")
        elif args.all:
            tag = "encrypted" if protected else payload[:16].hex()
            print(f"{t} {d.addr2} -> {d.addr1} rssi={rssi} len={len(payload)} {tag}")

    def loop(end):
        seq = 0
        while time.time() < end:
            if args.send:
                radio.send(BROADCAST, example_build(state=1, seq=seq, magic=args.magic))
                seq += 1
            time.sleep(args.period if args.send else 0.2)

    return on_payload, loop


def run_link(args, radio, stats, senders):
    want = None
    if args.connect:
        want = "any" if args.connect.lower() == "any" else args.connect.lower()
    peer = None if args.listen else LinkPeer(args.name, radio, want)
    me = args.src.lower()

    def on_payload(d, payload, rssi, protected):
        f = None if protected else aot_parse(payload)
        t = time.strftime("%H:%M:%S")
        if f is None:
            if args.all:
                tag = "encrypted" if protected else payload[:16].hex()
                print(f"{t} {d.addr2} -> {d.addr1} rssi={rssi} len={len(payload)} (not AOT) {tag}")
            return
        senders[d.addr2]["types"][f["type_name"]] += 1
        extra = f" data={show_data(f['data'])}" if f["type"] == T_DATA else ""
        print(f"{t} {d.addr2} -> {d.addr1} rssi={rssi} {f['type_name']:<8} {f['name']}"
              f"{' busy' if f['busy'] else ''}{extra}")
        if peer and d.addr1 in (me, BROADCAST):
            peer.on_frame(d.addr2, rssi, f)

    def loop(end):
        next_data = 0.0
        try:
            while time.time() < end:
                if peer:
                    peer.tick()
                    if args.send_text and time.time() >= next_data:
                        if peer.send_data(args.send_text.encode()[:AOT_MAX_DATA]):
                            next_data = time.time() + args.period
                time.sleep(0.05)
        finally:
            if peer:
                peer.hang_up()
                stats["connects"], stats["disconnects"] = peer.connects, peer.disconnects

    return on_payload, loop


def main():
    global PEER_TIMEOUT
    args = parse_args()
    PEER_TIMEOUT = args.peer_timeout
    sys.stdout.reconfigure(line_buffering=True)  # print live even when piped into tee

    if args.selftest:
        selftest()
        return
    if os.geteuid() != 0:
        sys.exit("needs root (raw socket); run with sudo")

    from scapy.all import AsyncSniffer, Dot11

    radio = None if (args.mode == "link" and args.listen) else Radio(args.iface, args.src.lower())
    stats = collections.Counter()
    senders = collections.defaultdict(lambda: {"n": 0, "rssi": None, "types": collections.Counter()})
    run = run_link if args.mode == "link" else run_example
    on_payload, loop = run(args, radio, stats, senders)
    me = args.src.lower()

    def on_frame(pkt):
        if not pkt.haslayer(Dot11):
            return
        d = pkt[Dot11]
        if d.type != 0 or d.subtype != 13:
            return
        payload = espnow_payload(bytes(d.payload))
        if payload is None:
            return
        if d.addr2 == me:
            stats["own"] += 1
            return
        rssi = getattr(pkt, "dBm_AntSignal", None)
        stats["rx"] += 1
        senders[d.addr2]["n"] += 1
        senders[d.addr2]["rssi"] = rssi
        on_payload(d, payload, rssi, bool(d.FCfield & 0x40))

    sniffer = AsyncSniffer(iface=args.iface, prn=on_frame, store=False)
    sniffer.start()
    what = f"{args.mode} mode"
    if args.mode == "link" and not args.listen:
        what += f" as {args.name.upper()[:3]} {me}" + (f", connecting to {args.connect}" if args.connect else "")
    print(f"listening on {args.iface} for {args.seconds:.0f} s, {what}")

    try:
        loop(time.time() + args.seconds)
    except KeyboardInterrupt:
        pass
    finally:
        time.sleep(0.2)
        sniffer.stop()

    print("\n--- summary")
    print(f"sent {radio.sent if radio else 0}, own frames echoed back {stats['own']}, "
          f"ESP-NOW frames from others {stats['rx']}")
    if args.mode == "example":
        print(f"  {stats['example']} in example format")
    else:
        print(f"  connects {stats['connects']}, disconnects {stats['disconnects']}")
    for mac, s in sorted(senders.items(), key=lambda kv: -kv[1]["n"]):
        types = " ".join(f"{k}={v}" for k, v in sorted(s["types"].items()))
        print(f"  {mac}  frames={s['n']:4d}  last rssi={s['rssi']}  {types}")


if __name__ == "__main__":
    main()
