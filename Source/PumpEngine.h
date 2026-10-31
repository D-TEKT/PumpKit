// PumpEngine.h
// All the "brains" of PumpKit that don't need JUCE: curve shapes, curve points,
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
constexpr int   kNumPresets = 16;
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
// Curve points
struct Node
{
    float x = 0.0f;   // time in the cycle, 0..1
    float y = 0.0f;   // volume, 0 = silent, 1 = full
};

// Ramer-Douglas-Peucker on evenly spaced samples: keeps only the points that matter
inline void simplifyRange (const std::vector<float>& ys, int a, int b, float tol, std::vector<char>& keep)
{
    if (b <= a + 1)
        return;

    float worst = 0.0f;
    int idx = -1;
    for (int i = a + 1; i < b; ++i)
    {
        const float t = (float) (i - a) / (float) (b - a);
        const float line = ys[(size_t) a] + (ys[(size_t) b] - ys[(size_t) a]) * t;
        const float e = std::fabs (ys[(size_t) i] - line);
        if (e > worst) { worst = e; idx = i; }
    }

    if (idx >= 0 && worst > tol)
    {
        keep[(size_t) idx] = 1;
        simplifyRange (ys, a, idx, tol, keep);
        simplifyRange (ys, idx, b, tol, keep);
    }
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
        std::vector<char> keep ((size_t) n, 0);
        keep[0] = keep[(size_t) n - 1] = 1;
        simplifyRange (ys, 0, n - 1, t, keep);

        out.clear();
        for (int i = 0; i < n; ++i)
            if (keep[(size_t) i])
                out.push_back ({ (float) i / (float) (n - 1), std::clamp (ys[(size_t) i], 0.0f, 1.0f) });

        if ((int) out.size() <= maxNodes)
            break;
        t *= 1.5f;
    }
    return out;
}

//==============================================================================
// The editable curve: a list of points (edited from the UI thread) that is baked
// into a 256-sample table which the audio thread reads without any locking.
class CurveTable
{
public:
    CurveTable()
    {
        for (auto& t : table)
            t.store (0.0f, std::memory_order_relaxed);
        setPreset (0);
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

    bool isSmooth() const { return smooth.load(); }

    void setSmooth (bool s)
    {
        std::lock_guard<std::mutex> g (lock);
        smooth.store (s);
        bake();
    }

    void setPreset (int preset)
    {
        std::vector<float> ys (257);
        for (size_t i = 0; i < ys.size(); ++i)
            ys[i] = presetValue (preset, (float) i / 256.0f);
        setNodes (simplify (ys, 0.012f, 24));
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

        nodes.insert (nodes.begin() + (std::ptrdiff_t) i, Node { std::clamp (x, lo, hi), y });
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
        std::string s = smooth.load() ? "pk2;1" : "pk2;0";
        char buf[64];
        for (const auto& n : nodes)
        {
            std::snprintf (buf, sizeof (buf), ";%.5f,%.5f", (double) n.x, (double) n.y);
            s += buf;
        }
        return s;
    }

    bool fromString (const std::string& s)
    {
        if (s.rfind ("pk2;", 0) == 0)
        {
            std::vector<Node> v;
            const char* p = s.c_str() + 4;
            const bool sm = *p == '1';
            while (*p != '\0' && *p != ';') ++p;

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
                v.push_back ({ x, y });
            }
            if (v.size() < 2)
                return false;

            setNodes (std::move (v));
            setSmooth (sm);
            return true;
        }

        // older format: 64 comma-separated heights
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
            v = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };

        for (auto& n : v)
        {
            n.x = std::clamp (n.x, 0.0f, 1.0f);
            n.y = std::clamp (n.y, 0.0f, 1.0f);
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

        const size_t n = v.size();
        for (size_t i = 1; i + 1 < n; ++i)
            v[i].x = std::max (v[i].x, v[i - 1].x + kMinGap);
        for (size_t i = n - 2; i >= 1; --i)
            v[i].x = std::min (v[i].x, v[i + 1].x - kMinGap);

        return v;
    }

    static float sgn (float v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }

    static float endSlope (float h0, float h1, float d0, float d1)
    {
        float s = ((2.0f * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
        if (sgn (s) != sgn (d0))
            s = 0.0f;
        else if (sgn (d0) != sgn (d1) && std::fabs (s) > 3.0f * std::fabs (d0))
            s = 3.0f * d0;
        return s;
    }

    // must be called with the lock held
    void bake()
    {
        const size_t n = nodes.size();
        const bool sm = smooth.load();

        std::vector<float> m (n, 0.0f);
        if (sm)
        {
            std::vector<float> d (n - 1), h (n - 1);
            for (size_t k = 0; k + 1 < n; ++k)
            {
                h[k] = nodes[k + 1].x - nodes[k].x;
                d[k] = (nodes[k + 1].y - nodes[k].y) / h[k];
            }

            if (n == 2)
            {
                m[0] = m[1] = d[0];
            }
            else
            {
                for (size_t k = 1; k + 1 < n; ++k)
                {
                    if (d[k - 1] * d[k] <= 0.0f)
                        m[k] = 0.0f;
                    else
                    {
                        const float w1 = 2.0f * h[k] + h[k - 1];
                        const float w2 = h[k] + 2.0f * h[k - 1];
                        m[k] = (w1 + w2) / (w1 / d[k - 1] + w2 / d[k]);
                    }
                }
                m[0]     = endSlope (h[0], h[1], d[0], d[1]);
                m[n - 1] = endSlope (h[n - 2], h[n - 3], d[n - 2], d[n - 3]);
            }
        }

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

            float y;
            if (sm)
            {
                const float u2 = u * u, u3 = u2 * u;
                y = (2.0f * u3 - 3.0f * u2 + 1.0f) * a.y
                  + (u3 - 2.0f * u2 + u) * h * m[seg]
                  + (-2.0f * u3 + 3.0f * u2) * b.y
                  + (u3 - u2) * h * m[seg + 1];
            }
            else
            {
                y = a.y + (b.y - a.y) * u;
            }
            table[(size_t) t].store (std::clamp (y, 0.0f, 1.0f), std::memory_order_relaxed);
        }
    }

    mutable std::mutex lock;
    std::vector<Node> nodes;
    std::atomic<bool> smooth { false };
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
