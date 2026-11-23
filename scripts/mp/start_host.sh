#!/bin/bash
# usage: start_host.sh [server host:port, default 127.0.0.1:27960]
# The host game: a headless copy of the game that runs the game's own events for the players on the server. It uses a throw-away copy of the user's data folder (XDG_DATA_HOME=/tmp/ao/host,
# so the user's own settings and saves are never touched) and walks the menus into the career free-roam world with pad commands (host_run.sh, which also starts it again after each event).
# Only a host game started by this script is ever stopped.
SERVER=${1:-127.0.0.1:27960}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p /tmp/ao
if [ -f /tmp/ao/host.loop ]; then
    lp=$(cat /tmp/ao/host.loop)
    tr '\0' ' ' < /proc/$lp/cmdline 2>/dev/null | grep -q host_run.sh && kill $lp
fi
for p in $(pgrep -x SpeedBreaker-host 2>/dev/null) $(pgrep -f "SpeedBreaker-host" 2>/dev/null); do tr '\0' '\n' < /proc/$p/environ 2>/dev/null | grep -q "XDG_DATA_HOME=/tmp/ao/host$" && kill -9 $p; done
sleep 2
nohup "$ROOT/scripts/mp/host_run.sh" "$SERVER" "$ROOT" > /tmp/ao/host_run.out 2>&1 &
echo "host game started, log /tmp/ao/host.log (it starts again after each event)"
