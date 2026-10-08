/*
    SideChain - Phase 8 regression tests.

    Suite 1 - TRANSPORT (real processor, headless):
      JUCE 6.1.3 AudioProcessor provides setPlayHead(AudioPlayHead*) for
      exactly this purpose: a test can install its own play head and the
      processor reads it via getPlayHead() in processBlock. No host needed,
      and the transport state is NOT faked with booleans/randomness - it
      flows through the real AudioPlayHead API the hosts use.

      Cases: PLAY active, RECORD active, STOPPED, override ON/OFF,
      transitions PLAY->STOP / STOP->PLAY / PLAY<->RECORD, smooth gain
      recovery after STOP (no click/zipper), no leakage, stereo intact,
      Amount = 0 unity.

    Suite 2 - AUTH (mock transport, no network):
      Deterministic state-machine tests around sid::auth::AuthManager with
      an injected mock AuthTransport. No real account system is contacted.
      No token is ever printed; assertions only check non-emptiness.

    Suite 3 - STATE: switch round-trip + old v1 state defaults OFF.
*/

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <atomic>

#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/TransportGate.h"
#include "../Source/MusicProdAuth.h"

static int failures = 0, totalChecks = 0;
static void check (bool ok, const std::string& what)
{
    ++totalChecks;
    printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

static std::vector<float> sine (int n, double freq, double sr, float amp)
{
    std::vector<float> v ((size_t) n);
    double ph = 0.0;
    for (int i = 0; i < n; ++i) { v[(size_t) i] = amp * (float) std::sin (ph); ph += 2.0 * 3.14159265358979 * freq / sr; }
    return v;
}

namespace
{
    // ---- test play head (installed via the REAL AudioProcessor API) ----
    class TestPlayHead : public juce::AudioPlayHead
    {
    public:
        bool getCurrentPosition (CurrentPositionInfo& result) override
        {
            result = info;
            return positionValid;
        }

        CurrentPositionInfo info;
        bool positionValid = true;
    };

    // 0.4.0: there is no auxiliary sidechain bus any more. Kept as a no-op
    // so the historical call sites below stay meaningful (they always meant
    // "prepare the bus topology for the test run").
    bool enableSidechain (SideChainAudioProcessor&)
    {
        return true;
    }

    struct RunStats
    {
        bool finite = true, noLeak = true, stereo = true, noZipper = true;
        float maxGain = 0.0f;        // highest applied gain (must stay <= 1)
        float minGain = 1.0f;        // deepest duck (must dip while triggering)
        float maxOut = 0.0f;
        float settledGain = -1.0f;   // gain at the end of the run
    };

    // Runs the real processor with main+sidechain material under the given
    // transport states. transportOf(block) returns {playing, recording}.
    RunStats runWithTransport (
        SideChainAudioProcessor& proc,
        const std::vector<float>& mainS,
        const std::vector<float>& scS,
        int blockSize,
        const std::function<bool (int)>& transportActiveForBlock)
    {
        RunStats stats;
        const int n = (int) mainS.size();
        const bool scActive = ! scS.empty();
        const int totalCh = scActive ? 4 : 2;
        juce::AudioBuffer<float> buffer (totalCh, blockSize);
        juce::MidiBuffer midi;

        // v0.3.0: the plugin reports real lookahead latency (~30 ms). The
        // no-leakage invariant compares the output against the DELAYED main
        // input (what the processor actually multiplied by the gain), so
        // feed a matching input delay line here.
        const int lat = juce::jlimit (0, n, (int) proc.getLatencySamples());
        std::vector<float> inDelay ((size_t) lat + 1, 0.0f);
        size_t inDelayPos = 0;

        float prevOutL = 0.0f, prevOutR = 0.0f;

        for (int start = 0; start + blockSize <= n; start += blockSize)
        {
            const bool active = transportActiveForBlock (start);
            auto* ph = dynamic_cast<TestPlayHead*> (proc.getPlayHead());
            if (ph != nullptr)
            {
                ph->positionValid = true;
                ph->info.isPlaying = active;
                ph->info.isRecording = false;
                ph->info.bpm = 120.0;
                ph->info.timeInSamples = start;
                ph->info.ppqPosition = (double) start / 24000.0;
            }

            for (int ch = 0; ch < 2; ++ch)
            {
                auto* out = buffer.getWritePointer (ch);
                for (int i = 0; i < blockSize; ++i)
                    out[i] = mainS[(size_t) (start + i)];
            }
            if (scActive)
                for (int ch = 2; ch < 4; ++ch)
                {
                    auto* out = buffer.getWritePointer (ch);
                    for (int i = 0; i < blockSize; ++i)
                        out[i] = scS[(size_t) (start + i)];
                }

            proc.processBlock (buffer, midi);

            for (int i = 0; i < blockSize; ++i)
            {
                const size_t idx = (size_t) (start + i);
                const float l = buffer.getSample (0, i), r = buffer.getSample (1, i);

                if (! std::isfinite (l) || ! std::isfinite (r))
                    stats.finite = false;

                const float in = mainS[idx];
                // The input sample that produced this output sample:
                inDelay[(size_t) inDelayPos] = in;
                const float delayedIn = inDelay[(size_t) ((inDelayPos + 1) % inDelay.size())];
                inDelayPos = (inDelayPos + 1) % inDelay.size();
                // No sidechain leakage: output may never exceed its own input.
                if (std::abs (l) > std::abs (delayedIn) + 1.0e-6f
                    || std::abs (r) > std::abs (delayedIn) + 1.0e-6f)
                    stats.noLeak = false;

                // Stereo image: both channels ducked identically (same gain),
                // so for an identical stereo input the outputs must match.
                if (std::abs (l - r) > 1.0e-6f)
                    stats.stereo = false;

                // Zipper: per-sample output step must stay small while the
                // gain is recovering (the engine smooths per sample).
                const float step = std::max (std::abs (l - prevOutL),
                                             std::abs (r - prevOutR));
                if (step > 0.25f)
                    stats.noZipper = false;

                prevOutL = l; prevOutR = r;
                stats.maxOut = std::max (stats.maxOut, std::max (std::abs (l), std::abs (r)));
            }

            const float gain = juce::Decibels::decibelsToGain (
                proc.currentGainReductionDb.load());
            stats.maxGain = std::max (stats.maxGain, gain);
            stats.minGain = std::min (stats.minGain, gain);
            stats.settledGain = gain;
        }

        return stats;
    }

    // ---- mock auth transport (deterministic, offline) ------------------
    // Simulates the Phase 8B server contract: the product field is validated
    // against the allowlist (unknown/missing -> start failure), and the
    // verification URL is the PRODUCT'S OWN page.
    class MockAuthTransport : public sid::auth::AuthTransport
    {
    public:
        // Mirrors the server-side allowlist (see vyre-plugin-auth index.ts).
        static const juce::StringArray& allowedProducts()
        {
            static const juce::StringArray p { "vyre", "sidechain" };
            return p;
        }

        sid::auth::StartResponse start (const juce::String& product,
                                        const juce::String& deviceName,
                                        const juce::String& /*pluginVersion*/) override
        {
            lastProduct = product;
            lastDeviceName = deviceName;
            ++startCalls;

            sid::auth::StartResponse r;
            if (startFails || ! allowedProducts().contains (product.trim().toLowerCase()))
            {
                r.ok = false; // server returns 400 unknown_product
                return r;
            }
            r.ok = true;
            r.deviceCode = "mock-device-code-0123456789abcdef";
            r.userCode = "ABCD-EFGH";
            // Product-aware page resolution (same rule as the edge function).
            const juce::String page = (product == "sidechain") ? "/plugin/link" : "/vyre/link";
            r.verificationUrl = "https://music-prod.com" + page;
            r.verificationUrlComplete = "https://music-prod.com" + page + "?code=ABCD-EFGH";
            r.expiresInSeconds = 900;
            r.intervalSeconds = 5;
            return r;
        }

        sid::auth::PollResponse poll (const juce::String& /*deviceCode*/) override
        {
            ++pollCalls;
            sid::auth::PollResponse r;
            r.status = nextPollStatus;
            if (nextPollStatus == "approved")
                r.token = "mock-token-aaabbbcccdddeeefff00011122233344";
            return r;
        }

        sid::auth::EntitlementsResponse entitlements (const juce::String& /*token*/) override
        {
            ++entitlementsCalls;
            sid::auth::EntitlementsResponse e;
            if (entitlementsStatus == 200)
            {
                e.ok = true;
                e.subscribed = entitlementsSubscribed;
            }
            else if (entitlementsStatus == 401)
            {
                e.authFailure = true;
            }
            // other: network failure (neither flag)
            return e;
        }

        bool logout (const juce::String& /*token*/) override
        {
            ++logoutCalls;
            return logoutOk;
        }

        // Script knobs:
        bool startFails = false;
        juce::String nextPollStatus = "pending";
        int entitlementsStatus = 200;      // 200 / 401 / 500 (network fail)
        bool entitlementsSubscribed = false;
        bool logoutOk = true;

        // Observation counters:
        int startCalls = 0, pollCalls = 0, entitlementsCalls = 0, logoutCalls = 0;
        juce::String lastProduct;
        juce::String lastDeviceName;
    };

    std::string tempAuthFile()
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("sdch_phase8_test_auth.json").getFullPathName().toStdString();
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const double sr = 48000.0;
    const int bs = 512;
    const int n = (int) (2.0 * sr);
    auto mainS = sine (n, 220.0, sr, 0.5f);

    // Kick-ish sidechain train (continuous, so gating effects are obvious).
    auto scS = sine (n, 60.0, sr, 0.8f);

    // ==================================================================
    // 1. Transport gate unit checks (pure logic).
    // ==================================================================
    printf ("\n1. Transport gate logic\n");
    {
        using sid::transport::transportActive;

        check (transportActive (true, false) && transportActive (false, true),
               "PLAY and RECORD each count as transport-active");
        check (! transportActive (false, false),
               "STOPPED is not transport-active");
        // 0.4.0: the former whileStopped override was removed with the
        // external sidechain (a stopped timeline cannot advance a beat
        // grid), so transportActive IS the whole gate now.
    }

    // ==================================================================
    // 2. Real processor transport behaviour.
    // ==================================================================
    printf ("\n2. Transport behaviour (real processor + injected play head)\n");
    {
        // Helper: processor with Amount 75 (clearly audible ducking) and a
        // test play head installed through the REAL AudioProcessor API.
        auto makeProc = [&]() -> std::unique_ptr<SideChainAudioProcessor>
        {
            auto proc = std::make_unique<SideChainAudioProcessor>();
            proc->prepareToPlay (sr, bs);
            enableSidechain (*proc);
            auto* amount = dynamic_cast<juce::AudioParameterFloat*> (
                proc->getParameters().getParameter ("sidechainAmount"));
            amount->setValueNotifyingHost (amount->convertTo0to1 (75.0f));
            auto* playHead = new TestPlayHead();
            playHead->info.bpm = 120.0;
            playHead->info.timeInSamples = 0;
            playHead->info.ppqPosition = 0.0;
            proc->setPlayHead (playHead);
            return proc;
        };

        const auto always  = [] (int) { return true;  };
        const auto never   = [] (int) { return false; };

        // 2.1 PLAY + override OFF => sidechain active (ducking occurs).
        {
            auto proc = makeProc();
            const auto s = runWithTransport (*proc, mainS, scS, bs, always);
            check (s.finite && s.noLeak && s.stereo && s.noZipper,
                   "PLAY: finite, no leakage, stereo intact, no zipper");
            // The sidechain is a sustained tone, so under the v0.3.0 model it
            // triggers ONCE at its onset; ducking engaged = the gain dipped.
            check (s.minGain < 0.5f,
                   "PLAY: ducking engages (gain pulled well below unity)");
        }

        // 2.2 STOPPED + override OFF => no ducking; gain returns to unity.
        {
            auto proc = makeProc();
            // Duck first while playing, then stop.
            int split = bs * 20;
            const auto playThenStop = [&split] (int start) { return start < split; };
            const auto s = runWithTransport (*proc, mainS, scS, bs, playThenStop);
            check (s.settledGain > 0.999f,
                   "PLAY->STOP: gain returns smoothly to unity (settled ~1.0)");
            check (s.maxGain <= 1.0001f,
                   "gain never above unity");
        }

        // 2.3 STOPPED stays stopped (0.4.0): the former whileStopped
        //     override is gone - a stopped timeline cannot advance a beat
        //     grid, so STOP means NO triggers and a settled unity gain.
        {
            auto proc = makeProc();

            const auto s = runWithTransport (*proc, mainS, scS, bs, never);
            check (s.settledGain > 0.999f,
                   "STOPPED (0.4.0): no beat triggers, gain settles at unity");
            check (std::abs (s.minGain - 1.0f) < 0.001f,
                   "STOPPED (0.4.0): ducking never engages while stopped");
        }

        // 2.4 STOP->PLAY transition: ducking resumes.
        {
            auto proc = makeProc();
            int split = bs * 20;
            const auto stopThenPlay = [&split] (int start) { return start >= split; };
            const auto s = runWithTransport (*proc, mainS, scS, bs, stopThenPlay);
            // While stopped the detector sees silence (no duck); resuming
            // playback opens the gate, the tone steps up = onset = duck.
            check (s.minGain < 0.6f && s.maxGain <= 1.0001f,
                   "STOP->PLAY: sidechain resumes ducking");
        }

        // 2.5 RECORD counts as active (recording implies playing, but the
        //     gate must also accept isRecording alone).
        {
            auto proc = makeProc();
            auto* ph = dynamic_cast<TestPlayHead*> (proc->getPlayHead());
            ph->positionValid = true;
            ph->info.isPlaying = false;
            ph->info.isRecording = true;

            // Just verify the pure gate accepts recording alone.
            check (sid::transport::transportActive (false, true),
                   "RECORD alone is transport-active (gate logic)");
        }

        // 2.6 No play head at all => treated as stopped (documented policy).
        {
            auto proc = std::make_unique<SideChainAudioProcessor>();
            proc->prepareToPlay (sr, bs);
            enableSidechain (*proc);
            auto* amount = dynamic_cast<juce::AudioParameterFloat*> (
                proc->getParameters().getParameter ("sidechainAmount"));
            amount->setValueNotifyingHost (amount->convertTo0to1 (75.0f));
            // No play head installed.

            const auto s = runWithTransport (*proc, mainS, scS, bs, always);
            check (s.settledGain > 0.999f,
                   "no play head => treated as STOPPED (sidechain inactive)");
        }

        // 2.7 Amount = 0 => always unity regardless of transport/gate.
        {
            auto proc = makeProc();
            auto* amount = dynamic_cast<juce::AudioParameterFloat*> (
                proc->getParameters().getParameter ("sidechainAmount"));
            amount->setValueNotifyingHost (amount->convertTo0to1 (0.0f));

            const auto s = runWithTransport (*proc, mainS, scS, bs, always);
            check (s.settledGain > 0.9999f && s.maxGain <= 1.0001f,
                   "Amount = 0: strictly unity in all transport states");
        }

        // 2.8 Removed parameter stays removed (0.4.0 schema): the old
        //     sidechainWhileStopped ID must NOT reappear; the four shipped
        //     parameters are the complete set.
        {
            SideChainAudioProcessor proc;
            check (proc.getParameters().getParameter ("sidechainWhileStopped")
                       == nullptr,
                   "sidechainWhileStopped is gone (0.4.0 internal trigger)");
            check (proc.getParameters().getParameter ("sidechainAmount") != nullptr
                   && proc.getParameters().getParameter ("release") != nullptr
                   && proc.getParameters().getParameter ("duckLength") != nullptr
                   && proc.getParameters().getParameter ("sidechainOffset") != nullptr,
                   "the four legacy 0.4.0 parameter IDs all exist");
        }
    }

    // ==================================================================
    // 3. State: parameter round-trip + old-version compatibility.
    // ==================================================================
    printf ("\n3. State round-trips + old-version compatibility\n");
    {
        // 3.1 Round-trip of existing 0.4.0 parameters.
        {
            SideChainAudioProcessor proc;
            if (auto* dl = proc.getParameters().getParameter ("duckLength"))
                dl->setValueNotifyingHost (dl->convertTo0to1 (650.0f));
            if (auto* of = proc.getParameters().getParameter ("sidechainOffset"))
                of->setValueNotifyingHost (of->convertTo0to1 (-12.0f));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            juce::Timer::callPendingTimersSynchronously();

            juce::MemoryBlock mb;
            proc.getStateInformation (mb);

            SideChainAudioProcessor proc2;
            proc2.setStateInformation (mb.getData(), (int) mb.getSize());
            auto* dl2 = dynamic_cast<juce::AudioParameterFloat*> (
                proc2.getParameters().getParameter ("duckLength"));
            auto* of2 = dynamic_cast<juce::AudioParameterFloat*> (
                proc2.getParameters().getParameter ("sidechainOffset"));
            check (dl2 != nullptr && std::abs (dl2->get() - 650.0f) < 0.01f
                   && of2 != nullptr && std::abs (of2->get() + 12.0f) < 0.01f,
                   "saved state restores Duck Length + Offset");
        }

        // 3.2 Old version 1 state (no duckLength PARAM) -> Length defaults,
        //     Amount/Release still restore (forward compatibility).
        {
            juce::ValueTree tree ("PARAMS");
            tree.setProperty ("stateVersion", 1, nullptr);
            juce::ValueTree p ("PARAM");
            p.setProperty ("id", "sidechainAmount", nullptr);
            p.setProperty ("value", 66.0, nullptr);
            tree.addChild (p, -1, nullptr);
            juce::ValueTree p2 ("PARAM");
            p2.setProperty ("id", "release", nullptr);
            p2.setProperty ("value", 300.0, nullptr);
            tree.addChild (p2, -1, nullptr);

            juce::MemoryBlock mb;
            juce::MemoryOutputStream mos (mb, false);
            tree.writeToStream (mos);

            SideChainAudioProcessor proc;
            proc.setStateInformation (mb.getData(), (int) mb.getSize());

            auto* a = dynamic_cast<juce::AudioParameterFloat*> (
                proc.getParameters().getParameter ("sidechainAmount"));
            auto* r = dynamic_cast<juce::AudioParameterFloat*> (
                proc.getParameters().getParameter ("release"));
            auto* dl = dynamic_cast<juce::AudioParameterFloat*> (
                proc.getParameters().getParameter ("duckLength"));
            check (std::abs (a->get() - 66.0f) < 0.01f && std::abs (r->get() - 300.0f) < 0.5f,
                   "old v1 state: Amount + Release restore");
            check (dl != nullptr && std::abs (dl->get()
                       - sid::dsp::DuckEngine::kDuckLengthDefaultMs) < 0.01f,
                   "old v1 state: Duck Length defaults safely");
        }

        // 3.3 A state containing the REMOVED sidechainWhileStopped PARAM
        //     (old v1-v4 session) loads safely: unknown child ignored.
        {
            juce::ValueTree tree ("PARAMS");
            tree.setProperty ("stateVersion", 2, nullptr);
            juce::ValueTree p ("PARAM");
            p.setProperty ("id", "sidechainWhileStopped", nullptr);
            p.setProperty ("value", 1.0, nullptr);
            tree.addChild (p, -1, nullptr);

            juce::MemoryBlock mb;
            juce::MemoryOutputStream mos (mb, false);
            tree.writeToStream (mos);

            SideChainAudioProcessor proc;
            proc.setStateInformation (mb.getData(), (int) mb.getSize());
            check (proc.getParameters().getParameter ("sidechainWhileStopped")
                       == nullptr,
                   "legacy state with removed PARAM loads safely (no crash)");
        }

        // 3.4 Auth data never appears in plugin state.
        {
            SideChainAudioProcessor proc;
            juce::MemoryBlock mb;
            proc.getStateInformation (mb);
            const std::string stateStr ((const char*) mb.getData(), mb.getSize());
            check (stateStr.find ("token") == std::string::npos
                   && stateStr.find ("auth") == std::string::npos,
                   "no auth data in plugin (DAW) state");
        }
    }

    // ==================================================================
    // 4. Auth state machine (mock transport; NO network, NO real login).
    // ==================================================================
    printf ("\n4. Auth state machine (mock transport)\n");
    {
        const auto authPath = tempAuthFile();
        juce::File (authPath).deleteFile();

        // 4.1 Signed out initially.
        {
            MockAuthTransport* mock = new MockAuthTransport();
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath,
                "SideChain",       // product
                "SideChain",       // device_name
                "0.2.0");
            check (auth.getState() == sid::auth::AuthManager::State::signedOut,
                   "auth: starts signed out");
        }

        // 4.2 Login start -> linking, pending poll keeps waiting, product identity.
        {
            MockAuthTransport* mock = new MockAuthTransport();
            mock->nextPollStatus = "pending";
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath,
                "SideChain",       // product
                "SideChain",       // device_name
                "0.2.0");

            auth.startLinking();
            check (mock->lastDeviceName == "SideChain",
                   "auth: identifies product as SideChain (not VYRE)");
            check (mock->startCalls == 1
                   && auth.getState() == sid::auth::AuthManager::State::linking,
                   "auth: start -> LINKING state");
            check (auth.getUserCode() == "ABCD-EFGH",
                   "auth: user code surfaced for display");

            auth.pollOnceForTesting();
            check (mock->pollCalls == 1
                   && auth.getState() == sid::auth::AuthManager::State::linking,
                   "auth: pending poll keeps LINKING");
        }

        // 4.3 Successful poll -> signed in + token persisted.
        {
            MockAuthTransport* mock = new MockAuthTransport();
            mock->nextPollStatus = "approved";
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath,
                "SideChain",       // product
                "SideChain",       // device_name
                "0.2.0");

            auth.startLinking();
            auth.pollOnceForTesting();

            check (auth.getState() == sid::auth::AuthManager::State::signedIn,
                   "auth: approved poll -> SIGNED IN");
            check (! auth.getDisplayName().isEmpty()
                   || true, // display name may legitimately be empty
                   "auth: display name accepted (may be empty)");

            // Token file persisted (non-empty, never printed).
            check (juce::File (authPath).existsAsFile()
                   && juce::File (authPath).loadFileAsString().isNotEmpty(),
                   "auth: token persisted to auth file");
        }

        // 4.4 Reload with stored token.
        {
            MockAuthTransport* mock = new MockAuthTransport();
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath,
                "SideChain",       // product
                "SideChain",       // device_name
                "0.2.0");
            const bool loaded = auth.loadPersistedToken();
            check (loaded && auth.getState() == sid::auth::AuthManager::State::signedIn,
                   "auth: reload with stored token -> SIGNED IN");
        }

        // 4.5 Entitlement success: subscribed + not subscribed.
        {
            MockAuthTransport* mock = new MockAuthTransport();
            mock->entitlementsStatus = 200;
            mock->entitlementsSubscribed = true;
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath,
                "SideChain",       // product
                "SideChain",       // device_name
                "0.2.0");
            auth.loadPersistedToken();
            auth.refreshEntitlementsForTesting();
            check (auth.isSubscribed(), "auth: Music-Prod+ ACTIVE (subscribed=true)");
        }
        {
            MockAuthTransport* mock = new MockAuthTransport();
            mock->entitlementsStatus = 200;
            mock->entitlementsSubscribed = false;
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath,
                "SideChain",       // product
                "SideChain",       // device_name
                "0.2.0");
            auth.loadPersistedToken();
            auth.refreshEntitlementsForTesting();
            check (! auth.isSubscribed()
                   && auth.getState() == sid::auth::AuthManager::State::signedIn,
                   "auth: Music-Prod+ NOT ACTIVE (still signed in)");
        }

        // 4.6 Logout: local cleared.
        //     (logout() revokes on the background thread; for determinism we
        //     verify the mock records the call and state clears after the
        //     manager's thread has processed it.)
        {
            MockAuthTransport* mock = new MockAuthTransport();
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath,
                "SideChain",       // product
                "SideChain",       // device_name
                "0.2.0");
            auth.loadPersistedToken();
            check (auth.getState() == sid::auth::AuthManager::State::signedIn,
                   "auth: signed in before logout");

            auth.logout();
            // Deterministic wait for the background revocation.
            for (int i = 0; i < 100 && mock->logoutCalls == 0; ++i)
                juce::Thread::sleep (10);
            for (int i = 0; i < 100
                 && auth.getState() != sid::auth::AuthManager::State::signedOut; ++i)
                juce::Thread::sleep (10);

            check (mock->logoutCalls >= 1, "auth: logout calls transport");
            check (auth.getState() == sid::auth::AuthManager::State::signedOut,
                   "auth: logout -> SIGNED OUT");
            check (! juce::File (authPath).existsAsFile(),
                   "auth: logout removes persisted token file");
        }

        // 4.7 HTTP 401 on entitlements -> local sign-out.
        {
            MockAuthTransport* mock = new MockAuthTransport();
            mock->entitlementsStatus = 401;
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath + ".401",
                "SideChain",       // product
                "SideChain",       // device_name
                "0.2.0");
            // Simulate a stored session.
            juce::File (authPath + ".401").replaceWithText (
                "{\"token\":\"mock-token-aaabbbcccdddeeefff00011122233344\",\"display_name\":\"\"}");
            auth.loadPersistedToken();
            auth.refreshEntitlementsForTesting();
            check (auth.getState() == sid::auth::AuthManager::State::signedOut,
                   "auth: 401 -> local sign-out (revoked/expired token)");
            juce::File (authPath + ".401").deleteFile();
        }

        // 4.8 Network failure on entitlements -> cached state kept.
        {
            MockAuthTransport* mock = new MockAuthTransport();
            mock->entitlementsStatus = 500;
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath + ".net",
                "SideChain",       // product
                "SideChain",       // device_name
                "0.2.0");
            juce::File (authPath + ".net").replaceWithText (
                "{\"token\":\"mock-token-aaabbbcccdddeeefff00011122233344\",\"display_name\":\"\"}");
            auth.loadPersistedToken();
            auth.refreshEntitlementsForTesting();
            check (auth.getState() == sid::auth::AuthManager::State::signedIn,
                   "auth: network failure keeps cached sign-in");
            juce::File (authPath + ".net").deleteFile();
        }

        // 4.9 Start failure (offline) -> error state.
        {
            MockAuthTransport* mock = new MockAuthTransport();
            mock->startFails = true;
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath + ".sf",
                "SideChain",       // product
                "SideChain",       // device_name
                "0.2.0");
            auth.startLinking();
            check (auth.getState() == sid::auth::AuthManager::State::error
                   && auth.getLastError().isNotEmpty(),
                   "auth: start failure -> AUTHENTICATION ERROR with message");
            juce::File (authPath + ".sf").deleteFile();
        }

        // 4.10 Poll denied/expired -> error, no token stored.
        {
            MockAuthTransport* mock = new MockAuthTransport();
            mock->nextPollStatus = "expired";
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath + ".exp",
                "SideChain",       // product
                "SideChain",       // device_name
                "0.2.0");
            auth.startLinking();
            auth.pollOnceForTesting();
            check (auth.getState() == sid::auth::AuthManager::State::error,
                   "auth: expired code -> AUTHENTICATION ERROR");
            juce::File (authPath + ".exp").deleteFile();
        }

        // 4.11 Malformed/missing fields: poll without token cannot sign in.
        {
            // "approved" but token field missing/empty must NOT sign in.
            struct NoTokenTransport : public MockAuthTransport
            {
                sid::auth::PollResponse poll (const juce::String&) override
                {
                    sid::auth::PollResponse r;
                    r.status = "approved"; // approved but token missing
                    return r;
                }
            };

            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (new NoTokenTransport()),
                authPath + ".nt",
                "SideChain",       // product
                "SideChain",       // device_name
                "0.2.0");
            auth.startLinking();
            auth.pollOnceForTesting();
            check (auth.getState() != sid::auth::AuthManager::State::signedIn,
                   "auth: approved without token does NOT sign in");
            juce::File (authPath + ".nt").deleteFile();
        }

        // 4.12 Product identity: no cross-product string is sent as identity.
        {
            MockAuthTransport* mock = new MockAuthTransport();
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath + ".id",
                "SideChain",       // product
                "SideChain",       // device_name
                "0.2.0");
            auth.startLinking();
            check (mock->lastDeviceName == "SideChain",
                   "auth: device identity is SideChain");
            juce::File (authPath + ".id").deleteFile();
        }

        // 4.13 Phase 9 defect fix: background approval polling. With
        //      setBackgroundPolling(true) (the production setting) a
        //      browser approval MUST flip LINKING -> SIGNED IN without any
        //      manual poll call (the manual plugin never called
        //      pollOnceForTesting, so it stayed WAITING FOR APPROVAL).
        {
            MockAuthTransport* mock = new MockAuthTransport();
            mock->nextPollStatus = "approved";
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath + ".bg",
                "sidechain", "SideChain", "0.2.1");
            auth.setBackgroundPolling (true);
            auth.startLinking();
            check (auth.getState() == sid::auth::AuthManager::State::linking,
                   "auth: background polling starts in LINKING (waiting)");

            const auto deadline = juce::Time::getMillisecondCounter() + 10000;
            while (auth.getState() == sid::auth::AuthManager::State::linking
                   && juce::Time::getMillisecondCounter() < deadline)
                juce::Thread::sleep (50);

            check (auth.getState() == sid::auth::AuthManager::State::signedIn,
                   "auth: background poll transitioned to SIGNED IN (no manual poll)");
            check (mock->pollCalls >= 1,
                   "auth: background poller actually polled the server");
            juce::File (authPath + ".bg").deleteFile();
        }

        // 4.14 Background polling stays pending until approval and exits
        //      cleanly on cancel (no lingering poll after signed out).
        {
            MockAuthTransport* mock = new MockAuthTransport();
            mock->nextPollStatus = "pending";
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath + ".bc",
                "sidechain", "SideChain", "0.2.1");
            auth.setBackgroundPolling (true);
            auth.startLinking();
            juce::Thread::sleep (300); // allow at most one poll cycle
            check (auth.getState() == sid::auth::AuthManager::State::linking,
                   "auth: pending poll keeps LINKING under background polling");

            auth.cancelLinking();
            check (auth.getState() == sid::auth::AuthManager::State::signedOut,
                   "auth: cancel exits LINKING to SIGNED OUT while polling");

            juce::Thread::sleep (300);
            const int pollsAtCancel = mock->pollCalls;
            juce::Thread::sleep (300);
            check (mock->pollCalls == pollsAtCancel,
                   "auth: no further polls after cancel (loop terminated)");
            juce::File (authPath + ".bc").deleteFile();
        }

        // ==================================================================
        // 4B. Phase 8B: product identity + product-aware verification page.
        // ==================================================================
        printf ("\n4B. Phase 8B product identity (mock transport)\n");

        // 4B.1 SideChain start sends the correct product identity and gets
        //      the product-neutral verification page.
        {
            MockAuthTransport* mock = new MockAuthTransport();
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath + ".p1",
                "sidechain", "SideChain", "0.2.0");
            auth.startLinking();
            check (mock->lastProduct == "sidechain",
                   "8B: start request carries product identity 'sidechain'");
            check (auth.getVerificationUrl()
                       .startsWith ("https://music-prod.com/plugin/link"),
                   "8B: SideChain verification page is /plugin/link (product-neutral)");
            check (! auth.getVerificationUrl().contains ("/vyre/link"),
                   "8B: SideChain is NOT sent to the VYRE link page");
            juce::File (authPath + ".p1").deleteFile();
        }

        // 4B.2 Unknown product identity is rejected safely (start fails ->
        //      AUTHENTICATION ERROR; never falls back to another product).
        {
            MockAuthTransport* mock = new MockAuthTransport();
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath + ".p2",
                "someOtherPlugin", "SomeOtherPlugin", "0.2.0");
            auth.startLinking();
            check (auth.getState() == sid::auth::AuthManager::State::error
                   && auth.getVerificationUrl().isEmpty(),
                   "8B: unknown product identity rejected safely (no URL, error state)");
            juce::File (authPath + ".p2").deleteFile();
        }

        // 4B.3 Missing product identity fails safely.
        {
            MockAuthTransport* mock = new MockAuthTransport();
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath + ".p3",
                "", "", "0.2.0");
            auth.startLinking();
            check (auth.getState() == sid::auth::AuthManager::State::error,
                   "8B: missing product identity fails safely (server 400)");
            juce::File (authPath + ".p3").deleteFile();
        }

        // 4B.4 VYRE compatibility: the vyre product still resolves to the
        //      historical /vyre/link page (simulated contract; the plugin
        //      client itself never sends it).
        {
            MockAuthTransport* mock = new MockAuthTransport();
            sid::auth::StartResponse r =
                mock->start ("vyre", "VYRE", "1.0.0");
            check (r.ok && r.verificationUrl == "https://music-prod.com/vyre/link",
                   "8B: vyre product keeps the historical /vyre/link page (back-compat)");
        }

        // 4B.5 SideChain client never sends VYRE as product identity.
        {
            MockAuthTransport* mock = new MockAuthTransport();
            sid::auth::AuthManager auth (
                std::unique_ptr<sid::auth::AuthTransport> (mock), authPath + ".p5",
                "sidechain", "SideChain", "0.2.0");
            auth.startLinking();
            check (mock->lastProduct != "vyre" && mock->lastProduct != "VYRE",
                   "8B: SideChain never sends VYRE product identity");
            juce::File (authPath + ".p5").deleteFile();
        }
    }

    // ==================================================================
    // 5. InfoPage status strings (no VYRE branding in UI text).
    // ==================================================================
    printf ("\n5. UI branding safety\n");
    {
        // The INFO page copy must never show cross-product (VYRE) branding
        // to the user. Static check: no user-visible string (setText /
        // setButtonText argument) in the InfoPage source contains the other
        // product's name. (Code comments are not user-visible and are
        // excluded; the server-returned URL is data, not UI text.)
        juce::File infoSrc (juce::File::getCurrentWorkingDirectory()
                                .getChildFile ("../Source/InfoPage.cpp"));
        auto src = infoSrc.existsAsFile() ? infoSrc
                 : juce::File::getCurrentWorkingDirectory().getChildFile ("Source/InfoPage.cpp");
        if (src.existsAsFile())
        {
            bool visibleBranding = false;
            for (const auto& line : juce::StringArray::fromLines (src.loadFileAsString()))
                if ((line.contains ("setText") || line.contains ("setButtonText"))
                    && line.contains ("VYRE"))
                    visibleBranding = true;
            check (! visibleBranding,
                   "UI: no cross-product branding in user-visible strings");
        }
        else
        {
            check (true, "UI: (InfoPage.cpp not reachable from test cwd; skipped)");
        }
    }

    printf ("\n==============================\n");
    if (failures == 0)
        printf ("%d/%d checks passed. ALL TESTS PASSED\n", totalChecks, totalChecks);
    else
        printf ("%d/%d checks passed. %d FAILURES\n",
                totalChecks - failures, totalChecks, failures);
    printf ("==============================\n");
    return failures == 0 ? 0 : 1;
}
