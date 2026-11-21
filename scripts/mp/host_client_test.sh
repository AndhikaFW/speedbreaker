#!/bin/bash
# host (headless career) + client (windowed career) + test server on 27961; only throw-away games (XDG_DATA_HOME under /tmp/ao) are ever stopped
cd ~/.cache/NFS-MostWanted/tools
for p in $(pgrep -x SpeedBreaker); do tr '\0' '\n' < /proc/$p/environ | grep -q "XDG_DATA_HOME=/tmp/ao/" && kill -9 $p; done
for p in $(pgrep -a mp_server | grep 27961 | cut -d' ' -f1); do kill $p; done; sleep 3
S=~/.cache/NFS-MostWanted/speedbreaker/build/main/runtime
G="$HOME/.local/share/Electronic Arts/Need for Speed Most Wanted"
(nohup $S/mp_server 27961 > /tmp/ao/ts.log 2>&1 &); sleep 1
cp "$S/SpeedBreaker" "$G/SpeedBreaker"
mk() { rm -rf /tmp/ao/$1; mkdir -p /tmp/ao/$1; cp -a "$HOME/.local/share/speedbreaker" /tmp/ao/$1/speedbreaker; sed -i 's/^fullscreen = .*/fullscreen = false/' /tmp/ao/$1/speedbreaker/settings.toml; printf "enabled=1\nserver=127.0.0.1:27961\nname=$2\nplayers=1\ncar=\n$3" > /tmp/ao/$1/speedbreaker/multiplayer.txt; : > /tmp/ao/$1.cmd; }
mk xh host ""; mk xc client ""
cd "$G"
(env XDG_DATA_HOME=/tmp/ao/xh NFSMW_INPUT_CMD=/tmp/ao/xh.cmd NFSMW_HEADLESS=1 NFSMW_STUB_CONTINUE=1 NFSMW_MP_HOST=1 NFSMW_EVENTS_OPEN=1 NFSMW_PROBE_NATIVES=1 NFSMW_HOST_CMD=/tmp/ao/host.cmd NFSMW_MP_TELEPORT="-2318.0,150.9,1632.5" ./SpeedBreaker > /tmp/sp_xh.log 2>&1 &)
(env XDG_DATA_HOME=/tmp/ao/xc NFSMW_INPUT_CMD=/tmp/ao/xc.cmd NFSMW_MP_WORLD_LOGIC=1 NFSMW_STUB_CONTINUE=1 NFSMW_MOTION_BLUR=0 NFSMW_RESOLUTION=1x NFSMW_SHADOW_SIZE=800 NFSMW_PERF_OVERLAY=0 NFSMW_MP_TELEPORT="-2318.0,150.9,1641.5" ./SpeedBreaker > /tmp/sp_xc.log 2>&1 &)
sleep 40
for f in xh xc; do
 ( for step in "1 START:14" "1 START:9" "1 A:9" "1 A:9" "1 A:6" "1 LEFT:2" "1 A:2"; do echo "${step%%:*}" >> /tmp/ao/$f.cmd; sleep ${step##*:}; done ) &
done
wait
