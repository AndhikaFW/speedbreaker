#!/usr/bin/env python3
"""Tests of the lobby server (scripts/netplay/server.py): starts it on a spare port and drives it with two UDP clients.

    python3 scripts/netplay/test_lobby.py
"""
import json, os, socket, struct, subprocess, sys, time

MAGIC = b'NFS1'
HELLO, INPUT, WELCOME, RELAY, FRAMES, RELAYF, CTRL, REPLY, EVENT = 1, 2, 3, 4, 5, 6, 7, 8, 9
PORT = 17777
HERE = os.path.dirname(os.path.abspath(__file__))


class Client:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(0.3)
        self.next_id = 1
        self.events = []
        self.relays = []

    def pump(self, wait=0.3):
        end = time.time() + wait
        out = []
        while time.time() < end:
            try:
                data, _ = self.sock.recvfrom(1024)
            except socket.timeout:
                continue
            if data[:4] != MAGIC:
                continue
            if data[4] == REPLY:
                out.append(json.loads(data[5:]))
            elif data[4] == EVENT:
                self.events.append(json.loads(data[5:]))
            elif data[4] in (RELAY, RELAYF):
                self.relays.append(data)
        return out

    def call(self, cmd, **kw):
        rid = self.next_id
        self.next_id += 1
        packet = MAGIC + bytes([CTRL]) + json.dumps({'cmd': cmd, 'id': rid, **kw}).encode()
        for _ in range(5):
            self.sock.sendto(packet, ('127.0.0.1', PORT))
            for r in self.pump(0.3):
                if r.get('id') == rid:
                    return r
        raise AssertionError('no reply to ' + cmd)

    def raw(self, kind, payload=b''):
        self.sock.sendto(MAGIC + bytes([kind]) + payload, ('127.0.0.1', PORT))


failures = []


def check(cond, what):
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        failures.append(what)


def main():
    server = subprocess.Popen([sys.executable, '-I', os.path.join(HERE, 'server.py'), '--port', str(PORT), '--bind', '127.0.0.1'],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.6)
    try:
        a, b, c = Client(), Client(), Client()
        check(a.call('list')['rooms'] == [], 'no rooms at first')
        check(not a.call('create', room='', name='A')['ok'], 'a room needs a name')
        r = a.call('create', room='race1', password='pw', name='Salt', setup={'mode': 'circuit', 'track': 1, 'laps': 2})
        check(r['ok'] and r['you'] == 0 and r['locked'], 'create makes the host slot 0, locked')
        check(not c.call('create', room='race1', name='C')['ok'], 'a taken name is refused')
        rooms = b.call('list')['rooms']
        check(len(rooms) == 1 and rooms[0]['name'] == 'race1' and rooms[0]['locked'] and rooms[0]['players'] == 1 and rooms[0]['setup']['laps'] == 2,
              'the list shows the room, locked, 1 player, its setup')
        check(not b.call('join', room='race1', password='nope', name='Dhika')['ok'], 'a wrong password is refused')
        check(not b.call('join', room='nothing', name='Dhika')['ok'], 'a missing room is refused')
        r = b.call('join', room='race1', password='pw', name='Dhika')
        check(r['ok'] and r['you'] == 1 and r['players'] == 2, 'join as slot 1')
        a.pump(0.3)
        check(any(e.get('event') == 'room' and e['players'] == 2 for e in a.events), 'the host is told the guest joined')
        check(not c.call('join', room='race1', password='pw', name='C')['ok'], 'a full room is refused')
        check(not a.call('start')['ok'], 'start refused until the guest is ready')
        check(not b.call('setup', setup={'laps': 9})['ok'], 'the guest cannot change the race')
        check(b.call('ready', ready=True)['ok'], 'the guest readies')
        a.pump(0.3)
        check(any(e.get('event') == 'room' and e['members'][1]['ready'] for e in a.events), 'the host sees the guest ready')
        check(a.call('setup', setup={'mode': 'sprint', 'track': 4})['ok'], 'the host changes the race')
        st = b.call('status')
        check(st['setup']['track'] == 4 and not st['members'][1]['ready'], 'a changed setup clears the ready flags')
        check(not a.call('start')['ok'], 'start refused again after the change')
        b.call('ready', ready=True)
        # idempotent requests: the same id answered again, not run twice
        rid = 99
        pkt = MAGIC + bytes([CTRL]) + json.dumps({'cmd': 'ready', 'ready': False, 'id': rid}).encode()
        b.sock.sendto(pkt, ('127.0.0.1', PORT)); b.pump(0.2)
        b.sock.sendto(pkt, ('127.0.0.1', PORT)); got = b.pump(0.3)
        check(len(got) == 1 and got[0]['id'] == rid, 'a repeated request id gets its answer again')
        b.call('ready', ready=True)
        a.events.clear(); b.events.clear()
        check(a.call('start')['ok'], 'the host starts')
        a.pump(0.3); b.pump(0.3)
        sa = [e for e in a.events if e.get('event') == 'start']
        sb = [e for e in b.events if e.get('event') == 'start']
        check(len(sa) == 1 and len(sb) == 1, 'both players get the start event')
        check(sa and sb and sa[0]['slot'] == 0 and sb[0]['slot'] == 1 and sa[0]['seed'] == sb[0]['seed'] and sa[0]['setup']['track'] == 4,
              'start gives each its slot, the same seed and the setup')
        check(not c.call('join', room='race1', password='pw', name='C')['ok'], 'no joining once racing')
        # game traffic relays between the two members, nobody else
        a.raw(FRAMES, bytes([0, 1]) + struct.pack('<I', 7) + b'\x01' * 12)
        b.pump(0.3); c.pump(0.2)
        check(len(b.relays) == 1 and b.relays[0][4] == RELAYF and b.relays[0][5] == 0 and not c.relays, 'frame inputs reach the other member only, tagged with the sender slot')
        b.relays.clear()
        a.raw(INPUT, struct.pack('<I', 1) + b'\x02' * 12)
        b.pump(0.3)
        check(len(b.relays) == 1 and b.relays[0][4] == RELAY, 'plain inputs relay too')
        b.events.clear()
        a.call('leave')
        b.pump(0.3)
        check(any(e.get('event') == 'closed' for e in b.events), 'the host leaving closes the room for the guest')
        check(b.call('list')['rooms'] == [], 'the room is gone')
        # the old way in: HELLO with a room name
        d, e = Client(), Client()

        def welcome(client):
            for _ in range(5):
                data = client.sock.recvfrom(1024)[0]
                if data[:4] == MAGIC and data[4] == WELCOME:
                    return data[5]
            return None

        d.raw(HELLO, b'old'.ljust(16, b'\0'))
        check(welcome(d) == 1, 'HELLO still joins a room by name')
        e.raw(HELLO, b'old'.ljust(16, b'\0'))
        check(welcome(e) == 2, 'a second HELLO makes two players')
        d.raw(INPUT, struct.pack('<I', 5) + b'\x03' * 12)
        e.pump(0.3)
        check(len(e.relays) == 1, 'and relays their inputs')
    finally:
        server.terminate()
        server.wait()
    print('\n%d check(s) failed' % len(failures) if failures else '\nall checks passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
