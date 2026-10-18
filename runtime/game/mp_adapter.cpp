// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The multiplayer adapter: the only place where the multiplayer layer (net/mp_*) meets the game's own code.
//
// The game keeps every simulated rigid body in one linked list (head 0x8290BE18, links at +16 of each body object) and steps them from sub_825285C8, which
// calls sub_82528448(body, dt) for each. A body object points (+28) to its state: three 16-byte rows of the orientation matrix (x y z 0) at +0, +16, +32,
// the world position (x y z 1.0) at +48, the linear velocity at +64 and (looks like) the angular velocity at +80, big-endian floats, metres and seconds.
// The player's position is also kept in a static copy at 0x82A2DBF0 (docs/MULTIPLAYER.md, "The player's car in memory").
//
// This hook runs the game's own step first, then reads the local player's car (to send) or writes a remote player's state into the body that stands for
// that player. NFSMW_MP_GHOST=<seconds> is the test without a network: the stand-in body replays the local car as it was that many seconds ago.
#include <stdafx.h>

#include <kernel/function.h>
#include "frame_time.h"
#include <net/mp_client.h>
#include <net/mp_math.h>
#include <chrono>
#include <cmath>
#include <array>
#include <deque>
#include <map>
#include <memory>
#include <vector>

extern "C" PPC_FUNC(__imp__sub_82528448);

namespace
{
    constexpr uint32_t kBodyListHead = 0x8290BE18;
    // The game's vehicle table: a fixed vector at 0x82C745A8 ({vtable, data, capacity in bytes, count}, data = 8-byte entries {owner object, 1}), the player first,
    // then the opponents in creation order. A vehicle's body is the body whose +8 points at that owner object.
    constexpr uint32_t kVehicleTable = 0x82C745A8;

    struct Pose
    {
        float m[3][3];     // rows
        float pos[3];
        float vel[3];
        float ang[3];
    };

    float LoadF(const uint8_t* base, uint32_t a)
    {
        uint32_t u = __builtin_bswap32(*(const volatile uint32_t*)(base + a));
        float f;
        memcpy(&f, &u, 4);
        return f;
    }
    void StoreF(uint8_t* base, uint32_t a, float f)
    {
        uint32_t u;
        memcpy(&u, &f, 4);
        *(volatile uint32_t*)(base + a) = __builtin_bswap32(u);
    }

    Pose ReadPose(const uint8_t* base, uint32_t s)
    {
        Pose p;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                p.m[r][c] = LoadF(base, s + 16 * r + 4 * c);
        for (int c = 0; c < 3; ++c)
        {
            p.pos[c] = LoadF(base, s + 48 + 4 * c);
            p.vel[c] = LoadF(base, s + 64 + 4 * c);
            p.ang[c] = LoadF(base, s + 80 + 4 * c);
        }
        return p;
    }
    void WritePose(uint8_t* base, uint32_t s, const Pose& p)
    {
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                StoreF(base, s + 16 * r + 4 * c, p.m[r][c]);
        for (int c = 0; c < 3; ++c)
        {
            StoreF(base, s + 48 + 4 * c, p.pos[c]);
            StoreF(base, s + 64 + 4 * c, p.vel[c]);
            StoreF(base, s + 80 + 4 * c, p.ang[c]);
        }
    }

    // The game's real physics body of a vehicle. A body object (the tracker the list holds) points at its owner (+8); the owner's component (+128) points through a
    // pointer (+48) at the rigid body, an element of an array in static memory: orientation quaternion (x y z w) at +0, position at +0x10, linear velocity at +0x20,
    // angular velocity at +0x30, mass at +0x2C, the orientation matrix (three 16-byte rows) at +0x80. Its axes are not the tracker's: tracker (x, y, z) = (z, -x, y)
    // of the body's. The tracker the list holds is refreshed from this body by the game's step (sub_82528448), and collisions use this body, so the remote players'
    // cars are written here (docs/MULTIPLAYER.md, "The physics body").
    struct Phys
    {
        float q[4], pos[3], vel[3], ang[3];
    };

    uint32_t PhysBody(const uint8_t* base, uint32_t trackerObj)
    {
        const uint32_t owner = PPC_LOAD_U32(trackerObj + 8);
        if (!owner)
            return 0;
        const uint32_t comp = PPC_LOAD_U32(owner + 128);
        if (!comp)
            return 0;
        const uint32_t ptr = PPC_LOAD_U32(comp + 48);
        return ptr ? PPC_LOAD_U32(ptr) : 0;
    }

