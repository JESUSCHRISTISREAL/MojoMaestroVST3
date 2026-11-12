#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "MojoMaestro.hpp"

// Stereo VST3 emulation of Ethan Winer's Mojo Maestro with an original faceplate editor.
class MojoMaestroProcessor final : public juce::AudioProcessor
{
public:
    MojoMaestroProcessor();
    ~MojoMaestroProcessor() override;

    void prepareToPlay (double sampleRate, int maxBlockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    bool hasEditor() const override                      { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    const juce::String getName() const override          { return "Mojo Maestro"; }
    bool acceptsMidi() const override                    { return false; }
    bool producesMidi() const override                   { return false; }
    bool isMidiEffect() const override                   { return false; }
    double getTailLengthSeconds() const override         { return 0.0; }

    int getNumPrograms() override                        { return 1; }
    int getCurrentProgram() override                     { return 0; }
    void setCurrentProgram (int) override                {}
    const juce::String getProgramName (int) override     { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Parameters (raw atomics, safe to read on the audio thread)
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pHarm = nullptr;
    std::atomic<float>* pAmount = nullptr;
    std::atomic<float>* pRefLevel = nullptr;
    std::atomic<float>* pTrim = nullptr;
    std::atomic<float>* pOversampling = nullptr;
    std::atomic<float>* pDcBlock = nullptr;
    std::atomic<float>* pModel = nullptr;

    // One signal path: the circuit at the oversampled rate plus a delay padding it to the largest
    // latency. Reported latency is fixed, so every setting lines up and can be crossfaded.
    struct Delay
    {
        std::vector<float> buf[2];
        int len = 0, pos = 0, capacity = 0;
        void allocate (int maxLen) { capacity = juce::jmax (1, maxLen); for (auto& b : buf) b.assign ((size_t) capacity, 0.0f); len = pos = 0; }
        void set (int n) { len = juce::jlimit (0, capacity, n); pos = 0; for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f); }
        float run (int ch, float x) { if (len == 0) return x; const float y = buf[ch][(size_t) pos]; buf[ch][(size_t) pos] = x; return y; }
        void advance() { if (len > 0 && ++pos >= len) pos = 0; }
    };
    struct Chain
    {
        MojoMaestroStereo core;
        int choice = -1, factor = 1;
        Delay delay;
    };
    void initChain (Chain&, int choice);
    void runChain (Chain&, float* l, float* r, int n, bool withModeFade);

    Chain cur, prev;                      // prev only runs while an oversampling change is crossfaded
    int osFadeLeft = 0, osFadeTotal = 0, osWarmLeft = 0;
    double osLatency[4] {};               // latency of each setting in host samples (0 = off)
    double maxLatency = 0.0;

    // Click-free switching: on a Mode / Harmonics / Model change the old config keeps running
    // (fadeCore) and fades out over ~10 ms. A change arriving mid-fade waits.
    MojoMaestroStereo fadeCore;
    int fadeRemainingUp = 0, fadeTotalUp = 0;
    int lastMode = -1, lastHarm = -1, lastModel = -1;

    // Oversampling: index 0 = 2x, 1 = 4x, 2 = 8x. Choice 0 = off.
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> oversamplers;
    double hostSampleRate = 44100.0;

    juce::SmoothedValue<float> amountSmoothed, refSmoothed, trimGainSmoothed, dcMixSmoothed;   // dcMix: 1 = DC blocker in, 0 = out
    double dcX1[2] {}, dcY1[2] {};
    double dcR = 0.999;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MojoMaestroProcessor)
};
