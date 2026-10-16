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
**Where the comparison stands (two identical two-player races, fixed steps, deterministic timebase, async shaders off, throttle held):**
- Whole-frame hashes never match between runs (0 of about 800 frames), and many constants change every frame even while the game is paused.
- Per-draw hashes do align: from about race frame 170 on, frame i of one run lines up with frame i+119 of the other (the same offset for 200+
  frames), and about 13% of a frame's draws match exactly, steady through the countdown and after. Draw counts are about 2700 per frame.
- So the runs follow the same course frame by frame (same frame structure, constant offset), but 87% of draws differ in some constant. Not yet
  known whether that is (a) a constant that holds absolute game time (the two runs' game frame counts differ by the offset, 119 frames), harmless
  to the simulation, or (b) real divergence of the cars and camera. That is the question to answer next.
**Next:** make the two runs share the same absolute game time at the race start (for example start the deterministic clock from the loading flag
instead of launch), then compare again; or dump the differing constants of an aligned frame pair and see which shader slots they are (time and
animation, or the car and camera matrices).
