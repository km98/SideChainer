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
      - "Active Only During Play/Rec" (id: transportGate), bool, default ON.
        ON: the internal beat clock runs only while the host transport plays
        or records. OFF: it free-runs at the host tempo so the effect operates
        in a stopped DAW. Serializes with plugin state; absent in older
        sessions, which therefore load with the default ON.

    Transport behaviour: internal beat triggers fire while the host is
    PLAYING or RECORDING; stopped hosts produce no triggers (the graph
    shows the preview). No fake timing, no GUI-timer triggers. The
    transportGate switch can relax this to free-running (see above).

    Visualization: the live waveform layer behind the curve is fed by a
    second lock-free FIFO (sid::scope::WaveformFifo) carrying per-chunk
    min/max envelopes of the real main input and the real processed output.
    It is display-only: nothing in the audio path depends on it.

    State: versioned ValueTree ("stateVersion"; 6 adds optional PumpCurve
    state without changing the existing PARAMS/APVTS root, 7 adds the
    transportGate parameter). Versions 1-5 remain in legacy mode; unknown
    children are tolerated.

    DEMO / FULL VERSION (0.5.0): the processor owns the demo session gate
    (sid::license::DemoGate) and the Music-Prod entitlement manager
    (sid::auth::AuthManager). A user WITH a server-verified entitlement
    (active paid Music-Prod+ subscription, or a valid Music-Prod licence key)
    is never time-limited. Without one, each plugin INSTANCE processes audio
    for 15 minutes and then stops, outputting silence until the instance is
    restarted - restarting grants a fresh 15-minute session. There is no
    permanent trial and no persistent countdown. The demo session is
    deliberately NOT part of the plugin state: saving/reloading a project
    neither extends nor resets it. See LicenseGate.h for the full model.

    Entitlement ownership: the AuthManager lives HERE, not in the editor, so a
    signed-in subscriber gets the full version even when no editor window was
    ever opened. The audio thread never calls into it - it only reads one
    atomic flag and one monotonic counter (see processBlock).
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <mutex>
#include "DuckEngine.h"
#include "BeatScheduler.h"
#include "GraphData.h"
#include "PresetManager.h"
#include "UserPresetLibrary.h"
#include "TransportGate.h"
#include "WaveformData.h"
#include "PumpCurve.h"
#include "LicenseGate.h"
#include "MusicProdAuth.h"
#include "UpdateCheck.h"

