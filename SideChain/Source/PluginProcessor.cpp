/*
    SideChainer (SideChain) - production processor implementation (0.4.0;
    transport switch + live waveform scope in 0.4.2).

    Processing policy:
      - Main path:  main input -> lookahead delay -> shared ducking gain
        -> main output. NO auxiliary input; the trigger is generated
        internally from the DAW transport timeline.
      - The beat scheduler fires one duck envelope per quarter-note beat
        at the exact sample (host PPQ/BPM/timeInSamples), while the host
        transport is playing or recording. With the user-facing
        "ACTIVE ONLY DURING PLAY/REC" switch OFF the scheduler free-runs at
        the host tempo instead, so the effect operates in a stopped DAW.
      - Amount = 0 % => depth 0 dB => gain stays exactly 1.0 => main path
        is bit-transparent apart from the trivial gain multiply by 1.0.

    Audio-thread safety: no allocations, no locks, no I/O, no GUI calls.
    Nothing in the audio path reads the waveform scope; it is a write-only
    side channel to the editor.
*/

#if defined (__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#elif defined (_WIN32)
#include <windows.h>
#endif
#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cstring>

namespace
{
    // State schema versions:
    //   1 = Phase 6/7 : sidechainAmount + release
    //   2 = Phase 8   : + sidechainWhileStopped (schema change -> bump)
    //   3 = 0.3.0     : + sidechainOffset PARAM
    //   4 = 0.3.0     : + duckLength PARAM
    //   5 = 0.4.0     : sidechainWhileStopped removed; presets own base length.
    //   6 = Phase B/D : PUMPCURVE subtree and Smooth parameter. Legacy-mode
    //                   states remain serialized at schema 5 until explicitly
    //                   edited or reset into a PumpCurve.
    //   7 = 0.4.2     : + transportGate ("ACTIVE ONLY DURING PLAY/REC",
    //                   default ON); a second parameter change, so the version
    //                   bumps as it did for schemas 2-4. States written by
    //                   0.4.1 and earlier carry no such PARAM child and load
    //                   with the ON default.
    constexpr int kCurrentStateVersion = 7;

    // The Music-Prod plugin-auth edge function - the same endpoint the
    // existing Music-Prod plugin flow uses. Public URL; contains no secret.
    // It implements start / approve / deny / poll / entitlements / download /
    // logout / activate-license (see MusicProdAuth.h). Every answer is scoped
    // to the product the token was issued for; only "sidechain" answers count.
    const char* const kPluginAuthEndpoint =
        "https://wfpeajmdojcjqyrsnxbk.supabase.co/functions/v1/vyre-plugin-auth";

    // The PUBLIC version-check route of the existing release service, on the
    // same functions origin. It is the same endpoint Music-Prod Studio's
    // self-update uses; the product slug is the release catalog's key for this
    // plugin. Public read-only URL, no secret, no token: it answers with the
    // latest PUBLISHED version and nothing else (drafts and withdrawn releases
    // are never served).
    const char* const kUpdatesEndpoint =
        "https://wfpeajmdojcjqyrsnxbk.supabase.co/functions/v1/music-prod-studio-api/v1/updates";

    // The release catalog's product key for this plugin. One place only: the
    // auth flow, the version check and (later) downloads must never disagree.
    const char* const kProductSlug = "sidechain";

    // Token file location, mirroring the existing plugin architecture (the
    // token lives outside DAW state and is never serialized into a project).
    juce::String defaultAuthFilePath()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Music-Prod/SideChain/auth.json").getFullPathName();
    }

#if defined (SIDECHAIN_HEADLESS_TEST)
    // Headless test builds must never open a socket. A suite that wants to
    // exercise authentication injects its own mock transport; a processor
    // constructed without one gets this do-nothing transport. (Plugin
    // scanning is unaffected either way: no request is issued at
    // construction, only when the plugin is actually in use.)
    struct NullAuthTransport : public sid::auth::AuthTransport
    {
        sid::auth::StartResponse start (const juce::String&, const juce::String&, const juce::String&) override
        {
            sid::auth::StartResponse r;
            return r;
        }
        sid::auth::PollResponse poll (const juce::String&) override
        {
            sid::auth::PollResponse r;
            r.status = "error";
            return r;
        }
        sid::auth::EntitlementsResponse entitlements (const juce::String&) override
        {
            return {};   // neither ok nor authFailure: nothing is granted
        }
        bool logout (const juce::String&) override { return false; }
    };

    // Headless test builds must never open a socket for the update check either.
    // A suite that wants to exercise the version-check contract injects its own
    // transport; a processor constructed without one gets this transport, which
    // reports "cannot reach" - the state the UI must handle honestly.
    struct NullUpdateTransport : public sid::update::UpdateTransport
    {
        bool fetch (const juce::String&, const juce::String&, const juce::String&,
                    const juce::String&, sid::update::UpdateInfo&) override
        {
            return false;
        }
    };
