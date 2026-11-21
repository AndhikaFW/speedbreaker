// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// PROBE (NFSMW_PROBE_NATIVES=1): every script native the game binds (the table in sub_8235E8A8; the names are the strings it uses) is logged when it is called, with its call
// object and parameter words (docs/MULTIPLAYER.md, "Events"). One hook per native function that nothing else hooks.
#include <stdafx.h>
#include <kernel/function.h>
#include "mp_adapter.h"

extern "C" PPC_FUNC(__imp__sub_82361D70);
extern "C" PPC_FUNC(__imp__sub_82361D78);
extern "C" PPC_FUNC(__imp__sub_82362570);
extern "C" PPC_FUNC(__imp__sub_82362718);
extern "C" PPC_FUNC(__imp__sub_82362578);
extern "C" PPC_FUNC(__imp__sub_823625C8);
extern "C" PPC_FUNC(__imp__sub_82362A08);
extern "C" PPC_FUNC(__imp__sub_82362618);
extern "C" PPC_FUNC(__imp__sub_82362720);
extern "C" PPC_FUNC(__imp__sub_823627D0);
extern "C" PPC_FUNC(__imp__sub_823627D8);
extern "C" PPC_FUNC(__imp__sub_823627E8);
extern "C" PPC_FUNC(__imp__sub_82366A48);
extern "C" PPC_FUNC(__imp__sub_823627F8);
extern "C" PPC_FUNC(__imp__sub_82362800);
extern "C" PPC_FUNC(__imp__sub_82362808);
extern "C" PPC_FUNC(__imp__sub_823629A8);
extern "C" PPC_FUNC(__imp__sub_823670E0);
extern "C" PPC_FUNC(__imp__sub_82367040);
extern "C" PPC_FUNC(__imp__sub_82363D28);
extern "C" PPC_FUNC(__imp__sub_823670A0);
extern "C" PPC_FUNC(__imp__sub_823671E8);
extern "C" PPC_FUNC(__imp__sub_82367150);
extern "C" PPC_FUNC(__imp__sub_823633D8);
extern "C" PPC_FUNC(__imp__sub_82363D68);
extern "C" PPC_FUNC(__imp__sub_82363FF0);
extern "C" PPC_FUNC(__imp__sub_82363EE8);
extern "C" PPC_FUNC(__imp__sub_823642F8);
extern "C" PPC_FUNC(__imp__sub_823642F0);
extern "C" PPC_FUNC(__imp__sub_82363C00);
extern "C" PPC_FUNC(__imp__sub_823655C0);
extern "C" PPC_FUNC(__imp__sub_823651C8);
extern "C" PPC_FUNC(__imp__sub_82364410);
extern "C" PPC_FUNC(__imp__sub_823644B0);
extern "C" PPC_FUNC(__imp__sub_82364630);
extern "C" PPC_FUNC(__imp__sub_82365730);
extern "C" PPC_FUNC(__imp__sub_82365B20);
extern "C" PPC_FUNC(__imp__sub_82365650);
extern "C" PPC_FUNC(__imp__sub_823656A8);
extern "C" PPC_FUNC(__imp__sub_82365D08);
extern "C" PPC_FUNC(__imp__sub_82367D70);
extern "C" PPC_FUNC(__imp__sub_82365E08);
extern "C" PPC_FUNC(__imp__sub_82365E60);
extern "C" PPC_FUNC(__imp__sub_82365E88);
extern "C" PPC_FUNC(__imp__sub_82365F00);
extern "C" PPC_FUNC(__imp__sub_82365F38);
extern "C" PPC_FUNC(__imp__sub_82365F70);
extern "C" PPC_FUNC(__imp__sub_82366000);
extern "C" PPC_FUNC(__imp__sub_82366170);
extern "C" PPC_FUNC(__imp__sub_823661F0);
extern "C" PPC_FUNC(__imp__sub_82366408);
extern "C" PPC_FUNC(__imp__sub_823640B0);
extern "C" PPC_FUNC(__imp__sub_82364158);
extern "C" PPC_FUNC(__imp__sub_823641E0);
extern "C" PPC_FUNC(__imp__sub_823641E8);
extern "C" PPC_FUNC(__imp__sub_82366388);
extern "C" PPC_FUNC(__imp__sub_823661F8);
extern "C" PPC_FUNC(__imp__sub_82366490);
extern "C" PPC_FUNC(__imp__sub_823664D0);
extern "C" PPC_FUNC(__imp__sub_823664D8);
extern "C" PPC_FUNC(__imp__sub_82366610);
extern "C" PPC_FUNC(__imp__sub_82366650);
extern "C" PPC_FUNC(__imp__sub_82366D80);
extern "C" PPC_FUNC(__imp__sub_82366F20);
extern "C" PPC_FUNC(__imp__sub_82364040);
extern "C" PPC_FUNC(__imp__sub_82364078);
extern "C" PPC_FUNC(__imp__sub_82368200);
extern "C" PPC_FUNC(__imp__sub_823680F0);
extern "C" PPC_FUNC(__imp__sub_82364F48);
extern "C" PPC_FUNC(__imp__sub_823650F0);
extern "C" PPC_FUNC(__imp__sub_82362A68);
extern "C" PPC_FUNC(__imp__sub_82364C70);
extern "C" PPC_FUNC(__imp__sub_82364E30);
extern "C" PPC_FUNC(__imp__sub_82366FD8);
extern "C" PPC_FUNC(__imp__sub_82364E48);
extern "C" PPC_FUNC(__imp__sub_82364E60);
extern "C" PPC_FUNC(__imp__sub_82364ED8);
extern "C" PPC_FUNC(__imp__sub_82364D30);
extern "C" PPC_FUNC(__imp__sub_82364DB0);
extern "C" PPC_FUNC(__imp__sub_82366810);
extern "C" PPC_FUNC(__imp__sub_82366788);
extern "C" PPC_FUNC(__imp__sub_82367560);
extern "C" PPC_FUNC(__imp__sub_82367588);
extern "C" PPC_FUNC(__imp__sub_82366898);
extern "C" PPC_FUNC(__imp__sub_82366910);
extern "C" PPC_FUNC(__imp__sub_82367380);
extern "C" PPC_FUNC(__imp__sub_82367440);
extern "C" PPC_FUNC(__imp__sub_823679F8);
extern "C" PPC_FUNC(__imp__sub_82367A08);
extern "C" PPC_FUNC(__imp__sub_82367A18);
extern "C" PPC_FUNC(__imp__sub_82368018);
extern "C" PPC_FUNC(__imp__sub_82362A60);
extern "C" PPC_FUNC(__imp__sub_8232FEA0);
extern "C" PPC_FUNC(__imp__sub_82330060);
extern "C" PPC_FUNC(__imp__sub_82330978);
extern "C" PPC_FUNC(__imp__sub_82330980);
extern "C" PPC_FUNC(__imp__sub_82330A08);
extern "C" PPC_FUNC(__imp__sub_82330A50);
extern "C" PPC_FUNC(__imp__sub_82330A98);
extern "C" PPC_FUNC(__imp__sub_82330238);
extern "C" PPC_FUNC(__imp__sub_8235DE60);
extern "C" PPC_FUNC(__imp__sub_82330988);
extern "C" PPC_FUNC(__imp__sub_8235DE78);
extern "C" PPC_FUNC(__imp__sub_8235DE70);
extern "C" PPC_FUNC(__imp__sub_823301D0);
extern "C" PPC_FUNC(__imp__sub_8232E7B0);
extern "C" PPC_FUNC(__imp__sub_82330880);
extern "C" PPC_FUNC(__imp__sub_8235DE90);
extern "C" PPC_FUNC(__imp__sub_82366C90);
extern "C" PPC_FUNC(__imp__sub_82366B08);
extern "C" PPC_FUNC(__imp__sub_82366F60);
extern "C" PPC_FUNC(__imp__sub_82365B80);
extern "C" PPC_FUNC(__imp__sub_82366FA8);
extern "C" PPC_FUNC(__imp__sub_82367280);
extern "C" PPC_FUNC(__imp__sub_82367290);
extern "C" PPC_FUNC(__imp__sub_823672A0);
extern "C" PPC_FUNC(__imp__sub_823672F0);
extern "C" PPC_FUNC(__imp__sub_823674F0);
extern "C" PPC_FUNC(__imp__sub_823674F8);
extern "C" PPC_FUNC(__imp__sub_823675B0);
extern "C" PPC_FUNC(__imp__sub_82367660);
extern "C" PPC_FUNC(__imp__sub_823676B8);
extern "C" PPC_FUNC(__imp__sub_823676E0);
extern "C" PPC_FUNC(__imp__sub_823676E8);
extern "C" PPC_FUNC(__imp__sub_823678C8);
extern "C" PPC_FUNC(__imp__sub_82366080);
extern "C" PPC_FUNC(__imp__sub_82367F18);
extern "C" PPC_FUNC(__imp__sub_82367FB8);
extern "C" PPC_FUNC(__imp__sub_82367990);
extern "C" PPC_FUNC(__imp__sub_823679D8);
extern "C" PPC_FUNC(__imp__sub_82367A28);
extern "C" PPC_FUNC(__imp__sub_82367A48);
extern "C" PPC_FUNC(__imp__sub_82367AE0);
extern "C" PPC_FUNC(__imp__sub_82367AD8);
extern "C" PPC_FUNC(__imp__sub_82367FE0);
extern "C" PPC_FUNC(__imp__sub_82368120);

