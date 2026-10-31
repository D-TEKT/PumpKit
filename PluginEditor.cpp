#include "PluginEditor.h"

namespace
{
    const juce::Colour kBackground (0xff0e0e14);
    const juce::Colour kPanel      (0xff17171f);
    const juce::Colour kAccent     (0xffff3d7f);
    const juce::Colour kText       (0xffe8e8f0);
    const juce::Colour kDim        (0xff8a8a9c);
    const juce::Colour kSidechain  (0xff35d0ff);
    const float kPointRadius = 5.5f;
    const float kHitRadius   = 11.0f;
}

//==============================================================================
CurveEditor::CurveEditor (PumpKitProcessor& p) : proc (p)
{
    startTimerHz (30);
}

juce::Rectangle<float> CurveEditor::plotArea() const
{
    return getLocalBounds().toFloat().reduced (14.0f, 16.0f);
}

juce::Point<float> CurveEditor::toScreen (const pump::Node& n) const
{
    const auto b = plotArea();
    return { b.getX() + n.x * b.getWidth(), b.getBottom() - n.y * b.getHeight() };
}

int CurveEditor::hitTest (juce::Point<float> p, const std::vector<pump::Node>& nodes) const
{
    int best = -1;
    float bestDist = kHitRadius;
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const float d = toScreen (nodes[i]).getDistanceFrom (p);
        if (d <= bestDist)
        {
            bestDist = d;
            best = (int) i;
        }
    }
    return best;
}

void CurveEditor::drawWave (juce::Graphics& g,
                            const std::array<std::atomic<float>, pump::kScopeBins>& bins,
                            juce::Rectangle<float> area, juce::Colour fill, juce::Colour line)
{
    const float mid = area.getCentreY();
    const float half = area.getHeight() * 0.5f;

    juce::Path top, shape;
    for (int i = 0; i < pump::kScopeBins; ++i)
    {
        const float a = std::min (1.0f, bins[(size_t) i].load (std::memory_order_relaxed));
        const float x = area.getX() + area.getWidth() * (float) i / (float) (pump::kScopeBins - 1);
        const float h = std::pow (a, 0.7f) * half;
        if (i == 0)
        {
            top.startNewSubPath (x, mid - h);
            shape.startNewSubPath (x, mid - h);
        }
        else
        {
            top.lineTo (x, mid - h);
            shape.lineTo (x, mid - h);
        }
    }
    for (int i = pump::kScopeBins - 1; i >= 0; --i)
    {
        const float a = std::min (1.0f, bins[(size_t) i].load (std::memory_order_relaxed));
        const float x = area.getX() + area.getWidth() * (float) i / (float) (pump::kScopeBins - 1);
        shape.lineTo (x, mid + std::pow (a, 0.7f) * half);
    }
    shape.closeSubPath();

    g.setColour (fill);
    g.fillPath (shape);
    if (! line.isTransparent())
    {
        g.setColour (line);
        g.strokePath (top, juce::PathStrokeType (1.2f));
    }
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

    // waveforms: sound going in (dim), sound coming out (bright), sidechain (blue)
    drawWave (g, proc.scopeIn,  b, juce::Colours::white.withAlpha (0.10f), juce::Colours::white.withAlpha (0.18f));
    drawWave (g, proc.scopeOut, b, juce::Colours::white.withAlpha (0.28f), juce::Colours::white.withAlpha (0.55f));
    drawWave (g, proc.scopeSc,  b, kSidechain.withAlpha (0.16f),           kSidechain.withAlpha (0.85f));

    // curve
    const auto nodes = proc.curve.getNodes();

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

    g.setColour (kAccent.withAlpha (0.14f));
    g.fillPath (fill);
    g.setColour (kAccent);
    g.strokePath (line, juce::PathStrokeType (2.2f));

    // playhead
    const float ph = proc.uiPhase.load();
    if (ph >= 0.0f)
    {
        const float px = b.getX() + ph * b.getWidth();
        const float py = b.getBottom() - proc.curve.eval (ph) * b.getHeight();
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawVerticalLine ((int) px, b.getY(), b.getBottom());
        g.setColour (juce::Colours::white);
        g.fillEllipse (px - 3.5f, py - 3.5f, 7.0f, 7.0f);
    }

    // points
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const auto c = toScreen (nodes[i]);
        const bool active = (int) i == hoverIndex || (int) i == dragIndex;
        const float r = active ? kPointRadius + 1.5f : kPointRadius;

        g.setColour (kBackground);
        g.fillEllipse (c.x - r - 2.0f, c.y - r - 2.0f, (r + 2.0f) * 2.0f, (r + 2.0f) * 2.0f);
        g.setColour (active ? juce::Colours::white : kAccent);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    }

    // value readout for the point under the mouse
    const int shown = dragIndex >= 0 ? dragIndex : hoverIndex;
    if (shown >= 0 && shown < (int) nodes.size())
    {
        const auto& n = nodes[(size_t) shown];
        const auto c = toScreen (n);
        const juce::String text = "time " + juce::String (juce::roundToInt (n.x * 100.0f)) + "%   volume "
                                + juce::String (juce::roundToInt (n.y * 100.0f)) + "%";
        const float w = 150.0f, h = 20.0f;
        float tx = c.x + 12.0f, ty = c.y - h - 8.0f;
        if (tx + w > getWidth() - 4.0f) tx = c.x - w - 12.0f;
        if (ty < 4.0f) ty = c.y + 10.0f;

        g.setColour (kBackground.withAlpha (0.92f));
        g.fillRoundedRectangle (tx, ty, w, h, 4.0f);
        g.setColour (kText);
        g.setFont (12.0f);
        g.drawText (text, juce::Rectangle<float> (tx, ty, w, h), juce::Justification::centred, false);
    }

    // legend and help
    g.setFont (11.5f);
    float lx = b.getX() + 4.0f;
    const float ly = getHeight() - 15.0f;
    auto legend = [&] (juce::Colour col, const juce::String& t)
    {
        g.setColour (col);
        g.fillEllipse (lx, ly + 3.0f, 7.0f, 7.0f);
        g.setColour (kDim);
        const int w = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t) + 6;
        g.drawText (t, (int) lx + 10, (int) ly, w, 14, juce::Justification::centredLeft, false);
        lx += 10.0f + (float) w + 10.0f;
    };
    legend (juce::Colours::white.withAlpha (0.5f), "sound in");
    legend (juce::Colours::white, "sound out");
    legend (kSidechain, "sidechain");

    g.setColour (kDim);
    g.drawText ("click = add point    drag = move    right-click a point = delete",
                getLocalBounds().reduced (16, 0).removeFromBottom (16).toFloat(),
                juce::Justification::centredRight, false);
}

