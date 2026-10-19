#pragma once

#include "PluginProcessor.h"
#include "ChromaHelpers.h"
#include <functional>
#include <vector>

namespace Palette
{
    // Colour IDs. The actual colours live in ChromaLookAndFeel (dark or light set)
    // and are looked up with findColour(), so each plugin window can have its own theme.
    enum ColourIds
    {
        background = 0x2c000001,   // window background (top)
        field,                     // slider value boxes
        meterBg,                   // slider groove / meter bar background
        panel,                     // card surface (bottom)
        panelEdge,                 // card border / dividers
        neon,                      // accent
        neonBright,
        neonDim,
        chromeEdge,                // button border
        textDim,
        title,                     // header title / strong text
        label,                     // EFFECT / CURVE captions
        meterLabel,
        infoBg,
        onTop,                     // toggled-on button gradient (top)
        onBottom,                  // toggled-on button gradient (bottom)
        buttonFace,                // idle button surface
        buttonFaceHover,           // hovered button surface
        buttonText,                // idle button text
        trackBg,                   // solid recessed slider / meter track
        trackEdge,                 // track outline
        backgroundBottom,          // window background (bottom of the gradient)
        panelTop,                  // card surface (top of the gradient)
        accent2,                   // second accent (cyan): slider fill + meter gradient
        warn                       // amber: meter -3..0 dB zone
    };
}

//==============================================================================
// Modern look: layered cards, gradient faders with a soft glow, segmented meters, pill buttons.
class ChromaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ChromaLookAndFeel();

    void setLight (bool useLight);
    bool isLight() const noexcept { return light; }

    int getSliderThumbRadius (juce::Slider&) override { return 7; }   // half the height of the fader cap

    // Keeps the thumb fully inside the slider so the track ends sit at a known pixel position.
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool isMouseOver, bool isButtonDown) override;

    void drawLabel (juce::Graphics&, juce::Label&) override;

    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Font getLabelFont (juce::Label&) override;

private:
    bool light = false;
};

//==============================================================================
// Band Split strip: a small log-frequency spectrum (20 Hz - 20 kHz) with the three bands tinted and two
// draggable lines that set the crossovers. Drag a line to move it, double-click it to reset it.
// It only exists on screen while SPLIT is on, and only then does the audio thread feed it samples.
class BandSplitStrip : public juce::Component
{
public:
    explicit BandSplitStrip (ChromaInflatorAudioProcessor&);

    void setActive (bool shouldRun);    // starts / stops the spectrum feed
    void tick (float dtSeconds);        // called once per display frame while visible

    void paint (juce::Graphics&) override;
    void resized() override { rebuildPath(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    float xForFreq (float hz) const noexcept;   // log-frequency -> x inside this strip (used for the axis labels)

    // LEARN: listens to the dry input for a few seconds of real signal, averages its spectrum, and places both
    // crossovers so the LOW / MID / HIGH bands each carry an equal share of the energy.
    enum class LearnState { idle, listening, done, noSignal };
    void startLearn();
    void cancelLearn();
    LearnState getLearnState() const noexcept { return learnState; }
    float getLearnProgress() const noexcept   { return learnProgress; }

private:
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder;
    static constexpr float fMin = 20.0f, fMax = 20000.0f;
    static constexpr float dbFloor = -100.0f, dbTop = -10.0f;

    float freqForX (float x) const noexcept;
    float currentFreq (int line) const;
    void setFreq (int line, float hz);
    int lineAt (float x) const noexcept;
    void rebuildPath();
    void learnStep (float dtSeconds);
    void finishLearn();

    // learn pass
    static constexpr int learnOrder = 13, learnSize = 1 << learnOrder, learnHop = learnSize / 2;
    static constexpr double learnSeconds = 4.0, learnTimeout = 20.0;
    LearnState learnState = LearnState::idle;
    float learnProgress = 0.0f;
    double learnGood = 0.0, learnElapsed = 0.0;
    int learnFrames = 0;
    juce::dsp::FFT learnFft { learnOrder };
    std::vector<float> learnWindow, learnPull, learnQueue, learnFftData;
    std::vector<double> learnPower;

    ChromaInflatorAudioProcessor& proc;
    juce::RangedAudioParameter* params[2] { nullptr, nullptr };   // 0 = low crossover, 1 = high crossover
    int dragging = -1;
    bool active = false;
    float accum = 0.0f;

    juce::dsp::FFT fft { fftOrder };
    std::array<float, (size_t) fftSize> frame {}, window {}, pullBuffer {};
    std::array<float, (size_t) fftSize * 2> fftData {};
    std::array<float, (size_t) fftSize / 2> smoothDb {};
    juce::Path spectrumPath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandSplitStrip)
};

//==============================================================================
// Stereo peak meter that sits beside the Input / Output faders: two thin bars (L and R) plus a 6 dB scale.
// It is exactly as tall as the fader track and spans 48 dB (-42 .. +6 dBFS), so each 6 dB line is level with a
// fader tick. Fast attack, smooth fall, a peak-hold tick, amber above -3 dB, red above 0 dB. Click to clear the hold.
class LevelMeter : public juce::Component
{
public:
    static constexpr int barsWidth = 10, totalWidth = 30;