    Phys ReadPhys(const uint8_t* base, uint32_t b)
    {
        Phys p;
        for (int i = 0; i < 4; ++i) p.q[i] = LoadF(base, b + 4 * i);
        for (int i = 0; i < 3; ++i)
        {
            p.pos[i] = LoadF(base, b + 0x10 + 4 * i);
            p.vel[i] = LoadF(base, b + 0x20 + 4 * i);
            p.ang[i] = LoadF(base, b + 0x30 + 4 * i);
        }
        return p;
    }

    void WritePhys(uint8_t* base, uint32_t b, const Phys& p)
    {
        float m[3][3];
        mp::QuatToMat(p.q, m);
        for (int i = 0; i < 4; ++i) StoreF(base, b + 4 * i, p.q[i]);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                StoreF(base, b + 0x80 + 16 * r + 4 * c, m[r][c]);
        for (int i = 0; i < 3; ++i)
        {
            StoreF(base, b + 0x10 + 4 * i, p.pos[i]);
            StoreF(base, b + 0x20 + 4 * i, p.vel[i]);
            StoreF(base, b + 0x30 + 4 * i, p.ang[i]);
        }
    }

    float Dist(const float* a, const float* b)
    {
        return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
    }

    struct Adapter
    {
        bool ghost = false;
        float ghostSeconds = 5.f;
        float ghostOffset = 0.f;            // NFSMW_MP_OFFSET: metres along the car's first matrix row, to see the stand-in beside the car
        int ghostBody = -1;                 // NFSMW_MP_BODY: the n-th body of the list stands in for the remote car
        uint32_t localObj = 0;
        uint32_t standInObj = 0;
        std::deque<std::pair<double, Pose>> history;
        // Network (NFSMW_MP_SERVER=host[:port], NFSMW_MP_NAME): the local car goes out at 30 Hz, each remote player drives one stand-in body.
        std::unique_ptr<mp::Client> net;
        std::string server, name = "player";
        double lastSend = 0, lastTry = 0;
        std::map<uint8_t, std::array<float, 3>> dbgShift;       // per remote: distance driven since first seen, shift in metres, join-order step (for the log)
        // Per remote car, the life of the grid shift: 0 = rolling into the grid (full shift), 1 = stationary on the grid, fading with the distance driven from
        // there, 2 = faded for good (also for a car first seen already racing).
        struct Grid
        {
            int phase = 0;
            std::array<float, 3> last{}, rest{};
            double stableSince = 0, firstSeen = 0;
        };
        std::map<uint8_t, Grid> grids;
        // Start barrier (NFSMW_MP_PLAYERS=n, n > 1): the race is held, world frozen, from the moment it appears until n-1 other players are there and ready too.
        int expected = 0;
        bool barrier = false;
        double barrierSince = 0;
        double localSince = 0, connectedAt = 0, lastTable = -1;
        std::map<uint8_t, uint32_t> standIns;   // remote player id -> body object
        double lastLog = -10;
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();

        Adapter()
        {
            if (const char* sv = std::getenv("NFSMW_MP_SERVER")) server = sv;
            if (const char* n = std::getenv("NFSMW_MP_NAME")) name = n;
            if (const char* n = std::getenv("NFSMW_MP_PLAYERS")) expected = atoi(n);
            if (const char* g = std::getenv("NFSMW_MP_GHOST")) { ghost = true; ghostSeconds = float(atof(g)); }
            if (const char* b = std::getenv("NFSMW_MP_BODY")) ghostBody = atoi(b);
            if (const char* o = std::getenv("NFSMW_MP_OFFSET")) ghostOffset = float(atof(o));
        }
        double Now() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }
    };

    Adapter& A()
    {
        static Adapter a;
        return a;
    }

    // The bodies of the list in order (links are at +16, objects start 16 before the link; the list is circular through a sentinel at 0x8290BE08).
    std::vector<uint32_t> Bodies(const uint8_t* base)
    {
        std::vector<uint32_t> v;
        const uint32_t sentinel = kBodyListHead - 16;
        uint32_t obj = PPC_LOAD_U32(kBodyListHead) - 16;
        while (obj && obj != sentinel && v.size() < 256)
        {
            v.push_back(obj);
            obj = PPC_LOAD_U32(obj + 16) - 16;
        }
        return v;
    }

    // Bodies of the racing vehicles (player first), 0 where a vehicle's body is not (yet) in the list.
    std::vector<uint32_t> VehicleBodies(const uint8_t* base)
    {
        std::vector<uint32_t> out;
        const uint32_t data = PPC_LOAD_U32(kVehicleTable + 4);
        const uint32_t count = PPC_LOAD_U32(kVehicleTable + 12);
        if (data != kVehicleTable + 16 || count > 16)
            return out;
        const std::vector<uint32_t> bodies = Bodies(base);
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t owner = PPC_LOAD_U32(data + 8 * i);
            uint32_t body = 0;
            for (uint32_t b : bodies)
                if (PPC_LOAD_U32(b + 8) == owner) { body = b; break; }
            out.push_back(body);
        }
        return out;
    }
}

