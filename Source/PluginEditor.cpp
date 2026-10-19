#include "PluginEditor.h"

using namespace juce;

namespace
{
    // One clean UI sans for everything: Segoe UI on Windows, system font on macOS, default sans elsewhere.
    Font uiFont (float size, int style = Font::plain)
    {
       #if JUCE_WINDOWS
        const String name ("Segoe UI");
       #elif JUCE_MAC
        const String name ("Helvetica Neue");
       #else
        const String name (Font::getDefaultSansSerifFontName());
       #endif
        return Font (FontOptions (name, size, style));
    }

    // Medium weight for titles, values and button text (the design uses regular + medium, never heavy bold).
    // Windows and macOS ship a real medium face, so nothing is synthetically emboldened;
    // elsewhere it falls back to the regular weight rather than a faux-bold.
    Font uiFontMedium (float size)
    {
       #if JUCE_WINDOWS
        return Font (FontOptions ("Segoe UI Semibold", size, Font::plain));
       #elif JUCE_MAC
        return Font (FontOptions ("Helvetica Neue", size, Font::plain).withStyle ("Medium"));
       #else
        return uiFont (size);
       #endif
    }

    // Small letter-spaced caps for captions and card titles.
    Font captionFont (float size)  { return uiFontMedium (size).withExtraKerningFactor (0.12f); }

    // Bright red used for every "off" state, the clip light and the destructive DELETE text.
    Colour brightRed() { return Colour (0xffff3b3b); }

    // "Him'z Audio ©2024" -- the one brand string used everywhere in the UI.
    String brandText() { return String ("Him'z Audio ") + String::charToString (0x00A9) + "2024"; }

    File presetDir()
    {
        const auto docs = File::getSpecialLocation (File::userDocumentsDirectory);
        auto dir = docs.getChildFile ("Him'z Audio").getChildFile ("Chroma Bloom");

        // Presets used to live in "Him'z DSP/Chroma Bloom": copy them across once so nothing is lost.
        const auto legacy = docs.getChildFile ("Him'z DSP").getChildFile ("Chroma Bloom");
        if (! dir.exists() && legacy.isDirectory())
        {
            dir.getParentDirectory().createDirectory();
            legacy.copyDirectoryTo (dir);
        }

        dir.createDirectory();
        return dir;
    }

    // Cheap soft shadow (a few stacked, slightly larger rounded rectangles) -- no blur, so it is free to repaint.
    void softShadow (Graphics& g, Rectangle<float> r, float radius, float alpha)
    {
        for (int i = 5; i >= 1; --i)
        {
            g.setColour (Colours::black.withAlpha (alpha * 0.2f));
            g.fillRoundedRectangle (r.expanded ((float) i * 1.3f).translated (0.0f, 1.5f), radius + (float) i * 1.3f);
        }
    }

    // A thin line that fades out towards both ends.
    void fadingRule (Graphics& g, float x, float y, float w, Colour c)
    {
        ColourGradient grad (c.withAlpha (0.0f), x, y, c.withAlpha (0.0f), x + w, y, false);
        grad.addColour (0.5, c);
        g.setGradientFill (grad);
        g.fillRect (x, y, w, 1.0f);
    }

    // Logo mark: six petals around a bright centre.
    void drawBloomMark (Graphics& g, Point<float> centre, float scale, Colour a, Colour b)
    {
        Path petal;
        petal.addEllipse (-2.2f, -9.5f, 4.4f, 9.5f);

        for (int i = 0; i < 6; ++i)
        {
            g.setColour ((i % 2 == 0 ? a : b).withAlpha (0.92f));
            g.fillPath (petal, AffineTransform::scale (scale)
                                   .rotated ((float) i * MathConstants<float>::pi / 3.0f)
                                   .translated (centre.x, centre.y));
        }

        g.setColour (Colours::white.withAlpha (0.95f));
        g.fillEllipse (Rectangle<float> (3.4f * scale, 3.4f * scale).withCentre (centre));
    }
}

//==============================================================================
ChromaLookAndFeel::ChromaLookAndFeel()
{
    setLight (false);   // dark by default
}

void ChromaLookAndFeel::setLight (bool useLight)
{
    light = useLight;
    auto pick = [useLight] (uint32 dark, uint32 lite) { return Colour (useLight ? lite : dark); };

    setColour (Palette::background,       pick (0xff0b0d13, 0xfff1f3f8));
    setColour (Palette::backgroundBottom, pick (0xff07080c, 0xffe5e8f0));
    setColour (Palette::field,            pick (0xff0a0c11, 0xfff3f5f9));
    setColour (Palette::meterBg,          pick (0xff1b1e27, 0xffdfe3ec));
    setColour (Palette::panel,            pick (0xff12151d, 0xffffffff));
    setColour (Palette::panelTop,         pick (0xff181c26, 0xfffbfcfe));
    setColour (Palette::panelEdge,        pick (0xff262b38, 0xffd7dbe6));
    setColour (Palette::neon,             pick (0xff3d6bff, 0xff2f5bff));
    setColour (Palette::neonBright,       pick (0xff7aa0ff, 0xff2146e6));
    setColour (Palette::neonDim,          pick (0xff1f3fb0, 0xff93a8f7));
    setColour (Palette::accent2,          pick (0xff3fd2ff, 0xff0aa5d6));
    setColour (Palette::warn,             pick (0xffffb43b, 0xffe08a00));
    setColour (Palette::chromeEdge,       pick (0xff2d3240, 0xffc3c9d6));
    setColour (Palette::textDim,          pick (0xff8a909c, 0xff5d636e));
    setColour (Palette::title,            pick (0xffe9ecf2, 0xff14161b));
    setColour (Palette::label,            pick (0xff9aa0aa, 0xff4a505a));
    setColour (Palette::meterLabel,       pick (0xff7d8390, 0xff5a606b));
    setColour (Palette::infoBg,           pick (0xf2101218, 0xf5ffffff));
    setColour (Palette::onTop,            pick (0xff4f7bff, 0xff4a74ff));
    setColour (Palette::onBottom,         pick (0xff2a4fe0, 0xff2146e6));
    setColour (Palette::buttonFace,       pick (0xff1a1e28, 0xffe9ecf3));
    setColour (Palette::buttonFaceHover,  pick (0xff252b39, 0xffdde2ec));
    setColour (Palette::buttonText,       pick (0xffcfd4de, 0xff2a2f3a));
    setColour (Palette::trackBg,          pick (0xff252a38, 0xffcdd3e0));
    setColour (Palette::trackEdge,        pick (0xff4a5266, 0xff8791a8));

    setColour (Slider::textBoxTextColourId,       findColour (Palette::neonBright));
    setColour (Slider::textBoxBackgroundColourId, findColour (Palette::field));
    setColour (Slider::textBoxOutlineColourId,    findColour (Palette::panelEdge));
    setColour (Slider::textBoxHighlightColourId,  findColour (Palette::neonDim));
    setColour (TextButton::textColourOffId,       findColour (Palette::buttonText));
    setColour (TextButton::textColourOnId,        Colours::white);

    setColour (TextEditor::backgroundColourId,       findColour (Palette::field));
    setColour (TextEditor::textColourId,             findColour (Palette::title));
    setColour (TextEditor::outlineColourId,          findColour (Palette::panelEdge));
    setColour (TextEditor::focusedOutlineColourId,   findColour (Palette::neonBright));
    setColour (TextEditor::highlightColourId,        findColour (Palette::neonDim));
    setColour (TextEditor::highlightedTextColourId,  findColour (Palette::title));
    setColour (CaretComponent::caretColourId,        findColour (Palette::neonBright));
    setColour (ScrollBar::thumbColourId,             findColour (Palette::chromeEdge));

    setColour (TooltipWindow::backgroundColourId,    pick (0xff171a23, 0xffffffff));
    setColour (TooltipWindow::textColourId,          findColour (Palette::title));
    setColour (TooltipWindow::outlineColourId,       findColour (Palette::neon));
}

Slider::SliderLayout ChromaLookAndFeel::getSliderLayout (Slider& s)
{
    auto layout = LookAndFeel_V4::getSliderLayout (s);

    if (s.getSliderStyle() == Slider::LinearVertical)
    {
        layout.sliderBounds = layout.sliderBounds.reduced (0, getSliderThumbRadius (s) + 1);
        layout.sliderBounds.removeFromBottom ((int) s.getProperties().getWithDefault ("captionGap", 0));   // room for a caption above the value box
    }

    return layout;
}

