#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String msText (float v, int)        { return juce::String (v, 1) + " ms"; }
    juce::String pctText (float v, int)       { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String dbText (float v, int)        { return juce::String (v, 1) + " dB"; }
    juce::String hzText (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    }
    float parseNumber (const juce::String& t)
    {
        const float v = t.getFloatValue();
        return t.containsIgnoreCase ("k") ? v * 1000.0f : v;
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PumpKitProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
                                                        StringArray { "Sync", "MIDI", "Audio" }, 0));

    StringArray lengths;
    for (int i = 0; i < pump::kNumLengths; ++i)
        lengths.add (pump::lengthName (i));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "length", 1 }, "Length", lengths, 3));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "mix", 1 }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pctText)
                                       .withValueFromStringFunction (parseNumber)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "smooth", 1 }, "Smooth", NormalisableRange<float> (0.0f, 50.0f, 0.1f), 2.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (msText)
                                       .withValueFromStringFunction (parseNumber)));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bandSplit", 1 }, "Band Split", false));

    NormalisableRange<float> freqRange (20.0f, 5120.0f, 1.0f);
    freqRange.setSkewForCentre (320.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "splitFreq", 1 }, "Split Freq", freqRange, 150.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (hzText)
                                       .withValueFromStringFunction (parseNumber)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "threshold", 1 }, "Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -24.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (dbText)
                                       .withValueFromStringFunction (parseNumber)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "retrigger", 1 }, "Retrigger", NormalisableRange<float> (10.0f, 500.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (msText)
                                       .withValueFromStringFunction (parseNumber)));

    // Audio mode: listen to the sidechain input (chosen in FL's plugin settings), or to the sound on this track itself
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "scSource", 1 }, "Trigger From",
                                                        StringArray { "Sidechain", "This track" }, 0));

    // Speed: in note lengths (follows the song tempo) or in Hz (free speed, easy to automate)
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "rateMode", 1 }, "Rate Unit",
                                                        StringArray { "Beats", "Hz" }, 0));

    NormalisableRange<float> rateRange (pump::kMinHz, pump::kMaxHz, 0.01f);
    rateRange.setSkewForCentre (3.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "rateHz", 1 }, "Rate (Hz)", rateRange, 2.6f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " Hz"; })
                                       .withValueFromStringFunction (parseNumber)));

    // ---- effect modules (up to six), and how they are combined
    StringArray types;
    for (int t = 0; t < pump::kNumFxTypes; ++t)
        types.add (pump::fxName (t));

    for (int s = 0; s < pump::kMaxSlots; ++s)
    {
        const String n (s + 1);
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "m" + n + "Type", 1 }, "Module " + n + " Effect",
                                                            types, s == 0 ? pump::fxVolume : pump::fxOff));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "m" + n + "Depth", 1 }, "Module " + n + " Depth", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (pctText).withValueFromStringFunction (parseNumber)));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "m" + n + "On", 1 }, "Module " + n + " On", true));
        // the option of the effect (low-pass/high-pass, overdrive/fuzz..., pitch range) and its second knob (resonance, tone)
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "m" + n + "Style", 1 }, "Module " + n + " Style",
                                                            StringArray { "Option 1", "Option 2", "Option 3", "Option 4", "Option 5", "Option 6", "Option 7", "Option 8" }, 0));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "m" + n + "Param", 1 }, "Module " + n + " Extra", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (pctText).withValueFromStringFunction (parseNumber)));
    }

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "combine", 1 }, "Combine",
                                                        StringArray { "Chain", "Side by side" }, 0));

    // editor helpers (remembered with the project, not meant for automation)
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "gridDiv", 1 }, "Grid",
                                                        StringArray { "Off", "2", "4", "8", "16", "32", "3 (triplet)", "6 (triplet)", "12 (triplet)", "24 (triplet)" }, 2,
                                                        AudioParameterChoiceAttributes().withAutomatable (false)));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "magnet", 1 }, "Magnet", true,
                                                      AudioParameterBoolAttributes().withAutomatable (false)));

    return layout;
}

