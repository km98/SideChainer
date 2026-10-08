/*
    SideChain - production editor (Phase 7).

    Layout (dark, premium, minimal Music-Prod direction):

        +----------------------------------------------+
        |  SideChain                        Music-Prod |   header
        +----------------------------------------------+
        |              [ Kick Pump   v ]               |   preset
        |                                              |
        |            REAL-TIME GRAPH (live DSP)        |   graph
        |                                              |
        +----------------------------------------------+
        |      ( O )              ( o )                |
        |       50%                150 ms              |
        |  SIDECHAIN AMOUNT        RELEASE             |   controls
        +----------------------------------------------+

    AMOUNT (primary, large): how MUCH the main signal ducks.
    RELEASE (secondary, smaller): how FAST it comes back.
    PRESET (compact, centered): named factory combinations of the two
    parameters only. Shows the derived identity: a factory name when the
    live values match one exactly, otherwise "Default" or "Custom".

    The graph consumes real DSP frames from the audio thread through the
    lock-free FIFO on a ~30 Hz timer. Nothing in the GUI is faked; the
    graph reflects Release changes because the DSP itself changes.
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GraphComponent.h"
#include "InfoPage.h"
#include "MusicProdAuth.h"

//==============================================================================
// Custom-drawn rotary control shared by AMOUNT and RELEASE (production-style
// knob: track arc, value arc, body, pointer).
class ProdKnob : public juce::Slider
{
public:
    ProdKnob();

    void paint (juce::Graphics& g) override;

private:
    float getArcFraction() const; // 0..1 current position

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProdKnob)
};

//==============================================================================
class SideChainAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit SideChainAudioProcessorEditor (SideChainAudioProcessor&);
    ~SideChainAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // Friend declaration not needed: harness uses public API only.

    // Preset selector helpers (see .cpp): the ComboBox lists the factory
    // presets; the closed-box TEXT doubles as the current-preset display
    // ("Custom"/"Default" appear as text with no item selected).
    void buildPresetMenu();
    void refreshPresetDisplay();
    void refreshOffsetDisplay();

    // View selector (0.3.0): segmented SIDECHAIN | ANALYZER control.
    void selectView (GraphComponent::ViewMode m);

    void showMainView();
    void showInfoView();
    void drawMusicProdLogo (juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintOverChildren (juce::Graphics&) override;

    juce::Rectangle<int> logoArea;
    juce::Rectangle<int> infoButtonBounds;   // keeps paint() text clear of the button

    SideChainAudioProcessor& processorRef;

    GraphComponent graph;

    // Phase 8: Music-Prod account authentication (editor-owned; the audio
    // thread never touches it). Token persists to the app-data auth file,
    // NEVER to plugin state.
    sid::auth::AuthManager authManager;

    // Phase 8: view swap (main <-> INFO) inside this editor; no second window.
    InfoPageComponent infoPage;
    bool showingInfo = false;
    juce::TextButton infoButton { "INFO" };

    // Music-Prod logo (approved asset, copied from the web project) and its
    // derived alpha mask (the asset is opaque; the mask makes the plugin's
    // own background show through around the artwork).
    juce::Image musicProdLogo;
    mutable juce::Image logoMask;

    // PRESET selector (compact, visually secondary to AMOUNT) with
    // previous/next step arrows. The PRESET caption is drawn by the editor
    // in paint() below the [arrow][selector][arrow] group.
    juce::ComboBox presetBox;
    juce::DrawableButton presetPrevButton { "prevPreset", juce::DrawableButton::ImageFitted };
    juce::DrawableButton presetNextButton { "nextPreset", juce::DrawableButton::ImageFitted };
    juce::Path presetPrevArrow;
    juce::Path presetNextArrow;
    juce::Rectangle<int> presetCaptionBounds;

    // OFFSET: envelope timing relative to the DAW beat.
    // Buttons step the sidechainOffset parameter 1 ms per click (bounded
    // -30..+30 via the parameter range); the readout label shows the value.
    juce::DrawableButton offsetPrevButton { "offsetLeft", juce::DrawableButton::ImageFitted };
    juce::DrawableButton offsetNextButton { "offsetRight", juce::DrawableButton::ImageFitted };
    juce::Path offsetPrevArrow;
    juce::Path offsetNextArrow;
    juce::Label offsetValueLabel;
    juce::Label offsetCaption;

    // View selector (0.3.0): compact segmented [SIDECHAIN | ANALYZER],
    // right-aligned in the header next to the wordmark. SIDECHAIN default.
    juce::TextButton sidechainViewButton { "SIDECHAIN" };
    juce::TextButton analyzerViewButton { "ANALYZER" };

    // PRIMARY: ducking depth (large knob, left).
    ProdKnob   amountKnob;
    juce::Label amountValueLabel;
    juce::Label amountCaption;

    // DUCK LENGTH (0.3.0): total duck envelope duration (ms knob, centre).
    // Independent of presets - presets never write this parameter.
    ProdKnob   duckLengthKnob;
    juce::Label duckLengthValueLabel;
    juce::Label duckLengthCaption;

    // SHAPE: Release as envelope character (plateau vs tail; smaller knob).
    ProdKnob   releaseKnob;
    juce::Label releaseValueLabel;
    juce::Label releaseCaption;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> duckLengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;

    // Test-only access to the raw controls (headless probes reproduce the
    // real user drag paths and assert layout geometry; production code never
    // touches these directly).
    friend struct SideChainEditorTestAccess;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SideChainAudioProcessorEditor)
};