#endif

    std::unique_ptr<sid::auth::AuthTransport> makeDefaultAuthTransport()
    {
#if defined (SIDECHAIN_HEADLESS_TEST)
        return std::make_unique<NullAuthTransport>();
#else
        return std::make_unique<sid::auth::HttpAuthTransport> (kPluginAuthEndpoint);
#endif
    }

    std::unique_ptr<sid::auth::AuthTransport> resolveAuthTransport (
        std::unique_ptr<sid::auth::AuthTransport> injected)
    {
        return injected != nullptr ? std::move (injected) : makeDefaultAuthTransport();
    }

    std::unique_ptr<sid::update::UpdateTransport> makeDefaultUpdateTransport()
    {
#if defined (SIDECHAIN_HEADLESS_TEST)
        return std::make_unique<NullUpdateTransport>();
#else
        return std::make_unique<sid::update::HttpUpdateTransport> (kUpdatesEndpoint);
#endif
    }

    std::unique_ptr<sid::update::UpdateTransport> resolveUpdateTransport (
        std::unique_ptr<sid::update::UpdateTransport> injected)
    {
        return injected != nullptr ? std::move (injected) : makeDefaultUpdateTransport();
    }
}

juce::String sid::presets::UserPresetLibrary::foldedName (const juce::String& name)
{
#if JUCE_MAC
    auto original = CFStringCreateWithCString (nullptr, name.toRawUTF8(), kCFStringEncodingUTF8);
    if (original == nullptr) return name.toLowerCase();
    auto folded = CFStringCreateMutableCopy (nullptr, 0, original);
    CFRelease (original);
    if (folded == nullptr) return name.toLowerCase();
    CFStringFold (folded, kCFCompareCaseInsensitive | kCFCompareNonliteral, nullptr);
    std::vector<char> bytes ((std::size_t) CFStringGetMaximumSizeForEncoding (CFStringGetLength (folded), kCFStringEncodingUTF8) + 1);
    const bool ok = CFStringGetCString (folded, bytes.data(), (CFIndex) bytes.size(), kCFStringEncodingUTF8);
    CFRelease (folded);
    return ok ? juce::String::fromUTF8 (bytes.data()) : name.toLowerCase();
#elif JUCE_WINDOWS
    const int size = LCMapStringEx (LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, name.toWideCharPointer(), -1, nullptr, 0, nullptr, nullptr, 0);
    if (size > 0)
    {
        std::vector<wchar_t> folded ((std::size_t) size);
        if (LCMapStringEx (LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, name.toWideCharPointer(), -1, folded.data(), size, nullptr, nullptr, 0) != 0)
            return juce::String (folded.data());
    }
#endif
    return name.toLowerCase();
}

