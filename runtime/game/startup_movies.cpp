// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Skipping the movies at launch (Settings > Advanced > Skip Startup Movies).
//
// The game plays eahd_bumper, ealogo and psa, then loads the title screen, which plays attract_movie; pressing A skips a
// movie (and on the title screen takes the player to the main menu). So from the first of those movies until the attract
// movie has started and then stopped, A is pulsed on top of the pad while the XMV player is rendering frames; it is the
// player's own input, so the game runs its normal path (no files are missing, no state is faked). After that the later
// movies (tutorials, story videos) are left alone. Nothing is pressed if no startup movie opens within a minute of the
// first one (a fallback, so a changed boot sequence can't leave the button held down).
#include <stdafx.h>

#include "startup_movies.h"

#include <user/settings.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <strings.h>

namespace
{
    int64_t NowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    constexpr int64_t kFallbackMs = 60000;      // give up this long after the first startup movie opened
    constexpr int64_t kIdleAfterAttractMs = 1500;  // no movie frame for this long after the attract movie: the title is up, done
    constexpr int64_t kPulsePeriodMs = 300, kPulseHoldMs = 120;

    std::atomic<int64_t> s_firstOpen{ 0 };     // when the first startup movie opened (0: none yet)
    std::atomic<int64_t> s_lastFrame{ 0 };     // the last movie frame rendered
    std::atomic<bool> s_attractSeen{ false };
    std::atomic<bool> s_done{ false };

    bool Contains(const char* text, const char* part)
    {
        return strcasestr(text, part) != nullptr;
    }
}

namespace startup_movies
{
    void NoteOpen(const char* guestPath)
    {
        if (s_done.load(std::memory_order_relaxed) || !guestPath || !Contains(guestPath, "\\movies\\"))
            return;
        const bool attract = Contains(guestPath, "attract_movie");
        if (!attract && !Contains(guestPath, "eahd_bumper") && !Contains(guestPath, "ealogo") && !Contains(guestPath, "psa_"))
            return;
        int64_t none = 0;
        s_firstOpen.compare_exchange_strong(none, NowMs());
        if (attract)
            s_attractSeen.store(true, std::memory_order_relaxed);
    }

    void NoteMovieFrame()
    {
        s_lastFrame.store(NowMs(), std::memory_order_relaxed);
    }

    bool WantA()
    {
        const int64_t first = s_firstOpen.load(std::memory_order_relaxed);
        if (first == 0 || s_done.load(std::memory_order_relaxed))
            return false;
        if (!settings::GetBool(settings::Id::SkipStartupMovies))
            return false;
        const int64_t now = NowMs(), last = s_lastFrame.load(std::memory_order_relaxed);
        if (now - first > kFallbackMs || (s_attractSeen.load(std::memory_order_relaxed) && last != 0 && now - last > kIdleAfterAttractMs))
        {
            s_done.store(true, std::memory_order_relaxed);
            return false;
        }
        // Only while a movie is on the screen: a pulse that lands on a loading screen would do nothing, and one on a menu
        // would act.
        if (last == 0 || now - last > 400)
            return false;
        return (now - first) % kPulsePeriodMs < kPulseHoldMs;
    }
}