//==============================================================================
PumpKitProcessor::PumpKitProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                                // switched on from the start, so FL Studio offers it in the plugin's Processing page
                                .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PumpKitState", createLayout())
{
    pMode      = apvts.getRawParameterValue ("mode");
    pLength    = apvts.getRawParameterValue ("length");
    pMix       = apvts.getRawParameterValue ("mix");
    pSmooth    = apvts.getRawParameterValue ("smooth");
    pSplit     = apvts.getRawParameterValue ("bandSplit");
    pFreq      = apvts.getRawParameterValue ("splitFreq");
    pThreshold = apvts.getRawParameterValue ("threshold");
    pRetrigger = apvts.getRawParameterValue ("retrigger");
    pSource    = apvts.getRawParameterValue ("scSource");
    pRateMode  = apvts.getRawParameterValue ("rateMode");
    pRateHz    = apvts.getRawParameterValue ("rateHz");
    pCombine   = apvts.getRawParameterValue ("combine");
    for (int s = 0; s < pump::kMaxSlots; ++s)
    {
        const juce::String n (s + 1);
        pType[(size_t) s]  = apvts.getRawParameterValue ("m" + n + "Type");
        pDepth[(size_t) s] = apvts.getRawParameterValue ("m" + n + "Depth");
        pOn[(size_t) s]    = apvts.getRawParameterValue ("m" + n + "On");
        pStyle[(size_t) s] = apvts.getRawParameterValue ("m" + n + "Style");
        pParam[(size_t) s] = apvts.getRawParameterValue ("m" + n + "Param");
    }

    for (auto* arr : { &scopeIn, &scopeOut, &scopeSc })
        for (auto& v : *arr)
            v.store (0.0f, std::memory_order_relaxed);

    // start on the first Hardstyle preset
    for (int s = 0; s < pump::kMaxSlots; ++s)
        curves[(size_t) s].setNodes (s == 0 ? pump::genres()[0].presets[0].nodes : pump::defaultNodes (s == 0 ? pump::fxVolume : pump::fxOff));
    lastType.fill (-1);

    trigPos.reserve (2048);
}

bool PumpKitProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::stereo() && mainOut != juce::AudioChannelSet::mono())
        return false;
    if (layouts.getMainInputChannelSet() != mainOut)
        return false;

    const auto sc = layouts.getChannelSet (true, 1);
    if (! sc.isDisabled() && sc != juce::AudioChannelSet::stereo() && sc != juce::AudioChannelSet::mono())
        return false;

    return true;
}

void PumpKitProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;

    juce::dsp::ProcessSpec spec { sr, (juce::uint32) samplesPerBlock, 2 };
    for (auto& c : crossovers) { c.prepare (spec); c.reset(); }
    for (auto& f : fx) f.prepare (sr);
    lastType.fill (-1);
    lastCutoff = -1.0f;
    wasSplit = false;

    for (auto& sm : smoothers) sm.prepare (sr);
    onset.prepare (sr);
    oneShot.stop();

    mixSmooth.reset (sr, 0.02);
    mixSmooth.setCurrentAndTargetValue (pMix->load() * 0.01f);

    freePhase = 0.0;
    lastMode = -1;
    wasPlaying = false;
    hzPhase.prepare (sr, pRateHz->load());
}