//==============================================================================
SideChainAudioProcessor::SideChainAudioProcessor (
    std::unique_ptr<sid::auth::AuthTransport> authTransport,
    juce::String authFilePath,
    std::unique_ptr<sid::update::UpdateTransport> updateTransport)
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMS", createParameterLayout()),
      authManager_ (std::make_unique<sid::auth::AuthManager> (
                        resolveAuthTransport (std::move (authTransport)),
                        authFilePath.isNotEmpty() ? authFilePath : defaultAuthFilePath(),
                        kProductSlug,    // product identity (server allowlist key)
                        "SideChain",     // device_name (human-readable token record)
                        JucePlugin_VersionString)),
      // Version check (Plugin Versions): installed version is the COMPILED-IN
      // project version and the platform/architecture are this running build, so
      // the server answers with an installer the user can actually run. No
      // request is made here - the editor's timer triggers the first check.
      updateChecker_ (std::make_unique<sid::update::UpdateChecker> (
                        resolveUpdateTransport (std::move (updateTransport)),
                        kProductSlug,
                        JucePlugin_VersionString,
                        sid::update::platformForThisBuild(),
                        sid::update::architectureForThisBuild()))
{
    amountRawParameter = parameters.getRawParameterValue ("sidechainAmount");
    releaseRawParameter = parameters.getRawParameterValue ("release");
    duckLengthRawParameter = parameters.getRawParameterValue ("duckLength");
    offsetRawParameter = parameters.getRawParameterValue ("sidechainOffset");
    transportGateRawParameter = parameters.getRawParameterValue ("transportGate");
    jassert (amountRawParameter != nullptr && releaseRawParameter != nullptr
             && duckLengthRawParameter != nullptr && offsetRawParameter != nullptr
             && transportGateRawParameter != nullptr
             && parameters.getRawParameterValue ("smooth") != nullptr);

    parameters.addParameterListener ("release", this);
    parameters.addParameterListener ("duckLength", this);
    publishPumpCurveSnapshot();
    applyReleaseToEngine();
    applyDuckLengthToEngine();

    // ------------------------------------------------------------------
    // Demo / full-version wiring (0.5.0).
    //
    // The entitlement answer is mirrored into the audio gate by ONE atomic
    // store, from whichever thread verified it, so processBlock never reads
    // anything but atomics and never talks to the network.
    // ------------------------------------------------------------------
    authManager_->onEntitlementChanged = [this] (bool fullVersion)
    {
        licenseGate_.setFullVersion (fullVersion);
    };

    // Restore the persisted account token. This is a FILE READ only - no
    // network request is issued here, so plugin scanning can never block on
    // the licensing server - and it also restores a still-fresh, previously
    // SERVER-VERIFIED subscription answer (see AuthManager), so a signed-in
    // subscriber is not silently downgraded to demo time while offline.
    authManager_->loadPersistedToken();
    authManager_->setBackgroundPolling (true);

    // Apply whatever entitlement we already hold before the first block runs.
    licenseGate_.setFullVersion (authManager_->hasFullEntitlement());
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

    // Phase B model control: normalized smoothness amount, persisted with
    // the existing parameters and used by curve rendering and DSP evaluation.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "smooth",
        "Smooth",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        50.0f));

    // TRANSPORT GATE (0.4.2): "ACTIVE ONLY DURING PLAY/REC". ON (default) =
    // the internal beat clock runs only while the host transport plays or
    // records. OFF = the clock free-runs at the host tempo, so the effect
    // operates in a stopped DAW.
    //
    // New, stable id: the pre-0.4.0 "sidechainWhileStopped" parameter encoded
    // the INVERSE condition with the opposite default, so its id is NOT reused
    // (that would silently flip behaviour for 0.3.x sessions). Being a normal
    // APVTS parameter makes the switch host-automatable and serialized; a
    // state without it loads with this default, which is the required ON.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        "transportGate",
        "Active Only During Play/Rec",
        true));

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

void SideChainAudioProcessor::publishPumpCurveSnapshot() noexcept
{
    static_assert (sizeof (float) == sizeof (std::uint32_t), "snapshot stores IEEE float words");
    const std::lock_guard<std::mutex> writerLock (curveSnapshotWriterMutex_);
    const std::lock_guard<std::mutex> modelLock (curveModelMutex_);

    curveSnapshotVersion_.fetch_add (1, std::memory_order_acq_rel); // odd: publish in progress
    curveSnapshotCount_.store ((int) pumpCurve_.size(), std::memory_order_relaxed);
    curveSnapshotPumpMode_.store (curveStateMode_ == sid::curve::StateMode::pumpCurve,
                                  std::memory_order_relaxed);
    for (std::size_t i = 0; i < sid::curve::PumpCurve::kMaximumPoints; ++i)
    {
        const auto point = i < pumpCurve_.size() ? pumpCurve_[i] : sid::curve::Point {};
        const float x = (float) point.x, y = (float) point.y;
        std::uint32_t xBits = 0, yBits = 0;
        std::memcpy (&xBits, &x, sizeof (xBits));
        std::memcpy (&yBits, &y, sizeof (yBits));
        curveSnapshotXY_[i * 2].store (xBits, std::memory_order_relaxed);
        curveSnapshotXY_[i * 2 + 1].store (yBits, std::memory_order_relaxed);
    }
    curveSnapshotVersion_.fetch_add (1, std::memory_order_release); // even: complete
}

