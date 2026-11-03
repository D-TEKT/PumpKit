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

pump::CurveTable& CurveEditor::curveRef() const
{
    return proc.curves[(size_t) juce::jlimit (0, pump::kMaxSlots - 1, proc.selectedSlot.load())];
}

int CurveEditor::slotType() const
{
    return (int) proc.apvts.getRawParameterValue ("m" + juce::String (proc.selectedSlot.load() + 1) + "Type")->load();
}

int CurveEditor::slotStyle() const
{
    return (int) proc.apvts.getRawParameterValue ("m" + juce::String (proc.selectedSlot.load() + 1) + "Style")->load();
}

float CurveEditor::slotDepth() const
{
    return proc.apvts.getRawParameterValue ("m" + juce::String (proc.selectedSlot.load() + 1) + "Depth")->load() * 0.01f;
}

bool CurveEditor::magnetOn() const
{
    return proc.apvts.getRawParameterValue ("magnet")->load() > 0.5f;
}

// all vertical grid lines: beats (or quarters of the cycle) are strong, the Grid setting adds finer ones
std::vector<CurveEditor::GridLine> CurveEditor::buildGrid() const
{
    const bool hz = proc.apvts.getRawParameterValue ("rateMode")->load() > 0.5f;
    const double L = hz ? 4.0 : pump::lengthQuarters ((int) proc.apvts.getRawParameterValue ("length")->load());
    const int divs[] = { 0, 2, 4, 8, 16, 32, 3, 6, 12, 24 };   // the last four are triplets
    const int n = divs[juce::jlimit (0, 9, (int) proc.apvts.getRawParameterValue ("gridDiv")->load())];

    std::vector<GridLine> g;
    auto put = [&g] (float x, bool major)
    {
        for (auto& l : g)
            if (std::fabs (l.x - x) < 1.0e-4f) { l.major = l.major || major; return; }
        g.push_back ({ x, major });
    };

    if (L >= 2.0 && std::fabs (L - std::round (L)) < 1.0e-6)
        for (int k = 0; k <= (int) std::round (L); ++k) put ((float) ((double) k / L), true);   // one strong line per beat
    else
        for (int k = 0; k <= 4; ++k) put ((float) k * 0.25f, true);                              // quarters of the cycle

    for (int k = 0; n > 0 && k <= n; ++k)
        put ((float) k / (float) n, false);

    std::sort (g.begin(), g.end(), [] (const GridLine& a, const GridLine& b) { return a.x < b.x; });
    return g;
}

float CurveEditor::snapX (float x01, float, const juce::ModifierKeys& mods)
{
    snapShownX = -1.0f;
    if (! magnetOn() || mods.isShiftDown())
        return x01;

    const float widthPx = plotArea().getWidth();
    float best = x01, bestDist = 7.0f;   // pull radius in pixels
    for (const auto& l : buildGrid())
    {
        const float d = std::fabs (l.x - x01) * widthPx;
        if (d <= bestDist) { bestDist = d; best = l.x; snapShownX = l.x; }
    }
    return best;
}