void ChromaLookAndFeel::drawLinearSlider (Graphics& g, int x, int, int width, int,
                                          float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                                          Slider::SliderStyle style, Slider& s)
{
    if (style != Slider::LinearVertical)
        return;

    const float cx = (float) x + (float) width * 0.5f;

    // NOTE: JUCE's minSliderPos / maxSliderPos are the two thumbs of a two-value slider, NOT the ends of the
    // range -- on a normal slider both sit at value 0, which collapsed the track to nothing. The real ends
    // of travel come from getPositionOfValue() (same coordinate space as sliderPos).
    const float top    = (float) s.getPositionOfValue (s.getMaximum());   // pixel position of the slider's maximum
    const float bottom = (float) s.getPositionOfValue (s.getMinimum());   // ... and of its minimum

    // Recessed track
    Rectangle<float> track (cx - 3.0f, top, 6.0f, bottom - top);
    g.setColour (findColour (Palette::trackBg));
    g.fillRoundedRectangle (track, 3.0f);
    g.setColour (findColour (Palette::trackEdge).withAlpha (0.55f));
    g.drawRoundedRectangle (track.reduced (0.5f), 2.5f, 1.0f);

    // Scale: 8 divisions, longer ticks at the ends and the middle. For the dB sliders these fall every 6 dB,
    // level with the numbers on the neighbouring meter.
    constexpr int divisions = 8;
    for (int i = 0; i <= divisions; ++i)
    {
        const float ty = top + (bottom - top) * (float) i / (float) divisions;
        const bool major = (i % 4) == 0;
        const float len = major ? 6.0f : 3.0f;

        g.setColour (findColour (Palette::textDim).withAlpha (major ? 0.85f : 0.45f));
        g.fillRect (cx - 4.5f - len, ty - 0.5f, len, 1.0f);
        g.fillRect (cx + 4.5f, ty - 0.5f, len, 1.0f);
    }

    // Bipolar sliders fill outwards from their centre (0), unipolar ones from the bottom.
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float origin = bipolar ? (float) s.getPositionOfValue (0.0) : bottom;

    const float fillTop = jmin (sliderPos, origin), fillBottom = jmax (sliderPos, origin);
    if (fillBottom - fillTop > 0.5f)
    {
        const Rectangle<float> fill (cx - 2.0f, fillTop, 4.0f, fillBottom - fillTop);
        const Colour a = findColour (Palette::accent2), b = findColour (Palette::neon);

        g.setColour (b.withAlpha (light ? 0.16f : 0.22f));                    // soft glow
        g.fillRoundedRectangle (fill.expanded (2.5f, 0.0f), 3.5f);

        g.setGradientFill (ColourGradient (a, 0.0f, fill.getY(), b, 0.0f, fill.getBottom(), false));
        g.fillRoundedRectangle (fill, 2.0f);
    }

    // Fader cap
    const float halfH = (float) getSliderThumbRadius (s);
    const Rectangle<float> cap (cx - 12.0f, sliderPos - halfH, 24.0f, halfH * 2.0f);
    const bool active = s.isMouseOverOrDragging();

    g.setColour (Colours::black.withAlpha (light ? 0.18f : 0.40f));         // drop shadow
    g.fillRoundedRectangle (cap.translated (0.0f, 1.6f).expanded (0.5f), 4.5f);

    g.setGradientFill (ColourGradient (Colour (light ? 0xffffffff : 0xfff4f6fa), 0.0f, cap.getY(),
                                       Colour (light ? 0xffd9dee9 : 0xffbfc6d4), 0.0f, cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 4.0f);

    g.setColour (active ? findColour (Palette::neonBright) : Colour (light ? 0xff9aa3b6 : 0xff1a1d26).withAlpha (light ? 1.0f : 0.7f));
    g.drawRoundedRectangle (cap.reduced (0.5f), 3.5f, 1.0f);

    g.setColour (active ? findColour (Palette::neonBright) : findColour (Palette::neon));
    g.fillRoundedRectangle (cap.getX() + 5.0f, cap.getCentreY() - 0.9f, cap.getWidth() - 10.0f, 1.8f, 0.9f);
}

void ChromaLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool isMouseOver, bool isDown)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    constexpr float cr = 6.0f;

    if (b.getToggleState())
    {
        g.setColour (findColour (Palette::neon).withAlpha (0.30f));          // glow
        g.fillRoundedRectangle (r.expanded (1.5f), cr + 1.5f);
        g.setGradientFill (ColourGradient (findColour (Palette::onTop),    r.getX(), r.getY(),
                                           findColour (Palette::onBottom), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, cr);
        g.setColour (Colours::white.withAlpha (0.20f));                       // top-lit edge
        g.drawRoundedRectangle (r.reduced (0.5f), cr, 1.0f);
    }
    else
    {
        const Colour face = findColour (isMouseOver ? Palette::buttonFaceHover : Palette::buttonFace);
        g.setGradientFill (ColourGradient (face.brighter (light ? 0.0f : 0.10f), r.getX(), r.getY(),
                                           face.darker (light ? 0.04f : 0.0f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, cr);
        g.setColour (findColour (isMouseOver ? Palette::neonDim : Palette::chromeEdge));
        g.drawRoundedRectangle (r, cr, 1.0f);
    }

    if (isDown)
    {
        g.setColour (Colours::black.withAlpha (0.2f));
        g.fillRoundedRectangle (r, cr);
    }
}

void ChromaLookAndFeel::drawLabel (Graphics& g, Label& label)
{
    auto r = label.getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (findColour (Palette::field));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (findColour (label.isBeingEdited() ? Palette::neonBright : Palette::panelEdge));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    if (! label.isBeingEdited())
    {
        g.setColour (label.findColour (Label::textColourId));
        g.setFont (getLabelFont (label));
        g.drawFittedText (label.getText(), label.getLocalBounds().reduced (2, 0), Justification::centred, 1);
    }
}

Font ChromaLookAndFeel::getTextButtonFont (TextButton&, int)
{
    return uiFontMedium (11.0f);
}

Font ChromaLookAndFeel::getLabelFont (Label&)
{
    return uiFontMedium (11.0f);
}

//==============================================================================
BandSplitStrip::BandSplitStrip (ChromaInflatorAudioProcessor& p)
    : proc (p)
{
    params[0] = p.apvts.getParameter (ParamIDs::lowXover);
    params[1] = p.apvts.getParameter (ParamIDs::highXover);

    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1)));

    smoothDb.fill (dbFloor);

    learnWindow.resize ((size_t) learnSize);
    for (int i = 0; i < learnSize; ++i)
        learnWindow[(size_t) i] = 0.5f * (1.0f - std::cos (MathConstants<float>::twoPi * (float) i / (float) learnSize));
    learnPull.resize ((size_t) learnSize);
    learnFftData.resize ((size_t) learnSize * 2);
    learnPower.assign ((size_t) learnSize / 2 + 1, 0.0);
}

void BandSplitStrip::startLearn()
{
    learnPower.assign (learnPower.size(), 0.0);
    learnQueue.clear();
    learnFrames = 0;
    learnGood = learnElapsed = 0.0;
    learnProgress = 0.0f;
    learnState = LearnState::listening;
    while (proc.popLearnSamples (learnPull.data(), learnSize) > 0) {}   // drop stale audio
    proc.setLearnActive (true);
}

void BandSplitStrip::cancelLearn()
{
    proc.setLearnActive (false);
    learnState = LearnState::idle;
    learnProgress = 0.0f;
}

void BandSplitStrip::learnStep (float dt)
{
    learnElapsed += dt;
    const double sr = proc.getUiSampleRate();

    for (int guard = 0; guard < 64; ++guard)
    {
        const int n = proc.popLearnSamples (learnPull.data(), learnSize);
        if (n <= 0)
            break;
        learnQueue.insert (learnQueue.end(), learnPull.begin(), learnPull.begin() + n);
    }

    while ((int) learnQueue.size() >= learnSize && learnGood < learnSeconds)
    {
        double ms = 0.0;
        for (int i = 0; i < learnSize; ++i)
            ms += (double) learnQueue[(size_t) i] * learnQueue[(size_t) i];
        ms /= learnSize;

        // Only listen to real signal: frames quieter than about -70 dBFS RMS (silence, stopped transport) are skipped.
        if (ms > 1.0e-7)
        {
            for (int i = 0; i < learnSize; ++i)
            {
                learnFftData[(size_t) i] = learnQueue[(size_t) i] * learnWindow[(size_t) i];
                learnFftData[(size_t) (i + learnSize)] = 0.0f;
            }
            learnFft.performRealOnlyForwardTransform (learnFftData.data());

            // Each frame is normalised to equal weight, so one loud hit cannot outvote the rest of the passage.
            double frameSum = 0.0;
            for (int k = 1; k < learnSize / 2; ++k)
            {
                const double re = learnFftData[(size_t) (2 * k)], im = learnFftData[(size_t) (2 * k + 1)];
                frameSum += re * re + im * im;
            }
            if (frameSum > 0.0)
            {
                for (int k = 1; k < learnSize / 2; ++k)
                {
                    const double re = learnFftData[(size_t) (2 * k)], im = learnFftData[(size_t) (2 * k + 1)];
                    learnPower[(size_t) k] += (re * re + im * im) / frameSum;
                }
                ++learnFrames;
                learnGood += (double) learnHop / sr;
            }
        }
        learnQueue.erase (learnQueue.begin(), learnQueue.begin() + learnHop);
    }

    learnProgress = (float) jlimit (0.0, 1.0, learnGood / learnSeconds);

    if (learnGood >= learnSeconds)
        finishLearn();
    else if (learnElapsed > learnTimeout)
    {
        proc.setLearnActive (false);
        learnState = LearnState::noSignal;
    }
}

void BandSplitStrip::finishLearn()
{
    proc.setLearnActive (false);

    const double sr = proc.getUiSampleRate();
    const double binHz = sr / (double) learnSize;
    const int nBins = learnSize / 2;

    // Pure equal-energy split between 30 Hz and 16 kHz (pink noise lands on about 240 Hz / 1.9 kHz).
    std::vector<double> cum ((size_t) nBins + 1, 0.0);
    for (int k = 1; k < nBins; ++k)
    {
        const double f = k * binHz;
        double w = 0.0;
        if (f >= 30.0 && f <= 16000.0)
            w = learnPower[(size_t) k];
        cum[(size_t) k] = cum[(size_t) k - 1] + w;
    }

    const double total = cum[(size_t) nBins - 1];
    if (learnFrames <= 0 || total <= 0.0)
    {
        learnState = LearnState::noSignal;
        return;
    }

    auto freqAtShare = [&] (double share)
    {
        const double target = total * share;
        int k = 1;
        while (k < nBins - 1 && cum[(size_t) k] < target) ++k;
        const double lo = cum[(size_t) k - 1], hi = cum[(size_t) k];
        const double frac = hi > lo ? (target - lo) / (hi - lo) : 0.5;     // interpolate inside the bin
        return (float) ((k - 1 + frac + 0.5) * binHz);
    };

    for (int line = 0; line < 2; ++line)
    {
        if (params[line] == nullptr)
            continue;

        const float hz = freqAtShare (line == 0 ? 1.0 / 3.0 : 2.0 / 3.0);
        const auto range = params[line]->getNormalisableRange();
        params[line]->beginChangeGesture();
        params[line]->setValueNotifyingHost (params[line]->convertTo0to1 (jlimit (range.start, range.end, hz)));
        params[line]->endChangeGesture();
    }

    learnState = LearnState::done;
    repaint();
}