    LevelMeter (ChromaInflatorAudioProcessor&, bool measuresOutput);

    void tick (float dtSeconds);   // once per display frame: reads the audio thread's peaks and animates

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    float yForDb (float db) const noexcept;

    ChromaInflatorAudioProcessor& proc;
    const bool isOutput;
    float levelDb[2] { chroma::meterFloorDb, chroma::meterFloorDb };
    float holdDb[2]  { chroma::meterFloorDb, chroma::meterFloorDb };
    float holdAge[2] { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
};

//==============================================================================
// Labelled on/off switch row: green when on, red when off. Works with ButtonAttachment.
class ToggleSwitch : public juce::Button
{
public:
    explicit ToggleSwitch (const juce::String& label) : juce::Button (label) { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

    // Called once per display frame by the UI. Moves the knob towards its target in real time, so the
    // animation looks the same on 60 / 120 / 144 Hz screens and stops costing anything once it has settled.
    void animate (float dtSeconds);

private:
    float pos = -1.0f;   // knob position 0..1, animated; -1 = not yet shown
};

//==============================================================================
// "CLIP MODE   [ Hard | Soft ]" row. Selecting a half calls onSelect (0 = hard, 1 = soft).
class ClipModeRow : public juce::Component,
                    public juce::SettableTooltipClient
{
public:
    ClipModeRow() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    std::function<void (int)> onSelect;
    void setSelected (int mode) { if (mode != selected) { selected = mode; repaint(); } }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> segmentArea() const noexcept;
    int selected = 0;
};

//==============================================================================
// Small LEARN button on the Band Split card. Shows the learn progress as a fill while it listens.
class LearnButton : public juce::Button
{
public:
    LearnButton() : juce::Button ("Learn") {}
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;
    void setStatus (BandSplitStrip::LearnState s, float progress);
private:
    BandSplitStrip::LearnState state = BandSplitStrip::LearnState::idle;
    float progress = 0.0f;
};

//==============================================================================
// Footer RESET button: ghost style.
class ResetButton : public juce::TextButton
{
public:
    ResetButton() : juce::TextButton ("RESET") {}
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;
};

// Footer dark / light switch. Toggle state ON = light mode. Default (off) = dark.
class ThemeToggle : public juce::Button
{
public:
    ThemeToggle() : juce::Button ("Theme") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;
};

//==============================================================================
// The preset "tab": shows the current preset name ("init" by default). Click to open the browser.
class PresetTab : public juce::Button
{
public:
    PresetTab() : juce::Button ("Preset") {}
    void setPresetName (const juce::String& n) { name = n; repaint(); }
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

private:
    juce::String name { "init" };
};

// Floating preset list card.
class PresetBrowser : public juce::Component,
                      private juce::ListBoxModel
{
public:
    struct Entry
    {
        juce::String name;
        juce::File file;   // File() = the built-in "init" preset
    };

    PresetBrowser();

    void refresh();                                  // rescans the preset folder
    void setCurrentName (const juce::String& n) { current = n; list.repaint(); }

    std::function<void (const Entry&)> onChoose;
    std::function<void()> onBrowse;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override { return (int) entries.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    static constexpr int headerH = 30;

    juce::ListBox list { {}, this };
    juce::TextButton browseButton { "LOAD FILE..." };
    std::vector<Entry> entries;
    juce::String current { "init" };
};

// Covers the whole UI while the browser is open; a click outside the card closes it.
class PresetOverlay : public juce::Component
{
public:
    PresetOverlay();
    PresetBrowser browser;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
};

// In-UI confirmation card (used before deleting a preset). Built from our own buttons so the result is
// explicit -- it does not depend on how a native/JUCE message box numbers its buttons -- and it scales
// with the window and follows the dark/light theme.
class ConfirmOverlay : public juce::Component
{
public:
    ConfirmOverlay();

    void show (const juce::String& heading, const juce::String& message,
               const juce::String& confirmText, std::function<void()> onConfirmAction);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // click outside the card = cancel

private:
    void dismiss();

    juce::String headingText, messageText;
    juce::TextButton cancelButton { "CANCEL" }, confirmButton { "DELETE" };
    std::function<void()> onConfirm;
    juce::Rectangle<int> card;
};

//==============================================================================
// Small floating "what does this do" card. Click it to dismiss.
class InfoBox : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
};

//==============================================================================
// The whole UI, laid out at a fixed design size (400 x 498). The editor below scales it.
class ChromaInflatorUI : public juce::Component
{
public:
    static constexpr int designWidth = 400, designHeight = 498;

