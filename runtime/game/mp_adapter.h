// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See mp_adapter.cpp.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace game
{
    // Once a game frame, from GameFrameTime (game/frame_time.cpp): the multiplayer layer's settings, status and start barrier.
    void MpFramePump();
    // The race clock (seconds) the world should be at now so that the games in a multiplayer race stay together, or < 0 if there is none.
    double MpWantedClock();
    // The settings page: the car this player chose in the lobby (its type key, from game::CarCatalog) and whether it is ready for the next race. Any thread.
    void MpSetLobby(uint64_t carKey, bool ready);
    bool MpLobbyReady();
    // From game/car_select.cpp, on the guest thread: the player's vehicle was just created; the race script is about to create its opponents.
    void MpPlayerCreated(uint8_t* base);
    void MpBeforeOpponents(uint8_t* base);
    // One line for the settings page: off / waiting for a race / connecting / holding the start / connected with n other players.
    std::string MpStatus();

    // The players on the server, for the settings page: this player first.
    struct MpPlayer
    {
        std::string name;
        std::string car;      // the car it races in (its display name), empty when it is not in a race
        bool inRace = false;
        bool self = false;
    };
    std::vector<MpPlayer> MpPlayers();
}
