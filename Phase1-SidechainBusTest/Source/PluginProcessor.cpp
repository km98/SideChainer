/*
    Phase1 SidechainBusTest - processor implementation.

    Bus layout (declared via BusesProperties, the first-class JUCE mechanism):

        Input bus 0: "Input"      stereo
        Input bus 1: "Sidechain"  stereo, inactive (disabled) by default
        Output bus 0: "Output"    stereo

    DSP behaviour (intentionally minimal):
      - main input passes through to main output untouched
      - sidechain is measured (RMS / peak) for diagnostics only
      - sidechain is NEVER copied into the output
      - a disabled/absent sidechain is treated as "no sidechain signal"
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
Phase1SidechainBusTestAudioProcessor::Phase1SidechainBusTestAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false) // false = disabled by default
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true))
{
}

//==============================================================================
void Phase1SidechainBusTestAudioProcessor::prepareToPlay (double /*sampleRate*/, int /*samplesPerBlock*/)
{
    sidechainRmsDb.store (-100.0f);
    sidechainPeakDb.store (-100.0f);
}

bool Phase1SidechainBusTestAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn    = layouts.getMainInputChannelSet();
    const auto& mainOut   = layouts.getMainOutputChannelSet();
    const auto& sidechain = layouts.getChannelSet (true, 1); // input bus index 1

    // main in/out must be exactly stereo
    if (mainIn != juce::AudioChannelSet::stereo() || mainOut != juce::AudioChannelSet::stereo())
        return false;

    // sidechain bus must be either disabled, stereo, or mono
    if (sidechain.isDisabled() || sidechain == juce::AudioChannelSet::stereo()
        || sidechain == juce::AudioChannelSet::mono())
        return true;

    return false;
}

void Phase1SidechainBusTestAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                         juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainIn  = getBusBuffer (buffer, true, 0);
    auto sideBuf = getBusBuffer (buffer, true, 1);
    auto out     = getBusBuffer (buffer, false, 0);

    const int numSamples = buffer.getNumSamples();

    // ------------------------------------------------------------------
    // Main path: strict passthrough. Nothing from the sidechain buffer
    // is ever written here.
    // ------------------------------------------------------------------
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
    {
        const int srcCh = juce::jmin (ch, mainIn.getNumChannels() - 1);

        if (mainIn.getNumChannels() > 0)
            out.copyFrom (ch, 0, mainIn, srcCh, 0, numSamples);
        else
            out.clear (ch, 0, numSamples);
    }

    // ------------------------------------------------------------------
    // Sidechain path: diagnostics only (RMS / peak), never audible.
    // ------------------------------------------------------------------
    lastMainInputChannels.store (mainIn.getNumChannels());

    if (sideBuf.getNumChannels() <= 0 || numSamples == 0)
    {
        // Disabled or not connected bus: treat as NO SIDECHAIN SIGNAL.
        lastSidechainChannels.store (0);
        sidechainRmsDb.store (-100.0f);
        sidechainPeakDb.store (-100.0f);
        return;
    }

    lastSidechainChannels.store (sideBuf.getNumChannels());

    double sumSquares = 0.0;
    float peak = 0.0f;
    int count = 0;

    for (int ch = 0; ch < sideBuf.getNumChannels(); ++ch)
    {
        const auto* data = sideBuf.getReadPointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float s = data[i];
            sumSquares += (double) s * (double) s;
            peak = juce::jmax (peak, std::abs (s));
        }

        count += numSamples;
    }

    if (count > 0)
    {
        const float rms = (float) std::sqrt (sumSquares / (double) count);

        sidechainRmsDb.store (juce::Decibels::gainToDecibels (rms, -100.0f));
        sidechainPeakDb.store (juce::Decibels::gainToDecibels (peak, -100.0f));
    }
}

//==============================================================================
juce::AudioProcessorEditor* Phase1SidechainBusTestAudioProcessor::createEditor()
{
    return new Phase1SidechainBusTestAudioProcessorEditor (*this);
}

//==============================================================================
void Phase1SidechainBusTestAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Minimal state: a single version tag. No parameters, no presets.
    juce::MemoryOutputStream stream (destData, false);
    stream.writeString ("Phase1SidechainBusTestState");
}

void Phase1SidechainBusTestAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::ignoreUnused (data, sizeInBytes);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Phase1SidechainBusTestAudioProcessor();
}