//==============================================================================
void PumpKitProcessor::setPlain (const juce::String& id, float value)
{
    if (auto* prm = apvts.getParameter (id))
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

float PumpKitProcessor::getPlain (const juce::String& id) const
{
    return apvts.getRawParameterValue (id)->load();
}

int PumpKitProcessor::activeModules() const
{
    int n = 0;
    for (int s = 0; s < pump::kMaxSlots; ++s)
        if ((int) pType[(size_t) s]->load() != pump::fxOff)
            ++n;
    return n;
}

void PumpKitProcessor::copyModule (int from, int to)
{
    const juce::String f (from + 1), t (to + 1);
    setPlain ("m" + t + "Type",  getPlain ("m" + f + "Type"));
    setPlain ("m" + t + "Depth", getPlain ("m" + f + "Depth"));
    setPlain ("m" + t + "On",    getPlain ("m" + f + "On"));
    setPlain ("m" + t + "Style", getPlain ("m" + f + "Style"));
    setPlain ("m" + t + "Param", getPlain ("m" + f + "Param"));
    curves[(size_t) to].setNodes (curves[(size_t) from].getNodes());
}

int PumpKitProcessor::addModule (int type)
{
    for (int s = 0; s < pump::kMaxSlots; ++s)
    {
        if ((int) pType[(size_t) s]->load() == pump::fxOff)
        {
            const juce::String n (s + 1);
            setPlain ("m" + n + "Type", (float) type);
            setPlain ("m" + n + "Depth", 100.0f);
            setPlain ("m" + n + "On", 1.0f);
            setPlain ("m" + n + "Style", 0.0f);
            setPlain ("m" + n + "Param", pump::fxParamDefault (type));
            curves[(size_t) s].setNodes (pump::defaultNodes (type));
            selectedSlot.store (s);
            return s;
        }
    }
    return -1;
}

void PumpKitProcessor::removeModule (int slot)
{
    if (slot < 0 || slot >= pump::kMaxSlots || activeModules() <= 1)
        return;

    for (int k = slot; k + 1 < pump::kMaxSlots; ++k)
        copyModule (k + 1, k);

    const int last = pump::kMaxSlots - 1;
    setPlain ("m" + juce::String (last + 1) + "Type", (float) pump::fxOff);
    curves[(size_t) last].setNodes (pump::defaultNodes (pump::fxOff));

    selectedSlot.store (std::min (slot, std::max (0, activeModules() - 1)));
}

void PumpKitProcessor::moveModule (int slot, int delta)
{
    const int other = slot + delta;
    if (slot < 0 || other < 0 || slot >= pump::kMaxSlots || other >= pump::kMaxSlots)
        return;
    if ((int) pType[(size_t) slot]->load() == pump::fxOff || (int) pType[(size_t) other]->load() == pump::fxOff)
        return;

    const juce::String a (slot + 1);
    const float type = getPlain ("m" + a + "Type"), depth = getPlain ("m" + a + "Depth"), on = getPlain ("m" + a + "On"),
                style = getPlain ("m" + a + "Style"), param = getPlain ("m" + a + "Param");
    const auto nodes = curves[(size_t) slot].getNodes();

    copyModule (other, slot);
    const juce::String b (other + 1);
    setPlain ("m" + b + "Type", type);
    setPlain ("m" + b + "Depth", depth);
    setPlain ("m" + b + "On", on);
    setPlain ("m" + b + "Style", style);
    setPlain ("m" + b + "Param", param);
    curves[(size_t) other].setNodes (nodes);

    selectedSlot.store (other);
}

void PumpKitProcessor::startWith (int type)
{
    setPlain ("m1Type", (float) type);
    setPlain ("m1Depth", 100.0f);
    setPlain ("m1On", 1.0f);
    setPlain ("m1Style", 0.0f);
    setPlain ("m1Param", pump::fxParamDefault (type));
    curves[0].setNodes (pump::defaultNodes (type));
    selectedSlot.store (0);
    started.store (true);
}

//==============================================================================
void PumpKitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numCh = std::min ({ getMainBusNumInputChannels(), buffer.getNumChannels(), 2 });
    if (numCh <= 0 || numSamples <= 0)
    {
        midi.clear();
        return;
    }

    const int mode       = (int) pMode->load();
    const int lengthIdx  = (int) pLength->load();
    const bool split     = pSplit->load() > 0.5f;
    const bool ownSource = (int) pSource->load() == 1;
    const bool useHz     = pRateMode->load() > 0.5f;
    const bool chain     = pCombine->load() < 0.5f;

    if (useHz)
        hzPhase.setHz (pRateHz->load());

    mixSmooth.setTargetValue (pMix->load() * 0.01f);
    onset.setParams (pThreshold->load(), pRetrigger->load());

    // ---- the effect modules: which are active, and reset what changed
    int slotType[pump::kMaxSlots], slotStyle[pump::kMaxSlots];
    float slotDepth[pump::kMaxSlots], slotParam[pump::kMaxSlots];
    bool anyVolumeSplit = false;
    for (size_t s = 0; s < (size_t) pump::kMaxSlots; ++s)
    {
        int t = std::clamp ((int) pType[s]->load(), 0, pump::kNumFxTypes - 1);
        if (pOn[s]->load() < 0.5f)
            t = pump::fxOff;
        slotType[s] = t;
        slotDepth[s] = pDepth[s]->load() * 0.01f;
        slotStyle[s] = std::clamp ((int) pStyle[s]->load(), 0, std::max (0, pump::fxStyleCount (t) - 1));
        slotParam[s] = pParam[s]->load() * 0.01f;

        const int key = t * 16 + slotStyle[s];   // a different effect or option rests at a different value
        if (key != lastType[s])
        {
            lastType[s] = key;
            fx[s].reset();
            smoothers[s].reset (pump::fxNeutral (t, slotStyle[s]));
            crossovers[s].reset();
        }
        smoothers[s].setTimeMs (pSmooth->load());
        anyVolumeSplit = anyVolumeSplit || (t == pump::fxVolume && split);
    }

    if (mode != lastMode)
    {
        onset.reset();
        oneShot.stop();
        lastMode = mode;
    }

    // ---- sidechain input (second input bus), if the host connected one
    const float* sc[2] = { nullptr, nullptr };
    int scCh = 0;
    if (auto* scBus = getBus (true, 1))
    {
        if (scBus->isEnabled())
        {
            const int n = std::min (scBus->getNumberOfChannels(), 2);
            for (int c = 0; c < n; ++c)
            {
                const int idx = getChannelIndexInProcessBlockBuffer (true, 1, c);
                if (idx >= 0 && idx < buffer.getNumChannels())
                    sc[scCh++] = buffer.getReadPointer (idx);
            }
        }
    }

    // ---- host tempo / position
    double bpm = lastBpm, ppq = 0.0;
    bool playing = false, havePpq = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())
                bpm = *b;
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition())
            {
                ppq = *p;
                havePpq = true;
            }
        }
    }
    bpm = std::clamp (bpm, 20.0, 999.0);
    lastBpm = bpm;

    // with Hz speed the song starts the cycle over when playback starts
    if (useHz && playing && ! wasPlaying)
        hzPhase.restart();
    wasPlaying = playing;

    const double lenQ       = pump::lengthQuarters (lengthIdx);
    const double qps        = bpm / 60.0 / sampleRate;            // quarter notes per sample
    const double lenSamples = useHz ? sampleRate / (double) hzPhase.currentHz()
                                    : lenQ * 60.0 / bpm * sampleRate;
    oneShot.setLengthSamples (lenSamples);

    // ---- MIDI triggers: remember where each note-on lands in this block
    trigPos.clear();
    if (mode == 1)
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn())
                trigPos.push_back (meta.samplePosition);
    midi.clear();

    // ---- band split setup (only the Volume modules use it)
    if (anyVolumeSplit)
    {
        const float f = pFreq->load();
        if (f != lastCutoff)
        {
            for (auto& c : crossovers)
                c.setCutoffFrequency (f);
            lastCutoff = f;
        }
        if (! wasSplit)
            for (auto& c : crossovers)
                c.reset();
    }
    wasSplit = anyVolumeSplit;

    // ---- main loop
    float srcPeak = 0.0f, uiPh = -1.0f;
    size_t ti = 0;
    int lastBin = -1;
    int triggers = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        // level of the sound on this track (before we change it) and of the sidechain input
        float own = 0.0f, scIn = 0.0f;
        float in[2] = { 0.0f, 0.0f };
        for (int c = 0; c < numCh; ++c)
        {
            in[c] = buffer.getReadPointer (c)[i];
            own = std::max (own, std::abs (in[c]));
        }
        for (int c = 0; c < scCh; ++c)
            scIn = std::max (scIn, std::abs (sc[c][i]));

        const float srcLevel = ownSource ? own : scIn;   // what triggers the curve in Audio mode
        srcPeak = std::max (srcPeak, srcLevel);

        // where we are in the cycle (-1 = nothing is playing, every effect rests)
        float ph = -1.0f;
        const double hzP = useHz ? hzPhase.next() : 0.0;   // keeps the speed gliding in every mode

        if (mode == 0 && useHz) // Sync with Hz speed: runs at its own speed, always
        {
            ph = (float) hzP;
        }
        else if (mode == 0) // Sync: locked to the host's position
        {
            if (playing)
            {
                double p;
                if (havePpq)
                    p = pump::syncPhase (ppq + (double) i * qps, lenQ);
                else
                {
                    freePhase += qps / lenQ;
                    freePhase -= std::floor (freePhase);
                    p = freePhase;
                }
                ph = (float) p;
            }
        }
        else
        {
            if (mode == 1)
            {
                while (ti < trigPos.size() && trigPos[ti] <= i)
                {
                    oneShot.trigger();
                    ++triggers;
                    ++ti;
                }
            }
            else if (onset.process (srcLevel))
            {
                oneShot.trigger();
                ++triggers;
            }

            ph = oneShot.nextPhase();
        }
        uiPh = ph;

        // ---- run the modules: in a chain each one gets the result of the one before,
        //      side by side each one gets the original and the results are averaged
        float cur[2] = { in[0], in[1] };
        float acc[2] = { 0.0f, 0.0f };
        int active = 0;

        for (size_t s = 0; s < (size_t) pump::kMaxSlots; ++s)
        {
            const int t = slotType[s];
            if (t == pump::fxOff)
                continue;

            const float v  = ph >= 0.0f ? curves[s].eval (ph) : pump::fxNeutral (t, slotStyle[s]);
            const float vs = smoothers[s].process (v);

            float y[2] = { chain ? cur[0] : in[0], chain ? cur[1] : in[1] };

            if (t == pump::fxVolume)
            {
                const float eff = pump::fxEffective (t, vs, slotDepth[s]);
                for (int c = 0; c < numCh; ++c)
                {
                    if (split)
                    {
                        float lo = 0.0f, hi = 0.0f;
                        crossovers[s].processSample (c, y[c], lo, hi);
                        y[c] = lo * eff + hi;   // only the lows are ducked
                    }
                    else
                    {
                        y[c] *= eff;
                    }
                }
            }
            else
            {
                fx[s].process (t, slotStyle[s], vs, slotDepth[s], slotParam[s], y, numCh);
            }

            if (chain)
            {
                cur[0] = y[0];
                cur[1] = y[1];
            }
            else
            {
                acc[0] += y[0];
                acc[1] += y[1];
            }
            ++active;
        }

        float wet[2] = { in[0], in[1] };
        if (active > 0)
        {
            if (chain)
            {
                wet[0] = cur[0];
                wet[1] = cur[1];
            }
            else
            {
                wet[0] = acc[0] / (float) active;
                wet[1] = acc[1] / (float) active;
            }
        }

        const float mix = mixSmooth.getNextValue();
        float outAbs = 0.0f;
        for (int c = 0; c < numCh; ++c)
        {
            const float o = in[c] + mix * (wet[c] - in[c]);
            buffer.getWritePointer (c)[i] = o;
            outAbs = std::max (outAbs, std::abs (o));
        }

        // feed the waveform display: the loudest level seen at this point of the cycle (the last pass is shown)
        if (uiPh >= 0.0f)
        {
            const int bin = std::min (pump::kScopeBins - 1, (int) (uiPh * (float) pump::kScopeBins));
            const size_t b = (size_t) bin;
            if (bin != lastBin)
            {
                lastBin = bin;
                scopeIn[b].store  (0.0f, std::memory_order_relaxed);   // a new pass starts here: drop the old one
                scopeOut[b].store (0.0f, std::memory_order_relaxed);
                scopeSc[b].store  (0.0f, std::memory_order_relaxed);
            }
            if (own     > scopeIn[b].load  (std::memory_order_relaxed)) scopeIn[b].store  (own,      std::memory_order_relaxed);
            if (outAbs  > scopeOut[b].load (std::memory_order_relaxed)) scopeOut[b].store (outAbs,   std::memory_order_relaxed);
            if (srcLevel > scopeSc[b].load (std::memory_order_relaxed)) scopeSc[b].store  (srcLevel, std::memory_order_relaxed);
        }
        else
        {
            lastBin = -1;
        }
    }

    uiPhase.store (uiPh);
    uiSrcLevel.store (std::max (srcPeak, uiSrcLevel.load() * 0.9f));
    if (triggers > 0)
        trigCount.fetch_add (triggers);
}

