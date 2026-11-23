#!/bin/bash
# The host game's loop (started by start_host.sh): runs the headless host game, walks its menus into the free-roam world, and starts it again whenever it ends (the host game exits when the
# server says its event is over, because the game's own event cannot be left from a headless game: NFSMW_HOST_RESTART=1).
SERVER=$1; ROOT=$2
G="$HOME/.local/share/Electronic Arts/Need for Speed Most Wanted"
echo $$ > /tmp/ao/host.loop
while :; do
    rm -rf /tmp/ao/host; mkdir -p /tmp/ao/host
    cp -a "$HOME/.local/share/speedbreaker" /tmp/ao/host/speedbreaker
    printf "enabled=1\nserver=$SERVER\nname=host\nplayers=1\ncar=\n" > /tmp/ao/host/speedbreaker/multiplayer.txt
    : > /tmp/ao/host.pad
    cp "$ROOT/build/main/runtime/SpeedBreaker" "$G/SpeedBreaker-host"
    cd "$G"
    (
        sleep 40
        for step in "1 START:14" "1 START:9" "1 A:9" "1 A:9" "1 A:6" "1 LEFT:2" "1 A:2"; do echo "${step%%:*}" >> /tmp/ao/host.pad; sleep ${step##*:}; done
    ) &
    MENU=$!
    env XDG_DATA_HOME=/tmp/ao/host NFSMW_INPUT_CMD=/tmp/ao/host.pad NFSMW_HEADLESS=1 NFSMW_STUB_CONTINUE=1 NFSMW_MP_HOST=1 NFSMW_HOST_RESTART=1 NFSMW_EVENTS_OPEN=1 NFSMW_RECORD_PATH=/tmp/ao/course.txt ./SpeedBreaker-host > /tmp/ao/host.log 2>&1
    kill $MENU 2>/dev/null
    sleep 3
done