void CurveEditor::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitTest (e.position, proc.curve.getNodes());
    if (h != hoverIndex)
    {
        hoverIndex = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::CrosshairCursor);
        repaint();
    }
}

void CurveEditor::mouseExit (const juce::MouseEvent&)
{
    hoverIndex = -1;
    repaint();
}

void CurveEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto nodes = proc.curve.getNodes();
    const int hit = hitTest (e.position, nodes);

    if (e.mods.isPopupMenu() || e.mods.isCtrlDown())
    {
        if (hit >= 0)
        {
            proc.curve.removeNode (hit);
            hoverIndex = -1;
        }
        dragIndex = -1;
        repaint();
        return;
    }

    if (hit >= 0)
    {
        dragIndex = hit;
    }
    else
    {
        const auto b = plotArea();
        const float x = juce::jlimit (0.0f, 1.0f, (e.position.x - b.getX()) / b.getWidth());
        float y = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - b.getY()) / b.getHeight());
        if (y < 0.02f) y = 0.0f;
        if (y > 0.98f) y = 1.0f;
        dragIndex = proc.curve.addNode (x, y);
    }

    hoverIndex = dragIndex;
    repaint();
}

void CurveEditor::dragTo (const juce::MouseEvent& e)
{
    if (dragIndex < 0)
        return;

    const auto b = plotArea();
    const float x = juce::jlimit (0.0f, 1.0f, (e.position.x - b.getX()) / b.getWidth());
    float y = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - b.getY()) / b.getHeight());

    // snap to fully ducked / fully open when close (hold Shift to turn this off)
    if (! e.mods.isShiftDown())
    {
        if (y < 0.02f) y = 0.0f;
        if (y > 0.98f) y = 1.0f;
    }

    proc.curve.moveNode (dragIndex, x, y);
    repaint();
}

void CurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    dragTo (e);
}

void CurveEditor::mouseUp (const juce::MouseEvent& e)
{
    dragIndex = -1;
    mouseMove (e);
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
    setSize (720, 490);

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
    setupLabel (presetLabel, "Preset");

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

    smoothLinesButton.setColour (juce::ToggleButton::textColourId, kText);
    smoothLinesButton.setColour (juce::ToggleButton::tickColourId, kAccent);
    smoothLinesButton.setTooltip ("Round off the corners between points");
    smoothLinesButton.onClick = [this]
    {
        proc.curve.setSmooth (smoothLinesButton.getToggleState());
        curveEditor.repaint();
    };
    addAndMakeVisible (smoothLinesButton);

    mixKnob.init (*this, "mix", "MIX");
    smoothKnob.init (*this, "smooth", "SMOOTH");
    freqKnob.init (*this, "splitFreq", "SPLIT FREQ");
    thresholdKnob.init (*this, "threshold", "THRESHOLD");
    retriggerKnob.init (*this, "retrigger", "RETRIGGER");

    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setColour (juce::Label::textColourId, kDim);
    statusLabel.setFont (juce::FontOptions (12.5f));
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
    statusLabel.setBounds (title.removeFromRight (470));

    r.removeFromTop (10);
    auto top = r.removeFromTop (28);
    modeLabel.setBounds (top.removeFromLeft (44));
    modeBox.setBounds (top.removeFromLeft (92));
    top.removeFromLeft (14);
    lengthLabel.setBounds (top.removeFromLeft (54));
    lengthBox.setBounds (top.removeFromLeft (112));
    top.removeFromLeft (14);
    presetLabel.setBounds (top.removeFromLeft (50));
    presetBox.setBounds (top.removeFromLeft (140));
    top.removeFromLeft (14);
    smoothLinesButton.setBounds (top.removeFromLeft (125));

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

    // keep the "Smooth lines" box in step when a project or preset loads
    if (smoothLinesButton.getToggleState() != proc.curve.isSmooth())
        smoothLinesButton.setToggleState (proc.curve.isSmooth(), juce::dontSendNotification);

    const float peak = proc.uiScPeak.exchange (0.0f);

    if (mode == 0)
        statusLabel.setText ("Sync: follows the host tempo while playing", juce::dontSendNotification);
    else if (mode == 1)
        statusLabel.setText ("MIDI: every note-on triggers the curve", juce::dontSendNotification);
    else
        statusLabel.setText (peak > 0.001f ? "Audio: sidechain signal detected"
                                           : "Audio: no sidechain signal. FL: send the kick with \"Sidechain to this track\", then plugin gear > Processing > pick it",
                             juce::dontSendNotification);
}
