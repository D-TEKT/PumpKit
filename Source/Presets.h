// Presets.h
// The preset library: groups by genre/vibe, each with its own colour.
// Every preset is a list of points {time, volume, bend} for one cycle, plus the
// Length (note value) it was drawn for.
#pragma once

#include "PumpEngine.h"
#include <string>
#include <vector>

namespace pump
{
using NodeList = std::vector<Node>;

struct PresetDef
{
    std::string name;
    int length = 3;     // index into the Length list (3 = 1/4)
    NodeList nodes;
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
            gd.presets.push_back ({ name, length, std::move (nodes) });
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
        }
        {   // ------------------------------------------------------------------ orange
            auto& d = genre ("Rawstyle", 0xffff8a1f);
            add (d, "Raw Stomp",          3, holdS (0.0f, 0.12f, 0.78f, -0.12f));
            add (d, "Gritty Gate",        3, gateS (0.45f));
            add (d, "Punch and Tail",     3, { { 0.0f, 0.0f, 0.0f }, { 0.10f, 0.0f, 0.0f }, { 0.30f, 0.40f, 0.10f }, { 0.60f, 0.40f, 0.0f }, { 0.85f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Raw Roll",           3, tile (holdS (0.05f, 0.10f, 0.70f, 0.10f), 2));
            add (d, "Bounce Back",        3, { { 0.0f, 0.0f, 0.0f }, { 0.40f, 1.0f, 0.15f }, { 0.55f, 0.70f, 0.0f }, { 0.80f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Distorted Pocket",   3, { { 0.0f, 0.12f, 0.20f }, { 0.35f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
        }
        {   // ------------------------------------------------------------------ yellow
            auto& d = genre ("Hardcore / Gabber", 0xffffd21f);
            add (d, "Gabber Hammer",      3, holdS (0.0f, 0.15f, 0.40f, 0.20f));
            add (d, "Stomp Gate",         3, gateS (0.30f));
            add (d, "Rolling Gabber",     3, tile (holdS (0.05f, 0.08f, 0.45f, 0.15f), 2));
            add (d, "Early Rave Bounce",  3, { { 0.0f, 0.0f, 0.0f }, { 0.25f, 1.0f, 0.20f }, { 0.45f, 0.55f, 0.0f }, { 0.65f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Terror Duck",        3, { { 0.0f, 0.0f, 0.0f }, { 0.50f, 0.0f, 0.0f }, { 0.70f, 1.0f, -0.10f }, { 1.0f, 1.0f, 0.0f } });
        }
        {   // ------------------------------------------------------------------ lime
            auto& d = genre ("Big Room / EDM", 0xffa6e22e);
            add (d, "Big Room Classic",   3, holdS (0.0f, 0.10f, 0.60f, 0.10f));
            add (d, "Festival Pump",      3, { { 0.0f, 0.0f, -0.18f }, { 0.75f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Fast Drop",          3, pumpS (0.0f, 0.30f, 0.25f));
            add (d, "Anthem Breath",      2, { { 0.0f, 0.10f, -0.15f }, { 0.90f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Offbeat Pad Duck",   3, pumpS (0.45f, 0.35f, 0.15f));
            add (d, "Build-up 8ths",      3, tile (pumpS (0.0f, 0.50f, 0.10f), 2));
        }
        {   // ------------------------------------------------------------------ green
            auto& d = genre ("House", 0xff2fe07a);
            add (d, "Classic House",      3, pumpS (0.25f, 0.60f, 0.20f));
            add (d, "Groove Bounce",      3, { { 0.0f, 0.20f, 0.0f }, { 0.35f, 0.90f, 0.15f }, { 0.50f, 0.75f, 0.0f }, { 0.75f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Deep House Breath",  3, pumpS (0.50f, 0.80f, -0.10f));
            add (d, "Funky Swing",        3, { { 0.0f, 0.0f, 0.0f }, { 0.35f, 1.0f, 0.15f }, { 0.50f, 0.50f, 0.0f }, { 0.85f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Disco 8ths",         3, tile (pumpS (0.40f, 0.70f, 0.20f), 2));
        }
        {   // ------------------------------------------------------------------ teal
            auto& d = genre ("Tech House", 0xff1fd1b8);
            add (d, "Tight Tech",         3, holdS (0.0f, 0.05f, 0.40f, 0.15f));
            add (d, "Rolling Tech",       3, tile (pumpS (0.10f, 0.60f, 0.10f), 2));
            add (d, "Offbeat Gap",        3, { { 0.0f, 0.0f, 0.0f }, { 0.35f, 1.0f, 0.20f }, { 0.50f, 0.60f, 0.0f }, { 0.70f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Percussive Chop",    3, tile (gateS (0.40f, 0.30f), 4));
            add (d, "Minimal Click",      3, pumpS (0.30f, 0.12f, 0.0f));
            add (d, "Swung 16ths",        3, { { 0.0f, 0.10f, 0.0f }, { 0.18f, 1.0f, 0.0f }, { 0.30f, 0.40f, 0.0f }, { 0.45f, 1.0f, 0.0f }, { 0.60f, 0.60f, 0.0f }, { 0.75f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
        }
        {   // ------------------------------------------------------------------ cyan
            auto& d = genre ("Future Bass", 0xff2fc8ff);
            add (d, "Wide Wobble",        2, { { 0.0f, 0.10f, 0.0f }, { 0.30f, 1.0f, 0.20f }, { 0.50f, 0.25f, 0.0f }, { 0.80f, 1.0f, 0.15f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Supersaw Swell",     1, { { 0.0f, 0.0f, -0.25f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Chop Gate 8ths",     3, tile (gateS (0.50f), 2));
            add (d, "Half-time Bounce",   2, { { 0.0f, 0.0f, 0.0f }, { 0.25f, 1.0f, 0.20f }, { 0.40f, 0.55f, 0.0f }, { 0.70f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Vocal Chop Stutter", 3, tile (gateS (0.45f, 0.10f), 4));
            add (d, "Pluck Pump",         3, pumpS (0.15f, 0.45f, 0.20f));
        }
        {   // ------------------------------------------------------------------ blue
            auto& d = genre ("Trap / Hip-hop", 0xff3d8bff);
            add (d, "808 Duck",           2, holdS (0.0f, 0.08f, 0.60f, -0.10f));
            add (d, "Hi-hat Roll Duck",   3, tile (pumpS (0.40f, 0.60f, 0.0f), 4));
            add (d, "Boom Bap Bounce",    2, { { 0.0f, 0.10f, 0.0f }, { 0.30f, 1.0f, 0.20f }, { 0.55f, 0.60f, 0.0f }, { 0.75f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Slap Back",          3, { { 0.0f, 0.0f, 0.0f }, { 0.15f, 0.70f, 0.0f }, { 0.20f, 0.0f, 0.0f }, { 0.60f, 1.0f, 0.10f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Triplet Flow",       3, tile (pumpS (0.35f, 0.60f, 0.10f), 3));
        }
        {   // ------------------------------------------------------------------ indigo
            auto& d = genre ("Techno", 0xff5b5bff);
            add (d, "Driving Techno",     3, pumpS (0.15f, 0.50f, 0.10f));
            add (d, "Rumble Gap",         3, holdS (0.05f, 0.25f, 0.65f, -0.10f));
            add (d, "Warehouse Pulse",    3, tile (pumpS (0.30f, 0.45f, 0.10f), 2));
            add (d, "Hypnotic Wave",      3, { { 0.0f, 1.0f, 0.0f }, { 0.15f, 0.90f, 0.0f }, { 0.35f, 0.20f, 0.0f }, { 0.50f, 0.10f, 0.0f }, { 0.65f, 0.20f, 0.0f }, { 0.85f, 0.90f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Offbeat Hat Gate",   3, { { 0.0f, 1.0f, 0.0f }, { 0.45f, 1.0f, 0.0f }, { 0.50f, 0.20f, 0.0f }, { 0.95f, 0.20f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
        }
        {   // ------------------------------------------------------------------ violet
            auto& d = genre ("Trance / Psytrance", 0xff9b59ff);
            add (d, "Uplifting Pump",     3, pumpS (0.30f, 0.55f, 0.15f));
            add (d, "Trance Gate 16ths",  3, tile (gateS (0.40f), 4));
            add (d, "Psy Gap",            3, holdS (0.0f, 0.20f, 0.40f, 0.10f));
            add (d, "Pad Swell",          1, { { 0.0f, 0.05f, -0.20f }, { 0.95f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Rolling Psy Bass",   3, tile (pumpS (0.20f, 0.60f, 0.10f), 3));
        }
        {   // ------------------------------------------------------------------ purple
            auto& d = genre ("Drum and Bass", 0xffcf4dff);
            add (d, "2-Step Duck",        1, { { 0.0f, 0.0f, 0.0f }, { 0.10f, 1.0f, 0.20f }, { 0.375f, 1.0f, 0.0f }, { 0.38f, 0.0f, 0.0f }, { 0.50f, 1.0f, 0.20f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Neuro Pump",         3, pumpS (0.20f, 0.40f, 0.15f));
            add (d, "Liquid Breath",      3, pumpS (0.55f, 0.50f, 0.10f));
            add (d, "Amen Chop",          1, { { 0.0f, 0.0f, 0.0f }, { 0.08f, 1.0f, 0.20f }, { 0.20f, 1.0f, 0.0f }, { 0.21f, 0.20f, 0.0f }, { 0.30f, 1.0f, 0.20f }, { 0.55f, 1.0f, 0.0f }, { 0.56f, 0.10f, 0.0f }, { 0.66f, 1.0f, 0.20f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Rollers 8ths",       3, tile (pumpS (0.15f, 0.55f, 0.15f), 2));
        }
        {   // ------------------------------------------------------------------ magenta
            auto& d = genre ("Dubstep / Riddim", 0xffff3dcb);
            add (d, "Half-time Hit",      1, { { 0.0f, 0.0f, 0.0f }, { 0.10f, 0.0f, 0.0f }, { 0.25f, 1.0f, 0.15f }, { 0.50f, 1.0f, 0.0f }, { 0.51f, 0.40f, 0.0f }, { 0.62f, 1.0f, 0.15f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Riddim Chop 8ths",   3, tile (gateS (0.50f), 2));
            add (d, "Growl Gate 16ths",   3, tile (gateS (0.50f, 0.05f), 4));
            add (d, "Brostep Pump",       3, holdS (0.0f, 0.10f, 0.50f, 0.20f));
            add (d, "Triplet Wub",        8, { { 0.0f, 1.0f, 0.0f }, { 0.20f, 0.90f, 0.0f }, { 0.40f, 0.15f, 0.0f }, { 0.50f, 0.05f, 0.0f }, { 0.60f, 0.15f, 0.0f }, { 0.80f, 0.90f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
        }
        {   // ------------------------------------------------------------------ rose
            auto& d = genre ("Chill / Lo-fi", 0xffff9ab0);
            add (d, "Gentle Breath",      3, pumpS (0.60f, 0.80f, -0.10f));
            add (d, "Tape Wobble",        1, { { 0.0f, 1.0f, 0.0f }, { 0.20f, 0.95f, 0.0f }, { 0.50f, 0.70f, 0.0f }, { 0.80f, 0.95f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Dreamy Duck",        2, pumpS (0.45f, 0.90f, -0.15f));
            add (d, "Sunday Sway",        1, { { 0.0f, 1.0f, 0.0f }, { 0.12f, 0.90f, 0.0f }, { 0.25f, 0.55f, 0.0f }, { 0.40f, 0.90f, 0.0f }, { 0.55f, 1.0f, 0.0f }, { 0.65f, 0.80f, 0.0f }, { 0.75f, 0.50f, 0.0f }, { 0.90f, 0.85f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Pad Bloom",          0, { { 0.0f, 0.20f, -0.20f }, { 1.0f, 1.0f, 0.0f } });
        }
        {   // ------------------------------------------------------------------ pink
            auto& d = genre ("Pop", 0xffff5c8a);
            add (d, "Radio Pump",         3, pumpS (0.35f, 0.55f, 0.20f));
            add (d, "Subtle Glue",        3, pumpS (0.70f, 0.40f, 0.20f));
            add (d, "Vocal Bed Duck",     3, pumpS (0.50f, 0.50f, 0.20f));
            add (d, "Dance Pop Bounce",   3, { { 0.0f, 0.20f, 0.0f }, { 0.40f, 1.0f, 0.20f }, { 0.55f, 0.75f, 0.0f }, { 0.80f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Chorus Lift",        1, { { 0.0f, 0.50f, -0.15f }, { 0.95f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
        }
        {   // ------------------------------------------------------------------ lilac
            auto& d = genre ("Creative / Weird", 0xffc9d6ff);
            add (d, "Stutter 1/16",       5, gateS (0.50f));
            add (d, "Reverse Swell",      3, { { 0.0f, 0.0f, -0.25f }, { 0.92f, 1.0f, 0.0f }, { 0.94f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });
            add (d, "Sawtooth Down",      3, { { 0.0f, 1.0f, 0.0f }, { 0.98f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });
            add (d, "Random Steps",       3, { { 0.0f, 0.20f, 0.0f }, { 0.12f, 0.20f, 0.0f }, { 0.14f, 0.80f, 0.0f }, { 0.30f, 0.80f, 0.0f }, { 0.32f, 0.10f, 0.0f }, { 0.50f, 0.10f, 0.0f }, { 0.52f, 0.60f, 0.0f }, { 0.70f, 0.60f, 0.0f }, { 0.72f, 0.30f, 0.0f }, { 0.85f, 0.30f, 0.0f }, { 0.87f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Double Dip",         3, { { 0.0f, 0.10f, 0.0f }, { 0.20f, 1.0f, 0.15f }, { 0.50f, 0.30f, 0.0f }, { 0.75f, 1.0f, 0.15f }, { 1.0f, 1.0f, 0.0f } });
            add (d, "Triangle Tremolo",   6, { { 0.0f, 1.0f, 0.0f }, { 0.50f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
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
