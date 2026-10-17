// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The lobby client: talks to scripts/netplay/server.py (its header describes the protocol) over one UDP socket, from a worker thread, and keeps a
// snapshot of what the menu shows (ui/online_menu.cpp). The socket is the one the server knows this player by, so when the host starts a race it is
// handed to net/netplay.cpp, which sends the game's inputs through it: the server forwards them to the other member of the room.
//
// Requests carry an id and are re-sent every 500 ms until answered (up to 6 times); the server answers a repeated id again without running it twice.
// While no room is joined the room list is asked for every 2 s, and in a room the status every 3 s: they keep the player alive on the server (15 s
// silence ends membership) and put right any event that was lost.
#include <stdafx.h>

#include "lobby.h"
#include "netplay.h"

#include <user/paths.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>

namespace
{
    constexpr uint8_t kMagic[4] = { 'N', 'F', 'S', '1' };
    enum : uint8_t { kCtrl = 7, kReply = 8, kEvent = 9 };

    using Clock = std::chrono::steady_clock;

    struct Pending
    {
        int id;
        std::string cmd;
        std::string payload;  // the JSON, ready to send
        Clock::time_point lastSent;
        int sends = 0;
    };

    std::mutex s_mutex;
    lobby::Snapshot s_state;                // under s_mutex
    std::deque<json::Value> s_queue;        // requests from the menu, under s_mutex
    std::vector<Pending> s_pending;         // worker thread only
    std::thread s_worker;
    std::atomic<bool> s_run{ false };
    std::atomic<int> s_generation{ 0 };
    int s_socket = -1;
    sockaddr_in s_server{};
    int s_nextId = 1;
    std::string s_playerName;

    std::filesystem::path PrefsPath() { return GetUserPath() / "online.txt"; }

    std::string ReadPref(const std::string& key, const std::string& fallback)
    {
        std::ifstream in(PrefsPath());
        std::string line;
        while (std::getline(in, line))
            if (line.rfind(key + "=", 0) == 0)
                return line.substr(key.size() + 1);
        return fallback;
    }

    void SavePrefs(const std::string& server, const std::string& name)
    {
        std::error_code ec;
        std::filesystem::create_directories(PrefsPath().parent_path(), ec);
        std::ofstream out(PrefsPath(), std::ios::trunc);
        out << "server=" << server << "\nname=" << name << "\n";
    }

    lobby::Room ParseRoom(const json::Value& v)
    {
        lobby::Room r;
        r.name = v.Str("name");
        r.players = int(v.Num("players"));
        r.max = int(v.Num("max", 2));
        r.locked = v.Bool("locked");
        r.state = v.Str("state", "lobby");
        if (const json::Value* setup = v.Find("setup"))
            r.setup = *setup;
        if (const json::Value* members = v.Find("members"); members && members->IsArray())
            for (const json::Value& m : members->a)
                r.members.push_back({ m.Str("name"), int(m.Num("slot")), m.Bool("ready") });
        return r;
    }

    void SendRaw(const std::string& payload)
    {
        std::string packet(reinterpret_cast<const char*>(kMagic), 4);
        packet += char(kCtrl);
        packet += payload;
        sendto(s_socket, packet.data(), packet.size(), 0, reinterpret_cast<const sockaddr*>(&s_server), sizeof(s_server));
    }

    void SetError(const std::string& text)
    {
        std::lock_guard lock(s_mutex);
        s_state.error = text;
    }