namespace
{
    // If `obj` stands in for a remote player, write that player's state into it. Done before the game's step as well as after it: the game finds contacts
    // from the pose it starts the step with, so the remote car is where its player is when it is hit, and the step's own drift is undone afterwards.
    void ApplyRemote(Adapter& a, uint8_t* base, uint32_t obj)
    {
        if (!a.net || !a.net->Connected())
            return;
        for (const auto& [id, body] : a.standIns)
        {
            if (body != obj)
                continue;
            const uint32_t pb = PhysBody(base, obj);
            mp::CarState c;
            if (!pb || !a.net->RemoteCar(id, a.net->NowMs(), c))
                return;
            Phys out;
            for (int i = 0; i < 4; ++i) out.q[i] = c.rot[i];
            for (int i = 0; i < 3; ++i) { out.pos[i] = c.pos[i]; out.vel[i] = c.vel[i]; out.ang[i] = c.angVel[i]; }
            // Every game puts its own player in grid slot 0, so on the grid all cars would stand in one place. Show each remote car beside ours instead, 3.5 m
            // per step of join order along the car's side axis, and let the shift fade out over the first 20 m that car drives, after which it is where its player is.
            const double now = a.Now();
            float fade = 1.f, travelled = 0.f;
            {
                Adapter::Grid& g = a.grids[id];
                if (g.firstSeen == 0)
                {
                    g.firstSeen = now;
                    g.stableSince = now;
                    g.last = { out.pos[0], out.pos[1], out.pos[2] };
                }
                const float speed = std::sqrt(out.vel[0] * out.vel[0] + out.vel[1] * out.vel[1] + out.vel[2] * out.vel[2]);
                if (g.phase == 0)
                {
                    if (Dist(out.pos, g.last.data()) > 0.05f)
                    {
                        g.last = { out.pos[0], out.pos[1], out.pos[2] };
                        g.stableSince = now;
                    }
                    if (now - g.stableSince > 1.0)
                    {
                        g.phase = 1;
                        g.rest = g.last;
                    }
                    else if (speed > 8.f || now - g.firstSeen > 90.0)
                        g.phase = 2;   // already racing when first seen
                }
                if (g.phase == 1)
                {
                    travelled = Dist(out.pos, g.rest.data());
                    if (travelled >= 20.f)
                        g.phase = 2;
                    else
                        fade = 1.f - travelled / 20.f;
                }
                if (g.phase == 2)
                    fade = 0.f;
            }
            {
                int d = 0;   // join-order rank of that player minus ours
                for (const auto& [rid, r] : a.net->Remotes())
                {
                    if (rid < id) ++d;
                    if (rid < a.net->MyId()) --d;
                }
                if (id > a.net->MyId()) ++d;   // the local player is before them
                float m[3][3];
                mp::QuatToMat(out.q, m);
                const float shift = 3.5f * float(d) * fade;
                a.dbgShift[id] = { travelled, shift, float(d) };
                for (int i = 0; i < 3; ++i)
                    out.pos[i] += m[0][i] * shift;   // row 0 of the body's matrix is its side axis
            }
            WritePhys(base, pb, out);
            return;
        }
    }
}

