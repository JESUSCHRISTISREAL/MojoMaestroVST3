#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr int kChunk = 32;   // smoothing step
    const juce::StringArray kModes   { "Bypass", "Glue", "Mojo" };
    const juce::StringArray kHarm    { "Even", "Odd" };
    const juce::StringArray kOsModes { "Off", "2x", "4x", "8x" };
    const juce::StringArray kModels  { "Simple", "Coupled" };
}

juce::AudioProcessorValueTreeState::ParameterLayout MojoMaestroProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", kModes, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "harmonics", 1 }, "Harmonics", kHarm, 0));

    // 1.0 = "More" on the original panel (Amount pot resistance minimum).
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "amount", 1 }, "Amount",
                                                       NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    // Volts represented by 0 dBFS. The passive diodes turn on at an absolute voltage.
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "reflevel", 1 }, "Input Reference",
                                                       NormalisableRange<float> (0.1f, 8.0f, 0.01f, 0.5f), 1.24f,
                                                       AudioParameterFloatAttributes().withLabel ("V")));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "trim", 1 }, "Output Trim",
                                                       NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "oversampling", 1 }, "Oversampling", kOsModes, 2));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { "dcblock", 1 }, "DC Block", true));

    // Coupled = C1, R1 and the diodes solved together at one node (closest to the circuit).
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "model", 1 }, "Circuit Model", kModels, 1));
    return layout;
}

MojoMaestroProcessor::MojoMaestroProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pMode         = apvts.getRawParameterValue ("mode");
    pHarm         = apvts.getRawParameterValue ("harmonics");
    pAmount       = apvts.getRawParameterValue ("amount");
    pRefLevel     = apvts.getRawParameterValue ("reflevel");
    pTrim         = apvts.getRawParameterValue ("trim");
    pOversampling = apvts.getRawParameterValue ("oversampling");
    pDcBlock      = apvts.getRawParameterValue ("dcblock");
    pModel        = apvts.getRawParameterValue ("model");

}

MojoMaestroProcessor::~MojoMaestroProcessor()
{
}

bool MojoMaestroProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MojoMaestroProcessor::prepareToPlay (double sampleRate, int maxBlockSize)
{
    hostSampleRate = sampleRate;

    const auto maxChunk = (size_t) juce::jmin (maxBlockSize, kChunk);
    for (size_t i = 0; i < oversamplers.size(); ++i)
    {
        oversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>> (
            2, i + 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        oversamplers[i]->initProcessing (juce::jmax ((size_t) 1, maxChunk));
    }

    amountSmoothed.reset (sampleRate, 0.02);
    refSmoothed.reset (sampleRate, 0.02);
    trimGainSmoothed.reset (sampleRate, 0.02);
    amountSmoothed.setCurrentAndTargetValue (pAmount->load());
    refSmoothed.setCurrentAndTargetValue (pRefLevel->load());
    trimGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pTrim->load()));

    dcR = std::exp (-2.0 * juce::MathConstants<double>::pi * 5.0 / sampleRate);   // 5 Hz
    std::fill (std::begin (dcX1), std::end (dcX1), 0.0);
    std::fill (std::begin (dcY1), std::end (dcY1), 0.0);

    dcMixSmoothed.reset (sampleRate, 0.010);
    dcMixSmoothed.setCurrentAndTargetValue (pDcBlock->load() > 0.5f ? 1.0f : 0.0f);

    // every setting gets padded to the largest latency
    osLatency[0] = 0.0;
    for (size_t i = 0; i < oversamplers.size(); ++i)
        osLatency[i + 1] = (double) oversamplers[i]->getLatencyInSamples();
    maxLatency = *std::max_element (std::begin (osLatency), std::end (osLatency));
    const int maxLen = (int) std::ceil (maxLatency) + 4;
    cur.delay.allocate (maxLen);
    prev.delay.allocate (maxLen);
    setLatencySamples (juce::roundToInt (maxLatency));

    fadeRemainingUp = 0; osFadeLeft = osFadeTotal = osWarmLeft = 0;
    lastMode = lastHarm = lastModel = -1;
    for (auto& o : oversamplers) o->reset();
    prev.choice = -1;
    initChain (cur, juce::jlimit (0, 3, (int) pOversampling->load()));
}

