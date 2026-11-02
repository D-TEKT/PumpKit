#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

#include "PumpEngine.h"
#include "Presets.h"

class PumpKitProcessor : public juce::AudioProcessor
{
public:
    PumpKitProcessor();
    ~PumpKitProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PumpKit"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // shared with the editor
    juce::AudioProcessorValueTreeState apvts;
    std::array<pump::CurveTable, pump::kMaxSlots> curves;   // one curve for every effect module
    pump::CurveTable& curve = curves[0];                    // the first module's curve (kept for convenience)

    std::atomic<int>  selectedSlot { 0 };      // which module the editor is showing
    std::atomic<bool> started { false };       // the start screen has been passed

    std::atomic<int> genreIndex  { 0 };     // which preset group is selected (sets the colour)
    std::atomic<int> presetIndex { 0 };

    std::atomic<float> uiPhase    { -1.0f }; // where we are in the curve (0..1), -1 = idle
    std::atomic<float> uiSrcLevel { 0.0f };  // level of the trigger source (sidechain or own sound), slowly falling
    std::atomic<int>   trigCount  { 0 };     // goes up by one every time the curve is triggered

    // module management (call from the editor / message thread)
    int  activeModules() const;
    int  addModule (int type);              // returns the slot it went into, or -1 if all six are used
    void removeModule (int slot);
    void moveModule (int slot, int delta);  // changes the order, which matters in a chain
    void startWith (int type);              // the start screen: first module + leave the start screen

    // Waveform display: peak level of the sound at each position in the cycle
    std::array<std::atomic<float>, pump::kScopeBins> scopeIn, scopeOut, scopeSc;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setPlain (const juce::String& id, float value);
    float getPlain (const juce::String& id) const;
    void copyModule (int from, int to);

    std::atomic<float>* pMode      = nullptr;
    std::atomic<float>* pLength    = nullptr;
    std::atomic<float>* pMix       = nullptr;
    std::atomic<float>* pSmooth    = nullptr;
    std::atomic<float>* pSplit     = nullptr;
    std::atomic<float>* pFreq      = nullptr;
    std::atomic<float>* pThreshold = nullptr;
    std::atomic<float>* pRetrigger = nullptr;
    std::atomic<float>* pSource    = nullptr;
    std::atomic<float>* pRateMode  = nullptr;
    std::atomic<float>* pRateHz    = nullptr;
    std::atomic<float>* pCombine   = nullptr;
    std::array<std::atomic<float>*, pump::kMaxSlots> pType {}, pDepth {}, pOn {};

    double sampleRate = 44100.0;
    double lastBpm = 120.0;
    double freePhase = 0.0;
    int lastMode = -1;
    bool wasPlaying = false;
    pump::HzPhase hzPhase;
    bool wasSplit = false;
    float lastCutoff = -1.0f;

    pump::OneShot oneShot;
    pump::OnsetDetector onset;
    juce::SmoothedValue<float> mixSmooth;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, pump::kMaxSlots> crossovers;
    std::array<pump::FxProcessor, pump::kMaxSlots> fx;
    std::array<pump::GainSmoother, pump::kMaxSlots> smoothers;
    std::array<int, pump::kMaxSlots> lastType {};
    std::vector<int> trigPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PumpKitProcessor)
};
