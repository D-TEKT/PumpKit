#include "PluginEditor.h"

namespace
{
    const juce::Colour kBackground (0xff0e0e14);
    const juce::Colour kPanel      (0xff17171f);
    const juce::Colour kAccent     (0xffff3d7f);
    const juce::Colour kText       (0xffe8e8f0);
    const juce::Colour kDim        (0xff8a8a9c);
}

//==============================================================================
CurveEditor::CurveEditor (PumpKitProcessor& p) : proc (p)
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    startTimerHz (30);
}

juce::Rectangle<float> CurveEditor::plotArea() const
{
    return getLocalBounds().toFloat().reduced (12.0f);
}

void CurveEditor::paint (juce::Graphics& g)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    const auto b = plotArea();

    // grid
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    for (int i = 0; i <= 4; ++i)
        g.drawVerticalLine ((int) (b.getX() + b.getWidth() * (float) i / 4.0f), b.getY(), b.getBottom());
    for (int i = 0; i <= 2; ++i)
        g.drawHorizontalLine ((int) (b.getY() + b.getHeight() * (float) i / 2.0f), b.getX(), b.getRight());

    // curve
    juce::Path line, fill;
    const int steps = std::max (2, (int) b.getWidth());
    for (int s = 0; s <= steps; ++s)
    {
        const float x01 = (float) s / (float) steps;
        const float px = b.getX() + x01 * b.getWidth();
        const float py = b.getBottom() - proc.curve.eval (x01) * b.getHeight();
        if (s == 0)
        {
            line.startNewSubPath (px, py);
            fill.startNewSubPath (px, b.getBottom());
            fill.lineTo (px, py);
        }
        else
        {
            line.lineTo (px, py);
            fill.lineTo (px, py);
        }
    }
    fill.lineTo (b.getRight(), b.getBottom());
    fill.closeSubPath();

    g.setColour (kAccent.withAlpha (0.22f));
    g.fillPath (fill);
    g.setColour (kAccent);
    g.strokePath (line, juce::PathStrokeType (2.0f));

    // playhead
    const float ph = proc.uiPhase.load();
    if (ph >= 0.0f)
    {
        const float px = b.getX() + ph * b.getWidth();
        const float py = b.getBottom() - proc.curve.eval (ph) * b.getHeight();
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawVerticalLine ((int) px, b.getY(), b.getBottom());
        g.setColour (juce::Colours::white);
        g.fillEllipse (px - 4.0f, py - 4.0f, 8.0f, 8.0f);
    }

    g.setColour (kDim);
    g.setFont (12.0f);
    g.drawText ("drag to draw your own curve", getLocalBounds().reduced (16, 8),
                juce::Justification::bottomRight, false);
}

void CurveEditor::mouseDown (const juce::MouseEvent& e)
{
    lastIdx = -1;
    drawAt (e);
}

void CurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    drawAt (e);
}

void CurveEditor::drawAt (const juce::MouseEvent& e)
{
    const auto b = plotArea();
    const float x = juce::jlimit (0.0f, 1.0f, (e.position.x - b.getX()) / b.getWidth());
    float y = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - b.getY()) / b.getHeight());

    // snap to fully ducked / fully open when close
    if (y < 0.03f) y = 0.0f;
    if (y > 0.97f) y = 1.0f;

    const int idx = juce::roundToInt (x * (float) (pump::kCurvePoints - 1));

    if (lastIdx >= 0 && lastIdx != idx)
    {
        const int step = idx > lastIdx ? 1 : -1;
        for (int k = lastIdx; k != idx + step; k += step)
        {
            const float t = (float) (k - lastIdx) / (float) (idx - lastIdx);
            proc.curve.setPoint (k, lastY + (y - lastY) * t);
        }
    }
    else
    {
        proc.curve.setPoint (idx, y);
    }

    lastIdx = idx;
    lastY = y;
    repaint();
}

//==============================================================================
void PumpKitEditor::Knob::init (PumpKitEditor& owner, const juce::String& paramId, const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    slider.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    slider.setColour (juce::Slider::thumbColourId, kText);
    slider.setColour (juce::Slider::textBoxTextColourId, kText);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    owner.addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, kDim);
    owner.addAndMakeVisible (label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (owner.proc.apvts, paramId, slider);
}