void BandSplitStrip::setActive (bool shouldRun)
{
    active = shouldRun;
    if (! shouldRun && learnState == LearnState::listening)
        cancelLearn();
    proc.setSpectrumActive (shouldRun);
    accum = 0.0f;

    if (shouldRun)
    {
        frame.fill (0.0f);
        smoothDb.fill (dbFloor);
        while (proc.popSpectrumSamples (pullBuffer.data(), fftSize) > 0) {}   // drop anything stale
        rebuildPath();
    }
}

float BandSplitStrip::xForFreq (float hz) const noexcept
{
    return (float) getWidth() * std::log (jlimit (fMin, fMax, hz) / fMin) / std::log (fMax / fMin);
}

float BandSplitStrip::freqForX (float x) const noexcept
{
    const float t = jlimit (0.0f, 1.0f, x / jmax (1.0f, (float) getWidth()));
    return fMin * std::pow (fMax / fMin, t);
}

float BandSplitStrip::currentFreq (int line) const
{
    return params[line] != nullptr ? params[line]->convertFrom0to1 (params[line]->getValue())
                                   : (line == 0 ? 200.0f : 2500.0f);
}

void BandSplitStrip::setFreq (int line, float hz)
{
    if (params[line] == nullptr)
        return;

    // Keep at least an octave between the two lines so they can never cross.
    const float other = currentFreq (1 - line);
    hz = line == 0 ? jmin (hz, other * 0.5f) : jmax (hz, other * 2.0f);

    const auto range = params[line]->getNormalisableRange();
    hz = jlimit (range.start, range.end, hz);
    params[line]->setValueNotifyingHost (params[line]->convertTo0to1 (hz));
}

int BandSplitStrip::lineAt (float x) const noexcept
{
    // Always the nearest line, so a click anywhere on the strip grabs one.
    const float dLow  = std::abs (x - xForFreq (currentFreq (0)));
    const float dHigh = std::abs (x - xForFreq (currentFreq (1)));

    return dLow <= dHigh ? 0 : 1;
}

void BandSplitStrip::mouseDown (const MouseEvent& e)
{
    dragging = lineAt (e.position.x);

    if (params[dragging] == nullptr)
    {
        dragging = -1;
        return;
    }

    params[dragging]->beginChangeGesture();

    // Clicked away from the line: bring it to the cursor (then it keeps following the drag)
    if (std::abs (e.position.x - xForFreq (currentFreq (dragging))) > 14.0f)
        setFreq (dragging, freqForX (e.position.x));

    repaint();
}

void BandSplitStrip::mouseDrag (const MouseEvent& e)
{
    if (dragging >= 0)
    {
        setFreq (dragging, freqForX (e.position.x));
        repaint();   // follow the cursor immediately, not at the spectrum's 30 fps
    }
}

void BandSplitStrip::mouseUp (const MouseEvent&)
{
    if (dragging >= 0 && params[dragging] != nullptr)
        params[dragging]->endChangeGesture();

    dragging = -1;
    repaint();
}

void BandSplitStrip::mouseDoubleClick (const MouseEvent&)
{
    // The second mouse-down of the double click has already opened the gesture for this line.
    if (dragging >= 0 && params[dragging] != nullptr)
    {
        params[dragging]->setValueNotifyingHost (params[dragging]->getDefaultValue());
        repaint();
    }
}

void BandSplitStrip::mouseMove (const MouseEvent& e)
{
    juce::ignoreUnused (e);
    setMouseCursor (MouseCursor::LeftRightResizeCursor);
}

void BandSplitStrip::tick (float dt)
{
    if (! active)
        return;

    if (learnState == LearnState::listening)
        learnStep (dt);

    accum += dt;
    if (accum < 1.0f / 30.0f)
        return;
    accum = 0.0f;

    bool gotAudio = false;
    for (int guard = 0; guard < 8; ++guard)
    {
        const int n = proc.popSpectrumSamples (pullBuffer.data(), fftSize);
        if (n <= 0)
            break;

        gotAudio = true;
        if (n >= fftSize)
        {
            std::copy (pullBuffer.begin(), pullBuffer.begin() + fftSize, frame.begin());
        }
        else
        {
            std::move (frame.begin() + n, frame.end(), frame.begin());
            std::copy (pullBuffer.begin(), pullBuffer.begin() + n, frame.end() - n);
        }
    }

    if (gotAudio)
    {
        for (int i = 0; i < fftSize; ++i)
        {
            fftData[(size_t) i] = frame[(size_t) i] * window[(size_t) i];
            fftData[(size_t) (i + fftSize)] = 0.0f;
        }

        fft.performFrequencyOnlyForwardTransform (fftData.data());

        // A full-scale sine reads 0 dB (Hann window: peak magnitude = N / 4).
        const float norm = 4.0f / (float) fftSize;
        for (int k = 0; k < fftSize / 2; ++k)
        {
            const float db = 20.0f * std::log10 (jmax (fftData[(size_t) k] * norm, 1.0e-6f));
            float& s = smoothDb[(size_t) k];
            s += (db - s) * (db > s ? 0.5f : 0.15f);
            s = jmax (s, dbFloor);
        }
    }
    else
    {
        for (auto& s : smoothDb)        // no audio (transport stopped): let the picture fall away
            s = jmax (dbFloor, s - 1.5f);
    }

    rebuildPath();
    repaint();
}

void BandSplitStrip::rebuildPath()
{
    spectrumPath.clear();

    const float w = (float) getWidth(), h = (float) getHeight();
    if (w < 4.0f || h < 4.0f)
        return;

    const float binHz = (float) proc.getUiSampleRate() / (float) fftSize;

    spectrumPath.startNewSubPath (0.0f, h);
    for (float x = 0.0f; x <= w; x += 2.0f)
    {
        const float f   = freqForX (x);
        const float pos = f / binHz;
        const int   k0  = jlimit (0, fftSize / 2 - 2, (int) pos);
        const float fr  = jlimit (0.0f, 1.0f, pos - (float) k0);

        float db = smoothDb[(size_t) k0] + (smoothDb[(size_t) k0 + 1] - smoothDb[(size_t) k0]) * fr;
        db += 4.5f * std::log2 (f / 1000.0f);   // tilt: music falls ~4.5 dB/octave, this keeps the picture level

        const float y = h * (1.0f - jlimit (0.0f, 1.0f, (db - dbFloor) / (dbTop - dbFloor)));
        spectrumPath.lineTo (x, y);
    }
    spectrumPath.lineTo (w, h);
    spectrumPath.closeSubPath();
}

void BandSplitStrip::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float w = b.getWidth(), h = b.getHeight();

    g.setColour (findColour (Palette::trackBg));
    g.fillRoundedRectangle (b, 5.0f);

    Path inside;
    inside.addRoundedRectangle (b, 5.0f);
    Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (inside);

    const float xLow = xForFreq (currentFreq (0)), xHigh = xForFreq (currentFreq (1));

    // The three bands, lightly tinted
    g.setColour (findColour (Palette::neon).withAlpha (0.16f));
    g.fillRect (0.0f, 0.0f, xLow, h);
    g.setColour (findColour (Palette::accent2).withAlpha (0.13f));
    g.fillRect (xLow, 0.0f, xHigh - xLow, h);
    g.setColour (findColour (Palette::warn).withAlpha (0.16f));
    g.fillRect (xHigh, 0.0f, w - xHigh, h);

    // Faint grid at 100 Hz, 1 kHz, 10 kHz
    g.setColour (findColour (Palette::textDim).withAlpha (0.18f));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
        g.fillRect (xForFreq (f) - 0.5f, 0.0f, 1.0f, h);

    // Spectrum
    g.setColour (findColour (Palette::textDim).withAlpha (0.55f));
    g.fillPath (spectrumPath);

    // Band names
    g.setColour (findColour (Palette::label).withAlpha (0.85f));
    g.setFont (captionFont (9.0f));
    g.drawText ("LOW",  Rectangle<float> (0.0f, h - 14.0f, xLow, 12.0f), Justification::centred);
    g.drawText ("MID",  Rectangle<float> (xLow, h - 14.0f, xHigh - xLow, 12.0f), Justification::centred);
    g.drawText ("HIGH", Rectangle<float> (xHigh, h - 14.0f, w - xHigh, 12.0f), Justification::centred);

    // The two draggable lines, with a handle and the frequency
    auto freqText = [] (float hz)
    {
        return hz >= 1000.0f ? String (hz / 1000.0f, 2) + " kHz" : String ((int) std::lround (hz)) + " Hz";
    };

    for (int line = 0; line < 2; ++line)
    {
        const float x = line == 0 ? xLow : xHigh;
        const bool hot = (dragging == line);

        g.setColour (findColour (hot ? Palette::neonBright : Palette::title));
        g.fillRect (x - 0.75f, 0.0f, 1.5f, h);
        g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre ({ x, 7.0f }));

        g.setFont (uiFontMedium (9.5f));
        const float tw = 46.0f;
        const bool flip = x + 8.0f + tw > w;
        g.drawText (freqText (currentFreq (line)),
                    Rectangle<float> (flip ? x - 8.0f - tw : x + 8.0f, 1.0f, tw, 12.0f),
                    flip ? Justification::centredRight : Justification::centredLeft);
    }
}

//==============================================================================
LevelMeter::LevelMeter (ChromaInflatorAudioProcessor& p, bool measuresOutput)
    : proc (p), isOutput (measuresOutput)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
}

float LevelMeter::yForDb (float db) const noexcept
{
    return (float) getHeight() * (1.0f - chroma::meterFraction (db));
}

void LevelMeter::tick (float dt)
{
    constexpr float fallDbPerSec = 28.0f, holdSeconds = 1.5f, holdFallDbPerSec = 40.0f;
    constexpr float floorDb = chroma::meterFloorDb;
    bool changed = false;

    for (int c = 0; c < 2; ++c)
    {
        const float peak = isOutput ? proc.takeOutputPeak (c) : proc.takeInputPeak (c);
        const float db   = peak > 1.0e-5f ? Decibels::gainToDecibels (peak) : floorDb;

        const float oldLevel = levelDb[c], oldHold = holdDb[c];

        // fast attack, steady fall
        levelDb[c] = db >= levelDb[c] ? db : jmax (db, levelDb[c] - fallDbPerSec * dt);

        // peak hold: stays put for a moment, then drops
        if (levelDb[c] >= holdDb[c])
        {
            holdDb[c] = levelDb[c];
            holdAge[c] = 0.0f;
        }
        else
        {
            holdAge[c] += dt;
            if (holdAge[c] > holdSeconds)
                holdDb[c] = jmax (levelDb[c], holdDb[c] - holdFallDbPerSec * dt);
        }

        changed = changed || std::abs (levelDb[c] - oldLevel) > 0.05f || std::abs (holdDb[c] - oldHold) > 0.05f;
    }

    if (changed)
        repaint();
}

