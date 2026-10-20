/*
    PumpCurve - production editor (0.5.0 redesign of the Phase 7 editor).

    Layout (fixed 800x520; graphite base, mint accents, hairline rules):

        +------------------------------------------------------------------+
        | PumpCurve   [<][   Preset name   v ][>] [SAVE PRESET]  (DEMO) INFO|  header
        +------------------------------------------------------------------+
        | (o) ACTIVE ONLY DURING PLAY/REC   status     TIMING OFFSET [<0>] |  transport row
        | +--------------------------------------------------------------+ |
        | | PUMP CURVE                                  IN  OUT  [RESET] | |
        | | UNITY  curve + control points over the live in/out waveform  | |  curve editor
        | |  DUCK                                                        | |
        | +--------------------------------------------------------------+ |
        | SMOOTH ===========================o==========================50% |
        | ---------------------------------------------------------------- |
        |   (O) AMOUNT 50%        (O) TIME 250 ms      (O) RELEASE 150 ms  |  sound controls
        |                           MUSIC-PROD                             |
        +------------------------------------------------------------------+

    The curve editor is the focal point and the largest element; the three
    sound-shaping knobs share one size and one baseline; presets live in the
    header where a producer expects them; INFO/licensing is one click away and
    the header pill always says which mode the instance is in.

    AMOUNT (primary, large): how MUCH the main signal ducks.
    RELEASE (secondary, smaller): how FAST it comes back.
    PRESET (compact, centered): named factory combinations of the two
    parameters only. Shows the derived identity: a factory name when the
    live values match one exactly, otherwise "Default" or "Custom".

    The graph consumes real DSP frames from the audio thread through the
    lock-free FIFO on a ~30 Hz timer, and the live waveform layer behind the
    curve is fed from the scope FIFO the same way. Nothing in the GUI is
    faked; the graph reflects Release changes because the DSP itself changes.

    TRANSPORT SWITCH (0.4.2): "ACTIVE ONLY DURING PLAY/REC" sits in the
    transport row next to TIMING OFFSET. It is an APVTS attachment on the
    `transportGate` parameter, so it is automatable, saved with state and
    defaults to ON (including for sessions saved before it existed).
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GraphComponent.h"
#include "InfoPage.h"
#include "MusicProdAuth.h"
#include "PumpCurveTheme.h"

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
    ~SideChainAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void moved() override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;
    void setScaleFactor (float newScale) override;

private:
    void timerCallback() override;
    void refreshRenderingAfterLifecycleChange();
    juce::int64 lastTimerCallbackTimeMs = 0;

    // Version-mode readout shown in the header (and mirrored on the INFO
    // page): FULL VERSION, DEMO mm:ss, or DEMO EXPIRED.
    juce::String demoStatusText() const;
    juce::String lastDemoStatusText;
    bool lastSignedInState = false;

    // Preset selector helpers (see .cpp): the ComboBox lists the factory
    // presets; the closed-box TEXT doubles as the current-preset display
    // ("Custom"/"Default" appear as text with no item selected).
    void buildPresetMenu();
    void showSavePresetForm();
    void commitUserPreset();
    void cancelSavePreset();
    // A short preset outcome ("Preset saved.") shown under the header, cleared
    // by the timer after kPresetStatusHoldMs so it never lingers.
    void showPresetStatus (const juce::String& text);
    void refreshPresetDisplay();
    void refreshOffsetDisplay();

    // View selector (0.3.0): segmented SIDECHAIN | ANALYZER control.
    void selectView (GraphComponent::ViewMode m);

    void showMainView();
    void showInfoView();
    void drawMusicProdLogo (juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    juce::Rectangle<int> logoArea;
    juce::Rectangle<int> infoButtonBounds;   // keeps paint() text clear of the button
    juce::Rectangle<int> statusPillBounds;   // header version-mode pill
    int headerHeight = 56;
    int deckRuleY = 0;                       // hairline above the knob deck

    // Declared before every child so it outlives them; detached in the
    // destructor before the children go (JUCE requires that order).
    sid::theme::LookAndFeel lookAndFeel;

    SideChainAudioProcessor& processorRef;

    // Hover help: the tooltips set on every control are only ever shown when
    // a TooltipWindow exists in the hierarchy.
    juce::TooltipWindow tooltipWindow { this, 700 };

    GraphComponent graph;

    // Music-Prod account authentication + entitlement (0.5.0). The manager is
    // OWNED BY THE PROCESSOR, not the editor, so the entitlement is known and
    // the full version applies even when no editor window is ever opened. This
    // is a reference for the UI only; the audio thread never touches it, and
    // the token persists to the app-data auth file, NEVER to plugin state.
    sid::auth::AuthManager& authManager;

    // Phase 8: view swap (main <-> INFO) inside this editor; no second window.
    InfoPageComponent infoPage;
    bool showingInfo = false;
    juce::TextButton infoButton { "INFO" };

    // Music-Prod logo (approved asset, copied from the web project) and its
    // derived alpha mask (the asset is opaque; the mask makes the plugin's
    // own background show through around the artwork).
    juce::Image musicProdLogo;
    mutable juce::Image logoMask;

    // PRESET selector in the header with previous/next step arrows and the
    // SAVE PRESET action (the in-place name form replaces the group).
    juce::ComboBox presetBox;
    sid::presets::UserPresetLibrary userPresetLibrary;
    juce::Array<sid::presets::UserPresetLibrary::Entry> userPresets;
    juce::TextButton savePresetButton { "SAVE PRESET" };
    juce::TextEditor presetNameEditor;
    juce::TextButton confirmPresetButton { "SAVE" }, cancelPresetButton { "CANCEL" };
    juce::Label presetStatusLabel;
    bool savingPreset = false;
    juce::int64 lastPresetLibraryRefreshMs = 0;
    juce::int64 presetStatusShownMs = 0;
    static constexpr juce::int64 kPresetStatusHoldMs = 4000;
    juce::DrawableButton presetPrevButton { "prevPreset", juce::DrawableButton::ImageFitted };
    juce::DrawableButton presetNextButton { "nextPreset", juce::DrawableButton::ImageFitted };
    juce::Path presetPrevArrow;
    juce::Path presetNextArrow;

    // OFFSET: envelope timing relative to the DAW beat.
    // Buttons step the sidechainOffset parameter 1 ms per click (bounded
    // -30..+30 via the parameter range); the readout label shows the value.
    juce::DrawableButton offsetPrevButton { "offsetLeft", juce::DrawableButton::ImageFitted };
    juce::DrawableButton offsetNextButton { "offsetRight", juce::DrawableButton::ImageFitted };
    juce::Path offsetPrevArrow;
    juce::Path offsetNextArrow;
    juce::Label offsetValueLabel;
    juce::Label offsetCaption;

    // TRANSPORT SWITCH (0.4.2): "ACTIVE ONLY DURING PLAY/REC", default ON.
    // Bound to the transportGate parameter through a ButtonAttachment, so the
    // click, host automation, undo and state restore all share one path.
    juce::ToggleButton transportGateToggle { "ACTIVE ONLY DURING PLAY/REC" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> transportGateAttachment;

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
    juce::Label smoothValueLabel;
    juce::Label smoothCaption;
    juce::Slider smoothSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smoothAttachment;

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