PumpKitEditor::PumpKitEditor (PumpKitProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), curveEditor (p)
{
    setSize (720, 470);

    addAndMakeVisible (curveEditor);

    auto setupLabel = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, kDim);
        l.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    };
    setupLabel (modeLabel, "Mode");
    setupLabel (lengthLabel, "Length");
    setupLabel (presetLabel, "Curve");

    if (auto* mode = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("mode")))
        modeBox.addItemList (mode->choices, 1);
    if (auto* len = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("length")))
        lengthBox.addItemList (len->choices, 1);
    for (int i = 0; i < pump::kNumPresets; ++i)
        presetBox.addItem (pump::presetName (i), i + 1);
    presetBox.setTextWhenNothingSelected ("Load a preset...");
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0)
        {
            proc.curve.setPreset (idx);
            curveEditor.repaint();
        }
    };

    addAndMakeVisible (modeBox);
    addAndMakeVisible (lengthBox);
    addAndMakeVisible (presetBox);

    modeAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mode", modeBox);
    lengthAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "length", lengthBox);

    splitButton.setColour (juce::ToggleButton::textColourId, kText);
    splitButton.setColour (juce::ToggleButton::tickColourId, kAccent);
    addAndMakeVisible (splitButton);
    splitAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "bandSplit", splitButton);

    mixKnob.init (*this, "mix", "MIX");
    smoothKnob.init (*this, "smooth", "SMOOTH");
    freqKnob.init (*this, "splitFreq", "SPLIT FREQ");
    thresholdKnob.init (*this, "threshold", "THRESHOLD");
    retriggerKnob.init (*this, "retrigger", "RETRIGGER");

    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setColour (juce::Label::textColourId, kDim);
    addAndMakeVisible (statusLabel);

    startTimerHz (10);
    timerCallback();
}

void PumpKitEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    g.setColour (kText);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("PUMPKIT", 20, 12, 200, 32, juce::Justification::centredLeft);

    g.setColour (kAccent);
    g.fillRect (20, 46, 36, 3);
}

void PumpKitEditor::resized()
{
    auto r = getLocalBounds().reduced (20, 14);

    auto title = r.removeFromTop (38);
    statusLabel.setBounds (title.removeFromRight (300));

    r.removeFromTop (10);
    auto top = r.removeFromTop (28);
    modeLabel.setBounds (top.removeFromLeft (44));
    modeBox.setBounds (top.removeFromLeft (100));
    top.removeFromLeft (18);
    lengthLabel.setBounds (top.removeFromLeft (54));
    lengthBox.setBounds (top.removeFromLeft (120));
    top.removeFromLeft (18);
    presetLabel.setBounds (top.removeFromLeft (46));
    presetBox.setBounds (top.removeFromLeft (150));

    r.removeFromTop (10);
    auto knobRow = r.removeFromBottom (140);
    r.removeFromBottom (10);
    curveEditor.setBounds (r);

    const int colW = knobRow.getWidth() / 6;
    auto place = [&] (Knob& k, juce::Rectangle<int> col)
    {
        k.label.setBounds (col.removeFromTop (18));
        k.slider.setBounds (col);
    };

    place (mixKnob,       knobRow.removeFromLeft (colW));
    place (smoothKnob,    knobRow.removeFromLeft (colW));
    splitButton.setBounds (knobRow.removeFromLeft (colW).withSizeKeepingCentre (colW - 10, 28));
    place (freqKnob,      knobRow.removeFromLeft (colW));
    place (thresholdKnob, knobRow.removeFromLeft (colW));
    place (retriggerKnob, knobRow);
}

void PumpKitEditor::timerCallback()
{
    const int mode = (int) proc.apvts.getRawParameterValue ("mode")->load();
    const bool audio = mode == 2;
    const bool split = proc.apvts.getRawParameterValue ("bandSplit")->load() > 0.5f;

    thresholdKnob.slider.setEnabled (audio);
    retriggerKnob.slider.setEnabled (audio);
    freqKnob.slider.setEnabled (split);

    const float peak = proc.uiScPeak.exchange (0.0f);

    if (mode == 0)
        statusLabel.setText ("Sync: follows the host tempo while playing", juce::dontSendNotification);
    else if (mode == 1)
        statusLabel.setText ("MIDI: every note-on triggers the curve", juce::dontSendNotification);
    else
        statusLabel.setText (peak > 0.001f ? "Audio: sidechain signal detected"
                                           : "Audio: no sidechain signal yet",
                             juce::dontSendNotification);
}
