// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The game's own developer network configuration, run on request (NFSMW_ONLINE_CFG=1).
//
// The executable contains a loader for a text file, online.cfg (KEY=VALUE lines; sub_82538950) and a reader of its keys (sub_82538C10): SKIPFE,
// SKIPFEPLAYERCAR, SKIPFENUMAICARS, SKIPFENUMLAPS, SKIPFETRACKNUMBER (start a race without the front end), ISSERVER, NUMCLIENTCARS, SERVERIP, SERVERPORT, CLIENTIP<n>
// (a direct server and its clients, no lobby), NetworkUseLobbies, NetworkLobbyIP and others (docs/MULTIPLAYER.md). Nothing in the release build calls the loader, so
// these never ran, but the code that reads the values they set is the game's own and is live: the main loop (sub_823B0008) creates the game (sub_823AF408), then
// reads the skip-front-end flag and starts a race directly (sub_822BE238) or the front end (sub_823B4428). With NFSMW_ONLINE_CFG=1 the loader is called
// once, right after the game is created, so the file (game/files/online.cfg, found as the game's file system gives it, D:\online.cfg) is read before that flag is.
#include <stdafx.h>

#include <kernel/function.h>

extern "C" PPC_FUNC(__imp__sub_823AF408);  // create the game (called by the main loop at 0x823B0028)
extern "C" PPC_FUNC(__imp__sub_82538950);  // the online.cfg loader (r3: a path, or 0 for "online.cfg")

PPC_FUNC(sub_823AF408)
{
    const uint32_t returnTo = uint32_t(ctx.lr);
    __imp__sub_823AF408(ctx, base);
    static bool done = false;
    if (!done && returnTo == 0x823B002C && std::getenv("NFSMW_ONLINE_CFG"))
    {
        done = true;
        fprintf(stderr, "[online.cfg] the game is created: calling its online.cfg loader (sub_82538950)\n");
        PPCContext saved = ctx;
        ctx.r3.u64 = 0;
        ctx.lr = 0x823B002C;
        __imp__sub_82538950(ctx, base);
        ctx = saved;
    }
}
