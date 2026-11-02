#include "PluginEditor.h"

namespace
{
    const juce::Colour kBackground (0xff0c0c12);
    const juce::Colour kPanel      (0xff12121a);
    const juce::Colour kText       (0xffe8e8f0);
    const juce::Colour kDim        (0xff8a8a9c);
    const float kPointRadius = 5.5f;
    const float kHitRadius   = 11.0f;
    const float kDotRadius   = 4.5f;
    const float kDotHit      = 10.0f;

    // The colour of the preset group that is selected
    juce::Colour accentOf (const PumpKitProcessor& p)
    {
        const auto& all = pump::genres();
        const int g = juce::jlimit (0, (int) all.size() - 1, p.genreIndex.load());
        return juce::Colour (all[(size_t) g].colour);
    }
}

//==============================================================================
void SourceMeter::timerCallback()
{
    const int t = proc.trigCount.load();
    if (t != lastTrig)
    {
        lastTrig = t;
        flash = 1.0f;
    }
    else
    {
        flash *= 0.85f;
    }
    repaint();
}

void SourceMeter::paint (juce::Graphics& g)
{
    const auto accent = accentOf (proc);
    auto b = getLocalBounds().toFloat();

    g.setColour (kPanel);
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);

    auto led = b.removeFromRight (b.getHeight()).reduced (5.0f);
    auto bar = b.reduced (3.0f);

    const float level = proc.uiSrcLevel.load();
    const float db = 20.0f * std::log10 (std::max (level, 1.0e-5f));
    const float pos = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);

    if (pos > 0.0f)
    {
        g.setColour (accent.withAlpha (0.85f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * pos), 2.0f);
    }

    // where the Threshold sits (Audio mode only)
    if ((int) proc.apvts.getRawParameterValue ("mode")->load() == 2)
    {
        const float tdb = proc.apvts.getRawParameterValue ("threshold")->load();
        const float tp = juce::jlimit (0.0f, 1.0f, (tdb + 60.0f) / 60.0f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillRect (bar.getX() + bar.getWidth() * tp - 1.0f, bar.getY() - 1.0f, 2.0f, bar.getHeight() + 2.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.12f + 0.88f * flash));
    g.fillEllipse (led);
}

//==============================================================================
CurveEditor::CurveEditor (PumpKitProcessor& p) : proc (p)
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    startTimerHz (30);
}

juce::Rectangle<float> CurveEditor::plotArea() const
{
    auto area = getLocalBounds().toFloat().reduced (14.0f, 16.0f);
    area.removeFromBottom (10.0f);   // room for the legend underneath
    return area;
}

juce::Point<float> CurveEditor::toScreen (float x, float y) const
{
    const auto b = plotArea();
    return { b.getX() + x * b.getWidth(), b.getBottom() - y * b.getHeight() };
}

bool CurveEditor::hasDot (const std::vector<pump::Node>& n, size_t i)
{
    return i + 1 < n.size() && n[i + 1].x - n[i].x >= 0.03f;   // very short lines (the steep drops) get no dot
}

juce::Point<float> CurveEditor::dotPos (const std::vector<pump::Node>& n, size_t i) const
{
    const float mx = 0.5f * (n[i].x + n[i + 1].x);
    const float my = pump::segmentValue (n[i].y, n[i + 1].y, n[i].bend, 0.5f);
    return toScreen (mx, my);
}

CurveEditor::Hit CurveEditor::hitTest (juce::Point<float> p, const std::vector<pump::Node>& nodes) const
{
    Hit best;
    float bestDist = kHitRadius;
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const float d = toScreen (nodes[i].x, nodes[i].y).getDistanceFrom (p);
        if (d <= bestDist)
        {
            bestDist = d;
            best = { Kind::point, (int) i };
        }
    }
    if (best.kind != Kind::none)
        return best;

    bestDist = kDotHit;
    for (size_t i = 0; i + 1 < nodes.size(); ++i)
    {
        if (! hasDot (nodes, i))
            continue;
        const float d = dotPos (nodes, i).getDistanceFrom (p);
        if (d <= bestDist)
        {
            bestDist = d;
            best = { Kind::bend, (int) i };
        }
    }
    return best;
}