// new chain from silence
void MojoMaestroProcessor::initChain (Chain& ch, int choice)
{
    ch.choice = choice;
    ch.factor = choice == 0 ? 1 : (1 << choice);
    ch.core.prepare (hostSampleRate * ch.factor);                    // filter coefficients, state cleared
    if (choice > 0) oversamplers[(size_t) choice - 1]->reset();
    ch.delay.set (juce::roundToInt (maxLatency - osLatency[choice]));
}

// run one chain in place
void MojoMaestroProcessor::runChain (Chain& ch, float* l, float* r, int n, bool withModeFade)
{
    auto clip = [&] (float* cl, float* cr, size_t count)
    {
        if (! withModeFade || fadeRemainingUp <= 0)
        {
            for (size_t i = 0; i < count; ++i)
            {
                cl[i] = ch.core.left.processSample  (cl[i]);
                cr[i] = ch.core.right.processSample (cr[i]);
            }
            return;
        }
        for (size_t i = 0; i < count; ++i)   // crossfade old to new Mode / Harmonics / Model
        {
            const float oldL = fadeCore.left.processSample  (cl[i]);
            const float oldR = fadeCore.right.processSample (cr[i]);
            const float newL = ch.core.left.processSample  (cl[i]);
            const float newR = ch.core.right.processSample (cr[i]);
            const float w = fadeRemainingUp > 0 ? 1.0f - (float) fadeRemainingUp / (float) fadeTotalUp : 1.0f;
            cl[i] = oldL + w * (newL - oldL);
            cr[i] = oldR + w * (newR - oldR);
            if (fadeRemainingUp > 0) --fadeRemainingUp;
        }
    };

    float* ptrs[2] { l, r };
    juce::dsp::AudioBlock<float> block (ptrs, 2, (size_t) n);
    if (ch.choice > 0)
    {
        auto& os = *oversamplers[(size_t) ch.choice - 1];
        auto up = os.processSamplesUp (block);
        clip (up.getChannelPointer (0), up.getChannelPointer (1), up.getNumSamples());
        os.processSamplesDown (block);
    }
    else
        clip (l, r, (size_t) n);

    for (int i = 0; i < n; ++i)
    {
        l[i] = ch.delay.run (0, l[i]);
        r[i] = ch.delay.run (1, r[i]);
        ch.delay.advance();
    }
}

void MojoMaestroProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
    if (buffer.getNumChannels() < 2)
        return;

    // Changes never overlap: wait for a running fade to finish, then apply the next on its own.
    const int choice = juce::jlimit (0, 3, (int) pOversampling->load());
    if (choice != cur.choice && fadeRemainingUp <= 0 && osFadeLeft <= 0)
    {
        // Oversampling changed: the old path keeps running and fades out. Same latency everywhere, so they line up.
        std::swap (cur, prev);
        initChain (cur, choice);
        osWarmLeft  = juce::roundToInt (0.004 * hostSampleRate);                 // warm-up for the new filters
        osFadeTotal = osFadeLeft = juce::jmax (1, juce::roundToInt (0.016 * hostSampleRate));
    }

    const int modeNow  = juce::jlimit (0, 2, (int) pMode->load());
    const int harmNow  = juce::jlimit (0, 1, (int) pHarm->load());
    const int modelNow = juce::jlimit (0, 1, (int) pModel->load());

    // lastMode / lastHarm / lastModel hold the configuration the circuit is running now.
    if (lastMode < 0)
    {
        lastMode = modeNow; lastHarm = harmNow; lastModel = modelNow;      // first block: adopt, nothing to fade from
    }
    else if ((modeNow != lastMode || harmNow != lastHarm || modelNow != lastModel) && osFadeLeft <= 0 && fadeRemainingUp <= 0)
    {
        fadeCore = cur.core;   // copy of the old config and its state
        fadeTotalUp = fadeRemainingUp = juce::jmax (1, juce::roundToInt (0.010 * hostSampleRate)) * cur.factor;
        lastMode = modeNow; lastHarm = harmNow; lastModel = modelNow;
    }

    auto setControls = [&] (MojoMaestroStereo& c)
    {
        c.setMode         ((MojoMaestroChannel::Mode) lastMode);
        c.setHarmonics    ((MojoMaestroChannel::Harmonics) lastHarm);
        c.setCircuitModel ((MojoMaestroChannel::CircuitModel) lastModel);
    };
    setControls (cur.core);
    if (osFadeLeft > 0) setControls (prev.core);

    amountSmoothed.setTargetValue (pAmount->load());
    refSmoothed.setTargetValue (pRefLevel->load());
    trimGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (pTrim->load()));
    dcMixSmoothed.setTargetValue (pDcBlock->load() > 0.5f ? 1.0f : 0.0f);

    float scratch[2][kChunk];

    for (int start = 0; start < numSamples; start += kChunk)
    {
        const int n = juce::jmin (kChunk, numSamples - start);

        // UI "Amount" 1.0 = More = 0 ohms of pot in the circuit.
        const double amountNow = 1.0 - (double) amountSmoothed.skip (n);
        const double refNow    = (double) refSmoothed.skip (n);
        auto setLevels = [&] (MojoMaestroStereo& c) { c.setAmount (amountNow); c.setFullScaleVolts (refNow); };
        setLevels (cur.core);
        if (fadeRemainingUp > 0) { fadeCore.setAmount (amountNow); fadeCore.setFullScaleVolts (refNow); }

        float* l = buffer.getWritePointer (0) + start;
        float* r = buffer.getWritePointer (1) + start;

        if (osFadeLeft > 0)
        {
            setLevels (prev.core);
            std::copy (l, l + n, scratch[0]);
            std::copy (r, r + n, scratch[1]);
            runChain (prev, scratch[0], scratch[1], n, false);
            runChain (cur, l, r, n, false);
            for (int i = 0; i < n; ++i)
            {
                float w = 0.0f;
                if (osWarmLeft > 0) --osWarmLeft;
                else if (osFadeLeft > 0)
                {
                    const float t = 1.0f - (float) osFadeLeft / (float) osFadeTotal;
                    w = t * t * (3.0f - 2.0f * t);          // smoothstep
                    --osFadeLeft;
                }
                else w = 1.0f;
                l[i] = scratch[0][i] + w * (l[i] - scratch[0][i]);
                r[i] = scratch[1][i] + w * (r[i] - scratch[1][i]);
            }
        }
        else
        {
            runChain (cur, l, r, n, true);
        }
    }

    // DC blocker (Even mode makes a DC offset). Always running; the switch is a 10 ms crossfade.
    for (int i = 0; i < numSamples; ++i)
    {
        const float m = dcMixSmoothed.getNextValue();
        for (int ch = 0; ch < 2; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            const double x = d[i];
            const double y = x - dcX1[ch] + dcR * dcY1[ch];
            dcX1[ch] = x;
            dcY1[ch] = y;
            d[i] = (float) (x + (double) m * (y - x));
        }
    }

    // output trim, no makeup gain (the real circuit loses level)
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = trimGainSmoothed.getNextValue();
        buffer.getWritePointer (0)[i] *= g;
        buffer.getWritePointer (1)[i] *= g;
    }
}

juce::AudioProcessorEditor* MojoMaestroProcessor::createEditor()
{
    return new MojoMaestroEditor (*this);
}

void MojoMaestroProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void MojoMaestroProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MojoMaestroProcessor();
}
