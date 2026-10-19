#pragma once

// Pure C++ (no JUCE) so every helper here can be unit-tested on any machine.

#include <algorithm>
#include <cmath>
#include <vector>

namespace chroma
{
    //==========================================================================
    // Click-free switches
    //
    // A linear 0..1 ramp towards an on/off target. Anything that used to flip
    // instantly (Effect In, Split, Clip, host bypass, oversampling mode) is
    // crossfaded with one of these instead, so toggling never produces a step
    // in the waveform -- the source of the "crackle" when changing settings.
    struct MixRamp
    {
        void prepare (double sampleRate, double seconds) noexcept
        {
            const double n = std::max (1.0, std::round (sampleRate * seconds));
            step = (float) (1.0 / n);
        }

        void snap (bool on) noexcept      { value = target = on ? 1.0f : 0.0f; }
        void setTarget (bool on) noexcept { target = on ? 1.0f : 0.0f; }
        bool isRamping() const noexcept   { return value != target; }

        float next() noexcept
        {
            if (value < target)       value = std::min (target, value + step);
            else if (value > target)  value = std::max (target, value - step);
            return value;
        }

        float value = 0.0f, target = 0.0f, step = 0.001f;
    };

    //==========================================================================
    // Fixed-length delay used to keep the bypassed (dry) signal time-aligned with
    // the oversampled (wet) path, so host bypass crossfades without comb filtering.
    struct DelayRing
    {
        void prepare (int maxDelaySamples)
        {
            int size = 1;
            while (size < maxDelaySamples + 1)
                size <<= 1;

            data.assign ((size_t) size, 0.0f);
            mask = size - 1;
            pos  = 0;
        }

        void reset() noexcept
        {
            std::fill (data.begin(), data.end(), 0.0f);
            pos = 0;
        }

        // Writes `in` and returns the sample written `delay` calls ago.
        float process (float in, int delay) noexcept
        {
            data[(size_t) pos] = in;
            const int d = std::min (std::max (delay, 0), mask);
            const float out = data[(size_t) ((pos - d) & mask)];
            pos = (pos + 1) & mask;
            return out;
        }

        std::vector<float> data = std::vector<float> (1, 0.0f);
        int mask = 0, pos = 0;
    };

    //==========================================================================
    // GUI helper: driven by real elapsed time (not a frame count), so the
    // motion looks identical on a 60 Hz, 120 Hz or 144 Hz display.

    // Exponential approach: the remaining gap shrinks by 1/e every `tauSeconds`.
    inline float easeToward (float position, float target, float dtSeconds, float tauSeconds) noexcept
    {
        const float k = 1.0f - std::exp (-dtSeconds / std::max (tauSeconds, 1.0e-4f));
        return position + (target - position) * k;
    }

    //==========================================================================
    // Level meter scale. The Input / Output faders have 8 equal divisions over 48 dB, so a meter that spans
    // 48 dB (here -42 .. +6 dBFS) puts every 6 dB line level with a fader tick: +6, 0, -6 ... -42.
    constexpr float meterTopDb = 6.0f, meterFloorDb = -42.0f;

    // 0 = bottom of the meter (floor), 1 = top.
    inline float meterFraction (float db) noexcept
    {
        return std::min (1.0f, std::max (0.0f, (db - meterFloorDb) / (meterTopDb - meterFloorDb)));
    }

    //==========================================================================
    // Gain Match: keeps the processed signal as loud (RMS, ~3 s window) as the dry signal, so A/B is fair.
    // The caller feeds per-sample energies; `gain` glides, never jumps.
    struct GainMatcher
    {
        // +/-24 dB: wide enough to undo the full Input gain range (+/-24 dB) as well as what the effect adds.
        static constexpr double minGain = 0.0630957344480193, maxGain = 15.848931924611133;

        void prepare (double sampleRate, double rmsSeconds = 3.0, double glideSeconds = 0.4) noexcept
        {
            rmsCoef   = 1.0 - std::exp (-1.0 / (rmsSeconds * sampleRate));
            glideRate = 1.0 / (glideSeconds * sampleRate);
            reset();
        }

        void reset() noexcept { dryMs = wetMs = 0.0; gain = 1.0; }

        // dryEnergy: dry sample squared (channel average). wetEnergy: processed sample squared with the
        // gain currently applied divided out, so the measurement is not fed back into itself.
        void accumulate (double dryEnergy, double wetEnergy) noexcept
        {
            dryMs += (dryEnergy - dryMs) * rmsCoef;
            wetMs += (wetEnergy - wetMs) * rmsCoef;
        }

        // Call once per block of n samples.
        double update (int n) noexcept
        {
            double target = gain;
            if (dryMs > 1.0e-10 && wetMs > 1.0e-10)
                target = std::min (maxGain, std::max (minGain, std::sqrt (dryMs / wetMs)));

            const double k = 1.0 - std::exp (-(double) n * glideRate);
            gain += (target - gain) * k;
            return gain;
        }

        double dryMs = 0.0, wetMs = 0.0, gain = 1.0;
        double rmsCoef = 1.0e-5, glideRate = 1.0e-4;
    };
}