void CurveEditor::drawWave (juce::Graphics& g,
                            const std::array<std::atomic<float>, pump::kScopeBins>& bins,
                            juce::Rectangle<float> area, juce::Colour fill, juce::Colour line, float lineWidth)
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
    g.setColour (line);
    g.strokePath (top, juce::PathStrokeType (lineWidth));
}

void CurveEditor::paint (juce::Graphics& g)
{
    const auto accent = accentOf (proc);
    const auto bounds = getLocalBounds().toFloat();

    // dark panel with a soft glow in the genre colour
    g.setColour (kPanel);
    g.fillRoundedRectangle (bounds, 8.0f);
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addRoundedRectangle (bounds, 8.0f);
        g.reduceClipRegion (clip);
        juce::ColourGradient glow (accent.withAlpha (0.20f), bounds.getCentreX(), bounds.getCentreY() + bounds.getHeight() * 0.1f,
                                   accent.withAlpha (0.0f),  bounds.getX(), bounds.getY(), true);
        g.setGradientFill (glow);
        g.fillRect (bounds);
    }

    const auto b = plotArea();

    // grid
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    for (int i = 0; i <= 4; ++i)
        g.drawVerticalLine ((int) (b.getX() + b.getWidth() * (float) i / 4.0f), b.getY(), b.getBottom());
    for (int i = 0; i <= 2; ++i)
        g.drawHorizontalLine ((int) (b.getY() + b.getHeight() * (float) i / 2.0f), b.getX(), b.getRight());

    // waveforms: sound going in (dim), sound coming out (bright), what triggers the curve (genre colour, lighter)
    drawWave (g, proc.scopeIn,  b, juce::Colours::white.withAlpha (0.09f), juce::Colours::white.withAlpha (0.16f), 1.0f);
    drawWave (g, proc.scopeOut, b, juce::Colours::white.withAlpha (0.24f), juce::Colours::white.withAlpha (0.55f), 1.2f);
    drawWave (g, proc.scopeSc,  b, accent.brighter (0.7f).withAlpha (0.20f), accent.brighter (0.9f).withAlpha (0.95f), 1.6f);

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

    g.setColour (accent.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (accent);
    g.strokePath (line, juce::PathStrokeType (2.4f));

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

    // bend dots (one in the middle of every line)
    for (size_t i = 0; i + 1 < nodes.size(); ++i)
    {
        if (! hasDot (nodes, i))
            continue;

        const auto c = dotPos (nodes, i);
        const bool active = hovering.kind == Kind::bend && hovering.index == (int) i;
        const bool grabbed = dragging.kind == Kind::bend && dragging.index == (int) i;
        const float r = (active || grabbed) ? kDotRadius + 1.5f : kDotRadius;
        const bool bent = std::fabs (nodes[i].bend) > 0.001f;

        g.setColour (kBackground);
        g.fillEllipse (c.x - r - 1.5f, c.y - r - 1.5f, (r + 1.5f) * 2.0f, (r + 1.5f) * 2.0f);
        g.setColour (active || grabbed ? juce::Colours::white : accent.brighter (0.4f));
        g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.8f);
        if (bent || grabbed)
        {
            g.setColour (active || grabbed ? juce::Colours::white : accent.brighter (0.4f));
            g.fillEllipse (c.x - r + 2.5f, c.y - r + 2.5f, (r - 2.5f) * 2.0f, (r - 2.5f) * 2.0f);
        }
    }

    // points
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const auto c = toScreen (nodes[i].x, nodes[i].y);
        const bool active = (hovering.kind == Kind::point && hovering.index == (int) i)
                         || (dragging.kind == Kind::point && dragging.index == (int) i);
        const float r = active ? kPointRadius + 1.5f : kPointRadius;

        g.setColour (kBackground);
        g.fillEllipse (c.x - r - 2.0f, c.y - r - 2.0f, (r + 2.0f) * 2.0f, (r + 2.0f) * 2.0f);
        g.setColour (active ? juce::Colours::white : accent);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    }

    // value readout for the point or dot under the mouse
    const Hit shown = dragging.kind != Kind::none ? dragging : hovering;
    if (shown.kind != Kind::none && shown.index >= 0 && shown.index < (int) nodes.size())
    {
        juce::Point<float> c;
        juce::String text;
        if (shown.kind == Kind::point)
        {
            const auto& n = nodes[(size_t) shown.index];
            c = toScreen (n.x, n.y);
            text = "time " + juce::String (juce::roundToInt (n.x * 100.0f)) + "%   volume "
                 + juce::String (juce::roundToInt (n.y * 100.0f)) + "%";
        }
        else if (hasDot (nodes, (size_t) shown.index))
        {
            c = dotPos (nodes, (size_t) shown.index);
            const int bend = juce::roundToInt (nodes[(size_t) shown.index].bend * 100.0f);
            text = bend == 0 ? juce::String ("straight line")
                             : "bend " + juce::String (bend > 0 ? "+" : "") + juce::String (bend) + "%";
        }

        if (text.isNotEmpty())
        {
            const float w = 150.0f, h = 20.0f;
            float tx = c.x + 12.0f, ty = c.y - h - 8.0f;
            if (tx + w > (float) getWidth() - 4.0f) tx = c.x - w - 12.0f;
            if (ty < 4.0f) ty = c.y + 10.0f;

            g.setColour (kBackground.withAlpha (0.92f));
            g.fillRoundedRectangle (tx, ty, w, h, 4.0f);
            g.setColour (kText);
            g.setFont (12.0f);
            g.drawText (text, juce::Rectangle<float> (tx, ty, w, h), juce::Justification::centred, false);
        }
    }

    // legend and help
    g.setFont (11.5f);
    float lx = b.getX() + 4.0f;
    const float ly = (float) getHeight() - 15.0f;
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
    legend (accent.brighter (0.9f), "trigger / sidechain");

    g.setColour (kDim);
    g.drawText ("click = add point   drag = move   small dot = bend the line   right-click = delete / straighten",
                getLocalBounds().reduced (16, 0).removeFromBottom (16).toFloat(),
                juce::Justification::centredRight, false);
}

void CurveEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.position, proc.curve.getNodes());
    if (h.kind != hovering.kind || h.index != hovering.index)
    {
        hovering = h;
        setMouseCursor (h.kind == Kind::none ? juce::MouseCursor::CrosshairCursor
                                             : (h.kind == Kind::bend ? juce::MouseCursor::UpDownResizeCursor
                                                                     : juce::MouseCursor::PointingHandCursor));
        repaint();
    }
}

void CurveEditor::mouseExit (const juce::MouseEvent&)
{
    hovering = {};
    repaint();
}

void CurveEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto nodes = proc.curve.getNodes();
    const auto hit = hitTest (e.position, nodes);

    if (e.mods.isPopupMenu() || e.mods.isCtrlDown())
    {
        if (hit.kind == Kind::point)
            proc.curve.removeNode (hit.index);
        else if (hit.kind == Kind::bend)
            proc.curve.setBend (hit.index, 0.0f);

        dragging = {};
        hovering = {};
        repaint();
        return;
    }

    if (hit.kind != Kind::none)
    {
        dragging = hit;
    }
    else
    {
        const auto b = plotArea();
        const float x = juce::jlimit (0.0f, 1.0f, (e.position.x - b.getX()) / b.getWidth());
        float y = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - b.getY()) / b.getHeight());
        if (y < 0.02f) y = 0.0f;
        if (y > 0.98f) y = 1.0f;
        const int idx = proc.curve.addNode (x, y);
        dragging = idx >= 0 ? Hit { Kind::point, idx } : Hit {};
    }

    hovering = dragging;
    repaint();
}

