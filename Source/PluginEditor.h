#pragma once

#include "PluginProcessor.h"

//==============================================================================
// The big curve display. Drag with the mouse to draw your own shape.
class CurveEditor : public juce::Component, private juce::Timer
{
public:
    explicit CurveEditor (PumpKitProcessor& p);
    ~CurveEditor() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { lastIdx = -1; }

private:
    juce::Rectangle<float> plotArea() const;
    void drawAt (const juce::MouseEvent&);
    void timerCallback() override { repaint(); }

    PumpKitProcessor& proc;
    int lastIdx = -1;
    float lastY = 0.0f;
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
    Knob mixKnob, smoothKnob, freqKnob, thresholdKnob, retriggerKnob;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt, lengthAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> splitAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PumpKitEditor)
};
