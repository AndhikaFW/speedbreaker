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
#include <net/mp_client.h>
#include <chrono>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <vector>

extern "C" PPC_FUNC(__imp__sub_82528448);

namespace
{
    constexpr uint32_t kBodyListHead = 0x8290BE18;
    constexpr uint32_t kPlayerPosCopy = 0x82A2DBF0;
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

    // Matrix (rows as the game stores them) <-> quaternion (x y z w). Both directions use the same convention, so a round trip is the identity.
    void ToQuat(const float m[3][3], float q[4])
    {
        const float t = m[0][0] + m[1][1] + m[2][2];
        if (t > 0)
        {
            const float r = std::sqrt(1.f + t) * 2;
            q[3] = 0.25f * r; q[0] = (m[2][1] - m[1][2]) / r; q[1] = (m[0][2] - m[2][0]) / r; q[2] = (m[1][0] - m[0][1]) / r;
        }
        else if (m[0][0] > m[1][1] && m[0][0] > m[2][2])
        {
            const float r = std::sqrt(1.f + m[0][0] - m[1][1] - m[2][2]) * 2;
            q[3] = (m[2][1] - m[1][2]) / r; q[0] = 0.25f * r; q[1] = (m[0][1] + m[1][0]) / r; q[2] = (m[0][2] + m[2][0]) / r;
        }
        else if (m[1][1] > m[2][2])
        {
            const float r = std::sqrt(1.f + m[1][1] - m[0][0] - m[2][2]) * 2;
            q[3] = (m[0][2] - m[2][0]) / r; q[0] = (m[0][1] + m[1][0]) / r; q[1] = 0.25f * r; q[2] = (m[1][2] + m[2][1]) / r;
        }
        else
        {
            const float r = std::sqrt(1.f + m[2][2] - m[0][0] - m[1][1]) * 2;
            q[3] = (m[1][0] - m[0][1]) / r; q[0] = (m[0][2] + m[2][0]) / r; q[1] = (m[1][2] + m[2][1]) / r; q[2] = 0.25f * r;
        }
    }
    void FromQuat(const float q[4], float m[3][3])
    {
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        m[0][0] = 1 - 2 * (y * y + z * z); m[0][1] = 2 * (x * y + z * w);     m[0][2] = 2 * (x * z - y * w);
        m[1][0] = 2 * (x * y - z * w);     m[1][1] = 1 - 2 * (x * x + z * z); m[1][2] = 2 * (y * z + x * w);
        m[2][0] = 2 * (x * z + y * w);     m[2][1] = 2 * (y * z - x * w);     m[2][2] = 1 - 2 * (x * x + y * y);
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
        double lastSend = 0;
        std::map<uint8_t, uint32_t> standIns;   // remote player id -> body object
        double lastLog = -10;
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();

        Adapter()
        {
            if (const char* sv = std::getenv("NFSMW_MP_SERVER")) server = sv;
            if (const char* n = std::getenv("NFSMW_MP_NAME")) name = n;
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

PPC_FUNC(sub_82528448)
{
    const uint32_t obj = ctx.r3.u32;
    __imp__sub_82528448(ctx, base);

    Adapter& a = A();
    if (!a.ghost && a.server.empty())
        return;
    const uint32_t s = PPC_LOAD_U32(obj + 28);
    if (!s)
        return;
    const double now = a.Now();

    // Which body is the local player's: the one at the static position copy.
    const float player[3] = { LoadF(base, kPlayerPosCopy), LoadF(base, kPlayerPosCopy + 4), LoadF(base, kPlayerPosCopy + 8) };
    const float here[3] = { LoadF(base, s + 48), LoadF(base, s + 52), LoadF(base, s + 56) };
    const bool havePlayer = std::fabs(player[0]) + std::fabs(player[1]) > 1.0f;   // the copy is zero before a race
    bool isLocal = havePlayer && obj == a.localObj;
    if (havePlayer && !isLocal && Dist(player, here) < 1.0f)
    {
        // Another body sits at the player's position. Take it over only if the body we had is no longer there (a new race): a stand-in can overlap the car.
        const uint32_t ls = a.localObj ? PPC_LOAD_U32(a.localObj + 28) : 0;
        const float old[3] = { ls ? LoadF(base, ls + 48) : 0.f, ls ? LoadF(base, ls + 52) : 0.f, ls ? LoadF(base, ls + 56) : 0.f };
        isLocal = !ls || Dist(player, old) > 3.0f;
    }
    if (isLocal)
    {
        if (obj != a.localObj)
        {
            // A new race builds new bodies: forget the old ones (the stand-ins are looked up again) and say so.
            fprintf(stderr, "[mp] local car body is now %08X\n", obj);
            a.localObj = obj;
            a.standIns.clear();
            a.standInObj = 0;
            a.history.clear();
        }
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
            if (a.net->Connected() && now - a.lastSend >= 1.0 / 30.0)
            {
                a.lastSend = now;
                mp::CarState c;
                for (int i = 0; i < 3; ++i) { c.pos[i] = pose.pos[i]; c.vel[i] = pose.vel[i]; c.angVel[i] = pose.ang[i]; }
                ToQuat(pose.m, c.rot);
                a.net->SendCar(c);
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
        for (const auto& [id, body] : a.standIns)
        {
            if (body != obj)
                continue;
            mp::CarState c;
            if (!a.net->RemoteCar(id, a.net->NowMs(), c))
                break;
            Pose out;
            FromQuat(c.rot, out.m);
            for (int i = 0; i < 3; ++i) { out.pos[i] = c.pos[i]; out.vel[i] = c.vel[i]; out.ang[i] = c.angVel[i]; }
            WritePose(base, s, out);
            break;
        }
    }
}