void CurveEditor::dragTo (const juce::MouseEvent& e)
{
    if (dragging.kind == Kind::none)
        return;

    const auto b = plotArea();
    const float x = juce::jlimit (0.0f, 1.0f, (e.position.x - b.getX()) / b.getWidth());
    float y = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - b.getY()) / b.getHeight());

    if (dragging.kind == Kind::point)
    {
        // snap to fully ducked / fully open when close (hold Shift to turn this off)
        if (! e.mods.isShiftDown())
        {
            if (y < 0.02f) y = 0.0f;
            if (y > 0.98f) y = 1.0f;
        }
        proc.curve.moveNode (dragging.index, x, y);
    }
    else
    {
        // the dot sits on the line, so dragging it up or down sets how far the middle is pushed
        const auto nodes = proc.curve.getNodes();
        const size_t i = (size_t) dragging.index;
        if (i + 1 < nodes.size())
        {
            const float lin = 0.5f * (nodes[i].y + nodes[i + 1].y);
            float bend = y - lin;
            if (! e.mods.isShiftDown() && std::fabs (bend) < 0.02f)   // easy to get back to straight
                bend = 0.0f;
            proc.curve.setBend (dragging.index, bend);
        }
    }
    repaint();
}

void CurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    dragTo (e);
}

void CurveEditor::mouseUp (const juce::MouseEvent& e)
{
    dragging = {};
    mouseMove (e);
    repaint();
}

//==============================================================================
void PumpKitEditor::Knob::init (PumpKitEditor& owner, const juce::String& paramId, const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
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

namespace
{
    struct MenuEntry { int genre; int preset; };

    // The order the presets appear in the menu (also the order the < > buttons step through):
    // per group first the loose presets, then each sub-folder.
    const std::vector<MenuEntry>& menuOrder()
    {
        static const std::vector<MenuEntry> order = []
        {
            std::vector<MenuEntry> o;
            const auto& all = pump::genres();
            for (size_t g = 0; g < all.size(); ++g)
            {
                const auto& list = all[g].presets;
                std::vector<std::string> folders;
                for (const auto& pr : list)
                    if (! pr.folder.empty() && std::find (folders.begin(), folders.end(), pr.folder) == folders.end())
                        folders.push_back (pr.folder);

                for (size_t p = 0; p < list.size(); ++p)
                    if (list[p].folder.empty())
                        o.push_back ({ (int) g, (int) p });
                for (const auto& f : folders)
                    for (size_t p = 0; p < list.size(); ++p)
                        if (list[p].folder == f)
                            o.push_back ({ (int) g, (int) p });
            }
            return o;
        }();
        return order;
    }

    int menuPosition (int genre, int preset)
    {
        const auto& o = menuOrder();
        for (size_t i = 0; i < o.size(); ++i)
            if (o[i].genre == genre && o[i].preset == preset)
                return (int) i;
        return 0;
    }
}

//==============================================================================
PumpLookAndFeel::PumpLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId,            juce::Colour (0xff14141c));
    setColour (juce::PopupMenu::textColourId,                  kText);
    setColour (juce::PopupMenu::headerTextColourId,            kDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2a2a38));
    setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId,             kPanel);
    setColour (juce::ComboBox::textColourId,                   kText);
    setColour (juce::ComboBox::outlineColourId,                juce::Colours::white.withAlpha (0.12f));
    setColour (juce::ComboBox::arrowColourId,                  kDim);
    setColour (juce::TextButton::buttonColourId,               kPanel);
    setColour (juce::TextButton::textColourOffId,              kText);
    setColour (juce::Label::textColourId,                      kText);
}

void HelpPanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff101018));
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (accent);
    g.drawRoundedRectangle (b.reduced (1.0f), 10.0f, 2.0f);

    auto r = getLocalBounds().reduced (28, 22);
    g.setColour (accent);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("Choosing the sidechain source", r.removeFromTop (30), juce::Justification::centredLeft);
    r.removeFromTop (6);

    g.setColour (kText);
    g.setFont (juce::FontOptions (14.0f));
    const juce::String text =
        "FL Studio's own plugins (like Fruity Limiter) can list FL's mixer tracks. Plugins in the VST3 format cannot, "
        "so PumpKit has its own way to choose the source, inside the plugin. It takes 4 steps:\n\n"
        "1.  Put a PumpKit on the kick's mixer track. Set SEND (bottom right) to \"Send to A\". "
        "The kick is not changed by it, it only reports its sound to channel A.\n"
        "2.  Put another PumpKit on the track you want to duck (bass, pad...).\n"
        "3.  On that one: Mode = Audio, TRIGGER FROM = \"Link A\".\n"
        "4.  Play. The meter moves and the light flashes on every kick. Pick the same letter on both. "
        "You can use up to 8 channels (A to H), one sender per letter.\n\n"
        "Tip: if the pump feels a little late, put the kick on a lower-numbered mixer track than the bass, "
        "or lower FL's buffer size.\n\n"
        "Other ways: Mode Sync (follows the song tempo), Mode MIDI, TRIGGER FROM = This track, or FL's own routing "
        "(right-click the send knob, \"Sidechain to this track\", then the wrapper's Processing page) with TRIGGER FROM = Sidechain.\n\n"
        "Click anywhere to close this panel.";
    g.drawFittedText (text, r, juce::Justification::topLeft, 20, 1.0f);
}

//==============================================================================
PumpKitEditor::PumpKitEditor (PumpKitProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), curveEditor (p), sourceMeter (p)
{
    setLookAndFeel (&laf);
    setSize (840, 580);

    addAndMakeVisible (curveEditor);
    addAndMakeVisible (sourceMeter);

    auto setupLabel = [this] (juce::Label& l, const juce::String& text, juce::Justification j)
    {
        l.setText (text, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, kDim);
        l.setJustificationType (j);
        addAndMakeVisible (l);
    };
    setupLabel (modeLabel,   "Mode",   juce::Justification::centredRight);
    setupLabel (lengthLabel, "Speed",  juce::Justification::centredRight);
    setupLabel (genreLabel,  "Preset", juce::Justification::centredRight);
    setupLabel (sourceLabel, "TRIGGER FROM", juce::Justification::centred);

    if (auto* mode = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("mode")))
        modeBox.addItemList (mode->choices, 1);
    if (auto* len = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("length")))
        lengthBox.addItemList (len->choices, 1);
    if (auto* src = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("scSource")))
        sourceBox.addItemList (src->choices, 1);
    if (auto* sd = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("sendTo")))
        sendBox.addItemList (sd->choices, 1);
    if (auto* ru = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("rateMode")))
        rateUnitBox.addItemList (ru->choices, 1);

    presetButton.setClickingTogglesState (false);
    presetButton.onClick = [this] { showPresetMenu(); };
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    helpButton.onClick = [this]
    {
        helpPanel.accent = accentOf (proc);
        helpPanel.setVisible (! helpPanel.isVisible());
        helpPanel.toFront (false);
    };
    helpButton.setTooltip ("How to choose the sidechain track in FL Studio");

    rateHzSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    rateHzSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 22);
    rateHzSlider.setColour (juce::Slider::textBoxTextColourId, kText);
    rateHzSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    rateHzSlider.setColour (juce::Slider::thumbColourId, kText);
    rateHzSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff3a3a4a));

    addAndMakeVisible (modeBox);
    addAndMakeVisible (rateUnitBox);
    addAndMakeVisible (lengthBox);
    addAndMakeVisible (rateHzSlider);
    addAndMakeVisible (sourceBox);
    addAndMakeVisible (sendBox);
    addAndMakeVisible (presetButton);
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);
    addAndMakeVisible (helpButton);
    addChildComponent (helpPanel);

    modeAtt     = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mode", modeBox);
    lengthAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "length", lengthBox);
    sourceAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "scSource", sourceBox);
    sendAtt     = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "sendTo", sendBox);
    rateUnitAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "rateMode", rateUnitBox);
    rateHzAtt   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "rateHz", rateHzSlider);

    splitButton.setColour (juce::ToggleButton::textColourId, kText);
    addAndMakeVisible (splitButton);
    splitAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "bandSplit", splitButton);

    mixKnob.init (*this, "mix", "MIX");
    smoothKnob.init (*this, "smooth", "SMOOTH");
    freqKnob.init (*this, "splitFreq", "SPLIT FREQ");
    thresholdKnob.init (*this, "threshold", "THRESHOLD");
    retriggerKnob.init (*this, "retrigger", "RETRIGGER");

    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setColour (juce::Label::textColourId, kDim);
    statusLabel.setFont (juce::FontOptions (12.5f));
    addAndMakeVisible (statusLabel);

    showSelection (proc.genreIndex.load(), proc.presetIndex.load());
    applyTheme();
    updateRateControls();

    startTimerHz (10);
    timerCallback();
}

