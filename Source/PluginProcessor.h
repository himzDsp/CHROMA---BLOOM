#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>
#include "InflatorMath.h"
#include "ChromaHelpers.h"

namespace ParamIDs
{
    inline constexpr const char* input     = "input";
    inline constexpr const char* effect    = "effect";
    inline constexpr const char* curve     = "curve";
    inline constexpr const char* output    = "output";
    inline constexpr const char* clip      = "clip";
    inline constexpr const char* bandSplit = "bandSplit";
    inline constexpr const char* effectIn  = "effectIn";
    inline constexpr const char* hq        = "hq";         // off = base 2x only (small fixed latency)
    inline constexpr const char* hqFactor  = "hqFactor";   // total factor when HQ is on: 4x / 8x / 16x
    inline constexpr const char* gainMatch = "gainMatch";  // keeps processed RMS equal to the dry RMS
    inline constexpr const char* clipMode  = "clipMode";   // 0 = hard ceiling, 1 = soft clipper
    inline constexpr const char* lowXover  = "lowXover";   // Band Split crossover, Hz
    inline constexpr const char* highXover = "highXover";  // Band Split crossover, Hz
}

class ChromaInflatorAudioProcessor : public juce::AudioProcessor,
                                     private juce::AsyncUpdater
{
public:
    ChromaInflatorAudioProcessor();
    ~ChromaInflatorAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // --- Band Split strip support (editor side) -----------------------------------------------------------
    // The strip asks for samples only while it is visible; otherwise the audio thread does no extra work.
    void setSpectrumActive (bool on) noexcept        { spectrumActive.store (on); }
    int  popSpectrumSamples (float* dest, int maxNum) noexcept;   // lock-free; returns how many were copied
    double getUiSampleRate() const noexcept          { return uiSampleRate.load(); }

    // Learn (Band Split): taps the DRY input (mono) only while a learn pass is running.
    void setLearnActive (bool on) noexcept           { learnActive.store (on); }
    int  popLearnSamples (float* dest, int maxNum) noexcept;

    // --- Level meters (editor side) -----------------------------------------------------------------------
    // The audio thread keeps the highest sample peak seen since the editor last asked; reading clears it.
    // Input = after the Input gain (what feeds the effect), Output = what leaves the plugin. Linear, 0..1+.
    float takeInputPeak  (int channel) noexcept      { return meterIn [(size_t) (channel & 1)].exchange (0.0f, std::memory_order_relaxed); }
    float takeOutputPeak (int channel) noexcept      { return meterOut[(size_t) (channel & 1)].exchange (0.0f, std::memory_order_relaxed); }

    // Parameters added after v1.0 fall back to their defaults when an older state / preset lacks them.
    void resetMissingParamsToDefault (const juce::ValueTree& incomingState);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void pushSpectrumSamples (const float* mono, int n) noexcept;

    // One snapshot of every parameter, read once per host block.
    struct Targets
    {
        float input  = 1.0f;    // linear gain
        float effect = 0.0f;    // 0..1
        float curve  = -0.3f;   // -1..1
        float output = 1.0f;    // linear gain
        bool  clip = false, bandSplit = false, effectIn = true;
        bool  softClip = false, gainMatch = false;
        float lowHz = 200.0f, highHz = 2500.0f;
    };

    // Everything that depends on the processing sample rate lives here, one per
    // oversampling mode (index = log2 of the TOTAL factor relative to host rate:
    // 0 -> 2x (base, always active), 1 -> 4x, 2 -> 8x, 3 -> 16x).
    //
    // Nothing in here ever jumps while audio is playing: continuous parameters use
    // smoothers, and the on/off switches (Effect In, Split, Clip) use short crossfade ramps.
    struct Chain
    {
        void prepare (double rate, int maxBlock);
        void setTargets (const Targets&) noexcept;
        void snapTo (const Targets&) noexcept;          // jump straight to these values (no audio running through yet)
        void resetFilters() noexcept;
        void applyCrossovers (double lowHz, double highHz) noexcept;

        // 3-band Linkwitz-Riley split. Double precision: low crossovers at very
        // high sample rates are noisy in float.
        juce::dsp::LinkwitzRileyFilter<double> splitLow, splitHigh, alignLow;

        juce::SmoothedValue<float> effectSm, curveSm;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputSm;   // gains glide evenly in dB
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowSm, highSm;   // crossover Hz glide evenly in octaves
        double appliedLow = 200.0, appliedHigh = 2500.0;

        chroma::MixRamp effectInMix, splitMix, clipMix, softMix;
    };

    Targets readTargets() const noexcept;

    void processInternal (juce::AudioBuffer<float>&, bool hostBypassed);
    void processChunk (juce::AudioBuffer<float>&, int start, int numSamples, int numCh,
                       const Targets&, bool hostBypassed) noexcept;
    void shape (Chain& chain, juce::dsp::AudioBlock<float> block, int numCh) noexcept;

    void beginModeChange (int newMode, const Targets&);
    juce::dsp::Oversampling<float>* getOversampler (int mode) noexcept;
    int requestedMode() const noexcept;

    // Latency changes are reported to the host from the message thread, never from the audio thread.
    void requestLatency (int samples);
    void handleAsyncUpdate() override;

    // Linear-phase half-band FIR oversamplers, all built on a 2x base stage that is
    // ALWAYS active (even with HQ off) so the plugin never runs alias-prone at 1x:
    //   mode 0:  2x  (base only)                     - HQ off
    //   mode 1:  4x  (base + one extra half-band)     - HQ on, factor 4x
    //   mode 2:  8x  (base + two extra half-bands)    - HQ on, factor 8x
    //   mode 3: 16x  (base + three extra half-bands)  - HQ on, factor 16x
    // Each is its own juce::dsp::Oversampling with numStages = log2(factor), so every
    // stage is linear-phase equiripple FIR end to end; only the active one runs
    // (two of them during the short crossfade when HQ / the factor is changed).
    juce::dsp::Oversampling<float> osBase2x { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true };
    juce::dsp::Oversampling<float> os4x     { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true };
    juce::dsp::Oversampling<float> os8x     { 2, 3, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true };
    juce::dsp::Oversampling<float> os16x    { 2, 4, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true };

    std::array<Chain, 4> chains;
    std::array<int, 4> latencyForMode { 0, 0, 0, 0 };
    int currentMode  = 0;   // always >= 0; mode 0 (2x) is the floor, never 1x/off
    int fadeFromMode = 0;   // the mode being faded out while modeFade is running

    int maxBlock = 0;
    bool bypassInitialised = false;

    // host-rate smoothing / crossfades
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGainSm;
    chroma::MixRamp modeFade;      // 0 -> 1 while crossfading from the old oversampling mode to the new one
    chroma::MixRamp clipHostMix;   // post-downsampling safety trim, faded the same way as the in-chain Clip
    chroma::MixRamp bypassMix;     // 1 = processing, 0 = host bypass (latency-aligned dry signal)

    std::array<chroma::DelayRing, 2> dryDelay;   // keeps the bypassed signal aligned with the wet latency
    juce::AudioBuffer<float> fadeBuffer, dryBuffer;

    std::atomic<int> pendingLatency { 0 };
    int reportedLatency = -1;

    std::atomic<float>* inputParam     = nullptr;
    std::atomic<float>* effectParam    = nullptr;
    std::atomic<float>* curveParam     = nullptr;
    std::atomic<float>* outputParam    = nullptr;
    std::atomic<float>* clipParam      = nullptr;
    std::atomic<float>* bandSplitParam = nullptr;
    std::atomic<float>* effectInParam  = nullptr;
    std::atomic<float>* hqParam        = nullptr;
    std::atomic<float>* hqFactorParam  = nullptr;
    std::atomic<float>* gainMatchParam = nullptr;
    std::atomic<float>* clipModeParam  = nullptr;
    std::atomic<float>* lowXoverParam  = nullptr;
    std::atomic<float>* highXoverParam = nullptr;

    // Peak meters (written by the audio thread, read + cleared by the editor)
    std::array<std::atomic<float>, 2> meterIn {}, meterOut {};

    // Gain Match: RMS of dry vs processed, measured at host rate (see chroma::GainMatcher)
    chroma::GainMatcher gainMatcher;

    // Spectrum feed for the editor's Band Split strip (mono, lock-free, only written while the strip is visible)
    static constexpr int spectrumFifoSize = 16384;
    juce::AbstractFifo spectrumFifo { spectrumFifoSize };
    std::array<float, (size_t) spectrumFifoSize> spectrumData {};
    std::vector<float> spectrumScratch;
    std::atomic<bool> spectrumActive { false };

    static constexpr int learnFifoSize = 65536;
    juce::AbstractFifo learnFifo { learnFifoSize };
    std::array<float, (size_t) learnFifoSize> learnData {};
    std::vector<float> learnScratch;
    std::atomic<bool> learnActive { false };
    std::atomic<double> uiSampleRate { 44100.0 };

    // ramp times (seconds)
    static constexpr double paramSmoothSeconds  = 0.020;   // sliders: Input / Effect / Curve / Output
    static constexpr double switchFadeSeconds   = 0.015;   // Effect In / Split / Clip / Bypass
    static constexpr double modeFadeSeconds     = 0.030;   // HQ on/off, HQ factor

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaInflatorAudioProcessor)
};