void LevelMeter::mouseDown (const MouseEvent&)
{
    for (int c = 0; c < 2; ++c)
    {
        holdDb[c] = levelDb[c];
        holdAge[c] = 0.0f;
    }
    repaint();
}

void LevelMeter::paint (Graphics& g)
{
    const float h = (float) getHeight();
    constexpr float gap = 2.0f;
    constexpr float barW = ((float) barsWidth - gap) * 0.5f;
    constexpr float floorDb = chroma::meterFloorDb;

    const float yZero = yForDb (0.0f), yMinus3 = yForDb (-3.0f);
    const Colour red = brightRed();

    for (int c = 0; c < 2; ++c)
    {
        const Rectangle<float> bar ((float) c * (barW + gap), 0.0f, barW, h);

        g.setColour (findColour (Palette::trackBg));
        g.fillRoundedRectangle (bar, 2.0f);

        Path inside;
        inside.addRoundedRectangle (bar, 2.0f);

        {
            Graphics::ScopedSaveState saved (g);
            g.reduceClipRegion (inside);

            const float bottom = bar.getBottom();
            const float db = levelDb[c];

            if (db > floorDb)
            {
                // body: neon at the bottom easing to cyan, up to -3 dB
                const float yBody = jmax (yForDb (jmin (db, -3.0f)), yMinus3);
                g.setGradientFill (ColourGradient (findColour (Palette::neon), 0.0f, bottom,
                                                   findColour (Palette::accent2), 0.0f, yForDb (-18.0f), false));
                g.fillRect (bar.getX(), yBody, barW, bottom - yBody);

                if (db > -3.0f)    // amber zone, -3 .. 0 dB
                {
                    const float y = yForDb (jmin (db, 0.0f));
                    g.setColour (findColour (Palette::warn));
                    g.fillRect (bar.getX(), y, barW, yMinus3 - y);
                }

                if (db > 0.0f)     // over 0 dBFS
                {
                    const float y = yForDb (db);
                    g.setColour (red);
                    g.fillRect (bar.getX(), y, barW, yZero - y);
                }
            }

            // peak-hold tick
            if (holdDb[c] > floorDb + 1.0f)
            {
                g.setColour (holdDb[c] > 0.0f ? red : findColour (Palette::title));
                g.fillRect (bar.getX(), yForDb (holdDb[c]) - 0.75f, barW, 1.5f);
            }
        }

        g.setColour (findColour (Palette::trackEdge).withAlpha (0.45f));
        g.drawRoundedRectangle (bar.reduced (0.5f), 2.0f, 1.0f);
    }

    // 6 dB scale: one short line per fader tick (+6 at the top, -42 at the bottom), numbers every 12 dB
    g.setFont (uiFont (8.5f));
    for (int i = 0; i <= 8; ++i)
    {
        const float db = chroma::meterTopDb - 6.0f * (float) i;
        const float y = h * (float) i / 8.0f;
        const bool numbered = (i % 2) == 1;           // 0, -12, -24, -36

        g.setColour (findColour (Palette::textDim).withAlpha (numbered ? 0.85f : 0.45f));
        g.fillRect ((float) barsWidth + 2.0f, y - 0.5f, numbered ? 4.0f : 2.5f, 1.0f);

        if (numbered)
            g.drawText (String ((int) std::lround (db)), barsWidth + 8, (int) std::lround (y) - 5, totalWidth - barsWidth - 8, 10,
                        Justification::centredLeft);
    }

    // 0 dB reference line across both bars
    g.setColour (findColour (Palette::textDim).withAlpha (0.8f));
    g.fillRect (-1.0f, yZero - 0.5f, (float) barsWidth + 2.0f, 1.0f);
}

//==============================================================================
void ToggleSwitch::animate (float dtSeconds)
{
    if (pos < 0.0f)      // not shown yet: the first paint places the knob without animating
        return;

    const float target = getToggleState() ? 1.0f : 0.0f;
    if (pos == target)
        return;

    pos = chroma::easeToward (pos, target, dtSeconds, 0.045f);

    if (std::abs (target - pos) < 0.003f)
        pos = target;

    repaint();
}