namespace
{
    void NativeTrace(const char* what, PPCContext& ctx, uint8_t* base)
    {
        static const bool on = std::getenv("NFSMW_PROBE_NATIVES") != nullptr;
        if (!on) return;
        const uint32_t call = ctx.r3.u32;
        const uint32_t p = call > 0x10000 && call < 0xD0000000u ? PPC_LOAD_U32(call + 4) : 0;
        fprintf(stderr, "[native] %s call %08X params %08X:", what, call, p);
        for (int i = 0; i < 12 && p > 0x10000 && p < 0xD0000000u; ++i) fprintf(stderr, " %08X", PPC_LOAD_U32(p + 4 * i));
        for (int i = 0; i < 12 && p > 0x10000 && p < 0xD0000000u; ++i)
        {
            const uint32_t q = PPC_LOAD_U32(p + 4 * i);
            std::string t;
            if (q > 0x10000 && q < 0xD0000000u)
                for (int k = 0; k < 40; ++k) { const uint8_t c = PPC_LOAD_U8(q + k); if (c < 32 || c > 126) break; t += char(c); }
            if (t.size() >= 3) fprintf(stderr, " [%d]=\"%s\"", i, t.c_str());
        }
        fprintf(stderr, "\n");
    }
}

PPC_FUNC(sub_82361D70) { NativeTrace("Suspend", ctx, base); __imp__sub_82361D70(ctx, base); }
PPC_FUNC(sub_82361D78) { NativeTrace("SetFlag", ctx, base); __imp__sub_82361D78(ctx, base); }
PPC_FUNC(sub_82362570) { NativeTrace("IsCopSpeechPlaying", ctx, base); __imp__sub_82362570(ctx, base); }
PPC_FUNC(sub_82362718) { NativeTrace("AddEngagedRace", ctx, base); __imp__sub_82362718(ctx, base); }
PPC_FUNC(sub_82362578) { NativeTrace("GetNumChallengesPassed", ctx, base); __imp__sub_82362578(ctx, base); }
PPC_FUNC(sub_823625C8) { NativeTrace("GetNumRacesWon", ctx, base); __imp__sub_823625C8(ctx, base); }
PPC_FUNC(sub_82362A08) { NativeTrace("RandomInt", ctx, base); __imp__sub_82362A08(ctx, base); }
PPC_FUNC(sub_82362618) { NativeTrace("SetGenericCamera", ctx, base); __imp__sub_82362618(ctx, base); }
PPC_FUNC(sub_82362720) { NativeTrace("Play", ctx, base); __imp__sub_82362720(ctx, base); }
PPC_FUNC(sub_823627D0) { NativeTrace("PlayHackE3FMV", ctx, base); __imp__sub_823627D0(ctx, base); }
PPC_FUNC(sub_823627D8) { NativeTrace("Print", ctx, base); __imp__sub_823627D8(ctx, base); }
PPC_FUNC(sub_823627E8) { NativeTrace("PrintInstance", ctx, base); __imp__sub_823627E8(ctx, base); }
PPC_FUNC(sub_82366A48) { NativeTrace("ShowScreenMessage", ctx, base); __imp__sub_82366A48(ctx, base); }
PPC_FUNC(sub_823627F8) { NativeTrace("SetRaceCompleteForFE", ctx, base); __imp__sub_823627F8(ctx, base); }
PPC_FUNC(sub_82362800) { NativeTrace("StorePursuitRepForFE", ctx, base); __imp__sub_82362800(ctx, base); }
PPC_FUNC(sub_82362808) { NativeTrace("ShowMessage", ctx, base); __imp__sub_82362808(ctx, base); }
PPC_FUNC(sub_823629A8) { NativeTrace("ShowTimeExtension", ctx, base); __imp__sub_823629A8(ctx, base); }
PPC_FUNC(sub_823670E0) { NativeTrace("SabotageEngine", ctx, base); __imp__sub_823670E0(ctx, base); }
PPC_FUNC(sub_82367040) { NativeTrace("BlowEngine", ctx, base); __imp__sub_82367040(ctx, base); }
PPC_FUNC(sub_82363D28) { NativeTrace("DetachCameraFromRacer", ctx, base); __imp__sub_82363D28(ctx, base); }
PPC_FUNC(sub_823670A0) { NativeTrace("ChallengeComplete", ctx, base); __imp__sub_823670A0(ctx, base); }
PPC_FUNC(sub_823671E8) { NativeTrace("ClearAIControl", ctx, base); __imp__sub_823671E8(ctx, base); }
PPC_FUNC(sub_82367150) { NativeTrace("ForceAIControl", ctx, base); __imp__sub_82367150(ctx, base); }
PPC_FUNC(sub_82363D68) { NativeTrace("WarpPlayerToTrigger", ctx, base); __imp__sub_82363D68(ctx, base); }
PPC_FUNC(sub_82363FF0) { NativeTrace("ResetTrigger", ctx, base); __imp__sub_82363FF0(ctx, base); }
PPC_FUNC(sub_82363EE8) { NativeTrace("SetPlayerStartPosition", ctx, base); __imp__sub_82363EE8(ctx, base); }
PPC_FUNC(sub_823642F8) { NativeTrace("SetRacerGoal", ctx, base); __imp__sub_823642F8(ctx, base); }
PPC_FUNC(sub_823642F0) { NativeTrace("SetRacerLapsLeft", ctx, base); __imp__sub_823642F0(ctx, base); }
PPC_FUNC(sub_82363C00) { NativeTrace("KnockoutRacer/TotalRacer", ctx, base); __imp__sub_82363C00(ctx, base); }
PPC_FUNC(sub_823655C0) { NativeTrace("NotifyRacePlacement", ctx, base); __imp__sub_823655C0(ctx, base); }
PPC_FUNC(sub_823651C8) { NativeTrace("NotifySpeedTrapTriggered", ctx, base); __imp__sub_823651C8(ctx, base); }
PPC_FUNC(sub_82364410) { NativeTrace("NotifyCheckpointReached", ctx, base); __imp__sub_82364410(ctx, base); }
PPC_FUNC(sub_823644B0) { NativeTrace("NotifyLapFinished", ctx, base); __imp__sub_823644B0(ctx, base); }
PPC_FUNC(sub_82364630) { NativeTrace("NotifyRaceFinished", ctx, base); __imp__sub_82364630(ctx, base); }
PPC_FUNC(sub_82365730) { NativeTrace("SetRaceActivity", ctx, base); __imp__sub_82365730(ctx, base); }
PPC_FUNC(sub_82365B20) { NativeTrace("StartRaceTimers", ctx, base); __imp__sub_82365B20(ctx, base); }
PPC_FUNC(sub_82365650) { NativeTrace("SaveStartPositions", ctx, base); __imp__sub_82365650(ctx, base); }
PPC_FUNC(sub_823656A8) { NativeTrace("RestoreStartPositions", ctx, base); __imp__sub_823656A8(ctx, base); }
PPC_FUNC(sub_82367D70) { NativeTrace("ShowOnlinePostRaceScreen", ctx, base); __imp__sub_82367D70(ctx, base); }
PPC_FUNC(sub_82365E08) { NativeTrace("SetCopsEnabled", ctx, base); __imp__sub_82365E08(ctx, base); }
PPC_FUNC(sub_82365E60) { NativeTrace("NoNewPursuitsOrCops", ctx, base); __imp__sub_82365E60(ctx, base); }
PPC_FUNC(sub_82365E88) { NativeTrace("ForcePursuitStart", ctx, base); __imp__sub_82365E88(ctx, base); }
PPC_FUNC(sub_82365F00) { NativeTrace("EnterEngagableTrigger", ctx, base); __imp__sub_82365F00(ctx, base); }
PPC_FUNC(sub_82365F38) { NativeTrace("ExitEngagableTrigger", ctx, base); __imp__sub_82365F38(ctx, base); }
PPC_FUNC(sub_82365F70) { NativeTrace("EnterGateZone", ctx, base); __imp__sub_82365F70(ctx, base); }
PPC_FUNC(sub_82366000) { NativeTrace("ExitGateZone", ctx, base); __imp__sub_82366000(ctx, base); }
PPC_FUNC(sub_82366170) { NativeTrace("ShowRaceOverSummary", ctx, base); __imp__sub_82366170(ctx, base); }
PPC_FUNC(sub_823661F0) { NativeTrace("HideRaceOverSummary", ctx, base); __imp__sub_823661F0(ctx, base); }
PPC_FUNC(sub_82366408) { NativeTrace("SetTrafficSpeed", ctx, base); __imp__sub_82366408(ctx, base); }
PPC_FUNC(sub_823640B0) { NativeTrace("SpawnCop", ctx, base); __imp__sub_823640B0(ctx, base); }
PPC_FUNC(sub_82364158) { NativeTrace("SpawnCharacter", ctx, base); __imp__sub_82364158(ctx, base); }
PPC_FUNC(sub_823641E0) { NativeTrace("UnspawnCharacter", ctx, base); __imp__sub_823641E0(ctx, base); }
PPC_FUNC(sub_823641E8) { NativeTrace("SendCharacterStimulus", ctx, base); __imp__sub_823641E8(ctx, base); }
PPC_FUNC(sub_82366388) { NativeTrace("JackKnife", ctx, base); __imp__sub_82366388(ctx, base); }
PPC_FUNC(sub_823661F8) { NativeTrace("SetAllStaging", ctx, base); __imp__sub_823661F8(ctx, base); }
PPC_FUNC(sub_82366490) { NativeTrace("ShowPauseMenu", ctx, base); __imp__sub_82366490(ctx, base); }
PPC_FUNC(sub_823664D0) { NativeTrace("AwardCash", ctx, base); __imp__sub_823664D0(ctx, base); }
PPC_FUNC(sub_823664D8) { NativeTrace("AwardPoints", ctx, base); __imp__sub_823664D8(ctx, base); }
PPC_FUNC(sub_82366610) { NativeTrace("ChallengeCompleted", ctx, base); __imp__sub_82366610(ctx, base); }
PPC_FUNC(sub_82366650) { NativeTrace("UnlockRace", ctx, base); __imp__sub_82366650(ctx, base); }
PPC_FUNC(sub_82366D80) { NativeTrace("NotifyCountdownDone", ctx, base); __imp__sub_82366D80(ctx, base); }
PPC_FUNC(sub_82366F20) { NativeTrace("ResetCopsForRestart", ctx, base); __imp__sub_82366F20(ctx, base); }
PPC_FUNC(sub_82364040) { NativeTrace("ShowTriggerIcon", ctx, base); __imp__sub_82364040(ctx, base); }
PPC_FUNC(sub_82364078) { NativeTrace("HideTriggerIcon", ctx, base); __imp__sub_82364078(ctx, base); }
PPC_FUNC(sub_82368200) { NativeTrace("WarpToMarkerWhenRoaming", ctx, base); __imp__sub_82368200(ctx, base); }
PPC_FUNC(sub_823680F0) { NativeTrace("DoSpecialFinalization", ctx, base); __imp__sub_823680F0(ctx, base); }
PPC_FUNC(sub_82364F48) { NativeTrace("IsActiveSpeedTrap", ctx, base); __imp__sub_82364F48(ctx, base); }
PPC_FUNC(sub_823650F0) { NativeTrace("IsActiveMenuGate", ctx, base); __imp__sub_823650F0(ctx, base); }
PPC_FUNC(sub_82362A68) { NativeTrace("GetSimTime", ctx, base); __imp__sub_82362A68(ctx, base); }
PPC_FUNC(sub_82364C70) { NativeTrace("GetRacerIndex", ctx, base); __imp__sub_82364C70(ctx, base); }
PPC_FUNC(sub_82364E30) { NativeTrace("GetRacerElement", ctx, base); __imp__sub_82364E30(ctx, base); }
PPC_FUNC(sub_82366FD8) { NativeTrace("GetPlayerElement", ctx, base); __imp__sub_82366FD8(ctx, base); }
PPC_FUNC(sub_82364E48) { NativeTrace("GetRacerCharacter", ctx, base); __imp__sub_82364E48(ctx, base); }
PPC_FUNC(sub_82364E60) { NativeTrace("GetNumRacers", ctx, base); __imp__sub_82364E60(ctx, base); }
PPC_FUNC(sub_82364ED8) { NativeTrace("GetSimableSpeedKmh", ctx, base); __imp__sub_82364ED8(ctx, base); }
PPC_FUNC(sub_82364D30) { NativeTrace("RacerIsHuman", ctx, base); __imp__sub_82364D30(ctx, base); }
PPC_FUNC(sub_82364DB0) { NativeTrace("PlayerIsLocal", ctx, base); __imp__sub_82364DB0(ctx, base); }
PPC_FUNC(sub_823633D8)   // InitRacers: the script sets an event's racers up
{
    NativeTrace("InitRacers", ctx, base);
    game::HostEventSet(true);
    __imp__sub_823633D8(ctx, base);
}
PPC_FUNC(sub_82365D08)   // EnterPostRaceFlow: the event is over
{
    NativeTrace("EnterPostRaceFlow", ctx, base);
    game::HostEventSet(false);
    __imp__sub_82365D08(ctx, base);
}
PPC_FUNC(sub_82366810)   // IsRaceCompleted(event object): the answer is in r3
{
    NativeTrace("IsRaceCompleted", ctx, base);
    __imp__sub_82366810(ctx, base);
    // NFSMW_EVENTS_OPEN=1: no event counts as won, so every event can be engaged again (a server hosts them without a career's progress).
    if (std::getenv("NFSMW_EVENTS_OPEN"))
        ctx.r3.u64 = 0;
}
PPC_FUNC(sub_82366788)   // IsRaceUnlocked(event object)
{
    NativeTrace("IsRaceUnlocked", ctx, base);
    __imp__sub_82366788(ctx, base);
    if (std::getenv("NFSMW_EVENTS_OPEN"))
        ctx.r3.u64 = 1;
}
PPC_FUNC(sub_82367560) { NativeTrace("IsOnlineGame", ctx, base); __imp__sub_82367560(ctx, base); }
PPC_FUNC(sub_82367588) { NativeTrace("IsLANGame", ctx, base); __imp__sub_82367588(ctx, base); }
PPC_FUNC(sub_82366898) { NativeTrace("AllRacersDone", ctx, base); __imp__sub_82366898(ctx, base); }
PPC_FUNC(sub_82366910) { NativeTrace("AllHumanPlayersDone", ctx, base); __imp__sub_82366910(ctx, base); }
PPC_FUNC(sub_82367380) { NativeTrace("SimableDistance", ctx, base); __imp__sub_82367380(ctx, base); }
PPC_FUNC(sub_82367440) { NativeTrace("SimableAngle", ctx, base); __imp__sub_82367440(ctx, base); }
PPC_FUNC(sub_823679F8) { NativeTrace("AllowEngageEvents", ctx, base); __imp__sub_823679F8(ctx, base); }
PPC_FUNC(sub_82367A08) { NativeTrace("AllowMenuGates", ctx, base); __imp__sub_82367A08(ctx, base); }
PPC_FUNC(sub_82367A18) { NativeTrace("AllowEngageSafehouse", ctx, base); __imp__sub_82367A18(ctx, base); }
PPC_FUNC(sub_82368018) { NativeTrace("DoSpecialSetup", ctx, base); __imp__sub_82368018(ctx, base); }
PPC_FUNC(sub_82362A60) { NativeTrace("IsNextGen", ctx, base); __imp__sub_82362A60(ctx, base); }
PPC_FUNC(sub_8232FEA0) { NativeTrace("RaceStatus", ctx, base); __imp__sub_8232FEA0(ctx, base); }
PPC_FUNC(sub_82330060) { NativeTrace("ClearRacers", ctx, base); __imp__sub_82330060(ctx, base); }
PPC_FUNC(sub_82330978) { NativeTrace("AddRacer", ctx, base); __imp__sub_82330978(ctx, base); }
PPC_FUNC(sub_82330980) { NativeTrace("AddAvailableEventToMap", ctx, base); __imp__sub_82330980(ctx, base); }
PPC_FUNC(sub_82330A08) { NativeTrace("AddSpeedTrapToMap", ctx, base); __imp__sub_82330A08(ctx, base); }
PPC_FUNC(sub_82330A50) { NativeTrace("ClearCheckpoints", ctx, base); __imp__sub_82330A50(ctx, base); }
PPC_FUNC(sub_82330A98) { NativeTrace("AddCheckpoint", ctx, base); __imp__sub_82330A98(ctx, base); }
PPC_FUNC(sub_82330238) { NativeTrace("SetRoaming", ctx, base); __imp__sub_82330238(ctx, base); }
PPC_FUNC(sub_8235DE60) { NativeTrace("SetIsLoading", ctx, base); __imp__sub_8235DE60(ctx, base); }
PPC_FUNC(sub_82330988) { NativeTrace("EnterSuddenDeath", ctx, base); __imp__sub_82330988(ctx, base); }
PPC_FUNC(sub_8235DE78) { NativeTrace("AwardBonusTime", ctx, base); __imp__sub_8235DE78(ctx, base); }
PPC_FUNC(sub_8235DE70) { NativeTrace("SetActivelyRacing", ctx, base); __imp__sub_8235DE70(ctx, base); }
PPC_FUNC(sub_823301D0) { NativeTrace("SetTaskTime", ctx, base); __imp__sub_823301D0(ctx, base); }
PPC_FUNC(sub_8232E7B0) { NativeTrace("DisableBarriers", ctx, base); __imp__sub_8232E7B0(ctx, base); }
PPC_FUNC(sub_82330880) { NativeTrace("EnableBinBarriers", ctx, base); __imp__sub_82330880(ctx, base); }
PPC_FUNC(sub_8235DE90) { NativeTrace("NotifyScriptWhenLoaded", ctx, base); __imp__sub_8235DE90(ctx, base); }
PPC_FUNC(sub_82366C90) { NativeTrace("GetPlayerBounty", ctx, base); __imp__sub_82366C90(ctx, base); }
PPC_FUNC(sub_82366B08) { NativeTrace("AwardPlayerBounty", ctx, base); __imp__sub_82366B08(ctx, base); }
PPC_FUNC(sub_82366F60) { NativeTrace("JumpToCarLot", ctx, base); __imp__sub_82366F60(ctx, base); }
PPC_FUNC(sub_82365B80) { NativeTrace("AbandonRace", ctx, base); __imp__sub_82365B80(ctx, base); }
PPC_FUNC(sub_82366FA8) { NativeTrace("JumpToSafeHouse", ctx, base); __imp__sub_82366FA8(ctx, base); }
PPC_FUNC(sub_82367280) { NativeTrace("SetTimer", ctx, base); __imp__sub_82367280(ctx, base); }
PPC_FUNC(sub_82367290) { NativeTrace("KillTimer", ctx, base); __imp__sub_82367290(ctx, base); }
PPC_FUNC(sub_823672A0) { NativeTrace("ShowGPS", ctx, base); __imp__sub_823672A0(ctx, base); }
PPC_FUNC(sub_823672F0) { NativeTrace("NavigatePlayerTo", ctx, base); __imp__sub_823672F0(ctx, base); }
PPC_FUNC(sub_823674F0) { NativeTrace("Assert", ctx, base); __imp__sub_823674F0(ctx, base); }
PPC_FUNC(sub_823674F8) { NativeTrace("NotifyActivityFinsihed", ctx, base); __imp__sub_823674F8(ctx, base); }
PPC_FUNC(sub_823675B0) { NativeTrace("SkipCareerIntro", ctx, base); __imp__sub_823675B0(ctx, base); }
PPC_FUNC(sub_82367660) { NativeTrace("SetChanceOfRain", ctx, base); __imp__sub_82367660(ctx, base); }
PPC_FUNC(sub_823676B8) { NativeTrace("Fade", ctx, base); __imp__sub_823676B8(ctx, base); }
PPC_FUNC(sub_823676E0) { NativeTrace("IntroduceRival", ctx, base); __imp__sub_823676E0(ctx, base); }
PPC_FUNC(sub_823676E8) { NativeTrace("PlayTutorial", ctx, base); __imp__sub_823676E8(ctx, base); }
PPC_FUNC(sub_823678C8) { NativeTrace("DoSafeHouseIntro", ctx, base); __imp__sub_823678C8(ctx, base); }
PPC_FUNC(sub_82366080) { NativeTrace("DoZoneMenuAction", ctx, base); __imp__sub_82366080(ctx, base); }
PPC_FUNC(sub_82367F18) { NativeTrace("SetCurrentTimeOfDay", ctx, base); __imp__sub_82367F18(ctx, base); }
PPC_FUNC(sub_82367FB8) { NativeTrace("ReloadWorld", ctx, base); __imp__sub_82367FB8(ctx, base); }
PPC_FUNC(sub_82367990) { NativeTrace("IsCareerMode", ctx, base); __imp__sub_82367990(ctx, base); }
PPC_FUNC(sub_823679D8) { NativeTrace("IsSplitScreen", ctx, base); __imp__sub_823679D8(ctx, base); }
PPC_FUNC(sub_82367A28) { NativeTrace("SetHasRapSheet", ctx, base); __imp__sub_82367A28(ctx, base); }
PPC_FUNC(sub_82367A48) { NativeTrace("SetWorldHeat", ctx, base); __imp__sub_82367A48(ctx, base); }
PPC_FUNC(sub_82367AE0) { NativeTrace("ShowWinningPostRaceScreen", ctx, base); __imp__sub_82367AE0(ctx, base); }
PPC_FUNC(sub_82367AD8) { NativeTrace("ShowLosingPostRaceScreen", ctx, base); __imp__sub_82367AD8(ctx, base); }
PPC_FUNC(sub_82367FE0) { NativeTrace("PreventPlayerBeingBusted", ctx, base); __imp__sub_82367FE0(ctx, base); }
PPC_FUNC(sub_82368120) { NativeTrace("CalculateRanking", ctx, base); __imp__sub_82368120(ctx, base); }