PPC_FUNC(sub_82528448)
{
    const uint32_t obj = ctx.r3.u32;
    ApplyRemote(A(), base, obj);
    __imp__sub_82528448(ctx, base);

    Adapter& a = A();
    if (!a.ghost && a.server.empty())
        return;
    const uint32_t s = PPC_LOAD_U32(obj + 28);
    if (!s)
        return;
    const double now = a.Now();

    // The local player's body: the first vehicle of the game's vehicle table (the player is always first). Looked up a few times a second, so a new race
    // (new bodies) is noticed, and not by position, because a stand-in can overlap the car.
    if (now - a.lastTable > 0.25)
    {
        a.lastTable = now;
        const std::vector<uint32_t> vb = VehicleBodies(base);
        const uint32_t first = vb.empty() ? 0 : vb[0];
        if (first && first != a.localObj)
        {
            // A new race builds new bodies: forget the old ones (the stand-ins are looked up again) and say so.
            fprintf(stderr, "[mp] %.2f s local car body is now %08X\n", now, first);
            a.localObj = first;
            a.standIns.clear();
            a.standInObj = 0;
            a.history.clear();
            a.grids.clear();
            if (a.expected > 1 && !a.server.empty())
            {
                a.barrier = true;
                a.barrierSince = now;
                game::SetWorldHold(true);
                fprintf(stderr, "[mp] start barrier: race held until %d players are ready\n", a.expected);
            }
            a.localSince = now;
        }
    }
    const float here[3] = { LoadF(base, s + 48), LoadF(base, s + 52), LoadF(base, s + 56) };
    if (a.localObj && obj == a.localObj)
    {
        const Pose pose = ReadPose(base, s);
        if (a.ghost)
        {
            a.history.emplace_back(now, pose);
            while (a.history.size() > 2 && a.history[1].first < now - a.ghostSeconds - 1.0)
                a.history.pop_front();
        }
        if (!a.server.empty())
        {
            if (!a.net)
            {
                a.net.reset(new mp::Client);
                std::string host = a.server;
                uint16_t port = mp::kDefaultPort;
                const size_t colon = host.rfind(':');
                if (colon != std::string::npos) { port = uint16_t(atoi(host.c_str() + colon + 1)); host.resize(colon); }
                if (!a.net->Connect(host, port, a.name))
                    fprintf(stderr, "[mp] cannot connect to %s\n", a.server.c_str());
                else
                    fprintf(stderr, "[mp] connecting to %s:%u as %s\n", host.c_str(), port, a.name.c_str());
            }
            a.net->Service();
            if (a.net->Connected() && a.connectedAt == 0)
                a.connectedAt = now;
            if (!a.net->Connected())
                a.connectedAt = 0;
            if (a.net->Failed() && now - a.lastTry > 5.0)
            {
                // The server went away or refused us: try again every few seconds (the stand-ins are looked up again once it is back).
                a.lastTry = now;
                a.standIns.clear();
                std::string host = a.server;
                uint16_t port = mp::kDefaultPort;
                const size_t colon = host.rfind(':');
                if (colon != std::string::npos) { port = uint16_t(atoi(host.c_str() + colon + 1)); host.resize(colon); }
                a.net->Disconnect();
                a.net->Connect(host, port, a.name);
                fprintf(stderr, "[mp] connection lost, reconnecting to %s\n", a.server.c_str());
            }
            if (a.net->Connected() && now - a.lastSend >= 1.0 / 30.0)
            {
                a.lastSend = now;
                if (const uint32_t pb = PhysBody(base, obj))
                {
                    const Phys ph = ReadPhys(base, pb);
                    mp::CarState c;
                    for (int i = 0; i < 4; ++i) c.rot[i] = ph.q[i];
                    for (int i = 0; i < 3; ++i) { c.pos[i] = ph.pos[i]; c.vel[i] = ph.vel[i]; c.angVel[i] = ph.ang[i]; }
                    a.net->SendCar(c);
                }
            }
        }
        if (now - a.lastLog > 3.0)
        {
            a.lastLog = now;
            std::vector<uint32_t> bodies = Bodies(base);
            fprintf(stderr, "[mp] local car body %08X at (%.1f %.1f %.1f); %zu bodies:", obj, here[0], here[1], here[2], bodies.size());
            for (size_t i = 0; i < bodies.size(); ++i)
            {
                uint32_t bs = PPC_LOAD_U32(bodies[i] + 28);
                fprintf(stderr, " #%zu %08X(%.0f,%.0f)", i, bodies[i], bs ? LoadF(base, bs + 48) : 0.f, bs ? LoadF(base, bs + 52) : 0.f);
            }
            for (const auto& [id, v] : a.dbgShift)
                fprintf(stderr, " | remote %u: driven %.0f m since first seen, grid shift %.2f m (step %.0f)", id, v[0], v[1], v[2]);
            if (a.net)
                fprintf(stderr, " | net %s, %zu remote, rtt %u ms", a.net->Connected() ? "up" : a.net->Failed() ? "FAILED" : "connecting", a.net->Remotes().size(), a.net->RttMs());
            fprintf(stderr, "\n");
        }
        return;
    }

    // Ghost test: the n-th body of the list (NFSMW_MP_BODY, see the log above) replays the local car from `ghostSeconds` ago.
    if (a.ghost && a.ghostBody >= 0)
    {
        if (!a.standInObj)
        {
            std::vector<uint32_t> bodies = Bodies(base);
            if (size_t(a.ghostBody) < bodies.size())
                a.standInObj = bodies[a.ghostBody];
        }
        if (obj == a.standInObj && !a.history.empty())
        {
            const double want = now - a.ghostSeconds;
            const Pose* pick = &a.history.front().second;
            for (const auto& h : a.history)
                if (h.first <= want)
                    pick = &h.second;
            Pose out = *pick;
            for (int c = 0; c < 3; ++c)
                out.pos[c] += out.m[0][c] * a.ghostOffset;
            WritePose(base, s, out);
        }
    }

    // Network: each remote player drives one opponent vehicle's body, in the order of the game's vehicle table (player first). NFSMW_MP_BODIES=i,j,... picks list
    // positions by hand instead (the log above shows the list).
    if (a.net && a.net->Connected() && a.localObj)
    {
        if (a.standIns.size() < a.net->Remotes().size())
        {
            static const std::vector<int> picks = [] {
                std::vector<int> v;
                if (const char* e = std::getenv("NFSMW_MP_BODIES"))
                    for (const char* c = e; *c;) { v.push_back(atoi(c)); while (*c && *c != ',') ++c; if (*c) ++c; }
                return v;
            }();
            std::vector<uint32_t> candidates;
            if (picks.empty())
            {
                for (uint32_t b : VehicleBodies(base))
                    if (b && b != a.localObj)
                        candidates.push_back(b);
            }
            else
            {
                const std::vector<uint32_t> bodies = Bodies(base);
                for (int p : picks)
                    if (size_t(p) < bodies.size())
                        candidates.push_back(bodies[p]);
            }
            size_t k = 0;
            for (const auto& [id, r] : a.net->Remotes())
            {
                if (!a.standIns.count(id) && k < candidates.size())
                {
                    a.standIns[id] = candidates[k];
                    fprintf(stderr, "[mp] remote player %u (%s) drives body %08X\n", id, r.name.c_str(), candidates[k]);
                }
                ++k;
            }
        }
        ApplyRemote(a, base, obj);
    }
}

