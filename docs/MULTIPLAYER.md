# Online multiplayer: plan and status

Goal: live online play in a shared world, built on the game's own two-player split-screen mode (the user reports the game has one).
Design: **lockstep**. Each player runs the whole game on their own machine in two-player mode; player 1 is the local controller, player 2's
"controller" is fed from the network. Every simulation step only the inputs cross the network. The game's world advances in fixed 1/60 s
steps from an accumulator (`game/frame_time.cpp`), which suits lockstep, provided the simulation is deterministic.

## Phases
1. **Second controller** (done in part, below).
2. **Determinism check**: record both players' inputs, replay twice, compare the cars' positions. If traffic, AI or physics depend on frame
   timing or unseeded randomness, find and pin those sources. Make or break.
3. **Netcode**: UDP, a few steps of input delay, a handshake that agrees the random seed and race setup, desync detection.
4. **Internet play**: NAT traversal or a relay, finding opponents.
Risks: determinism (may need work in many places), both views shown unless the renderer is changed to draw only the local one, input delay.

## Phase 1 status
Done (`runtime/hid/input.cpp`): XInput controller 1 is a second SDL gamepad (the first one player 1 is not using; a lone pad stays player 1's),
or `NFSMW_INPUT_SCRIPT2` (the `NFSMW_INPUT_SCRIPT` syntax) for tests without one. Sticks, triggers and rumble go through the same deadzones and
Vibration setting; no keyboard, no Back/Start chord for it. `NFSMW_LOG_PLAYER2=1` logs the first time the game asks about controller 1.
**Checked:** the game does ask about controller 1 at startup (before it only ever heard "not connected"); with player 2 present it was told
"connected" and the game reached the same point as without (frame 600 at 22 s, no new problems in the log).
**Local two-player race reached (driven with `NFSMW_INPUT_SCRIPT2` and the new `NFSMW_INPUT_CMD` file, one step at a time):**
main menu > Quick Race > Split Screen > Circuit > track > options > player 1's car and transmission > **"Select player 2: Player 2 press START"**
(the game identifies player 2 by the controller that presses START; it needs XInput controller 1 to answer, which is what Phase 1 added) >
player 2's car and transmission > the race starts with both HUDs (positions 1 and 2, lap 1/2, two speedometers). Seen: player 1's
throttle moved car 1 (top speedometer 24 mph, the other at 0); the top/bottom split-screen view was drawn.
**Player 2 controls a car (checked):** with only player 2's throttle pressed (`2 RT`), the speedometer in view went from 32 to 62 mph while
the other car's stayed at 0.
**Cost of the local mode (corrected):** the 120-170 ms frames seen once were a transient. Settled over about 30 s: 16-17 fps, GPU 60 ms, about 4500
draws and 30 passes a frame, against a single-player race at 26 fps, GPU 38 ms, about 2400 draws and 18 passes. So two views cost about 1.6x
the GPU time (about 2x the draws): what two scene renders should, not an anomaly. For online play, drawing only the local view would bring
it back to about single-player cost, but the game draws both views, so the viewport setup would need changing; not looked at yet.
**Not verified:** a real second pad (only the script and command file were used).

## Phase 2 (determinism): what has to hold, from what is known
- The game polls the controller once per frame, but its world advances in 1/60 s steps from a real-time accumulator, so the steps per frame
  depend on how long the frame took. Two machines of different speeds would run different numbers of steps between the same two inputs.
  Lockstep needs the steps per frame fixed (for example always the same number per presented frame, from the vblank count; `game/frame_time.cpp`
  and `game/whole_vblanks.h` already quantise frame time, which is the place to look).
- Inputs are then exchanged per frame, and each frame applies both players' inputs for that frame's steps.
- Random numbers, AI and traffic must start from the same seed on both machines (the race setup is chosen by the host).
- To measure it: apply scripted inputs by frame number (not time), run twice, hash the cars' state each frame, and compare. The cars' memory
  addresses move between launches (heap), so the hash needs the cars found by walking the game's own structures, not by a fixed address.

## Phase 2 status
Built (all off by default; nothing changes without them):
- `NFSMW_FIXED_STEPS=<1-4>`: every game frame is exactly that many world steps, whatever it took (`game/frame_time.cpp`). It also makes the
  game's `mftb` (the timebase it reads, `cpu/guest_time.h`) a function of the game frame (n/60 s a frame, plus a few ticks per read in a frame,
  so a wait for time to pass still ends); `NFSMW_REAL_TIMEBASE=1` keeps the host's. Changing that header rebuilds all recompiled sources.
