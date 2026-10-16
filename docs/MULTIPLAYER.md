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
**Not verified:** player 2's throttle moving car 2 (one screenshot was inconclusive); a real second pad.
**Found: split-screen is slow here.** In the split view the GPU time was 120-170 ms a frame (5.8-8.3 fps) at 1.5x with shadow size 800;
frames with a single view in the same race ran at 27 fps (GPU 36 ms). So the local mode costs about 4x. For online play this suggests
each machine should draw only its own view (the renderer or the game's viewport setup would need changing), otherwise it is unplayable.
Next: confirm player 2's control, find where the game draws the two viewports and why the split view costs so much (two scenes plus
two reflection and shadow passes would be about 2x, not 4x), then the determinism check (phase 2).
