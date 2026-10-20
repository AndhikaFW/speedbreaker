// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// A probe, off unless NFSMW_PROBE_RACE=1: logs each call of the game's "start a race" handlers with the registers and the guest call chain, to find
// what the front end calls to start a race (for starting one from the online lobby, docs/MULTIPLAYER.md).
//
// sub_82365788 is the handler bound to the script name "StartRace" in sub_8235E8A8 (its body prints "Game_StartRace: %s"). The chain is read from the
// guest stack: at a function's entry r1 is its caller's frame; a frame's first word is its caller's frame, and a function saves its link register at
// (its caller's frame) - 8.
#include <stdafx.h>
#include <chrono>

#include <kernel/function.h>
#include <map>

namespace game
{
    thread_local uint64_t g_wantedCarKey = 0;   // 0: leave the car alone (see sub_82147BF8 at the end of this file)
}

extern "C" PPC_FUNC(__imp__sub_82365788);
extern "C" PPC_FUNC(__imp__sub_82144CA0);
extern "C" PPC_FUNC(__imp__sub_82147BF8);
extern "C" PPC_FUNC(__imp__sub_82147D70);
extern "C" PPC_FUNC(__imp__sub_8214BAA8);
extern "C" PPC_FUNC(__imp__sub_823E59A0);
extern "C" PPC_FUNC(__imp__sub_823E6680);
extern "C" PPC_FUNC(__imp__sub_823D3F98);
extern "C" PPC_FUNC(__imp__sub_82333680);
extern "C" PPC_FUNC(__imp__sub_824729E0);
extern "C" PPC_FUNC(__imp__sub_82473100);
extern "C" PPC_FUNC(__imp__sub_82473870);
extern "C" PPC_FUNC(__imp__sub_8236C180);
extern "C" PPC_FUNC(__imp__sub_8233DC58);
extern "C" PPC_FUNC(__imp__sub_8232D7D8);
extern "C" PPC_FUNC(__imp__sub_8235DA08);
extern "C" PPC_FUNC(__imp__sub_823993C0);
extern "C" PPC_FUNC(__imp__sub_8253A3C0);
extern "C" PPC_FUNC(__imp__sub_8253B5C8);
extern "C" PPC_FUNC(__imp__sub_82539830);
extern "C" PPC_FUNC(__imp__sub_82819520);
extern "C" PPC_FUNC(__imp__sub_82825888);
extern "C" PPC_FUNC(__imp__sub_828198C0);
extern "C" PPC_FUNC(__imp__sub_82549558);

namespace
{
    bool Enabled()
    {
        static const bool on = [] { const char* v = std::getenv("NFSMW_PROBE_RACE"); return v && v[0] != '2'; }();
        return on;
    }

    // A guest string at `address`, if it looks like one (printable, 3-48 characters).
    std::string Text(uint8_t* base, uint32_t address)
    {
        if (address < 0x10000 || address > 0xBFFFFFFF)
            return "";
        std::string out;
        for (int i = 0; i < 48; i++)
        {
            const char c = char(PPC_LOAD_U8(address + i));
            if (!c)
                return out.size() >= 3 ? out : "";
            if (c < 32 || c > 126)
                return "";
            out += c;
        }
        return "";
    }

    void LogCall(const char* name, PPCContext& ctx, uint8_t* base)
    {
        const std::string t3 = Text(base, ctx.r3.u32), t4 = Text(base, ctx.r4.u32), t5 = Text(base, ctx.r5.u32);
        fprintf(stderr, "[race] %s called: lr=%08X r3=%08X r4=%08X r5=%08X r6=%08X%s%s%s%s%s%s; chain:", name, uint32_t(ctx.lr), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
            ctx.r6.u32, t3.empty() ? "" : " r3=\"", t3.c_str(), t3.empty() ? "" : "\"", t4.empty() ? "" : " r4=\"", t4.c_str(), t4.empty() ? "" : "\"");
        (void)t5;
        uint32_t frame = ctx.r1.u32;  // the caller's frame
        for (int depth = 0; depth < 14 && frame; depth++)
        {
            const uint32_t older = PPC_LOAD_U32(frame);  // its caller's frame
            if (older <= frame || older - frame > 0x100000)
                break;
            fprintf(stderr, " %08X", PPC_LOAD_U32(older - 8));
            frame = older;
        }
        fprintf(stderr, "\n");
    }
}

