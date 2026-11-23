#!/bin/bash
# usage: start_host.sh [server host:port, default 127.0.0.1:27960]
# The host game: a headless copy of the game that runs the game's own events for the players on the server. It uses a throw-away copy of the user's data folder (XDG_DATA_HOME=/tmp/ao/host,
# so the user's own settings and saves are never touched) and walks the menus into the career free-roam world with pad commands. Only a host game started by this script is ever stopped.
SERVER=${1:-127.0.0.1:27960}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
G="$HOME/.local/share/Electronic Arts/Need for Speed Most Wanted"
for p in $(pgrep -f "SpeedBreaker-host"); do tr '\0' '\n' < /proc/$p/environ | grep -q "XDG_DATA_HOME=/tmp/ao/host$" && kill -9 $p; done
sleep 2
mkdir -p /tmp/ao; rm -rf /tmp/ao/host; mkdir -p /tmp/ao/host
cp -a "$HOME/.local/share/speedbreaker" /tmp/ao/host/speedbreaker
printf "enabled=1\nserver=$SERVER\nname=host\nplayers=1\ncar=\n" > /tmp/ao/host/speedbreaker/multiplayer.txt
: > /tmp/ao/host.pad
cp "$ROOT/build/main/runtime/SpeedBreaker" "$G/SpeedBreaker-host"
cd "$G"
(env XDG_DATA_HOME=/tmp/ao/host NFSMW_INPUT_CMD=/tmp/ao/host.pad NFSMW_HEADLESS=1 NFSMW_STUB_CONTINUE=1 NFSMW_MP_HOST=1 NFSMW_EVENTS_OPEN=1 ./SpeedBreaker-host > /tmp/ao/host.log 2>&1 &)
echo "host game started, log /tmp/ao/host.log"
sleep 40
for step in "1 START:14" "1 START:9" "1 A:9" "1 A:9" "1 A:6" "1 LEFT:2" "1 A:2"; do echo "${step%%:*}" >> /tmp/ao/host.pad; sleep ${step##*:}; done
sleep 60
grep -c "" /tmp/ao/host.log | sed 's/^/log lines: /'
grep "host joined\|\[mp\] connecting\|crash\]" /tmp/ao/host.log | head -3
