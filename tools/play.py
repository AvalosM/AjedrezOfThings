#!/usr/bin/env python3
"""Play AjedrezOfThings from the laptop against a board.

The laptop shows up in the boards' lobbies like another board (espnow_probe.py's
link peer), then speaks the game protocol of firmware/main/match.c. While a match
runs, each side sends its state every 300 ms:

    STATE type=1, turn_count, board[25]

and takes the other side's board whenever its turn_count is newer. White (whoever
invited) makes the starting board; black waits for it. Each square is one byte:
the piece (chessboard.h order: A C P T R D, 6 = empty), plus 0x80 if white.

    sudo tools/espnow_mon.sh up                  # once: Wi-Fi card -> monitor mode
    sudo python3 tools/play.py                   # wait for a board to invite LAP: we play black
    sudo python3 tools/play.py --connect <MAC>   # invite that board: we play white

Type a move as "from to", e.g. "b2 b3": columns a-e left to right, rows 1-5 top
to bottom, as on the board's screen (white starts on rows 1-2). White pieces are
printed in UPPERCASE, black in lowercase. "board" prints it again, "quit" hangs up.
"""

import argparse
import os
import queue
import random
import sys
import threading
import time

import espnow_probe as probe

# --- game protocol (firmware/main/match.c) -----------------------------------------

MSG_STATE = 1
WHITE_BIT = 0x80
STATE_PERIOD = 0.3

# --- pieces and rules (firmware/main/chessboard.c) ---------------------------------

ALFIL, CABALLO, PEON, TORRE, REY, DAMA, VACIA = range(7)
LETTERS = "ACPTRD"


def piece(sq):
    return sq & 0x7F


def is_white(sq):
    return bool(sq & WHITE_BIT)


def initial_board():
    """Like iniciarTablero(): 5 random white pieces on rows 1-2, mirrored for black."""
    board = [VACIA] * 25
    for sq in random.sample(range(10), 5):
        kind = random.randrange(VACIA)
        y, x = divmod(sq, 5)
        board[y * 5 + x] = kind | WHITE_BIT
        board[(4 - y) * 5 + x] = kind
    return board


def path_clear(board, x0, y0, x1, y1):
    """Like camino_libre(): no piece between the two squares."""
    dx = (x1 > x0) - (x1 < x0)
    dy = (y1 > y0) - (y1 < y0)
    x, y = x0 + dx, y0 + dy
    while (x, y) != (x1, y1):
        if piece(board[y * 5 + x]) != VACIA:
            return False
        x, y = x + dx, y + dy
    return True


def try_move(board, x0, y0, x1, y1, white):
    """Like movimientoYCaptura(): the board after the move, or None if it's illegal."""
    if (x0, y0) == (x1, y1):
        return None
    src, dst = board[y0 * 5 + x0], board[y1 * 5 + x1]
    if piece(src) == VACIA or is_white(src) != white:
        return None
    dst_empty = piece(dst) == VACIA
    if not dst_empty and is_white(dst) == white:
        return None

    dx, dy = x1 - x0, y1 - y0
    adx, ady = abs(dx), abs(dy)
    kind = piece(src)
    if kind == TORRE:
        ok = (dx == 0 or dy == 0) and path_clear(board, x0, y0, x1, y1)
    elif kind == ALFIL:
        ok = adx == ady and path_clear(board, x0, y0, x1, y1)
    elif kind == DAMA:
        ok = (dx == 0 or dy == 0 or adx == ady) and path_clear(board, x0, y0, x1, y1)
    elif kind == CABALLO:
        ok = (adx, ady) in ((1, 2), (2, 1))
    elif kind == REY:
        ok = adx <= 1 and ady <= 1
    elif kind == PEON:
        forward = 1 if white else -1  # white moves down the screen
        ok = (dx == 0 and dy == forward and dst_empty) or (adx == 1 and dy == forward and not dst_empty)
    else:
        ok = False
    if not ok:
        return None

    new = list(board)
    new[y1 * 5 + x1] = src
    new[y0 * 5 + x0] = VACIA
    if kind == PEON and y1 in (0, 4):
        new[y1 * 5 + x1] = DAMA | (src & WHITE_BIT)  # promotion
    return new

# --- one match ------------------------------------------------------------------------