#define PROBE(addr)                                  \
    PPC_FUNC(sub_##addr)                             \
    {                                                \
        if (Enabled())                               \
            LogCall("sub_" #addr, ctx, base);        \
        __imp__sub_##addr(ctx, base);                \
    }
PROBE(8236C180)
PROBE(8233DC58)
PROBE(8232D7D8)
PROBE(8235DA08)
// The event dispatcher: every distinct event id (r5) is logged once, with the time, so the events around the countdown can be read off (NFSMW_PROBE_RACE=3).
#include <mutex>
#include <set>
PPC_FUNC(sub_823993C0)
{
    static const bool distinct = std::getenv("NFSMW_PROBE_RACE") && std::getenv("NFSMW_PROBE_RACE")[0] == '3';
    if (distinct)
    {
        static std::mutex m;
        static std::set<uint32_t> seen;
        static const auto t0 = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> g(m);
        if (seen.insert(ctx.r5.u32).second)
            fprintf(stderr, "[event] %8.2f s (epoch %.1f) id %08X r3=%08X r4=%08X r6=%08X\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(), ctx.r5.u32, ctx.r3.u32, ctx.r4.u32, ctx.r6.u32);
    }
    else if (Enabled())
        LogCall("sub_823993C0", ctx, base);
    __imp__sub_823993C0(ctx, base);
}

// Counted, not logged per call: how often each of these runs, printed once a second while NFSMW_PROBE_RACE=2 (the lobby thread's tick and the network module's).
#include <atomic>
#include <chrono>
namespace
{
    std::atomic<uint32_t> g_counts[8];
    void Count(int i, uint32_t address)
    {
        static const bool on = [] { const char* v = std::getenv("NFSMW_PROBE_RACE"); return v && v[0] == '2'; }();
        if (!on)
            return;
        g_counts[i].fetch_add(1, std::memory_order_relaxed);
        static std::atomic<int64_t> last{ 0 };
        const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        int64_t was = last.load();
        if (now - was >= 1000 && last.compare_exchange_strong(was, now))
            fprintf(stderr, "[count] 8253A3C0 %u | 8253B5C8 %u | 82539830 %u | 82819520 %u | 82825888 %u | 82546F80 %u | 825471D8 %u | 828198C0 %u\n",
                g_counts[0].load(), g_counts[1].load(), g_counts[2].load(), g_counts[3].load(), g_counts[4].load(), g_counts[5].load(), g_counts[6].load(), g_counts[7].load());
        (void)address;
    }
}
#define COUNTED(addr, index)                         \
    PPC_FUNC(sub_##addr)                             \
    {                                                \
        Count(index, 0x##addr);                      \
        __imp__sub_##addr(ctx, base);                \
    }
COUNTED(8253A3C0, 0)
COUNTED(8253B5C8, 1)
PPC_FUNC(sub_82539830)
{
    Count(2, 0x82539830);
    __imp__sub_82539830(ctx, base);
    static std::atomic<uint32_t> n{ 0 };
    if (std::getenv("NFSMW_PROBE_RACE") && n.fetch_add(1) % 4000 == 0)
        fprintf(stderr, "[count] sub_82539830 (net module is up?) returned %u\n", ctx.r3.u32);
}
COUNTED(82819520, 3)
COUNTED(82825888, 4)
COUNTED(828198C0, 7)
PPC_FUNC(sub_82549558)
{
    static std::atomic<uint32_t> n{ 0 };
    if (std::getenv("NFSMW_PROBE_RACE") && n.fetch_add(1) % 4000 == 0)
        fprintf(stderr, "[count] sub_82549558 (session tick) call %u, r3=%08X\n", n.load(), ctx.r3.u32);
    __imp__sub_82549558(ctx, base);
}

PPC_FUNC(sub_82365788)
{
    if (Enabled())
        LogCall("sub_82365788 (StartRace)", ctx, base);
    __imp__sub_82365788(ctx, base);
}

// sub_82144CA0(out, text): the game's string-key constructor. sub_82333680 (the script native that creates a racing vehicle) calls it at 0x82333764 with the car's
// preset name (a script parameter); NFSMW_PROBE_RACE=4 logs those names.
PPC_FUNC(sub_82144CA0)
{
    static const bool on = std::getenv("NFSMW_PROBE_RACE") && std::getenv("NFSMW_PROBE_RACE")[0] == '4';
    if (on)
    {
        static std::mutex m;
        static std::set<std::string> seen;
        const std::string t = Text(base, ctx.r4.u32);
        bool upper = t.size() >= 6;
        for (char c : t)
            upper = upper && ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_');
        if (upper)
        {
            std::lock_guard<std::mutex> g(m);
            if (seen.insert(t).second)
                fprintf(stderr, "[key] \"%s\" lr=%08X\n", t.c_str(), uint32_t(ctx.lr));
        }
    }
    __imp__sub_82144CA0(ctx, base);
}

// sub_82333680(script call, car): the script native that creates a racing vehicle (docs/MULTIPLAYER.md); NFSMW_PROBE_RACE=4 dumps its two arguments.
PPC_FUNC(sub_82333680)
{
    static const bool on = std::getenv("NFSMW_PROBE_RACE") && std::getenv("NFSMW_PROBE_RACE")[0] == '4';
    if (on)
    {
        auto words = [&](uint32_t a, int n) {
            std::string out;
            for (int i = 0; i < n; ++i)
            {
                char b[16];
                snprintf(b, sizeof b, " %08X", PPC_LOAD_U32(a + 4 * i));
                out += b;
            }
            return out;
        };
        fprintf(stderr, "[create] sub_82333680 r3=%08X:%s | r4=%08X:%s\n", ctx.r3.u32, words(ctx.r3.u32, 12).c_str(), ctx.r4.u32, ctx.r4.u32 > 0x10000 ? words(ctx.r4.u32, 8).c_str() : "");
    }
    // NFSMW_TEST_HASHNAMES=a,b,c: the game's own string-key function (sub_82144CA0) on each name, printed next to the keys the opponents are made with.
    if (const char* names = std::getenv("NFSMW_TEST_HASHNAMES"))
    {
        static bool done = false;
        if (!done)
        {
            done = true;
            std::string list = names;
            size_t pos = 0;
            while (pos <= list.size())
            {
                size_t comma = list.find(',', pos);
                std::string name = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                pos = comma == std::string::npos ? list.size() + 1 : comma + 1;
                if (name.empty())
                    continue;
                PPCContext saved = ctx;
                const uint32_t scratch = uint32_t(ctx.r1.u32) - 1024;
                for (size_t i = 0; i <= name.size(); ++i)
                    PPC_STORE_U8(scratch + 64 + i, i < name.size() ? uint8_t(name[i]) : 0);
                ctx.r3.u64 = scratch;          // out: 8 bytes
                ctx.r4.u64 = scratch + 64;     // text
                PPC_STORE_U32(scratch, 0);
                PPC_STORE_U32(scratch + 4, 0);
                __imp__sub_82144CA0(ctx, base);
                fprintf(stderr, "[hash] \"%s\" -> %08X %08X\n", name.c_str(), PPC_LOAD_U32(scratch), PPC_LOAD_U32(scratch + 4));
                ctx = saved;
            }
        }
    }
    // NFSMW_TEST_TEMPLATE=<16 hex digits>: every opponent is created as the car with this type key.
    struct WantedCar
    {
        WantedCar()
        {
            if (const char* k = std::getenv("NFSMW_TEST_TEMPLATE"))
                game::g_wantedCarKey = strtoull(k, nullptr, 16);
        }
        ~WantedCar() { game::g_wantedCarKey = 0; }
    } wantedCar;
    // NFSMW_TEST_CARKEY=<hex>: every opponent is created with this key (a test that r4 selects the car).
    if (const char* k = std::getenv("NFSMW_TEST_CARKEY"))
    {
        const uint32_t was = ctx.r4.u32;
        ctx.r4.u64 = uint32_t(strtoul(k, nullptr, 16));
        fprintf(stderr, "[create] opponent key %08X replaced by %08X\n", was, ctx.r4.u32);
    }
    __imp__sub_82333680(ctx, base);
}

// The player's car creation chain (the first vehicle of a race): NFSMW_PROBE_RACE=4 dumps the arguments.
#define PLAYER_PROBE(addr)                                                                                                                                  \
    PPC_FUNC(sub_##addr)                                                                                                                                    \
    {                                                                                                                                                       \
        static const bool on = std::getenv("NFSMW_PROBE_RACE") && std::getenv("NFSMW_PROBE_RACE")[0] == '4';                                                \
        if (on)                                                                                                                                             \
        {                                                                                                                                                   \
            auto words = [&](uint32_t a, int n) {                                                                                                           \
                std::string out;                                                                                                                            \
                for (int i = 0; i < n && a > 0x10000 && a < 0xD0000000; ++i)                                                                                \
                {                                                                                                                                           \
                    char b[16];                                                                                                                             \
                    snprintf(b, sizeof b, " %08X", PPC_LOAD_U32(a + 4 * i));                                                                                \
                    out += b;                                                                                                                               \
                }                                                                                                                                           \
                return out;                                                                                                                                 \
            };                                                                                                                                              \
            fprintf(stderr, "[create] sub_" #addr " r3=%08X r4=%08X r5=%08X r6=%08X | r3:%s | r4:%s | r5:%s\n", ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, \
                words(ctx.r3.u32, 8).c_str(), words(ctx.r4.u32, 8).c_str(), words(ctx.r5.u32, 8).c_str());                                                  \
        }                                                                                                                                                   \
        __imp__sub_##addr(ctx, base);                                                                                                                       \
    }
PLAYER_PROBE(824729E0)
PLAYER_PROBE(82473100)
PLAYER_PROBE(82473870)

// Attrib chain of an opponent's creation (docs/MULTIPLAYER.md): NFSMW_PROBE_RACE=6 reports calls in which a 64-bit car key (the hash of IS300, A4, GTI, CTS, RX8, ...) is in a register or in
// the 48 bytes a register points to, to find where the car is looked up.
namespace
{
    bool KeyIn(uint8_t* base, uint32_t v, std::string& where, const char* reg)
    {
        static const uint32_t keys[][2] = { { 0x0145D39E, 0x6027E317 }, { 0x2F672154, 0x9D286E74 }, { 0x0DB45E8F, 0x5A294DF0 }, { 0x0D928DCA, 0xF667ABA4 }, { 0xB17BA3D7, 0x12A66396 },
            { 0xACD524F6, 0x8AC41317 }, { 0x3677ACD2, 0xD2017447 }, { 0xBA99CE79, 0xDB3EE5CB }, { 0x271C1CB5, 0x60EBD21B }, { 0x585B4641, 0xA46FD6A9 } };
        if (v > 0x10000 && v < 0xD0000000)
        {
            for (uint32_t off = 0; off < 48; off += 4)
                for (auto& k : keys)
                    if (PPC_LOAD_U32(v + off) == k[0] && PPC_LOAD_U32(v + off + 4) == k[1])
                    {
                        char b[64];
                        snprintf(b, sizeof b, " %s+%u holds key %08X%08X", reg, off, k[0], k[1]);
                        where += b;
                        return true;
                    }
        }
        for (auto& k : keys)
            if (v == k[0] || v == k[1])
            {
                char b[64];
                snprintf(b, sizeof b, " %s itself is a half of %08X%08X", reg, k[0], k[1]);
                where += b;
                return true;
            }
        return false;
    }
}
#define KEY_PROBE(addr)                                                                                                                                      \
    PPC_FUNC(sub_##addr)                                                                                                                                     \
    {                                                                                                                                                        \
        static const bool on = std::getenv("NFSMW_PROBE_RACE") && std::getenv("NFSMW_PROBE_RACE")[0] == '6';                                                 \
        if (on)                                                                                                                                              \
        {                                                                                                                                                    \
            std::string w;                                                                                                                                   \
            KeyIn(base, ctx.r3.u32, w, "r3"); KeyIn(base, ctx.r4.u32, w, "r4"); KeyIn(base, ctx.r5.u32, w, "r5"); KeyIn(base, ctx.r6.u32, w, "r6"); KeyIn(base, ctx.r7.u32, w, "r7"); \
            if (!w.empty())                                                                                                                                  \
                fprintf(stderr, "[key] sub_" #addr " lr=%08X r3=%08X r4=%08X r5=%08X r6=%08X:%s\n", uint32_t(ctx.lr), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, w.c_str()); \
        }                                                                                                                                                    \
        __imp__sub_##addr(ctx, base);                                                                                                                        \
    }
KEY_PROBE(823D3F98)
KEY_PROBE(823E6680)
KEY_PROBE(823E59A0)
KEY_PROBE(8214BAA8)
KEY_PROBE(82147D70)

// Car selection for an opponent (docs/MULTIPLAYER.md): sub_82147BF8(layout, template, index, ...) instantiates a car from its template record (the car's 64-bit type key, the hash of its
// folder name, is at template+16). While the race script creates an opponent (sub_82333680, below) a replacement key can be set; the template passed in is then swapped for the
// template of the car with that key. NFSMW_TEST_TEMPLATE=<16 hex digits> sets one for every opponent (a test).
namespace
{
    uint32_t FindTemplate(uint8_t* base, uint64_t key)
    {
        static std::map<uint64_t, uint32_t> cache;
        auto it = cache.find(key);
        if (it != cache.end())
            return it->second;
        uint32_t found = 0;
        for (uint32_t a = 0xBAB30000; a < 0xBAB80000 && !found; a += 16)
            if (PPC_LOAD_U32(a + 16) == uint32_t(key >> 32) && PPC_LOAD_U32(a + 20) == uint32_t(key))
                found = a;
        if (found)
            cache[key] = found;
        return found;
    }
}
PPC_FUNC(sub_82147BF8)
{
    static const bool probe = std::getenv("NFSMW_PROBE_RACE") && std::getenv("NFSMW_PROBE_RACE")[0] == '6';
    if (game::g_wantedCarKey)
    {
        const uint32_t t = ctx.r4.u32;
        // only a car template (a record whose +16 is a 64-bit key found in the template area) is swapped
        if (t >= 0xBAB30000 && t < 0xBAB80000 && !(PPC_LOAD_U32(t + 16) == uint32_t(game::g_wantedCarKey >> 32) && PPC_LOAD_U32(t + 20) == uint32_t(game::g_wantedCarKey)))
        {
            const uint32_t w = FindTemplate(base, game::g_wantedCarKey);
            if (w)
            {
                if (probe)
                    fprintf(stderr, "[car] template %08X (key %08X%08X) replaced by %08X (key %016llX)\n", t, PPC_LOAD_U32(t + 16), PPC_LOAD_U32(t + 20), w, (unsigned long long)game::g_wantedCarKey);
                ctx.r4.u64 = w;
            }
        }
    }
    __imp__sub_82147BF8(ctx, base);
}
