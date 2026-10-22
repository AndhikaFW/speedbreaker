// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The open-world multiplayer mode (docs/MULTIPLAYER.md, "Open world"): players share the game's open world (the dev start, game/online_cfg.cpp, boots into it) and see each
// other's cars; a marker starts a race that happens in the world. This file holds the first experiment: a vehicle made in the world outside a race, with the game's own
// opponent creation (sub_82333680, the script native the race script calls with a racer key), to be driven by a remote player's state.
#include <stdafx.h>

#include "car_select.h"
#include "mp_adapter.h"
#include <algorithm>
#include <kernel/function.h>
#include <kernel/heap.h>

extern "C" PPC_FUNC(__imp__sub_82333680);

namespace
{
    constexpr uint32_t kVehicleTable = 0x82C745A8;
}

namespace game
{
    // Called once a game frame from the game's own thread (car_select.cpp's frame hook): each remote player that has no vehicle in the world yet gets one, made with the game's own
    // opponent creation for the car's racer collection; it is found as the vehicle that was not in the vehicle table before.
    void WorldModeFrame(PPCContext& ctx, uint8_t* base)
    {
        static const char* spawn = std::getenv("NFSMW_WORLD_SPAWN");   // an experiment: one racer made in the world, by car name (lower case), without the server
        static int frames = 0, stage = 0;
        uint8_t id = 0;
        std::string car;
        if (spawn)
        {
            static const int delayFrames = std::getenv("NFSMW_WORLD_SPAWN_FRAMES") ? atoi(std::getenv("NFSMW_WORLD_SPAWN_FRAMES")) : 900;
            if (PPC_LOAD_U32(kVehicleTable + 12) == 0 || ++frames < delayFrames || stage)
                return;
            stage = 1;
            car = spawn;
        }
        else if (!MpWorldMode() || !MpWorldNextSpawn(id, car))
            return;
        const std::vector<uint32_t> before = MpWorldVehicles(base);
        // The script native's call object: +0 receives the made object, +4 points at the parameter block (nothing in it: the racer key is the fallback).
        uint8_t* callBlock = static_cast<uint8_t*>(g_userHeap.Alloc(512));
        memset(callBlock, 0, 512);
        const uint32_t call = uint32_t(callBlock - base), params = call + 256;
        PPC_STORE_U32(call + 4, params);
        const uint32_t key = RacerKeyOf(car);
        fprintf(stderr, "[world] making a vehicle for player %u in the world: car %s, racer key %08X\n", unsigned(id), car.c_str(), key);
        PPCContext saved = ctx;
        ctx.r3.u64 = call;
        ctx.r4.u64 = key;
        __imp__sub_82333680(ctx, base);
        ctx = saved;
        uint32_t owner = 0;
        for (uint32_t v : MpWorldVehicles(base))
            if (std::find(before.begin(), before.end(), v) == before.end())
                owner = v;
        fprintf(stderr, "[world] vehicles now %zu, the new one is %08X\n", MpWorldVehicles(base).size(), owner);
        if (!spawn)
            MpWorldSpawned(id, owner);
        g_userHeap.Free(callBlock);
    }
}
