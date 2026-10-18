// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// A probe, off unless NFSMW_PROBE_RACE=1: logs each call of the game's "start a race" handlers with the registers and the guest call chain, to find
// what the front end calls to start a race (for starting one from the online lobby, docs/MULTIPLAYER.md).
//
// sub_82365788 is the handler bound to the script name "StartRace" in sub_8235E8A8 (its body prints "Game_StartRace: %s"). The chain is read from the
// guest stack: at a function's entry r1 is its caller's frame; a frame's first word is its caller's frame, and a function saves its link register at
// (its caller's frame) - 8.
#include <stdafx.h>

#include <kernel/function.h>

extern "C" PPC_FUNC(__imp__sub_82365788);
extern "C" PPC_FUNC(__imp__sub_8236C180);
extern "C" PPC_FUNC(__imp__sub_8233DC58);
extern "C" PPC_FUNC(__imp__sub_8232D7D8);
extern "C" PPC_FUNC(__imp__sub_8235DA08);
extern "C" PPC_FUNC(__imp__sub_823993C0);

namespace
{
    bool Enabled()
    {
        static const bool on = std::getenv("NFSMW_PROBE_RACE") != nullptr;
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
PROBE(823993C0)

PPC_FUNC(sub_82365788)
{
    if (Enabled())
        LogCall("sub_82365788 (StartRace)", ctx, base);
    __imp__sub_82365788(ctx, base);
}