    explicit ChromaInflatorUI (ChromaInflatorAudioProcessor&);
    ~ChromaInflatorUI() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // click the header to show/hide the info card

    juce::Colour getBackgroundColour() const { return findColour (Palette::background); }

private:
    void onVBlank();   // runs once per display frame (vsync): animations + the Band Split strip
    void setupSlider (juce::Slider&, const juce::String& accessibleName);
    void savePreset();
    void openPresetBrowser();
    void choosePreset (const PresetBrowser::Entry&);
    void browseForPreset();
    void loadPresetFile (const juce::File&);
    void setPresetName (const juce::String&, const juce::File& file = juce::File());
    void deletePreset();
    void updateDeleteButton();
    void resetToDefaults();
    void applyTheme (bool light);

    ChromaInflatorAudioProcessor& audioProcessor;
    ChromaLookAndFeel lnf;

    juce::Slider inputSlider, effectSlider, curveSlider, outputSlider;
    ToggleSwitch clipButton { "CLIP" }, splitButton { "SPLIT" }, effectInButton { "EFFECT" }, gainMatchButton { "GAIN MATCH" };
    juce::TextButton hqButton { "HQ" }, hqFactorButton { "4x" };
    ClipModeRow clipModeRow;
    PresetTab presetTab;
    juce::TextButton saveButton { "SAVE" };
    juce::TextButton deleteButton { "DELETE" };
    std::unique_ptr<juce::FileChooser> presetChooser;
    ResetButton resetButton;
    ThemeToggle themeToggle;
    InfoBox infoBox;
    PresetOverlay presetOverlay;
    ConfirmOverlay confirmOverlay;
    BandSplitStrip splitStrip;
    LearnButton learnButton;
    double learnResultUntil = 0.0;
    LevelMeter inputMeter, outputMeter;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> inputAtt, effectAtt, curveAtt, outputAtt;
    std::unique_ptr<ButtonAttachment> clipAtt, splitAtt, effectInAtt, hqAtt, gainMatchAtt;

    double lastFrameSeconds = 0.0;
    int shownHqIndex = -1;
    int shownHqOn = -1;
    int shownClipMode = -1;
    int shownClipOn = -1;
    int shownSplitOn = -1;
    juce::String presetName { "init" };
    juce::File presetFile;   // file behind the current preset (File() for "init" / unsaved)

    juce::Rectangle<int> inputPanel, processorPanel, outputPanel, splitPanel, togglePanel;

    // Hover help for CLIP / SPLIT / EFFECT: shows after the cursor rests on a control for 2 seconds.
    // Added as a child of this UI so it also scales with the window (no separate desktop window).
    juce::TooltipWindow tooltipWindow { this, 2000 };

    // Declared last so it is destroyed first. Ticks at the display's refresh rate (60 / 120 / 144 Hz) and only
    // while the editor is actually on screen.
    juce::VBlankAttachment vblank;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaInflatorUI)
};

//==============================================================================
// Resizable plugin window: opens at 400 x 498 (also the minimum), up to 2x, aspect ratio locked.
class ChromaInflatorAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ChromaInflatorAudioProcessorEditor (ChromaInflatorAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ChromaInflatorAudioProcessor& audioProcessor;
    ChromaInflatorUI ui;
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaInflatorAudioProcessorEditor)
};
