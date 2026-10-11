// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Post-processing that stays on its side of the 16:9 band's edge
// (video/presenter.cpp's anti-aliasing and scaling shaders do this in GLSL,
// line for line; tests/band_filter_test.cpp checks it here).
//
// Under Vert+ and Hor+ the frame holds the 16:9 band (video::BandOf) and the
// strips past it, and what the game draws often ends exactly at the band's
// edge: the movie player letterboxes (or pillarboxes) a movie into the band
// (game/ultrawide.cpp tells it the screen's aspect), and the edge-bar fills
// black out the strips (gpu/edge_bars.h). Both filters read across that
// edge: the anti-aliasing (an FXAA-style pass at the frame's resolution)
// blended the strip's last row towards the band's first, by up to half on a
// textured edge, and the scaling (Catmull-Rom from bilinear taps) a few per
// cent more; the career intro's movie (storyfmv_rac01) showed as a faint row
// just past each band edge (max 40-58 of 255 at 1280x800, the frame's own
// rows exactly black). With this each pixel's taps are kept to its own span
// of the three ([0, band start), the band, [band end, size)) along each
// axis, as if that span were the whole image (clamp to edge): a strip pixel
// reads only the strip, a band pixel only the band.
//
// Units: texels of the frame image, 0 at its edge, texel i covering
// [i, i + 1) with its centre at i + 0.5.
#pragma once
#include <algorithm>
#include <cstdint>

namespace video::band
{
    // The span along one axis holding coordinate t: [0, lo), [lo, hi) or
    // [hi, size); its first texel `a` and end `b`.
    struct Span
    {
        int32_t a, b;
    };

    inline Span SpanOf(float t, int32_t lo, int32_t hi, int32_t size)
    {
        if (t < float(lo))
            return { 0, lo };
        if (t < float(hi))
            return { lo, hi };
        return { hi, size };
    }

    // A sample coordinate kept between the span's first and last texel
    // centres: a bilinear fetch there weighs only that span's texels.
    inline float Clamp(float t, Span s)
    {
        return std::clamp(t, float(s.a) + 0.5f, float(s.b) - 0.5f);
    }

    // A texel index kept in the span (a gather's 2x2: the row or column
    // past the span's edge reads its edge one instead).
    inline int32_t ClampTexel(int32_t i, Span s)
    {
        return std::clamp(i, s.a, s.b - 1);
    }
}