void ToggleSwitch::paintButton (Graphics& g, bool isMouseOver, bool isButtonDown)
{
    const bool on = getToggleState();
    const float target = on ? 1.0f : 0.0f;

    if (pos < 0.0f)
        pos = target;          // first paint: no animation

    const Colour green (0xff22c55e), red = brightRed();
    const Colour state = on ? green : red;

    auto r = getLocalBounds().toFloat().reduced (1.0f);

    // row
    const Colour face = findColour (isMouseOver ? Palette::buttonFaceHover : Palette::buttonFace);
    g.setGradientFill (ColourGradient (face.brighter (0.08f), r.getX(), r.getY(), face, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (findColour (isMouseOver ? Palette::neonDim : Palette::chromeEdge));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    // switch
    constexpr float trackW = 26.0f, trackH = 14.0f;
    Rectangle<float> track (r.getRight() - 6.0f - trackW, r.getCentreY() - trackH * 0.5f, trackW, trackH);

    g.setColour (green.withAlpha (0.28f * pos));                               // glow while on
    g.fillRoundedRectangle (track.expanded (2.0f), (trackH + 4.0f) * 0.5f);

    g.setColour (red.interpolatedWith (green, pos));
    g.fillRoundedRectangle (track, trackH * 0.5f);

    const float d = trackH - 4.0f;
    const float kx = track.getX() + 2.0f + pos * (trackW - 4.0f - d);
    const Rectangle<float> knob (kx, track.getY() + 2.0f, d, d);
    g.setColour (Colours::black.withAlpha (0.25f));
    g.fillEllipse (knob.translated (0.0f, 0.8f));
    g.setColour (Colour (0xfff3f4f7));
    g.fillEllipse (knob);

    // label + state text
    g.setColour (findColour (Palette::title));
    g.setFont (uiFontMedium (11.0f));
    g.drawText (getButtonText(), Rectangle<float> (r.getX() + 8.0f, r.getY(), track.getX() - 29.0f - (r.getX() + 8.0f), r.getHeight()), Justification::centredLeft);

    g.setColour (state);
    g.setFont (captionFont (9.5f));
    g.drawText (on ? "ON" : "OFF", Rectangle<float> (track.getX() - 29.0f, r.getY(), 24.0f, r.getHeight()), Justification::centredRight);

    if (isButtonDown)
    {
        g.setColour (Colours::black.withAlpha (0.15f));
        g.fillRoundedRectangle (r, 6.0f);
    }
}

//==============================================================================
Rectangle<float> ClipModeRow::segmentArea() const noexcept
{
    constexpr float w = 132.0f;
    return Rectangle<float> ((float) getWidth() - w - 1.0f, 1.0f, w, (float) getHeight() - 2.0f);
}

void ClipModeRow::paint (Graphics& g)
{
    g.setColour (findColour (Palette::label));
    g.setFont (captionFont (9.5f));
    g.drawText ("CLIP MODE", Rectangle<float> (8.0f, 0.0f, 110.0f, (float) getHeight()), Justification::centredLeft);

    const auto seg = segmentArea();
    g.setColour (findColour (Palette::trackBg));
    g.fillRoundedRectangle (seg, 6.0f);
    g.setColour (findColour (Palette::chromeEdge));
    g.drawRoundedRectangle (seg.reduced (0.5f), 6.0f, 1.0f);

    const float half = seg.getWidth() * 0.5f;
    const Rectangle<float> halves[2] { seg.withWidth (half), seg.withTrimmedLeft (half) };
    const char* names[2] { "HARD", "SOFT" };

    for (int i = 0; i < 2; ++i)
    {
        const bool on = (selected == i);

        if (on)
        {
            const auto f = halves[i].reduced (2.0f);
            g.setGradientFill (ColourGradient (findColour (Palette::onTop), 0.0f, f.getY(),
                                               findColour (Palette::onBottom), 0.0f, f.getBottom(), false));
            g.fillRoundedRectangle (f, 4.5f);
        }

        g.setColour (on ? Colours::white : findColour (Palette::buttonText));
        g.setFont (captionFont (9.5f));
        g.drawText (names[i], halves[i], Justification::centred);
    }
}

void ClipModeRow::mouseDown (const MouseEvent& e)
{
    const auto seg = segmentArea();

    if (seg.contains (e.position) && onSelect)
        onSelect (e.position.x < seg.getCentreX() ? 0 : 1);
}

//==============================================================================
void LearnButton::setStatus (BandSplitStrip::LearnState s, float p)
{
    if (s != state || std::abs (p - progress) > 0.005f)
    {
        state = s;
        progress = p;
        repaint();
    }
}

void LearnButton::paintButton (Graphics& g, bool isMouseOver, bool isButtonDown)
{
    using LS = BandSplitStrip::LearnState;
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = isEnabled();

    g.setColour (findColour (Palette::buttonFace).withAlpha (isMouseOver && on ? 1.0f : 0.6f));
    g.fillRoundedRectangle (r, 5.0f);

    if (state == LS::listening)
    {
        Path clipPath;
        clipPath.addRoundedRectangle (r, 5.0f);
        Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (clipPath);
        g.setColour (findColour (Palette::neon).withAlpha (0.35f));
        g.fillRect (r.withWidth (r.getWidth() * progress));
    }

    Colour edge = findColour (isMouseOver && on ? Palette::neonDim : Palette::chromeEdge);
    if (state == LS::listening) edge = findColour (Palette::neon);
    if (state == LS::noSignal)  edge = findColour (Palette::warn);
    g.setColour (edge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    String label = "LEARN";
    if (state == LS::listening) label = String ((int) std::lround (progress * 100.0f)) + "%";
    if (state == LS::done)      label = "DONE";
    if (state == LS::noSignal)  label = "NO SIGNAL";

    g.setColour (findColour (state == LS::listening || state == LS::done ? Palette::neonBright
                                                                          : (isMouseOver && on ? Palette::title : Palette::textDim))
                     .withMultipliedAlpha ((on ? 1.0f : 0.4f) * (isButtonDown ? 0.7f : 1.0f)));
    g.setFont (captionFont (10.0f));
    g.drawText (label, getLocalBounds(), Justification::centred);
}

void ResetButton::paintButton (Graphics& g, bool isMouseOver, bool isButtonDown)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (findColour (Palette::buttonFace).withAlpha (isMouseOver ? 1.0f : 0.0f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (findColour (isMouseOver ? Palette::neonDim : Palette::chromeEdge));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    g.setColour (findColour (isMouseOver ? Palette::title : Palette::textDim).withMultipliedAlpha (isButtonDown ? 0.7f : 1.0f));
    g.setFont (captionFont (10.0f));
    g.drawText ("RESET", getLocalBounds(), Justification::centred);
}

void ThemeToggle::paintButton (Graphics& g, bool isMouseOver, bool)
{
    const bool lightMode = getToggleState();
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    // track
    g.setColour (findColour (Palette::meterBg));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (isMouseOver ? findColour (Palette::neonBright) : findColour (Palette::chromeEdge));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

    // knob: moon in dark mode (left), sun in light mode (right)
    const float d = r.getHeight() - 4.0f;
    Rectangle<float> knob (lightMode ? r.getRight() - 2.0f - d : r.getX() + 2.0f, r.getY() + 2.0f, d, d);
    const Colour knobColour (lightMode ? 0xfff7f8fa : 0xffc9ced6);
    g.setColour (knobColour);
    g.fillEllipse (knob);

    const auto c = knob.getCentre();
    if (lightMode)
    {
        const Colour sun (0xffe08a00);
        g.setColour (sun);
        g.fillEllipse (Rectangle<float> (4.4f, 4.4f).withCentre (c));
        for (int i = 0; i < 8; ++i)
        {
            const float a = (float) i * MathConstants<float>::pi * 0.25f;
            g.drawLine (c.x + std::cos (a) * 3.1f, c.y + std::sin (a) * 3.1f,
                        c.x + std::cos (a) * 4.4f, c.y + std::sin (a) * 4.4f, 1.0f);
        }
    }
    else
    {
        g.setColour (Colour (0xff23306b));
        g.fillEllipse (Rectangle<float> (6.6f, 6.6f).withCentre (c));
        g.setColour (knobColour);                                   // bite out of the circle -> crescent
        g.fillEllipse (Rectangle<float> (5.6f, 5.6f).withCentre (c.translated (1.7f, -1.3f)));
    }
}

//==============================================================================
void PresetTab::paintButton (Graphics& g, bool isMouseOver, bool isButtonDown)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);

    const Colour face = findColour (isMouseOver ? Palette::buttonFaceHover : Palette::buttonFace);
    g.setGradientFill (ColourGradient (face.brighter (0.08f), r.getX(), r.getY(), face, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (findColour (isMouseOver || isButtonDown ? Palette::neon : Palette::chromeEdge));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    g.setColour (findColour (Palette::textDim));
    g.setFont (captionFont (9.5f));
    g.drawText ("PRESET", Rectangle<float> (r.getX() + 12.0f, r.getY(), 52.0f, r.getHeight()), Justification::centredLeft);

    g.setColour (findColour (Palette::title));
    g.setFont (uiFontMedium (12.0f));
    g.drawText (name, Rectangle<float> (r.getX() + 68.0f, r.getY(), r.getWidth() - 68.0f - 28.0f, r.getHeight()),
                Justification::centredLeft);

    // chevron
    const auto c = Point<float> (r.getRight() - 15.0f, r.getCentreY());
    Path chev;
    chev.startNewSubPath (c.x - 4.0f, c.y - 2.0f);
    chev.lineTo (c.x, c.y + 2.0f);
    chev.lineTo (c.x + 4.0f, c.y - 2.0f);
    g.setColour (findColour (Palette::neonBright));
    g.strokePath (chev, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

//==============================================================================
PresetBrowser::PresetBrowser()
{
    list.setRowHeight (26);
    list.setMouseMoveSelectsRows (true);
    list.setColour (ListBox::backgroundColourId, Colours::transparentBlack);
    list.setColour (ListBox::outlineColourId,    Colours::transparentBlack);
    addAndMakeVisible (list);

    addAndMakeVisible (browseButton);
    browseButton.onClick = [this] { if (onBrowse) onBrowse(); };

    refresh();
}

void PresetBrowser::refresh()
{
    entries.clear();
    entries.push_back ({ "init", File() });

    auto files = presetDir().findChildFiles (File::findFiles, false, "*.chromapreset");
    std::sort (files.begin(), files.end(), [] (const File& a, const File& b)
    {
        return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0;
    });

    for (auto& f : files)
        entries.push_back ({ f.getFileNameWithoutExtension(), f });

    list.updateContent();
    list.repaint();
    repaint();
}

void PresetBrowser::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    g.setGradientFill (ColourGradient (findColour (Palette::panelTop), 0.0f, r.getY(),
                                       findColour (Palette::panel),    0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (findColour (Palette::neon).withAlpha (0.7f));
    g.drawRoundedRectangle (r, 10.0f, 1.0f);

    g.setColour (findColour (Palette::neonBright));
    g.setFont (captionFont (10.0f));
    g.drawText ("PRESETS", 14, 0, 100, headerH, Justification::centredLeft);

    fadingRule (g, 10.0f, (float) headerH, (float) getWidth() - 20.0f, findColour (Palette::panelEdge));

    if (entries.size() <= 1)
    {
        g.setColour (findColour (Palette::textDim));
        g.setFont (uiFont (10.0f));
        g.drawText ("No saved presets yet. Use SAVE to add one.",
                    Rectangle<int> (16, list.getY() + list.getRowHeight() + 6, getWidth() - 32, 16),
                    Justification::centredLeft);
    }
}

void PresetBrowser::resized()
{
    browseButton.setBounds (getWidth() - 10 - 96, 5, 96, 20);
    list.setBounds (6, headerH + 3, getWidth() - 12, getHeight() - headerH - 9);
}

void PresetBrowser::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (! isPositiveAndBelow (row, (int) entries.size()))
        return;

    const auto& e = entries[(size_t) row];
    const bool isCurrent = e.name == current;
    auto r = Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (2.0f, 1.0f);

    if (selected)
    {
        g.setColour (findColour (Palette::neon).withAlpha (0.22f));
        g.fillRoundedRectangle (r, 5.0f);
    }

    g.setColour (findColour (isCurrent ? Palette::neonBright : Palette::title));
    g.setFont (isCurrent ? uiFontMedium (11.0f) : uiFont (11.0f));
    g.drawText (e.name, r.reduced (10.0f, 0.0f), Justification::centredLeft);

    if (e.file == File())
    {
        g.setColour (findColour (Palette::textDim));
        g.setFont (captionFont (8.0f));
        g.drawText ("FACTORY", r.reduced (10.0f, 0.0f), Justification::centredRight);
    }
}

void PresetBrowser::listBoxItemClicked (int row, const MouseEvent&)
{
    if (isPositiveAndBelow (row, (int) entries.size()) && onChoose)
        onChoose (entries[(size_t) row]);
}

PresetOverlay::PresetOverlay()
{
    addAndMakeVisible (browser);
}

void PresetOverlay::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.50f));
    softShadow (g, browser.getBounds().toFloat(), 10.0f, 0.9f);
}

void PresetOverlay::resized()
{
    browser.setBounds (8, 64, getWidth() - 16, 206);
}

void PresetOverlay::mouseDown (const MouseEvent& e)
{
    if (! browser.getBounds().contains (e.getPosition()))
        setVisible (false);
}

//==============================================================================
ConfirmOverlay::ConfirmOverlay()
{
    addAndMakeVisible (cancelButton);
    addAndMakeVisible (confirmButton);

    cancelButton.setMouseCursor (MouseCursor::PointingHandCursor);
    confirmButton.setMouseCursor (MouseCursor::PointingHandCursor);
    confirmButton.setColour (TextButton::textColourOffId, brightRed());

    cancelButton.onClick  = [this] { dismiss(); };
    confirmButton.onClick = [this]
    {
        auto action = std::move (onConfirm);   // hide first, then act: the action may rebuild parts of the UI
        onConfirm = nullptr;
        setVisible (false);

        if (action)
            action();
    };
}

void ConfirmOverlay::show (const String& heading, const String& message,
                           const String& confirmText, std::function<void()> onConfirmAction)
{
    headingText = heading;
    messageText = message;
    onConfirm   = std::move (onConfirmAction);
    confirmButton.setButtonText (confirmText);

    setVisible (true);
    toFront (false);
    repaint();
}

void ConfirmOverlay::dismiss()
{
    onConfirm = nullptr;
    setVisible (false);
}

void ConfirmOverlay::resized()
{
    card = getLocalBounds().withSizeKeepingCentre (jmin (300, getWidth() - 24), 116);

    auto row = card.reduced (14, 12).removeFromBottom (24);
    confirmButton.setBounds (row.removeFromRight (88));
    row.removeFromRight (8);
    cancelButton.setBounds (row.removeFromRight (88));
}

void ConfirmOverlay::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.55f));

    const auto r = card.toFloat();
    softShadow (g, r, 10.0f, 1.0f);

    g.setGradientFill (ColourGradient (findColour (Palette::panelTop), 0.0f, r.getY(),
                                       findColour (Palette::panel),    0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (findColour (Palette::neon).withAlpha (0.7f));
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

    g.setColour (brightRed());
    g.setFont (captionFont (10.0f));
    g.drawText (headingText, card.getX() + 14, card.getY() + 10, card.getWidth() - 28, 16, Justification::centredLeft);

    g.setColour (findColour (Palette::title));
    g.setFont (uiFont (11.5f));
    g.drawFittedText (messageText, card.reduced (14, 0).withTrimmedTop (32).withTrimmedBottom (44),
                      Justification::topLeft, 3);
}

void ConfirmOverlay::mouseDown (const MouseEvent& e)
{
    if (! card.contains (e.getPosition()))
        dismiss();
}

//==============================================================================
void InfoBox::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.5f);

    softShadow (g, r, 6.0f, 0.7f);
    g.setColour (findColour (Palette::infoBg));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (findColour (Palette::neon));
    g.drawRoundedRectangle (r, 6.0f, 1.2f);

    const Font normal = uiFont (11.0f);
    const Font bold = uiFontMedium (11.0f);
    const Colour text = findColour (Palette::title);

    AttributedString a;
    a.setJustification (Justification::topLeft);
    a.setLineSpacing (2.0f);
    a.append ("Makes your sound louder and fuller without squashing it like a limiter.\n", normal, text);
    a.append ("EFFECT", bold, findColour (Palette::neonBright));
    a.append (": how much is added.\n", normal, text);
    a.append ("CURVE", bold, findColour (Palette::neonBright));
    a.append (": the tone of what is added.\n", normal, text);
    a.append ("Works on a track, a group, or the whole mix.\n", normal, text);
    a.append (brandText(), uiFontMedium (10.0f), findColour (Palette::textDim));
    a.draw (g, r.reduced (11.0f, 9.0f));
}