//==============================================================================
class SideChainAudioProcessor : public juce::AudioProcessor,
                                private juce::AudioProcessorValueTreeState::Listener
{
public:
    // Production constructs with no arguments. Tests may inject an auth
    // transport (so the entitlement routes are exercised without a network),
    // an auth-file path (so a real user token is never read) and an update
    // transport (so the version-check contract is exercised without a network).
    explicit SideChainAudioProcessor (std::unique_ptr<sid::auth::AuthTransport> authTransport = nullptr,
                                      juce::String authFilePath = {},
                                      std::unique_ptr<sid::update::UpdateTransport> updateTransport = nullptr);
    ~SideChainAudioProcessor() override = default;

    //==========================================================================
    // Demo session + entitlement state (0.5.0).

    // The demo gate is read on the audio thread (atomics only) and displayed
    // by the editor. Nothing here is serialized into plugin state.
    sid::license::DemoGate& getDemoGate() noexcept             { return licenseGate_; }
    const sid::license::DemoGate& getDemoGate() const noexcept { return licenseGate_; }

    // Music-Prod account + entitlement manager. Owned by the processor; the
    // audio thread never calls into it (its methods take a mutex).
    sid::auth::AuthManager& getAuthManager() noexcept { return *authManager_; }

    // Version-check (Plugin Versions). Owned by the processor so the check
    // interval belongs to the INSTANCE and survives opening/closing the editor,
    // but no request is issued until the UI asks (see refreshIfDue) - plugin
    // scanning and headless use therefore never open a socket. The audio thread
    // never calls into it.
    sid::update::UpdateChecker& getUpdateChecker() noexcept { return *updateChecker_; }

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

    // PumpCurve state model. The model is control/state-thread owned; the DSP
    // consumes a compact validated atomic snapshot at block boundaries.
    sid::curve::PumpCurve getPumpCurve() const noexcept
    {
        const std::lock_guard<std::mutex> lock (curveModelMutex_);
        return pumpCurve_;
    }
    sid::curve::StateMode getCurveStateMode() const noexcept
    {
        const std::lock_guard<std::mutex> lock (curveModelMutex_);
        return curveStateMode_;
    }
    bool setPumpCurvePoints (const sid::curve::Point* points, std::size_t count) noexcept;

    // APVTS listener: keeps the legacy envelope timing in sync with Release
    // and TIME parameters (message thread).
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    // Push Release timing into the legacy envelope. Schema 6 PumpCurve shape
    // is authored by the normalized curve and leaves Release semantics unchanged
    // for schemas 1-5.
    void applyReleaseToEngine();

    // Push the user-facing DUCK LENGTH (ms; total envelope duration) into
    // the engine. Called from the parameter listener (message thread).
    void applyDuckLengthToEngine();
    void publishPumpCurveSnapshot() noexcept;
    bool loadPumpCurveSnapshotForAudio (sid::curve::PumpCurve& curve) noexcept;

    //==========================================================================
    // Presets. Each preset retains its named Amount/Shape/base Length values
    // and carries a validated PumpCurve authored from those legacy envelope
    // settings (see PresetManager.h). Applying one sets production
    // parameters through the normal APVTS notification path (setValueNotifyingHost,
    // with change-gesture bracketing so hosts see
    // one logical gesture); the DSP and graph follow automatically. The
    // user's DUCK LENGTH survives until the next preset selection (a
    // preset restores its factory base length - conventional template
    // behaviour), and the preset identity stays value-derived ("Custom"
    // is never stored).

    // User presets reuse the project-state serializer/migration. File I/O is
    // explicitly control-thread only; no preset-library work in processBlock.
    juce::Result saveUserPreset (const sid::presets::UserPresetLibrary& library,
                                const juce::String& name, juce::File& savedFile);
    juce::Result loadUserPreset (const sid::presets::UserPresetLibrary& library,
                                const juce::File& file);
    juce::String getCurrentUserPresetName() const;

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

    // Real-time waveform scope transport (audio thread -> GUI) for the live
    // input/output traces drawn behind the curve. Same guarantees as
    // graphFifo: fixed capacity, no allocation, no locks, no GUI calls on the
    // audio thread.
    sid::scope::WaveformFifo waveformFifo;

    // Reset the graph stream on plugin reinitialization (GUI reads a
    // sequence of zero frames after this so history settles to silence).
    void resetGraphStream();

    // Same for the waveform scope: drains the FIFO and pushes silence frames,
    // so a restart / transport jump can never splice a stale trace onto a new
    // one and present it as one continuous signal.
    void resetWaveformStream();

#if defined (SIDECHAIN_HEADLESS_TEST)
    // Test-only engine access (headless test builds assert timing sync).
    const sid::dsp::DuckEngine& duckEngineForTest() const { return duckEngine; }
    const sid::transport::BeatScheduler& beatSchedulerForTest() const { return beatScheduler_; }
#endif

private:
    //==========================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Common preset-apply path: sets all established parameters through the
    // proper host-visible gesture, then installs the validated schema-6 curve.
    void applyPresetValues (float amountPercent, float releaseMs, float duckLengthMs,
                            const sid::curve::PumpCurve& curve);

    juce::AudioProcessorValueTreeState parameters;

    // Demo session gate (atomics only on the audio thread) and the Music-Prod
    // entitlement manager. Declared in this order so the constructor can wire
    // the gate to the entitlement as soon as the manager exists.
    sid::license::DemoGate licenseGate_;
    std::unique_ptr<sid::auth::AuthManager> authManager_;
    std::unique_ptr<sid::update::UpdateChecker> updateChecker_;

    juce::String userPresetName_;
    juce::MemoryBlock userPresetState_; // display identity, never serialized

    // Fixed-capacity editable model, owned by the state/control thread.
    sid::curve::PumpCurve pumpCurve_;
    sid::curve::StateMode curveStateMode_ = sid::curve::StateMode::pumpCurve;
    // Current schema version (kCurrentStateVersion in the .cpp: 7 in 0.4.2).
    // Retain future versions; legacy mode normalizes to schema 5.
    int stateVersionForSave_ = 7;
    mutable std::mutex curveModelMutex_; // control/state threads only, never audio
    std::mutex curveSnapshotWriterMutex_; // serializes snapshot publishers, never audio

    // Atomic-scalar seqlock snapshot: all scalar accesses are atomic so
    // readers can retry without locks or data races; audio copies one bounded
    // coherent version once per block.
    std::atomic<std::uint32_t> curveSnapshotVersion_ { 0 };
    std::array<std::atomic<std::uint32_t>, sid::curve::PumpCurve::kMaximumPoints * 2> curveSnapshotXY_ {};
    std::atomic<int> curveSnapshotCount_ { 0 };
    std::atomic<bool> curveSnapshotPumpMode_ { true };
    sid::curve::PumpCurve audioPumpCurve_;
    bool audioPumpMode_ = true;

    // Cached raw parameter values, read on the audio thread via atomic load.
    std::atomic<float>* amountRawParameter  = nullptr;
    std::atomic<float>* releaseRawParameter = nullptr;
    std::atomic<float>* duckLengthRawParameter = nullptr; // ms, 50..1000
    std::atomic<float>* offsetRawParameter  = nullptr;  // int ms, -30..+30
    std::atomic<float>* transportGateRawParameter = nullptr; // bool, 1 = ON

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