float CurveEditor::snapY (float y01, const juce::ModifierKeys& mods)
{
    snapShownY = -1.0f;
    if (! magnetOn() || mods.isShiftDown())
        return y01;

    const float heightPx = plotArea().getHeight();
    float best = y01, bestDist = 7.0f;
    for (int k = 0; k <= 4; ++k)
    {
        const float line = (float) k * 0.25f;
        const float d = std::fabs (line - y01) * heightPx;
        if (d <= bestDist) { bestDist = d; best = line; snapShownY = line; }
    }
    return best;
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

    // grid: strong lines on the beats, finer lines from the Grid setting, and quarter lines for the height
    {
        const int type = slotType();
        const auto lines = buildGrid();
        for (const auto& l : lines)
        {
            const float x = b.getX() + l.x * b.getWidth();
            g.setColour (juce::Colours::white.withAlpha (l.major ? 0.14f : 0.055f));
            g.drawVerticalLine ((int) x, b.getY(), b.getBottom());
        }
        for (int i = 0; i <= 4; ++i)
        {
            g.setColour (juce::Colours::white.withAlpha (i == 2 ? 0.10f : 0.05f));
            g.drawHorizontalLine ((int) (b.getBottom() - b.getHeight() * (float) i * 0.25f), b.getX(), b.getRight());
        }

        // beat numbers along the top
        const bool hz = proc.apvts.getRawParameterValue ("rateMode")->load() > 0.5f;
        const double L = hz ? 4.0 : pump::lengthQuarters ((int) proc.apvts.getRawParameterValue ("length")->load());
        g.setFont (10.5f);
        g.setColour (kDim);
        if (L >= 2.0 && std::fabs (L - std::round (L)) < 1.0e-6)
        {
            for (int k = 0; k < (int) std::round (L); ++k)
                g.drawText (juce::String (k + 1), (int) (b.getX() + (float) (k / L) * b.getWidth()) + 3, 1, 24, 13,
                            juce::Justification::centredLeft, false);
        }
        else
        {
            const char* q[] = { "0", "25%", "50%", "75%" };
            for (int k = 0; k < 4; ++k)
                g.drawText (q[k], (int) (b.getX() + (float) k * 0.25f * b.getWidth()) + 3, 1, 34, 13, juce::Justification::centredLeft, false);
        }

        // what the top and the bottom of the curve mean for this effect
        g.setColour (kDim.withAlpha (0.8f));
        g.drawText (pump::fxTopLabel (type, slotStyle()), (int) b.getRight() - 124, (int) b.getY() + 2, 120, 13, juce::Justification::centredRight, false);
        g.drawText (pump::fxBottomLabel (type, slotStyle()), (int) b.getRight() - 124, (int) b.getBottom() - 15, 120, 13, juce::Justification::centredRight, false);
        const char* mid = type == pump::fxPan ? "centre" : (type == pump::fxWidth ? "normal" : (type == pump::fxPitch ? "no change" : ""));
        if (*mid)
            g.drawText (mid, (int) b.getRight() - 124, (int) (b.getCentreY()) - 14, 120, 13, juce::Justification::centredRight, false);

        // the line a point is currently stuck to
        g.setColour (accent.withAlpha (0.55f));
        if (snapShownX >= 0.0f)
            g.drawVerticalLine ((int) (b.getX() + snapShownX * b.getWidth()), b.getY(), b.getBottom());
        if (snapShownY >= 0.0f)
            g.drawHorizontalLine ((int) (b.getBottom() - snapShownY * b.getHeight()), b.getX(), b.getRight());
    }

    // waveforms: sound going in (dim), sound coming out (bright), what triggers the curve (genre colour, lighter)
    drawWave (g, proc.scopeIn,  b, juce::Colours::white.withAlpha (0.09f), juce::Colours::white.withAlpha (0.16f), 1.0f);
    drawWave (g, proc.scopeOut, b, juce::Colours::white.withAlpha (0.24f), juce::Colours::white.withAlpha (0.55f), 1.2f);
    drawWave (g, proc.scopeSc,  b, accent.brighter (0.7f).withAlpha (0.20f), accent.brighter (0.9f).withAlpha (0.95f), 1.6f);

    // curve
    const auto nodes = curveRef().getNodes();

    juce::Path line, fill;
    const int steps = std::max (2, (int) b.getWidth());
    for (int s = 0; s <= steps; ++s)
    {
        const float x01 = (float) s / (float) steps;
        const float px = b.getX() + x01 * b.getWidth();
        const float py = b.getBottom() - curveRef().eval (x01) * b.getHeight();
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
        const float py = b.getBottom() - curveRef().eval (ph) * b.getHeight();
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
            text = "time " + juce::String (juce::roundToInt (n.x * 100.0f)) + "%   "
                 + juce::String (pump::fxValueText (slotType(), n.y, slotDepth(), slotStyle()));
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
            const float w = 215.0f, h = 20.0f;
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

}

void CurveEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.position, curveRef().getNodes());
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
    const auto nodes = curveRef().getNodes();
    const auto hit = hitTest (e.position, nodes);

    if (e.mods.isPopupMenu() || e.mods.isCtrlDown())
    {
        if (hit.kind == Kind::point)
            curveRef().removeNode (hit.index);
        else if (hit.kind == Kind::bend)
            curveRef().setBend (hit.index, 0.0f);

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
        float x = juce::jlimit (0.0f, 1.0f, (e.position.x - b.getX()) / b.getWidth());
        float y = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - b.getY()) / b.getHeight());
        x = snapX (x, e.position.x, e.mods);
        y = snapY (y, e.mods);
        if (y < 0.02f) y = 0.0f;
        if (y > 0.98f) y = 1.0f;
        const int idx = curveRef().addNode (x, y);
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
    float x = juce::jlimit (0.0f, 1.0f, (e.position.x - b.getX()) / b.getWidth());
    float y = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - b.getY()) / b.getHeight());

    if (dragging.kind == Kind::point)
    {
        x = snapX (x, e.position.x, e.mods);
        y = snapY (y, e.mods);
        // snap to fully ducked / fully open when close (hold Shift to turn this off)
        if (! e.mods.isShiftDown())
        {
            if (y < 0.02f) y = 0.0f;
            if (y > 0.98f) y = 1.0f;
        }
        curveRef().moveNode (dragging.index, x, y);
    }
    else
    {
        // the dot sits on the line, so dragging it up or down sets how far the middle is pushed
        const auto nodes = curveRef().getNodes();
        const size_t i = (size_t) dragging.index;
        if (i + 1 < nodes.size())
        {
            const float lin = 0.5f * (nodes[i].y + nodes[i + 1].y);
            float bend = y - lin;
            if (! e.mods.isShiftDown() && std::fabs (bend) < 0.02f)   // easy to get back to straight
                bend = 0.0f;
            curveRef().setBend (dragging.index, bend);
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
    snapShownX = snapShownY = -1.0f;
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

void PumpKitEditor::Knob::bind (PumpKitEditor& owner, const juce::String& paramId)
{
    attachment.reset();
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
    g.drawText ("Sidechain in FL Studio, and effects", r.removeFromTop (30), juce::Justification::centredLeft);
    r.removeFromTop (6);

    g.setColour (kText);
    g.setFont (juce::FontOptions (14.0f));
    const juce::String text =
        "FL Studio decides which mixer track feeds a plugin's sidechain, so this one step is done in FL (the same way as "
        "with other plugins such as Kickstart). It takes 20 seconds:\n\n"
        "1.  Put your kick on its own mixer track.\n"
        "2.  Put PumpKit on the track of the sound you want to duck (bass, pad...).\n"
        "3.  Click the kick's mixer track. Right-click the small send knob at the bottom of the bass track and choose "
        "\"Sidechain to this track\".\n"
        "4.  Open the PumpKit wrapper: click the gear icon at the top left of this window, then the Processing page. "
        "Under Sidechain, make sure the kick's track is selected.\n"
        "5.  Set Mode to Audio here. The meter at the bottom right moves and its light flashes on every kick.\n\n"
        "No routing wanted?  Use Mode: Sync (follows the song tempo), Mode: MIDI, or Audio with "
        "TRIGGER FROM = This track (listens to the sound on the track itself).\n\n"
        "Effects: every tab is one effect with its own curve. Use + to add more (up to six). Chain = each effect works on the "
        "result of the one before it (right-click a tab to change the order). Side by side = every effect works on the original "
        "sound and the results are mixed together.\n\n"
        "Click anywhere to close this panel.";
    g.drawFittedText (text, r, juce::Justification::topLeft, 20, 1.0f);
}

//==============================================================================
namespace
{
    const int kStartTypes[] = { pump::fxVolume, pump::fxFilter, pump::fxPan, pump::fxWidth, pump::fxDistortion, pump::fxPitch };
}

juce::Rectangle<float> StartScreen::tileRect (int index) const
{
    auto area = getLocalBounds().toFloat().reduced (40.0f, 0.0f);
    area.removeFromTop (110.0f);
    area.removeFromBottom (40.0f);
    const float gap = 16.0f;
    const float w = (area.getWidth() - gap * 2.0f) / 3.0f;
    const float h = (area.getHeight() - gap) / 2.0f;
    const int col = index % 3, row = index / 3;
    return { area.getX() + (float) col * (w + gap), area.getY() + (float) row * (h + gap), w, h };
}

int StartScreen::tileAt (juce::Point<float> p) const
{
    for (int i = 0; i < 6; ++i)
        if (tileRect (i).contains (p))
            return i;
    return -1;
}

void StartScreen::mouseMove (const juce::MouseEvent& e)
{
    const int t = tileAt (e.position);
    if (t != hover)
    {
        hover = t;
        setMouseCursor (t >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void StartScreen::mouseDown (const juce::MouseEvent& e)
{
    const int t = tileAt (e.position);
    if (t >= 0 && onPick)
        onPick (kStartTypes[t]);
}

void StartScreen::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
    juce::ColourGradient glow (accent.withAlpha (0.22f), (float) getWidth() * 0.5f, (float) getHeight() * 0.35f,
                               accent.withAlpha (0.0f), 0.0f, 0.0f, true);
    g.setGradientFill (glow);
    g.fillRect (getLocalBounds());

    g.setColour (kText);
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.drawText ("PUMPKIT", getLocalBounds().withHeight (70).withTrimmedTop (22), juce::Justification::centred);
    g.setColour (accent);
    g.fillRect ((float) getWidth() * 0.5f - 18.0f, 70.0f, 36.0f, 3.0f);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (15.0f));
    g.drawText ("What do you want to shape?  You can add more effects later with the + button.",
                getLocalBounds().withTrimmedTop (80).withHeight (30), juce::Justification::centred);

    for (int i = 0; i < 6; ++i)
    {
        const int type = kStartTypes[i];
        const auto r = tileRect (i);
        const bool over = hover == i;

        g.setColour (kPanel.brighter (over ? 0.12f : 0.0f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (over ? accent : juce::Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, over ? 2.0f : 1.0f);

        // a small preview of this effect's starting curve
        pump::CurveTable preview;
        preview.setNodes (pump::defaultNodes (type));
        auto pr = r.reduced (18.0f, 0.0f).withTrimmedTop (16.0f).withTrimmedBottom (r.getHeight() * 0.42f);
        juce::Path line, fill;
        for (int s = 0; s <= 80; ++s)
        {
            const float x01 = (float) s / 80.0f;
            const float px = pr.getX() + x01 * pr.getWidth();
            const float py = pr.getBottom() - preview.eval (x01) * pr.getHeight();
            if (s == 0) { line.startNewSubPath (px, py); fill.startNewSubPath (px, pr.getBottom()); fill.lineTo (px, py); }
            else        { line.lineTo (px, py); fill.lineTo (px, py); }
        }
        fill.lineTo (pr.getRight(), pr.getBottom());
        fill.closeSubPath();
        g.setColour (accent.withAlpha (over ? 0.22f : 0.12f));
        g.fillPath (fill);
        g.setColour (accent);
        g.strokePath (line, juce::PathStrokeType (2.2f));

        auto text = r.withTrimmedTop (r.getHeight() * 0.62f).reduced (14.0f, 0.0f);
        g.setColour (kText);
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText (pump::fxName (type), text.removeFromTop (26.0f), juce::Justification::centredLeft);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (pump::fxHint (type), text.toNearestInt(), juce::Justification::topLeft, 2);
    }
}

//==============================================================================
int TabStrip::slotType (int s) const
{
    return (int) proc.apvts.getRawParameterValue ("m" + juce::String (s + 1) + "Type")->load();
}

int TabStrip::slotAt (juce::Point<int> p) const
{
    int x = 0;
    for (int s = 0; s < pump::kMaxSlots; ++s)
    {
        if (slotType (s) == pump::fxOff)
            continue;
        if (juce::Rectangle<int> (x, 0, kTabW, getHeight()).contains (p))
            return s;
        x += kTabW + 4;
    }
    return -1;
}

bool TabStrip::overAdd (juce::Point<int> p) const
{
    int n = 0;
    for (int s = 0; s < pump::kMaxSlots; ++s)
        if (slotType (s) != pump::fxOff)
            ++n;
    return n < pump::kMaxSlots && juce::Rectangle<int> (n * (kTabW + 4), 0, kAddW, getHeight()).contains (p);
}

void TabStrip::mouseDown (const juce::MouseEvent& e)
{
    const int s = slotAt (e.getPosition());
    if (s >= 0)
    {
        if (onSelect) onSelect (s);
        if ((e.mods.isPopupMenu() || e.mods.isCtrlDown()) && onMenu)
            onMenu (s);
    }
    else if (overAdd (e.getPosition()) && onAdd)
        onAdd();
}

void TabStrip::paint (juce::Graphics& g)
{
    const auto& all = pump::genres();
    const auto accent = juce::Colour (all[(size_t) juce::jlimit (0, (int) all.size() - 1, proc.genreIndex.load())].colour);
    const int selected = proc.selectedSlot.load();

    int x = 0, n = 0;
    for (int s = 0; s < pump::kMaxSlots; ++s)
    {
        const int t = slotType (s);
        if (t == pump::fxOff)
            continue;

        const bool on = proc.apvts.getRawParameterValue ("m" + juce::String (s + 1) + "On")->load() > 0.5f;
        const auto r = juce::Rectangle<float> ((float) x, 2.0f, (float) kTabW, (float) getHeight() - 2.0f);
        const bool sel = s == selected;

        g.setColour (sel ? accent.withAlpha (0.22f) : kPanel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (sel ? accent : juce::Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, sel ? 1.8f : 1.0f);

        g.setColour (on ? (sel ? juce::Colours::white : kText) : kDim.withAlpha (0.6f));
        g.setFont (juce::FontOptions (13.5f, sel ? juce::Font::bold : juce::Font::plain));
        g.drawText (juce::String (s + 1) + "  " + pump::fxName (t) + (on ? "" : "  (off)"), r.reduced (8.0f, 0.0f),
                    juce::Justification::centredLeft, true);
        x += kTabW + 4;
        ++n;
    }

    if (n < pump::kMaxSlots)
    {
        const auto r = juce::Rectangle<float> ((float) x, 2.0f, (float) kAddW, (float) getHeight() - 2.0f);
        g.setColour (kPanel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (kText);
        g.setFont (juce::FontOptions (20.0f));
        g.drawText ("+", r, juce::Justification::centred);
    }
}

//==============================================================================
PumpKitEditor::PumpKitEditor (PumpKitProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), curveEditor (p), sourceMeter (p), tabs (p)
{
    setLookAndFeel (&laf);
    setSize (840, 664);

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
    setupLabel (sourceLabel, "FROM", juce::Justification::centred);
    setupLabel (styleLabel, "", juce::Justification::centred);
    setupLabel (effectHint, "", juce::Justification::centredLeft);
    effectHint.setFont (juce::FontOptions (13.0f));
    effectHint.setMinimumHorizontalScale (1.0f);

    if (auto* mode = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("mode")))
        modeBox.addItemList (mode->choices, 1);
    if (auto* len = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("length")))
        lengthBox.addItemList (len->choices, 1);
    if (auto* src = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("scSource")))
        sourceBox.addItemList (src->choices, 1);
    if (auto* cb = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("combine")))
        combineBox.addItemList (cb->choices, 1);
    if (auto* gd = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("gridDiv")))
        gridBox.addItemList (gd->choices, 1);
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
    addAndMakeVisible (styleBox);
    addAndMakeVisible (combineBox);
    addAndMakeVisible (gridBox);
    addAndMakeVisible (magnetButton);
    addAndMakeVisible (tabs);
    setupLabel (combineLabel, "Combine", juce::Justification::centredRight);
    setupLabel (gridLabel, "Grid", juce::Justification::centredRight);
    combineBox.setTooltip ("Chain: each effect works on the result of the one before. Side by side: every effect works on the original sound and the results are mixed.");
    gridBox.setTooltip ("Extra grid lines: how many parts the cycle is divided in. The (triplet) options divide it by 3, 6, 12 or 24.");
    magnetButton.setTooltip ("Points stick to grid lines when you drag them close (hold Shift for free movement)");
    magnetButton.setColour (juce::ToggleButton::textColourId, kText);
    addAndMakeVisible (presetButton);
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);
    addAndMakeVisible (helpButton);
    addChildComponent (helpPanel);

    modeAtt     = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mode", modeBox);
    lengthAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "length", lengthBox);
    sourceAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "scSource", sourceBox);
    combineAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "combine", combineBox);
    gridAtt     = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "gridDiv", gridBox);
    magnetAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "magnet", magnetButton);
    rateUnitAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "rateMode", rateUnitBox);
    rateHzAtt   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "rateHz", rateHzSlider);

    splitButton.setColour (juce::ToggleButton::textColourId, kText);
    addAndMakeVisible (splitButton);
    splitAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "bandSplit", splitButton);

    mixKnob.init (*this, "mix", "MIX");
    depthKnob.init (*this, "m1Depth", "DEPTH");
    smoothKnob.init (*this, "smooth", "SMOOTH");
    freqKnob.init (*this, "splitFreq", "SPLIT FREQ");
    thresholdKnob.init (*this, "threshold", "THRESHOLD");
    retriggerKnob.init (*this, "retrigger", "RETRIGGER");
    paramKnob.init (*this, "m1Param", "TONE");

    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setColour (juce::Label::textColourId, kDim);
    statusLabel.setFont (juce::FontOptions (12.5f));
    addAndMakeVisible (statusLabel);

    tabs.onSelect = [this] (int s) { selectSlot (s); };
    tabs.onAdd    = [this] { showAddMenu(); };
    tabs.onMenu   = [this] (int s) { showSlotMenu (s); };

    startScreen.onPick = [this] (int type)
    {
        proc.startWith (type);
        startScreen.setVisible (false);
        selectSlot (0);
        curveEditor.repaint();
    };
    addChildComponent (startScreen);
    startScreen.setVisible (! proc.started.load());

    showSelection (proc.genreIndex.load(), proc.presetIndex.load());
    selectSlot (juce::jlimit (0, pump::kMaxSlots - 1, proc.selectedSlot.load()));
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
    proc.curves[(size_t) juce::jlimit (0, pump::kMaxSlots - 1, proc.selectedSlot.load())].setNodes (def.nodes);

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

