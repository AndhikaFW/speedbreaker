// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Online play: the other player's controller as the game's second controller.
//
// The game's online side (ConnectionCore, Xbox Live, EA's services) is not used: its servers are gone and its lobby layer is a tangle of
// session objects. This is a small replacement. The game's own two-player mode already makes player 2 a full car driven by XInput controller 1
// (hid/input.cpp), so an online race is that race with controller 1's input coming from the network. Each machine sends its pad to a relay
// server (scripts/netplay/server.py) about 30 times a second and reads the other player's.
//
// Wire format (UDP, little-endian): 'N','F','S','1', type, then
//   type 1 HELLO   room name, 16 bytes, zero padded           (client -> server, once a second until WELCOME)
//   type 2 INPUT   u32 sequence, pad state (12 bytes)          (client -> server, every frame)
//   type 3 WELCOME u8 players in the room                      (server -> client, answers each HELLO)
//   type 4 RELAY   u8 sender slot, u32 sequence, pad state     (server -> client: the other player's INPUT)
// The server tells the two machines apart by address. Which game controller a machine's pad is (player 1 or 2) is set on each machine by the third
// part of NFSMW_NETPLAY (the sender slot in RELAY is the order of arrival, for information only).
#include <stdafx.h>

#include "netplay.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

namespace
{
    constexpr uint8_t kMagic[4] = { 'N', 'F', 'S', '1' };
    enum : uint8_t { kHello = 1, kInput = 2, kWelcome = 3, kRelay = 4 };
    constexpr size_t kPadBytes = 12;

    struct Config
    {
        bool enabled = false;
        std::string host, room;
        uint16_t port = 0;
        int slot = 0;
    };

    Config ReadConfig()
    {
        Config c;
        const char* v = std::getenv("NFSMW_NETPLAY");
        if (!v || !*v)
            return c;
        std::string s = v;
        const size_t colon = s.find(':'), slash = s.find('/');
        if (colon == std::string::npos || slash == std::string::npos || slash < colon)
        {
            fprintf(stderr, "[netplay] NFSMW_NETPLAY must look like <host>:<port>/<room>[/<slot>], got \"%s\"\n", v);
            return c;
        }
        c.host = s.substr(0, colon);
        c.port = uint16_t(std::atoi(s.substr(colon + 1, slash - colon - 1).c_str()));
        std::string rest = s.substr(slash + 1);
        if (const size_t slash2 = rest.find('/'); slash2 != std::string::npos)
        {
            c.slot = std::atoi(rest.c_str() + slash2 + 1) != 0 ? 1 : 0;
            rest.resize(slash2);
        }
        c.room = rest.substr(0, 16);
        c.enabled = !c.host.empty() && c.port && !c.room.empty();
        return c;
    }

    const Config s_config = ReadConfig();

    std::mutex s_mutex;
    netplay::PadState s_remote;
    bool s_haveRemote = false;
    std::atomic<int> s_players{ 0 };
    std::atomic<uint64_t> s_sent{ 0 }, s_received{ 0 };
    std::atomic<uint32_t> s_sequence{ 0 };
    int s_socket = -1;
    sockaddr_in s_server{};

    void Put(uint8_t* out, const netplay::PadState& p)
    {
        memcpy(out, &p.buttons, 2);
        out[2] = p.leftTrigger;
        out[3] = p.rightTrigger;
        memcpy(out + 4, &p.lx, 2);
        memcpy(out + 6, &p.ly, 2);
        memcpy(out + 8, &p.rx, 2);
        memcpy(out + 10, &p.ry, 2);
    }

    netplay::PadState Get(const uint8_t* in)
    {
        netplay::PadState p;
        memcpy(&p.buttons, in, 2);
        p.leftTrigger = in[2];
        p.rightTrigger = in[3];
        memcpy(&p.lx, in + 4, 2);
        memcpy(&p.ly, in + 6, 2);
        memcpy(&p.rx, in + 8, 2);
        memcpy(&p.ry, in + 10, 2);
        return p;
    }

    void SendHello()
    {
        uint8_t packet[5 + 16] = {};
        memcpy(packet, kMagic, 4);
        packet[4] = kHello;
        memcpy(packet + 5, s_config.room.data(), s_config.room.size());
        sendto(s_socket, packet, sizeof(packet), 0, reinterpret_cast<const sockaddr*>(&s_server), sizeof(s_server));
    }

