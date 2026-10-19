# Release checklist

Done in the repository: JUCE 9.0.4 pin, click-free DSP, vsync GUI, DSP unit tests, CI (Win / macOS / Linux + pluginval).

## Must be done on your machines (not verifiable from the repo alone)
- [ ] Green CI on all three platforms (first full JUCE 9 build). Fix any compile message, then tag the release.
- [ ] Listening test in a DAW: play audio and (1) drag every slider, (2) toggle CLIP / SPLIT / EFFECT, (3) switch HQ and 4x/8x/16x,
      (4) host Bypass, (5) automate Effect. There should be no clicks.
- [ ] pluginval strictness 5 on VST3 and AU (the CI job is non-blocking until it has passed once; then drop `continue-on-error`).
- [ ] macOS: `auval -v aufx Cinf Hmzd`, code-sign and notarise the AU/VST3/Standalone. Windows: sign the VST3/installer.
- [ ] Test at 44.1 / 48 / 96 / 192 kHz and buffer sizes 32 - 2048, plus a mono track.
- [ ] Check the window at 100 % and 200 % OS scaling, and on a 4K monitor.

## Decisions for you
- JUCE licence: `JUCE_DISPLAY_SPLASH_SCREEN=0` is set. Make sure your JUCE licence tier allows that (or ship under AGPLv3).
- Branding: "Him'z Audio ©2024" is used everywhere (footer, info card, CMake company + copyright, README, presets folder
  `Documents/Him'z Audio/Chroma Bloom`; presets from the old `Him'z DSP` folder are copied across once). Change the year before a later release.
- Never change `PLUGIN_CODE` (Cinf) or `PLUGIN_MANUFACTURER_CODE` (Hmzd) after release: it breaks existing projects.
