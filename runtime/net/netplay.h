// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See netplay.cpp.
#pragma once
#include <cstdint>

namespace netplay
{
    // One controller's state, as the game reads it.
    struct PadState
    {
        uint16_t buttons = 0;
        uint8_t leftTrigger = 0, rightTrigger = 0;
        int16_t lx = 0, ly = 0, rx = 0, ry = 0;
    };

    // NFSMW_NETPLAY=<host>:<port>/<room>[/<slot>] turns online play on: this machine joins <room> on the relay server and the other player's controller
    // arrives as the game's second controller. <slot> (0 or 1, default 0) says which controller the local pad is: 0 = player 1, 1 = player 2, so
    // the two machines agree who is on which side of the game's own two-player race.
    bool Enabled();
    // Local slot (0 or 1).
    int LocalSlot();
    // Main thread, once a frame: the local pad, to be sent (the game's player at LocalSlot).
    void SendLocal(const PadState& pad);
    // The latest state of the other player's pad; false until the other player has joined and sent one.
    bool Remote(PadState& out);
    // Lockstep, for a race (game::RaceFrame() >= 0): called when the game polls the controllers in race frame `frame`, with the local pad as it is
    // now. The first call for a frame records it, sends it to the other machine tagged to apply `kInputDelay` frames later (with the last few
    // frames' inputs again, so a lost packet costs nothing), and waits (up to 2 s) for the other machine's input for this frame. Later calls for the
    // same frame return the same answer. `local` and `remote` are what the game gets for this frame: the local pad as it was kInputDelay frames
    // ago, and the other player's pad as they sent it for this frame; both neutral for the first kInputDelay frames of a race.
    constexpr int kInputDelay = 3;
    void LockstepPoll(int64_t frame, const PadState& nowLocal, PadState& local, PadState& remote);
    // A line for the log once in a while (rates, who is connected); main thread, once a frame.
    void Tick();
}
