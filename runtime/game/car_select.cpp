// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Car selection for the multiplayer races (docs/MULTIPLAYER.md, "Car models").
//
// What the game does: every car has a type key, the 64-bit string key (sub_82144CA0) of its folder name (IS300, BMWM3GTRE46, ...), and a template record in the heap (the key is at
// template+16, the car's dimensions before it). The race script creates each opponent with sub_82333680(call, racer key); somewhere below it a car is instantiated from its
// template by sub_82147BF8(layout, template, index, ...). Passing another car's template there makes the opponent that car (checked: all opponents turned into RX8s, with the RX8's mass
// and inertia). So: while sub_82333680 runs, the template passed to sub_82147BF8 is swapped for the template of the wanted car.
// The names come from the game's own table of vehicle names (a run of NUL-separated names, "RX8", "rx8", "LANCEREVO8", ... in the heap); the keys from the game's own key function,
// called from a hook with the guest's own context, so the catalogue is built on the first game frame after the table exists (NFSMW_LOG_CARS=1 prints it).
#include <stdafx.h>

#include "car_select.h"
#include "mp_adapter.h"
#include <kernel/function.h>

#include <algorithm>
#include <map>
#include <mutex>

extern "C" PPC_FUNC(__imp__sub_823AFE20);   // the game's frame (the main loop calls it once a frame): where the catalogue is built
extern "C" PPC_FUNC(__imp__sub_82144CA0);   // the string key
extern "C" PPC_FUNC(__imp__sub_82333680);   // the script native that creates a racing opponent
extern "C" PPC_FUNC(__imp__sub_82147BF8);   // instantiates a car from its template
extern "C" PPC_FUNC(__imp__sub_824729E0);   // creates the first vehicle of a race: the player's

namespace
{
    constexpr uint32_t kTemplateLo = 0xBAB30000, kTemplateHi = 0xBAB80000;   // where the templates are (the heap does not move between launches)
    constexpr uint32_t kNameLo = 0xBAB00000, kNameHi = 0xBAB40000;           // where the table of vehicle names is

    std::mutex g_mutex;
    std::vector<game::CarEntry> g_catalog;
    bool g_built = false;
    std::vector<uint64_t> g_opponentKeys;
    int g_opponentIndex = 0;
    uint64_t g_playerKey = 0;
    thread_local uint64_t t_wantedKey = 0;

    uint32_t FindTemplate(uint8_t* base, uint64_t key)
    {
        for (uint32_t a = kTemplateLo; a < kTemplateHi; a += 16)
            if (PPC_LOAD_U32(a + 16) == uint32_t(key >> 32) && PPC_LOAD_U32(a + 20) == uint32_t(key))
                return a;
        return 0;
    }

    // What to show for the game's names we know (the rest show the name itself).
    const char* Display(const std::string& n)
    {
        static const std::map<std::string, const char*> names = {
            { "A3", "Audi A3 3.2" }, { "A4", "Audi RS4" }, { "TT", "Audi TT 3.2 Quattro" }, { "BMWM3GTRE46", "BMW M3 GTR" }, { "CARRERAGT", "Porsche Carrera GT" },
            { "CORVETTEC6R", "Chevrolet Corvette C6.R" }, { "COBALTSS", "Chevrolet Cobalt SS" }, { "CTS", "Cadillac CTS" }, { "CLK500", "Mercedes-Benz CLK 500" },
            { "SL500", "Mercedes-Benz SL 500" }, { "SLR", "Mercedes-Benz SLR McLaren" }, { "GTI", "Volkswagen Golf GTI" }, { "IS300", "Lexus IS 300" },
            { "IMPREZAWRX", "Subaru Impreza WRX STI" }, { "LANCEREVO8", "Mitsubishi Lancer Evolution VIII" }, { "LANCEREVO9", "Mitsubishi Lancer Evolution IX" },
            { "MUSTANGGT", "Ford Mustang GT" }, { "PUNTO", "Fiat Punto" }, { "RX7", "Mazda RX-7" }, { "RX8", "Mazda RX-8" }, { "SUPRA", "Toyota Supra" },
            { "GALLARDO", "Lamborghini Gallardo" }, { "MURCIELAGO", "Lamborghini Murcielago" }, { "ELISE", "Lotus Elise" }, { "VIPER", "Dodge Viper SRT10" },
            { "GTO", "Pontiac GTO" }, { "MONARO", "Vauxhall Monaro VXR" }, { "FOCUS", "Ford Focus ST" }, { "ECLIPSE", "Mitsubishi Eclipse GTS" }, { "FORDGT", "Ford GT" },
            { "PORSCHE911", "Porsche 911" }, { "911GT2", "Porsche 911 GT2" }, { "CARRERA", "Porsche 911 Carrera S" }, { "SLK", "Mercedes-Benz SLK" }, { "SUPRAMKIV", "Toyota Supra" },
            { "CLIO", "Renault Clio V6" }, { "SL65", "Mercedes-Benz SL 65 AMG" }, { "BMWM3GTR", "BMW M3 GTR (street)" }, { "DB9", "Aston Martin DB9" },
            { "ECLIPSEGT", "Mitsubishi Eclipse GT" }, { "911TURBO", "Porsche 911 Turbo" }, { "CAMARO", "Chevrolet Camaro" }, { "997S", "Porsche 911 Carrera S" },
            { "CAYMANS", "Porsche Cayman S" }, { "CORVETTE", "Chevrolet Corvette" }, { "RX8SPEEDT", "Mazda RX-8 (tuned)" },
        };
        auto it = names.find(n);
        return it == names.end() ? n.c_str() : it->second;
    }