void PumpKitEditor::selectSlot (int slot)
{
    slot = juce::jlimit (0, pump::kMaxSlots - 1, slot);
    proc.selectedSlot.store (slot);
    if (slot != shownSlot)
    {
        shownSlot = slot;
        depthKnob.attachment.reset();
        depthKnob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            proc.apvts, "m" + juce::String (slot + 1) + "Depth", depthKnob.slider);
    }
    paramKnob.bind (*this, "m" + juce::String (slot + 1) + "Param");
    refreshEffectPanel (true);
    curveEditor.repaint();
    tabs.repaint();
}

void PumpKitEditor::refreshEffectPanel (bool force)
{
    const int slot = juce::jlimit (0, pump::kMaxSlots - 1, shownSlot < 0 ? 0 : shownSlot);
    const juce::String n (slot + 1);
    const int type = (int) proc.apvts.getRawParameterValue ("m" + n + "Type")->load();
    const int key = slot * 100 + type;
    if (! force && key == shownKey)
        return;
    shownKey = key;

    const int styles = pump::fxStyleCount (type);
    styleBox.clear (juce::dontSendNotification);
    for (int i = 0; i < styles; ++i)
        styleBox.addItem (pump::fxStyleName (type, i), i + 1);
    // (not a ComboBoxAttachment: that one scales by the number of items, and the options differ per effect)
    styleBox.onChange = nullptr;
    if (styles > 0)
    {
        const int cur = juce::jlimit (0, styles - 1, (int) proc.apvts.getRawParameterValue ("m" + n + "Style")->load());
        styleBox.setSelectedId (cur + 1, juce::dontSendNotification);
        styleBox.onChange = [this, n]
        {
            if (auto* prm = proc.apvts.getParameter ("m" + n + "Style"))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (styleBox.getSelectedId() - 1)));
                prm->endChangeGesture();
            }
        };
    }
    styleLabel.setText (pump::fxStyleTitle (type), juce::dontSendNotification);

    const juce::String paramName = pump::fxParamName (type);
    paramKnob.label.setText (paramName, juce::dontSendNotification);
    paramKnob.bind (*this, "m" + n + "Param");
    paramKnob.slider.setEnabled (true);
    paramKnob.slider.repaint();

    const bool isVolume = type == pump::fxVolume;
    styleBox.setVisible (styles > 0);
    styleLabel.setVisible (styles > 0);
    paramKnob.slider.setVisible (paramName.isNotEmpty());
    paramKnob.label.setVisible (paramName.isNotEmpty());
    splitButton.setVisible (isVolume);
    freqKnob.slider.setVisible (isVolume);
    freqKnob.label.setVisible (isVolume);

    const bool noOptions = ! isVolume && styles == 0 && paramName.isEmpty();
    effectHint.setVisible (noOptions);
    effectHint.setText (juce::String (pump::fxHint (type)) + ". DEPTH sets how strong the effect is.", juce::dontSendNotification);
    resized();
    repaint();
}