// The start barrier, once a game frame (from GameFrameTime, so it runs while the world is held, when no body is stepped): keep the network going, tell the others
// this game is ready (the flag in the car state), and let the world go when the expected players are ready, or after a timeout.
namespace game
{
    void MpFramePump()
    {
        Adapter& a = A();
        // NFSMW_MP_WATCH=1: log when the state words found by tools/statdiff.py change (which of them mark the end of loading / the start of the intro).
        static const bool watch = std::getenv("NFSMW_MP_WATCH") != nullptr;
        if (watch)
        {
            static const uint32_t words[] = { 0x82A2C23C, 0x82A2C244, 0x82A3067C, 0x82A2C33C, 0x82A2BF4C, 0x82A2C26C, 0x82A2BB4C, 0x82A2DA3C, 0x82A2CF40, 0x82A2D608, 0x82A2C410 };
            static uint32_t last[sizeof words / sizeof *words];
            static bool first = true;
            const uint8_t* wbase = g_memory.base;
            for (size_t i = 0; i < sizeof words / sizeof *words; ++i)
            {
                const uint32_t v = __builtin_bswap32(*(const volatile uint32_t*)(wbase + words[i]));
                if (first || v != last[i])
                    fprintf(stderr, "[watch] %7.2f s %08X = %u%s\n", a.Now(), words[i], v, first ? " (initial)" : "");
                last[i] = v;
            }
            first = false;
        }
        if (!a.barrier || !a.net)
            return;
        const uint8_t* base = g_memory.base;
        const double now = a.Now();
        a.net->Service();
        if (a.net->Connected() && now - a.lastSend >= 1.0 / 30.0 && a.localObj)
        {
            if (const uint32_t pb = PhysBody(base, a.localObj))
            {
                a.lastSend = now;
                const Phys ph = ReadPhys(base, pb);
                mp::CarState c;
                for (int i = 0; i < 4; ++i) c.rot[i] = ph.q[i];
                for (int i = 0; i < 3; ++i) { c.pos[i] = ph.pos[i]; c.vel[i] = ph.vel[i]; c.angVel[i] = ph.ang[i]; }
                c.flags = mp::kFlagReady;
                a.net->SendCar(c);
            }
        }
        int ready = 0;
        for (const auto& [id, r] : a.net->Remotes())
            if (r.hasCar && (r.latest.flags & mp::kFlagReady))
                ++ready;
        const bool all = a.net->Connected() && ready + 1 >= a.expected;
        const bool timeout = now - a.barrierSince > 120.0 || (a.net->Failed() && now - a.barrierSince > 10.0);
        if (all || timeout)
        {
            a.barrier = false;
            game::SetWorldHold(false);
            fprintf(stderr, "[mp] start barrier released after %.1f s (%s)\n", now - a.barrierSince, all ? "everyone is ready" : "timeout");
        }
    }
}