    bool Skip(const std::string& n)
    {
        for (const char* p : { "COP", "TRAF", "TRAILER", "SEMI", "TRUCK", "TOW", "PURSUIT", "HELI", "BEHAVIOR", "CHOPPER", "TAXI", "GARB", "PIZZA", "PICKUP", "MINIVAN", "CEMTR", "SPOILER", "ROOF", "WHEELS", "TEXTURES", "GEOMETRY", "DRIVER" })
            if (n.find(p) != std::string::npos)
                return true;
        return n.size() < 2;
    }

    // Build the catalogue (on the guest thread of the frame hook, with its context, to call the game's key function).
    void Build(PPCContext& ctx, uint8_t* base)
    {
        // Find the table: the signature \0RX8\0rx8\0 .
        static const char sig[] = "\0RX8\0rx8\0";
        uint32_t at = 0;
        for (uint32_t a = kNameLo; a + sizeof sig < kNameHi && !at; ++a)
        {
            bool ok = true;
            for (size_t i = 0; i < sizeof sig - 1 && ok; ++i)
                ok = *(base + a + i) == uint8_t(sig[i]);
            if (ok)
                at = a;
        }
        if (!at)
            return;
        // The table: names (letters, digits, underscore) separated by NULs, around the signature.
        uint32_t lo = at, hi = at;
        auto isNameByte = [&](uint32_t a) { const uint8_t c = *(base + a); return c == 0 || c == '_' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
        while (lo > kNameLo && at - lo < 4096 && isNameByte(lo - 1)) --lo;
        while (hi < kNameHi && hi - at < 8192 && isNameByte(hi)) ++hi;
        std::vector<std::string> names;
        for (uint32_t a = lo; a < hi;)
        {
            std::string t;
            while (a < hi && *(base + a) != 0)
                t += char(*(base + a++));
            ++a;
            if (t.size() >= 2 && std::all_of(t.begin(), t.end(), [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }) && std::any_of(t.begin(), t.end(), [](char c) { return c >= 'A' && c <= 'Z'; }))
                if (std::find(names.begin(), names.end(), t) == names.end())
                    names.push_back(t);
        }
        std::vector<game::CarEntry> out;
        for (const std::string& n : names)
        {
            PPCContext saved = ctx;
            const uint32_t scratch = uint32_t(ctx.r1.u32) - 1024;
            for (size_t i = 0; i <= n.size(); ++i)
                PPC_STORE_U8(scratch + 64 + i, i < n.size() ? uint8_t(n[i]) : 0);
            PPC_STORE_U32(scratch, 0);
            PPC_STORE_U32(scratch + 4, 0);
            ctx.r3.u64 = scratch;
            ctx.r4.u64 = scratch + 64;
            __imp__sub_82144CA0(ctx, base);
            const uint64_t key = (uint64_t(PPC_LOAD_U32(scratch)) << 32) | PPC_LOAD_U32(scratch + 4);
            ctx = saved;
            if (key && FindTemplate(base, key))
            {
                game::CarEntry e;
                e.name = n;
                e.display = Display(n);
                e.key = key;
                out.push_back(e);
            }
        }
        if (out.empty())
            return;
        static const bool log = std::getenv("NFSMW_LOG_CARS") != nullptr;
        if (log)
        {
            fprintf(stderr, "[cars] %zu names in the table, %zu with a template:\n", names.size(), out.size());
            for (const auto& e : out)
                fprintf(stderr, "[cars]   %-16s %016llX %s%s\n", e.name.c_str(), (unsigned long long)e.key, e.display.c_str(), Skip(e.name) ? "  (not offered: not a race car)" : "");
        }
        out.erase(std::remove_if(out.begin(), out.end(), [](const game::CarEntry& e) { return Skip(e.name); }), out.end());
        std::lock_guard<std::mutex> g(g_mutex);
        g_catalog = std::move(out);
        g_built = true;
    }
}

namespace game
{
    std::vector<CarEntry> CarCatalog()
    {
        std::lock_guard<std::mutex> g(g_mutex);
        return g_catalog;
    }
    void SetOpponentCars(const std::vector<uint64_t>& keys)
    {
        std::lock_guard<std::mutex> g(g_mutex);
        g_opponentKeys = keys;
    }
    void SetPlayerCar(uint64_t key)
    {
        std::lock_guard<std::mutex> g(g_mutex);
        g_playerKey = key;
    }
    void ResetOpponentCount()
    {
        std::lock_guard<std::mutex> g(g_mutex);
        g_opponentIndex = 0;
    }
    uint64_t CarKeyOfRecord(const uint8_t* base, uint32_t record)
    {
        if (record < 0x10000 || record > 0xD0000000)
            return 0;
        return (uint64_t(PPC_LOAD_U32(record + 16)) << 32) | PPC_LOAD_U32(record + 20);
    }
}

PPC_FUNC(sub_823AFE20)
{
    static std::chrono::steady_clock::time_point last;
    {
        std::lock_guard<std::mutex> g(g_mutex);
        if (g_built)
            goto run;
    }
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - last > std::chrono::seconds(2))
        {
            last = now;
            Build(ctx, base);
        }
    }
