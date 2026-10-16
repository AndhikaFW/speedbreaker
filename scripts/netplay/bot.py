#!/usr/bin/env python3
"""A scripted second player for testing SpeedBreaker's online play without a second machine.

    python3 scripts/netplay/bot.py --server 127.0.0.1:7777 --room test [--cmd /tmp/bot.cmd] [--log /tmp/bot.log]

Sends a pad state 30 times a second. Each line appended to the command file is "<BUTTON> [seconds]" (A B X Y START BACK LB RB UP DOWN LEFT RIGHT,
or RT / LT for the triggers, LL LR LU LD for the left stick), held for that long (default 0.15 s). Writes what it receives from the game to the
log: the other player's buttons and triggers whenever they change.
"""
import argparse, os, socket, struct, time

MAGIC = b'NFS1'
HELLO, INPUT, WELCOME, RELAY = 1, 2, 3, 4
BUTTONS = {'UP': 1, 'DOWN': 2, 'LEFT': 4, 'RIGHT': 8, 'START': 0x10, 'BACK': 0x20, 'LS': 0x40, 'RS': 0x80,
           'LB': 0x100, 'RB': 0x200, 'A': 0x1000, 'B': 0x2000, 'X': 0x4000, 'Y': 0x8000}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--server', default='127.0.0.1:7777')
    ap.add_argument('--room', default='test')
    ap.add_argument('--cmd', default='/tmp/bot.cmd')
    ap.add_argument('--log', default='/tmp/bot.log')
    args = ap.parse_args()
    host, port = args.server.rsplit(':', 1)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(0.02)
    server = (host, int(port))
    open(args.cmd, 'a').close()
    pos = os.path.getsize(args.cmd)
    log = open(args.log, 'w', buffering=1)
    held = []   # (until, buttons, lt, rt, lx, ly)
    seq = 0
    last_hello = 0.0
    last_remote = None
    next_send = time.time()
    while True:
        now = time.time()
        if now - last_hello > 1.0:
            sock.sendto(MAGIC + bytes([HELLO]) + args.room.encode().ljust(16, b'\0'), server)
            last_hello = now
        with open(args.cmd) as f:
            f.seek(pos)
            for line in f.read().splitlines():
                parts = line.split()
                if not parts:
                    continue
                name = parts[0].upper()
                secs = float(parts[1]) if len(parts) > 1 else 0.15
                b = BUTTONS.get(name, 0)
                lt = 255 if name == 'LT' else 0
                rt = 255 if name == 'RT' else 0
                lx = -32767 if name == 'LL' else 32767 if name == 'LR' else 0
                ly = 32767 if name == 'LU' else -32767 if name == 'LD' else 0
                held.append((now + secs, b, lt, rt, lx, ly))
                log.write('%.2f command %s %.2fs\n' % (now, name, secs))
            pos = f.tell()
        held = [h for h in held if h[0] > now]
        b = lt = rt = lx = ly = 0
        for _, hb, hlt, hrt, hlx, hly in held:
            b |= hb; lt = max(lt, hlt); rt = max(rt, hrt); lx = lx or hlx; ly = ly or hly
        if now >= next_send:
            sock.sendto(MAGIC + bytes([INPUT]) + struct.pack('<IHBBhhhh', seq, b, lt, rt, lx, ly, 0, 0), server)
            seq += 1
            next_send = now + 1 / 30
        try:
            data, _ = sock.recvfrom(256)
        except socket.timeout:
            continue
        if data[:4] != MAGIC:
            continue
        if data[4] == WELCOME:
            log.write('%.2f welcome: %d player(s)\n' % (now, data[5]))
        elif data[4] == RELAY and len(data) >= 22:
            slot = data[5]
            rseq, rb, rlt, rrt, rlx, rly, rrx, rry = struct.unpack('<IHBBhhhh', data[6:22])
            state = (rb, rlt, rrt, rlx, rly)
            if state != last_remote:
                log.write('%.2f from slot %d (seq %d): buttons %04X lt %d rt %d lx %d ly %d\n' % (now, slot, rseq, rb, rlt, rrt, rlx, rly))
                last_remote = state


if __name__ == '__main__':
    main()
