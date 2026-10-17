#!/usr/bin/env python3
"""Lobby and relay server for SpeedBreaker online play (runtime/net/netplay.cpp).

    python3 scripts/netplay/server.py [--port 7777] [--bind 0.0.0.0]

One UDP port, two kinds of packet (all start with 'NFS1' and a type byte):

  Game traffic (unchanged): HELLO joins a room by name, INPUT / FRAMES carry a player's pad and are forwarded to the other player in the room.

  Lobby control (type 7 CTRL, JSON after the type byte; the answer is type 8 REPLY with the same JSON plus "ok" and the request's "id"):
    {"cmd":"list"}                                  -> {"rooms":[{"name","players","max","locked","state","setup"}]}
    {"cmd":"create","room":"x","password":"","name":"Salt","setup":{...}}    (the creator is the host, slot 0)
    {"cmd":"join","room":"x","password":"","name":"Dhika"}                    (the second player is the guest, slot 1)
    {"cmd":"leave"}
    {"cmd":"ready","ready":true}
    {"cmd":"setup","setup":{...}}                   (host only, before the start: mode, track, laps, traffic, ...)
    {"cmd":"start"}                                 (host only, when the guest is ready: both players get a START event)
    {"cmd":"status"}                                -> the room as the player sees it
  Events pushed to the members (type 9 EVENT, JSON): {"event":"room", ...the room...} on every change, {"event":"start","slot":0|1,"setup":{...},"seed":N},
  {"event":"closed","reason":"host left"}. A client re-sends a request it has no REPLY for after 500 ms; each request carries an "id", and a repeated id gets the
  answer again without being run twice. A player silent for 15 s leaves.
"""
import argparse, json, random, socket, time

MAGIC = b'NFS1'
HELLO, INPUT, WELCOME, RELAY, FRAMES, RELAYF, CTRL, REPLY, EVENT = 1, 2, 3, 4, 5, 6, 7, 8, 9
PAD = 12
MAX_PLAYERS = 2
TIMEOUT = 15.0


class Player:
    def __init__(self, addr, name=''):
        self.addr, self.name, self.ready, self.seen, self.slot = addr, name, False, time.time(), 0


class Room:
    def __init__(self, name, password, host, setup):
        self.name, self.password, self.setup = name, password, setup or {}
        self.players = [host]          # the host first
        self.state = 'lobby'           # lobby | racing
        self.seed = 0

    def view(self):
        return {'name': self.name, 'players': len(self.players), 'max': MAX_PLAYERS, 'locked': bool(self.password), 'state': self.state,
                'setup': self.setup,
                'members': [{'name': p.name, 'slot': i, 'ready': p.ready} for i, p in enumerate(self.players)]}


