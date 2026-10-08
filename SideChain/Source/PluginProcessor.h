/*
    SideChainer (technical name: SideChain) - tempo-synchronised rhythmic
    ducking effect (Music-Prod). 0.4.0 internal-trigger architecture.

    Bus architecture (0.4.0):
        bus 0 in : "Input"      stereo, enabled by default
        bus 0 out: "Output"     stereo
    There is NO auxiliary sidechain input any more: the plugin generates
    its own rhythmic trigger from the DAW transport (PPQ position + BPM).
    Normal operation requires NO sidechain routing in the host.

    DSP (0.4.0 internal trigger; envelope engine unchanged from 0.3.0):
      - BEAT SCHEDULER: one duck envelope per quarter-note beat, fired at
        the exact sample (host timeInSamples/PPQ; loop jumps, tempo
        changes and BPM loss handled fail-safe - see BeatScheduler.h).
      - envelope: fast attack, hold, exponential release
      - DUCK LENGTH: TOTAL duration of one duck envelope
      - Release (SHAPE): plateau/tail split within that duration
      - one gain value applied identically to main L and R
      - OFFSET (-30..+30 ms, 1 ms steps): the envelope can be scheduled
        earlier (lookahead) or later relative to the beat. The plugin
        latency is DuckEngine::lookaheadSamples(rate); the main path runs
        through a delay line of that length, and the envelope is delayed
        by (lookahead + offset) samples on the same timeline, so a
        negative offset genuinely ducks EARLIER than the beat.
        Host-compensated via setLatencySamples.

    Parameters:
      - "Amount" (id: sidechainAmount), 0..100 %, default 50 %. Duck
        DEPTH (musical pumping range, 100 % = -24 dB). PRIMARY.
      - "Duck Length" (id: duckLength), 50..1000 ms, default 250 ms.
        TOTAL duration of one duck envelope. Presets define their factory
        base length; selecting a preset restores it; the user's manual
        length then rules until the next preset selection.
      - "Shape" (id: release), 50..1000 ms, default 150 ms, skewed scale.
        Envelope SHAPE (plateau vs tail split within Duck Length).
      - "Offset" (id: sidechainOffset), int ms -30..+30, default 0,
        1 ms steps. Moves the duck envelope earlier/later relative to the
        BEAT. Negative values use lookahead (reported as plugin latency).

    (The former "Sidechain While Stopped" control was removed in 0.4.0:
    with no external sidechain, a stopped DAW timeline cannot advance a
    beat grid, so a while-stopped override had no honest audio meaning.
    Stopped state instead shows the parameter-driven envelope PREVIEW in
    the graph.)

    Transport behaviour: internal beat triggers fire while the host is
    PLAYING or RECORDING; stopped hosts produce no triggers (the graph
    shows the preview). No fake timing, no GUI-timer triggers.

    State: versioned ValueTree ("stateVersion"; 6 adds optional PumpCurve
    state without changing the existing PARAMS/APVTS root). Versions 1-5
    remain in legacy mode; unknown children are tolerated.
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include "DuckEngine.h"
#include "BeatScheduler.h"
#include "GraphData.h"
#include "PresetManager.h"
#include "TransportGate.h"
#include "PumpCurve.h"

//==============================================================================
class SideChainAudioProcessor : public juce::AudioProcessor,
                                private juce::AudioProcessorValueTreeState::Listener
{
public:
    SideChainAudioProcessor();
    ~SideChainAudioProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // Plugin latency = the fixed lookahead (DuckEngine::lookaheadSamples at
    // the current rate). Reported via setLatencySamples() in prepareToPlay;
    // constant regardless of the offset setting (the envelope delay line
    // absorbs the offset on the delayed timeline), so hosts see one stable
    // compensation value. Queried by the UI through latencySamplesForUi().
    int latencySamplesForUi() const noexcept
    {
        return sid::dsp::DuckEngine::lookaheadSamples (currentSampleRate);
    }

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return JucePlugin_Name; }
    bool acceptsMidi() const override                       { return false; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 0.0; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // PumpCurve state model (Phase B only; deliberately not connected to DSP).
    const sid::curve::PumpCurve& getPumpCurve() const noexcept { return pumpCurve_; }
    sid::curve::StateMode getCurveStateMode() const noexcept { return curveStateMode_; }
    bool setPumpCurvePoints (const sid::curve::Point* points, std::size_t count) noexcept;

    // APVTS listener: keeps the engine's envelope timing in sync with the
    // user-facing parameters (message thread).
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    // Push the user-facing Shape (ms; envelope SHAPE) into the engine.
    void applyReleaseToEngine();

    // Push the user-facing DUCK LENGTH (ms; total envelope duration) into
    // the engine. Called from the parameter listener (message thread).
    void applyDuckLengthToEngine();

    //==========================================================================
    // Presets. A preset is a named (depth, shape, base length) triple of
    // existing user parameters (see PresetManager.h). Applying one sets the
    // production parameters through the normal APVTS notification path
    // (setValueNotifyingHost, with change-gesture bracketing so hosts see
    // one logical gesture); the DSP and graph follow automatically. The
    // user's DUCK LENGTH survives until the next preset selection (a
    // preset restores its factory base length - conventional template
    // behaviour), and the preset identity stays value-derived ("Custom"
    // is never stored).

    // Apply a compiled-in factory preset by exact name. False if unknown.
    bool applyFactoryPreset (const juce::String& name);

    // Restore the plugin defaults (Amount 50 %, Shape 150 ms, Length 250 ms).
    void applyDefaultPreset();

    // Step to the previous (-1) or next (+1) factory preset through the SAME
    // parameter-notification path as applyFactoryPreset. Wraps at the ends.
    void stepPreset (int direction);

    // Display-only current-preset identity, derived from the LIVE parameter
    // values (never stored in state): exact match -> preset name,
    // plugin defaults -> "Default", anything else -> "Custom".
    juce::String getCurrentPresetDisplayName() const;

    //==========================================================================
    // Parameter access for the editor (APVTS attachment).
    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }

    // Live diagnostic (atomic; audio-safe).
    std::atomic<float> currentGainReductionDb { 0.0f };

    // Real-time graph transport (audio thread -> GUI). Single producer,
    // lock-free, pre-allocated. The editor drains it on a ~30 Hz timer.
    sid::graph::GraphFrameFifo graphFifo;

    // Reset the graph stream on plugin reinitialization (GUI reads a
    // sequence of zero frames after this so history settles to silence).
    void resetGraphStream();

#if defined (SIDECHAIN_HEADLESS_TEST)
    // Test-only engine access (headless test builds assert timing sync).
    const sid::dsp::DuckEngine& duckEngineForTest() const { return duckEngine; }
    const sid::transport::BeatScheduler& beatSchedulerForTest() const { return beatScheduler_; }
#endif

private:
    //==========================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Common preset-apply path: sets ALL preset parameters through the
    // proper notification mechanism (message thread, host-visible gesture).
    void applyPresetValues (float amountPercent, float releaseMs, float duckLengthMs);

    juce::AudioProcessorValueTreeState parameters;

    // Fixed-capacity data model. State mode records whether saved/restored
    // state explicitly contains PumpCurve data; it has no DSP effect in B.
    sid::curve::PumpCurve pumpCurve_;
    sid::curve::StateMode curveStateMode_ = sid::curve::StateMode::pumpCurve;

    // Cached raw parameter values, read on the audio thread via atomic load.
    std::atomic<float>* amountRawParameter  = nullptr;
    std::atomic<float>* releaseRawParameter = nullptr;
    std::atomic<float>* duckLengthRawParameter = nullptr; // ms, 50..1000
    std::atomic<float>* offsetRawParameter  = nullptr;  // int ms, -30..+30

    double currentSampleRate = 48000.0;

    // Main-path delay line (lookahead). Read/write on the audio thread only.
    std::vector<float> mainDelayL_;
    std::vector<float> mainDelayR_;
    int mainDelayPos_ = 0;

    sid::dsp::DuckEngine duckEngine;

    // Internal tempo-synced beat trigger (0.4.0). Advanced once per block
    // from the host playhead; fires into pendingTriggers_[].
    sid::transport::BeatScheduler beatScheduler_;

    // Sample offsets (from block start) of beats scheduled inside the
    // current block, filled before the per-sample loop. Small fixed
    // capacity: even at 250 BPM a quarter note is 10 ms (~640 samples at
    // 96k), far above any block size; capacity 8 is generous.
    static constexpr int kMaxTriggersPerBlock = 8;
    int pendingTriggerSamples_[kMaxTriggersPerBlock] {};
    int numPendingTriggers_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SideChainAudioProcessor)
};
