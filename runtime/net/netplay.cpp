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
//   type 5 FRAMES  u8 count, count x (u32 apply frame, pad state)   (client -> server: lockstep inputs, the last few frames each time)
//   type 6 RELAYF  u8 sender slot, then the FRAMES payload      (server -> client)
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
    enum : uint8_t { kHello = 1, kInput = 2, kWelcome = 3, kRelay = 4, kFrames = 5, kRelayFrames = 6 };
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
    std::atomic<bool> s_external{ false };  // the socket is the lobby's (Attach), not our own
    int s_externalSlot = 0;

    // Lockstep buffers, indexed by frame modulo kRing: the other player's pad for frame g (valid when its tag is g), and our own samples.
    constexpr int kRing = 512, kRedundancy = 8;
    struct Slot { int64_t frame = -1; netplay::PadState pad; };
    Slot s_remoteRing[kRing];   // under s_mutex
    Slot s_localRing[kRing];    // main game thread only
    int64_t s_lastPolled = -2;
    netplay::PadState s_appliedLocal, s_appliedRemote;
    std::atomic<uint64_t> s_lockLate{ 0 }, s_lockFrames{ 0 };

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
            netplay::HandlePacket(packet, size_t(n));
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
    void HandlePacket(const uint8_t* packet, size_t n)
    {
        if (n < 5 || memcmp(packet, kMagic, 4) != 0)
            return;
        if (packet[4] == kWelcome && n >= 6)
        {
            const int players = packet[5];
            if (s_players.exchange(players) != players)
                fprintf(stderr, "[netplay] room \"%s\": %d player%s\n", s_config.room.c_str(), players, players == 1 ? "" : "s");
        }
        else if (packet[4] == kRelayFrames && n >= 7)
        {
            const int count = packet[6];
            if (n < 7 + size_t(count) * (4 + kPadBytes))
                return;
            std::lock_guard lock(s_mutex);
            for (int k = 0; k < count; k++)
            {
                const uint8_t* e = packet + 7 + k * (4 + kPadBytes);
                uint32_t g;
                memcpy(&g, e, 4);
                Slot& slot = s_remoteRing[g % kRing];
                slot.pad = Get(e + 4);
                slot.frame = g;
            }
            if (!s_haveRemote)
                fprintf(stderr, "[netplay] the other player's controller is arriving\n");
            s_haveRemote = true;
            s_received.fetch_add(1, std::memory_order_relaxed);
        }
        else if (packet[4] == kRelay && n >= 5 + 1 + 4 + kPadBytes)
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

    void Attach(int socketFd, const sockaddr_in& server, int slot)
    {
        std::lock_guard lock(s_mutex);
        s_socket = socketFd;
        s_server = server;
        s_externalSlot = slot != 0 ? 1 : 0;
        s_external = true;
        s_haveRemote = false;
        for (Slot& r : s_remoteRing)
            r.frame = -1;
        for (Slot& l : s_localRing)
            l.frame = -1;
        s_lastPolled = -2;
        s_remote = PadState{};
        fprintf(stderr, "[netplay] attached to the lobby's connection: this machine is player %d\n", s_externalSlot + 1);
    }

    void Detach()
    {
        std::lock_guard lock(s_mutex);
        if (!s_external)
            return;
        s_external = false;
        s_haveRemote = false;
        fprintf(stderr, "[netplay] detached\n");
    }

    bool Enabled() { return s_external || (s_config.enabled && Running()); }
    int LocalSlot() { return s_external ? s_externalSlot : s_config.slot; }

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

    void LockstepPoll(int64_t frame, const PadState& nowLocal, PadState& local, PadState& remote)
    {
        if (!Enabled())
            return;
        if (frame == s_lastPolled)  // the same frame: the same answer
        {
            local = s_appliedLocal;
            remote = s_appliedRemote;
            return;
        }
        if (frame < s_lastPolled || frame <= 0)  // a new race: nothing from the old one counts
        {
            for (Slot& slot : s_localRing)
                slot.frame = -1;
            std::lock_guard lock(s_mutex);
            for (Slot& slot : s_remoteRing)
                slot.frame = -1;
        }
        s_lastPolled = frame;
        s_lockFrames.fetch_add(1, std::memory_order_relaxed);
        Slot& mine = s_localRing[frame % kRing];
        mine.frame = frame;
        mine.pad = nowLocal;
        // What this machine's pad does at frame g is what it sampled at frame g - kInputDelay. Send the last kRedundancy frames' worth, from frame
        // + kInputDelay down, so one packet carries what the last few lost ones did.
        uint8_t packet[7 + kRedundancy * (4 + kPadBytes)];
        memcpy(packet, kMagic, 4);
        packet[4] = kFrames;
        int count = 0;
        packet[5] = 0;
        uint8_t* out = packet + 7;
        for (int64_t g = frame + kInputDelay; g > frame + kInputDelay - kRedundancy && g >= kInputDelay; g--)
        {
            const Slot& sample = s_localRing[(g - kInputDelay) % kRing];
            if (sample.frame != g - kInputDelay)
                continue;
            const uint32_t g32 = uint32_t(g);
            memcpy(out, &g32, 4);
            Put(out + 4, sample.pad);
            out += 4 + kPadBytes;
            count++;
        }
        packet[6] = uint8_t(count);
        sendto(s_socket, packet, size_t(out - packet), 0, reinterpret_cast<const sockaddr*>(&s_server), sizeof(s_server));
        s_sent.fetch_add(1, std::memory_order_relaxed);
        // What the game gets this frame.
        local = PadState{};
        if (frame >= kInputDelay)
        {
            const Slot& old = s_localRing[(frame - kInputDelay) % kRing];
            if (old.frame == frame - kInputDelay)
                local = old.pad;
        }
        remote = PadState{};
        if (frame >= kInputDelay)
        {
            // Wait for the other machine's input for this frame (it sends it kInputDelay frames earlier, so normally it is here).
            const auto start = std::chrono::steady_clock::now();
            bool have = false;
            while (true)
            {
                {
                    std::lock_guard lock(s_mutex);
                    const Slot& slot = s_remoteRing[frame % kRing];
                    if (slot.frame == frame)
                    {
                        remote = slot.pad;
                        have = true;
                    }
                }
                if (have)
                    break;
                if (std::chrono::steady_clock::now() - start > std::chrono::seconds(2))
                    break;
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
            if (!have)
            {
                s_lockLate.fetch_add(1, std::memory_order_relaxed);
                fprintf(stderr, "[netplay] race frame %lld: the other player's input did not arrive in 2 s: playing on without it (the two games are no longer in step)\n",
                    (long long)frame);
            }
            else if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(60))
                fprintf(stderr, "[netplay] race frame %lld: waited %lld ms for the other player's input\n", (long long)frame,
                    (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
        }
        s_appliedLocal = local;
        s_appliedRemote = remote;
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
