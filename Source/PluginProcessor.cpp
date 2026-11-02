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

    // Audio mode: listen to the sidechain input, or to the sound on this track itself
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

    for (auto* arr : { &scopeIn, &scopeOut, &scopeSc })
        for (auto& v : *arr)
            v.store (0.0f, std::memory_order_relaxed);

    // start on the first Hardstyle preset
    curve.setNodes (pump::genres()[0].presets[0].nodes);

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
    crossover.prepare (spec);
    crossover.reset();
    lastCutoff = -1.0f;
    wasSplit = false;

    smoother.prepare (sr);
    smoother.reset (1.0f);
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
void PumpKitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numCh = std::min (getMainBusNumInputChannels(), buffer.getNumChannels());
    if (numCh <= 0 || numSamples <= 0)
    {
        midi.clear();
        return;
    }

    const int mode       = (int) pMode->load();
    const int lengthIdx  = (int) pLength->load();
    const bool split     = pSplit->load() > 0.5f;
    const bool ownSource = pSource->load() > 0.5f;
    const bool useHz     = pRateMode->load() > 0.5f;

    if (useHz)
        hzPhase.setHz (pRateHz->load());

    mixSmooth.setTargetValue (pMix->load() * 0.01f);
    smoother.setTimeMs (pSmooth->load());
    onset.setParams (pThreshold->load(), pRetrigger->load());

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

    // ---- band split setup
    if (split)
    {
        const float f = pFreq->load();
        if (f != lastCutoff)
        {
            crossover.setCutoffFrequency (f);
            lastCutoff = f;
        }
        if (! wasSplit)
            crossover.reset();
    }
    wasSplit = split;

    // ---- main loop
    float srcPeak = 0.0f, uiPh = -1.0f;
    size_t ti = 0;
    int lastBin = -1;
    int triggers = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        // level of the sound on this track (before we change it) and of the sidechain input
        float own = 0.0f, scIn = 0.0f;
        for (int c = 0; c < numCh; ++c)
            own = std::max (own, std::abs (buffer.getReadPointer (c)[i]));
        for (int c = 0; c < scCh; ++c)
            scIn = std::max (scIn, std::abs (sc[c][i]));

        const float srcLevel = ownSource ? own : scIn;   // what triggers the curve in Audio mode
        srcPeak = std::max (srcPeak, srcLevel);

        float g = 1.0f;
        uiPh = -1.0f;

        const double hzP = useHz ? hzPhase.next() : 0.0;   // keeps the speed gliding in every mode

        if (mode == 0 && useHz) // Sync with Hz speed: runs at its own speed, always
        {
            g = curve.eval ((float) hzP);
            uiPh = (float) hzP;
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
                g = curve.eval ((float) p);
                uiPh = (float) p;
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

            g = oneShot.next (curve);
            uiPh = oneShot.phase();
        }

        const float gs  = smoother.process (g);
        const float mix = mixSmooth.getNextValue();
        const float eff = 1.0f - mix * (1.0f - gs);

        float outAbs = 0.0f;
        for (int c = 0; c < numCh; ++c)
        {
            float* d = buffer.getWritePointer (c);
            const float x = d[i];
            if (split)
            {
                float lo = 0.0f, hi = 0.0f;
                crossover.processSample (c, x, lo, hi);
                d[i] = lo * eff + hi;   // only the lows are ducked
            }
            else
            {
                d[i] = x * eff;
            }
            outAbs = std::max (outAbs, std::abs (d[i]));
        }

        // feed the waveform display: remember the loudest level seen at this point of the cycle
        if (uiPh >= 0.0f)
        {
            const int bin = std::min (pump::kScopeBins - 1, (int) (uiPh * (float) pump::kScopeBins));
            const size_t b = (size_t) bin;
            if (bin != lastBin)
            {
                lastBin = bin;
                // fade what was there on the previous pass, so old audio disappears over time
                scopeIn[b].store  (scopeIn[b].load  (std::memory_order_relaxed) * 0.85f, std::memory_order_relaxed);
                scopeOut[b].store (scopeOut[b].load (std::memory_order_relaxed) * 0.85f, std::memory_order_relaxed);
                scopeSc[b].store  (scopeSc[b].load  (std::memory_order_relaxed) * 0.85f, std::memory_order_relaxed);
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
    state.setProperty ("curve", juce::String (curve.toString()), nullptr);
    state.setProperty ("genre", genreIndex.load(), nullptr);
    state.setProperty ("preset", presetIndex.load(), nullptr);
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

            if (tree.hasProperty ("curve"))
                curve.fromString (tree["curve"].toString().toStdString());

            const int ng = pump::numGenres();
            const int g = juce::jlimit (0, ng - 1, (int) tree.getProperty ("genre", 0));
            const int np = (int) pump::genres()[(size_t) g].presets.size();
            genreIndex.store (g);
            presetIndex.store (juce::jlimit (0, np - 1, (int) tree.getProperty ("preset", 0)));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PumpKitProcessor();
}