run:
    __imp__sub_823AFE20(ctx, base);
}

PPC_FUNC(sub_82333680)
{
    bool first = false;
    {
        std::lock_guard<std::mutex> g(g_mutex);
        first = g_opponentIndex == 0;
    }
    if (first)
        game::MpBeforeOpponents(base);   // waits for the other players' cars and sets the opponents' cars
    uint64_t want = 0;
    {
        std::lock_guard<std::mutex> g(g_mutex);
        if (g_opponentIndex < int(g_opponentKeys.size()))
            want = g_opponentKeys[g_opponentIndex];
        ++g_opponentIndex;
    }
    if (const char* k = std::getenv("NFSMW_TEST_TEMPLATE"))   // a test: every opponent is this car (16 hex digits)
        want = strtoull(k, nullptr, 16);
    if (want)
        fprintf(stderr, "[cars] opponent %d is made a car with key %016llX\n", g_opponentIndex - 1, (unsigned long long)want);
    t_wantedKey = want;
    __imp__sub_82333680(ctx, base);
    t_wantedKey = 0;
}

PPC_FUNC(sub_82147BF8)
{
    if (t_wantedKey)
    {
        const uint32_t t = ctx.r4.u32;
        if (t >= kTemplateLo && t < kTemplateHi && !(PPC_LOAD_U32(t + 16) == uint32_t(t_wantedKey >> 32) && PPC_LOAD_U32(t + 20) == uint32_t(t_wantedKey)))
            if (const uint32_t w = FindTemplate(base, t_wantedKey))
                ctx.r4.u64 = w;
    }
    __imp__sub_82147BF8(ctx, base);
}

// The player's vehicle is the first one a race creates (the vehicle table, 0x82C745A8, still has no entries), through sub_824729E0.
PPC_FUNC(sub_824729E0)
{
    uint64_t want = 0;
    if (PPC_LOAD_U32(0x82C745B4) == 0)
    {
        std::lock_guard<std::mutex> g(g_mutex);
        want = g_playerKey;
    }
    if (const char* k = std::getenv("NFSMW_TEST_PLAYERCAR"))   // a test: the player's car is this one (16 hex digits)
        if (PPC_LOAD_U32(0x82C745B4) == 0)
            want = strtoull(k, nullptr, 16);
    if (want)
        fprintf(stderr, "[cars] the player's car is made a car with key %016llX\n", (unsigned long long)want);
    if (std::getenv("NFSMW_PROBE_PLAYER") && PPC_LOAD_U32(0x82C745B4) == 0)
    {
        // where the player's car is named in the arguments: the IS300 key halves or its template pointer, in the object and one level of pointers below it
        auto report = [&](uint32_t a, const char* what) {
            for (uint32_t off = 0; off < 0x200; off += 4)
            {
                const uint32_t v = PPC_LOAD_U32(a + off);
                if (v == 0x0145D39E || v == 0x6027E317 || v == 0xBAB58070 || v == 0xBAB58080)
                    fprintf(stderr, "[player] %s+0x%X = %08X\n", what, off, v);
            }
        };
        report(ctx.r3.u32, "r3");
        report(ctx.r4.u32, "r4");
        report(ctx.r5.u32, "r5");
        for (uint32_t off = 0; off < 0x100; off += 4)
        {
            const uint32_t q = PPC_LOAD_U32(ctx.r3.u32 + off);
            if (q > 0x10000 && q < 0xD0000000 && (q & 3) == 0)
            {
                char w[32];
                snprintf(w, sizeof w, "[r3+0x%X]", off);
                report(q, w);
            }
        }
    }
    const uint64_t was = t_wantedKey;
    t_wantedKey = want ? want : was;
    __imp__sub_824729E0(ctx, base);
    t_wantedKey = was;
    if (PPC_LOAD_U32(0x82C745B4) == 1)
        game::MpPlayerCreated(base);   // the player's vehicle is the only one: tell the others which car it is
}
