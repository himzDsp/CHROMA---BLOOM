# Chroma Bloom — Him'z Audio ©2024

JUCE 9.0.4 / CMake. Builds VST3, AU (macOS) and Standalone.
Linux needs `libxi-dev` and `libegl-dev` (new JUCE 9 dependencies); CI installs them.

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

The plugin is copied to your system plugin folder after the build (COPY_PLUGIN_AFTER_BUILD).
Run the Standalone target first to check it opens, then rescan in your DAW.

## Transparency design
- Low-order odd polynomial curve (nothing above the 5th harmonic, no even harmonics); full-scale peaks are never moved.
- Always oversampled, even with HQ off: a 2x linear-phase half-band FIR runs at all times, so the plugin never processes at bare 1x and never aliases. HQ button (off by default) adds further linear-phase FIR stages; when on, the factor button cycles the *total* factor through 4x / 8x / 16x. Latency is small and fixed per mode, reported to the host and updated when the mode changes.
- Band Split uses double-precision Linkwitz-Riley crossovers (flat magnitude, phase-aligned bands).
- All gain changes are smoothed; Effect at 0 passes the signal through unchanged.

## Click-free by design
Nothing in the audio path steps while you play:
- Input / Effect / Curve / Output glide over 20 ms (gains glide evenly in dB).
- EFFECT, SPLIT, CLIP and host Bypass are 15 ms crossfades, not instant switches.
- HQ on/off and the HQ factor crossfade (30 ms) between the old and new oversampler instead of resetting it.
  Rapid toggling queues up rather than glitching. The new latency is reported to the host from the message thread.
- Host bypass uses a copy of the input delayed by the plugin latency, so it neither clicks nor shifts timing.
- NaN/Inf input is zeroed before it can poison the crossover filters; blocks larger than the host announced are processed in safe chunks.
`Tests/dsp_test.cpp` measures this: a hard switch produces a ~12x larger sample-to-sample jump than the crossfade, which adds none.

## Tests
`Tests/dsp_test.cpp` checks the maths without JUCE (identity at Effect 0, peaks pinned at 0 dBFS,
odd symmetry, monotonic, harmonics limited to 3rd/5th, alias-free at 8x, Clip ceiling) plus the click-free ramps,
the bypass delay, the frame-rate independence of the GUI motion, plus the smooth curve above 0 dBFS, the soft clipper, Gain Match accuracy and a flatness check of the 3-band crossover topology. CI runs it first. Also available via
`cmake -B build -DCHROMA_BUILD_TESTS=ON` and `ctest`.

    g++ -std=c++17 -O2 -I Source Tests/dsp_test.cpp -o dsp_test && ./dsp_test

CLIP, SPLIT and EFFECT are switches (green = on, bright red = off). Rest the cursor on one for 2 seconds to see what it does. CLIP (0 dB) is a hard ceiling on the final output, applied after Output gain.

## Defaults
Effect 0%, Input 0 dB, Output 0 dB, Clip off (Hard), Gain Match off, Band Split off (200 Hz / 2.5 kHz), Effect In on, HQ off (2x base oversampling):
with Effect at 0 the shaping curve is an exact identity, so the signal passes through unchanged other than the small, fixed oversampling latency reported to the host.

## Presets
The preset tab under the header shows the current preset (`init` by default). Click it to open a floating preset list:
`init` (factory defaults) plus every `.chromapreset` in Documents/Him'z Audio/Chroma Bloom. Click a row to load it,
click outside the list to close it, or use LOAD FILE... to pick a preset from anywhere.
SAVE (next to the tab) writes the current settings to a `.chromapreset` file (XML, same format as the host state)
and the tab then shows its name.
DELETE (right of SAVE) removes the current preset's file after an in-window confirmation card and returns to `init`; it is greyed out for `init`
and for presets loaded from outside the presets folder. Presets never contain the dark/light theme.

## Window size
Opens at 400 x 498, which is also the minimum. Drag the bottom-right corner (or resize from the host) to scale
the whole UI up to 2x (800 x 996); the aspect ratio is locked and everything is vector drawn, so it stays sharp on 4K / HiDPI screens.
Switch animations and the Band Split strip run on the display's vertical blank (60 / 120 / 144 Hz) and use real elapsed time,
so they look the same at any refresh rate and cost nothing while idle.

## Gain Match, Clip Mode, Band Split strip
- **GAIN MATCH** keeps the processed signal as loud as the input (RMS averaged over about 3 s, limited to +/-24 dB, glides so it never pumps). Turn it on to compare Effect on/off at equal loudness. The Output slider still works on top of it.
- **LEVEL METERS** (beside the Input and Output faders): stereo sample-peak meters as tall as the fader track, -42 to +6 dB, so each 6 dB line is level with a fader tick (numbers every 12 dB), with a peak-hold tick that falls after about 1.5 s. The input meter reads after the Input gain (what feeds the effect); the output meter reads what leaves the plugin. Amber above -3 dB, red above 0 dB. Click a meter to clear its hold.
- **CLIP MODE** (Hard | Soft selector under the switches): HARD is the flat 0 dB ceiling; SOFT eases into 0 dB with a gentle tanh curve (knee at 0.7), never over 0 dB. Switching crossfades.
- **BAND SPLIT strip** (bottom card, only live while SPLIT is on): a small log-frequency spectrum with the low / mid / high bands tinted and two draggable lines for the crossovers (drag a line, or click anywhere on the strip to bring the nearest line there). Low 60-500 Hz (default 200), high 1-8 kHz (default 2500); the lines keep at least an octave apart. Double-click a line to reset it. Moves are smoothed (no clicks) and the Linkwitz-Riley allpass alignment follows the crossovers. The audio thread only feeds the spectrum while the strip is visible, so it costs nothing when SPLIT is off.
- **Smooth overs**: above 0 dBFS the shaping curve now continues with a continuous slope (no kink), so hot input does not add extra harsh harmonics. Anything at or below 0 dBFS is unchanged.
- The Input / Output peak meters were removed; use your DAW's meters.
- Old projects and presets without the new parameters load with Gain Match off, Hard clip, and crossovers at 200 Hz / 2500 Hz, so they sound as before.
