// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See startup_movies.cpp.
#pragma once

namespace startup_movies
{
    // True for the four movies the game plays at launch while Skip Startup Movies is on: the open of the file should fail.
    bool HideFile(const char* guestPath);
}
