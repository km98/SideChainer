/*
    Phase1 SidechainBusTest - minimal diagnostic editor implementation.
    Shows plugin identity, bus status, and sidechain RMS/peak levels.
    Deliberately plain: this is NOT the production UI.
*/

#include "PluginEditor.h"

//==============================================================================
Phase1SidechainBusTestAudioProcessorEditor::Phase1SidechainBusTestAudioProcessorEditor (
    Phase1SidechainBusTestAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    addAndMakeVisible (statusLabel);
    statusLabel.setJustificationType (juce::Justification::topLeft);
    statusLabel.setFont (juce::Font (13.0f));

    setSize (420, 200);
    startTimerHz (30);
}

void Phase1SidechainBusTestAudioProcessorEditor::timerCallback()
{
    const float rms  = processorRef.sidechainRmsDb.load();
    const float peak = processorRef.sidechainPeakDb.load();
    const int   scCh = processorRef.lastSidechainChannels.load();
    const int   inCh = processorRef.lastMainInputChannels.load();

    statusLabel.setText (juce::String (
        "Phase1 SidechainBusTest  (JUCE 6.1.3)\n"
        "Main input ch:  " + juce::String (inCh) + "\n"
        "Sidechain ch:   " + juce::String (scCh) +
        (scCh == 0 ? juce::String ("  (disabled / not connected)") : juce::String()) + "\n"
        "Sidechain RMS:  " + (scCh == 0 ? juce::String ("n/a") : juce::String (rms, 1) + " dB") + "\n"
        "Sidechain Peak: " + (scCh == 0 ? juce::String ("n/a") : juce::String (peak, 1) + " dB")),
        juce::dontSendNotification);
}

void Phase1SidechainBusTestAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff20242b));
    g.setColour (juce::Colours::white);
    g.drawRect (getLocalBounds());
}
