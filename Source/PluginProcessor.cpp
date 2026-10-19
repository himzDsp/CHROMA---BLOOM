#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
    // Hands a block's peaks to the editor: keeps the larger of the stored and the new value.
    // A mono host feeds both meter channels from channel 0.
    void publishPeak (std::array<std::atomic<float>, 2>& dst, const float (&peak)[2], int numCh) noexcept
    {
        for (size_t c = 0; c < 2; ++c)
        {
            const float v = peak[numCh > 1 ? c : 0];

            if (v > dst[c].load (std::memory_order_relaxed))
                dst[c].store (v, std::memory_order_relaxed);
        }
    }
}

//==============================================================================
AudioProcessorValueTreeState::ParameterLayout ChromaInflatorAudioProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto dbText    = [] (float v, int) { return String (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; };
    auto pctText   = [] (float v, int) { return String (v, 1) + "%"; };
    auto curveText = [] (float v, int) { return String (v > 0.0f ? "+" : "") + String (v, 1); };

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::input, 1 }, "Input",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::effect, 1 }, "Effect",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::curve, 1 }, "Curve",
        NormalisableRange<float> (-50.0f, 50.0f, 0.1f), -15.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (curveText)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::output, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::clip, 1 },      "Clip (0 dB)", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::bandSplit, 1 }, "Band Split", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::effectIn, 1 },  "Effect In",  true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::hq, 1 },        "HQ",          false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::hqFactor, 1 }, "HQ Oversampling",
                                                        StringArray { "4x", "8x", "16x" }, 0));

    // Added after v1.0 (version hint 2). Old projects and presets without them load these defaults.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::gainMatch, 2 }, "Gain Match", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::clipMode, 2 }, "Clip Mode",
                                                        StringArray { "Hard", "Soft" }, 0));

    auto hzText = [] (float v, int)
    {
        return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String ((int) std::lround (v)) + " Hz";
    };

    // Log-feeling range: the geometric centre of the range sits at the slider's halfway point.
    auto logRange = [] (float lo, float hi)
    {
        const float skew = std::log (0.5f) / std::log ((std::sqrt (lo * hi) - lo) / (hi - lo));
        return NormalisableRange<float> (lo, hi, 0.1f, skew);
    };

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::lowXover, 2 }, "Low Crossover", logRange (60.0f, 500.0f), 200.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (hzText)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::highXover, 2 }, "High Crossover", logRange (1000.0f, 8000.0f), 2500.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (hzText)));

    return layout;
}

//==============================================================================
ChromaInflatorAudioProcessor::ChromaInflatorAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    inputParam     = apvts.getRawParameterValue (ParamIDs::input);
    effectParam    = apvts.getRawParameterValue (ParamIDs::effect);
    curveParam     = apvts.getRawParameterValue (ParamIDs::curve);
    outputParam    = apvts.getRawParameterValue (ParamIDs::output);
    clipParam      = apvts.getRawParameterValue (ParamIDs::clip);
    bandSplitParam = apvts.getRawParameterValue (ParamIDs::bandSplit);
    effectInParam  = apvts.getRawParameterValue (ParamIDs::effectIn);
    hqParam        = apvts.getRawParameterValue (ParamIDs::hq);
    hqFactorParam  = apvts.getRawParameterValue (ParamIDs::hqFactor);
    gainMatchParam = apvts.getRawParameterValue (ParamIDs::gainMatch);
    clipModeParam  = apvts.getRawParameterValue (ParamIDs::clipMode);
    lowXoverParam  = apvts.getRawParameterValue (ParamIDs::lowXover);
    highXoverParam = apvts.getRawParameterValue (ParamIDs::highXover);
}

ChromaInflatorAudioProcessor::~ChromaInflatorAudioProcessor()
{
    cancelPendingUpdate();
}

bool ChromaInflatorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

