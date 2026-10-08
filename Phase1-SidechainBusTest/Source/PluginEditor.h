/*
    Phase1 SidechainBusTest - minimal diagnostic editor.
    Plain background, identifying text, sidechain RMS/peak readout.
    NOT the production UI.
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class Phase1SidechainBusTestAudioProcessorEditor : public juce::AudioProcessorEditor,
                                                   private juce::Timer
{
public:
    explicit Phase1SidechainBusTestAudioProcessorEditor (Phase1SidechainBusTestAudioProcessor&);
    ~Phase1SidechainBusTestAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    void timerCallback() override;

    Phase1SidechainBusTestAudioProcessor& processorRef;
    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Phase1SidechainBusTestAudioProcessorEditor)
};