//==============================================================================
ChromaInflatorUI::ChromaInflatorUI (ChromaInflatorAudioProcessor& p)
    : audioProcessor (p), splitStrip (p), inputMeter (p, false), outputMeter (p, true)
{
    setLookAndFeel (&lnf);
    setOpaque (true);   // paint() fills the whole area, so the host never has to blend behind us

    setupSlider (inputSlider,  "Input gain");
    setupSlider (effectSlider, "Effect amount");
    setupSlider (curveSlider,  "Curve");
    setupSlider (outputSlider, "Output gain");

    for (auto* b : std::initializer_list<Button*> { &clipButton, &splitButton, &effectInButton, &gainMatchButton, &hqButton })
    {
        b->setClickingTogglesState (true);
        b->setMouseCursor (MouseCursor::PointingHandCursor);
        addAndMakeVisible (*b);
    }

    addChildComponent (splitStrip);   // visible only while SPLIT is on (see onVBlank)
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    auto& apvts = audioProcessor.apvts;
    inputAtt    = std::make_unique<SliderAttachment> (apvts, ParamIDs::input,  inputSlider);
    effectAtt   = std::make_unique<SliderAttachment> (apvts, ParamIDs::effect, effectSlider);
    curveAtt    = std::make_unique<SliderAttachment> (apvts, ParamIDs::curve,  curveSlider);
    outputAtt   = std::make_unique<SliderAttachment> (apvts, ParamIDs::output, outputSlider);
    clipAtt     = std::make_unique<ButtonAttachment> (apvts, ParamIDs::clip,      clipButton);
    splitAtt    = std::make_unique<ButtonAttachment> (apvts, ParamIDs::bandSplit, splitButton);
    effectInAtt = std::make_unique<ButtonAttachment> (apvts, ParamIDs::effectIn,  effectInButton);
    hqAtt       = std::make_unique<ButtonAttachment> (apvts, ParamIDs::hq,        hqButton);
    gainMatchAtt = std::make_unique<ButtonAttachment> (apvts, ParamIDs::gainMatch, gainMatchButton);

    // Clip mode: Hard | Soft selector.
    addAndMakeVisible (clipModeRow);
    clipModeRow.onSelect = [this] (int mode)
    {
        if (auto* p = audioProcessor.apvts.getParameter (ParamIDs::clipMode))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (mode != 0 ? 1.0f : 0.0f);
            p->endChangeGesture();
        }
    };

    // Cycles 4x -> 8x -> 16x; only active while HQ is on (base is always 2x).
    addAndMakeVisible (hqFactorButton);
    hqFactorButton.onClick = [this]
    {
        if (auto* p = audioProcessor.apvts.getParameter (ParamIDs::hqFactor))
        {
            const int next = ((int) std::lround (p->convertFrom0to1 (p->getValue())) + 1) % 3;
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) next));
            p->endChangeGesture();
        }
    };

    // Preset tab (opens the floating browser) + separate save button
    presetName = apvts.state.getProperty ("presetName", "init").toString();
    presetTab.setPresetName (presetName);
    presetTab.setMouseCursor (MouseCursor::PointingHandCursor);
    presetTab.onClick = [this] { openPresetBrowser(); };
    addAndMakeVisible (presetTab);

    saveButton.setMouseCursor (MouseCursor::PointingHandCursor);
    saveButton.onClick = [this] { savePreset(); };
    addAndMakeVisible (saveButton);

    deleteButton.setMouseCursor (MouseCursor::PointingHandCursor);
    deleteButton.setColour (TextButton::textColourOffId, brightRed());
    deleteButton.onClick = [this] { deletePreset(); };
    addAndMakeVisible (deleteButton);

    {
        const String path = apvts.state.getProperty ("presetPath", "").toString();
        presetFile = path.isNotEmpty() ? File (path) : File();
        updateDeleteButton();
    }

    // Hover help (appears after the cursor rests on the control for 2 s, see tooltipWindow)
    clipButton.setTooltip ("CLIP\n"
                           "Keeps the final output from going over\n"
                           "0 dB, after the Output gain. The Clip\n"
                           "mode row picks a hard or a soft clipper.");
    clipModeRow.setTooltip ("CLIP MODE\n"
                               "HARD: a flat ceiling at 0 dB.\n"
                               "SOFT: eases into 0 dB with a gentle\n"
                               "curve, no hard edge.");
    splitButton.setTooltip ("SPLIT\n"
                            "Splits the sound into 3 bands, inflates\n"
                            "each one separately, then mixes them back.\n"
                            "Drag the two lines in the Band Split strip to\n"
                            "move the crossovers (double-click resets).");
    gainMatchButton.setTooltip ("GAIN MATCH\n"
                                "Keeps the processed sound as loud as\n"
                                "the input (about 3 s RMS average), so\n"
                                "on/off comparisons are loudness-fair.");
    effectInButton.setTooltip ("EFFECT\n"
                               "Turns the effect on or off so you\n"
                               "can compare before and after.\n"
                               "Input and Output gain still apply.");

    presetOverlay.browser.onChoose = [this] (const PresetBrowser::Entry& e) { choosePreset (e); };
    presetOverlay.browser.onBrowse = [this] { browseForPreset(); };

    addAndMakeVisible (learnButton);
    learnButton.setMouseCursor (MouseCursor::PointingHandCursor);
    learnButton.setTooltip ("LEARN\n"
                            "Plays the input for ~4 s and listens.\n"
                            "Sets both crossovers so LOW / MID / HIGH\n"
                            "each carry an equal share of the energy.\n"
                            "Needs SPLIT on and audio playing.");
    learnButton.onClick = [this]
    {
        if (splitStrip.getLearnState() == BandSplitStrip::LearnState::listening)
            splitStrip.cancelLearn();
        else
            splitStrip.startLearn();
    };

    addAndMakeVisible (resetButton);
    resetButton.setMouseCursor (MouseCursor::PointingHandCursor);
    resetButton.onClick = [this] { resetToDefaults(); };

    // Dark by default; the choice is remembered in the plugin state so it survives closing the window.
    const bool startLight = (bool) audioProcessor.apvts.state.getProperty ("uiLight", false);
    lnf.setLight (startLight);
    themeToggle.setToggleState (startLight, dontSendNotification);
    themeToggle.setMouseCursor (MouseCursor::PointingHandCursor);
    themeToggle.onClick = [this] { applyTheme (themeToggle.getToggleState()); };
    addAndMakeVisible (themeToggle);

    addChildComponent (infoBox);         // hidden until the header is clicked
    addChildComponent (presetOverlay);
    addChildComponent (confirmOverlay);  // added last so it sits above everything

    setSize (designWidth, designHeight);

    // Animations and the Band Split strip are driven by the display's vertical blank instead of a fixed timer: they update
    // exactly once per frame (60 / 120 / 144 Hz), in step with what the screen shows.
    vblank = VBlankAttachment (this, [this] (double) { onVBlank(); });
}

ChromaInflatorUI::~ChromaInflatorUI()
{
    audioProcessor.setSpectrumActive (false);   // the audio thread stops feeding the strip
    audioProcessor.setLearnActive (false);
    setLookAndFeel (nullptr);
}

void ChromaInflatorUI::setPresetName (const String& name, const File& file)
{
    presetName = name;
    presetFile = file;
    audioProcessor.apvts.state.setProperty ("presetName", name, nullptr);
    audioProcessor.apvts.state.setProperty ("presetPath", file == File() ? String() : file.getFullPathName(), nullptr);
    presetTab.setPresetName (name);
    updateDeleteButton();
}

// DELETE is only live for a saved preset that sits in the presets folder ("init" and presets
// loaded from elsewhere can't be deleted from here).
void ChromaInflatorUI::updateDeleteButton()
{
    const bool canDelete = presetFile != File() && presetFile.existsAsFile() && presetFile.isAChildOf (presetDir());
    deleteButton.setEnabled (canDelete);
    deleteButton.setAlpha (canDelete ? 1.0f : 0.4f);
}

void ChromaInflatorUI::deletePreset()
{
    if (! presetFile.existsAsFile())
    {
        updateDeleteButton();
        return;
    }

    const File target = presetFile;
    Component::SafePointer<ChromaInflatorUI> safeThis (this);

    infoBox.setVisible (false);
    presetOverlay.setVisible (false);

    confirmOverlay.show ("DELETE PRESET",
                         "Delete the preset \"" + presetName + "\"? It is moved to the Recycle Bin / Trash.",
                         "DELETE",
                         [safeThis, target]
    {
        if (safeThis == nullptr)   // the window closed meanwhile
            return;

        if (! target.moveToTrash())   // Recycle Bin / Trash first, hard delete as a fallback
            target.deleteFile();

        safeThis->resetToDefaults();  // back to "init" (factory settings)
    });
}