//==============================================================================
void ChromaInflatorAudioProcessor::Chain::prepare (double rate, int maxBlockSize)
{
    dsp::ProcessSpec spec { rate, (uint32) maxBlockSize, 2 };
    splitLow.prepare (spec);
    splitHigh.prepare (spec);
    alignLow.prepare (spec);
    alignLow.setType (dsp::LinkwitzRileyFilterType::allpass);   // phase-aligns the low band with the high split
    applyCrossovers (200.0, 2500.0);

    effectSm.reset (rate, paramSmoothSeconds);
    curveSm.reset  (rate, paramSmoothSeconds);
    outputSm.reset (rate, paramSmoothSeconds);
    lowSm.reset  (rate, paramSmoothSeconds);
    highSm.reset (rate, paramSmoothSeconds);

    effectInMix.prepare (rate, switchFadeSeconds);
    splitMix.prepare    (rate, switchFadeSeconds);
    clipMix.prepare     (rate, switchFadeSeconds);
    softMix.prepare     (rate, switchFadeSeconds);

    snapTo (Targets {});
}

// All three filters share the two crossover frequencies; alignLow follows the HIGH crossover so the low band
// stays phase-aligned with the mid + high split wherever the crossovers are.
void ChromaInflatorAudioProcessor::Chain::applyCrossovers (double lowHz, double highHz) noexcept
{
    splitLow.setCutoffFrequency  (lowHz);
    splitHigh.setCutoffFrequency (highHz);
    alignLow.setCutoffFrequency  (highHz);
    appliedLow  = lowHz;
    appliedHigh = highHz;
}

void ChromaInflatorAudioProcessor::Chain::setTargets (const Targets& t) noexcept
{
    lowSm.setTargetValue  (t.lowHz);
    highSm.setTargetValue (t.highHz);
    softMix.setTarget     (t.softClip);
    effectSm.setTargetValue (t.effect);
    curveSm.setTargetValue  (t.curve);
    outputSm.setTargetValue (t.output);

    effectInMix.setTarget (t.effectIn);
    splitMix.setTarget    (t.bandSplit);
    clipMix.setTarget     (t.clip);
}

void ChromaInflatorAudioProcessor::Chain::snapTo (const Targets& t) noexcept
{
    resetFilters();

    effectSm.setCurrentAndTargetValue (t.effect);
    curveSm.setCurrentAndTargetValue  (t.curve);
    outputSm.setCurrentAndTargetValue (t.output);

    effectInMix.snap (t.effectIn);
    splitMix.snap    (t.bandSplit);
    clipMix.snap     (t.clip);
    softMix.snap     (t.softClip);

    lowSm.setCurrentAndTargetValue  (t.lowHz);
    highSm.setCurrentAndTargetValue (t.highHz);
    applyCrossovers ((double) t.lowHz, (double) t.highHz);
}

void ChromaInflatorAudioProcessor::Chain::resetFilters() noexcept
{
    splitLow.reset();
    splitHigh.reset();
    alignLow.reset();
}

ChromaInflatorAudioProcessor::Targets ChromaInflatorAudioProcessor::readTargets() const noexcept
{
    Targets t;
    t.input     = Decibels::decibelsToGain (inputParam->load());
    t.effect    = effectParam->load() / 100.0f;
    t.curve     = curveParam->load()  / 50.0f;
    t.output    = Decibels::decibelsToGain (outputParam->load());
    t.clip      = clipParam->load()      > 0.5f;
    t.bandSplit = bandSplitParam->load() > 0.5f;
    t.effectIn  = effectInParam->load()  > 0.5f;
    t.softClip  = clipModeParam->load()  > 0.5f;
    t.gainMatch = gainMatchParam->load() > 0.5f;

    // Keep at least an octave between the two crossovers, whatever the host or a preset asks for.
    t.lowHz  = jlimit (20.0f, 1000.0f, lowXoverParam->load());
    t.highHz = jmax (t.lowHz * 2.0f, highXoverParam->load());
    return t;
}

dsp::Oversampling<float>* ChromaInflatorAudioProcessor::getOversampler (int mode) noexcept
{
    // mode 0 (2x) is the always-on base -- never nullptr, so the plugin is never
    // run at bare 1x. HQ only adds further linear-phase FIR stages on top of it.
    switch (mode)
    {
        case 1:  return &os4x;
        case 2:  return &os8x;
        case 3:  return &os16x;
        default: return &osBase2x;   // HQ off: base 2x only
    }
}