bool SideChainAudioProcessor::loadPumpCurveSnapshotForAudio (sid::curve::PumpCurve& curve) noexcept
{
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const auto before = curveSnapshotVersion_.load (std::memory_order_acquire);
        if ((before & 1u) != 0u)
            continue;

        const int count = curveSnapshotCount_.load (std::memory_order_relaxed);
        const bool pumpMode = curveSnapshotPumpMode_.load (std::memory_order_relaxed);
        sid::curve::Point points[sid::curve::PumpCurve::kMaximumPoints] {};
        if (count >= (int) sid::curve::PumpCurve::kMinimumPoints
            && count <= (int) sid::curve::PumpCurve::kMaximumPoints)
            for (int i = 0; i < count; ++i)
            {
                const auto xBits = curveSnapshotXY_[(std::size_t) i * 2].load (std::memory_order_relaxed);
                const auto yBits = curveSnapshotXY_[(std::size_t) i * 2 + 1].load (std::memory_order_relaxed);
                float x = 0.0f, y = 0.0f;
                std::memcpy (&x, &xBits, sizeof (x));
                std::memcpy (&y, &yBits, sizeof (y));
                points[i] = { x, y };
            }

        const auto after = curveSnapshotVersion_.load (std::memory_order_acquire);
        if (before != after || (after & 1u) != 0u)
            continue;

        sid::curve::PumpCurve candidate;
        if (! candidate.trySetPoints (points, (std::size_t) count))
            return false;
        curve = candidate;
        audioPumpMode_ = pumpMode;
        return true;
    }
    return false; // retain the last complete snapshot instead of waiting
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
                                                 float duckLengthMs,
                                                 const sid::curve::PumpCurve& curve)
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

    setPumpCurvePoints (curve.storage().data(), curve.size());
}

bool SideChainAudioProcessor::applyFactoryPreset (const juce::String& name)
{
    const auto* preset = sid::presets::findByName (name);
    if (preset == nullptr)
        return false;

    applyPresetValues (preset->amountPercent, preset->releaseMs, preset->duckLengthMs,
                       preset->pumpCurve);
    return true;
}

