// PumpEngine.h
// All the "brains" of PumpKit that don't need JUCE: curve points and bends,
// timing, trigger detection and gain smoothing. Kept separate so it can be unit-tested.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace pump
{
constexpr int   kNumPresets = 16;     // the 16 basic shapes (used for the "Utility" group)
constexpr int   kNumLengths = 12;
constexpr int   kTableSize  = 256;    // the curve is baked into this many samples for the audio thread
constexpr int   kMaxNodes   = 32;     // most points a curve can have
constexpr float kMinGap     = 0.004f; // closest two points can get (as a fraction of the cycle)
constexpr int   kScopeBins  = 256;    // resolution of the waveform display

//==============================================================================
// Length (duration) choices, in quarter notes
inline const char* lengthName (int i)
{
    static const char* names[kNumLengths] = {
        "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32",
        "1/4 triplet", "1/8 triplet", "1/16 triplet", "1/4 dotted", "1/8 dotted"
    };
    return names[std::clamp (i, 0, kNumLengths - 1)];
}

inline double lengthQuarters (int i)
{
    static const double q[kNumLengths] = {
        8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
        2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.5, 0.75
    };
    return q[std::clamp (i, 0, kNumLengths - 1)];
}

// Position inside the cycle (0..1) for a host position in quarter notes
inline double syncPhase (double ppqQuarters, double lengthInQuarters)
{
    const double p = ppqQuarters / lengthInQuarters;
    return p - std::floor (p);
}

//==============================================================================
// 16 basic mathematical shapes. x = time through the cycle (0..1),
// result = gain (0 = fully ducked, 1 = no ducking).
inline const char* presetName (int i)
{
    static const char* names[kNumPresets] = {
        "Linear Ramp", "Slow Curve", "Fast Curve", "Very Fast",
        "Long Hit",    "S-Curve",    "Shallow",    "Short Hit",
        "Very Slow",   "Gate Half",  "Staircase",  "Double Pump",
        "Triple Pump", "Bounce",     "Late Rise",  "Exp Release"
    };
    return names[std::clamp (i, 0, kNumPresets - 1)];
}

inline float presetValue (int preset, float x)
{
    x = std::clamp (x, 0.0f, 1.0f);
    float y = 1.0f;

    switch (preset)
    {
        case 0:  y = x; break;
        case 1:  y = x * x; break;
        case 2:  y = 1.0f - std::pow (1.0f - x, 2.0f); break;
        case 3:  y = 1.0f - std::pow (1.0f - x, 4.0f); break;
        case 4:  y = std::pow (x, 3.0f); break;
        case 5:  y = x * x * (3.0f - 2.0f * x); break;
        case 6:  y = 0.4f + 0.6f * (1.0f - std::pow (1.0f - x, 2.0f)); break;
        case 7:  y = std::min (1.0f, x / 0.3f); break;
        case 8:  y = std::pow (x, 5.0f); break;
        case 9:  y = x < 0.5f ? 0.0f : 1.0f; break;
        case 10: y = std::floor (x * 4.0f) / 3.0f; break;
        case 11: { const float u = x >= 1.0f ? 1.0f : x * 2.0f - std::floor (x * 2.0f); y = std::pow (u, 1.5f); break; }
        case 12: { const float u = x >= 1.0f ? 1.0f : x * 3.0f - std::floor (x * 3.0f); y = std::pow (u, 1.5f); break; }
        case 13:
        {
            const float base = 1.0f - std::pow (1.0f - std::min (1.0f, x * 1.5f), 2.0f);
            const float d = (x - 0.6f) / 0.07f;
            y = base * (1.0f - 0.4f * std::exp (-d * d));
            break;
        }
        case 14: y = x < 0.3f ? 0.0f : std::pow ((x - 0.3f) / 0.7f, 1.5f); break;
        case 15: y = (1.0f - std::exp (-5.0f * x)) / (1.0f - std::exp (-5.0f)); break;
        default: break;
    }
    return std::clamp (y, 0.0f, 1.0f);
}

//==============================================================================
// A curve point. Between this point and the next one the line is straight, plus a
// "bend": how far the middle dot of the line was dragged up (+) or down (-). 0 = straight.
// See segmentValue() for how that becomes a smooth, never-clipping curve.
struct Node
{
    float x = 0.0f;      // time in the cycle, 0..1
    float y = 0.0f;      // volume, 0 = silent, 1 = full
    float bend = 0.0f;   // bend of the segment that starts at this point (-1..1)
};

// Value of one line (from volume ya to yb) at position u (0..1) along it.
// "bend" is how far you dragged the middle dot, in volume units. The line never goes
// above or below its two end points and never clips flat: the further you drag, the
// more gently the shape follows (the effect fades off smoothly instead of hitting a wall).
inline float segmentValue (float ya, float yb, float bend, float u)
{
    const float d = yb - ya;
    const float ad = std::fabs (d);

    // flat (or almost flat) lines can only arch up or down a little
    const float archOffset = 0.4f * std::tanh (bend / 0.4f);
    float y = ya + d * u + archOffset * 4.0f * u * (1.0f - u);

    if (ad >= 0.02f)
    {
        // sloped lines become a power curve: where the middle dot sits between the two ends
        const float t = 0.5f + 0.4f * std::tanh (bend / (0.4f * d));
        const float p = std::log (t) / std::log (0.5f);
        const float curved = ya + d * std::pow (u, p);

        const float w = std::clamp ((ad - 0.02f) / 0.10f, 0.0f, 1.0f);   // fade between the two kinds
        y += (curved - y) * w;
    }
    return std::clamp (y, 0.0f, 1.0f);
}

// The bend value that puts the middle of a line at `ymid` (used when turning sampled shapes into points)
inline float bendForMid (float ya, float yb, float ymid)
{
    const float d = yb - ya;
    const float off = ymid - 0.5f * (ya + yb);

    if (std::fabs (d) >= 0.12f)
    {
        const float te = std::clamp (off / d, -0.39f, 0.39f);
        return std::clamp (d * 0.4f * std::atanh (te / 0.4f), -1.0f, 1.0f);
    }
    const float oe = std::clamp (off, -0.39f, 0.39f);
    return std::clamp (0.4f * std::atanh (oe / 0.4f), -1.0f, 1.0f);
}

//------------------------------------------------------------------------------
// Turning a sampled shape into a few points + bends (used for the basic shapes
// and for loading projects saved by older versions).
struct FitSeg { int a = 0, b = 0; float bend = 0.0f; };

inline void fitRange (const std::vector<float>& ys, int a, int b, float tol, std::vector<FitSeg>& out)
{
    const float ya = ys[(size_t) a], yb = ys[(size_t) b];

    float bend = 0.0f;
    if (b - a >= 4)
    {
        const float pos = 0.5f * (float) (a + b);
        const int i0 = (int) std::floor (pos);
        const float fr = pos - (float) i0;
        const float ymid = ys[(size_t) i0] + (ys[(size_t) std::min (i0 + 1, b)] - ys[(size_t) i0]) * fr;
        bend = bendForMid (ya, yb, ymid);
    }

    float worst = 0.0f;
    int wi = -1;
    for (int i = a + 1; i < b; ++i)
    {
        const float u = (float) (i - a) / (float) (b - a);
        const float e = std::fabs (ys[(size_t) i] - segmentValue (ya, yb, bend, u));
        if (e > worst) { worst = e; wi = i; }
    }

    if (wi < 0 || worst <= tol || b - a <= 1)
    {
        out.push_back ({ a, b, bend });
        return;
    }

    fitRange (ys, a, wi, tol, out);
    fitRange (ys, wi, b, tol, out);
}

inline std::vector<Node> simplify (const std::vector<float>& ys, float tol, int maxNodes)
{
    const int n = (int) ys.size();
    std::vector<Node> out;
    if (n < 2)
        return out;

    float t = tol;
    for (int attempt = 0; attempt < 12; ++attempt)
    {
        std::vector<FitSeg> segs;
        fitRange (ys, 0, n - 1, t, segs);

        out.clear();
        for (const auto& s : segs)
            out.push_back ({ (float) s.a / (float) (n - 1), std::clamp (ys[(size_t) s.a], 0.0f, 1.0f), s.bend });
        out.push_back ({ 1.0f, std::clamp (ys[(size_t) n - 1], 0.0f, 1.0f), 0.0f });

        if ((int) out.size() <= maxNodes)
            break;
        t *= 1.5f;
    }
    return out;
}

//==============================================================================
// The editable curve: a list of points with bends (edited from the UI thread) that is
// baked into a 256-sample table which the audio thread reads without any locking.
class CurveTable
{
public:
    CurveTable()
    {
        for (auto& t : table)
            t.store (0.0f, std::memory_order_relaxed);
        setNodes ({ { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
    }

    // ---- audio thread: gain at position x (0..1)
    float eval (float x) const
    {
        x = std::clamp (x, 0.0f, 1.0f);
        const float f = x * (float) (kTableSize - 1);
        const int i = (int) f;
        if (i >= kTableSize - 1)
            return table[(size_t) kTableSize - 1].load (std::memory_order_relaxed);
        const float frac = f - (float) i;
        const float a = table[(size_t) i].load (std::memory_order_relaxed);
        const float b = table[(size_t) i + 1].load (std::memory_order_relaxed);
        return a + (b - a) * frac;
    }

    // ---- UI thread
    std::vector<Node> getNodes() const
    {
        std::lock_guard<std::mutex> g (lock);
        return nodes;
    }

    void setNodes (std::vector<Node> v)
    {
        std::lock_guard<std::mutex> g (lock);
        nodes = sanitize (std::move (v));
        bake();
    }

    // returns the index of the new point, or -1 if there is no room
    int addNode (float x, float y)
    {
        std::lock_guard<std::mutex> g (lock);
        if ((int) nodes.size() >= kMaxNodes)
            return -1;

        x = std::clamp (x, 0.0f, 1.0f);
        y = std::clamp (y, 0.0f, 1.0f);

        size_t i = 1;
        while (i < nodes.size() - 1 && nodes[i].x <= x)
            ++i;

        const float lo = nodes[i - 1].x + kMinGap;
        const float hi = nodes[i].x - kMinGap;
        if (lo > hi)
            return -1;

        nodes.insert (nodes.begin() + (std::ptrdiff_t) i, Node { std::clamp (x, lo, hi), y, 0.0f });
        bake();
        return (int) i;
    }

    void moveNode (int i, float x, float y)
    {
        std::lock_guard<std::mutex> g (lock);
        if (i < 0 || i >= (int) nodes.size())
            return;

        auto& n = nodes[(size_t) i];
        n.y = std::clamp (y, 0.0f, 1.0f);

        if (i > 0 && i < (int) nodes.size() - 1)
        {
            const float lo = nodes[(size_t) i - 1].x + kMinGap;
            const float hi = nodes[(size_t) i + 1].x - kMinGap;
            n.x = std::clamp (x, lo, std::max (lo, hi));
        }
        bake();
    }

    // bend of the segment between point i and point i+1
    void setBend (int i, float bend)
    {
        std::lock_guard<std::mutex> g (lock);
        if (i < 0 || i + 1 >= (int) nodes.size())
            return;
        nodes[(size_t) i].bend = std::clamp (bend, -1.0f, 1.0f);
        bake();
    }

    // the first and last point can't be removed
    bool removeNode (int i)
    {
        std::lock_guard<std::mutex> g (lock);
        if (i <= 0 || i >= (int) nodes.size() - 1)
            return false;
        nodes.erase (nodes.begin() + i);
        bake();
        return true;
    }

    // ---- saving / loading
    std::string toString() const
    {
        std::lock_guard<std::mutex> g (lock);
        std::string s = "pk3";
        char buf[96];
        for (const auto& n : nodes)
        {
            std::snprintf (buf, sizeof (buf), ";%.5f,%.5f,%.5f", (double) n.x, (double) n.y, (double) n.bend);
            s += buf;
        }
        return s;
    }

    bool fromString (const std::string& s)
    {
        const bool v3 = s.rfind ("pk3;", 0) == 0;
        const bool v2 = s.rfind ("pk2;", 0) == 0;   // version 1.1: points without bends
        if (v3 || v2)
        {
            const char* p = s.c_str() + 3;
            if (v2)
                for (++p; *p != '\0' && *p != ';'; ++p) {}   // skip the old "smooth lines" flag

            std::vector<Node> v;
            while (*p == ';')
            {
                ++p;
                char* end = nullptr;
                const float x = std::strtof (p, &end);
                if (end == p || *end != ',') return false;
                p = end + 1;
                const float y = std::strtof (p, &end);
                if (end == p) return false;
                p = end;
                float b = 0.0f;
                if (v3)
                {
                    if (*p != ',') return false;
                    ++p;
                    b = std::strtof (p, &end);
                    if (end == p) return false;
                    p = end;
                }
                v.push_back ({ x, y, b });
            }
            if (v.size() < 2)
                return false;

            setNodes (std::move (v));
            return true;
        }

        // first version: 64 comma-separated heights
        std::vector<float> ys;
        const char* p = s.c_str();
        for (int i = 0; i < 64; ++i)
        {
            char* end = nullptr;
            const float v = std::strtof (p, &end);
            if (end == p) return false;
            ys.push_back (v);
            p = end;
            if (*p == ',') ++p;
        }
        setNodes (simplify (ys, 0.012f, 24));
        return true;
    }

private:
    static std::vector<Node> sanitize (std::vector<Node> v)
    {
        if (v.size() < 2)
            v = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };

        for (auto& n : v)
        {
            n.x = std::clamp (n.x, 0.0f, 1.0f);
            n.y = std::clamp (n.y, 0.0f, 1.0f);
            n.bend = std::clamp (n.bend, -1.0f, 1.0f);
        }

        std::stable_sort (v.begin(), v.end(), [] (const Node& a, const Node& b) { return a.x < b.x; });

        if ((int) v.size() > kMaxNodes)
        {
            const Node last = v.back();
            v.resize ((size_t) kMaxNodes - 1);
            v.push_back (last);
        }

        v.front().x = 0.0f;
        v.back().x  = 1.0f;
        v.back().bend = 0.0f;

        const size_t n = v.size();
        for (size_t i = 1; i + 1 < n; ++i)
            v[i].x = std::max (v[i].x, v[i - 1].x + kMinGap);
        for (size_t i = n - 2; i >= 1; --i)
            v[i].x = std::min (v[i].x, v[i + 1].x - kMinGap);

        return v;
    }

    // must be called with the lock held
    void bake()
    {
        const size_t n = nodes.size();
        size_t seg = 0;
        for (int t = 0; t < kTableSize; ++t)
        {
            const float x = (float) t / (float) (kTableSize - 1);
            while (seg + 2 < n && x > nodes[seg + 1].x)
                ++seg;

            const Node& a = nodes[seg];
            const Node& b = nodes[seg + 1];
            const float h = b.x - a.x;
            const float u = h > 0.0f ? std::clamp ((x - a.x) / h, 0.0f, 1.0f) : 1.0f;
            table[(size_t) t].store (segmentValue (a.y, b.y, a.bend, u), std::memory_order_relaxed);
        }
    }

    mutable std::mutex lock;
    std::vector<Node> nodes;
    std::array<std::atomic<float>, kTableSize> table;
};

//==============================================================================
// Plays the curve once, from the moment it is triggered (MIDI / Audio modes)
class OneShot
{
public:
    void setLengthSamples (double n) { length = std::max (1.0, n); }
    void trigger()                   { elapsed = 0.0; active = true; }
    void stop()                      { active = false; lastPhase = -1.0f; }
    float phase() const              { return lastPhase; }

    // advances by one sample and returns the position in the cycle (0..1), or -1 when idle
    float nextPhase()
    {
        if (! active) { lastPhase = -1.0f; return -1.0f; }
        const double x = elapsed / length;
        if (x >= 1.0) { stop(); return -1.0f; }
        elapsed += 1.0;
        lastPhase = (float) x;
        return lastPhase;
    }

    float next (const CurveTable& curve)
    {
        if (! active) { lastPhase = -1.0f; return 1.0f; }

        const double x = elapsed / length;
        if (x >= 1.0) { stop(); return 1.0f; }

        elapsed += 1.0;
        lastPhase = (float) x;
        return curve.eval ((float) x);
    }

private:
    double elapsed = 0.0, length = 1.0;
    bool active = false;
    float lastPhase = -1.0f;
};

//==============================================================================
// Smooths the gain so steep curve edges don't click.
// Two one-pole filters in a row (rounder than one), a tiny built-in minimum so
// a hard edge can never be a true click, and the smoothing time itself glides
// to its new value so turning the knob can't cause a jump.
class GainSmoother
{
public:
    static constexpr float kMinMs = 0.5f;

    void prepare (double sr)
    {
        sampleRate = sr;
        glide = (float) (1.0 - std::exp (-1.0 / (0.02 * sr)));   // ~20 ms
        timeCur = timeTarget = std::max (timeTarget, kMinMs);
        updateCoef();
        reset (1.0f);
    }

    void setTimeMs (float ms) { timeTarget = std::max (ms, kMinMs); }

    void reset (float v = 1.0f) { v1 = v2 = v; }

    float process (float target)
    {
        if (timeCur != timeTarget)
        {
            timeCur += glide * (timeTarget - timeCur);
            if (std::fabs (timeTarget - timeCur) < 1e-4f)
                timeCur = timeTarget;
            if (timeCur == timeTarget || (++counter & 15) == 0)
                updateCoef();
        }
        v1 += coef * (target - v1);
        v2 += coef * (v1 - v2);
        return v2;
    }

private:
    void updateCoef()
    {
        const double tau = 0.5 * 0.001 * (double) timeCur;   // each of the two stages
        coef = (float) (1.0 - std::exp (-1.0 / (tau * sampleRate)));
    }

    double sampleRate = 44100.0;
    float timeCur = 2.0f, timeTarget = 2.0f, glide = 0.01f, coef = 0.1f;
    float v1 = 1.0f, v2 = 1.0f;
    unsigned counter = 0;
};

//==============================================================================
// Speed in Hz: a free-running position in the cycle (0..1) that advances `hz` times per second.
// The rate itself glides to a new value (in the log domain, so a sweep from 1 to 8 Hz feels
// even), which is what makes automating the speed sound smooth instead of stepping.
constexpr float kMinHz = 0.05f;
constexpr float kMaxHz = 40.0f;

class HzPhase
{
public:
    void prepare (double sr, float hz)
    {
        sampleRate = sr;
        coef = (float) (1.0 - std::exp (-1.0 / (0.05 * sr)));   // ~50 ms glide
        target = cur = std::log (std::clamp (hz, kMinHz, kMaxHz));
        phase = 0.0;
    }

    void setHz (float hz) { target = std::log (std::clamp (hz, kMinHz, kMaxHz)); }
    void restart()        { phase = 0.0; }
    float currentHz() const { return std::exp (cur); }

    // position in the cycle for this sample, then move on by one sample
    double next()
    {
        cur += coef * (target - cur);
        const double p = phase;
        phase += (double) std::exp (cur) / sampleRate;
        phase -= std::floor (phase);
        return p;
    }

private:
    double sampleRate = 44100.0, phase = 0.0;
    float coef = 0.0004f, cur = 0.0f, target = 0.0f;
};

//==============================================================================
// Finds the start of each kick in the sidechain signal.
// A fast envelope must cross the threshold AND jump well above a slow envelope,
// so a new kick is still caught on top of the long tail of the previous one.
class OnsetDetector
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        fastAtk  = coefFor (0.3);
        fastRel  = coefFor (20.0);
        slowCoef = coefFor (120.0);
        reset();
    }

    void reset() { fast = 0.0f; slow = 0.0f; holdLeft = 0; }

    void setParams (float thresholdDb, float holdMs)
    {
        threshold   = std::pow (10.0f, thresholdDb / 20.0f);
        holdSamples = (int) (holdMs * 0.001 * sampleRate);
    }

    // feed the absolute level of one sample; returns true on a kick onset
    bool process (float level)
    {
        const float a = std::fabs (level);
        fast = a + (a > fast ? fastAtk : fastRel) * (fast - a);
        slow = fast + slowCoef * (slow - fast);

        if (holdLeft > 0) { --holdLeft; return false; }

        if (fast > threshold && fast > ratio * slow)
        {
            holdLeft = holdSamples;
            return true;
        }
        return false;
    }

private:
    float coefFor (double ms) const { return (float) std::exp (-1.0 / (0.001 * ms * sampleRate)); }

    double sampleRate = 44100.0;
    float fastAtk = 0.9f, fastRel = 0.99f, slowCoef = 0.999f;
    float fast = 0.0f, slow = 0.0f;
    float threshold = 0.06f;
    float ratio = 1.8f;
    int holdSamples = 4410, holdLeft = 0;
};


//==============================================================================
// Effect modules: every module shapes one thing with its own curve.
enum FxType { fxOff = 0, fxVolume, fxFilter, fxPan, fxWidth, fxDistortion, fxPitch, kNumFxTypes };
constexpr int kMaxSlots = 6;

inline const char* fxName (int t)
{
    static const char* n[kNumFxTypes] = { "Off", "Volume", "Filter", "Pan", "Width", "Distortion", "Pitch" };
    return n[std::clamp (t, 0, kNumFxTypes - 1)];
}

inline const char* fxHint (int t)
{
    switch (t)
    {
        case fxVolume:     return "Sidechain pumping and gating";
        case fxFilter:     return "Low-pass cutoff opens and closes";
        case fxPan:        return "Moves the sound left and right";
        case fxWidth:      return "Stereo width, from mono to extra wide";
        case fxDistortion: return "Amount of drive follows the curve";
        case fxPitch:      return "Pitch dives and risers (up to 12 semitones)";
        default:           return "";
    }
}

// ---- options of the effects (the "style" of a module and its second knob)
constexpr int kMaxStyles = 8;

inline int fxStyleCount (int t)
{
    switch (t) { case fxFilter: return 2; case fxDistortion: return 6; case fxPitch: return 4; default: return 0; }
}

inline const char* fxStyleTitle (int t)
{
    switch (t) { case fxFilter: return "TYPE"; case fxDistortion: return "STYLE"; case fxPitch: return "RANGE"; default: return ""; }
}

inline const char* fxStyleName (int t, int i)
{
    static const char* filter[] = { "Low-pass", "High-pass" };
    static const char* dist[]   = { "Overdrive", "Tube", "Fuzz", "Hard clip", "Wavefold", "Bitcrush" };
    static const char* pitch[]  = { "+/- 12 semitones", "+/- 24 semitones", "+/- 7 (a fifth)", "+/- 2 (wobble)" };
    const int n = fxStyleCount (t);
    if (n == 0) return "";
    i = std::clamp (i, 0, n - 1);
    return t == fxFilter ? filter[i] : (t == fxDistortion ? dist[i] : pitch[i]);
}

inline const char* fxParamName (int t)
{
    switch (t) { case fxFilter: return "RESONANCE"; case fxDistortion: return "TONE"; default: return ""; }
}

inline float fxParamDefault (int t)
{
    switch (t) { case fxFilter: return 25.0f; case fxDistortion: return 100.0f; default: return 50.0f; }
}

inline float fxPitchRange (int style)
{
    static const float r[] = { 12.0f, 24.0f, 7.0f, 2.0f };
    return r[std::clamp (style, 0, 3)];
}

inline bool fxIsHighpass (int t, int style) { return t == fxFilter && style == 1; }

// the curve height that leaves the sound unchanged (used while nothing is playing)
inline float fxNeutral (int t, int style = 0)
{
    switch (t)
    {
        case fxVolume: return 1.0f;
        case fxFilter: return fxIsHighpass (t, style) ? 0.0f : 1.0f;
        case fxPan: case fxWidth: case fxPitch: return 0.5f;
        default: return 0.0f;
    }
}

// what the top and the bottom of the curve mean
inline const char* fxTopLabel (int t, int style = 0)
{
    static const char* n[kNumFxTypes] = { "", "full volume", "open", "right", "wide", "max drive", "up" };
    if (fxIsHighpass (t, style)) return "high cutoff";
    return n[std::clamp (t, 0, kNumFxTypes - 1)];
}
inline const char* fxBottomLabel (int t, int style = 0)
{
    static const char* n[kNumFxTypes] = { "", "silent", "closed", "left", "mono", "clean", "down" };
    if (fxIsHighpass (t, style)) return "open";
    return n[std::clamp (t, 0, kNumFxTypes - 1)];
}

// the curve height after the Depth knob is applied
inline float fxEffective (int t, float v, float depth, int style = 0)
{
    switch (t)
    {
        case fxVolume: return 1.0f - depth * (1.0f - v);
        case fxFilter: return fxIsHighpass (t, style) ? depth * v : 1.0f - depth * (1.0f - v);
        case fxPan: case fxWidth: case fxPitch: return 0.5f + depth * (v - 0.5f);
        default: return depth * v;
    }
}

inline std::string fxValueText (int t, float v, float depth, int style = 0)
{
    char b[64];
    const float e = fxEffective (t, v, depth, style);
    switch (t)
    {
        case fxVolume:
            std::snprintf (b, sizeof b, "volume %d%%", (int) std::lround (e * 100.0f)); break;
        case fxFilter:
        {
            const float fc = 20.0f * std::pow (1000.0f, e);
            if (fc >= 1000.0f) std::snprintf (b, sizeof b, "cutoff %.1f kHz", fc / 1000.0f);
            else               std::snprintf (b, sizeof b, "cutoff %d Hz", (int) std::lround (fc));
            break;
        }
        case fxPan:
        {
            const int p = (int) std::lround ((e * 2.0f - 1.0f) * 100.0f);
            if (p == 0) std::snprintf (b, sizeof b, "pan centre");
            else        std::snprintf (b, sizeof b, "pan %d%% %s", std::abs (p), p < 0 ? "left" : "right");
            break;
        }
        case fxWidth:
            std::snprintf (b, sizeof b, "width %d%%", (int) std::lround (e * 200.0f)); break;
        case fxDistortion:
            std::snprintf (b, sizeof b, "drive %d%%", (int) std::lround (e * 100.0f)); break;
        case fxPitch:
            std::snprintf (b, sizeof b, "pitch %+.1f st", (e * 2.0f - 1.0f) * fxPitchRange (style)); break;
        default: b[0] = 0;
    }
    return b;
}

//------------------------------------------------------------------------------
// The sound processing of the non-volume modules (volume is done by the plugin itself, because
// it can also work on the low frequencies only).
class FxProcessor
{
public:
    static constexpr int kPitchBuf = 1 << 15;

    void prepare (double sr)
    {
        sampleRate = sr;
        windowSamples = std::clamp (0.04 * sr, 64.0, (double) kPitchBuf / 4.0);
        for (auto& b : buf) b.assign ((size_t) kPitchBuf, 0.0f);
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            ic1[c] = ic2[c] = 0.0f;
            std::fill (buf[c].begin(), buf[c].end(), 0.0f);
            dcX[c] = dcY[c] = toneState[c] = 0.0f;
        }
        wpos = 0;
        phi = 0.0;
        lastEff = lastRes = -1.0f;
    }

    // process one frame in place. x[0..n-1] = channels, v = curve value after smoothing,
    // depth 0..1, style = the option chosen for this effect, param = its second knob (0..1)
    void process (int type, int style, float v, float depth, float param, float* x, int n)
    {
        const float e = fxEffective (type, v, depth, style);
        switch (type)
        {
            case fxFilter:     filter (e, style, param, x, n); break;
            case fxPan:        pan (e, x, n); break;
            case fxWidth:      width (e, x, n); break;
            case fxDistortion: distort (e, style, param, x, n); break;
            case fxPitch:      pitch (e, fxPitchRange (style), x, n); break;
            default: break;
        }
    }

    // short form with the first option of every effect
    void process (int type, float v, float depth, float* x, int n)
    {
        process (type, 0, v, depth, fxParamDefault (type) * 0.01f, x, n);
    }

private:
    void filter (float e, int style, float res, float* x, int n)
    {
        if (e != lastEff || res != lastRes)
        {
            lastEff = e;
            lastRes = res;
            double fc = 20.0 * std::pow (1000.0, (double) e);
            fc = std::min (fc, 0.45 * sampleRate);
            const double g = std::tan (3.14159265358979323846 * fc / sampleRate);
            const double k = 1.4 - 1.2 * (double) std::clamp (res, 0.0f, 1.0f);   // lower k = more resonance
            kCoef = (float) k;
            a1 = (float) (1.0 / (1.0 + g * (g + k)));
            a2 = (float) (g * a1);
            a3 = (float) (g * a2);
        }
        for (int c = 0; c < n; ++c)
        {
            const float v3 = x[c] - ic2[c];
            const float v1 = a1 * ic1[c] + a2 * v3;
            const float v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
            const float in = x[c];
            ic1[c] = 2.0f * v1 - ic1[c];
            ic2[c] = 2.0f * v2 - ic2[c];
            x[c] = style == 1 ? in - kCoef * v1 - v2 : v2;
        }
    }

    static void pan (float e, float* x, int n)
    {
        if (n < 2) return;
        const float theta = e * 1.57079633f;   // 0 = left, pi/2 = right
        x[0] *= std::cos (theta) * 1.41421356f;
        x[1] *= std::sin (theta) * 1.41421356f;
    }

    static void width (float e, float* x, int n)
    {
        if (n < 2) return;
        const float w = e * 2.0f;                                     // 0 = mono, 1 = normal, 2 = extra wide
        const float m = 0.5f * (x[0] + x[1]), s = 0.5f * (x[0] - x[1]) * w;
        x[0] = m + s;
        x[1] = m - s;
    }

    void distort (float e, int style, float tone, float* x, int n)
    {
        if (e <= 0.0001f) return;
        const float pre = 1.0f + 30.0f * e;
        const float comp = std::pow (pre, -0.4f);
        const float a = std::min (1.0f, e * 10.0f);

        // low-pass after the distortion (the Tone knob): 800 Hz .. 20 kHz
        const bool useTone = tone < 0.99f;
        float toneCoef = 1.0f;
        if (useTone)
        {
            const double fc = std::min (800.0 * std::pow (25.0, (double) tone), 0.45 * sampleRate);
            toneCoef = (float) (1.0 - std::exp (-2.0 * 3.14159265358979323846 * fc / sampleRate));
        }

        for (int c = 0; c < n; ++c)
        {
            const float in = x[c];
            float y;
            switch (style)
            {
                case 1:   // tube: uneven top and bottom (adds even harmonics), centred again afterwards
                {
                    const float u = pre * (in + 0.3f * in * std::fabs (in)) + 0.15f;
                    y = (std::tanh (u) - std::tanh (0.15f)) * comp;
                    y = dcBlock (c, y);
                    break;
                }
                case 2:   // fuzz: very hard, uneven and bright
                {
                    y = (std::tanh (pre * 2.5f * in + 0.35f) - std::tanh (0.35f)) * comp * 0.9f;
                    y = dcBlock (c, y);
                    break;
                }
                case 3:   // hard clip: flat tops
                    y = std::clamp (pre * in, -1.0f, 1.0f) * comp;
                    break;
                case 4:   // wavefold: the wave folds back on itself
                    y = std::sin (1.5707963f * pre * in * 0.7f) * 0.8f;
                    break;
                case 5:   // bitcrush: fewer and fewer steps (16 bit down to 3 bit)
                {
                    const float bits = 16.0f - 13.0f * e;
                    const float levels = std::pow (2.0f, bits - 1.0f);
                    y = std::round (in * levels) / levels;
                    break;
                }
                default:  // overdrive: smooth tube-like saturation
                    y = std::tanh (pre * in) * comp;
                    break;
            }

            y = in + (y - in) * a;
            if (useTone)
            {
                toneState[c] += toneCoef * (y - toneState[c]);
                y = toneState[c];
            }
            x[c] = y;
        }
    }

    float dcBlock (int c, float y)
    {
        const float out = y - dcX[c] + 0.995f * dcY[c];
        dcX[c] = y;
        dcY[c] = out;
        return out;
    }

    void pitch (float e, float range, float* x, int n)
    {
        const float semis = (e * 2.0f - 1.0f) * range;
        const int mask = kPitchBuf - 1;
        for (int c = 0; c < n; ++c)
            buf[c][(size_t) (wpos & mask)] = x[c];

        const double r = std::pow (2.0, (double) semis / 12.0);
        phi += (1.0 - r) / windowSamples;
        phi -= std::floor (phi);

        const float amount = std::min (1.0f, std::fabs (semis) / 0.15f);   // fades to the clean sound at 0 semitones
        if (amount > 0.0f)
        {
            const double u[2] = { phi, phi + 0.5 - std::floor (phi + 0.5) };
            for (int c = 0; c < n; ++c)
            {
                float y = 0.0f;
                for (int t = 0; t < 2; ++t)
                {
                    const double d = u[t] * windowSamples + 2.0;
                    const double rp = (double) wpos - d;
                    const long i0 = (long) std::floor (rp);
                    const float fr = (float) (rp - (double) i0);
                    const float a = buf[c][(size_t) (i0 & mask)], b = buf[c][(size_t) ((i0 + 1) & mask)];
                    const float s = std::sin (3.14159265f * (float) u[t]);
                    y += (a + (b - a) * fr) * s * s;
                }
                x[c] += (y - x[c]) * amount;
            }
        }
        ++wpos;
    }

    double sampleRate = 44100.0, windowSamples = 1764.0, phi = 0.0;
    std::array<std::vector<float>, 2> buf;
    long wpos = 0;
    float ic1[2] = { 0, 0 }, ic2[2] = { 0, 0 }, dcX[2] = { 0, 0 }, dcY[2] = { 0, 0 }, toneState[2] = { 0, 0 };
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, kCoef = 1.0f, lastEff = -1.0f, lastRes = -1.0f;
};

} // namespace pump