int ChromaInflatorAudioProcessor::requestedMode() const noexcept
{
    if (hqParam->load() < 0.5f)
        return 0;   // base 2x -- the floor, always oversampled, never 1x
    return 1 + jlimit (0, 2, (int) std::lround (hqFactorParam->load()));   // 1=4x, 2=8x, 3=16x
}

//==============================================================================
void ChromaInflatorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = jmax (1, samplesPerBlock);

    for (auto* os : { &osBase2x, &os4x, &os8x, &os16x })
    {
        os->initProcessing ((size_t) maxBlock);
        os->reset();
    }

    const auto targets = readTargets();

    // mode 0 is the always-on 2x base (not 1x), so the oversampling factor at
    // the host rate is 2^(mode+1): 2x, 4x, 8x, 16x.
    int maxLatency = 0;
    for (int mode = 0; mode < 4; ++mode)
    {
        const int factor = 1 << (mode + 1);
        auto& chain = chains[(size_t) mode];
        chain.prepare (sampleRate * factor, maxBlock * factor);
        chain.snapTo (targets);

        latencyForMode[(size_t) mode] = (int) std::lround (getOversampler (mode)->getLatencyInSamples());
        maxLatency = jmax (maxLatency, latencyForMode[(size_t) mode]);
    }

    gainMatcher.prepare (sampleRate);
    uiSampleRate.store (sampleRate);
    spectrumScratch.assign ((size_t) maxBlock, 0.0f);
    learnScratch.assign ((size_t) maxBlock, 0.0f);

    inputGainSm.reset (sampleRate, paramSmoothSeconds);
    inputGainSm.setCurrentAndTargetValue (targets.input);

    modeFade.prepare    (sampleRate, modeFadeSeconds);
    modeFade.snap (true);                              // resting state: no fade in progress
    clipHostMix.prepare (sampleRate, switchFadeSeconds);
    clipHostMix.snap (targets.clip);
    bypassMix.prepare   (sampleRate, switchFadeSeconds);
    bypassMix.snap (true);
    bypassInitialised = false;                         // first block decides (a project may load already bypassed)

    for (auto& d : dryDelay)
        d.prepare (maxLatency);

    fadeBuffer.setSize (2, maxBlock, false, false, true);
    dryBuffer.setSize  (2, maxBlock, false, false, true);
    fadeBuffer.clear();
    dryBuffer.clear();

    currentMode  = requestedMode();
    fadeFromMode = currentMode;

    reportedLatency = latencyForMode[(size_t) currentMode];
    pendingLatency.store (reportedLatency);
    setLatencySamples (reportedLatency);
}

void ChromaInflatorAudioProcessor::requestLatency (int samples)
{
    if (samples == reportedLatency)
        return;

    reportedLatency = samples;
    pendingLatency.store (samples);
    triggerAsyncUpdate();
}

void ChromaInflatorAudioProcessor::handleAsyncUpdate()
{
    setLatencySamples (pendingLatency.load());
}

// Switching HQ on/off or the HQ factor used to reset the oversampler and filters on the spot, which
// cut the waveform off mid-cycle (an audible click). Now the new mode starts from silence and the
// two are crossfaded for modeFadeSeconds, so the output never steps.
void ChromaInflatorAudioProcessor::beginModeChange (int newMode, const Targets& t)
{
    fadeFromMode = currentMode;
    currentMode  = newMode;

    chains[(size_t) newMode].snapTo (t);
    getOversampler (newMode)->reset();

    modeFade.snap (false);
    modeFade.setTarget (true);

    requestLatency (latencyForMode[(size_t) newMode]);
}