void SideChainAudioProcessor::applyDefaultPreset()
{
    const sid::curve::PumpCurve defaultCurve;
    applyPresetValues (50.0f, sid::dsp::DuckEngine::kReleaseDefaultMs,
                       sid::dsp::DuckEngine::kDuckLengthDefaultMs, defaultCurve);
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
    applyPresetValues (preset.amountPercent, preset.releaseMs, preset.duckLengthMs,
                       preset.pumpCurve);
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
    {
        const auto curve = getPumpCurve();
        const bool hasAuthoredCurve = getCurveStateMode() == sid::curve::StateMode::pumpCurve;
        const auto* match = hasAuthoredCurve
            ? sid::presets::findMatchingPreset (amount->get(), release->get(), length->get(), curve)
            : sid::presets::findMatchingPreset (amount->get(), release->get(), length->get());
        if (match != nullptr)
            return match->name;

        // Exactly the plugin defaults are meaningful regardless of whether
        // they came from an older legacy state or the canonical curve state.
        if (std::abs (amount->get() - 50.0f) < 0.051f
            && std::abs (release->get() - sid::dsp::DuckEngine::kReleaseDefaultMs) < 0.051f
            && std::abs (length->get() - sid::dsp::DuckEngine::kDuckLengthDefaultMs) < 0.051f
            && (! hasAuthoredCurve
                || sid::presets::samePumpCurve (curve, sid::curve::PumpCurve())))
            return "Default";
    }

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
    resetWaveformStream();

    // The plugin is now genuinely in use (not merely instantiated for a
    // plugin scan), so re-check the subscription in the background. Only a
    // restored sign-in triggers this, the request is asynchronous, and the
    // audio thread never waits for it. A failed request leaves the cached
    // answer in place (never a silent downgrade).
    if (authManager_->getState() == sid::auth::AuthManager::State::signedIn)
        authManager_->refreshEntitlements();
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

void SideChainAudioProcessor::resetWaveformStream()
{
    // Same policy for the scope: drop whatever was in flight and publish a
    // run of silence frames, so a restart can never splice an old trace onto
    // the new stream and present it as one continuous signal.
    sid::scope::WaveformFrame silence;
    sid::scope::WaveformFrame drained;
    while (waveformFifo.pop (drained)) {}

    for (int i = 0; i < sid::scope::WaveformHistory::kColumns; ++i)
        waveformFifo.push (silence);
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
    // DEMO / LICENCE GATE (0.5.0).
    //
    // A demo instance processes audio for 15 minutes from the moment that
    // instance entered demo mode. Once the session expires, processing stops
    // and the plugin outputs silence until the instance is restarted; a
    // restart begins a fresh 15-minute session. A server-verified entitlement
    // (paid Music-Prod+ subscription or valid licence key) bypasses this
    // entirely.
    //
    // Cost on the audio thread: two relaxed atomic loads plus one monotonic
    // counter read. No lock, no allocation, no file I/O, no network - the
    // entitlement itself is verified on the auth thread, which only ever
    // flips the flag read here.
    // ------------------------------------------------------------------
    if (! licenseGate_.processingAllowed())
    {
        out.clear();   // real silence; nothing downstream sees a bypass
        return;
    }

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

    // The user-facing "ACTIVE ONLY DURING PLAY/REC" switch (0.4.2): one atomic
    // read per block, then a pure decision on plain values (unit-testable
    // without a host, free on the audio thread).
    const bool gateOnlyDuringTransport =
        transportGateRawParameter->load (std::memory_order_relaxed) >= 0.5f;
    const bool triggersAllowed =
        sid::transport::triggerAllowed (gateOnlyDuringTransport, hostPlaying, hostRecording);

    // ------------------------------------------------------------------
    // INTERNAL BEAT SCHEDULER: schedule every quarter-note beat of this
    // block at its exact sample offset.
    //   - Gate ON (default): host-anchored only, exactly as in 0.4.0/0.4.1; a
    //     stopped timeline produces no triggers and resets the schedule.
    //   - Gate OFF: the effect must operate regardless of the transport, so a
    //     stopped (or unusable) host timeline hands over to the free-running
    //     clock. That clock arms one beat AHEAD, so flipping the switch does
    //     not punch a trigger into the middle of a bar.
    // ------------------------------------------------------------------
    numPendingTriggers_ = 0;

    const auto scheduleBeat = [this] (int sampleOffset)
    {
        if (numPendingTriggers_ < kMaxTriggersPerBlock)
            pendingTriggerSamples_[numPendingTriggers_++] = sampleOffset;
    };

    if (transportActive
        && timeInSamples >= 0.0)
    {
        beatScheduler_.advance (
            (long long) timeInSamples, numSamples, bpm, ppqPosition,
            scheduleBeat);
    }
    else if (triggersAllowed && ! gateOnlyDuringTransport)
    {
        // Gate OFF: free-run. The host schedule is dropped on the way in so a
        // later resumption cannot emit a beat that belongs to the stopped
        // timeline.
        if (! beatScheduler_.hasFreeRunSchedule())
            beatScheduler_.resetHostSchedule();

        beatScheduler_.advanceFreeRunning (numSamples, bpm, scheduleBeat);
    }
    else
    {
        beatScheduler_.reset(); // stopped (gate ON): no stale schedule survives
    }

    // ------------------------------------------------------------------
    // Depth from Amount and PumpCurve smoothness, plus the legacy-compatible
    // TIME duration. Reads are atomic once per block; the model snapshot is
    // fixed-capacity and never touched inside the sample loop.
    // ------------------------------------------------------------------
    const float amount01 = juce::jlimit (0.0f, 1.0f,
                                         amountRawParameter->load (std::memory_order_relaxed) / 100.0f);
    const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (amount01);
    const float smoothPercent = parameters.getRawParameterValue ("smooth")->load (std::memory_order_relaxed);
    const double duckLengthMs = duckLengthRawParameter->load (std::memory_order_relaxed);

    // ------------------------------------------------------------------
    // OFFSET: envelope scheduled at (beat + offset) on the delayed
    // timeline. The main path is delayed by the full lookahead (reported
    // to the host); the engine additionally delays its envelope by
    // (lookahead + offset) samples, so the offset is real, audible and
    // host-compensated.
    // ------------------------------------------------------------------
    duckEngine.setOffsetMs ((int) std::lround (
        offsetRawParameter->load (std::memory_order_relaxed)));
    loadPumpCurveSnapshotForAudio (audioPumpCurve_);
    duckEngine.setPumpCurve (audioPumpCurve_, smoothPercent);

    // Per-block peak accumulators for the graph (aggregated once per block,
    // never per-sample GUI data).
    float inPeak = 0.0f, outPeak = 0.0f;

    // ------------------------------------------------------------------
    // Live waveform scope: the block is split into up to kFramesPerBlock
    // equal chunks and each chunk publishes ONE min/max frame of the main
    // input (the signal the gain is applied to) and of the processed output
    // (what is heard). The accumulators are seeded with a value no real
    // sample can produce, so a chunk always owns at least one sample. This
    // is the only new per-sample work and it is two comparisons per channel.
    // ------------------------------------------------------------------
    const int scopeChunk = juce::jmax (1, numSamples / sid::scope::kFramesPerBlock);
    float scopeInMin  =  sid::scope::kAccumulatorSeed;
    float scopeInMax  = -sid::scope::kAccumulatorSeed;
    float scopeOutMin =  sid::scope::kAccumulatorSeed;
    float scopeOutMax = -sid::scope::kAccumulatorSeed;

    const auto publishScopeChunk = [this, &scopeInMin, &scopeInMax,
                                        &scopeOutMin, &scopeOutMax] ()
    {
        sid::scope::WaveformFrame frame;
        frame.inputMin  = sid::scope::sanitiseSample (scopeInMin);
        frame.inputMax  = sid::scope::sanitiseSample (scopeInMax);
        frame.outputMin = sid::scope::sanitiseSample (scopeOutMin);
        frame.outputMax = sid::scope::sanitiseSample (scopeOutMax);
        waveformFifo.push (frame);

        scopeInMin  =  sid::scope::kAccumulatorSeed;
        scopeInMax  = -sid::scope::kAccumulatorSeed;
        scopeOutMin =  sid::scope::kAccumulatorSeed;
        scopeOutMax = -sid::scope::kAccumulatorSeed;
    };

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
        // Scope chunk boundary: publish the completed chunk before the first
        // sample of the next one (a no-op for blocks smaller than
        // kFramesPerBlock, where every sample is its own chunk). A trailing
        // chunk shorter than scopeChunk is folded into the final publish, so a
        // block always produces at most kFramesPerBlock frames and no samples
        // are dropped from the display.
        if (i > 0 && (i % scopeChunk) == 0 && numSamples - i >= scopeChunk)
            publishScopeChunk();

        // Internal trigger: fire the envelope at the EXACT beat sample.
        if (nextTriggerIdx < numPendingTriggers_
            && pendingTriggerSamples_[nextTriggerIdx] == i)
        {
            if (audioPumpMode_)
                duckEngine.fireCurveTrigger();
            else
                duckEngine.fireTrigger();
            ++nextTriggerIdx;
        }

        // One shared envelope gain from the trigger-driven engine for
        // BOTH channels. (No sidechain input exists any more.)
        const float gain = audioPumpMode_
                         ? duckEngine.processCurveSample (depthDb, duckLengthMs)
                         : duckEngine.processSample (depthDb);

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

        // Scope envelopes: channel UNION, so anti-phase stereo cannot cancel
        // itself into a misleading flat trace.
        scopeInMin  = juce::jmin (scopeInMin,  l, r);
        scopeInMax  = juce::jmax (scopeInMax,  l, r);
        scopeOutMin = juce::jmin (scopeOutMin, outL[i], outR[i]);
        scopeOutMax = juce::jmax (scopeOutMax, outL[i], outR[i]);
    }

    // Publish the final chunk of the block (blocks are never empty: numSamples
    // > 0 here, and a zero-length block leaves the accumulators untouched).
    if (numSamples > 0)
        publishScopeChunk();

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
    // DEMO/LICENCE STATE IS DELIBERATELY ABSENT FROM THIS SERIALIZATION.
    // The remaining demo time and the entitlement are NOT written to plugin
    // state, so saving and reloading a DAW project can neither extend nor
    // reset a live demo session, and a hand-edited project file can never
    // grant the full version. The subscription is re-verified from the
    // server-side account contract (see AuthManager); the demo clock is
    // per-instance and monotonic (see LicenseGate.h).
    //
    // The configured JUCE 6.1.3 copyState() synchronously flushes parameter
    // values before copying. No message-loop pumping/reentrancy is needed,
    // including when Save Preset immediately follows a control edit.
    auto state = parameters.copyState();
    sid::curve::PumpCurve savedCurve;
    sid::curve::StateMode savedCurveMode;
    int versionForSave = kCurrentStateVersion;
    {
        const std::lock_guard<std::mutex> lock (curveModelMutex_);
        savedCurve = pumpCurve_;
        savedCurveMode = curveStateMode_;
        versionForSave = stateVersionForSave_;
    }
    // Keep legacy-mode snapshots explicitly at schema 5 instead of emitting
    // a schema-7 state without its required curve and relying on fallback.
    if (savedCurveMode == sid::curve::StateMode::legacy
        && versionForSave <= kCurrentStateVersion)
        versionForSave = 5;
    state.setProperty ("stateVersion", versionForSave, nullptr);
    for (int i = state.getNumChildren(); --i >= 0;)
        if (state.getChild (i).hasType ("PUMPCURVE"))
            state.removeChild (i, nullptr);

    if (savedCurveMode == sid::curve::StateMode::pumpCurve)
    {
        juce::ValueTree curveTree ("PUMPCURVE");
        curveTree.setProperty ("pointCount", (int) savedCurve.size(), nullptr);
        for (std::size_t i = 0; i < savedCurve.size(); ++i)
        {
            juce::ValueTree pointTree ("POINT");
            pointTree.setProperty ("x", savedCurve[i].x, nullptr);
            pointTree.setProperty ("y", savedCurve[i].y, nullptr);
            curveTree.addChild (pointTree, -1, nullptr);
        }
        state.addChild (curveTree, -1, nullptr);
    }

    juce::MemoryOutputStream stream (destData, false);
    state.writeToStream (stream);
}

