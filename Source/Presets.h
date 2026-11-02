// Presets.h
// The preset library: groups by genre/vibe, each with its own colour, and optional
// sub-folders inside a group. Every preset is a list of points {time, volume, bend}
// for one cycle, plus the Length (note value) it was drawn for.
#pragma once

#include "PumpEngine.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace pump
{
using NodeList = std::vector<Node>;

struct PresetDef
{
    std::string name;
    int length = 3;       // index into the Length list (3 = 1/4)
    NodeList nodes;
    std::string folder;   // optional sub-folder inside the genre ("" = directly in the genre)
};

struct GenreDef
{
    std::string name;
    unsigned colour = 0xffff3b3b;   // 0xAARRGGBB
    std::vector<PresetDef> presets;
};

//------------------------------------------------------------------------------
// Small building blocks, so presets stay readable
//   depth = how low it ducks (0 = silent), rec = where it is back at full volume

// duck to `depth`, then rise back to full
inline NodeList pumpS (float depth, float rec, float bend = 0.0f)
{
    return { { 0.0f, depth, bend }, { rec, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
}

// duck, stay down until `hold`, then rise back to full by `rec`
inline NodeList holdS (float depth, float hold, float rec, float bend = 0.0f)
{
    return { { 0.0f, depth, 0.0f }, { hold, depth, bend }, { rec, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
}

// closed (ducked) until `closed`, then open
inline NodeList gateS (float closed, float depth = 0.0f)
{
    return { { 0.0f, depth, 0.0f }, { closed, depth, 0.0f }, { closed + 0.015f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
}

// repeat a shape n times inside one cycle
inline NodeList tile (const NodeList& base, int n)
{
    NodeList out;
    for (int k = 0; k < n; ++k)
        for (const auto& b : base)
            out.push_back ({ ((float) k + b.x) / (float) n, b.y, b.bend });
    return out;
}

// Open pulses (start, width as a fraction of the cycle) with closed gaps in between.
// Every edge has a tiny ramp so a gate never clicks.
inline NodeList pulseN (std::vector<std::pair<float, float>> pulses, float closed = 0.0f, float open = 1.0f)
{
    const float ramp = 0.006f;

    // pulses that overlap or nearly touch become one longer pulse
    std::sort (pulses.begin(), pulses.end());
    std::vector<std::pair<float, float>> merged;
    for (const auto& pw : pulses)
    {
        if (! merged.empty() && pw.first <= merged.back().first + merged.back().second + 3.0f * ramp)
            merged.back().second = std::max (merged.back().second, pw.first + pw.second - merged.back().first);
        else
            merged.push_back (pw);
    }
    pulses = merged;

    NodeList n;
    n.push_back ({ 0.0f, closed, 0.0f });
    for (const auto& pw : pulses)
    {
        const float on  = pw.first;
        const float off = std::min (pw.first + pw.second, 1.0f);
        if (on > n.back().x + 0.002f)
            n.push_back ({ on, closed, 0.0f });
        n.push_back ({ std::min (on + ramp, off), open, 0.0f });
        if (off > on + ramp + 0.002f)
            n.push_back ({ off, open, 0.0f });
        if (off < 1.0f - 0.002f)
            n.push_back ({ std::min (off + ramp, 1.0f), closed, 0.0f });
    }
    if (n.back().x < 1.0f - 0.002f)
        n.push_back ({ 1.0f, n.back().y, 0.0f });
    else
        n.back().x = 1.0f;
    return n;
}

// the same, with the pulses given as steps of an even grid
inline NodeList pulseS (const std::vector<float>& startSteps, float steps, float widthSteps,
                        float closed = 0.0f, float open = 1.0f)
{
    std::vector<std::pair<float, float>> p;
    for (float s : startSteps)
        p.push_back ({ s / steps, widthSteps / steps });
    return pulseN (p, closed, open);
}

// a pseudo-random but always identical pattern of `count` pulses on a grid of `steps`
inline NodeList randomPulses (unsigned seed, int steps, int count, float widthSteps, float closed = 0.0f)
{
    std::vector<int> pool;
    for (int i = 0; i < steps; ++i) pool.push_back (i);
    unsigned s = seed * 2654435761u + 12345u;
    for (int i = steps - 1; i > 0; --i)
    {
        s = s * 1664525u + 1013904223u;
        std::swap (pool[(size_t) i], pool[(size_t) ((s >> 8) % (unsigned) (i + 1))]);
    }
    std::vector<int> chosen (pool.begin(), pool.begin() + count);
    std::sort (chosen.begin(), chosen.end());
    std::vector<float> starts;
    for (int c : chosen) starts.push_back ((float) c);
    return pulseS (starts, (float) steps, widthSteps, closed);
}

// one smooth up-and-down wave (like a sine tremolo): starts at `hi`, dips to `lo`, comes back
inline NodeList sineS (float lo, float hi)
{
    const float shape[9][2] = { { 0.0f, 1.0f }, { 0.1f, 0.905f }, { 0.25f, 0.5f }, { 0.4f, 0.095f }, { 0.5f, 0.0f },
                                { 0.6f, 0.095f }, { 0.75f, 0.5f }, { 0.9f, 0.905f }, { 1.0f, 1.0f } };
    NodeList n;
    for (const auto& s : shape)
        n.push_back ({ s[0], lo + (hi - lo) * s[1], 0.0f });
    return n;
}


// the starting curve of each effect module (curve height meaning depends on the effect)
inline NodeList defaultNodes (int type)
{
    switch (type)
    {
        case fxFilter:     return pumpS (0.10f, 0.60f, 0.15f);                                   // closes on the hit, opens again
        case fxPan:        return { { 0.0f, 0.5f, 0.0f }, { 0.25f, 1.0f, 0.0f }, { 0.75f, 0.0f, 0.0f }, { 1.0f, 0.5f, 0.0f } };
        case fxWidth:      return { { 0.0f, 0.5f, 0.0f }, { 0.30f, 1.0f, 0.0f }, { 1.0f, 0.5f, 0.0f } };
        case fxDistortion: return { { 0.0f, 1.0f, 0.0f }, { 0.50f, 0.0f, 0.10f }, { 1.0f, 0.0f, 0.0f } };
        case fxPitch:      return { { 0.0f, 0.75f, 0.0f }, { 0.35f, 0.5f, 0.10f }, { 1.0f, 0.5f, 0.0f } };   // dives down after the hit
        default:           return holdS (0.0f, 0.08f, 0.55f, 0.15f);
    }
}

//------------------------------------------------------------------------------
inline const std::vector<GenreDef>& genres()
{
    static const std::vector<GenreDef> all = []
    {
        std::vector<GenreDef> g;

        auto genre = [&g] (const char* name, unsigned colour) -> GenreDef&
        {
            g.push_back ({ name, colour, {} });
            return g.back();
        };
        auto add = [] (GenreDef& gd, const char* name, int length, NodeList nodes)
        {
            gd.presets.push_back ({ name, length, std::move (nodes), "" });
        };
        auto addIn = [] (GenreDef& gd, const char* folder, const char* name, int length, NodeList nodes)
        {
            gd.presets.push_back ({ name, length, std::move (nodes), folder });
        };

        // Length indexes: 0 = 2 bars, 1 = 1 bar, 2 = 1/2, 3 = 1/4, 4 = 1/8, 5 = 1/16, 6 = 1/32, 8 = 1/8 triplet

        {   // ------------------------------------------------------------------ red
            auto& d = genre ("Hardstyle", 0xffff3b3b);
            add (d, "Hard Hit",           3, holdS (0.00f, 0.08f, 0.55f, 0.15f));
            add (d, "Klaplong Rumble",    3, holdS (0.02f, 0.16f, 0.82f, -0.08f));
            add (d, "Rolling 16ths",      3, tile (holdS (0.05f, 0.12f, 0.80f, 0.12f), 4));
            add (d, "Euphoric Pump",      3, { { 0.0f, 0.18f, -0.12f }, { 0.70f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Screech Space",      3, pumpS (0.35f, 0.45f, 0.10f));
            add (d, "Double Kick Gap",    3, tile (holdS (0.0f, 0.10f, 0.60f, 0.15f), 2));
            add (d, "Tight Reverse Bass", 3, tile (holdS (0.0f, 0.06f, 0.50f, 0.10f), 3));
            add (d, "Euphoric Long",      3, { { 0.0f, 0.10f, -0.10f }, { 0.90f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Lead Pump Soft",     3, pumpS (0.50f, 0.60f, 0.15f));
            add (d, "Kick and Rumble",    3, { { 0.0f, 0.0f, 0.0f }, { 0.10f, 0.0f, 0.0f }, { 0.22f, 0.45f, 0.10f }, { 0.55f, 0.20f, 0.0f }, { 0.85f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
        }
        {   // ------------------------------------------------------------------ orange
            auto& d = genre ("Rawstyle", 0xffff8a1f);
            add (d, "Raw Stomp",          3, holdS (0.0f, 0.12f, 0.78f, -0.12f));
            add (d, "Gritty Gate",        3, gateS (0.45f));
            add (d, "Punch and Tail",     3, { { 0.0f, 0.0f, 0.0f }, { 0.10f, 0.0f, 0.0f }, { 0.30f, 0.40f, 0.10f }, { 0.60f, 0.40f, 0.0f }, { 0.85f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Raw Roll",           3, tile (holdS (0.05f, 0.10f, 0.70f, 0.10f), 2));
            add (d, "Bounce Back",        3, { { 0.0f, 0.0f, 0.0f }, { 0.40f, 1.0f, 0.15f }, { 0.55f, 0.70f, 0.0f }, { 0.80f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Distorted Pocket",   3, { { 0.0f, 0.12f, 0.20f }, { 0.35f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Raw Kick Hold",      3, holdS (0.0f, 0.20f, 0.75f, -0.10f));
            add (d, "Raw Offbeat Gap",    3, { { 0.0f, 0.0f, 0.0f }, { 0.30f, 1.0f, 0.15f }, { 0.50f, 0.40f, 0.0f }, { 0.70f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Dirty Triplets",     3, tile (holdS (0.05f, 0.08f, 0.60f, 0.10f), 3));
        }
        {   // ------------------------------------------------------------------ yellow
            auto& d = genre ("Hardcore / Gabber", 0xffffd21f);
            add (d, "Gabber Hammer",      3, holdS (0.0f, 0.15f, 0.40f, 0.20f));
            add (d, "Stomp Gate",         3, gateS (0.30f));
            add (d, "Rolling Gabber",     3, tile (holdS (0.05f, 0.08f, 0.45f, 0.15f), 2));
            add (d, "Early Rave Bounce",  3, { { 0.0f, 0.0f, 0.0f }, { 0.25f, 1.0f, 0.20f }, { 0.45f, 0.55f, 0.0f }, { 0.65f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Terror Duck",        3, { { 0.0f, 0.0f, 0.0f }, { 0.50f, 0.0f, 0.0f }, { 0.70f, 1.0f, -0.10f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Frenchcore Slam",    3, holdS (0.0f, 0.20f, 0.30f, 0.0f));
            add (d, "Uptempo Chop",       3, tile (gateS (0.55f, 0.1f), 2));
        }
        {   // ------------------------------------------------------------------ lime
            auto& d = genre ("Big Room / EDM", 0xffa6e22e);
            add (d, "Big Room Classic",   3, holdS (0.0f, 0.10f, 0.60f, 0.10f));
            add (d, "Festival Pump",      3, { { 0.0f, 0.0f, -0.18f }, { 0.75f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Fast Drop",          3, pumpS (0.0f, 0.30f, 0.25f));
            add (d, "Anthem Breath",      2, { { 0.0f, 0.10f, -0.15f }, { 0.90f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Offbeat Pad Duck",   3, pumpS (0.45f, 0.35f, 0.15f));
            add (d, "Build-up 8ths",      3, tile (pumpS (0.0f, 0.50f, 0.10f), 2));
            add (d, "Drop Slam",          3, holdS (0.0f, 0.18f, 0.55f, 0.0f));
            add (d, "Pluck Chop 16ths",   3, tile (gateS (0.45f, 0.15f), 4));
            add (d, "Pre-drop Swell",     1, { { 0.0f, 0.10f, -0.25f }, { 0.97f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
        }
        {   // ------------------------------------------------------------------ green
            auto& d = genre ("House", 0xff2fe07a);
            add (d, "Classic House",      3, pumpS (0.25f, 0.60f, 0.20f));
            add (d, "Groove Bounce",      3, { { 0.0f, 0.20f, 0.0f }, { 0.35f, 0.90f, 0.15f }, { 0.50f, 0.75f, 0.0f }, { 0.75f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Deep House Breath",  3, pumpS (0.50f, 0.80f, -0.10f));
            add (d, "Funky Swing",        3, { { 0.0f, 0.0f, 0.0f }, { 0.35f, 1.0f, 0.15f }, { 0.50f, 0.50f, 0.0f }, { 0.85f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Disco 8ths",         3, tile (pumpS (0.40f, 0.70f, 0.20f), 2));
            add (d, "Piano Stab Space",   3, pumpS (0.30f, 0.35f, 0.15f));
            add (d, "French Touch Pump",  3, holdS (0.05f, 0.10f, 0.75f, -0.15f));
            add (d, "Garage Skip",        3, { { 0.0f, 0.10f, 0.0f }, { 0.30f, 1.0f, 0.15f }, { 0.52f, 0.30f, 0.0f }, { 0.75f, 1.0f, 0.10f }, { 1.0f, 1.0f, 0.0f } });
        }
        {   // ------------------------------------------------------------------ teal
            auto& d = genre ("Tech House", 0xff1fd1b8);
            add (d, "Tight Tech",         3, holdS (0.0f, 0.05f, 0.40f, 0.15f));
            add (d, "Rolling Tech",       3, tile (pumpS (0.10f, 0.60f, 0.10f), 2));
            add (d, "Offbeat Gap",        3, { { 0.0f, 0.0f, 0.0f }, { 0.35f, 1.0f, 0.20f }, { 0.50f, 0.60f, 0.0f }, { 0.70f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Percussive Chop",    3, tile (gateS (0.40f, 0.30f), 4));
            add (d, "Minimal Click",      3, pumpS (0.30f, 0.12f, 0.0f));
            add (d, "Swung 16ths",        3, { { 0.0f, 0.10f, 0.0f }, { 0.18f, 1.0f, 0.0f }, { 0.30f, 0.40f, 0.0f }, { 0.45f, 1.0f, 0.0f }, { 0.60f, 0.60f, 0.0f }, { 0.75f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Shuffle Roll",       3, { { 0.0f, 0.0f, 0.0f }, { 0.22f, 1.0f, 0.10f }, { 0.36f, 0.35f, 0.0f }, { 0.58f, 1.0f, 0.10f }, { 0.68f, 0.55f, 0.0f }, { 0.85f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Bassline Breathe",   3, pumpS (0.55f, 0.55f, 0.10f));
        }
        {   // ------------------------------------------------------------------ cyan
            auto& d = genre ("Future Bass", 0xff2fc8ff);
            add (d, "Wide Wobble",        2, { { 0.0f, 0.10f, 0.0f }, { 0.30f, 1.0f, 0.20f }, { 0.50f, 0.25f, 0.0f }, { 0.80f, 1.0f, 0.15f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Supersaw Swell",     1, { { 0.0f, 0.0f, -0.25f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Chop Gate 8ths",     3, tile (gateS (0.50f), 2));
            add (d, "Half-time Bounce",   2, { { 0.0f, 0.0f, 0.0f }, { 0.25f, 1.0f, 0.20f }, { 0.40f, 0.55f, 0.0f }, { 0.70f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Vocal Chop Stutter", 3, tile (gateS (0.45f, 0.10f), 4));
            add (d, "Pluck Pump",         3, pumpS (0.15f, 0.45f, 0.20f));
            add (d, "Chord Pulse",        3, pumpS (0.05f, 0.30f, 0.0f));
            add (d, "Kawaii Bounce",      3, { { 0.0f, 0.25f, 0.0f }, { 0.20f, 1.0f, 0.15f }, { 0.35f, 0.45f, 0.0f }, { 0.55f, 1.0f, 0.15f }, { 0.70f, 0.60f, 0.0f }, { 0.90f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
        }
        {   // ------------------------------------------------------------------ blue
            auto& d = genre ("Trap / Hip-hop", 0xff3d8bff);
            add (d, "808 Duck",           2, holdS (0.0f, 0.08f, 0.60f, -0.10f));
            add (d, "Hi-hat Roll Duck",   3, tile (pumpS (0.40f, 0.60f, 0.0f), 4));
            add (d, "Boom Bap Bounce",    2, { { 0.0f, 0.10f, 0.0f }, { 0.30f, 1.0f, 0.20f }, { 0.55f, 0.60f, 0.0f }, { 0.75f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Slap Back",          3, { { 0.0f, 0.0f, 0.0f }, { 0.15f, 0.70f, 0.0f }, { 0.20f, 0.0f, 0.0f }, { 0.60f, 1.0f, 0.10f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Triplet Flow",       3, tile (pumpS (0.35f, 0.60f, 0.10f), 3));
            add (d, "Sub Bass Hold",      2, holdS (0.0f, 0.15f, 0.70f, 0.0f));
            add (d, "Dark Pad Duck",      3, pumpS (0.40f, 0.70f, -0.10f));
            add (d, "Chopped and Screwed", 1, { { 0.0f, 0.0f, 0.0f }, { 0.20f, 0.0f, 0.0f }, { 0.35f, 1.0f, 0.0f }, { 0.50f, 1.0f, 0.0f }, { 0.52f, 0.30f, 0.0f }, { 0.75f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
        }
        {   // ------------------------------------------------------------------ indigo
            auto& d = genre ("Techno", 0xff5b5bff);
            add (d, "Driving Techno",     3, pumpS (0.15f, 0.50f, 0.10f));
            add (d, "Rumble Gap",         3, holdS (0.05f, 0.25f, 0.65f, -0.10f));
            add (d, "Warehouse Pulse",    3, tile (pumpS (0.30f, 0.45f, 0.10f), 2));
            add (d, "Hypnotic Wave",      3, { { 0.0f, 1.0f, 0.0f }, { 0.15f, 0.90f, 0.0f }, { 0.35f, 0.20f, 0.0f }, { 0.50f, 0.10f, 0.0f }, { 0.65f, 0.20f, 0.0f }, { 0.85f, 0.90f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Offbeat Hat Gate",   3, { { 0.0f, 1.0f, 0.0f }, { 0.45f, 1.0f, 0.0f }, { 0.50f, 0.20f, 0.0f }, { 0.95f, 0.20f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Industrial Stomp",   3, holdS (0.0f, 0.14f, 0.50f, 0.0f));
            add (d, "Berlin Rumble Long", 3, holdS (0.10f, 0.35f, 0.80f, -0.10f));
            add (d, "Minimal Tick",       3, pumpS (0.60f, 0.15f, 0.0f));
        }
        {   // ------------------------------------------------------------------ violet
            auto& d = genre ("Trance / Psytrance", 0xff9b59ff);
            add (d, "Uplifting Pump",     3, pumpS (0.30f, 0.55f, 0.15f));
            add (d, "Trance Gate 16ths",  3, tile (gateS (0.40f), 4));
            add (d, "Psy Gap",            3, holdS (0.0f, 0.20f, 0.40f, 0.10f));
            add (d, "Pad Swell",          1, { { 0.0f, 0.05f, -0.20f }, { 0.95f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Rolling Psy Bass",   3, tile (pumpS (0.20f, 0.60f, 0.10f), 3));
            add (d, "Progressive Breath", 3, pumpS (0.45f, 0.70f, 0.0f));
            add (d, "Goa Roll",           3, tile (holdS (0.10f, 0.06f, 0.55f, 0.10f), 3));
            add (d, "Gate Echo Fade",     3, { { 0.0f, 0.0f, 0.0f }, { 0.20f, 0.0f, 0.0f }, { 0.22f, 1.0f, 0.0f }, { 0.40f, 1.0f, 0.0f }, { 0.42f, 0.0f, 0.0f }, { 0.55f, 0.0f, 0.0f }, { 0.57f, 0.7f, 0.0f }, { 0.70f, 0.7f, 0.0f }, { 0.72f, 0.0f, 0.0f }, { 0.80f, 0.0f, 0.0f }, { 0.82f, 0.4f, 0.0f }, { 1.0f, 0.4f, 0.0f } });
        }
        {   // ------------------------------------------------------------------ purple
            auto& d = genre ("Drum and Bass", 0xffcf4dff);
            add (d, "2-Step Duck",        1, { { 0.0f, 0.0f, 0.0f }, { 0.10f, 1.0f, 0.20f }, { 0.375f, 1.0f, 0.0f }, { 0.38f, 0.0f, 0.0f }, { 0.50f, 1.0f, 0.20f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Neuro Pump",         3, pumpS (0.20f, 0.40f, 0.15f));
            add (d, "Liquid Breath",      3, pumpS (0.55f, 0.50f, 0.10f));
            add (d, "Amen Chop",          1, { { 0.0f, 0.0f, 0.0f }, { 0.08f, 1.0f, 0.20f }, { 0.20f, 1.0f, 0.0f }, { 0.21f, 0.20f, 0.0f }, { 0.30f, 1.0f, 0.20f }, { 0.55f, 1.0f, 0.0f }, { 0.56f, 0.10f, 0.0f }, { 0.66f, 1.0f, 0.20f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Rollers 8ths",       3, tile (pumpS (0.15f, 0.55f, 0.15f), 2));
            add (d, "Jump Up Slam",       1, { { 0.0f, 0.0f, 0.0f }, { 0.12f, 0.0f, 0.0f }, { 0.22f, 1.0f, 0.15f }, { 0.375f, 1.0f, 0.0f }, { 0.385f, 0.0f, 0.0f }, { 0.45f, 0.0f, 0.0f }, { 0.55f, 1.0f, 0.15f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Halftime DnB Pulse", 1, pumpS (0.10f, 0.40f, 0.15f));
        }
        {   // ------------------------------------------------------------------ magenta
            auto& d = genre ("Dubstep / Riddim", 0xffff3dcb);
            add (d, "Half-time Hit",      1, { { 0.0f, 0.0f, 0.0f }, { 0.10f, 0.0f, 0.0f }, { 0.25f, 1.0f, 0.15f }, { 0.50f, 1.0f, 0.0f }, { 0.51f, 0.40f, 0.0f }, { 0.62f, 1.0f, 0.15f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Riddim Chop 8ths",   3, tile (gateS (0.50f), 2));
            add (d, "Growl Gate 16ths",   3, tile (gateS (0.50f, 0.05f), 4));
            add (d, "Brostep Pump",       3, holdS (0.0f, 0.10f, 0.50f, 0.20f));
            add (d, "Triplet Wub",        8, { { 0.0f, 1.0f, 0.0f }, { 0.20f, 0.90f, 0.0f }, { 0.40f, 0.15f, 0.0f }, { 0.50f, 0.05f, 0.0f }, { 0.60f, 0.15f, 0.0f }, { 0.80f, 0.90f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Skank Gate",         3, pulseS ({ 1.0f }, 4.0f, 1.0f));
            add (d, "Wobble Slow",        2, sineS (0.05f, 1.0f));
        }
        {   // ------------------------------------------------------------------ rose
            auto& d = genre ("Chill / Lo-fi", 0xffff9ab0);
            add (d, "Gentle Breath",      3, pumpS (0.60f, 0.80f, -0.10f));
            add (d, "Tape Wobble",        1, { { 0.0f, 1.0f, 0.0f }, { 0.20f, 0.95f, 0.0f }, { 0.50f, 0.70f, 0.0f }, { 0.80f, 0.95f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Dreamy Duck",        2, pumpS (0.45f, 0.90f, -0.15f));
            add (d, "Sunday Sway",        1, { { 0.0f, 1.0f, 0.0f }, { 0.12f, 0.90f, 0.0f }, { 0.25f, 0.55f, 0.0f }, { 0.40f, 0.90f, 0.0f }, { 0.55f, 1.0f, 0.0f }, { 0.65f, 0.80f, 0.0f }, { 0.75f, 0.50f, 0.0f }, { 0.90f, 0.85f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Pad Bloom",          0, { { 0.0f, 0.20f, -0.20f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Vinyl Pulse",        3, sineS (0.65f, 1.0f));
            add (d, "Slow Tide",          1, sineS (0.30f, 1.0f));
            add (d, "Soft Kick Pocket",   3, pumpS (0.75f, 0.45f, 0.10f));
        }
        {   // ------------------------------------------------------------------ pink
            auto& d = genre ("Pop", 0xffff5c8a);
            add (d, "Radio Pump",         3, pumpS (0.35f, 0.55f, 0.20f));
            add (d, "Subtle Glue",        3, pumpS (0.70f, 0.40f, 0.20f));
            add (d, "Vocal Bed Duck",     3, pumpS (0.50f, 0.50f, 0.20f));
            add (d, "Dance Pop Bounce",   3, { { 0.0f, 0.20f, 0.0f }, { 0.40f, 1.0f, 0.20f }, { 0.55f, 0.75f, 0.0f }, { 0.80f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Chorus Lift",        1, { { 0.0f, 0.50f, -0.15f }, { 0.95f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Synth Pop Pulse",    4, pumpS (0.30f, 0.60f, 0.15f));
            add (d, "Verse Tuck",         3, pumpS (0.80f, 0.50f, 0.0f));
            add (d, "Last Chorus Pump",   3, holdS (0.15f, 0.10f, 0.60f, 0.15f));
        }
        {   // ------------------------------------------------------------------ lilac
            auto& d = genre ("Creative / Weird", 0xffc9d6ff);
            add (d, "Stutter 1/16",       5, gateS (0.50f));
            add (d, "Reverse Swell",      3, { { 0.0f, 0.0f, -0.25f }, { 0.92f, 1.0f, 0.0f }, { 0.94f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });
            add (d, "Sawtooth Down",      3, { { 0.0f, 1.0f, 0.0f }, { 0.98f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });
            add (d, "Random Steps",       3, { { 0.0f, 0.20f, 0.0f }, { 0.12f, 0.20f, 0.0f }, { 0.14f, 0.80f, 0.0f }, { 0.30f, 0.80f, 0.0f }, { 0.32f, 0.10f, 0.0f }, { 0.50f, 0.10f, 0.0f }, { 0.52f, 0.60f, 0.0f }, { 0.70f, 0.60f, 0.0f }, { 0.72f, 0.30f, 0.0f }, { 0.85f, 0.30f, 0.0f }, { 0.87f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Double Dip",         3, { { 0.0f, 0.10f, 0.0f }, { 0.20f, 1.0f, 0.15f }, { 0.50f, 0.30f, 0.0f }, { 0.75f, 1.0f, 0.15f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Triangle Tremolo",   6, { { 0.0f, 1.0f, 0.0f }, { 0.50f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Heartbeat",          3, { { 0.0f, 0.0f, 0.0f }, { 0.12f, 1.0f, 0.10f }, { 0.22f, 0.40f, 0.0f }, { 0.34f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Breathing Machine",  1, sineS (0.20f, 1.0f));
            add (d, "Broken Elevator",    3, { { 0.0f, 1.0f, 0.0f }, { 0.30f, 1.0f, 0.0f }, { 0.50f, 0.0f, 0.0f }, { 0.60f, 0.0f, 0.0f }, { 0.62f, 0.8f, 0.0f }, { 0.75f, 0.8f, 0.0f }, { 0.77f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });
        }
        {   // ------------------------------------------------------------------ amber
            // Gate, stutter and glitch patterns in the spirit of volume-envelope tools like Gross Beat
            auto& d = genre ("Gater / Stutter", 0xffffb340);

            addIn (d, "Gates", "Gate 50%",              3, gateS (0.50f));
            addIn (d, "Gates", "Gate 25% open",         3, pulseN ({ { 0.0f, 0.25f } }));
            addIn (d, "Gates", "Gate 75% open",         3, pulseN ({ { 0.0f, 0.75f } }));
            addIn (d, "Gates", "Gate Offbeat",          3, pulseN ({ { 0.5f, 0.45f } }));
            addIn (d, "Gates", "Gate Soft (half level)", 3, pulseN ({ { 0.0f, 0.5f } }, 0.4f));
            addIn (d, "Gates", "Gate Triplets",         3, tile (pulseN ({ { 0.0f, 0.5f } }), 3));
            addIn (d, "Gates", "Gate Dotted 3-3-2",     1, pulseS ({ 0, 3, 6 }, 8.0f, 1.4f));
            addIn (d, "Gates", "Gate Four Chops",       1, pulseS ({ 0, 4, 8, 12 }, 16.0f, 2.0f));

            addIn (d, "Stutters", "Ratchet Down",       3, pulseN ({ { 0.0f, 0.30f }, { 0.40f, 0.20f }, { 0.70f, 0.12f }, { 0.86f, 0.07f }, { 0.95f, 0.04f } }));
            addIn (d, "Stutters", "Ratchet Up",         3, pulseN ({ { 0.0f, 0.04f }, { 0.09f, 0.07f }, { 0.20f, 0.12f }, { 0.36f, 0.20f }, { 0.64f, 0.30f } }));
            addIn (d, "Stutters", "Repeat 6",           3, tile (pulseN ({ { 0.0f, 0.5f } }), 6));
            addIn (d, "Stutters", "Glitch Repeat",      3, pulseN ({ { 0.0f, 0.10f }, { 0.16f, 0.10f }, { 0.32f, 0.05f }, { 0.42f, 0.05f }, { 0.52f, 0.25f }, { 0.85f, 0.05f } }));
            addIn (d, "Stutters", "Stutter Fade Out",   3, { { 0.0f, 1.0f, 0.0f }, { 0.18f, 1.0f, 0.0f }, { 0.20f, 0.0f, 0.0f }, { 0.30f, 0.0f, 0.0f }, { 0.32f, 0.75f, 0.0f }, { 0.45f, 0.75f, 0.0f }, { 0.47f, 0.0f, 0.0f }, { 0.58f, 0.0f, 0.0f }, { 0.60f, 0.5f, 0.0f }, { 0.70f, 0.5f, 0.0f }, { 0.72f, 0.0f, 0.0f }, { 0.82f, 0.0f, 0.0f }, { 0.84f, 0.25f, 0.0f }, { 0.92f, 0.25f, 0.0f }, { 0.94f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });
            addIn (d, "Stutters", "Scratch Cut",        3, pulseN ({ { 0.05f, 0.18f }, { 0.30f, 0.10f }, { 0.50f, 0.28f }, { 0.88f, 0.06f } }));

            addIn (d, "Tremolo", "Sine Soft",           5, sineS (0.50f, 1.0f));
            addIn (d, "Tremolo", "Sine Deep",           5, sineS (0.0f, 1.0f));
            addIn (d, "Tremolo", "Triangle",            5, { { 0.0f, 1.0f, 0.0f }, { 0.50f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            addIn (d, "Tremolo", "Saw Up",              5, { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            addIn (d, "Tremolo", "Saw Down",            5, { { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });
            addIn (d, "Tremolo", "Square",              5, gateS (0.50f));
            addIn (d, "Tremolo", "Pulse 25%",           5, pulseN ({ { 0.0f, 0.25f } }));
            addIn (d, "Tremolo", "Slow Sine Pad",       1, sineS (0.20f, 1.0f));

            addIn (d, "Fades", "Fade In",               1, { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            addIn (d, "Fades", "Fade In (slow start)",  1, { { 0.0f, 0.0f, -0.28f }, { 1.0f, 1.0f, 0.0f } });
            addIn (d, "Fades", "Fade In (fast start)",  1, { { 0.0f, 0.0f, 0.28f }, { 1.0f, 1.0f, 0.0f } });
            addIn (d, "Fades", "Fade Out",              1, { { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });
            addIn (d, "Fades", "Fade Out (late)",       1, { { 0.0f, 1.0f, 0.28f }, { 1.0f, 0.0f, 0.0f } });
            addIn (d, "Fades", "Fade Out (early)",      1, { { 0.0f, 1.0f, -0.28f }, { 1.0f, 0.0f, 0.0f } });
            addIn (d, "Fades", "Fade In and Out",       0, { { 0.0f, 0.0f, -0.15f }, { 0.50f, 1.0f, 0.15f }, { 1.0f, 0.0f, 0.0f } });
            addIn (d, "Fades", "Staircase Up",          1, { { 0.0f, 0.0f, 0.0f }, { 0.24f, 0.0f, 0.0f }, { 0.26f, 0.33f, 0.0f }, { 0.49f, 0.33f, 0.0f }, { 0.51f, 0.66f, 0.0f }, { 0.74f, 0.66f, 0.0f }, { 0.76f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            addIn (d, "Fades", "Staircase Down",        1, { { 0.0f, 1.0f, 0.0f }, { 0.24f, 1.0f, 0.0f }, { 0.26f, 0.66f, 0.0f }, { 0.49f, 0.66f, 0.0f }, { 0.51f, 0.33f, 0.0f }, { 0.74f, 0.33f, 0.0f }, { 0.76f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });

            addIn (d, "Rhythms", "Offbeat Eighths",     3, pulseS ({ 1.0f }, 2.0f, 0.9f));
            addIn (d, "Rhythms", "Dotted Eighth Echo",  1, pulseS ({ 0, 3, 6, 9, 12 }, 16.0f, 1.5f));
            addIn (d, "Rhythms", "Syncopated 1",        1, pulseS ({ 0, 3, 5, 8, 11, 13 }, 16.0f, 1.5f));
            addIn (d, "Rhythms", "Syncopated 2",        1, pulseS ({ 2, 4, 7, 10, 14 }, 16.0f, 1.5f));
            addIn (d, "Rhythms", "Swing 16ths",         3, pulseN ({ { 0.0f, 0.18f }, { 0.30f, 0.18f }, { 0.50f, 0.18f }, { 0.80f, 0.18f } }));
            addIn (d, "Rhythms", "Clave",               1, pulseS ({ 0, 3, 6, 10, 12 }, 16.0f, 1.6f));
            addIn (d, "Rhythms", "Backbeat Only",       1, pulseS ({ 4, 12 }, 16.0f, 2.0f));
            addIn (d, "Rhythms", "Hat Pattern",         1, pulseS ({ 0, 2, 4, 6, 8, 10, 12 }, 16.0f, 0.8f));

            addIn (d, "Random", "Random A",             1, randomPulses (1, 16, 6, 1.4f));
            addIn (d, "Random", "Random B",             1, randomPulses (2, 16, 7, 1.2f));
            addIn (d, "Random", "Random C",             1, randomPulses (3, 16, 5, 1.8f));
            addIn (d, "Random", "Random D",             1, randomPulses (4, 16, 6, 1.0f));
            addIn (d, "Random", "Random E (soft)",      3, randomPulses (5, 8, 5, 0.9f, 0.35f));
            addIn (d, "Random", "Random F (soft)",      1, randomPulses (6, 16, 7, 1.3f, 0.30f));
        }
        {   // ------------------------------------------------------------------ silver
            auto& d = genre ("Utility / Basic Shapes", 0xffaab4c8);
            add (d, "Flat (no ducking)",  3, { { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            for (int p = 0; p < kNumPresets; ++p)
            {
                std::vector<float> ys (257);
                for (size_t i = 0; i < ys.size(); ++i)
                    ys[i] = presetValue (p, (float) i / 256.0f);
                add (d, presetName (p), 3, simplify (ys, 0.012f, 24));
            }
        }

        return g;
    }();
    return all;
}

inline int numGenres() { return (int) genres().size(); }

} // namespace pump