void PumpKitEditor::showAddMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Add an effect");
    for (int t = pump::fxVolume; t < pump::kNumFxTypes; ++t)
        m.addItem (t, juce::String (pump::fxName (t)) + "   -   " + pump::fxHint (t));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (tabs), [this] (int r)
    {
        if (r > 0)
        {
            proc.addModule (r);
            selectSlot (proc.selectedSlot.load());
        }
    });
}

void PumpKitEditor::showSlotMenu (int slot)
{
    const bool on = proc.apvts.getRawParameterValue ("m" + juce::String (slot + 1) + "On")->load() > 0.5f;
    const int active = proc.activeModules();
    juce::PopupMenu m;
    m.addItem (1, on ? "Bypass this effect" : "Switch this effect on");
    m.addItem (2, "Move left (earlier in the chain)", slot > 0);
    m.addItem (3, "Move right (later in the chain)", slot + 1 < active);
    m.addSeparator();
    m.addItem (4, "Remove this effect", active > 1);
    m.showMenuAsync (juce::PopupMenu::Options(), [this, slot, on] (int r)
    {
        if (r == 1)
        {
            if (auto* prm = proc.apvts.getParameter ("m" + juce::String (slot + 1) + "On"))
                prm->setValueNotifyingHost (on ? 0.0f : 1.0f);
        }
        else if (r == 2) proc.moveModule (slot, -1);
        else if (r == 3) proc.moveModule (slot, 1);
        else if (r == 4) proc.removeModule (slot);
        selectSlot (proc.selectedSlot.load());
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
    for (auto* k : { &mixKnob, &depthKnob, &smoothKnob, &freqKnob, &thresholdKnob, &retriggerKnob, &paramKnob })
        k->slider.setColour (juce::Slider::rotarySliderFillColourId, accent);

    rateHzSlider.setColour (juce::Slider::trackColourId, accent.withAlpha (0.55f));
    splitButton.setColour (juce::ToggleButton::tickColourId, accent);
    presetButton.setColour (juce::TextButton::textColourOffId, accent.brighter (0.3f));
    helpPanel.accent = accent;
    startScreen.accent = accent;
    magnetButton.setColour (juce::ToggleButton::tickColourId, accent);
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

    auto panel = [&] (juce::Rectangle<int> r, const juce::String& title, bool active)
    {
        g.setColour (kPanel.withAlpha (0.85f));
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
        if (title.isNotEmpty())
        {
            g.setColour (active ? accent : kDim);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (title, r.getX() + 12, r.getY() + 6, r.getWidth() - 24, 14, juce::Justification::centredLeft);
        }
    };

    const int slot = juce::jlimit (0, pump::kMaxSlots - 1, proc.selectedSlot.load());
    const int type = (int) proc.apvts.getRawParameterValue ("m" + juce::String (slot + 1) + "Type")->load();
    const bool audio = (int) proc.apvts.getRawParameterValue ("mode")->load() == 2;

    panel (topPanel, {}, true);
    panel (effectPanel,  "EFFECT " + juce::String (slot + 1) + ":  " + juce::String (pump::fxName (type)).toUpperCase(), true);
    panel (outputPanel,  "WHOLE PLUGIN", true);
    panel (triggerPanel, audio ? "AUDIO TRIGGER" : "AUDIO TRIGGER  (only used in Audio mode)", audio);

    // short reminder of how to edit the curve, right under it
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText ("click = add point    drag = move    dot = bend the line    right-click = delete    Shift = no magnet",
                toolbarRow.withTrimmedLeft (230), juce::Justification::centredRight, false);
}

void PumpKitEditor::resized()
{
    auto r = getLocalBounds().reduced (20, 14);

    auto title = r.removeFromTop (38);
    statusLabel.setBounds (title.removeFromRight (560));

    r.removeFromTop (8);
    topPanel = r.removeFromTop (42);
    {
        auto top = topPanel.reduced (10, 7);
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
    }

    r.removeFromTop (8);
    auto tabRow = r.removeFromTop (30);
    combineBox.setBounds (tabRow.removeFromRight (112));
    combineLabel.setBounds (tabRow.removeFromRight (62));
    tabs.setBounds (tabRow);
    r.removeFromTop (6);

    auto bottom = r.removeFromBottom (164);
    r.removeFromBottom (8);
    toolbarRow = r.removeFromBottom (26);
    r.removeFromBottom (4);
    curveEditor.setBounds (r);

    {
        auto t = toolbarRow;
        gridLabel.setBounds (t.removeFromLeft (36));
        gridBox.setBounds (t.removeFromLeft (118));
        t.removeFromLeft (10);
        magnetButton.setBounds (t.removeFromLeft (80));
    }

    effectPanel = bottom.removeFromLeft (310);
    bottom.removeFromLeft (5);
    outputPanel = bottom.removeFromLeft (180);
    bottom.removeFromLeft (5);
    triggerPanel = bottom;

    auto place = [] (Knob& k, juce::Rectangle<int> col)
    {
        k.label.setBounds (col.removeFromTop (16));
        k.slider.setBounds (col);
    };
    auto inner = [] (juce::Rectangle<int> p) { auto i = p.reduced (8, 4); i.removeFromTop (20); return i; };

    {   // the selected effect: Depth, then its options
        auto in = inner (effectPanel);
        const int w = in.getWidth() / 3;
        auto c1 = in.removeFromLeft (w), c2 = in.removeFromLeft (w), c3 = in;
        place (depthKnob, c1);

        splitButton.setBounds (c2.withTrimmedTop (44).withHeight (28).reduced (4, 0));
        styleLabel.setBounds (c2.withTrimmedTop (28).withHeight (16));
        styleBox.setBounds (c2.withTrimmedTop (46).withHeight (28).reduced (4, 0));
        place (freqKnob, c3);
        place (paramKnob, c3);
        effectHint.setBounds (juce::Rectangle<int> (c2.getX() + 6, c2.getY(), c2.getWidth() + c3.getWidth() - 12, c2.getHeight()));
    }
    {   // the whole plugin
        auto in = inner (outputPanel);
        const int w = in.getWidth() / 2;
        place (mixKnob, in.removeFromLeft (w));
        place (smoothKnob, in);
    }
    {   // audio trigger
        auto in = inner (triggerPanel);
        place (thresholdKnob, in.removeFromLeft (74));
        place (retriggerKnob, in.removeFromLeft (74));
        in.removeFromLeft (6);
        sourceLabel.setBounds (in.removeFromTop (16));
        in.removeFromTop (6);
        sourceBox.setBounds (in.removeFromTop (28));
        in.removeFromTop (14);
        sourceMeter.setBounds (in.removeFromTop (22));
    }

    helpPanel.setBounds (getLocalBounds().reduced (50, 70));
    startScreen.setBounds (getLocalBounds());
}

void PumpKitEditor::timerCallback()
{
    const int mode = (int) proc.apvts.getRawParameterValue ("mode")->load();
    const bool audio = mode == 2;
    const bool own = (int) proc.apvts.getRawParameterValue ("scSource")->load() == 1;
    const bool split = proc.apvts.getRawParameterValue ("bandSplit")->load() > 0.5f;

    thresholdKnob.slider.setEnabled (audio);
    retriggerKnob.slider.setEnabled (audio);
    sourceBox.setEnabled (audio);
    freqKnob.slider.setEnabled (split);

    // follow the selected module (it can change when a project loads, or from the tab strip)
    if (proc.selectedSlot.load() != shownSlot)
        selectSlot (proc.selectedSlot.load());
    tabs.repaint();
    if (startScreen.isVisible() == proc.started.load())
        startScreen.setVisible (! proc.started.load());
    refreshEffectPanel (false);
    if (styleBox.isVisible())
    {
        const int want = (int) proc.apvts.getRawParameterValue ("m" + juce::String (shownSlot + 1) + "Style")->load() + 1;
        if (styleBox.getSelectedId() != want && want >= 1 && want <= styleBox.getNumItems())
            styleBox.setSelectedId (want, juce::dontSendNotification);
    }
    repaint (triggerPanel);
    updateRateControls();

    // follow the preset button when a project loads while the window is open
    if (proc.genreIndex.load() != shownGenre || proc.presetIndex.load() != shownPreset)
    {
        showSelection (proc.genreIndex.load(), proc.presetIndex.load());
        applyTheme();
    }

    const bool signal = proc.uiSrcLevel.load() > 0.002f;
    const bool hz = proc.apvts.getRawParameterValue ("rateMode")->load() > 0.5f;

    if (mode == 0)
        statusLabel.setText (hz ? "Sync: runs at the Hz speed while playing" : "Sync: follows the host tempo while playing",
                             juce::dontSendNotification);
    else if (mode == 1)
        statusLabel.setText ("MIDI: every note-on triggers the curve", juce::dontSendNotification);
    else if (signal)
        statusLabel.setText (own ? "Audio: listening to the sound on this track"
                                 : "Audio: sidechain signal detected", juce::dontSendNotification);
    else
        statusLabel.setText (own ? "Audio: no sound on this track yet"
                                 : "Audio: no sidechain signal yet. Click ? for the 20-second FL setup",
                             juce::dontSendNotification);
}
