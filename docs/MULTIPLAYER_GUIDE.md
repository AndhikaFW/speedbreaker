# Multiplayer races: how to host and join

Race other people in the game's own races, over the internet or a LAN. Each player runs their own copy of the game and a small server relays everyone's car; the cars of the other players
appear as the race's opponents (they take over the AI cars). Career mode is not part of it. How it works inside: [MULTIPLAYER.md](MULTIPLAYER.md).

## What you need
- A copy of SpeedBreaker built from this fork (the multiplayer layer is in `runtime/net/mp_*` and `runtime/game/mp_adapter.cpp`), with your own game files, on every machine.
- One machine that runs the server and that every player can reach. The server is the `mp_server` program built next to the game (`ninja -C build/main mp_server`): it needs no game files.
  It listens on UDP port 27960 by default and can run anywhere, also on a machine that doesn't play.
- The players' machines need to reach the server's address. Over the internet: forward UDP 27960 on the router, or put all machines on one virtual network such as ZeroTier or Tailscale
  and use the server's address on it (this is what the tests used). On the server machine open the port in its firewall (Fedora: `sudo firewall-cmd --add-port=27960/udp`; add `--permanent`
  and `sudo firewall-cmd --reload` to keep it after a restart).

## Host
```
build/main/runtime/mp_server 27960
```
It prints a line when a player joins or leaves. Leave it running.

## Join
Open the settings (F1, or Back + Start on a pad), the **Online** page, section *Multiplayer races*:
- tick **Join multiplayer races**,
- **Server**: the server's address and port, for example `10.65.96.90:27960`,
- **Your name**,
- **Players to wait for at the start**: how many players the race has, yourself included (1 = don't wait).

The settings are saved (`multiplayer.txt` in the user data folder) and take effect from the next race. The status line under them shows the connection. For scripts the environment variables
`NFSMW_MP_SERVER`, `NFSMW_MP_NAME` and `NFSMW_MP_PLAYERS` override the page. The old *Online Play* switch (the earlier lockstep link) is ignored while multiplayer is on.

## Cars
Pick your car in the game's own car selection, as always. Your game announces the car it made for you to the others as soon as the race loads, and every game makes its opponents as the other
players' cars: the opponent that stands for a player is that player's real car (same model), in the order the players joined. A game waits a few seconds (up to 25, or press B) for the other
players of the race to announce their cars before it creates its opponents. The connection to the server is made as soon as multiplayer is switched on and kept between races, so the
settings page shows who is on the server and whether they are in a race or in the menus.

## Race
1. Every player starts **the same race**: the same mode, **the same track and direction**, with at least as many opponents as there are other players (the other players take over the first
   opponents). Choose it in Quick Race > Custom Race; Quick Play picks a random track.
2. When your race has loaded, your game connects to the server and **holds the start** (the screen stays on the loading bar, with a message at the bottom) until all the players are there.
   Press **B** (or Back) to stop waiting and start without the others. The wait ends by itself after five minutes.
3. Everybody is released together; the games line their race clocks up in the first seconds, so the countdown and GO happen at the same time. Players start in the grid slots in the order they
   joined.
4. Race. When the race is over (quit to the menu, or finish), your game leaves the server; start the next race the same way.

## Limits
- Other players have no name tags or minimap marks (the settings page lists the players and their cars).
- Laps and finishing places are the game's own (computed on each machine from where the cars are); there is no shared results screen yet.
- No lobby: the players agree the race outside the game.
- Basic protection only: the server and every game drop car states that are not finite or are far outside what a car can do, limit one player to 120 states a second, clean player names, and
  disconnect a connection that does not say hello within five seconds. A modified client can still cheat with its own car (each game is trusted with it); there are no accounts or passwords.

## If it does not work
- *Cannot reach the server*: the status line says so. Check the address, that `mp_server` is running, the firewall port, and (on ZeroTier) that the service is up and shows the address you typed.
- *Waiting forever*: the other player's game is not at the start yet, or has a different `Players to wait for`, or an older build (every machine has to be on the same commit; the server and
  the games must have the same protocol version, which changes whenever the messages do: rebuild everything after a `git pull`).
- *"Is not in the same race as you"*: the message at the start says another player is held hundreds of metres from your start line, i.e. a different track or direction. Quit (pause menu) and choose the same race.
