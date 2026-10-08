/*
    SideChainer (SideChain) - production processor implementation (0.4.0).

    Processing policy:
      - Main path:  main input -> lookahead delay -> shared ducking gain
        -> main output. NO auxiliary input; the trigger is generated
        internally from the DAW transport timeline.
      - The beat scheduler fires one duck envelope per quarter-note beat
        at the exact sample (host PPQ/BPM/timeInSamples), while the host
        transport is playing or recording.
      - Amount = 0 % => depth 0 dB => gain stays exactly 1.0 => main path
        is bit-transparent apart from the trivial gain multiply by 1.0.

    Audio-thread safety: no allocations, no locks, no I/O, no GUI calls.
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // State schema versions:
    //   1 = Phase 6/7 : sidechainAmount + release
    //   2 = Phase 8   : + sidechainWhileStopped (schema change -> bump)
    //   3 = 0.3.0     : + sidechainOffset PARAM
    //   4 = 0.3.0     : + duckLength PARAM
    //   5 = 0.4.0     : sidechainWhileStopped REMOVED (schema change ->
    //                   bump); presets now own a base duckLength.
    constexpr int kCurrentStateVersion = 5;
}

//==============================================================================
SideChainAudioProcessor::SideChainAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMS", createParameterLayout())
{
    amountRawParameter = parameters.getRawParameterValue ("sidechainAmount");
    releaseRawParameter = parameters.getRawParameterValue ("release");
    duckLengthRawParameter = parameters.getRawParameterValue ("duckLength");
    offsetRawParameter = parameters.getRawParameterValue ("sidechainOffset");
    jassert (amountRawParameter != nullptr && releaseRawParameter != nullptr
             && duckLengthRawParameter != nullptr && offsetRawParameter != nullptr);

    parameters.addParameterListener ("release", this);
    parameters.addParameterListener ("duckLength", this);
    applyReleaseToEngine();
    applyDuckLengthToEngine();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
SideChainAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // PRIMARY control: ducking depth. ID stable since Phase 3.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "sidechainAmount",
        "Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        50.0f));

    // DUCK LENGTH: TOTAL audible duck duration (attack + hold + 3 release
    // taus). 50..1000 ms, skewed so the musically dense short region keeps
    // resolution. Presets define their factory base length (restored on
    // preset selection); the user's manual length rules until the next
    // preset selection.
    juce::NormalisableRange<float> duckLengthRange (
        sid::dsp::DuckEngine::kDuckLengthMinMs, sid::dsp::DuckEngine::kDuckLengthMaxMs, 0.0f, 0.35f);
    duckLengthRange.setSkewForCentre (200.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "duckLength",
        "Duck Length",
        duckLengthRange,
        sid::dsp::DuckEngine::kDuckLengthDefaultMs));

    // SHAPE (historical id: release): plateau vs tail split within Duck
    // Length. 50..1000 ms, skewed so the musically dense 50..400 ms region
    // gets most of the travel. 150 ms default == the validated DSP.
    juce::NormalisableRange<float> releaseRange (
        sid::dsp::DuckEngine::kReleaseMinMs, sid::dsp::DuckEngine::kReleaseMaxMs, 0.0f, 0.3f);
    releaseRange.setSkewForCentre (200.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "release",
        "Shape",
        releaseRange,
        sid::dsp::DuckEngine::kReleaseDefaultMs));

    // OFFSET. Integer ms -30..+30, 1 ms UI steps. Moves the duck envelope
    // earlier (<0, lookahead) or later (>0) relative to the BEAT. Linear
    // range so each step is exactly 1 ms.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "sidechainOffset",
        "Offset",
        juce::NormalisableRange<float> (
            (float) sid::dsp::DuckEngine::kOffsetMinMs,
            (float) sid::dsp::DuckEngine::kOffsetMaxMs, 1.0f),
        0.0f));

    return layout;
}

//==============================================================================
// Push the user-facing Shape (ms) into the engine (envelope hold +
// recovery shape). Called from the message thread (parameter listener).
void SideChainAudioProcessor::applyReleaseToEngine()
{
    duckEngine.setReleaseTimes (sid::dsp::DuckEngine::kReleaseDefaultMs,
                                releaseRawParameter->load());
}

// DUCK LENGTH -> engine (total envelope duration). Message thread only.
void SideChainAudioProcessor::applyDuckLengthToEngine()
{
    duckEngine.setDuckLengthMs (duckLengthRawParameter->load());
}

void SideChainAudioProcessor::parameterChanged (const juce::String& parameterID, float)
{
    if (parameterID == "release")
        applyReleaseToEngine();
    else if (parameterID == "duckLength")
        applyDuckLengthToEngine();
}

//==============================================================================
// Presets. Values go through the parameters' own notification path
// (setValueNotifyingHost): the host sees one normal gesture, the editor's
// attachments update automatically, the APVTS listener re-syncs the
// engine, and the ValueTree sync happens through the standard APVTS
// mechanism. No direct/atomic mutation, no DSP access here.
// Audio safety: the engine's envelope timing is re-derived on the message
// thread and applied via coefficient writes that are per-sample safe.
void SideChainAudioProcessor::applyPresetValues (float amountPercent, float releaseMs,
                                                 float duckLengthMs)
{
    auto* amountParam     = parameters.getParameter ("sidechainAmount");
    auto* releaseParam    = parameters.getParameter ("release");
    auto* duckLengthParam = parameters.getParameter ("duckLength");
    jassert (amountParam != nullptr && releaseParam != nullptr && duckLengthParam != nullptr);
    if (amountParam == nullptr || releaseParam == nullptr || duckLengthParam == nullptr)
        return;

    amountParam->beginChangeGesture();
    releaseParam->beginChangeGesture();
    duckLengthParam->beginChangeGesture();

    amountParam->setValueNotifyingHost     (amountParam->convertTo0to1 (amountPercent));
    releaseParam->setValueNotifyingHost    (releaseParam->convertTo0to1 (releaseMs));
    duckLengthParam->setValueNotifyingHost (duckLengthParam->convertTo0to1 (duckLengthMs));

    amountParam->endChangeGesture();
    releaseParam->endChangeGesture();
    duckLengthParam->endChangeGesture();
}

bool SideChainAudioProcessor::applyFactoryPreset (const juce::String& name)
{
    const auto* preset = sid::presets::findByName (name);
    if (preset == nullptr)
        return false;

    applyPresetValues (preset->amountPercent, preset->releaseMs, preset->duckLengthMs);
    return true;
}

void SideChainAudioProcessor::applyDefaultPreset()
{
    applyPresetValues (50.0f, sid::dsp::DuckEngine::kReleaseDefaultMs,
                       sid::dsp::DuckEngine::kDuckLengthDefaultMs);
}

void SideChainAudioProcessor::stepPreset (int direction)
{
    const auto& table = sid::presets::factoryPresets();
    if (table.isEmpty())
        return;

    auto* amountParam     = parameters.getParameter ("sidechainAmount");
    auto* releaseParam    = parameters.getParameter ("release");
    auto* duckLengthParam = parameters.getParameter ("duckLength");
    if (amountParam == nullptr || releaseParam == nullptr || duckLengthParam == nullptr)
        return;

    const float amount  = dynamic_cast<juce::AudioParameterFloat*> (amountParam)->get();
    const float release = dynamic_cast<juce::AudioParameterFloat*> (releaseParam)->get();
    const float length  = dynamic_cast<juce::AudioParameterFloat*> (duckLengthParam)->get();

    // Anchor index for the step (see header comment). -1 == before the table.
    int anchor = -1;

    if (const auto* exact = sid::presets::findMatchingPreset (amount, release, length))
    {
        for (int i = 0; i < table.size(); ++i)
            if (&table.getReference (i) == exact) { anchor = i; break; }
    }
    else if (std::abs (amount - 50.0f) < 0.051f
             && std::abs (release - sid::dsp::DuckEngine::kReleaseDefaultMs) < 0.051f
             && std::abs (length - sid::dsp::DuckEngine::kDuckLengthDefaultMs) < 0.051f)
    {
        anchor = -1;                       // "Default" sits before the first preset
    }
    else
    {
        // Custom: anchor to the closest preset by Amount (ties -> lower).
        int best = 0;
        float bestDist = -1.0f;
        for (int i = 0; i < table.size(); ++i)
        {
            const float dist = std::abs (table.getReference (i).amountPercent - amount);
            if (bestDist < 0.0f || dist < bestDist) { bestDist = dist; best = i; }
        }
        anchor = best;
    }

    const int n = table.size();
    int target = anchor + (direction >= 0 ? 1 : -1);
    if (target < 0)      target = n - 1;   // wrap at the ends
    if (target >= n)     target = 0;

    const auto& preset = table.getReference (target);
    applyPresetValues (preset.amountPercent, preset.releaseMs, preset.duckLengthMs);
}

juce::String SideChainAudioProcessor::getCurrentPresetDisplayName() const
{
    const auto* amount  = dynamic_cast<const juce::AudioParameterFloat*> (
        parameters.getParameter ("sidechainAmount"));
    const auto* release = dynamic_cast<const juce::AudioParameterFloat*> (
        parameters.getParameter ("release"));
    const auto* length  = dynamic_cast<const juce::AudioParameterFloat*> (
        parameters.getParameter ("duckLength"));

    if (amount != nullptr && release != nullptr && length != nullptr)
        if (const auto* match = sid::presets::findMatchingPreset (
                amount->get(), release->get(), length->get()))
            return match->name;

    // Exactly the plugin defaults -> a "Default" identity is meaningful.
    if (amount != nullptr && release != nullptr && length != nullptr
        && std::abs (amount->get() - 50.0f) < 0.051f
        && std::abs (release->get() - sid::dsp::DuckEngine::kReleaseDefaultMs) < 0.051f
        && std::abs (length->get() - sid::dsp::DuckEngine::kDuckLengthDefaultMs) < 0.051f)
        return "Default";

    return "Custom";
}

//==============================================================================
void SideChainAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock); // engine is per-sample; any block size works

    currentSampleRate = sampleRate;
    duckEngine.prepare (sampleRate);
    beatScheduler_.setSampleRate (sampleRate);
    beatScheduler_.reset();

    // Main-path lookahead delay line (supports the negative offset range).
    const int look = sid::dsp::DuckEngine::lookaheadSamples (sampleRate);
    mainDelayL_.assign ((size_t) look + 1, 0.0f);
    mainDelayR_.assign ((size_t) look + 1, 0.0f);
    mainDelayPos_ = 0;

    // Report the lookahead latency to the host (AU/VST3 compensation).
    setLatencySamples (latencySamplesForUi());

    applyReleaseToEngine();    // prepare() resets coefficients; re-apply Shape
    applyDuckLengthToEngine(); // ... and Duck Length (prepare() uses defaults)
    resetGraphStream();
}

void SideChainAudioProcessor::resetGraphStream()
{
    // Drain the FIFO and push a short run of silence frames so the GUI
    // history settles to a clean zero baseline (used on prepare/reset).
    sid::graph::GraphFrame f;
    sid::graph::GraphFrame drained;
    while (graphFifo.pop (drained)) {}

    for (int i = 0; i < 128; ++i)
        graphFifo.push (f);
}

bool SideChainAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    // Product policy: strict stereo in/out. There is no auxiliary bus; any
    // (legacy) extra input the host presents is left untouched by not
    // requesting it (see BusesProperties in the constructor).
    return mainIn == juce::AudioChannelSet::stereo()
        && mainOut == juce::AudioChannelSet::stereo();
}

void SideChainAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    auto mainIn = getBusBuffer (buffer, true,  0);
    auto out    = getBusBuffer (buffer, false, 0);

    // ------------------------------------------------------------------
    // Host transport + musical timeline (playhead only; no inference).
    // ------------------------------------------------------------------
    bool  hostPlaying   = false;
    bool  hostRecording = false;
    double bpm          = 0.0;
    double ppqPosition  = -1.0;
    double timeInSamples = -1.0;

    if (auto* playHeadPtr = getPlayHead())
    {
        juce::AudioPlayHead::CurrentPositionInfo position;
        if (playHeadPtr->getCurrentPosition (position))
        {
            hostPlaying    = position.isPlaying;
            hostRecording  = position.isRecording;
            bpm            = position.bpm;
            ppqPosition    = position.ppqPosition;
            timeInSamples  = position.timeInSamples;
        }
    }

    const bool transportActive = sid::transport::transportActive (hostPlaying, hostRecording);

    // ------------------------------------------------------------------
    // 0.4.0 INTERNAL BEAT SCHEDULER: schedule every quarter-note beat of
    // this block at its exact sample offset (only while the transport
    // rolls; a stopped timeline produces no triggers).
    // ------------------------------------------------------------------
    numPendingTriggers_ = 0;
    if (transportActive
        && timeInSamples >= 0.0)
    {
        beatScheduler_.advance (
            (long long) timeInSamples, numSamples, bpm, ppqPosition,
            [this] (int sampleOffset)
            {
                if (numPendingTriggers_ < kMaxTriggersPerBlock)
                    pendingTriggerSamples_[numPendingTriggers_++] = sampleOffset;
            });
    }
    else
    {
        beatScheduler_.reset(); // stopped: no stale schedule survives
    }

    // ------------------------------------------------------------------
    // Depth from Amount (per-block atomic read; smoothed per-sample by the
    // envelope engine).
    // ------------------------------------------------------------------
    const float amount01 = juce::jlimit (0.0f, 1.0f,
                                         amountRawParameter->load (std::memory_order_relaxed) / 100.0f);
    const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (amount01);

    // ------------------------------------------------------------------
    // OFFSET: envelope scheduled at (beat + offset) on the delayed
    // timeline. The main path is delayed by the full lookahead (reported
    // to the host); the engine additionally delays its envelope by
    // (lookahead + offset) samples, so the offset is real, audible and
    // host-compensated.
    // ------------------------------------------------------------------
    duckEngine.setOffsetMs ((int) std::lround (
        offsetRawParameter->load (std::memory_order_relaxed)));

    // Per-block peak accumulators for the graph (aggregated once per block,
    // never per-sample GUI data).
    float inPeak = 0.0f, outPeak = 0.0f;

    const long long triggersBefore = duckEngine.getTriggerCount();
    int nextTriggerIdx = 0;

    const int look = sid::dsp::DuckEngine::lookaheadSamples (currentSampleRate);
    const bool delayActive = look > 0 && (int) mainDelayL_.size() > look;

    float* outL = out.getWritePointer (0);
    float* outR = (out.getNumChannels() > 1) ? out.getWritePointer (1) : outL;
    const float* inL = (mainIn.getNumChannels() > 0) ? mainIn.getReadPointer (0) : nullptr;
    const float* inR = (mainIn.getNumChannels() > 1) ? mainIn.getReadPointer (1) : inL;
    const bool hasMainInput = (inL != nullptr);

    for (int i = 0; i < numSamples; ++i)
    {
        // Internal trigger: fire the envelope at the EXACT beat sample.
        if (nextTriggerIdx < numPendingTriggers_
            && pendingTriggerSamples_[nextTriggerIdx] == i)
        {
            duckEngine.fireTrigger();
            ++nextTriggerIdx;
        }

        // One shared envelope gain from the trigger-driven engine for
        // BOTH channels. (No sidechain input exists any more.)
        const float gain = duckEngine.processSample (depthDb);

        // Main path through the lookahead delay (write-then-read = delay of
        // `look` samples). With offset == 0 the delayed audio is ducked
        // starting exactly at the beat; negative offsets pull the envelope
        // earlier on this same (host-compensated) timeline.
        float l = hasMainInput ? inL[i] : 0.0f;
        float r = hasMainInput ? inR[i] : 0.0f;

        if (delayActive)
        {
            const float delayedL = mainDelayL_[(size_t) mainDelayPos_];
            const float delayedR = mainDelayR_[(size_t) mainDelayPos_];
            mainDelayL_[(size_t) mainDelayPos_] = l;
            mainDelayR_[(size_t) mainDelayPos_] = r;
            mainDelayPos_ = (mainDelayPos_ + 1) % look;
            l = delayedL;
            r = delayedR;
        }

        outL[i] = l * gain;
        outR[i] = r * gain;

        inPeak  = juce::jmax (inPeak,  std::abs (l), std::abs (r));
        outPeak = juce::jmax (outPeak, std::abs (outL[i]), std::abs (outR[i]));
    }

    const bool triggerFiredThisBlock =
        duckEngine.getTriggerCount() != triggersBefore;

    // ------------------------------------------------------------------
    // Graph data: one pre-aggregated frame per block into the lock-free
    // FIFO. The frame carries the host beat grid (bpm/ppq) so the graph
    // can draw BEAT MARKERS from the real host timeline - no fake
    // animation. sidechainLevelDb is retired (no sidechain bus) and set
    // from the input as the analyzer's second trace.
    // ------------------------------------------------------------------
    sid::graph::GraphFrame frame;
    frame.inputLevelDb     = juce::Decibels::gainToDecibels (inPeak,  -100.0f);
    frame.sidechainLevelDb = frame.inputLevelDb;
    frame.gainReductionDb  = duckEngine.getCurrentReductionDb();   // <= 0
    frame.outputLevelDb    = juce::Decibels::gainToDecibels (outPeak, -100.0f);
    frame.triggerFired     = triggerFiredThisBlock;
    frame.bpm              = (float) bpm;   // beat-aware graph display
    graphFifo.push (frame);

    // Diagnostic readout (atomic store; no locking).
    currentGainReductionDb.store (duckEngine.getCurrentReductionDb(),
                                  std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* SideChainAudioProcessor::createEditor()
{
    // The editor lives in PluginEditor.cpp. Test builds that exercise the
    // processor headless link a tiny stub instead of the GUI sources.
#if defined (SIDECHAIN_HEADLESS_TEST)
    class HeadlessEditor : public juce::AudioProcessorEditor
    {
    public:
        explicit HeadlessEditor (SideChainAudioProcessor& p) : AudioProcessorEditor (&p) { setSize (8, 8); }
    };
    return new HeadlessEditor (*this);
#else
    return new SideChainAudioProcessorEditor (*this);
#endif
}

//==============================================================================
void SideChainAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Versioned state persistence (stateVersion 5 in 0.4.0).
    //
    // JUCE 6.1.3 syncs parameter -> ValueTree lazily (internal ~50 Hz timer;
    // flushParameterValuesToValueTree() is private). Without a wait, a host
    // saving state immediately after automation would serialize a STALE
    // value. Deterministic workaround: briefly pump the message loop so the
    // timer fires (hosts call getStateInformation on the message thread).
    if (! juce::MessageManager::existsAndIsCurrentThread())
    {
        // Extremely defensive fallback (should not happen in practice):
        // serialize whatever the tree currently holds.
        juce::MemoryOutputStream stream (destData, false);
        parameters.state.writeToStream (stream);
        return;
    }

    const juce::String paramIds[4] =
        { "sidechainAmount", "duckLength", "release", "sidechainOffset" };

    for (int i = 0; i < 20; ++i) // up to ~0.4 s, far more than one 50 Hz tick
    {
        bool allSynced = true;

        for (const auto& id : paramIds)
        {
            auto paramChild = parameters.state.getChildWithName ("PARAM");
            for (int c = 0; c < parameters.state.getNumChildren(); ++c)
            {
                auto child = parameters.state.getChild (c);
                if (child.getProperty ("id").toString() == id)
                {
                    paramChild = child;
                    break;
                }
            }

            const auto atomicValue = parameters.getRawParameterValue (id)->load();
            const auto treeValue = paramChild.getProperty ("value");

            if (treeValue.isVoid()
                || std::abs ((float) (double) treeValue - atomicValue) > 1.0e-4f)
                allSynced = false;
        }

        if (allSynced && i > 0)
            break;

        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        juce::Timer::callPendingTimersSynchronously();
    }

    // Stamp the state version (0.4.0 = 5) before serializing so every
    // state this build produces is identifiable.
    parameters.state.setProperty ("stateVersion", kCurrentStateVersion, nullptr);

    juce::MemoryOutputStream stream (destData, false);
    parameters.state.writeToStream (stream);
}

//==============================================================================
// State recovery policy (Phase 7A hardening, 0.4.0 update).
//
// Goal: malformed/corrupt/unexpected state must never crash the plugin,
// never put a NaN/Inf into the DSP, and never leave a parameter outside
// its documented range. Unknown PARAM children (e.g. the removed
// sidechainWhileStopped from v1-4 states) are ignored by APVTS - old
// sessions load safely. Missing values default.
namespace
{
    std::optional<float> sanitisedParamValue (const juce::ValueTree& state,
                                              const juce::String& paramId)
    {
        for (int c = 0; c < state.getNumChildren(); ++c)
        {
            const auto child = state.getChild (c);
            if (child.getProperty ("id").toString() != paramId)
                continue;

            const auto v = child.getProperty ("value");
            if (v.isVoid() || ! v.isDouble())
                return std::nullopt;

            const float f = (float) (double) v;
            if (! std::isfinite (f))
                return std::nullopt;
            return f;
        }
        return std::nullopt;
    }

    bool paramValueInRange (const juce::ValueTree& state,
                            const juce::String& paramId,
                            juce::RangedAudioParameter& param)
    {
        const auto v = sanitisedParamValue (state, paramId);
        if (! v.has_value())
            return false;

        const auto& range = param.getNormalisableRange();
        return *v >= range.start - 1.0e-6f && *v <= range.end + 1.0e-6f;
    }
}

void SideChainAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return; // nothing usable: keep current state (policy above)

    auto tree = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);

    // Sanity gate: only accept a tree typed like our own state ("PARAMS").
    if (! tree.isValid() || tree.getType() != juce::Identifier ("PARAMS"))
        return; // malformed XML/binary: ignore entirely, keep current state

    // Snapshot the values we intend to restore BEFORE replaceState, so a
    // single invalid field can be reset to default without discarding the
    // rest of the (otherwise valid) state.
    auto* amountParam = parameters.getParameter ("sidechainAmount");
    auto* releaseParam = parameters.getParameter ("release");
    auto* duckLengthParam = parameters.getParameter ("duckLength");
    auto* offsetParam = parameters.getParameter ("sidechainOffset");
    const bool amountOk = amountParam != nullptr
        && paramValueInRange (tree, "sidechainAmount", *amountParam);
    const bool releaseOk = releaseParam != nullptr
        && paramValueInRange (tree, "release", *releaseParam);
    const bool duckLengthOk = duckLengthParam != nullptr
        && paramValueInRange (tree, "duckLength", *duckLengthParam);
    const bool offsetOk = offsetParam != nullptr
        && paramValueInRange (tree, "sidechainOffset", *offsetParam);

    const float amountRestore = amountOk
        ? *sanitisedParamValue (tree, "sidechainAmount") : 50.0f;
    const float releaseRestore = releaseOk
        ? *sanitisedParamValue (tree, "release")
        : sid::dsp::DuckEngine::kReleaseDefaultMs;
    const float duckLengthRestore = duckLengthOk
        ? *sanitisedParamValue (tree, "duckLength")
        : sid::dsp::DuckEngine::kDuckLengthDefaultMs;
    const float offsetRestore = offsetOk
        ? *sanitisedParamValue (tree, "sidechainOffset") : 0.0f;

    // Version handling: stamp missing version; keep saved version if
    // present (a future version's parameters we recognise still load).
    if (! tree.hasProperty ("stateVersion"))
        tree.setProperty ("stateVersion", kCurrentStateVersion, nullptr);

    parameters.replaceState (tree);

    // Enforce the policy: anything missing/invalid/range-violating goes to
    // its default through the parameter object (which clamps, notifies and
    // syncs the ValueTree). Valid values stay untouched.
    if (! amountOk && amountParam != nullptr)
        amountParam->setValueNotifyingHost (
            amountParam->convertTo0to1 (amountRestore));
    if (! releaseOk && releaseParam != nullptr)
        releaseParam->setValueNotifyingHost (
            releaseParam->convertTo0to1 (releaseRestore));
    if (! duckLengthOk && duckLengthParam != nullptr)
        duckLengthParam->setValueNotifyingHost (
            duckLengthParam->convertTo0to1 (duckLengthRestore));
    if (! offsetOk && offsetParam != nullptr)
        offsetParam->setValueNotifyingHost (
            offsetParam->convertTo0to1 (offsetRestore));

    applyReleaseToEngine();    // re-sync engine with the (sanitised) Shape
    applyDuckLengthToEngine(); // re-sync engine with the (sanitised) Duck Length
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SideChainAudioProcessor();
}