- `NFSMW_INPUT_FRAMES` / `NFSMW_INPUT_FRAMES2`: scripted inputs keyed to world frames, with a `SHOT` step (`hid/input.cpp`).
- `NFSMW_INPUT_CMD=<file>`: step-by-step commands for either player; `NFSMW_FRAME_HASH=1|detail`, `NFSMW_FRAME_HASH_FILE=<file>`: a hash of the
  shader constants of every draw, per frame and per draw (`gpu/renderer.cpp`), to compare two runs.
**Where the first attempts went wrong:** frame-keyed inputs counted frames from launch (the front end also runs the world stepper), so they
fired in menus. The one state value found (`0x82A38D70`, 0 in the front end, 2 from the start of loading) marks the start of loading, not the first
race frame, and loading takes a varying number of frames. No exact race-start anchor yet.
**Where the comparison stands (two identical two-player races, fixed steps, deterministic timebase, async shaders off, player 1's throttle held):**
- Whole-frame hashes never match between runs (0 of about 800 frames), and many constants change every frame even while the game is paused.
- Per-draw hashes align: from about race frame 170 on, frame i of one run lines up with frame i+offset of the other for 200+ frames (offsets 119 and
  -41 in the two pairs compared), and about 13% of a frame's draws match exactly, steady through the countdown and after.
- Per-slot digests (`NFSMW_FRAME_GROUPS_FILE`, 3200 (stage, slot, shader) groups compared per pair of aligned frames): 61% of the groups differ.
  The vertex shader slots c0-c3 (the transform) differ in 120 of 123 shaders, and no shift between -120 and +120 frames makes them agree
  (best 2.8%). So the camera and the transformed objects are not the same in the two runs at any alignment.
- **Cause not known.** The harness itself is a suspect: the throttle was pressed by wall-clock command during loading, so it was held for a
  different number of frames before the start in each run (the engine may ramp with it), and the alignment of static draws cannot pin a frame
  exactly. Real divergence of the world (random seeds, thread timing, streaming) is equally possible.
**What this means for lockstep:** not yet shown to work, and not shown to be impossible. It needs an exact race-start anchor in the game's own state
(the first frame of the race world), so that inputs can be keyed to the race's clock and not the wall's; with that, repeat the comparison. If the
runs still differ with identical inputs on the same frames, the game has sources of nondeterminism beyond the clock that would each have to be found.
**Next:** find the race's own clock or start flag in the game's state (the heap structures reached from a global pointer), key the inputs to it, and
repeat the two-run comparison. If lockstep proves out of reach, other designs: ghost racing (record and replay a run, no live sync) or a
host-authoritative design where one machine runs the race and the other only draws.

## Design (with a server): native, reusing the game's own two-player mode
Rule for this work: no poking at cars in memory. Every car is a real game vehicle created and simulated by the game's own code, and every change goes
through a path the game already has (its input, its split-screen setup, its reset and teleport routines).
The game already has everything a second car needs: the split-screen mode makes player 2 a full native car whose controller input comes from XInput
controller 1, which `hid/input.cpp` already supplies (Phase 1). So an online race is the game's own two-player race in which controller 1's input
comes from the network:
1. **Input relay (native input path).** Each machine sends its local pad state once per game frame to the server, and the server forwards it. On
   each machine the remote player's input is what the game reads for its second controller. The local pad maps to controller 0 on one machine and
   controller 1 on the other, so both machines have the same grid (the "Player 2 press START" screen picks whichever pad presses Start).
2. **Both cars simulated natively on both machines**, with a few frames of input delay so inputs arrive before they are used.
3. **Resync with the game's own routines.** The two simulations will drift (see Phase 2: not shown deterministic). The server periodically sends one
   machine's car states (position, orientation, velocity) and the other applies them through the same code the game uses to put a car back on the
   road (the respawn/reset routine), at a moment when that is not visible, not by writing the rigid body.
4. **One view.** The game draws both viewports; each machine should draw only its own (skip the other viewport's draws in the renderer: about 1.6x
   GPU time saved), without changing what the game simulates.
5. Server: any small UDP relay (rooms, input forwarding, state sync).
Cost: the second car's physics on every machine; benefit: nothing in the game's rules has to be re-implemented.

### Findings so far on the game's own structures
- The data segment names the classes: `PVehicle`, `AIVehicle`, `AIVehicleHuman`, `AIVehicleRacecar`, `AIVehicleTraffic`, `AIVehicleCopCar`, `RigidBody`,
  `SimpleRigidBody`. In a race the globals hold about 3000 aligned heap pointers; 471 point at objects with a vtable-like first word (373 share
  0x8208A794). The heap moves between launches: objects have to be found by walking pointers.
- Two snapshots of about 52,000 objects reachable from the globals (two levels), taken at the start of a circuit with one opponent and about 10 s
  later, showed world positions of the form (x, y, z) = (377, 4525, 234) in 4x4 matrices with the translation in the fourth row, in arrays with a
  0x40-byte stride (one object holds a history of such poses: spacing about 0x40, positions moving along the road). These are poses of a car or
  the camera; which object is the player's vehicle, and which fields are position, orientation and velocity of its rigid body, is not yet known.
- Next: identify the vehicle object by a second signal (the speedometer value and the race clock), then find the game's own reset/teleport function
  for step 3 (the code behind the "Reset car" button and the respawn after a wreck), and confirm the controller-1 input moves the second car.

## The game already has an online stack: the native path (found 2026-10-16)
Strings in the executable show the game's own networking: `ConnectionCore::ResetSession_HaveMutex` ("removing all players from my session"), the
Xbox Live menu item, and EA web calls (`XeRecoverAccount.ashx`). Its imports are the plain Xbox networking set: `NetDll_XNet*` (startup, XNADDR, key
registration, DNS, QoS) and Winsock (`socket`, `bind`, `sendto`, `recvfrom`, `select`, `WSAWaitForMultipleEvents`, ...), 89 of them stubbed here; it uses
no `XSession*` functions. So the native route is to supply what the game's own online code asks of the runtime, with a server as the network, instead
of adding a second system beside it.
**Probe (`NFSMW_FAKE_LIVE=1`, off by default):** `XamUserCheckPrivilege` grants every privilege (the game asks for 254, multiplayer sessions),
`XamUserGetSigninState` says signed in to Live (2), and when the game asks for privilege 254 (the player opened Xbox Live) the listeners are sent
`XN_SYS_SIGNINCHANGED`, `XN_LIVE_CONNECTIONCHANGED` (0x001510F0) and `XN_LIVE_LINK_STATE_CHANGED`. (Sent at boot instead, the game crashed on a null object:
the notifications must come when it asks.) With `NFSMW_STUB_CONTINUE=1 NFSMW_TRACE=1` the game's own flow then ran:
1. `NetDll_XNetStartup`, `NetDll_WSAStartup` (twice, versions 2.2 and 0.2) from the online module (0x8281xxxx / 0x8282xxxx);
2. `NetDll_XNetGetTitleXnAddr` polled (56 times): it is waiting for the console to have a network address;
3. a networking thread loops on `NetDll_WSAWaitForMultipleEvents` / `NetDll_WSAResetEvent` (40,000 calls: the stubs return at once, so it spins);
4. `XamAlloc` (unimplemented) returns 0, and the game dereferences it: SIGSEGV at guest address 8, `lr=0x828100E4`, about 146 s in.
So the next steps are concrete and visible in the log:
- `XamAlloc`/`XamFree`; `XNetStartup`/`Cleanup`; `WSAStartup`/`Cleanup`; `XNetGetTitleXnAddr` returning a static address with the right flags;
- the Winsock event calls (`WSACreateEvent`, `WSAEventSelect`, `WSAWaitForMultipleEvents`, `WSAResetEvent`, `WSASetEvent`) on host primitives;
- sockets on host UDP/TCP (`socket`, `bind`, `sendto`, `recvfrom`, `select`, `ioctlsocket`, `getsockname`, ...), with the Xbox's secure addresses
  (`XNetXnAddrToInAddr`, `XNetServerToInAddr`, key registration) mapped to ordinary host addresses;
- then whatever the game's lobby needs to find a session: it was a hosted matchmaking, so the server has to provide that, and it is not known yet
  which web calls the game makes (EA's services are gone). That is the unknown to measure next.
The probe, as it stands, only gets the game to the start of its network bring-up.

## Decision: our own small netplay layer instead of the game's online stack
The game's own online code (`ConnectionCore`, Xbox Live, EA's services) was brought up as far as its first network calls (probe above), but it
depends on servers that no longer exist and a lobby layer of session objects I would have to emulate completely. So it is replaced, not repaired:
- `runtime/net/netplay.{h,cpp}`: a UDP client (about 150 lines) that sends the local pad about 30 times a second and receives the other player's.
  `NFSMW_NETPLAY=<host>:<port>/<room>[/<slot>]` turns it on; the slot (0 or 1) says whether this machine's pad is player 1 or 2 of the game's own
  two-player race, so both machines agree.
- `hid/input.cpp`: the remote pad is delivered as XInput controller 1 (or 0, by slot), the same path as a real second pad. The cars are the game's own;
  nothing in the game's memory is written.
- `scripts/netplay/server.py`: the relay (rooms of two, forwards INPUT as RELAY, 15 s timeout). `scripts/netplay/bot.py`: a scripted second player for tests.
**Checked (this machine, localhost, throw-away save):** the game joined a room, a bot joined as the second player; the bot's START was accepted
at "Player 2 press START" (no local controller involved), the bot chose a car, and in the race the bot's throttle moved the bottom car (67 mph) while
the local throttle moved the top car; about 290 inputs arrived in each 10-second window from the bot (30 a second) with no loss counted.
**Not done:** the two machines' simulations will drift (determinism was not shown); input delay and a periodic resync through the game's own reset
routine; hiding the other viewport; a second real machine and the internet (the server must be reachable: a public host or a port-forward);
authentication and cheating (inputs are trusted); more than two players (the game's own mode has two).

## First test between two real machines (2026-10-17, over a ZeroTier link, 42 ms)
Both machines joined the relay (which ran on the x86 machine) and each saw "2 players"; inputs arrived at about 520 a second from the x86 machine (its client
sent every loop iteration: now limited to 60 a second, and at once on a change). The x86 machine's START reached "Player 2 press START" here and the game
went on to player 2's car select. Then my game entered the race while the other machine's game sat on its main menu: the menus had diverged, because button
state shared during menus is lost or counted differently when the two games are at different points (loading, a transition). So the pads are now shared
only after an explicit link (F8 on both machines at "Select player 2"); before it each machine's menus run on its own pad. A bug found on the way: a
command-file line that made no tap (the link toggle) was never cleared and toggled every frame; fixed.
Checked locally with the bot: unlinked, the bot's START is ignored at the title screen and at "Select player 2"; after the link it reaches player 2.
**Still not measured:** whether the two simulations stay together in a race (no race ran on both machines yet).

## Second two-machine test (2026-10-17, same link, pads linked by hand at "Select player 2")
**Worked:** with each machine on its own menus and the pads linked (F8) at "Select player 2", the other machine's START went through, player 2 chose a car and a
transmission on that machine, and both games entered the same race (same Ironwood Estates circuit, 2 laps, minimum traffic) and showed **the same race clock**
(reported by the other player). I held player 1's throttle for 25 s on my side (car reached 84 mph, scraping a barrier at race time 1:12). About 4 minutes
later (clock 4:58 on mine) the other player reported the **same scene** as mine on their screen (a car stopped on grass beside a wooden fence and a tree),
with both cars stationary, after car movements that came from both machines.
**What this does and does not show:** both simulations were still showing the same stationary scene four minutes in. It is one observation, reported from
the other screen in words, with no screenshot or number from it. It does not show that the cars follow the same path during the movement, or after a
long race, a collision or a wrecked car; it is encouraging, not a measurement.
**Unexplained:** while the other player held their throttle (for an unknown time), the car that accelerated on my screen (27, 56, 61, 85 mph) was in the **top**
view (player 1), where I expected the bottom one (player 2). Possible causes: which viewport is which depends on the cars' relative positions, or the other
machine's input arrives as player 1's. Not tested. To settle it, log the remote pad's state changes on arrival (not built), and have the other machine press
throttle at a stated moment.
**Next:** log the remote pad on my side; hold throttle on the second machine at a stated moment and see which car moves; then compare the two screens during a
long race and after a crash; then the resync design if they drift.

## Third two-machine test (2026-10-17): logging confirms the routing; clocks not comparable by eye
With `NFSMW_LOG_NET_PADS=1` the log shows each remote change and what the game is given, to the millisecond: the other machine's START (buttons 0010) arrived
and the game had it on **pad1** (player 2) 2 ms later; its A presses (car and transmission) likewise; pad0 stayed 0. So the other machine's controller is
player 2 on mine, as designed. The earlier surprise (the top car moving) is therefore not explained by routing: the other player's throttle was not
timestamped, so that run cannot be matched to the log.
Race clocks: mine read 49.99 at 16:06:22 and 1:41.77 at 16:07:14 (51.78 game seconds in 51.8 wall seconds: the game runs at real time here). The other player read
about 57 at the first moment (offset about 7 s, partly reading delay) and could not give the second reading. Reading a running clock by eye is not a
usable measurement. The 7 s may just be each machine's own loading time before its race clock starts.
**Needed:** a self-measuring test: each machine logs, with a shared time base, a number that depends on the race state (the race clock or a car's
position), and the logs are compared afterwards. That needs the address of such a value in the game's memory (found by walking objects, as done earlier,
the heap moves between launches) or a value the game exposes through a function. Alternatively each machine could send the other a small state digest
over the relay and log both, so one log holds the comparison.

## Lockstep netplay (built 2026-10-17, after the two-machine tests showed the cars staying together)
The other player reported that, with the pads linked, a car moved on one machine moved the same way on the other and at the same coordinates, until
it hit the first player's car. That is consistent with a deterministic game, and the clean way to use it is lockstep: both machines apply the same
inputs on the same race frames, so a collision comes out the same on both, because each machine has the other player's input for that frame.
- **Race frame:** `game::RaceFrame()` (game/frame_time.cpp): the number of game frames since the world clock first read above 0. The world clock is a float
  in seconds at guest address 0x82A153B8 (found by watching floats that advance one second per second in two snapshots; it equals the HUD race clock plus
  a constant 9.95 s; 0 in the front end; with `NFSMW_FIXED_STEPS=2` it advances 0.03333 s per world-step call). It makes both machines agree on the frame
  number whatever their loading times (race frames were not comparable before: loading takes a different number of frames).
- **Protocol** (net/netplay.cpp, scripts/netplay/server.py): FRAMES packets carry the local pad for the last 8 frames, each tagged with the frame it applies on
  (current frame + 3: an input delay of 3 frames, 100 ms at the game's 30 fps), so a lost packet costs nothing; the server forwards them.
- **Lockstep poll** (hid/input.cpp, `LockstepPads`): in a race, linked, with fixed steps, the game's controller reads for frame g return the local pad of frame
  g-3 and the other player's pad for frame g, waiting up to 2 s for it; the same answer for every read in a frame. Mapped to controller 0/1 by slot, so both
  machines give the game the same two pads in the same order. If the other input never comes (2 s), a message says the games are no longer in step.
- **Needs** `NFSMW_FIXED_STEPS=2` on both machines (fixed 2 world steps a frame: the same simulation steps on both), and the same game build on both.
**Checked here (one machine, the bot as the other player):** lockstep ran through a race (the bot saw the game's frame number advance; the game logged no late input);
the bot's throttle arrived on controller 1 and moved the second car; the race clock advanced normally. **Not checked:** two machines in lockstep, drift over a
long race and a collision (the point of the change), how much the 3-frame delay and a 42 ms link hurt the feel, and what happens when a machine's frame rate
differs (the faster waits for the slower). Both machines must be restarted onto the new build and use `NFSMW_FIXED_STEPS=2`.

## Lobby server (2026-10-17): the server learns about matches
Decision: the F8 link and the menu-by-hand steps exist only because the server knows nothing of a match. So the server grows into a lobby (scripts/netplay/server.py,
protocol in its header):
- **Rooms:** named, optional password, host (slot 0) and one guest (slot 1); `list`, `create`, `join`, `leave`, `ready`, `setup` (host only: mode, track, laps,
  traffic, whatever the client puts in the setup object), `start` (host only, when the guest is ready), `status`. Changing the setup clears the ready flags.
- **Events pushed to members:** `room` on every change, `start` (each player's slot, the setup, one shared random seed), `closed` (the host left).
- **Reliability over UDP:** each request carries an id; the client re-sends until it gets the reply; a repeated id is answered again, not run twice.
- **Game traffic unchanged:** `HELLO` by room name, `INPUT`/`FRAMES` relayed to the other member only. A player silent for 15 s leaves; the host leaving closes the room.
- `scripts/netplay/test_lobby.py` runs the server on a spare port and drives it with scripted clients: 29 checks (create, duplicate name, password, full room,
  ready, host-only setup and start, the idempotent request, start event, relay to the other member only, host leaving, the old HELLO way in), all passing.
**Not built yet (the client and game side):**
1. A lobby screen in the runtime's own UI (it already has an ImGui settings menu): server address, room list, create (name, password, the race setup), join, ready, start.
2. Starting the race without the manual menus and the F8 link: on the `start` event, drive the game's own front end with the agreed setup. That needs the front
   end's own function for "start this race" (the Split Screen flow ends in one), found and called, instead of key presses; and the pad link made automatic at the
   moment it asks for player 2. Not found yet.
3. Rematch and returning to the lobby after the race; a player dropping mid-race; the lockstep test on two machines.

## The lobby in the game (2026-10-17): Settings > Online
Built: a lobby client (`runtime/net/lobby.cpp`, with `net/json_lite.h`, tested in `tests/json_lite_test.cpp` under AddressSanitizer) and an **Online** page in
the settings menu (`ui/online_menu.cpp`).
- **Online Play** (Settings > Online, restart): every game frame runs exactly two world steps and the game's timer follows the frame (what lockstep needs): no
  environment variable any more (`NFSMW_ONLINE_PLAY=1` and `NFSMW_FIXED_STEPS=n` still work for tests). With it on, the page shows the lobby: server address and
  name (saved in `online.txt` in the user folder), Connect; the room list (name, players, password lock, race), Join (password if locked), Refresh; New room
  (name, optional password; the creator is the host); in a room, who is in it and who is ready; the host chooses the race (mode, track, laps, traffic) and
  starts it, the guest says Ready, either can Leave.
- **The start event** gives each player their slot and one shared seed and hands the lobby's connection to the game's input relay
  (`netplay::Attach`: the same socket, so the server knows the sender).
**Checked through the real game (keys scripted, a scripted host as the other member):** connect, the list showing "bot1 (1/2) Circuit, track 3, 2 laps, traffic Minimum",
create a room (the room appeared with the player as host), join the scripted host's room, Ready (the host saw it), the host's start (the game attached as player 2
with the seed and showed the match screen). **Not done:** the guest and the host both starting the game's own race automatically (below), the pad link without F8,
what the game does when a player quits mid-race, a password room tried in the UI, text fields on a controller (they need a keyboard).
**Still open (step 3):** on the start event, the game's own front end has to be driven to the agreed race instead of asking the players to click through
Quick Race > Split Screen by hand and press F8 at "Select player 2".