//==============================================================================
// State recovery policy (Phase 7A hardening, extended for schema 6).
//
// Goal: malformed/corrupt/unexpected state must never crash the plugin,
// never put a NaN/Inf into a parameter, and never leave a parameter outside
// its documented range. Unknown PARAM children remain ignored by APVTS.
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

    bool readPumpCurve (const juce::ValueTree& state, sid::curve::PumpCurve& curve)
    {
        juce::ValueTree found;
        int matches = 0;
        for (int i = 0; i < state.getNumChildren(); ++i)
            if (state.getChild (i).hasType ("PUMPCURVE"))
            {
                found = state.getChild (i);
                ++matches;
            }

        if (matches != 1 || found.getNumProperties() != 1
            || ! found.hasProperty ("pointCount"))
            return false;
        const auto countValue = found.getProperty ("pointCount");
        if (! countValue.isInt())
            return false;
        const int count = (int) countValue;
        if (count < (int) sid::curve::PumpCurve::kMinimumPoints
            || count > (int) sid::curve::PumpCurve::kMaximumPoints
            || found.getNumChildren() != count)
            return false;

        sid::curve::Point points[sid::curve::PumpCurve::kMaximumPoints] {};
        for (int i = 0; i < count; ++i)
        {
            const auto point = found.getChild (i);
            if (! point.hasType ("POINT") || point.getNumProperties() != 2
                || ! point.hasProperty ("x") || ! point.hasProperty ("y"))
                return false;
            const auto x = point.getProperty ("x");
            const auto y = point.getProperty ("y");
            if (! x.isDouble() || ! y.isDouble())
                return false;
            points[i] = { (double) x, (double) y };
        }
        return curve.trySetPoints (points, (std::size_t) count);
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
    auto* smoothParam = parameters.getParameter ("smooth");
    const bool smoothOk = smoothParam != nullptr
        && paramValueInRange (tree, "smooth", *smoothParam);
    // The transport gate is a bool parameter: a state from 0.4.1 or earlier has
    // no such child at all, which is exactly the "missing" case that restores
    // the documented default (ON).

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
    const float smoothRestore = smoothOk
        ? *sanitisedParamValue (tree, "smooth") : 50.0f;
    auto* transportGateParam = parameters.getParameter ("transportGate");
    const bool transportGateOk = transportGateParam != nullptr
        && paramValueInRange (tree, "transportGate", *transportGateParam);
    const float transportGateRestore = transportGateOk
        ? *sanitisedParamValue (tree, "transportGate") : 1.0f;   // default ON

    // Only a valid schema 6+ PUMPCURVE activates the new shape engine.
    // Missing or malformed curve data selects the legacy envelope deterministically;
    // schemas 1-5 remain on their established Release/duckLength behavior.
    int savedVersion = 0;
    const auto versionValue = tree.getProperty ("stateVersion");
    if (versionValue.isInt())
        savedVersion = (int) versionValue;
    if (savedVersion < 1)
        savedVersion = 1;
    sid::curve::PumpCurve loadedCurve;
    const bool curveValid = savedVersion >= 6 && readPumpCurve (tree, loadedCurve);
    {
        const std::lock_guard<std::mutex> lock (curveModelMutex_);
        curveStateMode_ = curveValid ? sid::curve::StateMode::pumpCurve
                                     : sid::curve::StateMode::legacy;
        pumpCurve_ = curveValid ? loadedCurve : sid::curve::PumpCurve();
        stateVersionForSave_ = savedVersion > kCurrentStateVersion
            ? savedVersion : (curveValid ? kCurrentStateVersion : 5);
    }

    // Remove curve data after validation: only the fixed-capacity model is
    // retained, and malformed curve children cannot survive a fallback.
    for (int i = tree.getNumChildren(); --i >= 0;)
        if (tree.getChild (i).hasType ("PUMPCURVE"))
            tree.removeChild (i, nullptr);

    // The APVTS root remains unchanged; its state tree tolerates other
    // unknown children. A missing version is treated as legacy schema 1.
    if (! tree.hasProperty ("stateVersion"))
    {
        savedVersion = 1;
        const std::lock_guard<std::mutex> lock (curveModelMutex_);
        stateVersionForSave_ = 1;
        tree.setProperty ("stateVersion", savedVersion, nullptr);
    }

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
    if (! smoothOk && smoothParam != nullptr)
        smoothParam->setValueNotifyingHost (
            smoothParam->convertTo0to1 (smoothRestore));
    if (! transportGateOk && transportGateParam != nullptr)
        transportGateParam->setValueNotifyingHost (
            transportGateParam->convertTo0to1 (transportGateRestore));

    applyReleaseToEngine();    // re-sync legacy envelope with the (sanitised) Shape
    applyDuckLengthToEngine(); // re-sync engine timing with the (sanitised) Duck Length
    publishPumpCurveSnapshot();
}

