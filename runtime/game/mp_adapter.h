// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See mp_adapter.cpp.
#pragma once
#include <string>

namespace game
{
    // Once a game frame, from GameFrameTime (game/frame_time.cpp): the multiplayer layer's settings, status and start barrier.
    void MpFramePump();
    // One line for the settings page: off / waiting for a race / connecting / holding the start / connected with n other players.
    std::string MpStatus();
}