//==============================================================================
void ChromaInflatorAudioProcessor::shape (Chain& chain, dsp::AudioBlock<float> block, int numCh) noexcept
{
    const size_t n = block.getNumSamples();

    for (size_t i = 0; i < n; ++i)
    {
        const double e  = (double) chain.effectSm.getNextValue();
        const double c  = (double) chain.curveSm.getNextValue();
        const double og = (double) chain.outputSm.getNextValue();

        // Crossover glide: the coefficients are only recomputed every 16 samples, and only while they are moving.
        const double lo = (double) chain.lowSm.getNextValue();
        const double hi = (double) chain.highSm.getNextValue();
        if ((i & 15u) == 0 && (std::abs (lo - chain.appliedLow)  > 1.0e-4 * chain.appliedLow
                            || std::abs (hi - chain.appliedHigh) > 1.0e-4 * chain.appliedHigh))
            chain.applyCrossovers (lo, hi);

        // 0 = off .. 1 = on, ramped so toggling a switch crossfades instead of stepping
        const double effMix   = (double) chain.effectInMix.next();
        const double splitMix = (double) chain.splitMix.next();
        const double clipMix  = (double) chain.clipMix.next();
        const double softAmt  = (double) chain.softMix.next();   // 0 = hard ceiling .. 1 = soft clipper

        // Only evaluate the paths that actually contribute to the mix (cheap when nothing is moving).
        const bool wantWet    = effMix > 0.0;
        const bool wantDry    = effMix < 1.0;
        const bool wantSplit  = splitMix > 0.0;
        const bool wantSingle = splitMix < 1.0;

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* d = block.getChannelPointer ((size_t) ch);
            const double x = (double) d[i];

            // Crossover runs every sample so its state stays warm when toggled.
            double low1 = 0.0, high1 = 0.0, mid = 0.0, high = 0.0;
            chain.splitLow.processSample  (ch, x, low1, high1);
            chain.splitHigh.processSample (ch, high1, mid, high);
            const double low = chain.alignLow.processSample (ch, low1);

            double wet = 0.0, dry = 0.0;

            if (wantWet)
            {
                const double single = wantSingle ? chroma::inflate (x, e, c) : 0.0;
                const double split  = wantSplit  ? chroma::inflate (low, e, c) + chroma::inflate (mid, e, c) + chroma::inflate (high, e, c)
                                                 : 0.0;
                wet = chroma::mix (single, split, splitMix);
            }

            if (wantDry)
            {
                const double split = wantSplit ? low + mid + high : 0.0;
                dry = chroma::mix (wantSingle ? x : 0.0, split, splitMix);
            }

            const double y = chroma::mix (dry, wet, effMix);

            // Output gain and the 0 dB ceiling happen here, at the oversampled rate,
            // so clipping is band-limited by the downsampling filter.
            d[i] = (float) chroma::outputStageMixMode (y, og, clipMix, softAmt);
        }
    }
}