PumpKitEditor::~PumpKitEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void PumpKitEditor::showSelection (int genre, int preset)
{
    const auto& all = pump::genres();
    genre = juce::jlimit (0, (int) all.size() - 1, genre);
    const auto& list = all[(size_t) genre].presets;
    preset = juce::jlimit (0, (int) list.size() - 1, preset);

    const auto& def = list[(size_t) preset];
    juce::String text = all[(size_t) genre].name;
    if (! def.folder.empty())
        text << "  >  " << def.folder;
    text << "  >  " << def.name;
    presetButton.setButtonText (text);

    shownGenre = genre;
    shownPreset = preset;
    proc.genreIndex.store (genre);
    proc.presetIndex.store (preset);
}

void PumpKitEditor::loadPreset (int genre, int preset)
{
    showSelection (genre, preset);

    const auto& def = pump::genres()[(size_t) proc.genreIndex.load()].presets[(size_t) proc.presetIndex.load()];
    proc.curve.setNodes (def.nodes);

    // each preset was drawn for a certain note length. In Hz mode the speed is left alone
    // (so a preset never changes an automated or hand-set Hz value).
    if (proc.apvts.getRawParameterValue ("rateMode")->load() < 0.5f)
        lengthBox.setSelectedItemIndex (juce::jlimit (0, pump::kNumLengths - 1, def.length), juce::sendNotification);

    applyTheme();
    curveEditor.repaint();
}

void PumpKitEditor::stepPreset (int delta)
{
    const auto& o = menuOrder();
    const int n = (int) o.size();
    const int pos = (menuPosition (proc.genreIndex.load(), proc.presetIndex.load()) + delta + n) % n;
    loadPreset (o[(size_t) pos].genre, o[(size_t) pos].preset);
}

