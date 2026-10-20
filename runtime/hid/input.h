// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Controller input (Phase 6): SDL3 gamepads and the keyboard, presented to
// the game as XInput controller 0 through XamInputGetState.
//
//   - Sticks get a radial deadzone with rescaling (Settings > Input, or
//     NFSMW_DEADZONE; default 0.08 of full deflection), so worn or drifting
//     sticks rest at exactly zero while small movements still ramp up
//     smoothly from the edge of the deadzone. Triggers get their own
//     (NFSMW_TRIGGER_DEADZONE, default 0.02).
//   - Keyboard: arrows = D-pad + left stick, W/S = gas/brake (RT/LT), A/D =
//     steer, Space = A, Backspace = B, Left Shift = X, Left Ctrl = Y,
//     Q/E = LB/RB, Enter = START, Esc = BACK, O/L and K/; = right stick
//     (up/down, left/right), C = right stick click.
//   - NFSMW_INPUT_SCRIPT="12:START,15.5:A,30-90/4:DOWN,100~160:RT": taps
//     (150 ms) at seconds since launch, taps every `step` seconds over a
//     range, or inputs held over a range; RT/LT (triggers) and LL/LR/LU/LD
//     (left stick) besides the buttons. For automated runs; works headless.
//     It feeds the game after the deadzones.
//   - NFSMW_VIRTUAL_PAD="2:LX=0.05,4:RT=0.06": an SDL virtual controller whose
//     axes take those values at those seconds, through the deadzones, and
//     whose rumble is logged with what the game asked for (tests of the
//     Input settings without a person). Buttons too ("5:BACK=1", "5.2:BACK=0",
//     "6:DOWN" for a tap), which reach the menus as a real controller's do.
//     Both count seconds of guest time (cpu/guest_time.h): a suspension
//     doesn't count, so what falls due during one happens after it.
//   - Player 2 (XInput controller 1: the game's two-player split-screen mode): a second SDL gamepad, once player 1 has its own
//     (a lone pad is player 1's; the keyboard is player 1's only). Same deadzones, rumble to its own motors.
//     NFSMW_INPUT_SCRIPT2 takes the NFSMW_INPUT_SCRIPT syntax for player 2, for tests without a second pad;
//     NFSMW_LOG_PLAYER2=1 logs the first time the game asks about controller 1 and what it was told.
//   - NFSMW_INPUT_CMD=<file>: commands for either player, one a line ("1 A", "2 START", "1 RT 5": player, button, seconds held),
//     read and emptied once a frame, so a session can be driven step by step with a look at the screen between steps (the timed
//     scripts drift with the load times).
//   - NFSMW_INPUT_FRAMES / NFSMW_INPUT_FRAMES2: the NFSMW_INPUT_SCRIPT syntax for player 1 / 2, but the times and lengths are
//     world frames (game/frame_time.h: WorldFrameIndex; taps last 4 frames), so the inputs follow the game's own clock instead of
//     the wall's; "N:SHOT" captures the next presented frame (build/ui_shot_<n>.ppm) once the world has run N frames.
//   - NFSMW_INPUT_LOG=1: the left stick and triggers once a second, raw and
//     as the game gets them.
//   - iOS: with no controller (and no keyboard attached) a toast says the
//     game needs one, 5 s after the game starts (NoteGameStarting) and 1 s
//     after the last one goes; one connecting takes it down.
#pragma once
#include <SDL3/SDL_events.h>

namespace hid
{
    // The accelerator and brake triggers (0..255) as the game last read them (after the deadzones), from any
    // thread: the motion blur follows them (gpu/renderer.cpp).
    uint8_t GameRightTrigger();
    uint8_t GameLeftTrigger();
    // The buttons of controller 0 (XINPUT_GAMEPAD_* bits) as the game last read them, from any thread: the multiplayer start barrier is left with B.
    uint16_t GameButtons();
    // B or Back on the pad, Backspace or Esc on the keyboard, read from the devices (any thread), for a wait that must be left while the game is not polling its input.
    bool CancelHeld();

    // Main thread, after SDL video is up.
    void Initialize();
    // Main thread, as the game itself starts (after the installer, if it
    // ran): on iOS, the "needs a controller" notice is due 5 s later if
    // there is none by then. Elsewhere nothing.
    void NoteGameStarting();
    void HandleEvent(const SDL_Event& event);
    // Main thread, once per presented frame: sample devices.
    void Update();
    // While set (a menu is open), the game sees an idle controller, and
    // after it is cleared until every control has been released.
    void SetBlocked(bool blocked);
    // At exit: stop any rumble (the process may outlive nothing that would).
    void Shutdown();
    // Main thread, from the frame loop (not UIKit's callback), once the game
    // is suspended (iOS: the app isn't active) and once it's back: Suspend
    // stops any rumble, Resume sends the game's last request again.
    void Suspend();
    void Resume();
}