//==============================================================================
void ChromaInflatorAudioProcessor::processChunk (AudioBuffer<float>& buffer, int start, int n, int numCh,
                                                 const Targets& userTargets, bool hostBypassed) noexcept
{
    // Gain Match rides on the Output gain. The same factor is divided out of the measurement further down,
    // so the matcher never chases its own correction.
    const double gmApplied = userTargets.gainMatch ? gainMatcher.gain : 1.0;
    Targets t = userTargets;
    t.output = userTargets.output * (float) gmApplied;
    const double refGain = (double) userTargets.output * gmApplied;
    const double invRefGainSq = 1.0 / jmax (1.0e-12, refGain * refGain);

    float* io[2] = { nullptr, nullptr };
    for (int c = 0; c < numCh; ++c)
        io[c] = buffer.getWritePointer (c, start);

    // A mode change only starts between crossfades, so rapid HQ toggling queues up instead of glitching.
    if (! modeFade.isRamping())
    {
        const int wanted = requestedMode();
        if (wanted != currentMode)
            beginModeChange (wanted, t);
    }

    const bool fading = modeFade.isRamping();

    chains[(size_t) currentMode].setTargets (t);
    if (fading)
        chains[(size_t) fadeFromMode].setTargets (t);

    // --- Input: sanitise, keep a latency-aligned dry copy (for host bypass), apply smoothed gain ----
    inputGainSm.setTargetValue (t.input);

    float* dry[2] = { dryBuffer.getWritePointer (0), dryBuffer.getWritePointer (1) };
    float inPeak[2] = { 0.0f, 0.0f }, outPeak[2] = { 0.0f, 0.0f };
    const int dryLatency = latencyForMode[(size_t) currentMode];

    for (int i = 0; i < n; ++i)
    {
        const float g = inputGainSm.getNextValue();

        for (int c = 0; c < numCh; ++c)
        {
            float x = io[c][i];

            // A single NaN/Inf would stay in the crossover filters forever. Never let one in.
            if (! std::isfinite (x))
                x = 0.0f;

            dry[c][i] = dryDelay[(size_t) c].process (x, dryLatency);

            x *= g;
            io[c][i] = x;
            inPeak[c] = jmax (inPeak[c], std::abs (x));
        }
    }

    publishPeak (meterIn, inPeak, numCh);

    // --- Chroma Bloom: always oversampled (>=2x base; HQ adds 4x/8x/16x) ------------------------------
    dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numCh, (size_t) start, (size_t) n);

    if (fading)
    {
        // Outgoing mode: run the same input through it so it can be faded out.
        for (int c = 0; c < numCh; ++c)
            FloatVectorOperations::copy (fadeBuffer.getWritePointer (c), io[c], n);

        dsp::AudioBlock<float> fadeBlock (fadeBuffer.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);

        auto* osOld = getOversampler (fadeFromMode);
        auto upOld = osOld->processSamplesUp (fadeBlock);
        shape (chains[(size_t) fadeFromMode], upOld, numCh);
        osOld->processSamplesDown (fadeBlock);
    }

    auto* os = getOversampler (currentMode);
    auto up = os->processSamplesUp (block);
    shape (chains[(size_t) currentMode], up, numCh);
    os->processSamplesDown (block);

    if (fading)
    {
        const float* old[2] = { fadeBuffer.getReadPointer (0), fadeBuffer.getReadPointer (1) };

        for (int i = 0; i < n; ++i)
        {
            const float f = modeFade.next();
            for (int c = 0; c < numCh; ++c)
                io[c][i] = old[c][i] * (1.0f - f) + io[c][i] * f;
        }
    }

    // --- Output trim, host bypass crossfade, output meter -------------------------------------------------
    // The downsampling filter can overshoot the ceiling by a hair; with Clip on, trim that so the output
    // never exceeds 0 dBFS (faded in/out together with the Clip switch).
    clipHostMix.setTarget (t.clip);
    bypassMix.setTarget (! hostBypassed);

    const bool wantSpectrum = spectrumActive.load (std::memory_order_relaxed);
    const bool wantLearn    = learnActive.load (std::memory_order_relaxed);

    for (int i = 0; i < n; ++i)
    {
        const float clipAmount = clipHostMix.next();
        const float active     = bypassMix.next();

        double dryEnergy = 0.0, wetEnergy = 0.0, mono = 0.0, dryMono = 0.0;

        for (int c = 0; c < numCh; ++c)
        {
            float y = io[c][i];

            // Gain Match measures the processed signal as it leaves the chain, against the latency-aligned dry copy.
            dryMono   += (double) dry[c][i];
            dryEnergy += (double) dry[c][i] * (double) dry[c][i];
            wetEnergy += (double) y * (double) y;

            if (clipAmount > 0.0f)
                y = y * (1.0f - clipAmount) + jlimit (-1.0f, 1.0f, y) * clipAmount;

            if (active < 1.0f)
                y = dry[c][i] * (1.0f - active) + y * active;

            io[c][i] = y;
            outPeak[c] = jmax (outPeak[c], std::abs (y));
            mono += (double) y;
        }

        gainMatcher.accumulate (dryEnergy / numCh, wetEnergy / numCh * invRefGainSq);

        if (wantSpectrum)
            spectrumScratch[(size_t) i] = (float) (mono / numCh);

        if (wantLearn)
            learnScratch[(size_t) i] = (float) (dryMono / numCh);
    }

    if (wantLearn)
    {
        const auto scope = learnFifo.write (n);
        if (scope.blockSize1 > 0)
            std::copy (learnScratch.data(), learnScratch.data() + scope.blockSize1, learnData.begin() + scope.startIndex1);
        if (scope.blockSize2 > 0)
            std::copy (learnScratch.data() + scope.blockSize1, learnScratch.data() + scope.blockSize1 + scope.blockSize2,
                       learnData.begin() + scope.startIndex2);
    }

    gainMatcher.update (n);
    publishPeak (meterOut, outPeak, numCh);

    if (wantSpectrum)
        pushSpectrumSamples (spectrumScratch.data(), n);
}

