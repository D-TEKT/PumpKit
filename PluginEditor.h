#pragma once

#include "PluginProcessor.h"

//==============================================================================
// The big curve display with the waveforms behind it.
//   click empty space  = add a point
//   drag a point       = move it
//   right-click point  = delete it
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
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen (const pump::Node&) const;
    int hitTest (juce::Point<float> p, const std::vector<pump::Node>& nodes) const;
    void dragTo (const juce::MouseEvent&);
    void drawWave (juce::Graphics&, const std::array<std::atomic<float>, pump::kScopeBins>& bins,
                   juce::Rectangle<float> area, juce::Colour fill, juce::Colour line);
    void timerCallback() override { repaint(); }

    PumpKitProcessor& proc;
    int dragIndex = -1;
    int hoverIndex = -1;
};

//==============================================================================
class PumpKitEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PumpKitEditor (PumpKitProcessor&);
    ~PumpKitEditor() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

        void init (PumpKitEditor& owner, const juce::String& paramId, const juce::String& text);
    };

    PumpKitProcessor& proc;

    CurveEditor curveEditor;
    juce::ComboBox modeBox, lengthBox, presetBox;
    juce::Label modeLabel, lengthLabel, presetLabel, statusLabel;
    juce::ToggleButton splitButton { "Band Split" };
    juce::ToggleButton smoothLinesButton { "Smooth lines" };
    Knob mixKnob, smoothKnob, freqKnob, thresholdKnob, retriggerKnob;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt, lengthAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> splitAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PumpKitEditor)
};
