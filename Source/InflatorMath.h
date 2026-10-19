#pragma once

// Pure C++ (no JUCE) so the maths can be unit-tested on any machine.

#include <algorithm>
#include <cmath>

namespace chroma
{
    inline double clampd (double v, double lo, double hi) noexcept
    {
        return std::min (hi, std::max (lo, v));
    }

    // Transparent inflator curve: a low-order odd polynomial (no even harmonics,
    // nothing above the 5th), the same family the original Inflator is built on.
    //
    //   y = x + effect * ( a * (x - x^3) + b * (x^3 - x^5) )
    //
    // Both terms are zero at |x| = 1, so full-scale peaks are never moved or
    // crushed; only the body of the waveform is lifted. a + b <= 0.5 keeps the
    // curve monotonic at any Effect setting. Curve (-1..+1) morphs a -> b:
    // negative lifts the whole level range (denser, gentler), positive leaves
    // quiet material untouched and works on the upper-mid levels. Beyond +/-1 the curve
    // continues with a continuous slope (see below).
    inline double inflate (double x, double effect, double curve) noexcept
    {
        const double xc = clampd (x, -1.0, 1.0);
        const double t  = 0.5 * (curve + 1.0);          // 0 .. 1
        const double a  = 0.5 * (1.0 - t);
        const double b  = 0.5 * t;

        const double x2 = xc * xc;
        const double x3 = x2 * xc;
        const double x5 = x3 * x2;

        const double shaped = xc + effect * (a * (xc - x3) + b * (x3 - x5));

        const double over = std::abs (x) - 1.0;
        if (over <= 0.0)
            return shaped;

        // Above full scale the curve continues smoothly. Its slope at |x| = 1 is (1 - effect), and it eases
        // back to slope 1 with tanh, so the slope never jumps (no kink -> no extra harsh harmonics when the
        // input is driven hot). Value at |x| = 1 is exactly 1, so peaks at or below 0 dBFS are untouched.
        const double k    = 2.0 * effect * (a + b);          // == effect, since a + b == 0.5
        const double tail = 1.0 + over - k * std::tanh (over);
        return x < 0.0 ? -tail : tail;
    }

    // Final stage: Output gain, then (if Clip is on) a hard ceiling at 0 dBFS.
    inline double outputStage (double y, double gain, bool clip) noexcept
    {
        y *= gain;
        return clip ? clampd (y, -1.0, 1.0) : y;
    }

    inline double mix (double a, double b, double t) noexcept
    {
        return a * (1.0 - t) + b * t;
    }

    // Gentle clipper: linear up to a knee, then eases into +/-1 with tanh. Never exceeds 1, no hard corner.
    inline double softClip (double x) noexcept
    {
        constexpr double knee = 0.7;
        const double a = std::abs (x);
        if (a <= knee)
            return x;

        const double s = knee + (1.0 - knee) * std::tanh ((a - knee) / (1.0 - knee));
        return x < 0.0 ? -s : s;
    }

    // Output stage with Clip as a 0..1 crossfade (so toggling never steps) and Clip Mode as a second
    // 0..1 crossfade between the hard ceiling (0) and the soft clipper (1).
    inline double outputStageMixMode (double y, double gain, double clipMix, double softMix) noexcept
    {
        y *= gain;
        if (clipMix <= 0.0)
            return y;

        const double hard    = clampd (y, -1.0, 1.0);
        const double clipped = softMix <= 0.0 ? hard : mix (hard, softClip (y), softMix);
        return mix (y, clipped, clipMix);
    }

    // clipMix == 0 is exactly outputStage (.., false); clipMix == 1 is exactly (.., true).
    inline double outputStageMix (double y, double gain, double clipMix) noexcept
    {
        return outputStageMixMode (y, gain, clipMix, 0.0);
    }
}