//==============================================================================
juce::AudioProcessorEditor* PumpKitProcessor::createEditor()
{
    return new PumpKitEditor (*this);
}

void PumpKitProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    for (int s = 0; s < pump::kMaxSlots; ++s)
        state.setProperty ("curve" + juce::String (s), juce::String (curves[(size_t) s].toString()), nullptr);
    state.setProperty ("genre", genreIndex.load(), nullptr);
    state.setProperty ("preset", presetIndex.load(), nullptr);
    state.setProperty ("started", started.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PumpKitProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (tree);

            for (int s = 0; s < pump::kMaxSlots; ++s)
            {
                const juce::String key = "curve" + juce::String (s);
                if (tree.hasProperty (key))
                    curves[(size_t) s].fromString (tree[key].toString().toStdString());
                else if (s == 0 && tree.hasProperty ("curve"))   // projects from before the modules existed
                    curves[0].fromString (tree["curve"].toString().toStdString());
            }

            const int ng = pump::numGenres();
            const int g = juce::jlimit (0, ng - 1, (int) tree.getProperty ("genre", 0));
            const int np = (int) pump::genres()[(size_t) g].presets.size();
            genreIndex.store (g);
            presetIndex.store (juce::jlimit (0, np - 1, (int) tree.getProperty ("preset", 0)));

            // an older project has no start screen to go through
            started.store ((bool) tree.getProperty ("started", true));
            selectedSlot.store (0);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PumpKitProcessor();
}
