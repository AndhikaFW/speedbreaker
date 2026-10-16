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

## Revised design (with a server): state relay, not lockstep
Each player's machine runs its own normal race. A server relays each player's car state (position, orientation, velocity, wheel and steering state,
plus race progress) to the others, and each machine shows the remote players as extra cars. This needs no determinism: the cars do not have to
evolve identically, only be drawn where the server says. (Lockstep over the two-player mode was tried first; see Phase 2 above.)
What it needs from the game, none of it done yet:
1. **Find the car objects.** Names from the game's own strings (the class names exist in the data segment): `PVehicle`, `AIVehicle`, `AIVehicleHuman`,
   `AIVehicleRacecar`, `AIVehicleTraffic`, `AIVehicleCopCar`, `RigidBody`, `SimpleRigidBody`. In a Quick Play sprint with one opponent, the globals hold
   about 3000 aligned heap pointers, of which 471 point at objects with a vtable-like first word (373 share one vtable, 0x8208A794). The car
   objects have to be found by walking from these, not by fixed addresses (the heap moves between launches; the old speed addresses read garbage).
2. **Read the local player's state** each frame (position, orientation, velocity, steering, throttle) and send it.
3. **Drive a remote car:** make an opponent slot (AIVehicleRacecar) follow received state instead of its AI: write its rigid body's position and
   velocity every frame, or replace its controller's output. The simplest version is a single opponent (as in Quick Play) driven this way.
4. Server (any small UDP service: join a room, relay states at 20-30 Hz, interpolate on the receiver), and race setup (same track, same car list).
Risks: forcing a rigid body's state may fight the game's own physics for that car (teleport it each frame and zero its forces); collisions between
a real player and a remote car then behave oddly; cops and traffic will not match across machines.
First step: log the player's car position and the opponent's every frame in a Quick Play sprint, to find the structures and confirm which
fields hold position, orientation and velocity.