    // An answer to a request.
    void OnReply(const json::Value& r)
    {
        const int id = int(r.Num("id", -1));
        std::string cmd;
        for (auto it = s_pending.begin(); it != s_pending.end(); ++it)
            if (it->id == id)
            {
                cmd = it->cmd;
                s_pending.erase(it);
                break;
            }
        std::lock_guard lock(s_mutex);
        s_state.connected = true;
        s_state.connecting = false;
        if (!r.Bool("ok"))
        {
            s_state.error = r.Str("error", "refused");
            return;
        }
        if (cmd == "list")
        {
            s_state.rooms.clear();
            if (const json::Value* rooms = r.Find("rooms"); rooms && rooms->IsArray())
                for (const json::Value& room : rooms->a)
                    s_state.rooms.push_back(ParseRoom(room));
            s_state.error.clear();
        }
        else if (cmd == "create" || cmd == "join" || cmd == "status")
        {
            s_state.inRoom = true;
            s_state.room = ParseRoom(r);
            s_state.you = int(r.Num("you"));
            s_state.error.clear();
        }
        else if (cmd == "leave")
        {
            s_state.inRoom = false;
            s_state.starting = false;
            s_state.room = lobby::Room();
        }
    }

    // Something the server says unasked.
    void OnEvent(const json::Value& e)
    {
        const std::string kind = e.Str("event");
        if (kind == "room")
        {
            std::lock_guard lock(s_mutex);
            s_state.inRoom = true;
            s_state.room = ParseRoom(e);
            s_state.you = int(e.Num("you"));
        }
        else if (kind == "start")
        {
            const int slot = int(e.Num("slot"));
            {
                std::lock_guard lock(s_mutex);
                s_state.starting = true;
                s_state.seed = int(e.Num("seed"));
                s_state.you = slot;
                if (const json::Value* setup = e.Find("setup"))
                    s_state.room.setup = *setup;
                s_state.room.state = "racing";
            }
            // From here the game's inputs go through this socket, to the other member.
            netplay::Attach(s_socket, s_server, slot);
            fprintf(stderr, "[lobby] the race is on: this machine is player %d, seed %d\n", slot + 1, int(e.Num("seed")));
        }
        else if (kind == "closed")
        {
            netplay::Detach();
            std::lock_guard lock(s_mutex);
            s_state.inRoom = false;
            s_state.starting = false;
            s_state.room = lobby::Room();
            s_state.error = "The room was closed: " + e.Str("reason", "the host left");
        }
    }

    void Worker(int generation)
    {
        uint8_t packet[1500];
        auto lastPoll = Clock::time_point{};
        while (s_run.load() && s_generation.load() == generation)
        {
            const auto now = Clock::now();
            // Requests from the menu.
            {
                std::deque<json::Value> queue;
                {
                    std::lock_guard lock(s_mutex);
                    queue.swap(s_queue);
                }
                for (json::Value& req : queue)
                {
                    Pending p;
                    p.id = s_nextId++;
                    p.cmd = req.Str("cmd");
                    req["id"] = p.id;
                    p.payload = json::Write(req);
                    p.lastSent = now;
                    p.sends = 1;
                    SendRaw(p.payload);
                    s_pending.push_back(std::move(p));
                }
            }
            // Re-send what has no answer; give up after a few tries.
            for (auto it = s_pending.begin(); it != s_pending.end();)
            {
                if (now - it->lastSent > std::chrono::milliseconds(500))
                {
                    if (it->sends >= 6)
                    {
                        SetError("The server does not answer.");
                        {
                            std::lock_guard lock(s_mutex);
                            s_state.connecting = false;
                            s_state.connected = false;
                        }
                        it = s_pending.erase(it);
                        continue;
                    }
                    SendRaw(it->payload);
                    it->lastSent = now;
                    it->sends++;
                }
                ++it;
            }
            // The poll: the room list, or the room's status.
            bool inRoom;
            {
                std::lock_guard lock(s_mutex);
                inRoom = s_state.inRoom;
            }
            if (now - lastPoll > std::chrono::seconds(inRoom ? 3 : 2))
            {
                lastPoll = now;
                json::Value req = json::Value::Object();
                req["cmd"] = inRoom ? "status" : "list";
                Pending p;
                p.id = s_nextId++;
                p.cmd = req.Str("cmd");
                req["id"] = p.id;
                p.payload = json::Write(req);
                p.lastSent = now;
                p.sends = 1;
                SendRaw(p.payload);
                s_pending.push_back(std::move(p));
            }
            const ssize_t n = recv(s_socket, packet, sizeof(packet), 0);  // 100 ms timeout
            if (n < 5 || memcmp(packet, kMagic, 4) != 0)
                continue;
            if (packet[4] == kReply || packet[4] == kEvent)
            {
                json::Value v;
                if (!json::Parse(reinterpret_cast<const char*>(packet) + 5, size_t(n) - 5, v))
                    continue;
                if (packet[4] == kReply)
                    OnReply(v);
                else
                    OnEvent(v);
            }
            else
                netplay::HandlePacket(packet, size_t(n));  // the game's inputs
        }
    }