    void ReceiveLoop()
    {
        uint8_t packet[256];
        auto lastHello = std::chrono::steady_clock::time_point{};
        while (true)
        {
            // Until the server has answered, say hello once a second; afterwards, once every 5 s (it keeps the room's address fresh).
            const auto now = std::chrono::steady_clock::now();
            if (now - lastHello > std::chrono::seconds(s_players.load() ? 5 : 1))
            {
                SendHello();
                lastHello = now;
            }
            const ssize_t n = recv(s_socket, packet, sizeof(packet), 0);  // 100 ms timeout (SO_RCVTIMEO)
            if (n < 5 || memcmp(packet, kMagic, 4) != 0)
                continue;
            if (packet[4] == kWelcome && n >= 6)
            {
                const int players = packet[5];
                if (s_players.exchange(players) != players)
                    fprintf(stderr, "[netplay] room \"%s\": %d player%s\n", s_config.room.c_str(), players, players == 1 ? "" : "s");
            }
            else if (packet[4] == kRelay && size_t(n) >= 5 + 1 + 4 + kPadBytes)
            {
                std::lock_guard lock(s_mutex);
                const netplay::PadState before = s_remote;
                s_remote = Get(packet + 5 + 1 + 4);
                // NFSMW_LOG_NET_PADS=1: every change of the other player's pad as it arrives, with the time, to line up what one player does with
                // what the other sees.
                static const bool logPads = std::getenv("NFSMW_LOG_NET_PADS") != nullptr;
                if (logPads && memcmp(&before, &s_remote, sizeof(netplay::PadState)) != 0)
                {
                    const auto t = std::chrono::system_clock::now().time_since_epoch();
                    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
                    fprintf(stderr, "[netpad] %lld.%03lld remote: buttons %04X lt %u rt %u lx %d ly %d\n", ms / 1000, ms % 1000, s_remote.buttons,
                        s_remote.leftTrigger, s_remote.rightTrigger, int(s_remote.lx), int(s_remote.ly));
                }
                if (!s_haveRemote)
                    fprintf(stderr, "[netplay] the other player's controller is arriving\n");
                s_haveRemote = true;
                s_received.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    bool Start()
    {
        if (!s_config.enabled)
            return false;
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(s_config.host.c_str(), std::to_string(s_config.port).c_str(), &hints, &res) != 0 || !res)
        {
            fprintf(stderr, "[netplay] can't resolve %s\n", s_config.host.c_str());
            return false;
        }
        s_server = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
        freeaddrinfo(res);
        s_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (s_socket < 0)
            return false;
        timeval timeout{ 0, 100000 };
        setsockopt(s_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        std::thread(ReceiveLoop).detach();
        fprintf(stderr, "[netplay] room \"%s\" on %s:%u, this machine is player %d\n", s_config.room.c_str(), s_config.host.c_str(),
            unsigned(s_config.port), s_config.slot + 1);
        return true;
    }

    bool Running()
    {
        static const bool on = Start();
        return on;
    }
}

namespace netplay
{
    bool Enabled() { return s_config.enabled && Running(); }
    int LocalSlot() { return s_config.slot; }

    void SendLocal(const PadState& pad)
    {
        if (!Enabled())
            return;
        // At most 60 a second (the game polls once a frame, and a machine with no frame limit would send hundreds), and at once when the
        // pad changes, so no press is held back.
        static PadState lastSent;
        static std::chrono::steady_clock::time_point lastTime;
        const auto now = std::chrono::steady_clock::now();
        const bool changed = memcmp(&lastSent, &pad, sizeof(PadState)) != 0;
        if (!changed && now - lastTime < std::chrono::milliseconds(16))
            return;
        lastSent = pad;
        lastTime = now;
        uint8_t packet[5 + 4 + kPadBytes];
        memcpy(packet, kMagic, 4);
        packet[4] = kInput;
        const uint32_t seq = s_sequence.fetch_add(1, std::memory_order_relaxed);
        memcpy(packet + 5, &seq, 4);
        Put(packet + 9, pad);
        sendto(s_socket, packet, sizeof(packet), 0, reinterpret_cast<const sockaddr*>(&s_server), sizeof(s_server));
        s_sent.fetch_add(1, std::memory_order_relaxed);
    }

    bool Remote(PadState& out)
    {
        if (!Enabled())
            return false;
        std::lock_guard lock(s_mutex);
        out = s_remote;
        return s_haveRemote;
    }

    void Tick()
    {
        if (!Enabled())
            return;
        static auto last = std::chrono::steady_clock::now();
        static uint64_t lastSent = 0, lastReceived = 0;
        const auto now = std::chrono::steady_clock::now();
        if (now - last < std::chrono::seconds(10))
            return;
        const uint64_t sent = s_sent.load(), received = s_received.load();
        fprintf(stderr, "[netplay] last 10 s: sent %llu, received %llu inputs; %d player(s) in the room\n", (unsigned long long)(sent - lastSent),
            (unsigned long long)(received - lastReceived), s_players.load());
        lastSent = sent;
        lastReceived = received;
        last = now;
    }
}
