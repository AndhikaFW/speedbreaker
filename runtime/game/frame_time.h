// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Game time in whole vblanks (frame_time.cpp): each guest vblank is one world
// step, at any display rate the vblank locks to.
#pragma once
#include <cstdint>

namespace game
{
    // Since launch, for the [perf] line: game frames (main loop iterations),
    // those whose world ran no step or two and more, and those given whole
    // vblanks of time.
    struct FrameTimeStats
    {
        uint64_t frames, noStep, multiStep, snapped;
    };
    FrameTimeStats GetFrameTimeStats();

    // The game time the main loop has handed its frames since launch, in ms:
    // each frame's measured time, or the whole vblanks GameFrameTime gives it
    // (1/60 s each). It moves once a frame, at the frame's start, paused or
    // not; the game draws the frame after (the nitrous trails, game/light_trails.h).
    double FrameClockMs();

    // Steps the world runs in every game frame when NFSMW_FIXED_STEPS=<n> is set (0: off, the default): the frame is given exactly n/60 s,
    // whatever it took, so the world's state depends only on the frame count and the inputs, not on timing (lockstep play needs that).
    int FixedSteps();
    // The race's own frame count for lockstep play (net/netplay.h): -1 outside a race, else the number of main-loop frames since the world clock (a
    // float in seconds at 0x82A153B8: 0 in the front end, starting to count when a race world is created) first read above 0. The same on two
    // machines that run the same race, whatever their loading times. Valid with NFSMW_FIXED_STEPS.
    int64_t RaceFrame();
    // Frames the world stepper has run so far (calls of the stepper since launch: the front end has no world, a race does), for
    // inputs and captures keyed to the race's own clock.
    uint64_t WorldFrameIndex();
}