// Genres open as sub-menus when you hover over them; groups with folders open one level further.
void PumpKitEditor::showPresetMenu()
{
    const auto& all = pump::genres();
    const auto& order = menuOrder();
    const int cur = menuPosition (proc.genreIndex.load(), proc.presetIndex.load());
    const int curGenre = proc.genreIndex.load();

    auto itemFor = [&] (size_t g, size_t p, int id)
    {
        juce::PopupMenu::Item it (all[g].presets[p].name);
        it.itemID = id;
        it.colour = juce::Colour (all[g].colour).brighter (0.25f);
        it.isTicked = (id - 1 == cur);
        return it;
    };
    auto submenuItem = [] (const juce::String& name, juce::PopupMenu sub, juce::Colour col, bool ticked)
    {
        juce::PopupMenu::Item it (name);
        it.subMenu = std::make_unique<juce::PopupMenu> (std::move (sub));
        it.colour = col;
        it.isTicked = ticked;
        return it;
    };

    juce::PopupMenu menu;
    for (size_t g = 0; g < all.size(); ++g)
    {
        juce::PopupMenu genreMenu;
        std::vector<std::string> folders;
        for (const auto& pr : all[g].presets)
            if (! pr.folder.empty() && std::find (folders.begin(), folders.end(), pr.folder) == folders.end())
                folders.push_back (pr.folder);

        for (size_t k = 0; k < order.size(); ++k)
            if (order[k].genre == (int) g && all[g].presets[(size_t) order[k].preset].folder.empty())
                genreMenu.addItem (itemFor (g, (size_t) order[k].preset, (int) k + 1));

        if (! folders.empty() && genreMenu.getNumItems() > 0)
            genreMenu.addSeparator();

        for (const auto& f : folders)
        {
            juce::PopupMenu folderMenu;
            bool hasCurrent = false;
            for (size_t k = 0; k < order.size(); ++k)
                if (order[k].genre == (int) g && all[g].presets[(size_t) order[k].preset].folder == f)
                {
                    folderMenu.addItem (itemFor (g, (size_t) order[k].preset, (int) k + 1));
                    hasCurrent = hasCurrent || (int) k == cur;
                }
            genreMenu.addItem (submenuItem (f, std::move (folderMenu), juce::Colour (all[g].colour).brighter (0.25f), hasCurrent));
        }

        menu.addItem (submenuItem (all[g].name, std::move (genreMenu), juce::Colour (all[g].colour).brighter (0.1f), (int) g == curGenre));
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton).withMinimumWidth (presetButton.getWidth()),
                        [this] (int result)
                        {
                            if (result > 0 && result <= (int) menuOrder().size())
                                loadPreset (menuOrder()[(size_t) result - 1].genre, menuOrder()[(size_t) result - 1].preset);
                        });
}

void PumpKitEditor::updateRateControls()
{
    const bool hz = proc.apvts.getRawParameterValue ("rateMode")->load() > 0.5f;
    lengthBox.setVisible (! hz);
    rateHzSlider.setVisible (hz);
}

void PumpKitEditor::applyTheme()
{
    const auto accent = accentOf (proc);
    for (auto* k : { &mixKnob, &smoothKnob, &freqKnob, &thresholdKnob, &retriggerKnob })
        k->slider.setColour (juce::Slider::rotarySliderFillColourId, accent);

    rateHzSlider.setColour (juce::Slider::trackColourId, accent.withAlpha (0.55f));
    splitButton.setColour (juce::ToggleButton::tickColourId, accent);
    presetButton.setColour (juce::TextButton::textColourOffId, accent.brighter (0.3f));
    helpPanel.accent = accent;
    repaint();
}

void PumpKitEditor::paint (juce::Graphics& g)
{
    const auto accent = accentOf (proc);

    g.fillAll (kBackground);

    // soft glow in the genre colour in the top-left corner
    juce::ColourGradient glow (accent.withAlpha (0.16f), 0.0f, 0.0f,
                               accent.withAlpha (0.0f), (float) getWidth() * 0.7f, (float) getHeight() * 0.5f, true);
    g.setGradientFill (glow);
    g.fillRect (getLocalBounds());

    g.setColour (kText);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("PUMPKIT", 20, 12, 200, 32, juce::Justification::centredLeft);

    g.setColour (accent);
    g.fillRect (20, 46, 36, 3);
}

