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
#include <chrono>
#include <cmath>
#include <deque>
#include <vector>

extern "C" PPC_FUNC(__imp__sub_82528448);

namespace
{
    constexpr uint32_t kBodyListHead = 0x8290BE18;
    constexpr uint32_t kPlayerPosCopy = 0x82A2DBF0;

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
        double lastLog = -10;
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();

        Adapter()
        {
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
}

PPC_FUNC(sub_82528448)
{
    const uint32_t obj = ctx.r3.u32;
    __imp__sub_82528448(ctx, base);

    Adapter& a = A();
    if (!a.ghost)
        return;
    const uint32_t s = PPC_LOAD_U32(obj + 28);
    if (!s)
        return;
    const double now = a.Now();

    // Which body is the local player's: the one at the static position copy.
    const float player[3] = { LoadF(base, kPlayerPosCopy), LoadF(base, kPlayerPosCopy + 4), LoadF(base, kPlayerPosCopy + 8) };
    const float here[3] = { LoadF(base, s + 48), LoadF(base, s + 52), LoadF(base, s + 56) };
    const bool havePlayer = std::fabs(player[0]) + std::fabs(player[1]) > 1.0f;   // the copy is zero before a race
    if (havePlayer && (obj == a.localObj || (!a.localObj && Dist(player, here) < 1.0f)))
    {
        a.localObj = obj;
        a.history.emplace_back(now, ReadPose(base, s));
        while (a.history.size() > 2 && a.history[1].first < now - a.ghostSeconds - 1.0)
            a.history.pop_front();
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
            fprintf(stderr, "\n");
        }
        return;
    }

    // The stand-in: the n-th body of the list (chosen with NFSMW_MP_BODY, see the log above), replaying the local car from `ghostSeconds` ago.
    if (a.ghostBody < 0)
        return;
    if (!a.standInObj)
    {
        std::vector<uint32_t> bodies = Bodies(base);
        if (size_t(a.ghostBody) < bodies.size())
            a.standInObj = bodies[a.ghostBody];
    }
    if (obj != a.standInObj || a.history.empty())
        return;
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
