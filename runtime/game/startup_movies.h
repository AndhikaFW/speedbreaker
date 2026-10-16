// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See startup_movies.cpp.
#pragma once

namespace startup_movies
{
    // A guest file was opened (the path as the game wrote it): the startup movies are recognised by name.
    void NoteOpen(const char* guestPath);
    // The XMV player rendered a movie frame.
    void NoteMovieFrame();
    // True while the A button should be held down on top of the pad: a pulse, during the startup movies.
    bool WantA();
}
