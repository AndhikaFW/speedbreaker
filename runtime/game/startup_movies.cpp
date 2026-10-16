// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Skipping the movies at launch (Settings > Advanced > Skip Startup Movies).
//
// The game plays eahd_bumper, ealogo and psa, then the title screen's attract_movie. A movie is started by sub_8245DBF8
// (r3: the movie's name): it opens the file with the XMV player (sub_826D74B8) and, when that fails, does what it does for a
// movie that is over: it sends the "movie finished" event (0xC3A00EB9) to the screens listening, and the front end goes on to
// its next screen. So the open of these four files reports "not found" (kernel/imports_file.cpp), and the game takes its own
// path for a movie that has ended. No button is pressed and no frame is dropped; the other movies (the tutorials, the career's
// story videos, the blacklist rivals) are not touched.
//
// Two things that were tried and do not work: failing the XMV open itself (sub_826D74B8 returning an error, or being given a
// path with no such drive) leaves the front end waiting on a black screen: it needs the player's own failure from inside the
// file open; and pressing A skips the attract movie but not the three logos (they play to the end).
#include <stdafx.h>

#include "startup_movies.h"

#include <user/settings.h>

#include <strings.h>

namespace startup_movies
{
    bool HideFile(const char* guestPath)
    {
        static const char* const kNames[] = { "eahd_bumper", "ealogo", "psa_", "attract_movie" };
        if (!guestPath || !strcasestr(guestPath, "\\movies\\") || !settings::GetBool(settings::Id::SkipStartupMovies))
            return false;
        for (const char* name : kNames)
            if (strcasestr(guestPath, name))
                return true;
        return false;
    }
}