    void Push(json::Value req)
    {
        std::lock_guard lock(s_mutex);
        if (s_queue.size() < 32)
            s_queue.push_back(std::move(req));
    }
}

namespace lobby
{
    Snapshot Get()
    {
        std::lock_guard lock(s_mutex);
        return s_state;
    }

    std::string SavedServer() { return ReadPref("server", "127.0.0.1:7777"); }
    std::string SavedName() { return ReadPref("name", "Player"); }

    void Connect(const std::string& hostPort, const std::string& playerName)
    {
        Disconnect();
        const size_t colon = hostPort.rfind(':');
        const std::string host = colon == std::string::npos ? hostPort : hostPort.substr(0, colon);
        const std::string port = colon == std::string::npos ? "7777" : hostPort.substr(colon + 1);
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (host.empty() || getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res)
        {
            SetError("Can't find the server \"" + hostPort + "\".");
            return;
        }
        s_server = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
        freeaddrinfo(res);
        s_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (s_socket < 0)
        {
            SetError("Can't open a network socket.");
            return;
        }
        timeval timeout{ 0, 100000 };
        setsockopt(s_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        s_playerName = playerName.empty() ? "Player" : playerName.substr(0, 16);
        SavePrefs(hostPort, s_playerName);
        {
            std::lock_guard lock(s_mutex);
            s_state = Snapshot();
            s_state.server = hostPort;
            s_state.connecting = true;
            s_queue.clear();
        }
        s_pending.clear();
        s_run = true;
        const int generation = ++s_generation;
        s_worker = std::thread(Worker, generation);
        fprintf(stderr, "[lobby] connecting to %s as %s\n", hostPort.c_str(), s_playerName.c_str());
    }

    void Disconnect()
    {
        if (!s_run.exchange(false))
            return;
        s_generation++;
        if (s_worker.joinable())
            s_worker.join();
        netplay::Detach();
        close(s_socket);
        s_socket = -1;
        std::lock_guard lock(s_mutex);
        s_state = Snapshot();
    }

    void Refresh()
    {
        json::Value r = json::Value::Object();
        r["cmd"] = "list";
        Push(std::move(r));
    }

    void Create(const std::string& room, const std::string& password, const json::Value& setup)
    {
        json::Value r = json::Value::Object();
        r["cmd"] = "create";
        r["room"] = room;
        r["password"] = password;
        r["name"] = s_playerName;
        r["setup"] = setup;
        Push(std::move(r));
    }

    void Join(const std::string& room, const std::string& password)
    {
        json::Value r = json::Value::Object();
        r["cmd"] = "join";
        r["room"] = room;
        r["password"] = password;
        r["name"] = s_playerName;
        Push(std::move(r));
    }

    void Leave()
    {
        json::Value r = json::Value::Object();
        r["cmd"] = "leave";
        Push(std::move(r));
    }

    void SetReady(bool ready)
    {
        json::Value r = json::Value::Object();
        r["cmd"] = "ready";
        r["ready"] = ready;
        Push(std::move(r));
    }

    void SetSetup(const json::Value& setup)
    {
        json::Value r = json::Value::Object();
        r["cmd"] = "setup";
        r["setup"] = setup;
        Push(std::move(r));
    }

    void Start()
    {
        json::Value r = json::Value::Object();
        r["cmd"] = "start";
        Push(std::move(r));
    }

    void EndMatch()
    {
        netplay::Detach();
        std::lock_guard lock(s_mutex);
        s_state.starting = false;
    }
}
