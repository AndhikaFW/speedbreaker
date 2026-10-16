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
**Not checked:** a two-player race itself (menu path to it, the split-screen render under this renderer, player 2's input reaching the second
car). That is the next step. Also not checked: a real second physical pad (none available here, only the script).
