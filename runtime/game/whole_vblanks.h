// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// How GameFrameTime (frame_time.cpp) gives game frames whole guest vblanks
// of time, apart so tests/whole_vblanks_test.cpp can check it.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace game
{
    // Game time follows the guest vblanks that fire: over any run of frames
    // it adds up to them, to within kSlack vblanks, and each frame that saw
    // any gets whole ones (1 to 6), so the fixed 1/60 s world step runs a
    // whole number of times a frame.
    //   - A frame the vblank paces (60 fps held, or 30 at Frame Rate 30) saw
    //     one and gets one.
    //   - The main loop runs up to a few frames ahead of the display (its
    //     render thread and the GPU ring queue them), so its own frames
    //     don't line up with the vblanks: a frame the loop is slow with
    //     (streaming, a file read, a page fault) spans 2 vblanks and the next
    //     one, catching up, none, while the display still shows one frame a
    //     vblank from the queue. Each of those frames gets one: game time may
    //     run up to kSlack vblanks behind or ahead of the vblanks that fired.
    //     Until 2026-10-11 they got their own counts (2, then the measured
    //     few ms): the world ran 2 steps in a frame the display showed for
    //     one vblank and none in the next, a hitch every few seconds while
    //     driving that no frame-time graph shows (GitHub issue #9).
    //   - Behind by more than that, the queue ran dry and the display waited
    //     (a frame shown twice): the frame gets the vblanks beyond kSlack,
    //     at most 6, so game time keeps up with the display.
    //   - A frame it doesn't pace (GPU-bound) starts anywhere between two,
    //     so its count is its length rounded down or up: a 44 ms frame (2.64
    //     refreshes) sees 2 or 3, and once game time is kSlack behind (the
    //     first such frame), gets them.
    //   - Ahead by kSlack (more frames than vblanks for a while: the queue
    //     filling after a load), a frame no vblank fired in keeps its
    //     measured time.
    // The rule until 2026-10-07 gave a frame its count only when its
    // measured time was within half a vblank of it, else the measured time:
    // GPU-bound 44 ms frames got 3 vblanks or their own 2.64, so the game ran
    // fast (3-6% on an Intel laptop GPU), and at Frame Rate 30 (44-49 ms
    // frames, 1 or 2 guest vblanks of 33.3 ms) 9-15% slow; a late vblank
    // thread's 0 and 2 counts around 18 ms frames (the Mac at 3x3) ran it
    // 6-9% slow.
    struct WholeVblanks
    {
        // Game time given beyond the vblanks that fired, in vblanks (below 0:
        // behind), within +-kSlack: what can't be given back is forgotten (a
        // held vblank, as a GPU-only suspension's, isn't replayed).
        double ahead = 0.0;
        // How far the main loop's frames may drift from the vblanks without
        // the display showing it: the frames queued between the loop and the
        // display (2-3, measured on the Steam Machine).
        static constexpr double kSlack = 3.0;

        // A frame the main loop measured at `measuredMs`, during which
        // `vblanks` guest vblanks fired (from its start to the next frame's),
        // each `vblankMs` long: the vblanks to give it, or 0 to keep its
        // measured time. More than 6 (loading) keeps it too, and starts over.
        uint64_t Give(double measuredMs, uint64_t vblanks, double vblankMs)
        {
            if (vblanks > 6 || !(vblankMs > 0.0))
            {
                ahead = 0.0;
                return 0;
            }
            ahead -= double(vblanks);
            if (ahead + 1.0 > kSlack)
            {
                ahead = std::min(ahead + std::max(measuredMs, 0.0) / vblankMs, kSlack);
                return 0;
            }
            int64_t give = 1;
            if (ahead + 1.0 < -kSlack)
                give = std::clamp<int64_t>(std::llround(-kSlack - ahead), 1, 6);
            ahead = std::max(ahead + double(give), -kSlack);
            return uint64_t(give);
        }

        // Game time measured again (NFSMW_GAME_TIME=0, a rate far from 60).
        void Reset() { ahead = 0.0; }
    };
}