bool SideChainAudioProcessor::setPumpCurvePoints (const sid::curve::Point* points,
                                                    std::size_t count) noexcept
{
    {
        const std::lock_guard<std::mutex> lock (curveModelMutex_);
        if (! pumpCurve_.trySetPoints (points, count))
            return false;
        curveStateMode_ = sid::curve::StateMode::pumpCurve;
        stateVersionForSave_ = kCurrentStateVersion;
    }
    publishPumpCurveSnapshot();
    return true;
}

juce::Result SideChainAudioProcessor::saveUserPreset (
    const sid::presets::UserPresetLibrary& library, const juce::String& name, juce::File& file)
{
    juce::MemoryBlock bytes;
    getStateInformation (bytes);
    auto result = library.save (name, juce::ValueTree::readFromData (bytes.getData(), bytes.getSize()), file);
    if (result.wasOk()) { userPresetName_ = name; userPresetState_ = bytes; }
    return result;
}

juce::Result SideChainAudioProcessor::loadUserPreset (
    const sid::presets::UserPresetLibrary& library, const juce::File& file)
{
    juce::String name;
    juce::ValueTree state;
    auto result = library.read (file, name, state);
    if (result.failed()) return result;
    const auto version = state.getProperty ("stateVersion");
    if (! version.isInt() || (int) version < 1 || (int) version > kCurrentStateVersion)
        return juce::Result::fail ("This preset uses an unsupported sound-state version.");
    const juce::String ids[] = { "sidechainAmount", "duckLength", "release", "sidechainOffset", "smooth", "transportGate" };
    for (const auto& id : ids)
    {
        int count = 0;
        for (int i = 0; i < state.getNumChildren(); ++i)
            if (state.getChild (i).getProperty ("id").toString() == id) ++count;
        if (count > 1 || (count == 1 && ! paramValueInRange (state, id, *parameters.getParameter (id))))
            return juce::Result::fail ("The preset contains an invalid parameter value.");
        if (count == 0 && (int) version >= 7)
            return juce::Result::fail ("The preset is missing a sound parameter.");
    }
    if ((int) version >= 6)
    {
        sid::curve::PumpCurve curve;
        if (! readPumpCurve (state, curve))
            return juce::Result::fail ("The preset contains an invalid curve.");
    }
    juce::MemoryBlock bytes;
    { juce::MemoryOutputStream stream (bytes, false); state.writeToStream (stream); }
    setStateInformation (bytes.getData(), (int) bytes.getSize()); // reuse all migration rules
    // Project restoration remains unchanged. Preset selection additionally
    // brackets host-visible parameter gestures so automation/attachments follow.
    for (const auto& id : ids)
    {
        auto* parameter = parameters.getParameter (id);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->getValue());
        parameter->endChangeGesture();
    }
    updateHostDisplay();
    userPresetName_ = name;
    getStateInformation (userPresetState_);
    return juce::Result::ok();
}

juce::String SideChainAudioProcessor::getCurrentUserPresetName() const
{
    if (userPresetName_.isEmpty()) return {};
    juce::MemoryBlock current;
    const_cast<SideChainAudioProcessor*> (this)->getStateInformation (current);
    const auto a = juce::ValueTree::readFromData (current.getData(), current.getSize());
    const auto b = juce::ValueTree::readFromData (userPresetState_.getData(), userPresetState_.getSize());
    return a.isEquivalentTo (b) ? userPresetName_ : juce::String();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SideChainAudioProcessor();
}
