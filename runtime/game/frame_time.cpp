// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See frame_time.h.
//
// NFSMW times itself with mftb. The main loop (sub_823B0008) measures each
// frame and hands it, in milliseconds as 16.16 fixed point, to the frame
// (sub_823AFE20). That quantises it to 1/3600 s (sub_823C6500; the frame's
// time for its real-time users, [0x82A39660]), advances RealTimeFrames and a
// 4000 Hz tick counter at 0x82A399AC (AdvanceRealTime, 0x823C66C8), and runs
// the world simulation in fixed 1/60 s steps from an accumulator over those
// ticks (sub_823A2348: floor(elapsed / step) steps, the remainder kept; the
// step is stored once, at boot, and tasks copy it). There is no interpolation:
// a frame's world state is its steps'.
//
// So a frame that isn't 1/60 s long shows it. With the vblank locked to the
// player's 59.9726 Hz monitor, a frame lasts 16.674 ms and every 36.6 s one
// runs two steps: the car jumps, where the presenter used to drop a frame.
// And at any rate a frame's measured length jitters (the main thread's
// wake-up, the 250 us ticks), so while the accumulator's remainder sits near
// a step boundary, frames alternate between no step and two.
//
// Each guest vblank is made one step instead (NFSMW_GAME_TIME=0 turns this
// off). A frame is given whole vblanks of 1/60 s, as the console's 60 Hz
// vblank would have given it; the stepper's elapsed time, then k steps give
// or take a tick, is rounded to k. The main loop runs ahead of the display:
// the game's render thread swaps (VdSwap) during the loop's next frame, and
// the command processor lets each swap go at a vblank about two frames after
// that, so the loop's frames drift against the vblanks (counted from one
// frame's start to the next) while the display shows one frame a vblank from
// the queue: a frame the loop is slow with counts 2 or 3, the next ones none.
// Each is given one vblank while game time stays within a few vblanks of the
// vblanks that fired (game/whole_vblanks.h; given their counts, the world
// ran 2-3 steps in one displayed frame and none in the next: GitHub issue
// #9). Further behind, the queue ran dry and the display waited, and the
// frame gets the rest; a frame slower than the vblank (GPU-bound) gets its
// count, its length rounded either way. Game time adds up to the vblanks
// that fired, to within those few. It runs at the display's rate over 60:
// 0.044% slow on a 59.97 Hz monitor, at most 2.5% (the lock's range). Frames
// of more than 6 vblanks (a hitch, loading) keep their measured time; below
// 20 fps, where they are the frame rate's own, only those over half a second
// do, the rest get 6 (the stepper's own cap) and later frames give back what
// that leaves owed, so game time keeps up down to 10 fps. At Frame Rate 30
// (video::GuestVblankDivider) a guest vblank comes every other refresh and is
// given 2/60 s: two steps a frame, the game at its speed.
#include <stdafx.h>
#include "frame_time.h"
#include "whole_vblanks.h"
#include "mp_adapter.h"
#include <net/multiplayer_config.h>

#include <cpu/guest_time.h>
#include <kernel/function.h>
#include <user/settings.h>
#include <video/presenter.h>
#include <video/vblank_lock.h>

namespace
{
    const bool s_enabled = [] {
        const char* v = std::getenv("NFSMW_GAME_TIME");
        bool on = !v || v[0] != '0';
        if (!on)
            fprintf(stderr, "[game] game time as measured (NFSMW_GAME_TIME=0)\n");
        return on;
    }();
    std::atomic<uint64_t> s_frames{ 0 }, s_snapped{ 0 }, s_noStep{ 0 }, s_multiStep{ 0 };
    std::atomic<uint64_t> s_worldFrames{ 0 };
    std::atomic<int64_t> s_raceFrame{ -1 };
    int s_fixedSteps = 0;  // set once by game::InitFixedSteps before the guest runs, then only read

    // Game time handed to the frames so far (game::FrameClockMs), ms in 16.16.
    std::atomic<uint64_t> s_clock{ 0 };
    // NFSMW_GAME_TIME_LOG=<file>: one line per frame, written when the next
    // one starts: its measured ms, guest vblanks, whether it was given whole
    // vblanks, the world steps it ran (-1: no world), how long after the last
    // vblank it started (us), and the guest vblanks it was given (0: its
    // measured time).
    FILE* const s_log = [] {
        const char* path = std::getenv("NFSMW_GAME_TIME_LOG");
        FILE* f = path ? fopen(path, "w") : nullptr;
        if (f)
            fprintf(f, "measured_ms,vblanks,snapped,steps,after_vblank_us,given\n");
        return f;
    }();
    struct LogLine
    {
        double measuredMs = 0, afterVblankUs = 0;
        uint64_t vblanks = 0, given = 0;
        int steps = -1;
        bool snapped = false, pending = false;
    } s_line;  // main thread only
    game::WholeVblanks s_whole;  // main thread only