class Server:
    def __init__(self, sock):
        self.sock = sock
        self.rooms = {}        # name -> Room
        self.where = {}        # addr -> Room
        self.players = {}      # addr -> Player
        self.replies = {}      # (addr, id) -> (payload, time), so a repeated request is answered again, not run again

    # ---- sending
    def send_json(self, addr, kind, obj):
        self.sock.sendto(MAGIC + bytes([kind]) + json.dumps(obj, separators=(',', ':')).encode(), addr)

    def push_room(self, room, event='room'):
        for p in room.players:
            obj = {'event': event, **room.view()}
            obj['you'] = room.players.index(p)
            self.send_json(p.addr, EVENT, obj)

    # ---- membership
    def leave(self, addr, reason='left'):
        room = self.where.pop(addr, None)
        if not room:
            return
        player = next((p for p in room.players if p.addr == addr), None)
        if player:
            room.players.remove(player)
        print('%s left %s (%s)' % (addr, room.name, reason), flush=True)
        if not room.players:
            self.rooms.pop(room.name, None)
            return
        # the host going ends the room; the guest going leaves it open with the host alone
        if player is not None and player.slot == 0:  # (slot is the position the player held in the room)
            for p in room.players:
                self.send_json(p.addr, EVENT, {'event': 'closed', 'reason': 'host left'})
                self.where.pop(p.addr, None)
            self.rooms.pop(room.name, None)
        else:
            for p in room.players:
                p.ready = False
            room.state = 'lobby'
            self.push_room(room)

    def join_room(self, addr, room, name):
        player = self.players.setdefault(addr, Player(addr, name))
        player.name = name or player.name or 'player'
        player.ready = False
        player.slot = len(room.players)
        room.players.append(player)
        self.where[addr] = room
        for i, p in enumerate(room.players):
            p.slot = i
        print('%s (%s) joined %s as slot %d' % (addr, player.name, room.name, player.slot), flush=True)
        self.push_room(room)

    # ---- control commands
    def control(self, addr, req):
        cmd = req.get('cmd')
        player = self.players.get(addr)
        room = self.where.get(addr)
        if cmd == 'list':
            return {'ok': True, 'rooms': [r.view() | {'members': None} for r in self.rooms.values()]}
        if cmd == 'create':
            if room:
                return {'ok': False, 'error': 'already in a room'}
            rname = str(req.get('room', '')).strip()[:16]
            if not rname:
                return {'ok': False, 'error': 'a room needs a name'}
            if rname in self.rooms:
                return {'ok': False, 'error': 'that name is taken'}
            host = self.players.setdefault(addr, Player(addr))
            host.name = str(req.get('name', 'host'))[:16]
            host.ready = False
            host.slot = 0
            new = Room(rname, str(req.get('password', '')), host, req.get('setup') if isinstance(req.get('setup'), dict) else {})
            self.rooms[rname] = new
            self.where[addr] = new
            print('%s created %s%s' % (addr, rname, ' (locked)' if new.password else ''), flush=True)
            return {'ok': True, 'you': 0, **new.view()}
        if cmd == 'join':
            if room:
                return {'ok': False, 'error': 'already in a room'}
            target = self.rooms.get(str(req.get('room', '')))
            if not target:
                return {'ok': False, 'error': 'no such room'}
            if target.password and target.password != str(req.get('password', '')):
                return {'ok': False, 'error': 'wrong password'}
            if len(target.players) >= MAX_PLAYERS:
                return {'ok': False, 'error': 'the room is full'}
            if target.state != 'lobby':
                return {'ok': False, 'error': 'the race has started'}
            self.join_room(addr, target, str(req.get('name', 'guest'))[:16])
            return {'ok': True, 'you': self.where[addr].players.index(self.players[addr]), **target.view()}
        if not room:
            return {'ok': False, 'error': 'not in a room'}
        me = room.players.index(player) if player in room.players else -1
        if cmd == 'leave':
            self.leave(addr, 'asked')
            return {'ok': True}
        if cmd == 'status':
            return {'ok': True, 'you': me, **room.view()}
        if cmd == 'ready':
            player.ready = bool(req.get('ready', True))
            self.push_room(room)
            return {'ok': True}
        if cmd == 'setup':
            if me != 0:
                return {'ok': False, 'error': 'only the host chooses the race'}
            if room.state != 'lobby':
                return {'ok': False, 'error': 'the race has started'}
            room.setup = req.get('setup') if isinstance(req.get('setup'), dict) else {}
            for p in room.players:
                p.ready = False   # a changed setup needs everyone to agree again
            self.push_room(room)
            return {'ok': True}
        if cmd == 'start':
            if me != 0:
                return {'ok': False, 'error': 'only the host starts the race'}
            if len(room.players) < MAX_PLAYERS:
                return {'ok': False, 'error': 'waiting for the second player'}
            if not all(p.ready for p in room.players[1:]):
                return {'ok': False, 'error': 'the other player is not ready'}
            room.state = 'racing'
            room.seed = random.getrandbits(31)
            print('%s starts in %s (seed %d)' % (addr, room.name, room.seed), flush=True)
            for i, p in enumerate(room.players):
                self.send_json(p.addr, EVENT, {'event': 'start', 'slot': i, 'setup': room.setup, 'seed': room.seed, 'room': room.name})
            return {'ok': True}
        return {'ok': False, 'error': 'unknown command'}

    # ---- the packet loop
    def handle(self, data, addr):
        if len(data) < 5 or data[:4] != MAGIC:
            return
        kind = data[4]
        now = time.time()
        if addr in self.players:
            self.players[addr].seen = now
        if kind == CTRL:
            try:
                req = json.loads(data[5:].decode())
            except Exception:
                return
            rid = req.get('id')
            key = (addr, rid)
            if rid is not None and key in self.replies:
                self.send_json(addr, REPLY, self.replies[key][0])
                return
            self.players.setdefault(addr, Player(addr)).seen = now
            answer = self.control(addr, req)
            answer['id'] = rid
            if rid is not None:
                self.replies[key] = (answer, now)
            self.send_json(addr, REPLY, answer)
        elif kind == HELLO and len(data) >= 21:
            # the plain way in (clients of before the lobby, and tests): a room by name, made if missing; the first in is the host
            name = data[5:21].rstrip(b'\0').decode('latin1')
            room = self.where.get(addr)
            if room is None:
                target = self.rooms.get(name)
                if target is None:
                    host = self.players.setdefault(addr, Player(addr, 'player'))
                    host.slot = 0
                    target = Room(name, '', host, {})
                    self.rooms[name] = target
                    self.where[addr] = target
                    print('%s joined %s (1 player(s))' % (addr, name), flush=True)
                elif len(target.players) < MAX_PLAYERS:
                    self.join_room(addr, target, 'player')
                else:
                    return
                room = self.where[addr]
            self.sock.sendto(MAGIC + bytes([WELCOME, len(room.players)]), addr)
        elif kind in (INPUT, FRAMES) and addr in self.where:
            room = self.where[addr]
            me = next((p for p in room.players if p.addr == addr), None)
            slot = room.players.index(me) if me else 0
            if kind == INPUT and len(data) >= 5 + 4 + PAD:
                relay = MAGIC + bytes([RELAY, slot]) + data[5:5 + 4 + PAD]
            elif kind == FRAMES and len(data) >= 7:
                relay = MAGIC + bytes([RELAYF, slot]) + data[6:]
            else:
                return
            for p in room.players:
                if p.addr != addr:
                    self.sock.sendto(relay, p.addr)

    def expire(self):
        now = time.time()
        for addr in [a for a, p in self.players.items() if now - p.seen > TIMEOUT]:
            self.leave(addr, 'timeout')
            self.players.pop(addr, None)
        for key in [k for k, (_, t) in self.replies.items() if now - t > 30]:
            del self.replies[key]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=7777)
    ap.add_argument('--bind', default='0.0.0.0')
    args = ap.parse_args()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.bind, args.port))
    sock.settimeout(1.0)
    server = Server(sock)
    print('lobby and relay on %s:%d' % (args.bind, args.port), flush=True)
    last_expire = 0.0
    while True:
        try:
            data, addr = sock.recvfrom(1024)
            server.handle(data, addr)
        except socket.timeout:
            pass
        if time.time() - last_expire > 1.0:
            server.expire()
            last_expire = time.time()


if __name__ == '__main__':
    main()
