// JUCE-free unit tests for the maths in Source/InflatorMath.h and Source/ChromaHelpers.h
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I Source Tests/dsp_test.cpp -o dsp_test && ./dsp_test
//
// Exit code is 0 only if every check passes (CI relies on that).

#include "InflatorMath.h"
#include "ChromaHelpers.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

namespace
{
    int failures = 0, checks = 0;

    void check (bool ok, const char* what)
    {
        ++checks;
        if (! ok)
        {
            ++failures;
            std::printf ("  FAIL: %s\n", what);
        }
    }

    constexpr double pi = 3.14159265358979323846;

    // Plain radix-2 FFT, enough for these tests.
    void fft (std::vector<std::complex<double>>& a)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (a[i], a[j]);
        }

        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = -2.0 * pi / (double) len;
            const std::complex<double> wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1.0, 0.0);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = a[i + k];
                    const auto v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }

    // Magnitude spectrum, normalised so a sine of amplitude A reads A.
    std::vector<double> spectrum (const std::vector<double>& x)
    {
        std::vector<std::complex<double>> a (x.begin(), x.end());
        fft (a);

        std::vector<double> mag (a.size() / 2);
        for (size_t k = 0; k < mag.size(); ++k)
            mag[k] = 2.0 * std::abs (a[k]) / (double) x.size();
        return mag;
    }

    //==========================================================================
    void testShapingCurve()
    {
        std::printf ("shaping curve\n");

        // Identity at Effect 0, and peaks never move at any setting.
        bool identity = true, pinned = true, odd = true;
        for (double c = -1.0; c <= 1.0001; c += 0.25)
        {
            for (double x = -1.5; x <= 1.5001; x += 0.01)
                identity = identity && (chroma::inflate (x, 0.0, c) == x);

            for (double e = 0.0; e <= 1.0001; e += 0.1)
            {
                pinned = pinned && chroma::inflate (1.0, e, c) == 1.0 && chroma::inflate (-1.0, e, c) == -1.0;

                for (double x = 0.0; x <= 1.0001; x += 0.01)
                    odd = odd && std::abs (chroma::inflate (-x, e, c) + chroma::inflate (x, e, c)) < 1.0e-15;
            }
        }
        check (identity, "Effect 0 is an exact identity");
        check (pinned,   "full-scale peaks are never moved");
        check (odd,      "curve is odd-symmetric");

        // Monotonic at Effect 100% for every Curve value.
        bool mono = true;
        for (double c = -1.0; c <= 1.0001; c += 0.1)
        {
            double prev = chroma::inflate (-1.0, 1.0, c);
            for (double x = -1.0 + 1.0e-4; x <= 1.0; x += 1.0e-4)
            {
                const double y = chroma::inflate (x, 1.0, c);
                mono = mono && y >= prev - 1.0e-12;
                prev = y;
            }
        }
        check (mono, "curve is monotonic at Effect 100%");

        // Beyond +/-1 the curve continues with a continuous value AND slope (no kink at full scale), is
        // monotonic, odd, and eases back towards slope 1 for hot signals.
        bool cont = true, slopeOk = true, monoOver = true, oddOver = true;
        for (double c = -1.0; c <= 1.0001; c += 0.25)
        {
            for (double e = 0.1; e <= 1.0001; e += 0.3)
            {
                const double h = 1.0e-6;
                const double yIn  = chroma::inflate (1.0, e, c), yOut = chroma::inflate (1.0 + h, e, c);
                cont = cont && std::abs (yOut - yIn) < 3.0 * h;

                const double slopeIn  = (chroma::inflate (1.0, e, c) - chroma::inflate (1.0 - h, e, c)) / h;
                const double slopeOut = (chroma::inflate (1.0 + h, e, c) - chroma::inflate (1.0, e, c)) / h;
                slopeOk = slopeOk && std::abs (slopeIn - slopeOut) < 1.0e-3;

                double prev = chroma::inflate (1.0, e, c);
                for (double x = 1.0; x <= 4.0; x += 0.001)
                {
                    const double y = chroma::inflate (x, e, c);
                    monoOver = monoOver && y >= prev - 1.0e-12;
                    prev = y;
                    oddOver = oddOver && std::abs (chroma::inflate (-x, e, c) + y) < 1.0e-12;
                }
            }
        }
        check (cont,     "value is continuous at |x| = 1");
        check (slopeOk,  "slope is continuous at |x| = 1 (no kink above full scale)");
        check (monoOver, "curve stays monotonic above full scale");
        check (oddOver,  "curve stays odd-symmetric above full scale");
        check (std::abs ((chroma::inflate (6.0, 1.0, 0.3) - chroma::inflate (5.0, 1.0, 0.3)) - 1.0) < 1.0e-3,
               "very hot signals return to slope 1");
        check (std::abs (chroma::inflate (1.7, 0.0, 0.3) - 1.7) < 1.0e-12, "Effect 0 passes overs through unchanged");
    }

    //==========================================================================
    void testHarmonics()
    {
        std::printf ("harmonic content\n");

        const int N = 4096, k0 = 427, M = N * 8;   // 8x oversampled capture of the same N-sample window

        for (double curve : { -1.0, 0.0, 1.0 })
        {
            std::vector<double> y ((size_t) M);
            for (int m = 0; m < M; ++m)
                y[(size_t) m] = chroma::inflate (0.9 * std::sin (2.0 * pi * k0 * m / M), 1.0, curve);

            const auto mag = spectrum (y);

            bool evenClean = true, highClean = true;
            for (int h = 2; h * k0 < M / 2; ++h)
            {
                const double v = mag[(size_t) (h * k0)];
                if (h % 2 == 0)  evenClean = evenClean && v < 1.0e-9;
                if (h > 5)       highClean = highClean && v < 1.0e-9;
            }
            check (evenClean, "no even harmonics");
            check (highClean, "nothing above the 5th harmonic");
            check (mag[(size_t) (3 * k0)] > 1.0e-4 || curve > 0.99, "3rd harmonic present where expected");
        }
    }

    //==========================================================================
    void testAliasing()
    {
        std::printf ("aliasing at 1x vs 8x\n");

        // Base rate window of N samples; the 5th harmonic (bin 2135) is above Nyquist (bin 2048)
        // and folds back to bin 4096 - 2135 = 1961 when processed at 1x.
        const int N = 4096, k0 = 427, aliasBin = N - 5 * k0;

        std::vector<double> x1 ((size_t) N);
        for (int n = 0; n < N; ++n)
            x1[(size_t) n] = chroma::inflate (0.9 * std::sin (2.0 * pi * k0 * n / N), 1.0, 1.0);

        const double alias1x = spectrum (x1)[(size_t) aliasBin];

        std::vector<double> x8 ((size_t) N * 8);
        for (int m = 0; m < N * 8; ++m)
            x8[(size_t) m] = chroma::inflate (0.9 * std::sin (2.0 * pi * k0 * m / (N * 8)), 1.0, 1.0);

        const auto mag8 = spectrum (x8);
        const double alias8x = mag8[(size_t) aliasBin];

        std::printf ("  alias bin level: 1x = %.2e, 8x = %.2e\n", alias1x, alias8x);
        check (alias1x > 1.0e-3, "1x processing aliases (sanity check of the test itself)");
        check (alias8x < 1.0e-9, "8x processing is alias-free inside the base band");
        check (mag8[(size_t) (5 * k0)] > 1.0e-3, "the 5th harmonic really exists at 8x (and is filtered on the way down)");
    }

    //==========================================================================
    void testClip()
    {
        std::printf ("clip ceiling\n");

        bool hardOk = true, mixEnds = true, mixBounded = true;
        for (double gain : { 0.5, 1.0, 4.0, 15.85 })
            for (double y = -2.0; y <= 2.0001; y += 0.01)
            {
                hardOk  = hardOk  && std::abs (chroma::outputStage (y, gain, true)) <= 1.0;
                mixEnds = mixEnds && chroma::outputStageMix (y, gain, 0.0) == chroma::outputStage (y, gain, false)
                                  && chroma::outputStageMix (y, gain, 1.0) == chroma::outputStage (y, gain, true);

                // Part-way through a fade the result must stay between the unclipped and clipped values.
                const double unclipped = y * gain, clipped = chroma::clampd (unclipped, -1.0, 1.0);
                const double mid = chroma::outputStageMix (y, gain, 0.4);
                mixBounded = mixBounded && mid >= std::min (unclipped, clipped) - 1.0e-12
                                        && mid <= std::max (unclipped, clipped) + 1.0e-12;
            }

        check (hardOk,     "Clip on never exceeds 0 dBFS");
        check (mixEnds,    "ramped clip stage equals the old switch at both ends");
        check (mixBounded, "ramped clip stage stays between unclipped and clipped");
    }

    //==========================================================================
    void testMixRamp()
    {
        std::printf ("MixRamp\n");

        const double fs = 48000.0;
        chroma::MixRamp r;
        r.prepare (fs, 0.015);   // 720 samples
        r.snap (false);
        r.setTarget (true);

        int steps = 0;
        float prev = r.value, maxStep = 0.0f;
        bool monotonic = true;
        while (r.isRamping() && steps < 100000)
        {
            const float v = r.next();
            monotonic = monotonic && v >= prev;
            maxStep = std::max (maxStep, v - prev);
            prev = v;
            ++steps;
        }

        check (steps >= 719 && steps <= 722, "ramp takes the requested time (15 ms)");
        check (monotonic, "ramp is monotonic");
        check (maxStep <= 1.0f / 720.0f + 1.0e-6f, "no per-sample step larger than 1/N");
        check (r.value == 1.0f && ! r.isRamping(), "ramp lands exactly on the target");

        // Reversing mid-ramp must not jump.
        chroma::MixRamp q;
        q.prepare (fs, 0.015);
        q.snap (false);
        q.setTarget (true);
        for (int i = 0; i < 300; ++i) q.next();
        const float before = q.value;
        q.setTarget (false);
        const float after = q.next();
        check (std::abs (after - before) <= q.step + 1.0e-7f, "reversing mid-ramp does not jump");
    }

    // The headline test: how big is the biggest sample-to-sample jump when a switch is flipped
    // while audio is playing -- a hard switch versus the crossfade the plugin now uses.
    void testSwitchingIsClickFree()
    {
        std::printf ("switching while playing\n");

        const double fs = 48000.0;
        const int n = 48000;
        const int flipAt = 20000;
        const double freq = 100.0;
        const double effect = 1.0, curve = -1.0;   // strongest setting: Effect 100%

        auto sine = [&] (int i) { return 0.8 * std::sin (2.0 * pi * freq * i / fs); };

        auto maxJump = [&] (auto&& render)
        {
            double worst = 0.0, prev = render (0);
            for (int i = 1; i < n; ++i)
            {
                const double y = render (i);
                worst = std::max (worst, std::abs (y - prev));
                prev = y;
            }
            return worst;
        };

        // Natural steepness of the signal itself (what "no click" should look like). With Effect at 100%
        // the processed signal is legitimately steeper than the dry one (slope 1.5x at the zero crossing),
        // so "no click" means: no step beyond the steepest of the two signals being crossfaded.
        const double naturalDry = maxJump ([&] (int i) { return sine (i); });
        const double naturalWet = maxJump ([&] (int i) { return chroma::inflate (sine (i), effect, curve); });
        const double natural    = std::max (naturalDry, naturalWet);

        // Old behaviour: EFFECT button flips instantly.
        const double hard = maxJump ([&] (int i)
        {
            const double x = sine (i);
            return i < flipAt ? x : chroma::inflate (x, effect, curve);
        });

        // New behaviour: 15 ms crossfade.
        chroma::MixRamp ramp;
        ramp.prepare (fs, 0.015);
        ramp.snap (false);
        const double soft = maxJump ([&] (int i)
        {
            if (i == flipAt)
                ramp.setTarget (true);
            const double x = sine (i);
            return chroma::mix (x, chroma::inflate (x, effect, curve), ramp.next());
        });

        std::printf ("  largest sample-to-sample jump: natural %.4f | hard switch %.4f | crossfade %.4f\n",
                     natural, hard, soft);

        check (hard > 5.0 * natural,  "test is meaningful: a hard switch creates a large step");
        check (soft < 1.1 * natural,  "crossfaded switch adds no step beyond the signal's own steepness");

        // Same idea for Clip with a hot signal (+6 dB over the ceiling).
        const double hot = 2.0;
        auto hotSine = [&] (int i) { return hot * std::sin (2.0 * pi * freq * i / fs); };
        const double hotNatural = maxJump ([&] (int i) { return hotSine (i); });
        const double clipHard = maxJump ([&] (int i)
        {
            return chroma::outputStage (hotSine (i), 1.0, i >= flipAt);
        });

        chroma::MixRamp clipRamp;
        clipRamp.prepare (fs, 0.015);
        clipRamp.snap (false);
        const double clipSoft = maxJump ([&] (int i)
        {
            if (i == flipAt)
                clipRamp.setTarget (true);
            return chroma::outputStageMix (hotSine (i), 1.0, clipRamp.next());
        });

        std::printf ("  Clip on a hot signal: natural %.4f | hard switch %.4f | crossfade %.4f\n",
                     hotNatural, clipHard, clipSoft);
        check (clipHard > 3.0 * hotNatural, "Clip test is meaningful: a hard switch steps");
        check (clipSoft < 1.2 * hotNatural, "ramped Clip adds no step");
    }

    //==========================================================================
    void testDelayRing()
    {
        std::printf ("DelayRing\n");

        bool exact = true;
        for (int delay : { 0, 1, 7, 63, 64, 200 })
        {
            chroma::DelayRing d;
            d.prepare (256);

            int seen = -1;
            for (int i = 0; i < 400; ++i)
                if (d.process (i == 10 ? 1.0f : 0.0f, delay) == 1.0f)
                    seen = i;

            exact = exact && seen == 10 + delay;
        }
        check (exact, "impulse emerges after exactly `delay` samples");

        chroma::DelayRing big;
        big.prepare (16);
        bool clamped = true;
        for (int i = 0; i < 64; ++i)
            clamped = clamped && std::isfinite (big.process ((float) i, 100000));
        check (clamped, "an oversized delay is clamped, never reads out of range");
    }


    //==========================================================================
    void testSoftClip()
    {
        std::printf ("soft clip\n");

        bool bounded = true, ident = true, mono = true, odd = true;
        double prev = chroma::softClip (-20.0);
        for (double x = -20.0; x <= 20.0; x += 0.001)
        {
            const double y = chroma::softClip (x);
            bounded = bounded && std::abs (y) <= 1.0;
            ident   = ident   && (std::abs (x) > 0.7 || y == x);
            mono    = mono    && y >= prev - 1.0e-12;
            odd     = odd     && std::abs (chroma::softClip (-x) + y) < 1.0e-12;
            prev = y;
        }
        check (bounded, "soft clip never exceeds 0 dBFS");
        check (ident,   "soft clip leaves everything below the knee untouched");
        check (mono,    "soft clip is monotonic");
        check (odd,     "soft clip is odd-symmetric");

        const double h = 1.0e-6;
        const double sIn = (chroma::softClip (0.7) - chroma::softClip (0.7 - h)) / h;
        const double sOut = (chroma::softClip (0.7 + h) - chroma::softClip (0.7)) / h;
        check (std::abs (sIn - sOut) < 1.0e-3, "no corner at the knee");

        // Clip Mode crossfade is exact at both ends, and Clip off ignores the mode.
        bool ends = true;
        for (double y = -3.0; y <= 3.0; y += 0.07)
        {
            ends = ends && chroma::outputStageMixMode (y, 1.0, 1.0, 0.0) == chroma::outputStage (y, 1.0, true)
                        && std::abs (chroma::outputStageMixMode (y, 1.0, 1.0, 1.0) - chroma::softClip (y)) < 1.0e-12
                        && chroma::outputStageMixMode (y, 1.0, 0.0, 1.0) == y;
        }
        check (ends, "Clip Mode: hard and soft ends are exact; Clip off passes through");
    }

    //==========================================================================
    void testGainMatch()
    {
        std::printf ("gain match\n");

        const double sr = 48000.0;
        auto run = [&] (double wetFactor)
        {
            chroma::GainMatcher gm;
            gm.prepare (sr);
            double applied = 1.0;
            for (int blk = 0; blk < (int) (sr * 20.0 / 512.0); ++blk)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const double t = (double) (blk * 512 + i) / sr;
                    const double dry = 0.3 * std::sin (2.0 * pi * 220.0 * t);
                    const double wet = dry * wetFactor * applied;       // what the chain produces
                    gm.accumulate (dry * dry, (wet * wet) / (applied * applied));
                }
                applied = gm.update (512);
            }
            return applied;
        };

        const double g2 = run (2.0), g05 = run (0.5), g1 = run (1.0);
        std::printf ("  wet 2x louder -> gain %.3f (want 0.500) | wet 0.5x -> %.3f (want 2.000) | equal -> %.3f\n", g2, g05, g1);
        check (std::abs (g2 - 0.5) < 0.01,  "gain match brings a 2x louder signal back to the dry level");
        check (std::abs (g05 - 2.0) < 0.02, "gain match lifts a quieter signal back to the dry level");
        check (std::abs (g1 - 1.0) < 0.01,  "gain match is unity when levels already agree");

        // Silence must never push the gain around, and the gain is always limited to +/-24 dB.
        chroma::GainMatcher quiet;
        quiet.prepare (sr);
        for (int i = 0; i < 48000; ++i) quiet.accumulate (0.0, 0.0);
        check (quiet.update (512) == 1.0, "silence leaves the gain alone");

        chroma::GainMatcher extreme;
        extreme.prepare (sr);
        for (int blk = 0; blk < 4000; ++blk)
        {
            for (int i = 0; i < 512; ++i) extreme.accumulate (1.0, 1.0e-6);
            extreme.update (512);
        }
        check (extreme.gain <= chroma::GainMatcher::maxGain + 1.0e-9, "gain is capped at +24 dB");
        check (std::abs (20.0 * std::log10 (chroma::GainMatcher::maxGain) - 24.0) < 1.0e-6, "gain match range is +/-24 dB");

        // End to end, with the same bookkeeping as the plugin (the output gain, including the match gain, is divided
        // out of the measurement): the long-run output level must equal the dry level times the Output setting,
        // for quiet and for hot Input gains, and with the hard 0 dB clip on.
        struct Case { double inDb, effect, curve, outDb; bool clip; };
        const Case cases[] = { { 0.0, 0.4, -0.3, 0.0, false }, { 6.0, 0.8, -0.3, 0.0, false },
                               { 18.0, 1.0, 0.3, 0.0, false }, { -12.0, 0.6, -0.3, -3.0, false },
                               { 6.0, 0.7, -0.3, 0.0, true } };
        double worst = 0.0;
        for (const auto& c : cases)
        {
            chroma::GainMatcher gm;
            gm.prepare (sr);
            const double ig = std::pow (10.0, c.inDb / 20.0), og = std::pow (10.0, c.outDb / 20.0);
            double lp = 0.0, eDry = 0.0, eOut = 0.0, seed = 12345.0;

            for (int blk = 0; blk < (int) (sr * 24.0 / 512.0); ++blk)
            {
                const double applied = gm.gain, ref = og * applied, inv = 1.0 / (ref * ref);
                for (int i = 0; i < 512; ++i)
                {
                    seed = std::fmod (seed * 16807.0, 2147483647.0);
                    lp += 0.1 * ((seed / 2147483647.0 - 0.5) * 3.4 - lp);              // coloured noise
                    const double dry = 0.4 * lp * (((blk * 512 + i) / (int) (sr * 4.0)) % 2 ? 0.5 : 1.0);   // loud / quiet sections
                    double y = chroma::inflate (dry * ig, c.effect, c.curve) * ref;
                    if (c.clip) y = std::max (-1.0, std::min (1.0, y));

                    gm.accumulate (dry * dry, y * y * inv);
                    if (blk * 512 > (int) (sr * 12.0)) { eDry += dry * dry * og * og; eOut += y * y; }
                }
                gm.update (512);
            }
            worst = std::max (worst, std::abs (10.0 * std::log10 (eOut / eDry)));
        }
        std::printf ("  end-to-end loudness error, worst of %d cases: %.2f dB\n", (int) (sizeof (cases) / sizeof (cases[0])), worst);
        check (worst < 0.5, "gain match keeps the output within 0.5 dB of the dry level (incl. +18 dB Input and Clip on)");
    }

    //==========================================================================
    void testMeterScale()
    {
        std::printf ("meter scale\n");

        // The meter is 48 dB tall, so its 6 dB lines fall exactly on the fader's 8 divisions (top = +6 dB).
        bool onTicks = true;
        for (int i = 0; i <= 8; ++i)
        {
            const float db = chroma::meterTopDb - 6.0f * (float) i;
            const float fractionFromTop = 1.0f - chroma::meterFraction (db);
            onTicks = onTicks && std::abs (fractionFromTop - (float) i / 8.0f) < 1.0e-6f;
        }
        check (onTicks, "meter 6 dB lines sit level with the 8 fader ticks");
        check (chroma::meterFraction (0.0f) == 7.0f / 8.0f, "0 dBFS is one tick below the top");
        check (chroma::meterFraction (-100.0f) == 0.0f && chroma::meterFraction (20.0f) == 1.0f, "out-of-range levels clamp to the ends");
    }

    //==========================================================================
    // A tiny Linkwitz-Riley (4th order) crossover, to prove the 3-band topology used in the plugin
    // (low -> allpass at the HIGH crossover, mid, high) sums back flat at any crossover setting.
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    Biquad butter2 (double fc, double sr, bool high)
    {
        const double K = std::tan (pi * fc / sr), q = std::sqrt (0.5);
        const double norm = 1.0 / (1.0 + K / q + K * K);
        Biquad b;
        b.b0 = (high ? 1.0 : K * K) * norm;
        b.b1 = (high ? -2.0 : 2.0) * b.b0;
        b.b2 = b.b0;
        b.a1 = 2.0 * (K * K - 1.0) * norm;
        b.a2 = (1.0 - K / q + K * K) * norm;
        return b;
    }

    struct LR4
    {
        LR4 (double fc, double sr) : lp1 (butter2 (fc, sr, false)), lp2 (butter2 (fc, sr, false)),
                                     hp1 (butter2 (fc, sr, true)),  hp2 (butter2 (fc, sr, true)) {}
        void process (double x, double& low, double& high)
        {
            low  = lp2.process (lp1.process (x));
            high = hp2.process (hp1.process (x));
        }
        Biquad lp1, lp2, hp1, hp2;
    };

    void testCrossoverFlatness()
    {
        std::printf ("crossover reconstruction\n");

        const double sr = 96000.0;
        const size_t N = 1 << 16;
        const double settings[][2] = { { 200, 2500 }, { 60, 1000 }, { 500, 8000 }, { 120, 4000 }, { 350, 1500 } };

        bool flat = true;
        double worst = 0.0;
        for (const auto& s : settings)
        {
            LR4 split1 (s[0], sr), split2 (s[1], sr), align (s[1], sr);
            std::vector<double> out (N);
            for (size_t n = 0; n < N; ++n)
            {
                const double x = n == 0 ? 1.0 : 0.0;       // impulse
                double low1, high1, mid, high, lowAlignedLow, lowAlignedHigh;
                split1.process (x, low1, high1);
                split2.process (high1, mid, high);
                align.process (low1, lowAlignedLow, lowAlignedHigh);
                out[n] = (lowAlignedLow + lowAlignedHigh) + mid + high;   // allpass = LP + HP
            }

            std::vector<std::complex<double>> a (out.begin(), out.end());
            fft (a);
            for (size_t k = 1; k < N / 2; ++k)
            {
                const double f = (double) k * sr / (double) N;
                if (f < 20.0 || f > 20000.0) continue;
                const double db = 20.0 * std::log10 (std::abs (a[k]));
                worst = std::max (worst, std::abs (db));
            }
        }
        std::printf ("  worst deviation from flat across 5 crossover settings: %.4f dB\n", worst);
        flat = worst < 0.05;
        check (flat, "low + mid + high sums back flat (within 0.05 dB) at every crossover setting");
    }

    //==========================================================================
    void testFrameRateIndependence()
    {
        std::printf ("GUI motion is frame-rate independent\n");

        auto toggleAfter = [] (double fps)
        {
            const float dt = (float) (1.0 / fps);
            float pos = 0.0f;
            for (int i = 0; i < (int) std::round (fps * 0.1); ++i)
                pos = chroma::easeToward (pos, 1.0f, dt, 0.045f);
            return pos;
        };

        // 100 ms is a whole number of frames at these rates (3, 6, 12, 24), so all runs cover identical time.
        const float t30 = toggleAfter (30), t60 = toggleAfter (60), t120 = toggleAfter (120), t240 = toggleAfter (240);
        std::printf ("  toggle position after 100 ms: 30 Hz %.4f | 60 Hz %.4f | 120 Hz %.4f | 240 Hz %.4f\n", t30, t60, t120, t240);
        check (std::abs (t30 - t240) < 1.0e-3f && std::abs (t60 - t240) < 1.0e-3f && std::abs (t120 - t240) < 1.0e-3f,
               "toggle animation is the same at 30/60/120/240 Hz");
    }
}

int main()
{
    testShapingCurve();
    testHarmonics();
    testAliasing();
    testClip();
    testMixRamp();
    testSwitchingIsClickFree();
    testDelayRing();
    testSoftClip();
    testGainMatch();
    testMeterScale();
    testCrossoverFlatness();
    testFrameRateIndependence();

    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
