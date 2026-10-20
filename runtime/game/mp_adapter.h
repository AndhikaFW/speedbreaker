// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See mp_adapter.cpp.
#pragma once
#include <string>

namespace game
{
    // Once a game frame, from GameFrameTime (game/frame_time.cpp): the multiplayer layer's settings, status and start barrier.
    void MpFramePump();
    // The race clock (seconds) the world should be at now so that the games in a multiplayer race stay together, or < 0 if there is none.
    double MpWantedClock();
    // One line for the settings page: off / waiting for a race / connecting / holding the start / connected with n other players.
    std::string MpStatus();
}
