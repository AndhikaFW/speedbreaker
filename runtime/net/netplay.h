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
    // A line for the log once in a while (rates, who is connected); main thread, once a frame.
    void Tick();
}
