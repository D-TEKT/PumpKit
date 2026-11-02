#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

#include "PumpEngine.h"
#include "Presets.h"
#include "LinkBus.h"

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
    pump::CurveTable curve;

    std::atomic<int> genreIndex  { 0 };     // which preset group is selected (sets the colour)
    std::atomic<int> presetIndex { 0 };

    std::atomic<float> uiPhase    { -1.0f }; // where we are in the curve (0..1), -1 = idle
    std::atomic<float> uiSrcLevel { 0.0f };  // level of the trigger source (sidechain or own sound), slowly falling
    std::atomic<int>   trigCount  { 0 };     // goes up by one every time the curve is triggered

    // Waveform display: peak level of the sound at each position in the cycle
    std::array<std::atomic<float>, pump::kScopeBins> scopeIn, scopeOut, scopeSc;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

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
    std::atomic<float>* pSendTo    = nullptr;

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
    pump::GainSmoother smoother;
    juce::SmoothedValue<float> mixSmooth;
    juce::dsp::LinkwitzRileyFilter<float> crossover;
    std::vector<int> trigPos;
    std::vector<float> linkTmp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PumpKitProcessor)
};
