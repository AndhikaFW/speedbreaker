#!/usr/bin/env python3
"""Relay server for SpeedBreaker online play (runtime/net/netplay.cpp).

    python3 scripts/netplay/server.py [--port 7777]

Rooms of up to two players, told apart by their UDP address. A player's INPUT packets are forwarded to the other player in the room as RELAY
packets; HELLO is answered with the number of players. Nothing is stored; a player silent for 15 s leaves the room.
"""
import argparse, socket, struct, time

MAGIC = b'NFS1'
HELLO, INPUT, WELCOME, RELAY, FRAMES, RELAYF = 1, 2, 3, 4, 5, 6
PAD = 12


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=7777)
    ap.add_argument('--bind', default='0.0.0.0')
    args = ap.parse_args()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.bind, args.port))
    sock.settimeout(1.0)
    rooms = {}   # room -> {addr: last seen}
    where = {}   # addr -> room
    slots = {}   # addr -> slot (order of arrival in the room)
    print('relay on %s:%d' % (args.bind, args.port), flush=True)
    while True:
        now = time.time()
        for room in list(rooms):
            for addr in [a for a, t in rooms[room].items() if now - t > 15]:
                del rooms[room][addr]; where.pop(addr, None); slots.pop(addr, None)
                print('%s left %s' % (addr, room), flush=True)
            if not rooms[room]:
                del rooms[room]
        try:
            data, addr = sock.recvfrom(512)
        except socket.timeout:
            continue
        if len(data) < 5 or data[:4] != MAGIC:
            continue
        kind = data[4]
        if kind == HELLO and len(data) >= 21:
            room = data[5:21].rstrip(b'\0').decode('latin1')
            members = rooms.setdefault(room, {})
            if addr not in members and len(members) >= 2:
                continue   # full
            if addr not in members:
                slots[addr] = len(members)
                print('%s joined %s (%d player(s))' % (addr, room, len(members) + 1), flush=True)
            members[addr] = now
            where[addr] = room
            sock.sendto(MAGIC + bytes([WELCOME, len(members)]), addr)
        elif kind == FRAMES and len(data) >= 7 and addr in where:
            room = where[addr]
            rooms[room][addr] = now
            relay = MAGIC + bytes([RELAYF, slots.get(addr, 0)]) + data[6:]
            for other in rooms[room]:
                if other != addr:
                    sock.sendto(relay, other)
        elif kind == INPUT and len(data) >= 5 + 4 + PAD and addr in where:
            room = where[addr]
            rooms[room][addr] = now
            relay = MAGIC + bytes([RELAY, slots.get(addr, 0)]) + data[5:5 + 4 + PAD]
            for other in rooms[room]:
                if other != addr:
                    sock.sendto(relay, other)


if __name__ == '__main__':
    main()
