// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See lobby.cpp.
#pragma once
#include <string>
#include <vector>

#include <net/json_lite.h>

namespace lobby
{
    struct Member
    {
        std::string name;
        int slot = 0;
        bool ready = false;
    };

    struct Room
    {
        std::string name;
        int players = 0, max = 2;
        bool locked = false;
        std::string state;       // "lobby" or "racing"
        json::Value setup;       // what the host chose: mode, track, laps, traffic
        std::vector<Member> members;
    };

    // What the menu draws from: a copy, taken under a lock.
    struct Snapshot
    {
        bool connected = false;   // the server answered
        bool connecting = false;  // a connection was asked for and nothing has answered yet
        std::string server, error;
        std::vector<Room> rooms;  // the list, while not in a room
        bool inRoom = false;
        Room room;                // the room joined, while in one
        int you = 0;              // this player's slot in it (0 = host)
        bool starting = false;    // the host started the race: the game traffic is attached, the match is on
        int seed = 0;
    };

    Snapshot Get();

    // The saved server address and player name (online.txt in the user folder); the menu edits them and Connect saves them.
    std::string SavedServer();
    std::string SavedName();

    // All of these return at once; the worker thread does the talking, and Get() shows the outcome.
    void Connect(const std::string& hostPort, const std::string& playerName);
    void Disconnect();
    void Refresh();
    void Create(const std::string& room, const std::string& password, const json::Value& setup);
    void Join(const std::string& room, const std::string& password);
    void Leave();
    void SetReady(bool ready);
    void SetSetup(const json::Value& setup);
    void Start();
    // The race is over (or abandoned): the game traffic is detached, the room is left as it is.
    void EndMatch();
}