void PumpKitEditor::resized()
{
    auto r = getLocalBounds().reduced (20, 14);

    auto title = r.removeFromTop (38);
    statusLabel.setBounds (title.removeFromRight (560));

    r.removeFromTop (10);
    auto top = r.removeFromTop (28);
    helpButton.setBounds (top.removeFromRight (28));
    top.removeFromRight (6);

    modeLabel.setBounds (top.removeFromLeft (40));
    modeBox.setBounds (top.removeFromLeft (84));
    top.removeFromLeft (10);
    lengthLabel.setBounds (top.removeFromLeft (46));
    rateUnitBox.setBounds (top.removeFromLeft (70));
    top.removeFromLeft (6);
    auto speed = top.removeFromLeft (170);
    rateHzSlider.setBounds (speed);
    lengthBox.setBounds (speed.withWidth (110));
    top.removeFromLeft (10);
    genreLabel.setBounds (top.removeFromLeft (48));
    nextButton.setBounds (top.removeFromRight (26));
    top.removeFromRight (4);
    prevButton.setBounds (top.removeFromRight (26));
    top.removeFromRight (6);
    presetButton.setBounds (top);

    r.removeFromTop (10);
    auto knobRow = r.removeFromBottom (150);
    r.removeFromBottom (10);
    curveEditor.setBounds (r);

    const int colW = knobRow.getWidth() / 7;
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
    place (retriggerKnob, knobRow.removeFromLeft (colW));

    auto src = knobRow.reduced (6, 0);
    sourceLabel.setBounds (src.removeFromTop (18));
    src.removeFromTop (8);
    sourceBox.setBounds (src.removeFromTop (28));
    src.removeFromTop (8);
    sourceMeter.setBounds (src.removeFromTop (20));
    src.removeFromTop (8);
    sendBox.setBounds (src.removeFromTop (26));

    helpPanel.setBounds (getLocalBounds().reduced (50, 70));
}

void PumpKitEditor::timerCallback()
{
    const int mode = (int) proc.apvts.getRawParameterValue ("mode")->load();
    const bool audio = mode == 2;
    const bool own = (int) proc.apvts.getRawParameterValue ("scSource")->load() == 1;
    const bool split = proc.apvts.getRawParameterValue ("bandSplit")->load() > 0.5f;

    thresholdKnob.slider.setEnabled (audio);
    retriggerKnob.slider.setEnabled (audio);
    const bool sending = proc.apvts.getRawParameterValue ("sendTo")->load() > 0.5f;
    sourceBox.setEnabled (audio && ! sending);
    sendBox.setTooltip ("Send this track's sound to a link channel so another PumpKit can listen to it. This track is not changed.");
    freqKnob.slider.setEnabled (split);
    updateRateControls();

    // follow the preset button when a project loads while the window is open
    if (proc.genreIndex.load() != shownGenre || proc.presetIndex.load() != shownPreset)
    {
        showSelection (proc.genreIndex.load(), proc.presetIndex.load());
        applyTheme();
    }

    const bool signal = proc.uiSrcLevel.load() > 0.002f;
    const int srcChoice = (int) proc.apvts.getRawParameterValue ("scSource")->load();
    const bool hz = proc.apvts.getRawParameterValue ("rateMode")->load() > 0.5f;

    if (sending)
        statusLabel.setText ("Sending to " + sendBox.getText().fromFirstOccurrenceOf ("Send to ", false, false)
                             + ": this track is not changed", juce::dontSendNotification);
    else if (mode == 0)
        statusLabel.setText (hz ? "Sync: runs at the Hz speed while playing" : "Sync: follows the host tempo while playing",
                             juce::dontSendNotification);
    else if (mode == 1)
        statusLabel.setText ("MIDI: every note-on triggers the curve", juce::dontSendNotification);
    else if (signal)
        statusLabel.setText (own ? "Audio: listening to the sound on this track"
                                 : (srcChoice >= 2 ? "Audio: receiving " + sourceBox.getText() : juce::String ("Audio: sidechain signal detected")),
                             juce::dontSendNotification);
    else
        statusLabel.setText (own ? "Audio: no sound on this track yet"
                                 : (srcChoice >= 2 ? "Audio: nothing arriving on " + sourceBox.getText() + ". Is a PumpKit on the kick set to Send?"
                                                   : juce::String ("Audio: no sidechain signal yet. Click ? for setup help")),
                             juce::dontSendNotification);
}