void ChromaInflatorUI::openPresetBrowser()
{
    infoBox.setVisible (false);
    presetOverlay.browser.setCurrentName (presetName);
    presetOverlay.browser.refresh();
    presetOverlay.setVisible (true);
    presetOverlay.toFront (false);
}

void ChromaInflatorUI::choosePreset (const PresetBrowser::Entry& entry)
{
    // Deferred: this is called from inside the list box, which we are about to hide.
    Component::SafePointer<ChromaInflatorUI> safeThis (this);
    MessageManager::callAsync ([safeThis, entry]
    {
        if (safeThis == nullptr)
            return;

        safeThis->presetOverlay.setVisible (false);

        if (entry.file == File())
            safeThis->resetToDefaults();       // "init"
        else
            safeThis->loadPresetFile (entry.file);
    });
}

void ChromaInflatorUI::browseForPreset()
{
    presetOverlay.setVisible (false);

    presetChooser = std::make_unique<FileChooser> ("Load preset", presetDir(), "*.chromapreset");

    Component::SafePointer<ChromaInflatorUI> safeThis (this);
    presetChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                [safeThis] (const FileChooser& fc)
    {
        auto file = fc.getResult();
        if (safeThis != nullptr && file.existsAsFile())
            safeThis->loadPresetFile (file);
    });
}

void ChromaInflatorUI::loadPresetFile (const File& file)
{
    auto xml = parseXML (file);
    auto& apvts = audioProcessor.apvts;

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = ValueTree::fromXml (*xml);
    tree.setProperty ("uiLight",  apvts.state.getProperty ("uiLight", false), nullptr);   // theme is not part of a preset
    tree.setProperty ("uiWidth",  apvts.state.getProperty ("uiWidth",  0),     nullptr);   // nor is window size
    tree.setProperty ("uiHeight", apvts.state.getProperty ("uiHeight", 0),     nullptr);
    tree.setProperty ("presetName", file.getFileNameWithoutExtension(), nullptr);
    tree.setProperty ("presetPath", file.getFullPathName(), nullptr);
    apvts.replaceState (tree);
    audioProcessor.resetMissingParamsToDefault (tree);   // presets saved before the newer parameters existed

    setPresetName (file.getFileNameWithoutExtension(), file);
}

void ChromaInflatorUI::savePreset()
{
    presetChooser = std::make_unique<FileChooser> ("Save preset",
                                                   presetDir().getChildFile ("My Preset.chromapreset"),
                                                   "*.chromapreset");

    Component::SafePointer<ChromaInflatorUI> safeThis (this);

    presetChooser->launchAsync (FileBrowserComponent::saveMode
                                    | FileBrowserComponent::canSelectFiles
                                    | FileBrowserComponent::warnAboutOverwriting,
                                [safeThis] (const FileChooser& fc)
    {
        auto file = fc.getResult();
        if (safeThis == nullptr || file == File())
            return;

        file = file.withFileExtension ("chromapreset");

        // Same XML the host gets from getStateInformation()
        auto state = safeThis->audioProcessor.apvts.copyState();
        state.removeProperty ("uiLight",  nullptr);       // presets hold sound settings only, not the theme
        state.removeProperty ("uiWidth",  nullptr);       // nor the window size
        state.removeProperty ("uiHeight", nullptr);
        state.removeProperty ("presetName", nullptr);
        state.removeProperty ("presetPath", nullptr);

        if (auto xml = state.createXml())
            if (xml->writeTo (file))
                safeThis->setPresetName (file.getFileNameWithoutExtension(), file);
    });
}

void ChromaInflatorUI::resetToDefaults()
{
    for (auto* param : audioProcessor.getParameters())
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->getDefaultValue());
        param->endChangeGesture();
    }

    setPresetName ("init");
}

void ChromaInflatorUI::applyTheme (bool light)
{
    lnf.setLight (light);
    audioProcessor.apvts.state.setProperty ("uiLight", light, nullptr);
    sendLookAndFeelChange();   // repaints everything with the new colours
}

void ChromaInflatorUI::mouseDown (const MouseEvent& e)
{
    // Controls handle their own clicks, so this only fires on empty parts of the window.
    if (e.y < 33)   // header strip
        infoBox.setVisible (! infoBox.isVisible());
}

void ChromaInflatorUI::setupSlider (Slider& s, const String& accessibleName)
{
    s.setSliderStyle (Slider::LinearVertical);
    s.setTextBoxStyle (Slider::TextBoxBelow, false, 52, 18);
    s.setMouseDragSensitivity (200);
    s.setTitle (accessibleName);   // read out by screen readers (JUCE accessibility)
    addAndMakeVisible (s);
}

void ChromaInflatorUI::onVBlank()
{
    // Real elapsed time since the previous frame: all motion below is expressed per second, not per frame.
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = lastFrameSeconds > 0.0 ? (float) jlimit (0.0, 0.1, now - lastFrameSeconds) : (1.0f / 60.0f);
    lastFrameSeconds = now;

    inputMeter.tick (dt);
    outputMeter.tick (dt);

    clipButton.animate (dt);
    splitButton.animate (dt);
    effectInButton.animate (dt);
    gainMatchButton.animate (dt);

    // Band Split strip: only on screen (and only fed by the audio thread) while SPLIT is on
    const int splitOn = splitButton.getToggleState() ? 1 : 0;
    if (splitOn != shownSplitOn)
    {
        shownSplitOn = splitOn;
        splitStrip.setVisible (splitOn != 0);
        splitStrip.setActive (splitOn != 0);
        repaint (splitPanel);
    }
    if (splitOn != 0)
        splitStrip.tick (dt);

    learnButton.setEnabled (splitOn != 0);
    {
        using LS = BandSplitStrip::LearnState;
        const auto ls = splitStrip.getLearnState();
        const double now = Time::getMillisecondCounterHiRes() * 0.001;
        if ((ls == LS::done || ls == LS::noSignal) && learnResultUntil == 0.0)
            learnResultUntil = now + 2.0;
        if (learnResultUntil != 0.0 && now > learnResultUntil)
        {
            splitStrip.cancelLearn();   // back to idle
            learnResultUntil = 0.0;
        }
        if (ls == LS::listening) learnResultUntil = 0.0;
        learnButton.setStatus (splitStrip.getLearnState(), splitStrip.getLearnProgress());
    }

    // CLIP MODE label / dimming: only touched when something changed
    const int clipMode = audioProcessor.apvts.getRawParameterValue (ParamIDs::clipMode)->load() > 0.5f ? 1 : 0;
    const int clipOn   = clipButton.getToggleState() ? 1 : 0;
    if (clipMode != shownClipMode)
    {
        shownClipMode = clipMode;
        clipModeRow.setSelected (clipMode);
    }
    if (clipOn != shownClipOn)
    {
        shownClipOn = clipOn;
        clipModeRow.setAlpha (clipOn != 0 ? 1.0f : 0.4f);
    }

    // HQ factor label / enabled state: only touched when something actually changed
    static const char* labels[] = { "4x", "8x", "16x" };
    const int idx  = jlimit (0, 2, (int) std::lround (audioProcessor.apvts.getRawParameterValue (ParamIDs::hqFactor)->load()));
    const int hqOn = hqButton.getToggleState() ? 1 : 0;

    if (idx != shownHqIndex)
    {
        shownHqIndex = idx;
        hqFactorButton.setButtonText (labels[idx]);
    }

    if (hqOn != shownHqOn)
    {
        shownHqOn = hqOn;
        hqFactorButton.setEnabled (hqOn != 0);
        hqFactorButton.setAlpha (hqOn != 0 ? 1.0f : 0.4f);
    }
}