//==============================================================================
void ChromaInflatorAudioProcessor::pushSpectrumSamples (const float* mono, int n) noexcept
{
    const auto scope = spectrumFifo.write (n);   // takes what fits; the display just skips what does not

    if (scope.blockSize1 > 0)
        std::copy (mono, mono + scope.blockSize1, spectrumData.begin() + scope.startIndex1);

    if (scope.blockSize2 > 0)
        std::copy (mono + scope.blockSize1, mono + scope.blockSize1 + scope.blockSize2,
                   spectrumData.begin() + scope.startIndex2);
}

int ChromaInflatorAudioProcessor::popSpectrumSamples (float* dest, int maxNum) noexcept
{
    const auto scope = spectrumFifo.read (jmin (maxNum, spectrumFifo.getNumReady()));

    if (scope.blockSize1 > 0)
        std::copy (spectrumData.begin() + scope.startIndex1,
                   spectrumData.begin() + scope.startIndex1 + scope.blockSize1, dest);

    if (scope.blockSize2 > 0)
        std::copy (spectrumData.begin() + scope.startIndex2,
                   spectrumData.begin() + scope.startIndex2 + scope.blockSize2, dest + scope.blockSize1);

    return scope.blockSize1 + scope.blockSize2;
}

int ChromaInflatorAudioProcessor::popLearnSamples (float* dest, int maxNum) noexcept
{
    const auto scope = learnFifo.read (jmin (maxNum, learnFifo.getNumReady()));

    if (scope.blockSize1 > 0)
        std::copy (learnData.begin() + scope.startIndex1, learnData.begin() + scope.startIndex1 + scope.blockSize1, dest);
    if (scope.blockSize2 > 0)
        std::copy (learnData.begin() + scope.startIndex2, learnData.begin() + scope.startIndex2 + scope.blockSize2, dest + scope.blockSize1);

    return scope.blockSize1 + scope.blockSize2;
}

void ChromaInflatorAudioProcessor::resetMissingParamsToDefault (const ValueTree& incomingState)
{
    for (auto* id : { ParamIDs::gainMatch, ParamIDs::clipMode, ParamIDs::lowXover, ParamIDs::highXover })
    {
        if (incomingState.getChildWithProperty ("id", String (id)).isValid())
            continue;

        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->getDefaultValue());
    }
}

void ChromaInflatorAudioProcessor::processInternal (AudioBuffer<float>& buffer, bool hostBypassed)
{
    ScopedNoDenormals noDenormals;

    const int total  = buffer.getNumSamples();
    const int numIn  = getTotalNumInputChannels();
    const int numCh  = jmin (numIn, 2, buffer.getNumChannels());

    for (int ch = numIn; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, total);

    if (numCh <= 0 || total <= 0 || maxBlock <= 0)
        return;

    if (! bypassInitialised)
    {
        bypassMix.snap (! hostBypassed);   // a project saved while bypassed must not fade in on load
        bypassInitialised = true;
    }

    const Targets targets = readTargets();

    // Some hosts hand over blocks larger than the size announced in prepareToPlay(). The scratch buffers and the
    // oversamplers are sized for maxBlock, so never process more than that at once.
    for (int pos = 0; pos < total; pos += maxBlock)
        processChunk (buffer, pos, jmin (maxBlock, total - pos), numCh, targets, hostBypassed);
}

void ChromaInflatorAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    processInternal (buffer, false);
}

// Called by the host's bypass button. The signal is crossfaded to a copy of the input that is delayed by the
// same latency as the processed path, so bypassing neither clicks nor shifts the timing.
void ChromaInflatorAudioProcessor::processBlockBypassed (AudioBuffer<float>& buffer, MidiBuffer&)
{
    processInternal (buffer, true);
}

//==============================================================================
AudioProcessorEditor* ChromaInflatorAudioProcessor::createEditor()
{
    return new ChromaInflatorAudioProcessorEditor (*this);
}

void ChromaInflatorAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", 1, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ChromaInflatorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto tree = ValueTree::fromXml (*xml);
            apvts.replaceState (tree);
            resetMissingParamsToDefault (tree);   // projects saved before the newer parameters existed
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChromaInflatorAudioProcessor();
}
