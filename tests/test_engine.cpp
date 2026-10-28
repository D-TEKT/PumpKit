// Plain C++ tests for the JUCE-free engine. Build: g++ -std=c++17 -I Source tests/test_engine.cpp
#include "PumpEngine.h"
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { std::printf ("FAIL: %s\n", msg); ++failures; } else { std::printf ("ok:   %s\n", msg); } } while (0)

// A hardstyle-ish kick: pitch-dropping sine with an exponential tail
static void addKick (std::vector<float>& sig, double sr, double startSec, double amp, double decaySec)
{
    const size_t start = (size_t) (startSec * sr);
    double phase = 0.0;
    for (size_t n = 0; start + n < sig.size(); ++n)
    {
        const double t = (double) n / sr;
        if (t > 4.0 * decaySec) break;
        const double freq = 50.0 + 150.0 * std::exp (-t / 0.02);
        phase += 2.0 * 3.14159265358979 * freq / sr;
        sig[start + n] += (float) (amp * std::exp (-t / decaySec) * std::sin (phase + 1.5707963));
    }
}

static std::vector<double> detect (const std::vector<float>& sig, double sr, float thrDb, float holdMs)
{
    pump::OnsetDetector det;
    det.prepare (sr);
    det.setParams (thrDb, holdMs);
    std::vector<double> hits;
    for (size_t i = 0; i < sig.size(); ++i)
        if (det.process (sig[i])) hits.push_back ((double) i / sr);
    return hits;
}

int main()
{
    // ---- presets
    bool rangeOk = true, endOk = true, startOk = true;
    for (int p = 0; p < pump::kNumPresets; ++p)
    {
        for (int i = 0; i <= 100; ++i)
        {
            const float v = pump::presetValue (p, i / 100.0f);
            if (v < 0.0f || v > 1.0f) rangeOk = false;
        }
        if (pump::presetValue (p, 1.0f) < 0.99f) endOk = false;
        if (pump::presetValue (p, 0.0f) > 0.45f) startOk = false;
    }
    CHECK (rangeOk, "all 16 presets stay within 0..1");
    CHECK (endOk,   "all presets recover to full volume at the end of the cycle");
    CHECK (startOk, "all presets start ducked");

    // ---- curve table
    pump::CurveTable ct;
    CHECK (std::fabs (ct.eval (0.5f) - 0.5f) < 0.01f, "linear curve evaluates to ~0.5 at the midpoint");
    ct.setPreset (9);
    CHECK (ct.eval (0.2f) < 0.01f && ct.eval (0.8f) > 0.99f, "gate preset: closed first half, open second half");
    ct.setPoint (10, 0.25f);
    const std::string s = ct.toString();
    pump::CurveTable ct2;
    CHECK (ct2.fromString (s), "curve text can be parsed back");
    CHECK (std::fabs (ct2.getPoint (10) - 0.25f) < 0.001f, "edited point survives save/restore");
    CHECK (! ct2.fromString ("1,2,3"), "short/garbage curve text is rejected");

    // ---- sync phase
    CHECK (std::fabs (pump::syncPhase (4.0, 1.0)) < 1e-9, "quarter-note cycle restarts on every beat");
    CHECK (std::fabs (pump::syncPhase (4.5, 1.0) - 0.5) < 1e-9, "half-way between beats is phase 0.5");
    CHECK (std::fabs (pump::syncPhase (-0.25, 1.0) - 0.75) < 1e-9, "negative (pre-roll) positions wrap correctly");

    // ---- one-shot
    {
        pump::CurveTable lin; // linear ramp
        pump::OneShot os;
        os.setLengthSamples (1000.0);
        CHECK (os.next (lin) == 1.0f, "one-shot idle = unity gain");
        os.trigger();
        float first = os.next (lin);
        float last = 0.0f, mid = 0.0f;
        for (int i = 1; i < 1000; ++i) { last = os.next (lin); if (i == 500) mid = last; }
        CHECK (first < 0.01f, "one-shot starts fully ducked");
        CHECK (std::fabs (mid - 0.5f) < 0.02f, "one-shot is half way after half its length");
        CHECK (last > 0.97f, "one-shot is almost back to full at the end");
        CHECK (os.next (lin) == 1.0f && os.phase() < 0.0f, "one-shot returns to idle after its length");
    }

    // ---- smoother
    {
        pump::GainSmoother sm;
        sm.prepare (48000.0);
        sm.setTimeMs (0.0f);
        CHECK (sm.process (0.0f) == 0.0f, "smooth = 0 follows instantly");
        sm.reset (1.0f);
        sm.setTimeMs (5.0f);
        float v = 1.0f;
        for (int i = 0; i < (int) (0.005 * 48000); ++i) v = sm.process (0.0f);
        CHECK (std::fabs (v - std::exp (-1.0f)) < 0.02f, "smooth reaches ~37% remaining after one time constant");
    }

    // ---- onset detector
    const double sr = 48000.0;
    {
        // 158 BPM -> 0.38 s per beat, 8 kicks, long 0.15 s tail
        const double beat = 60.0 / 158.0;
        std::vector<float> sig ((size_t) (sr * 3.5), 0.0f);
        for (int k = 0; k < 8; ++k) addKick (sig, sr, 0.1 + k * beat, 0.9, 0.15);
        auto hits = detect (sig, sr, -24.0f, 100.0f);
        CHECK (hits.size() == 8, "finds all 8 kicks at 158 BPM with long tails");
        bool timingOk = hits.size() == 8;
        for (size_t k = 0; k < hits.size() && k < 8; ++k)
        {
            const double err = hits[k] - (0.1 + (double) k * beat);
            if (err < 0.0 || err > 0.003) timingOk = false;
            std::printf ("      kick %zu detected %.2f ms after its start\n", k, err * 1000.0);
        }
        CHECK (timingOk, "each onset is caught within 3 ms of the kick starting");
    }
    {
        // very long tails (0.3 s) overlapping the next kick at 150 BPM
        const double beat = 60.0 / 150.0;
        std::vector<float> sig ((size_t) (sr * 3.0), 0.0f);
        for (int k = 0; k < 6; ++k) addKick (sig, sr, 0.1 + k * beat, 0.9, 0.3);
        auto hits = detect (sig, sr, -24.0f, 100.0f);
        CHECK (hits.size() == 6, "finds all 6 kicks even when tails overlap the next kick");
    }
    {
        // quiet noise below threshold must not trigger
        std::vector<float> sig ((size_t) (sr * 2.0), 0.0f);
        unsigned seed = 12345;
        for (auto& v : sig) { seed = seed * 1664525u + 1013904223u; v = ((float) (seed >> 8) / 16777216.0f - 0.5f) * 0.02f; }
        auto hits = detect (sig, sr, -24.0f, 100.0f);
        CHECK (hits.empty(), "background noise below the threshold does not trigger");
    }
    {
        // two hits closer than the hold-off count once
        std::vector<float> sig ((size_t) (sr * 1.0), 0.0f);
        addKick (sig, sr, 0.1, 0.9, 0.1);
        addKick (sig, sr, 0.14, 0.9, 0.1);
        auto hits = detect (sig, sr, -24.0f, 100.0f);
        CHECK (hits.size() == 1, "hold-off prevents double-triggering inside 100 ms");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
