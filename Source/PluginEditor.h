#pragma once

#include "PluginProcessor.h"

//==============================================================================
// Shows the level of whatever triggers the curve, a marker for the Threshold,
// and a light that flashes every time the curve is triggered.
class SourceMeter : public juce::Component, private juce::Timer
{
public:
    explicit SourceMeter (PumpKitProcessor& p) : proc (p) { startTimerHz (30); }
    ~SourceMeter() override { stopTimer(); }

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    PumpKitProcessor& proc;
    int lastTrig = 0;
    float flash = 0.0f;
};

//==============================================================================
// The big curve display with the waveforms behind it.
//   click empty space   = add a point
//   drag a point        = move it
//   drag a small dot    = bend that line up or down
//   right-click a point = delete it, right-click a dot = straighten the line
class CurveEditor : public juce::Component, private juce::Timer
{
public:
    explicit CurveEditor (PumpKitProcessor& p);
    ~CurveEditor() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    enum class Kind { none, point, bend };
    struct Hit { Kind kind = Kind::none; int index = -1; };

    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> dotPos (const std::vector<pump::Node>& n, size_t i) const;
    static bool hasDot (const std::vector<pump::Node>& n, size_t i);
    Hit hitTest (juce::Point<float> p, const std::vector<pump::Node>& nodes) const;
    void dragTo (const juce::MouseEvent&);
    void drawWave (juce::Graphics&, const std::array<std::atomic<float>, pump::kScopeBins>& bins,
                   juce::Rectangle<float> area, juce::Colour fill, juce::Colour line, float lineWidth);
    void timerCallback() override { repaint(); }

    PumpKitProcessor& proc;
    Hit dragging, hovering;
};

//==============================================================================
// Menu colours that match the dark theme
class PumpLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PumpLookAndFeel();
};

// Panel that explains how to pick the sidechain track in FL Studio
class HelpPanel : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
    juce::Colour accent { 0xffff3b3b };
};

//==============================================================================
class PumpKitEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PumpKitEditor (PumpKitProcessor&);
    ~PumpKitEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void showSelection (int genre, int preset);   // update the button only
    void loadPreset (int genre, int preset);      // update the button and load the curve
    void stepPreset (int delta);
    void showPresetMenu();
    void updateRateControls();
    void applyTheme();

    struct Knob
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

        void init (PumpKitEditor& owner, const juce::String& paramId, const juce::String& text);
    };

    PumpKitProcessor& proc;
    PumpLookAndFeel laf;

    CurveEditor curveEditor;
    SourceMeter sourceMeter;
    HelpPanel helpPanel;
    juce::ComboBox modeBox, lengthBox, rateUnitBox, sourceBox;
    juce::TextButton presetButton, prevButton { "<" }, nextButton { ">" }, helpButton { "?" };
    juce::Slider rateHzSlider;
    juce::Label modeLabel, lengthLabel, genreLabel, sourceLabel, statusLabel;
    juce::ToggleButton splitButton { "Band Split" };
    Knob mixKnob, smoothKnob, freqKnob, thresholdKnob, retriggerKnob;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt, lengthAtt, sourceAtt, rateUnitAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rateHzAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> splitAtt;

    int shownGenre = -1, shownPreset = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PumpKitEditor)
};