    // Whole vblanks are game time only at the lock's rates: NFSMW_VBLANK_HZ
    // far from 60 keeps game time as measured, in the stepper too.
    bool NearSixty(double vblankMs)
    {
        return vblankMs >= 1000.0 / video::RefreshFit::kMaxHz && vblankMs <= 1000.0 / video::RefreshFit::kMinHz;
    }
}

namespace game
{
    void InitFixedSteps()
    {
        const char* v = std::getenv("NFSMW_FIXED_STEPS");
        int n = v ? std::atoi(v) : 0;
        // Settings > Online > Online Play is the old lockstep link (two steps a frame, whatever the frame took: on a machine that draws more than 30 frames a second the
        // world then runs faster than real time). The multiplayer races (net/mp_*) need the world at real time, so they win when both are on.
        if (!v && settings::GetBool(settings::Id::OnlinePlay) && !mpcfg::Load().enabled)
            n = 2;
        n = std::clamp(n, 0, 4);
        if (!n)
            return;
        // The game's mftb follows the frame too (cpu/guest_time.h), unless NFSMW_REAL_TIMEBASE keeps the host's.
        if (!std::getenv("NFSMW_REAL_TIMEBASE"))
        {
            guesttime::g_detTicksPerFrame.store(uint64_t(guesttime::kTimebaseHz) * uint64_t(n) / 60, std::memory_order_relaxed);
            guesttime::g_detFrame.store(0, std::memory_order_relaxed);
        }
        s_fixedSteps = n;
        fprintf(stderr, "[game] fixed game time: %d world steps (%.2f ms) a frame, whatever the frame took (%s)\n", n, n * 1000.0 / 60.0,
            v ? "NFSMW_FIXED_STEPS" : "Settings > Online > Online Play");
    }

    FrameTimeStats GetFrameTimeStats()
    {
        return { s_frames.load(std::memory_order_relaxed), s_noStep.load(std::memory_order_relaxed),
            s_multiStep.load(std::memory_order_relaxed), s_snapped.load(std::memory_order_relaxed) };
    }

    std::atomic<bool> s_hold{ false };
    void SetWorldHold(bool hold) { s_hold.store(hold, std::memory_order_relaxed); }
    bool WorldHeld() { return s_hold.load(std::memory_order_relaxed); }

    int FixedSteps() { return s_fixedSteps; }
    uint64_t WorldFrameIndex() { return s_worldFrames.load(std::memory_order_relaxed); }
    int64_t RaceFrame() { return s_raceFrame.load(std::memory_order_relaxed); }

    double FrameClockMs()
    {
        return double(s_clock.load(std::memory_order_relaxed)) / 65536.0;
    }
}

