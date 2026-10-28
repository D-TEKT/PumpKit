// PumpEngine.h
// All the "brains" of PumpKit that don't need JUCE: curve shapes, timing,
// trigger detection and gain smoothing. Kept separate so it can be unit-tested.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace pump
{
constexpr int kCurvePoints = 64;
constexpr int kNumPresets  = 16;
constexpr int kNumLengths  = 12;

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
// The 16 built-in curves. x = time through the cycle (0..1),
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
// The editable curve: 64 points, linear interpolation between them.
// Points are atomics so the UI thread can draw into it while audio reads it.
class CurveTable
{
public:
    CurveTable() { setPreset (0); }

    void setPreset (int preset)
    {
        for (int i = 0; i < kCurvePoints; ++i)
            pts[(size_t) i].store (presetValue (preset, (float) i / (float) (kCurvePoints - 1)),
                                   std::memory_order_relaxed);
    }

    void setPoint (int i, float v)
    {
        if (i >= 0 && i < kCurvePoints)
            pts[(size_t) i].store (std::clamp (v, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    float getPoint (int i) const
    {
        return pts[(size_t) std::clamp (i, 0, kCurvePoints - 1)].load (std::memory_order_relaxed);
    }

    float eval (float x) const
    {
        x = std::clamp (x, 0.0f, 1.0f);
        const float f = x * (float) (kCurvePoints - 1);
        const int i = (int) f;
        if (i >= kCurvePoints - 1)
            return getPoint (kCurvePoints - 1);
        const float frac = f - (float) i;
        const float a = getPoint (i), b = getPoint (i + 1);
        return a + (b - a) * frac;
    }

    std::string toString() const
    {
        std::string s;
        char buf[32];
        for (int i = 0; i < kCurvePoints; ++i)
        {
            std::snprintf (buf, sizeof (buf), "%.4f", (double) getPoint (i));
            if (i > 0) s += ',';
            s += buf;
        }
        return s;
    }

    bool fromString (const std::string& s)
    {
        std::array<float, kCurvePoints> tmp {};
        const char* p = s.c_str();
        for (int i = 0; i < kCurvePoints; ++i)
        {
            char* end = nullptr;
            const float v = std::strtof (p, &end);
            if (end == p) return false;
            tmp[(size_t) i] = v;
            p = end;
            if (*p == ',') ++p;
        }
        for (int i = 0; i < kCurvePoints; ++i)
            setPoint (i, tmp[(size_t) i]);
        return true;
    }

private:
    std::array<std::atomic<float>, kCurvePoints> pts;
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
// One-pole smoothing of the gain, so steep curve edges don't click
class GainSmoother
{
public:
    void prepare (double sr) { sampleRate = sr; setTimeMs (timeMs); value = 1.0f; }

    void setTimeMs (float ms)
    {
        timeMs = ms;
        coef = ms <= 0.01f ? 1.0f
                           : (float) (1.0 - std::exp (-1.0 / (0.001 * (double) ms * sampleRate)));
    }

    void reset (float v = 1.0f) { value = v; }

    float process (float target)
    {
        value += coef * (target - value);
        return value;
    }

private:
    double sampleRate = 44100.0;
    float timeMs = 2.0f, coef = 1.0f, value = 1.0f;
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

} // namespace pump
