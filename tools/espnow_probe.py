#!/usr/bin/env python3
"""Sniff and inject ESP-NOW frames from a laptop Wi-Fi card in monitor mode.

Talks the frame format of ESP-IDF's examples/wifi/espnow (example_espnow_data_t),
so a board running that example sees the laptop as a broadcast peer.

    sudo tools/espnow_mon.sh up                       # card -> monitor, channel 1
    sudo python3 tools/espnow_probe.py                # listen only, 30 s
    sudo python3 tools/espnow_probe.py --send         # also broadcast every 1 s
    sudo tools/espnow_mon.sh down                     # back to normal Wi-Fi
    python3 tools/espnow_probe.py --selftest          # no root, no radio

What proves what:
  - receive works  -> lines for frames from boards (ours or other teams')
  - injection works -> the board running the example logs
                       "Receive Nth broadcast data from: 02:00:00:c5:00:01"
    (seeing our own frames in this sniffer does NOT prove they went on air)

Limits: the example's unicast phase is encrypted with its LMK, so the laptop can
only take part in the broadcast phase. We send magic=0xffffffff so the board
never switches to unicast towards us. The laptop never sends 802.11 ACKs.
"""

import argparse
import os
import struct
import sys
import time

ESPRESSIF_OUI = b"\x18\xfe\x34"
CATEGORY_VENDOR = 0x7F
ESPNOW_TYPE = 0x04
ESPNOW_VERSION = 0x01
BROADCAST = "ff:ff:ff:ff:ff:ff"

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


def selftest():
    frame = example_build(state=1, seq=7, magic=0xFFFFFFFF)
    # Same values as the ROM table in esp_rom/linux/esp_rom_crc.c (crc16_le_table[1] == 0x1189).
    assert (~crc16_le(b"\x01", init=0xFFFF) & 0xFFFF) == 0x1189
    parsed = example_parse(frame)
    assert parsed == {"type": "bcast", "state": 1, "seq": 7, "magic": 0xFFFFFFFF}, parsed
    assert example_parse(frame[:-1] + bytes([frame[-1] ^ 1])) is None
    assert espnow_payload(espnow_body(frame)) == frame
    print("selftest ok:", frame.hex())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iface", default="wlp2s0")
    ap.add_argument("--seconds", type=float, default=30)
    ap.add_argument("--send", action="store_true", help="broadcast example frames every --period s")
    ap.add_argument("--period", type=float, default=1.0)
    ap.add_argument("--src", default="02:00:00:c5:00:01", help="source MAC we inject with")
    ap.add_argument("--magic", type=lambda s: int(s, 0), default=0xFFFFFFFF)
    ap.add_argument("--all", action="store_true", help="also print non-example ESP-NOW frames in full")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    sys.stdout.reconfigure(line_buffering=True)  # print live even when piped into tee

    if args.selftest:
        selftest()
        return
    if os.geteuid() != 0:
        sys.exit("needs root (raw socket); run with sudo")

    from scapy.all import AsyncSniffer, Dot11, RadioTap, Raw, sendp

    stats = {"rx": 0, "rx_example": 0, "rx_own": 0, "tx": 0}
    senders = {}

    def on_frame(pkt):
        if not pkt.haslayer(Dot11):
            return
        d = pkt[Dot11]
        if d.type != 0 or d.subtype != 13:
            return
        body = bytes(d.payload)
        payload = espnow_payload(body)
        if payload is None:
            return
        src = d.addr2
        rssi = getattr(pkt, "dBm_AntSignal", None)
        protected = bool(d.FCfield & 0x40)
        if src == args.src.lower():
            stats["rx_own"] += 1
            return
        stats["rx"] += 1
        s = senders.setdefault(src, {"n": 0, "rssi": None})
        s["n"] += 1
        s["rssi"] = rssi
        ex = None if protected else example_parse(payload)
        t = time.strftime("%H:%M:%S")
        if ex:
            stats["rx_example"] += 1
            print(f"{t} {src} -> {d.addr1} rssi={rssi} example {ex['type']} seq={ex['seq']} "
                  f"state={ex['state']} magic={ex['magic']:#010x}")
        elif args.all:
            tag = "encrypted" if protected else payload[:16].hex()
            print(f"{t} {src} -> {d.addr1} rssi={rssi} len={len(payload)} {tag}")

    sniffer = AsyncSniffer(iface=args.iface, prn=on_frame, store=False)
    sniffer.start()
    print(f"listening on {args.iface} for {args.seconds:.0f} s"
          + (f", broadcasting as {args.src} every {args.period} s" if args.send else ""))

    seq = 0
    end = time.time() + args.seconds
    try:
        while time.time() < end:
            if args.send:
                payload = example_build(state=1, seq=seq, magic=args.magic)
                frame = (RadioTap() /
                         Dot11(type=0, subtype=13, addr1=BROADCAST, addr2=args.src, addr3=BROADCAST,
                               SC=(seq & 0xFFF) << 4) /
                         Raw(espnow_body(payload)))
                sendp(frame, iface=args.iface, verbose=False)
                stats["tx"] += 1
                seq += 1
            time.sleep(args.period if args.send else 0.2)
    except KeyboardInterrupt:
        pass
    finally:
        sniffer.stop()

    print("\n--- summary")
    print(f"sent {stats['tx']}, own frames echoed back {stats['rx_own']}, "
          f"ESP-NOW frames from others {stats['rx']} ({stats['rx_example']} in example format)")
    for mac, s in sorted(senders.items(), key=lambda kv: -kv[1]["n"]):
        print(f"  {mac}  frames={s['n']:4d}  last rssi={s['rssi']}")


if __name__ == "__main__":
    main()