//==============================================================================
void ChromaInflatorUI::paint (Graphics& g)
{
    const bool light = lnf.isLight();
    const float W = (float) getWidth(), H = (float) getHeight();
    constexpr int headerH = 30;

    // Background: a very gentle top-to-bottom gradient
    g.setGradientFill (ColourGradient (findColour (Palette::background), 0.0f, 0.0f,
                                       findColour (Palette::backgroundBottom), 0.0f, H, false));
    g.fillAll();

    // Header: bloom mark + letter-spaced title
    drawBloomMark (g, { 19.0f, 15.5f }, 0.82f, findColour (Palette::neon), findColour (Palette::accent2));

    {
        const Font titleFont = uiFontMedium (13.0f).withExtraKerningFactor (0.14f);
        AttributedString title;
        title.setJustification (Justification::centredLeft);
        title.setWordWrap (AttributedString::none);
        title.append ("CHROMA ", titleFont, findColour (Palette::title));
        title.append ("BLOOM", titleFont, findColour (Palette::neonBright));
        title.draw (g, Rectangle<float> (34.0f, 0.0f, 200.0f, (float) headerH - 1.0f));
    }

    fadingRule (g, 0.0f, (float) headerH - 1.0f, W, findColour (Palette::panelEdge));

    // Cards: soft shadow (light theme), gradient surface, hairline border, top highlight
    auto drawCard = [&] (Rectangle<int> r)
    {
        const auto rf = r.toFloat();

        if (light)
            softShadow (g, rf, 10.0f, 0.35f);

        g.setGradientFill (ColourGradient (findColour (Palette::panelTop), 0.0f, rf.getY(),
                                           findColour (Palette::panel),    0.0f, rf.getBottom(), false));
        g.fillRoundedRectangle (rf, 10.0f);
        g.setColour (findColour (Palette::panelEdge));
        g.drawRoundedRectangle (rf.reduced (0.5f), 10.0f, 1.0f);

        if (! light)
        {
            g.setColour (Colours::white.withAlpha (0.05f));
            g.fillRect (rf.getX() + 10.0f, rf.getY() + 1.0f, rf.getWidth() - 20.0f, 1.0f);
        }
    };

    // Titled card: accent dot + title, unit / hint on the right
    auto drawPanel = [&] (Rectangle<int> r, const String& title, const String& unit)
    {
        drawCard (r);
        const auto rf = r.toFloat();

        g.setColour (findColour (Palette::neon));
        g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre ({ rf.getX() + 14.0f, rf.getY() + 12.5f }));

        g.setColour (findColour (Palette::neonBright));
        g.setFont (captionFont (10.0f));
        g.drawText (title, r.getX() + 23, r.getY() + 5, r.getWidth() - 60, 15, Justification::centredLeft);

        if (unit.isNotEmpty())
        {
            g.setColour (findColour (Palette::textDim));
            g.setFont (uiFont (10.0f));
            g.drawText (unit, r.getRight() - 110, r.getY() + 5, 100, 15, Justification::centredRight);
        }

        fadingRule (g, rf.getX() + 8.0f, rf.getY() + 23.0f, rf.getWidth() - 16.0f, findColour (Palette::panelEdge));
    };

    drawPanel (inputPanel, "INPUT", "dB");
    drawPanel (processorPanel, "PROCESSOR", {});
    drawPanel (outputPanel, "OUTPUT", "dB");
    drawPanel (splitPanel, "BAND SPLIT", {});
    drawCard (togglePanel);

    if (! splitButton.getToggleState())
    {
        g.setColour (findColour (Palette::textDim));
        g.setFont (uiFont (11.0f));
        g.drawText ("Turn SPLIT on to set the crossovers", splitPanel.withTrimmedTop (26), Justification::centred);
    }
    else
    {
        // Frequency axis under the strip: 100 / 1k / 10k
        g.setColour (findColour (Palette::textDim));
        g.setFont (uiFont (9.0f));

        struct Tick { float hz; const char* text; };
        for (const auto& t : { Tick { 100.0f, "100" }, Tick { 1000.0f, "1k" }, Tick { 10000.0f, "10k" } })
        {
            const int cx = splitStrip.getX() + (int) std::lround (splitStrip.xForFreq (t.hz));
            g.drawText (t.text, cx - 18, splitStrip.getBottom() + 1, 36, 12, Justification::centred);
        }
    }

    // Effect / Curve captions sit between the fader and its value box
    g.setColour (findColour (Palette::label));
    g.setFont (uiFont (10.0f));
    g.drawText ("Effect", effectSlider.getX(), effectSlider.getBottom() - 18 - 13, effectSlider.getWidth(), 12, Justification::centred);
    g.drawText ("Curve",  curveSlider.getX(),  curveSlider.getBottom()  - 18 - 13, curveSlider.getWidth(),  12, Justification::centred);

    // Footer: brand + version on the left (RESET and the theme switch sit on the right)
    {
        AttributedString footer;
        footer.setJustification (Justification::centredLeft);
        footer.setWordWrap (AttributedString::none);
        footer.append (brandText(), uiFontMedium (10.5f), findColour (Palette::label));
       #ifdef JucePlugin_VersionString
        footer.append (String ("   v") + JucePlugin_VersionString, uiFont (10.0f), findColour (Palette::textDim));
       #endif
        footer.draw (g, Rectangle<float> (10.0f, H - 18.0f, 200.0f, 18.0f));
    }
}

void ChromaInflatorUI::resized()
{
    constexpr int footerH = 18;
    constexpr int presetY = 36, presetH = 24;
    constexpr int top = 66, margin = 4;

    // Header: HQ toggle + oversampling factor, right aligned
    hqFactorButton.setBounds (getWidth() - 10 - 32, 5, 32, 20);
    hqButton.setBounds (hqFactorButton.getX() - 4 - 36, 5, 36, 20);

    // Preset row: [ PRESET  init  v ] [SAVE] [DELETE]
    deleteButton.setBounds (getWidth() - 8 - 58, presetY, 58, presetH);
    saveButton.setBounds (deleteButton.getX() - 6 - 52, presetY, 52, presetH);
    presetTab.setBounds (8, presetY, saveButton.getX() - 6 - 8, presetH);

    infoBox.setBounds (10, 38, getWidth() - 20, 104);
    presetOverlay.setBounds (getLocalBounds());
    confirmOverlay.setBounds (getLocalBounds());

    // Footer: [Him'z Audio ©2024 ...]                [RESET] [dark/light]
    const int fy = getHeight() - footerH + 2;
    themeToggle.setBounds (getWidth() - 10 - 30, fy, 30, 14);
    resetButton.setBounds (themeToggle.getX() - 8 - 44, fy, 44, 14);

    // Three cards on top (Input | Processor | Output), then Band Split, then the toggle card
    constexpr int splitH = 98, toggleH = 98;
    const int stackBottom = getHeight() - footerH - margin;
    const int h = stackBottom - top - 2 * margin - splitH - toggleH;
    const int colW = (getWidth() - 4 * margin) / 3;

    inputPanel     = { margin, top, colW, h };
    processorPanel = { margin + colW + margin, top, colW, h };
    outputPanel    = { margin + 2 * (colW + margin), top, colW, h };
    splitPanel     = { margin, top + h + margin, getWidth() - 2 * margin, splitH };
    togglePanel    = { margin, splitPanel.getBottom() + margin, getWidth() - 2 * margin, toggleH };

    learnButton.setBounds (splitPanel.getRight() - 8 - 54, splitPanel.getY() + 4, 54, 16);

    // Band Split strip, leaving a line underneath for the 100 / 1k / 10k labels
    splitStrip.setBounds (splitPanel.getX() + 8, splitPanel.getY() + 28, splitPanel.getWidth() - 16, splitH - 28 - 7 - 13);

    // Faders: the value box is part of each slider and sits underneath
    const int sliderY = top + 30;
    const int sliderW = 56;
    const int pad     = 8;
    const int sliderH = (top + h) - 6 - sliderY;

    // Input / Output: fader + a meter beside it. The pair is nudged left so it stays centred in the card.
    constexpr int meterShift = 17;
    inputSlider.setBounds  (inputPanel.getX()  + (colW - sliderW) / 2 - meterShift, sliderY, sliderW, sliderH);
    outputSlider.setBounds (outputPanel.getX() + (colW - sliderW) / 2 - meterShift, sliderY, sliderW, sliderH);

    // The meter spans exactly the fader's track: its ends are read from the slider itself (the same numbers the
    // fader is drawn with), so the two stay aligned at any window size.
    auto placeMeter = [] (LevelMeter& m, const Slider& s)
    {
        constexpr int fromCentre = 20;
        const int top    = s.getY() + (int) std::lround (s.getPositionOfValue (s.getMaximum()));
        const int bottom = s.getY() + (int) std::lround (s.getPositionOfValue (s.getMinimum()));
        m.setBounds (s.getX() + s.getWidth() / 2 + fromCentre, top, LevelMeter::totalWidth, bottom - top);
    };
    placeMeter (inputMeter,  inputSlider);
    placeMeter (outputMeter, outputSlider);

    // Effect + Curve: a caption goes between the track and the value box
    for (auto* s : { &effectSlider, &curveSlider })
        s->getProperties().set ("captionGap", 14);

    effectSlider.setBounds (processorPanel.getX() + pad, sliderY, sliderW, sliderH);
    curveSlider.setBounds  (processorPanel.getRight() - pad - sliderW, sliderY, sliderW, sliderH);

    // Toggle card: 2 x 2 switches, then the clip mode row
    constexpr int rowH = 22, rowGap = 5, cardPad = 8;
    auto area = togglePanel.reduced (cardPad, cardPad);

    auto row1 = area.removeFromTop (rowH);
    area.removeFromTop (rowGap);
    auto row2 = area.removeFromTop (rowH);
    area.removeFromTop (cardPad - 2);
    auto row3 = area.removeFromTop (rowH);

    const int colGap = 6;
    const int half = (row1.getWidth() - colGap) / 2;

    effectInButton.setBounds (row1.removeFromLeft (half));
    row1.removeFromLeft (colGap);
    splitButton.setBounds (row1);

    clipButton.setBounds (row2.removeFromLeft (half));
    row2.removeFromLeft (colGap);
    gainMatchButton.setBounds (row2);

    clipModeRow.setBounds (row3);
}

//==============================================================================
ChromaInflatorAudioProcessorEditor::ChromaInflatorAudioProcessorEditor (ChromaInflatorAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), ui (p)
{
    addAndMakeVisible (ui);

    constexpr int w = ChromaInflatorUI::designWidth, h = ChromaInflatorUI::designHeight;
    constrainer.setMinimumSize (w, h);             // default size is also the minimum
    constrainer.setMaximumSize (w * 2, h * 2);     // up to 2x
    constrainer.setFixedAspectRatio ((double) w / (double) h);

    setConstrainer (&constrainer);
    setResizable (true, true);                     // host can resize + corner grip

    setOpaque (true);   // the UI fills every pixel, so the host never has to blend behind the window

    // Restore the last size the user dragged to in this project, if any; otherwise the design size
    // (also the minimum). Clamped in case a preset/state came from a window of a different limit.
    const int savedW = (int) p.apvts.state.getProperty ("uiWidth",  w);
    const int savedH = (int) p.apvts.state.getProperty ("uiHeight", h);
    setSize (jlimit (w, w * 2, savedW), jlimit (h, h * 2, savedH));
}

void ChromaInflatorAudioProcessorEditor::paint (Graphics& g)
{
    // Covers the 1 px sliver that rounding can leave beside the scaled UI at fractional scale factors.
    g.fillAll (ui.getBackgroundColour());
}

void ChromaInflatorAudioProcessorEditor::resized()
{
    // Lay the UI out at its design size, then scale it to fill the window.
    const float scale = (float) getWidth() / (float) ChromaInflatorUI::designWidth;
    ui.setTransform (AffineTransform::scale (scale));
    ui.setBounds (0, 0, ChromaInflatorUI::designWidth, ChromaInflatorUI::designHeight);

    // Remember the size the user dragged to, so reopening this window (closing and
    // reopening the plugin, or the host recreating the editor) restores it. Not part
    // of .chromapreset files, same as the theme -- see loadPresetFile()/savePreset().
    auto& state = audioProcessor.apvts.state;
    state.setProperty ("uiWidth",  getWidth(),  nullptr);
    state.setProperty ("uiHeight", getHeight(), nullptr);
}