// GameFrameTime, before `bl 0x823AFE20` at 0x823B0148 in the main loop: r3 is
// the frame's measured time, milliseconds in 16.16 fixed point.
void GameFrameTime(PPCRegister& r3)
{
    game::MpFramePump();
    if (game::WorldHeld())
    {
        // No time passes for the world while it is held: not in the clock, not in the stepper's accumulator.
        r3.u64 = 0;
        s_frames.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // Whatever the frame is given below (or its measured time), on every path.
    struct AddToClock
    {
        PPCRegister& r3;
        ~AddToClock() { s_clock.fetch_add(uint64_t(std::max(int32_t(r3.u32), 0)), std::memory_order_relaxed); }
    } addToClock{ r3 };
    // Multiplayer race clock (game/mp_adapter.cpp): follow the agreed clock by giving the frame a little more or less time (at most 100% more, 50% less), so a game
    // whose loading made its game time lag (or run ahead) closes the gap within a few seconds, before the cars leave the line.
    struct FollowRaceClock
    {
        PPCRegister& r3;
        ~FollowRaceClock()
        {
            const double want = game::MpWantedClock();
            if (want < 0)
                return;
            const uint32_t bits = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(g_memory.base + 0x82A153B8));
            float clockNow;
            memcpy(&clockNow, &bits, 4);
            if (!(clockNow > 0.0f) || !std::isfinite(clockNow))
                return;
            const double err = want - double(clockNow);          // seconds the world is behind (positive) or ahead
            if (std::abs(err) < 0.02 || std::abs(err) > 30.0)
                return;
            const double frameMs = double(int32_t(r3.u32)) / 65536.0;
            if (frameMs <= 0.0)
                return;
            const double extraMs = std::clamp(err * 1000.0 * 0.25, -0.5 * frameMs, 1.0 * frameMs);
            r3.u64 = uint32_t(std::llround((frameMs + extraMs) * 65536.0));
        }
    } followRaceClock{ r3 };
    static uint64_t last = 0;
    uint64_t now = video::GuestVblanks(), vblanks = now - last;
    last = now;
    s_frames.fetch_add(1, std::memory_order_relaxed);
    // The race frame (game::RaceFrame): from the world clock, read as the game left it after the last frame.
    {
        static float prevClock = 0.0f;
        static uint64_t epoch = 0;
        static bool inRace = false;
        const uint32_t bits = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(g_memory.base + 0x82A153B8));
        float clock;
        memcpy(&clock, &bits, 4);
        if (clock > 0.0f && std::isfinite(clock))
        {
            if (!inRace || clock < prevClock)  // a new race world: its clock started again
            {
                inRace = true;
                epoch = s_frames.load(std::memory_order_relaxed);
            }
            s_raceFrame.store(int64_t(s_frames.load(std::memory_order_relaxed) - epoch), std::memory_order_relaxed);
        }
        else
        {
            inRace = false;
            s_raceFrame.store(-1, std::memory_order_relaxed);
        }
        prevClock = clock;
    }
    if (guesttime::g_detFrame.load(std::memory_order_relaxed) != guesttime::kDetOff)
        guesttime::g_detFrame.fetch_add(1, std::memory_order_relaxed);
    double measuredMs = double(int32_t(r3.u32)) / 65536.0;
    if (s_log)
    {
        if (s_line.pending)
            fprintf(s_log, "%.3f,%llu,%d,%d,%.0f,%llu\n", s_line.measuredMs, (unsigned long long)s_line.vblanks, s_line.snapped,
                s_line.steps, s_line.afterVblankUs, (unsigned long long)s_line.given);
        int64_t since = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() -
            video::LastGuestVblank();
        s_line = { measuredMs, double(since) / 1e3, vblanks, 0, -1, false, true };
    }
    if (s_fixedSteps)
    {
        // Every frame is exactly n steps long: no measuring, no vblanks.
        r3.u64 = uint32_t(std::llround(double(s_fixedSteps) * (1000.0 / 60.0) * 65536.0));
        s_line.given = uint64_t(s_fixedSteps);
        return;
    }
    if (!s_enabled)
        return;
    double refreshMs = video::GuestVblank().PeriodNs() / 1e6;
    if (!NearSixty(refreshMs))
    {
        s_whole.Reset();
        return;
    }
    // At Frame Rate 30 a guest vblank is two refreshes, and two 1/60 s steps.
    int divider = video::GuestVblankDivider();
    uint64_t given = s_whole.Give(measuredMs, vblanks, refreshMs * divider);
    if (!given)
        return;
    r3.u64 = uint32_t(std::llround(double(given * divider) * (1000.0 / 60.0) * 65536.0));
    s_snapped.fetch_add(1, std::memory_order_relaxed);
    s_line.snapped = true;
    s_line.given = given;
}

// WorldStepTime, before `fadds f0,f10,f9` at 0x823A23E8 in the stepper: f10 is
// the elapsed time in steps (f9 the remainder it joins).
void WorldStepTime(PPCRegister& f10)
{
    if (s_fixedSteps)
    {
        // The elapsed time is n steps give or take the rounding of its ticks (and 0 while the game doesn't run its world).
        if (std::abs(f10.f64 - s_fixedSteps) <= 0.3)
            f10.f64 = s_fixedSteps;
        return;
    }
    if (!s_enabled || !NearSixty(video::GuestVblank().PeriodNs() / 1e6))
        return;
    double k = std::round(f10.f64);
    if (k >= 1 && k <= 6 && std::abs(f10.f64 - k) <= 0.05)
        f10.f64 = k;
}

// WorldSteps, at `mr r4,r30` (0x823A2424) in the stepper: r30 is the number of
// steps this frame runs, before the step runner's cap.
void WorldSteps(PPCRegister& r30)
{
    s_worldFrames.fetch_add(1, std::memory_order_relaxed);
    if (game::WorldHeld())
        r30.u32 = 0;
    // NFSMW_LOG_WORLD_CLOCK=1: the game's world clock (a float in seconds at 0x82A153B8, found by watching it advance in real time and matching the
    // HUD's race clock, which is this minus a constant 9.95 s) with each world step run, to see whether it advances by a fixed amount per frame.
    {
        static const bool log = std::getenv("NFSMW_LOG_WORLD_CLOCK") != nullptr;
        if (log)
        {
            const uint32_t bits = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(g_memory.base + 0x82A153B8));
            float clock;
            memcpy(&clock, &bits, 4);
            fprintf(stderr, "[wclock] frame %llu steps %u clock %.5f\n", (unsigned long long)s_worldFrames.load(), unsigned(r30.u32), double(clock));
        }
    }
    s_line.steps = int(r30.u32);
    if (r30.u32 == 0)
        s_noStep.fetch_add(1, std::memory_order_relaxed);
    else if (r30.u32 >= 2)
        s_multiStep.fetch_add(1, std::memory_order_relaxed);
}