class Game:
    """Mirror of match.c. All methods run on the main thread."""

    def __init__(self, link, white):
        self.link = link            # anything with send_data(bytes)
        self.white = white
        self.board = [VACIA] * 25
        self.started = False
        self.turn_count = 0
        self.sent_at = 0.0
        if white:  # white makes the board; black gets it from white's first STATE
            self.board = initial_board()
            self.started = True
            self._send_state()

    def my_turn(self):
        return self.started and (self.turn_count % 2 == 0) == self.white

    def _send_state(self):
        self.link.send_data(bytes([MSG_STATE, self.turn_count & 0xFF]) + bytes(self.board))
        self.sent_at = time.time()

    def on_data(self, data):
        """The other side's STATE: take its board if it's newer. True if it was."""
        if len(data) != 27 or data[0] != MSG_STATE:
            return False
        turn = data[1]
        if self.started and turn <= self.turn_count:
            return False  # nothing new: we already have this turn
        self.board = [sq if piece(sq) <= VACIA else VACIA for sq in data[2:]]
        self.turn_count = turn
        self.started = True
        return True

    def tick(self):
        if self.started and time.time() - self.sent_at >= STATE_PERIOD:
            self._send_state()

    def move(self, x0, y0, x1, y1):
        """Our move. Returns an error message, or None if the move was made."""
        if not self.my_turn():
            return "not your turn"
        new = try_move(self.board, x0, y0, x1, y1, self.white)
        if new is None:
            return "illegal move"
        self.board = new
        self.turn_count += 1
        self._send_state()  # right away, not at the next tick
        return None

    def status(self):
        if not self.started:
            return "waiting for the board..."
        return "your turn" if self.my_turn() else "their turn"

# --- terminal ----------------------------------------------------------------------


def show(game, peer_name):
    print("\n    a b c d e")
    for y in range(5):
        row = []
        for x in range(5):
            sq = game.board[y * 5 + x]
            letter = "." if piece(sq) == VACIA else LETTERS[piece(sq)]
            row.append(letter if is_white(sq) or letter == "." else letter.lower())
        print(f"  {y + 1} {' '.join(row)}")
    color = "white (UPPERCASE)" if game.white else "black (lowercase)"
    print(f"  you: {color} vs {peer_name} | turn {game.turn_count} | {game.status()}")


def parse_square(text):
    """'b3' -> (1, 2), or None."""
    if len(text) == 2 and text[0] in "abcde" and text[1] in "12345":
        return "abcde".index(text[0]), int(text[1]) - 1
    return None


def read_lines(lines):
    for line in sys.stdin:
        lines.put(line.strip().lower())
    lines.put("quit")  # stdin closed (Ctrl+D)


def drain(q):
    while not q.empty():
        yield q.get()

# --- main ------------------------------------------------------------------------------


def parse_args():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iface", default="wlp2s0")
    ap.add_argument("--name", default="LAP", help="our 3-letter tag")
    ap.add_argument("--src", default="02:00:00:c5:00:09", help="MAC we send as (sim_boards.sh uses :01..:08)")
    ap.add_argument("--connect", metavar="MAC", help="invite this board (we play white); default: wait to be invited")
    return ap.parse_args()


def main():
    args = parse_args()
    if os.geteuid() != 0:
        sys.exit("needs root (raw socket); run with sudo")
    from scapy.all import AsyncSniffer, Dot11

    me = args.src.lower()
    link = probe.LinkPeer(args.name, probe.Radio(args.iface, me), args.connect.lower() if args.connect else None)
    data_in = queue.Queue()  # DATA payloads from our peer, handled on the main thread
    lines = queue.Queue()    # what the user types

    def on_frame(pkt):  # sniffer thread
        if not pkt.haslayer(Dot11):
            return
        d = pkt[Dot11]
        if d.type != 0 or d.subtype != 13 or d.addr2 == me or d.addr1 not in (me, probe.BROADCAST):
            return
        payload = probe.espnow_payload(bytes(d.payload))
        f = probe.aot_parse(payload) if payload else None
        if f is None:
            return
        link.on_frame(d.addr2, getattr(pkt, "dBm_AntSignal", None), f)
        if f["type"] == probe.T_DATA and link.state == "connected" and d.addr2 == link.peer:
            data_in.put(f["data"])

    sniffer = AsyncSniffer(iface=args.iface, prn=on_frame, store=False)
    sniffer.start()
    threading.Thread(target=read_lines, args=(lines,), daemon=True).start()
    if args.connect:
        print(f"inviting {args.connect} as {args.name.upper()} ({me}); accept on the board")
    else:
        print(f"waiting for a board to invite {args.name.upper()} ({me})")

    game = None
    was_connected = False
    running = True
    try:
        while running:
            link.tick()
            connected = link.state == "connected"
            if connected and not was_connected:
                game = Game(link, white=link.initiator)  # whoever invited plays white
                show(game, link.peer_name)
            elif was_connected and not connected:
                print("*** connection lost")
                game = None
            was_connected = connected

            for data in drain(data_in):
                if game and game.on_data(data):
                    show(game, link.peer_name)
            if game:
                game.tick()

            for line in drain(lines):
                parts = line.split()
                squares = [parse_square(p) for p in parts]
                if line == "quit":
                    running = False
                elif not game:
                    print("not connected yet")
                elif line == "board":
                    show(game, link.peer_name)
                elif len(parts) != 2 or None in squares:
                    print('type a move like "b2 b3", or "board" / "quit"')
                else:
                    error = game.move(*squares[0], *squares[1])
                    print(error) if error else show(game, link.peer_name)
            time.sleep(0.05)
    except KeyboardInterrupt:
        pass
    finally:
        link.hang_up()
        time.sleep(0.2)
        sniffer.stop()


if __name__ == "__main__":
    main()
