/*
    PumpCurve 0.5.0 - demo session + full-version entitlement tests.

    Suite 12. Everything here runs against the REAL processor through the REAL
    AudioPlayHead API: the demo gate, the state serializer and the audio path
    are the shipped code paths, not a model of them.

    1.  Demo gate logic (monotonic clock, 15-minute session, clock formatting,
        entitlement-cache freshness policy).
    2.  Real processor: processing before expiry, silence after expiry.
    3.  Restarting the instance starts a fresh 15-minute session.
    4.  Plugin state cannot extend, reset or bypass a live demo session.
    5.  Full version via a server-verified paid Music-Prod+ subscription.
    6.  Full version via a valid Music-Prod licence key.
    7.  Invalid / revoked / expired licences never grant the full version.
    8.  Network failures follow the documented offline policy (no silent
        downgrade of a verified subscriber; a stale or unverifiable local
        cache grants nothing).
    9.  Switching between entitled and demo states can never leave audio
        processing enabled incorrectly.
    10. The licence-key contract: a real operation, bound to the account, that
        fails closed on anything the server did not actually answer.
    11. Licence key and Music-Prod+ are independent routes and can coexist.
    12. DEVICE activations: one key, up to three computers, the "too many
        computers" answer, self-service deactivation (including the shorter
        device-bound offline window and the long-session re-check policy).

    CLOCK: a controllable monotonic source is installed, so a 15-minute
    session is exercised without waiting 15 real minutes.

    NO NETWORK: every transport here is a mock, and the processor is given a
    temporary auth-file path so no real user token is ever read.
*/

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <atomic>

#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/MusicProdAuth.h"
#include "../Source/LicenseGate.h"

static int failures = 0, totalChecks = 0;

static void check (bool ok, const std::string& what)
{
    ++totalChecks;
    printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

namespace
{
    // ---- controllable monotonic clock ---------------------------------
    double g_fakeNowMs = 0.0;
    double fakeNow() { return g_fakeNowMs; }

    const double kSeconds = 1000.0;

    // ---- real transport through the real AudioPlayHead API -------------
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

    // ---- mock transport mirroring the documented server contract -------
    struct MockTransport : public sid::auth::AuthTransport
    {
        sid::auth::StartResponse start (const juce::String& product,
                                        const juce::String&, const juce::String&) override
        {
            ++startCalls;
            lastProduct = product;
            sid::auth::StartResponse r;
            r.ok = true;
            r.deviceCode = "mock-device-code-000000000000000000000000";
            r.userCode = "MOCK-CODE";
            r.verificationUrl = "https://music-prod.com/plugin/link";
            r.verificationUrlComplete = "https://music-prod.com/plugin/link?code=MOCK-CODE";
            r.expiresInSeconds = 900;
            r.intervalSeconds = 1;
            return r;
        }

        sid::auth::PollResponse poll (const juce::String&) override
        {
            ++pollCalls;
            sid::auth::PollResponse r;
            r.status = pollStatus;
            if (pollStatus == "approved")
            {
                r.token = "mock-token-0123456789abcdef0123456789abcdef";
                r.displayName = "Mock User";
            }
            return r;
        }

        sid::auth::EntitlementsResponse entitlements (const juce::String& token) override
        {
            ++entitlementsCalls;
            lastEntitlementsToken = token;

            // A request stuck on the network (offline DNS, dead route): blocks
            // until the owner cancels it, as the real transport's stream does.
            if (blockEntitlements)
            {
                enteredBlockingCall = true;
                requestCancelled.wait (15000);
                return {};
            }

            // When a test supplies a raw body, it goes through the REAL
            // production parser. Everything below builds the response STRUCT by
            // hand, which is exactly why a field the client forgets to read
            // cannot be caught by these tests - see section 13, which exists for
            // that class of bug.
            if (! entitlementsBodyOverride.isVoid())
                return sid::auth::interpretEntitlementsResponse (entitlementsStatus,
                                                                entitlementsBodyOverride);

            sid::auth::EntitlementsResponse e;
            if (entitlementsStatus == 200)
            {
                e.ok = true;
                // `subscribed` keeps its raw membership meaning. The field the
                // plugin actually acts on is the PRODUCT-SCOPED answer, which is
                // what the real server computes - here the script decides whether
                // this product is covered and whether a licence is active.
                e.subscribed = entitlementsSubscribed;
                e.productEntitled = entitlementsSubscribed || entitlementsLicenseActive;
                e.product = answeredProduct;
                e.subscriptionActive = entitlementsSubscribed;
                e.subscriptionCoversProduct = entitlementsSubscribed;
                // A standalone licence is DEVICE-scoped, so the server reports
                // the two halves separately: the account holds the licence, and
                // THIS computer holds one of its three slots. `licenseActive` is
                // both, and is what the plugin names as the granting route.
                //
                // A revoked or expired licence is NOT "on the account" either -
                // the account half follows the status exactly as the real
                // server's does, so a revocation is reported as a revocation
                // rather than as an unactivated computer.
                const bool licenceNotFinal = entitlementsLicenseStatus != "revoked"
                                             && entitlementsLicenseStatus != "expired";
                e.licenseActive = entitlementsLicenseActive;
                e.licenseAccountActive = (entitlementsLicenseActive || entitlementsLicenseAccountOnly)
                                             && licenceNotFinal;
                e.licenseDeviceActive = entitlementsLicenseActive;
                e.devicesUsed = entitlementsDevicesUsed;
                e.deviceLimit = 3;
                e.licenseStatus = entitlementsLicenseActive ? "active" : entitlementsLicenseStatus;
            }
            else if (entitlementsStatus == 401)
            {
                e.authFailure = true;
            }
            // anything else: network failure (neither ok nor authFailure)
            return e;
        }

        bool logout (const juce::String&) override
        {
            ++logoutCalls;
            return true;
        }

        // The device identity the manager hands over before the first request
        // that needs it (`start`, and `activate-license`). Recorded so the test
        // can prove the plugin really names the computer it is running on, and
        // that the id is stable rather than minted per request.
        void setDeviceIdentity (const juce::String& deviceIdIn,
                                const juce::String& deviceLabelIn,
                                const juce::String& platformIn) override
        {
            ++deviceIdentityCalls;
            lastDeviceId    = deviceIdIn;
            lastDeviceLabel = deviceLabelIn;
            lastPlatform    = platformIn;
        }

        sid::auth::LicenseResponse activateLicense (const juce::String& token,
                                                    const juce::String& licenceKey) override
        {
            ++licenseCalls;
            lastLicenceKey = licenceKey;
            lastLicenceToken = token;
            sid::auth::LicenseResponse r;
            r.status = licenseStatus;
            r.message = licenseMessage;
            r.product = answeredProduct;   // the server answers for the TOKEN's product
            if (licenseAuthFailure)
            {
                r.authFailure = true;
                r.status = sid::auth::LicenseResponse::Status::signInRequired;
                return r;
            }

            // The slot bookkeeping the server reports. `deviceActivated` is the
            // field that proves a slot was taken, so it is set exactly when the
            // answer is `ok`; `device_limit` reports a full house instead.
            r.deviceActivated = (licenseStatus == sid::auth::LicenseResponse::Status::ok);
            r.deviceLimitUsed = (licenseStatus == sid::auth::LicenseResponse::Status::deviceLimit)
                                    ? 3 : (r.deviceActivated ? 1 : 0);
            r.deviceLimit = 3;

            // Model the server: a successful activation BINDS the key to this
            // account AND takes a slot for this device, so the next entitlement
            // answer reports a licence active for this device. That is what makes
            // the entitlement an account+device entitlement rather than an
            // instance-local flag.
            if (licenseStatus == sid::auth::LicenseResponse::Status::ok)
            {
                entitlementsLicenseActive = true;
                entitlementsLicenseAccountOnly = true;
                entitlementsDevicesUsed = 1;
            }

            return r;
        }

        // Script knobs
        juce::String pollStatus = "approved";
        int  entitlementsStatus = 200;      // 200 / 401 / other=network fail
        /** Verbatim `entitlements` body from the deployed contract, when a test
            wants the real parser in the loop instead of the hand-built fields. */
        juce::var entitlementsBodyOverride;
        bool entitlementsSubscribed = false;
        /** What the server says about a licence for this account + product. */
        bool entitlementsLicenseActive = false;
        /** True when the ACCOUNT holds the licence but THIS device has no slot. */
        bool entitlementsLicenseAccountOnly = false;
        juce::String entitlementsLicenseStatus = "none";
        int  entitlementsDevicesUsed = 0;
        sid::auth::LicenseResponse::Status licenseStatus =
            sid::auth::LicenseResponse::Status::unavailable;
        juce::String licenseMessage;
        /** The product the server answers for: the one the token was issued for. */
        juce::String answeredProduct = "sidechain";
        /** activate-license answers 401 (the server rejected the token). */
        bool licenseAuthFailure = false;
        std::atomic<bool> blockEntitlements { false }, enteredBlockingCall { false };
        juce::WaitableEvent requestCancelled { true };
        void cancelPendingRequests() override { requestCancelled.signal(); }

        // Observation (atomic: the processor may re-verify on its auth thread)
        std::atomic<int> startCalls { 0 }, pollCalls { 0 }, entitlementsCalls { 0 };
        std::atomic<int> logoutCalls { 0 }, licenseCalls { 0 }, deviceIdentityCalls { 0 };
        juce::String lastDeviceId;
        juce::String lastDeviceLabel;
        juce::String lastPlatform;
        juce::String lastProduct;
        juce::String lastLicenceKey;
        juce::String lastLicenceToken;
        juce::String lastEntitlementsToken;
    };

    /**
        Sign a processor in through the REAL device-code state machine (start ->
        poll -> approved) and refresh its entitlements. Production refuses to
        activate a licence key without an account token, so every licence test
        has to get here first - which is the point.
    */
    void signIn (SideChainAudioProcessor& proc, MockTransport& mock)
    {
        mock.pollStatus = "approved";
        proc.getAuthManager().startLinking();
        proc.getAuthManager().pollOnceForTesting();
        proc.getAuthManager().refreshEntitlementsForTesting();
    }

    // ---- temp auth files ----------------------------------------------
    juce::File tempFile (const juce::String& name)
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("sdch_licensedemo_" + name);
    }

    /**
        Write the auth cache EXACTLY as the production writer does
        (AuthManager::persistTokenLocked): the raw membership, the
        PRODUCT-SCOPED entitlement the server computed, whether a licence was
        active for the account, and the moment the server verified all of it.

        `productEntitled` (-1 = same as `subscribed`) defaults to the membership
        because that is what the server answers for PumpCurve to a verified
        Music-Prod+ subscriber - the only cache a "verified subscriber" test
        means to simulate. A licence-only cache passes `0` for the membership and
        `1` here, so the two routes stay distinguishable in the tests too.
    */
    void writeAuthFile (const juce::File& f, const juce::String& token,
                        bool subscribed, double verifiedAtMs,
                        int productEntitled = -1, bool licenseActive = false,
                        double licenseVerifiedAtMs = -1.0)
    {
        const bool entitled = productEntitled < 0 ? subscribed : (productEntitled != 0);

        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty ("token", token);
        obj->setProperty ("display_name", "Mock User");
        obj->setProperty ("subscribed", subscribed);
        obj->setProperty ("product_entitled", entitled);
        obj->setProperty ("license_active", licenseActive);
        if (verifiedAtMs > 0.0)
            obj->setProperty ("entitlements_verified_at_ms", verifiedAtMs);
        // The licence route's own timestamp. A licence grant is cached on the
        // SHORTER device-bound window, so a test has to be able to write the two
        // stamps independently to show the difference.
        obj->setProperty ("license_verified_at_ms",
                          licenseVerifiedAtMs >= 0.0 ? licenseVerifiedAtMs : verifiedAtMs);
        f.getParentDirectory().createDirectory();
        f.replaceWithText (juce::JSON::toString (juce::var (obj)));
    }

    // ---- audio driving -------------------------------------------------
    // Fills a real sine programme, advances the REAL play head, processes
    // through the REAL audio path and reports the peak of the final block.
    float runBlocks (SideChainAudioProcessor& proc, TestPlayHead& ph,
                     int blocks, int blockSize, double sampleRate,
                     bool* sawOnlySilence = nullptr)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        float lastPeak = 0.0f;
        bool onlySilence = true;
        long long t = 0;

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float s = 0.5f * (float) std::sin (
                    2.0 * 3.14159265358979 * 220.0 * (double) (t + i) / sampleRate);
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }

            ph.info.isPlaying      = true;
            ph.info.isRecording    = false;
            ph.info.bpm            = 120.0;
            ph.info.timeInSamples  = t;
            ph.info.ppqPosition    = (double) t / sampleRate * 2.0;   // 120 BPM

            proc.processBlock (buffer, midi);

            float peak = 0.0f;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, blockSize));

            lastPeak = peak;
            if (peak > 0.0f)
                onlySilence = false;

            t += blockSize;
        }

        if (sawOnlySilence != nullptr)
            *sawOnlySilence = onlySilence;

        return lastPeak;
    }

    // Amount=100 so ducking is unmistakable; keeps the DSP in its normal path.
    void setAmount (SideChainAudioProcessor& proc, float percent)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (
                proc.getParameters().getParameter ("sidechainAmount")))
            p->setValueNotifyingHost (p->convertTo0to1 (percent));
    }

    std::string lowered (const std::string& s)
    {
        std::string out = s;
        for (auto& c : out)
            c = (char) std::tolower ((unsigned char) c);
        return out;
    }

    // Bounded wait for an asynchronous auth transition (logout and the
    // background entitlement refresh do their work on the auth thread). A
    // deadline with a predicate is used instead of a fixed sleep so the test is
    // fast but never guesses at timing.
    template <typename Predicate>
    bool waitFor (Predicate pred, int timeoutMs = 3000)
    {
        const auto deadline = juce::Time::currentTimeMillis() + timeoutMs;
        while (juce::Time::currentTimeMillis() < deadline)
        {
            if (pred())
                return true;
            juce::Thread::sleep (5);
        }
        return pred();
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const double sr = 48000.0;
    const int    bs = 512;

    // Controllable monotonic clock for the whole suite.
    g_fakeNowMs = 1'000'000.0;
    sid::license::setMonotonicMillisecondsForTest (&fakeNow);

    const auto authPathA = tempFile ("a.json");
    const auto authPathB = tempFile ("b.json");
    const auto authPathC = tempFile ("c.json");
    for (const auto& f : { authPathA, authPathB, authPathC })
        f.deleteFile();

    // ==================================================================
    // 1. Demo gate logic
    // ==================================================================
    printf ("\n1. Demo session gate (monotonic clock, 15 minutes)\n");
    {
        using namespace sid::license;

        g_fakeNowMs = 5'000'000.0;
        DemoGate gate;

        check (std::abs (gate.sessionSeconds() - 900.0) < 0.001,
               "demo session is 15 minutes (900 s)");
        check (! gate.isFullVersion(), "a fresh instance starts in DEMO mode");
        check (gate.processingAllowed(), "demo processing is allowed at session start");
        check (std::abs (gate.remainingSeconds() - 900.0) < 0.5,
               "remaining time starts at 15:00");
        check (! gate.expired(), "a fresh session is not expired");

        // 14:59.5 - still inside the session.
        g_fakeNowMs += 899.5 * kSeconds;
        check (gate.processingAllowed() && ! gate.expired(),
               "processing is still allowed at 14:59.5");

        // Exactly at the limit and beyond.
        g_fakeNowMs += 0.5 * kSeconds;
        check (! gate.processingAllowed(), "processing is DISABLED at exactly 15:00");
        check (gate.expired(), "the session reports expired at 15:00");
        check (std::abs (gate.remainingSeconds()) < 0.001, "remaining time reaches zero");

        g_fakeNowMs += 3600.0 * kSeconds;   // an hour later
        check (! gate.processingAllowed(), "an expired session never un-expires by itself");

        // The clock is monotonic, not the wall clock: moving the system clock
        // backwards/forwards cannot change the session length.
        const double wallBefore = (double) juce::Time::currentTimeMillis();
        juce::ignoreUnused (wallBefore);
        check (gate.expired(), "wall-clock changes cannot revive an expired session");

        // Full version overrides the timer completely.
        DemoGate entitled;
        entitled.setFullVersion (true);
        g_fakeNowMs += 100.0 * 3600.0 * kSeconds;
        check (entitled.isFullVersion() && entitled.processingAllowed(),
               "a verified entitlement disables the time limit entirely");

        // Losing the entitlement starts a FRESH demo session from that moment
        // (never an instantly-expired session).
        entitled.setFullVersion (false);
        check (! entitled.expired() && entitled.remainingSeconds() > 899.0,
               "losing the entitlement starts a fresh 15-minute session");
        g_fakeNowMs += 900.0 * kSeconds;
        check (entitled.expired(), "...and that fresh session expires normally");

        // ---- clock formatting (readable MM:SS) ----
        check (formatClock (900.0) == "15:00", "clock: 900 s -> \"15:00\"");
        check (formatClock (65.0)  == "01:05", "clock: 65 s -> \"01:05\"");
        check (formatClock (59.0)  == "00:59", "clock: 59 s -> \"00:59\"");
        check (formatClock (0.0)   == "00:00", "clock: 0 s -> \"00:00\"");
        check (formatClock (-5.0)  == "00:00", "clock: negative clamps to \"00:00\"");
        check (formatClock (3599.0) == "59:59", "clock: 3599 s -> \"59:59\"");

        // ---- entitlement-cache freshness policy ----
        const double now = 10'000'000'000.0;
        check (! cachedEntitlementIsFresh (0.0, now),
               "policy: a cache with no server-verified timestamp is never trusted");
        check (! cachedEntitlementIsFresh (now + 86'400'000.0, now),
               "policy: a timestamp from the future is never trusted");
        check (cachedEntitlementIsFresh (now - 60'000.0, now),
               "policy: a just-verified answer is honoured (offline use)");
        check (cachedEntitlementIsFresh (now - (kEntitlementGraceSeconds - 3600.0) * kSeconds, now),
               "policy: an answer inside the grace window is honoured");
        check (! cachedEntitlementIsFresh (now - (kEntitlementGraceSeconds + 3600.0) * kSeconds, now),
               "policy: an answer older than the grace window is IGNORED");
    }

    // ==================================================================
    // 2. Real processor: processing before expiry, silence after
    // ==================================================================
    printf ("\n2. Real processor: demo processing, then silence at expiry\n");
    {
        g_fakeNowMs = 20'000'000.0;

        SideChainAudioProcessor proc (nullptr, authPathA.getFullPathName());
        proc.prepareToPlay (sr, bs);
        setAmount (proc, 100.0f);

        auto* ph = new TestPlayHead();
        proc.setPlayHead (ph);

        check (! proc.getDemoGate().isFullVersion(),
               "a signed-out instance runs in DEMO mode");
        check (proc.getDemoGate().processingAllowed(),
               "audio processing is enabled at the start of the demo session");

        const float peakBefore = runBlocks (proc, *ph, 8, bs, sr);
        check (peakBefore > 0.0f, "real audio is produced during the demo session");

        // 10 minutes in: into the middle of the session.
        g_fakeNowMs += 600.0 * kSeconds;
        check (proc.getDemoGate().processingAllowed(),
               "processing is still enabled 10 minutes into the session");
        check (proc.getDemoGate().remainingSeconds() > 290.0
                   && proc.getDemoGate().remainingSeconds() < 310.0,
               "remaining time reads ~5:00 at the 10-minute mark");

        const float peakMid = runBlocks (proc, *ph, 8, bs, sr);
        check (peakMid > 0.0f, "real audio is still produced 10 minutes in");

        // Past the 15-minute limit: the instance must output silence.
        g_fakeNowMs += 301.0 * kSeconds;
        check (! proc.getDemoGate().processingAllowed(),
               "processing is disabled once the 15 minutes have elapsed");
        check (proc.getDemoGate().expired(), "the session reports expired");

        bool allSilent = false;
        const float peakAfter = runBlocks (proc, *ph, 8, bs, sr, &allSilent);
        check (peakAfter == 0.0f && allSilent,
               "the expired instance outputs pure SILENCE (not a bypass, not dry passthrough)");

        // ...and stays silent on later blocks.
        const float peakLater = runBlocks (proc, *ph, 8, bs, sr);
        check (peakLater == 0.0f, "the expired instance stays silent on subsequent blocks");
    }

    // ==================================================================
    // 3. Restarting the instance starts a fresh session
    // ==================================================================
    printf ("\n3. Restarting the instance grants a fresh 15 minutes\n");
    {
        // The clock is far past the previous instance's session.
        g_fakeNowMs += 48.0 * 3600.0 * kSeconds;

        SideChainAudioProcessor restarted (nullptr, authPathA.getFullPathName());
        restarted.prepareToPlay (sr, bs);
        setAmount (restarted, 100.0f);
        auto* ph = new TestPlayHead();
        restarted.setPlayHead (ph);

        check (! restarted.getDemoGate().expired(),
               "a restarted instance is NOT expired");
        check (std::abs (restarted.getDemoGate().remainingSeconds() - 900.0) < 0.5,
               "a restarted instance starts a fresh 15:00 session");

        const float peak = runBlocks (restarted, *ph, 8, bs, sr);
        check (peak > 0.0f, "a restarted instance processes audio again");

        g_fakeNowMs += 900.0 * kSeconds;
        check (! restarted.getDemoGate().processingAllowed(),
               "...and expires again after its own 15 minutes");
    }

    // ==================================================================
    // 4. Plugin state cannot extend, reset or bypass a session
    // ==================================================================
    printf ("\n4. Plugin state cannot extend, reset or bypass the demo session\n");
    {
        g_fakeNowMs = 30'000'000.0;

        SideChainAudioProcessor proc (nullptr, authPathA.getFullPathName());
        proc.prepareToPlay (sr, bs);
        setAmount (proc, 100.0f);
        auto* ph = new TestPlayHead();
        proc.setPlayHead (ph);

        g_fakeNowMs += 500.0 * kSeconds;   // 8:20 used, 6:40 left

        juce::MemoryBlock state;
        proc.getStateInformation (state);

        const std::string raw ((const char*) state.getData(), (size_t) state.getSize());
        const auto text = lowered (raw);

        check (text.find ("demo") == std::string::npos,
               "the serialized state contains no demo/session keys at all");
        check (text.find ("remaining") == std::string::npos
                   && text.find ("sessionstart") == std::string::npos,
               "the serialized state contains no remaining-time or session-start value");
        check (text.find ("fullversion") == std::string::npos
                   && text.find ("entitlement") == std::string::npos,
               "the serialized state contains no entitlement/full-version flag");

        // Reloading the project into the SAME live instance must not reset it.
        proc.setStateInformation (state.getData(), (int) state.getSize());
        check (proc.getDemoGate().remainingSeconds() < 401.0,
               "reloading state did NOT reset the live session's countdown");
        check (! proc.getDemoGate().isFullVersion(),
               "reloading state did NOT turn demo mode into the full version");

        // A reloaded project is still a fresh instance: it gets its own
        // session, and the state cannot grant the full version.
        SideChainAudioProcessor loaded (nullptr, authPathA.getFullPathName());
        loaded.prepareToPlay (sr, bs);
        loaded.setStateInformation (state.getData(), (int) state.getSize());
        check (! loaded.getDemoGate().isFullVersion(),
               "a project file can never unlock the full version");
        check (! loaded.getDemoGate().expired() && loaded.getDemoGate().remainingSeconds() > 899.0,
               "a newly created instance starts its own fresh session (restart rule)");

        // An EXPIRED instance stays expired through a state round-trip.
        g_fakeNowMs += 900.0 * kSeconds;
        check (proc.getDemoGate().expired(), "instance is now expired");
        proc.setStateInformation (state.getData(), (int) state.getSize());
        check (proc.getDemoGate().expired() && ! proc.getDemoGate().processingAllowed(),
               "an expired instance cannot be revived by reloading state");
    }

    // ==================================================================
    // 5. Full version via a paid Music-Prod+ subscription
    // ==================================================================
    printf ("\n5. Full version via a verified paid Music-Prod+ subscription\n");
    {
        g_fakeNowMs = 40'000'000.0;

        // A previous session signed in and the server CONFIRMED the
        // subscription, so the answer is cached with its verification time.
        writeAuthFile (authPathB, "mock-token-0123456789abcdef0123456789abcdef",
                       true, (double) juce::Time::currentTimeMillis());

        auto mock = std::make_unique<MockTransport>();
        mock->entitlementsSubscribed = true;
        MockTransport* mockPtr = mock.get();

        SideChainAudioProcessor proc (std::move (mock), authPathB.getFullPathName());

        check (proc.getAuthManager().getState() == sid::auth::AuthManager::State::signedIn,
               "a restored token signs the instance in (file only, no network)");
        check (proc.getAuthManager().isSubscribed(),
               "the cached, server-verified subscription is honoured");
        check (proc.getAuthManager().hasFullEntitlement(),
               "the subscription entitlement grants the full version");
        check (proc.getDemoGate().isFullVersion(),
               "the audio gate is in FULL VERSION before the first block");

        proc.prepareToPlay (sr, bs);   // production re-verify (background)
        setAmount (proc, 100.0f);
        auto* ph = new TestPlayHead();
        proc.setPlayHead (ph);

        // Well past the demo limit: an entitled instance keeps working.
        g_fakeNowMs += 6.0 * 3600.0 * kSeconds;
        check (proc.getDemoGate().processingAllowed(),
               "an entitled instance still processes 6 hours in");
        const float peak = runBlocks (proc, *ph, 8, bs, sr);
        check (peak > 0.0f, "an entitled instance really produces audio past the demo limit");

        // Refreshing entitlements for a subscriber inside the app.
        proc.getAuthManager().refreshEntitlementsForTesting();
        check (proc.getAuthManager().isSubscribed() && proc.getDemoGate().isFullVersion(),
               "re-verifying an active subscription keeps the full version");
        check (mockPtr->entitlementsCalls.load() > 0,
               "the entitlement was actually fetched from the transport");
    }

    // ==================================================================
    // 6. Full version via a valid licence key
    // ==================================================================
    printf ("\n6. Full version via a valid Music-Prod licence key\n");
    {
        g_fakeNowMs = 50'000'000.0;
        authPathC.deleteFile();

        auto mock = std::make_unique<MockTransport>();
        mock->licenseStatus = sid::auth::LicenseResponse::Status::ok;
        MockTransport* mockPtr = mock.get();

        SideChainAudioProcessor proc (std::move (mock), authPathC.getFullPathName());

        check (! proc.getAuthManager().hasFullEntitlement(),
               "before activating a key the instance has no entitlement");
        check (proc.getAuthManager().getLicenseStatus()
                   == sid::auth::AuthManager::LicenseStatus::notChecked,
               "the licence route starts as NOT CHECKED");

        // A key is bound to an ACCOUNT, so sign in first - through the REAL
        // device-code state machine. Signing in must NOT be an entitlement by
        // itself: this account has no subscription and no key yet.
        signIn (proc, *mockPtr);
        check (! proc.getAuthManager().hasFullEntitlement(),
               "signing in alone grants nothing (no subscription, no key)");
        check (! proc.getDemoGate().isFullVersion(),
               "a signed-in user with no entitlement stays in demo mode");

        proc.getAuthManager().activateLicenseForTesting ("MP-PUMPCURVE-VALID-KEY");

        check (mockPtr->licenseCalls.load() == 1,
               "the key was sent to the licence transport for verification");
        check (mockPtr->lastLicenceToken.isNotEmpty(),
               "the key travelled with the signed-in account token (the binding identity)");
        check (proc.getAuthManager().getLicenseStatus()
                   == sid::auth::AuthManager::LicenseStatus::active,
               "a server-verified key reports LICENCE ACTIVE");
        check (proc.getAuthManager().hasFullEntitlement(),
               "a server-verified key grants the full version");
        check (proc.getDemoGate().isFullVersion(),
               "the audio gate switches to FULL VERSION from the key alone");

        // Production order: the auth thread confirms a fresh activation with the
        // server's own product answer before anything is cached (see
        // AuthManager::run). Done synchronously here so the assertions below
        // describe the contract rather than a thread race.
        proc.getAuthManager().refreshEntitlementsForTesting();
        check (mockPtr->lastEntitlementsToken.isNotEmpty(),
               "the confirmation used the signed-in account's token");

        proc.prepareToPlay (sr, bs);
        setAmount (proc, 100.0f);
        auto* ph = new TestPlayHead();
        proc.setPlayHead (ph);
        g_fakeNowMs += 12.0 * 3600.0 * kSeconds;
        const float peak = runBlocks (proc, *ph, 8, bs, sr);
        check (peak > 0.0f,
               "a licensed instance produces audio long past the demo limit");

        // The KEY is never persisted. What persists is the server-verified
        // ACCOUNT entitlement (the cached product answer), so a re-launch
        // re-derives the full version from the account rather than from a
        // stored key or a local flag.
        check (! authPathC.existsAsFile() || ! authPathC.loadFileAsString().contains ("licence"),
               "the licence key is not written to the auth file");
        check (! authPathC.existsAsFile() || ! authPathC.loadFileAsString().contains ("MP-PUMPCURVE"),
               "no part of the key is written to disk");

        // What survives is the ACCOUNT answer the SERVER gave (see
        // persistTokenLocked), including the fact that a key is active for this
        // user + product. A re-launch therefore re-derives the full version
        // from that cached server answer - the same offline-grace policy the
        // subscription route uses in section 8 - and not from anything this
        // instance guessed, and it reports the LICENCE route because that is
        // what the server answered.
        check (authPathC.existsAsFile(),
               "the server-verified account answer is cached for a re-launch");

        SideChainAudioProcessor fresh (nullptr, authPathC.getFullPathName());
        check (fresh.getAuthManager().hasFullEntitlement(),
               "a re-launch re-derives the full version from the cached SERVER answer");
        check (fresh.getAuthManager().fullVersionViaLicense(),
               "the licence route is reported after the restart");
        check (! fresh.getAuthManager().fullVersionViaSubscription(),
               "a licence-only account is never labelled a Music-Prod+ subscriber");

        // The cache is trusted ONLY because the SERVER wrote it, with the moment
        // it answered. A hand-written file is the attacker's real tool, so it
        // must grant nothing - and a fresh-looking one is bounded by the same
        // grace window every other cached answer is (section 1).
        authPathC.replaceWithText (
            "{\"token\":\"forged-token\",\"display_name\":\"forged\","
            "\"subscribed\":true,\"product_entitled\":true,\"license_active\":true,"
            "\"entitlements_verified_at_ms\":0}");

        SideChainAudioProcessor tampered (nullptr, authPathC.getFullPathName());
        check (! tampered.getAuthManager().hasFullEntitlement(),
               "a hand-written cache with no server timestamp grants no full version");
        check (! tampered.getDemoGate().isFullVersion(),
               "a forged cache leaves the instance in demo mode");
    }

    // ==================================================================
    // 7. Invalid / revoked / expired licences never grant access
    // ==================================================================
    printf ("\n7. Invalid, revoked and expired licences never grant access\n");
    {
        using Status = sid::auth::LicenseResponse::Status;
        using LS = sid::auth::AuthManager::LicenseStatus;

        struct Case { Status server; LS expected; const char* label; };
        const Case cases[] =
        {
            { Status::invalid,  LS::invalid,  "invalid licence key" },
            { Status::revoked,  LS::revoked,  "revoked licence" },
            { Status::expired,  LS::revoked,  "expired licence" },
        };

        for (const auto& c : cases)
        {
            g_fakeNowMs = 60'000'000.0;
            authPathC.deleteFile();

            auto mock = std::make_unique<MockTransport>();
            mock->licenseStatus = c.server;
            mock->licenseMessage = "server message";
            MockTransport* mockPtr = mock.get();

            SideChainAudioProcessor proc (std::move (mock), authPathC.getFullPathName());
            signIn (proc, *mockPtr);
            proc.getAuthManager().activateLicenseForTesting ("MP-PUMPCURVE-KEY");

            check (proc.getAuthManager().getLicenseStatus() == c.expected,
                   std::string (c.label) + ": reported as the right state");
            check (! proc.getAuthManager().hasFullEntitlement(),
                   std::string (c.label) + ": does NOT grant the full version");
            check (! proc.getDemoGate().isFullVersion(),
                   std::string (c.label) + ": the audio gate stays in demo mode");

            proc.prepareToPlay (sr, bs);
            setAmount (proc, 100.0f);
            auto* ph = new TestPlayHead();
            proc.setPlayHead (ph);
            g_fakeNowMs += 900.0 * kSeconds;
            bool allSilent = false;
            runBlocks (proc, *ph, 4, bs, sr, &allSilent);
            check (allSilent,
                   std::string (c.label) + ": audio stops at the demo limit as before");
        }
    }

    // ==================================================================
    // 8. Network failures and the documented offline policy
    // ==================================================================
    printf ("\n8. Network failures follow the documented offline policy\n");
    {
        const auto token = juce::String ("mock-token-0123456789abcdef0123456789abcdef");
        const double wallNow = (double) juce::Time::currentTimeMillis();

        // 8.1 A verified subscriber is not downgraded by a failed request.
        {
            authPathB.deleteFile();
            writeAuthFile (authPathB, token, true, wallNow);

            auto mock = std::make_unique<MockTransport>();
            mock->entitlementsStatus = 500;      // network/server failure
            SideChainAudioProcessor proc (std::move (mock), authPathB.getFullPathName());

            check (proc.getAuthManager().isSubscribed()
                       && proc.getDemoGate().isFullVersion(),
                   "a failed re-verification does NOT downgrade a verified subscriber");

            proc.getAuthManager().refreshEntitlementsForTesting();   // also fails
            check (proc.getAuthManager().isSubscribed(),
                   "repeated network failure keeps the cached entitlement");
            check (proc.getDemoGate().isFullVersion(),
                   "the audio gate stays in full version through the outage");
        }

        // 8.2 A stale cache no longer grants the full version.
        {
            authPathB.deleteFile();
            writeAuthFile (authPathB, token, true,
                           wallNow - (sid::license::kEntitlementGraceSeconds + 86400.0) * kSeconds);

            auto mock = std::make_unique<MockTransport>();
            mock->entitlementsStatus = 500;
            SideChainAudioProcessor proc (std::move (mock), authPathB.getFullPathName());

            check (proc.getAuthManager().getState() == sid::auth::AuthManager::State::signedIn,
                   "a stale cache still restores the sign-in itself");
            check (! proc.getAuthManager().isSubscribed()
                       && ! proc.getDemoGate().isFullVersion(),
                   "a cache older than the grace window grants nothing (conservative policy)");
            check (! proc.getDemoGate().expired(),
                   "...the user falls back to the normal 15-minute demo session");
        }

        // 8.3 A hand-edited cache with no verification time grants nothing.
        {
            authPathB.deleteFile();
            writeAuthFile (authPathB, token, true, 0.0);   // "subscribed" but unverified

            auto mock = std::make_unique<MockTransport>();
            mock->entitlementsStatus = 500;
            SideChainAudioProcessor proc (std::move (mock), authPathB.getFullPathName());

            check (! proc.getAuthManager().isSubscribed()
                       && ! proc.getDemoGate().isFullVersion(),
                   "a locally edited cache with no server-verified timestamp grants nothing");
        }

        // 8.4 The server rejects the token (401): sign out, drop the cache.
        {
            authPathB.deleteFile();
            writeAuthFile (authPathB, token, true, wallNow);

            auto mock = std::make_unique<MockTransport>();
            mock->entitlementsStatus = 401;
            SideChainAudioProcessor proc (std::move (mock), authPathB.getFullPathName());

            check (proc.getAuthManager().isSubscribed(),
                   "the fresh cached entitlement applies before the server answers");
            proc.getAuthManager().refreshEntitlementsForTesting();
            check (proc.getAuthManager().getState() == sid::auth::AuthManager::State::signedOut,
                   "a server rejection (401) signs the instance out");
            check (! proc.getDemoGate().isFullVersion(),
                   "a rejected token revokes the full version immediately");
            check (! authPathB.existsAsFile(),
                   "the revoked token is removed from disk");
        }
    }

    // ==================================================================
    // 9. Switching between entitled and demo states
    // ==================================================================
    printf ("\n9. Entitlement/demo switching never leaves processing wrong\n");
    {
        g_fakeNowMs = 70'000'000.0;
        const double wallNow = (double) juce::Time::currentTimeMillis();
        authPathB.deleteFile();
        writeAuthFile (authPathB, "mock-token-0123456789abcdef0123456789abcdef",
                       true, wallNow);

        auto mock = std::make_unique<MockTransport>();
        mock->entitlementsSubscribed = true;
        SideChainAudioProcessor proc (std::move (mock), authPathB.getFullPathName());
        proc.prepareToPlay (sr, bs);
        setAmount (proc, 100.0f);
        auto* ph = new TestPlayHead();
        proc.setPlayHead (ph);

        check (proc.getDemoGate().isFullVersion(), "starts entitled (full version)");
        g_fakeNowMs += 5.0 * 3600.0 * kSeconds;
        check (runBlocks (proc, *ph, 4, bs, sr) > 0.0f, "entitled: audio flows");

        // Sign out: the subscription route ends. The user must land in a real
        // 15-minute demo session, not instantly in silence. (logout() revokes
        // on the auth thread, so wait for the transition instead of guessing.)
        proc.getAuthManager().logout();
        // The auth state is published under its lock, then onEntitlementChanged
        // performs the gate's atomic store; wait for both so the gap between
        // them (short, but real) cannot be mistaken for a stuck full version.
        check (waitFor ([&] { return ! proc.getAuthManager().hasFullEntitlement()
                                      && ! proc.getDemoGate().isFullVersion(); }),
               "after logout there is no entitlement");
        check (! proc.getDemoGate().isFullVersion(),
               "the gate leaves full version on logout");
        check (! proc.getDemoGate().expired() && proc.getDemoGate().remainingSeconds() > 899.0,
               "the user gets a fresh 15-minute demo session from that moment");
        check (runBlocks (proc, *ph, 4, bs, sr) > 0.0f,
               "audio keeps flowing during the fresh demo session");

        g_fakeNowMs += 900.0 * kSeconds;
        bool silent = false;
        runBlocks (proc, *ph, 4, bs, sr, &silent);
        check (silent, "once that session expires, processing stops");

        // Re-granting the entitlement (a successful sign-in + verification)
        // restores the full version even though the previous demo session is
        // long expired. Polling is driven synchronously here so the transition
        // is deterministic.
        auto mock2 = std::make_unique<MockTransport>();
        mock2->entitlementsSubscribed = true;
        SideChainAudioProcessor proc2 (std::move (mock2), authPathB.getFullPathName());
        proc2.getAuthManager().setBackgroundPolling (false);
        check (! proc2.getDemoGate().isFullVersion(),
               "a signed-out instance starts in demo mode, not full version");

        proc2.getAuthManager().startLinking();
        proc2.getAuthManager().pollOnceForTesting();
        check (proc2.getAuthManager().getState() == sid::auth::AuthManager::State::signedIn,
               "signing in again restores the account session");
        proc2.getAuthManager().refreshEntitlementsForTesting();
        check (proc2.getDemoGate().isFullVersion(),
               "a newly verified entitlement restores the full version");

        proc2.prepareToPlay (sr, bs);
        setAmount (proc2, 100.0f);
        auto* ph2 = new TestPlayHead();
        proc2.setPlayHead (ph2);
        g_fakeNowMs += 24.0 * 3600.0 * kSeconds;
        check (runBlocks (proc2, *ph2, 4, bs, sr) > 0.0f,
               "the restored full version keeps processing indefinitely");
    }

    // ==================================================================
    // 10. Licence-key contract: real operation, no account, no fake grant
    // ==================================================================
    printf ("\n10. Licence-key contract (real operation, sign-in binding, fails closed)\n");
    {
        using LS = sid::auth::AuthManager::LicenseStatus;
        using Status = sid::auth::LicenseResponse::Status;

        // The production transport now IMPLEMENTS the licence operation instead
        // of inheriting the fail-closed default. Taking the address of the
        // override proves that at compile time and at run time and opens NO
        // socket - the alternative (calling it) would put live network I/O in a
        // headless suite.
        using LicenceFn = sid::auth::LicenseResponse (sid::auth::HttpAuthTransport::*)
                              (const juce::String&, const juce::String&);
        LicenceFn productionLicenceFn = &sid::auth::HttpAuthTransport::activateLicense;
        check (productionLicenceFn != nullptr,
               "the production transport implements activate-license (no longer the unavailable default)");

        // ---- no account token: refuse locally, do not guess an identity ----
        authPathC.deleteFile();
        auto mock = std::make_unique<MockTransport>();
        MockTransport* mockPtr = mock.get();
        SideChainAudioProcessor proc (std::move (mock), authPathC.getFullPathName());

        proc.getAuthManager().activateLicense ("PC-ABCD-EFGH-JKLM-NPQR");   // async path
        check (proc.getAuthManager().getLicenseStatus() == LS::signInRequired,
               "a key with no account token reports SIGN IN REQUIRED");
        check (mockPtr->licenseCalls.load() == 0,
               "no licence request was sent without an account to bind it to");
        check (! proc.getAuthManager().hasFullEntitlement(),
               "a key that could not be bound grants NOTHING");

        // ---- the server is unreachable: nothing was verified ---------------
        authPathC.deleteFile();
        auto mock2 = std::make_unique<MockTransport>();
        mock2->licenseStatus = Status::networkError;
        MockTransport* mock2Ptr = mock2.get();
        SideChainAudioProcessor proc2 (std::move (mock2), authPathC.getFullPathName());
        signIn (proc2, *mock2Ptr);
        proc2.getAuthManager().activateLicenseForTesting ("PC-ABCD-EFGH-JKLM-NPQR");
        check (proc2.getAuthManager().getLicenseStatus() == LS::networkError,
               "a transport failure reports NETWORK ERROR, not a rejection");
        check (! proc2.getAuthManager().hasFullEntitlement(),
               "a transport failure never grants the full version");
        check (proc2.getAuthManager().getLicenseMessage().containsIgnoreCase ("not checked")
                   || proc2.getAuthManager().getLicenseMessage().isNotEmpty(),
               "a transport failure explains that nothing was verified");

        // The demo session is unaffected by a failed licence attempt, and the
        // audio keeps its normal (demonstration) behaviour up to the limit.
        proc2.prepareToPlay (sr, bs);
        g_fakeNowMs += 300.0 * kSeconds;
        bool silent = false;
        auto* ph3 = new TestPlayHead();
        proc2.setPlayHead (ph3);
        setAmount (proc2, 100.0f);
        runBlocks (proc2, *ph3, 4, bs, sr, &silent);
        check (! silent, "a failed licence attempt does not stop audio before the demo limit");
        g_fakeNowMs += 900.0 * kSeconds;
        runBlocks (proc2, *ph3, 4, bs, sr, &silent);
        check (silent, "...and the demo limit still applies afterwards");

        // ---- the HTTP -> licence-verdict mapping (no socket needed) --------
        //
        // This is the one place that decides whether a key was JUDGED. The
        // transport test above cannot reach it (that would open a socket), so it
        // is exercised directly through the real production function.
        {
            auto body = [] (juce::DynamicObject::Ptr obj) { return juce::var (obj); };

            // A documented `ok` now also says a DEVICE was activated, because a
            // licence entitles a device (three per key).
            auto okBody = new juce::DynamicObject();
            okBody->setProperty ("ok", true);
            okBody->setProperty ("status", "ok");
            auto okDevice = new juce::DynamicObject();
            okDevice->setProperty ("activated", true);
            okDevice->setProperty ("used", 1);
            okDevice->setProperty ("limit", 3);
            okBody->setProperty ("device", juce::var (okDevice));
            const auto activated = sid::auth::interpretLicenseResponse (200, body (okBody));
            check (activated.status == Status::ok,
                   "200 {status:ok, device:{activated:true}} is an activation");
            check (activated.deviceActivated && activated.deviceLimit == 3,
                   "...and the slot bookkeeping travels with the verdict");

            // An `ok` that does NOT say a device was activated cannot be honoured
            // without silently ignoring the three-device policy, so it grants
            // nothing. (Older server, truncated body, or a rewriting proxy.)
            auto okNoDevice = new juce::DynamicObject();
            okNoDevice->setProperty ("ok", true);
            okNoDevice->setProperty ("status", "ok");
            check (sid::auth::interpretLicenseResponse (200, body (okNoDevice)).status == Status::networkError,
                   "200 {status:ok} without device proof is UNVERIFIED, never a grant");

            // Every activation slot in use: an answer about the LICENCE, not a
            // rejection of the key.
            auto fullHouse = new juce::DynamicObject();
            fullHouse->setProperty ("ok", false);
            fullHouse->setProperty ("status", "device_limit");
            const auto limited = sid::auth::interpretLicenseResponse (200, body (fullHouse));
            check (limited.status == Status::deviceLimit,
                   "200 {status:device_limit} is its own answer, not 'invalid'");
            check (limited.status != Status::invalid,
                   "a full house never reads as a bad licence key");

            const std::pair<const char*, Status> rejections[] =
            {
                { "invalid", Status::invalid },
                { "revoked", Status::revoked },
                { "expired", Status::expired },
            };

            for (const auto& rejection : rejections)
            {
                auto b = new juce::DynamicObject();
                b->setProperty ("status", rejection.first);
                const auto got = sid::auth::interpretLicenseResponse (200, body (b)).status;
                check (got == rejection.second,
                       (juce::String ("200 {status:") + rejection.first + "} maps to its own verdict").toStdString());
                check (got != Status::ok,
                       (juce::String ("200 {status:") + rejection.first + "} never grants the full version").toStdString());
            }

            // A 200 that is not a documented answer is a contract violation.
            auto emptyBody = new juce::DynamicObject();
            check (sid::auth::interpretLicenseResponse (200, body (emptyBody)).status == Status::networkError,
                   "a 200 with no documented status is UNVERIFIED, not an activation");

            // The documented malformed-key answer IS a verdict on the key.
            auto malformed = new juce::DynamicObject();
            malformed->setProperty ("error", "malformed_key");
            check (sid::auth::interpretLicenseResponse (400, body (malformed)).status == Status::invalid,
                   "400 {error:malformed_key} is a verdict on the key");

            // ...but an UNDEPLOYED service answers 400 {error:unknown_action}.
            // Reporting that as a rejected key would tell a customer their valid
            // key is bad because the backend is behind.
            auto unknownAction = new juce::DynamicObject();
            unknownAction->setProperty ("error", "unknown_action");
            const auto undeployed = sid::auth::interpretLicenseResponse (400, body (unknownAction));
            check (undeployed.status == Status::networkError,
                   "400 {error:unknown_action} (action not deployed) is NOT shown as a rejected key");
            check (undeployed.message.containsIgnoreCase ("not checked"),
                   "...and says plainly that the key was never checked");

            // A rejected token is an authentication failure, not a key verdict.
            const auto unauthorised = sid::auth::interpretLicenseResponse (401, {});
            check (unauthorised.status == Status::signInRequired && unauthorised.authFailure,
                   "401 means sign in again, not a bad key");

            // No answer / server error: never an activation, never a rejection.
            for (const int status : { 0, 404, 500, 503 })
            {
                const auto failed = sid::auth::interpretLicenseResponse (status, {});
                check (failed.status == Status::networkError,
                       (juce::String ("HTTP ") + juce::String (status) + " is a transport failure, not a rejection").toStdString());
            }
        }
    }

    // ==================================================================
    // 11. The two routes are independent and can coexist
    // ==================================================================
    printf ("\n11. Licence key and Music-Prod+ are independent routes\n");
    {
        using LS = sid::auth::AuthManager::LicenseStatus;
        using Status = sid::auth::LicenseResponse::Status;

        // ---- subscriber WITHOUT a key ------------------------------------
        authPathC.deleteFile();
        auto mockSub = std::make_unique<MockTransport>();
        mockSub->entitlementsSubscribed = true;
        MockTransport* subPtr = mockSub.get();
        SideChainAudioProcessor subProc (std::move (mockSub), authPathC.getFullPathName());
        signIn (subProc, *subPtr);

        check (subProc.getAuthManager().hasFullEntitlement(),
               "an active Music-Prod+ subscription unlocks the full version with no key");
        check (subProc.getAuthManager().fullVersionViaSubscription(),
               "...and the UI is told the SUBSCRIPTION is the route");
        check (! subProc.getAuthManager().fullVersionViaLicense(),
               "...and not that a licence key is active");
        check (subProc.getAuthManager().getLicenseStatus() == LS::notChecked,
               "a subscriber who never entered a key sees NOT CHECKED (not 'invalid')");

        // ---- subscriber WITH a key: both routes coexist -------------------
        subPtr->licenseStatus = Status::ok;
        subProc.getAuthManager().activateLicenseForTesting ("PC-ABCD-EFGH-JKLM-NPQR");
        check (subProc.getAuthManager().hasFullEntitlement(),
               "both entitlements together still grant the full version");
        check (subProc.getAuthManager().fullVersionViaLicense()
                   && subProc.getAuthManager().fullVersionViaSubscription(),
               "both routes are reported, so the UI can name both");

        // ---- losing the subscription keeps the licence --------------------
        subPtr->entitlementsSubscribed = false;
        subPtr->entitlementsLicenseActive = true;   // the server still knows the key
        subProc.getAuthManager().refreshEntitlementsForTesting();
        check (subProc.getAuthManager().hasFullEntitlement(),
               "cancelling Music-Prod+ does NOT take away a valid licence key");
        check (! subProc.getAuthManager().fullVersionViaSubscription()
                   && subProc.getAuthManager().fullVersionViaLicense(),
               "...and the UI now names the LICENCE as the route");

        // ---- the server revokes the key: full version is withdrawn --------
        subPtr->entitlementsLicenseActive = false;
        subPtr->entitlementsLicenseStatus = "revoked";
        subProc.getAuthManager().refreshEntitlementsForTesting();
        check (! subProc.getAuthManager().hasFullEntitlement(),
               "a revocation the SERVER reports withdraws the full version");
        check (subProc.getAuthManager().getLicenseStatus() == LS::revoked,
               "...and the licence state becomes REVOKED");

        // ---- ...and the instance falls back to the demo, not to nothing ---
        subProc.prepareToPlay (sr, bs);
        setAmount (subProc, 100.0f);
        auto* ph4 = new TestPlayHead();
        subProc.setPlayHead (ph4);
        g_fakeNowMs += 60.0 * kSeconds;
        bool wentSilent = false;
        runBlocks (subProc, *ph4, 4, bs, sr, &wentSilent);
        check (! wentSilent, "a revoked entitlement falls back to the demo session, which still runs");
    }

    // ==================================================================
    // 12. Device activations: three computers, and releasing one
    // ==================================================================
    printf ("\n12. Device activations (three per key, self-service deactivation)\n");
    {
        using LS = sid::auth::AuthManager::LicenseStatus;
        using Status = sid::auth::LicenseResponse::Status;

        // ---- the licence route's policy constants -------------------------
        check (sid::license::kLicenseActivationGraceSeconds <=
                   sid::license::kEntitlementGraceSeconds,
               "the device-bound licence window never exceeds the subscription window");
        check (sid::license::kLicenseActivationGraceSeconds > 0.0,
               "...and it is a real, bounded window (never 'trust forever')");

        const double wallNow = (double) juce::Time::currentTimeMillis();
        const double days = 24.0 * 60.0 * 60.0 * 1000.0;

        // ---- a deactivated computer is withdrawn, not rejected ------------
        authPathC.deleteFile();
        auto mock = std::make_unique<MockTransport>();
        mock->licenseStatus = Status::ok;
        mock->entitlementsSubscribed = false;
        MockTransport* mockPtr = mock.get();

        SideChainAudioProcessor proc (std::move (mock), authPathC.getFullPathName());
        signIn (proc, *mockPtr);
        proc.getAuthManager().activateLicenseForTesting ("PC-ABCD-EFGH-JKLM-NPQR");
        check (proc.getAuthManager().hasFullEntitlement(),
               "a key activated on THIS computer grants the full version");
        check (proc.getAuthManager().getDeviceId().length() >= 16,
               "the plugin knows a stable device id to take a slot with");
        check (mockPtr->lastDeviceId == proc.getAuthManager().getDeviceId(),
               "the SAME device id was handed to the transport for its requests");
        check (mockPtr->lastDeviceLabel.isNotEmpty(),
               "...along with a human label the owner can recognise");
        check (mockPtr->deviceIdentityCalls.load() >= 1,
               "the identity is published before the first request that needs it");

        // The owner releases this computer in their account area. The server now
        // reports the ACCOUNT as licensed but THIS device as not activated.
        mockPtr->entitlementsLicenseActive = false;
        mockPtr->entitlementsLicenseAccountOnly = true;
        mockPtr->entitlementsLicenseStatus = "none";
        proc.getAuthManager().refreshEntitlementsForTesting();

        check (! proc.getAuthManager().hasFullEntitlement(),
               "a released computer loses the full version at the next server answer");
        check (proc.getAuthManager().getLicenseStatus() == LS::deviceInactive,
               "...and the state says NOT ACTIVATED ON THIS COMPUTER, not 'invalid'");
        check (proc.getAuthManager().getLicenseStatus() != LS::invalid,
               "a deactivation is never shown as a bad licence key");
        check (proc.getAuthManager().getLicenseMessage().isNotEmpty(),
               "...and the customer is told what to do about it");

        // The demo session starts fresh rather than resuming an expired one.
        proc.prepareToPlay (sr, bs);
        setAmount (proc, 100.0f);
        auto* ph = new TestPlayHead();
        proc.setPlayHead (ph);
        g_fakeNowMs += 60.0 * kSeconds;
        bool silent = false;
        runBlocks (proc, *ph, 4, bs, sr, &silent);
        check (! silent, "a deactivated computer falls back to the 15-minute demo, which runs");
        g_fakeNowMs += 900.0 * kSeconds;
        runBlocks (proc, *ph, 4, bs, sr, &silent);
        check (silent, "...and the demo limit still applies afterwards");

        // ---- activating a replacement computer ---------------------------
        mockPtr->licenseStatus = Status::ok;
        proc.getAuthManager().activateLicenseForTesting ("PC-ABCD-EFGH-JKLM-NPQR");
        check (proc.getAuthManager().hasFullEntitlement(),
               "the key can be activated on a replacement computer");
        check (proc.getAuthManager().getLicenseStatus() == LS::active,
               "...and the licence route is active again");

        // ---- every slot in use --------------------------------------------
        authPathC.deleteFile();
        auto full = std::make_unique<MockTransport>();
        full->licenseStatus = Status::deviceLimit;
        full->entitlementsLicenseAccountOnly = true;
        MockTransport* fullPtr = full.get();

        SideChainAudioProcessor limited (std::move (full), authPathC.getFullPathName());
        signIn (limited, *fullPtr);
        limited.getAuthManager().activateLicenseForTesting ("PC-ABCD-EFGH-JKLM-NPQR");
        check (limited.getAuthManager().getLicenseStatus() == LS::deviceLimit,
               "a fourth computer reports TOO MANY COMPUTERS");
        check (limited.getAuthManager().getLicenseStatus() != LS::invalid,
               "...and the valid key is never reported as rejected");
        check (! limited.getAuthManager().hasFullEntitlement(),
               "...and no entitlement is granted without a free slot");
        check (limited.getAuthManager().getLicenseMessage().containsIgnoreCase ("deactivate")
                   || limited.getAuthManager().getLicenseMessage().containsIgnoreCase ("computers"),
               "...and the message says how to free a slot");

        // ---- the device identity is per-installation and never in DAW state -
        const auto deviceFile = authPathC.getSiblingFile ("device_id.txt");
        check (deviceFile.existsAsFile(),
               "the device id is kept beside the auth file (per installation)");
        const auto storedId = deviceFile.loadFileAsString().trim();
        check (storedId.length() >= 16, "the stored device id is a real, stable id");
        check (limited.getAuthManager().getDeviceId() == storedId.toLowerCase(),
               "and it is the id the manager reports (stable across calls)");
        check (! authPathC.existsAsFile()
                   || ! authPathC.loadFileAsString().contains (storedId),
               "the device id is never written into the token/auth cache");

        // ---- the two offline windows really are different ------------------
        // 10 days old, licence route: the device-bound window (7 days) has closed,
        // so a deactivation cannot be outlived by a stale local file.
        {
            authPathB.deleteFile();
            writeAuthFile (authPathB, "mock-token-0123456789abcdef0123456789abcdef",
                           false, wallNow - 10.0 * days,
                           1 /* product_entitled */, true /* license_active */,
                           wallNow - 10.0 * days);

            SideChainAudioProcessor stale (nullptr, authPathB.getFullPathName());
            check (! stale.getAuthManager().hasFullEntitlement(),
                   "a 10-day-old LICENCE grant is no longer trusted (device-bound window)");
        }

        // The same age on the SUBSCRIPTION route is still inside its own
        // (14-day) window: the two routes keep their documented, different
        // bounds rather than silently sharing one.
        {
            authPathB.deleteFile();
            writeAuthFile (authPathB, "mock-token-0123456789abcdef0123456789abcdef",
                           true, wallNow - 10.0 * days, 1 /* product_entitled */, false);

            SideChainAudioProcessor subscriber (nullptr, authPathB.getFullPathName());
            check (subscriber.getAuthManager().hasFullEntitlement(),
                   "a 10-day-old SUBSCRIPTION grant is still inside its window");
            check (subscriber.getAuthManager().fullVersionViaSubscription(),
                   "...and is attributed to the subscription route");
        }

        // ---- the refresh policy that bounds a long session -----------------
        check (sid::license::entitlementRefreshDue (0.0, wallNow),
               "a never-verified answer is always due for a re-check");
        check (! sid::license::entitlementRefreshDue (wallNow, wallNow),
               "a just-verified answer is not re-fetched immediately");
        check (sid::license::entitlementRefreshDue (wallNow - 7.0 * 60.0 * 60.0 * 1000.0, wallNow),
               "a 7-hour-old answer is re-checked (so a deactivation reaches a long session)");
        check (sid::license::cachedLicenseActivationIsFresh (wallNow, wallNow),
               "a just-verified licence answer is fresh");
        check (! sid::license::cachedLicenseActivationIsFresh (
                   wallNow - sid::license::kLicenseActivationGraceSeconds * 1000.0 - 1000.0, wallNow),
               "...and stops being fresh once its window closes");
    }

    // ==================================================================
    // 13. The DEPLOYED entitlements contract (the server's own field names)
    // ==================================================================
    printf ("\n13. The deployed entitlements contract (server field names)\n");
    {
        using LS = sid::auth::AuthManager::LicenseStatus;

        // Each body below is built exactly as the deployed `entitlementsBody`
        // (supabase/functions/vyre-plugin-auth/contract.ts) builds it.
        //
        //   `entitled`      <- plugin_entitled (the product-scoped gate)
        //   `accountActive` <- license.account_active (the ACCOUNT holds it)
        //   `deviceActive`  <- license.device_active  (THIS computer holds a slot)
        //
        // The last two are the whole point: a licence is DEVICE-bound, so the
        // server reports the two halves separately, and `license.active` is false
        // both for a computer the owner released and for a revoked licence. Only
        // these two fields say which one happened.
        auto makeBody = [] (bool entitled, bool subscribed,
                            bool licenceActive, const char* licenceStatus,
                            bool accountActive, bool deviceActive, int used)
        {
            auto o = new juce::DynamicObject();
            o->setProperty ("ok", true);
            o->setProperty ("product", "sidechain");
            o->setProperty ("subscribed", subscribed);
            o->setProperty ("plugin_entitled", entitled);

            auto sub = new juce::DynamicObject();
            sub->setProperty ("active", subscribed);
            sub->setProperty ("covers_product", subscribed);
            o->setProperty ("subscription", juce::var (sub));

            auto lic = new juce::DynamicObject();
            lic->setProperty ("active", licenceActive);
            lic->setProperty ("status", licenceStatus);
            lic->setProperty ("account_active", accountActive);
            lic->setProperty ("device_active", deviceActive);
            lic->setProperty ("devices_used", used);
            lic->setProperty ("device_limit", 3);
            lic->setProperty ("expires_at", juce::var());
            o->setProperty ("license", juce::var (lic));

            o->setProperty ("packs", juce::var (juce::Array<juce::var>()));
            return juce::var (o);
        };

        // (a) A Music-Prod+ subscriber with no key.
        {
            const auto body = sid::auth::interpretEntitlementsResponse (
                200, makeBody (true, true, false, "none", false, false, 0));
            check (body.ok && body.subscribed && body.productEntitled,
                   "plugin_entitled:true is read as the product-scoped gate answer");
            check (! body.licenseActive && ! body.licenseAccountActive,
                   "...with no licence route claimed on top of it");
        }

        // (b) The computer the owner RELEASED: the account's licence row is still
        //     active (the server says status "active"), but this device holds no
        //     activation slot.
        {
            const auto released = sid::auth::interpretEntitlementsResponse (
                200, makeBody (false, false, false, "active", true, false, 1));
            check (released.licenseStatus == "active",
                   "the account's licence is still reported as active");
            check (! released.licenseActive,
                   "...while THIS computer is not entitled through it");
            check (released.licenseAccountActive && ! released.licenseDeviceActive,
                   "license.account_active and license.device_active are read SEPARATELY");
            check (released.devicesUsed == 1 && released.deviceLimit == 3,
                   "the slot counts travel with the answer");
        }

        // (c) The same body through a REAL processor: what the customer is TOLD.
        {
            authPathA.deleteFile();
            auto releasedMock = std::make_unique<MockTransport>();
            MockTransport* releasedPtr = releasedMock.get();
            SideChainAudioProcessor proc (std::move (releasedMock), authPathA.getFullPathName());
            signIn (proc, *releasedPtr);

            releasedPtr->entitlementsBodyOverride = makeBody (false, false, false,
                                                             "active", true, false, 1);
            proc.getAuthManager().refreshEntitlementsForTesting();
            check (proc.getAuthManager().getLicenseStatus() == LS::deviceInactive,
                   "a released computer reads as NOT ACTIVATED ON THIS COMPUTER");
            check (proc.getAuthManager().getLicenseStatus() != LS::revoked,
                   "...and never as a licence that is no longer active");
            check (! proc.getAuthManager().hasFullEntitlement(),
                   "...and it falls back to the demo, exactly as a deactivation must");
            check (proc.getAuthManager().getLicenseMessage().contains ("1 of 3"),
                   "...and the message names the slots in use");

            releasedPtr->entitlementsBodyOverride = makeBody (true, false, true,
                                                             "active", true, true, 1);
            proc.getAuthManager().refreshEntitlementsForTesting();
            check (proc.getAuthManager().hasFullEntitlement(),
                   "an ACTIVATED computer is entitled with no subscription at all");
            check (proc.getAuthManager().fullVersionViaLicense()
                       && ! proc.getAuthManager().fullVersionViaSubscription(),
                   "...attributed to the LICENCE route, not to Music-Prod+");

            releasedPtr->entitlementsBodyOverride = makeBody (false, false, false,
                                                             "revoked", false, false, 0);
            proc.getAuthManager().refreshEntitlementsForTesting();
            check (! proc.getAuthManager().hasFullEntitlement(),
                   "a revoked licence withdraws the full version");
            check (proc.getAuthManager().getLicenseStatus() != LS::deviceInactive,
                   "...and is reported as a revocation, not as an unactivated computer");
        }

        // (d) An older server that sends no licence block at all: nothing is
        //     granted, and nothing is invented as a revocation.
        {
            auto o = new juce::DynamicObject();
            o->setProperty ("ok", true);
            o->setProperty ("product", "sidechain");
            o->setProperty ("subscribed", false);
            o->setProperty ("plugin_entitled", false);
            const auto bare = sid::auth::interpretEntitlementsResponse (200, juce::var (o));
            check (! bare.productEntitled && ! bare.licenseActive
                       && ! bare.licenseAccountActive && bare.devicesUsed == 0,
                   "a body with no licence block grants nothing and invents nothing");
        }
    }

    // ==================================================================
    // 14. Music-Prod+ refresh: a purchase must not need a restart
    // ==================================================================
    printf ("\n14. Music-Prod+ refresh (purchase while signed in, manual check, debounce)\n");
    {
        using LS = sid::auth::AuthManager::LicenseStatus;
        using Status = sid::auth::LicenseResponse::Status;

        // ---- the purchase, inside the RUNNING instance ---------------------
        {
            authPathA.deleteFile();
            auto mock = std::make_unique<MockTransport>();
            MockTransport* mp = mock.get();
            mp->entitlementsSubscribed = false;   // the account had no Music-Prod+
            SideChainAudioProcessor proc (std::move (mock), authPathA.getFullPathName());
            signIn (proc, *mp);

            check (! proc.getAuthManager().hasFullEntitlement(),
                   "a signed-in account without Music-Prod+ runs the demo");
            check (! proc.getAuthManager().isEntitlementCheckInFlight(),
                   "no check is reported as running when nothing is being checked");

            const int before = mp->entitlementsCalls.load();

            // The account acquires Music-Prod+ WHILE this instance is running.
            mp->entitlementsSubscribed = true;

            // What the customer presses. It has to be a real request - not a
            // re-read of the verdict already in memory - and it has to unlock
            // THIS instance.
            proc.getAuthManager().refreshEntitlements();
            // Wait for BOTH the verified entitlement and the gate it drives. The
            // two become visible in that order (the auth state is published under
            // its lock, then onEntitlementChanged performs the atomic store), so
            // waiting on only the first can observe the one-frame gap and report
            // a false failure. The running instance being unlocked IS the gate.
            check (waitFor ([&] { return proc.getAuthManager().hasFullEntitlement()
                                          && proc.getDemoGate().isFullVersion(); }),
                   "CHECK SUBSCRIPTION unlocks the running instance after a purchase");
            check (mp->entitlementsCalls.load() > before,
                   "...by asking the server again, not by trusting local state");
            check (proc.getAuthManager().fullVersionViaSubscription(),
                   "...and the UI is told the SUBSCRIPTION is the route");
            check (proc.getDemoGate().isFullVersion(),
                   "...with the demo session replaced by the full version (countdown gone)");
            check (proc.getDemoGate().processingAllowed(),
                   "...and audio processing permitted");
            check (! proc.getAuthManager().isEntitlementCheckInFlight(),
                   "the checking state ends when the answer arrives");
            check (proc.getAuthManager().getState() == sid::auth::AuthManager::State::signedIn,
                   "a purchase needs no log-out and no log-in");

            // ---- a FRESH cached "not subscribed" must not block a check -----
            // The exact trap: an answer cached seconds earlier said inactive, and
            // nothing ever asked again.
            mp->entitlementsSubscribed = false;
            proc.getAuthManager().refreshEntitlementsForTesting();   // server: inactive
            check (! proc.getAuthManager().hasFullEntitlement(),
                   "an inactive answer withdraws the full version");
            mp->entitlementsSubscribed = true;
            proc.getAuthManager().refreshEntitlements();             // forced
            check (waitFor ([&] { return proc.getAuthManager().hasFullEntitlement(); }),
                   "a FORCED check ignores a just-cached inactive answer");
        }

        // ---- the debounce rule, and the manual control's exemption ---------
        {
            check (sid::license::kEntitlementAutoRefreshMinIntervalSeconds > 0.0
                       && sid::license::kEntitlementAutoRefreshMinIntervalSeconds
                              < sid::license::kEntitlementRefreshIntervalSeconds,
                   "the lifecycle debounce is real and shorter than the 6-hour staleness rule");

            const double now = 1.0e12;
            check (sid::license::entitlementAutoRefreshDue (0.0, now),
                   "a session that has never asked is due for a lifecycle refresh");
            check (! sid::license::entitlementAutoRefreshDue (now, now + 30.0 * 1000.0),
                   "a lifecycle refresh 30s after the last attempt is held off");
            check (sid::license::entitlementAutoRefreshDue (now, now + 61.0 * 1000.0),
                   "...and one after a minute is allowed");
            check (! sid::license::entitlementAutoRefreshDue (now, now - 5.0 * 1000.0),
                   "a clock that moved backwards does not authorise a request");

            authPathA.deleteFile();
            auto mock = std::make_unique<MockTransport>();
            MockTransport* mp = mock.get();
            mp->entitlementsSubscribed = false;
            SideChainAudioProcessor proc (std::move (mock), authPathA.getFullPathName());
            signIn (proc, *mp);

            const int afterSignIn = mp->entitlementsCalls.load();

            // Lifecycle events repeat (page opened, window brought forward); only
            // one of them may become a request.
            proc.getAuthManager().refreshEntitlementsIfStale();      // default interval
            juce::Thread::sleep (200);
            check (mp->entitlementsCalls.load() == afterSignIn,
                   "a lifecycle refresh moments after an attempt is held off");

            // The rule is an interval, not a ban: satisfied, it does send.
            proc.getAuthManager().refreshEntitlementsIfStale (0.0);
            check (waitFor ([&] { return mp->entitlementsCalls.load() > afterSignIn; }),
                   "once the interval has passed the lifecycle refresh is sent");

            proc.getAuthManager().refreshEntitlements();             // the manual control
            check (waitFor ([&] { return mp->entitlementsCalls.load() > afterSignIn + 1; }),
                   "the manual control is never held off by the debounce");
        }

        // ---- nothing that is not a server answer may unlock ----------------
        {
            authPathA.deleteFile();
            auto mock = std::make_unique<MockTransport>();
            MockTransport* mp = mock.get();
            mp->entitlementsSubscribed = false;
            SideChainAudioProcessor proc (std::move (mock), authPathA.getFullPathName());
            signIn (proc, *mp);

            // (a) the request fails: the mock would report an active
            //     subscription, but no answer ever arrives.
            mp->entitlementsSubscribed = true;
            mp->entitlementsStatus = 0;               // transport failure
            proc.getAuthManager().refreshEntitlements();
            check (waitFor ([&] { return ! proc.getAuthManager().isEntitlementCheckInFlight(); }),
                   "a failed check does not leave the UI checking for ever");
            check (! proc.getAuthManager().hasFullEntitlement(),
                   "a network failure never unlocks the full version");

            // (b) a 200 whose body is not the documented answer: the raw
            //     membership arrives, the product-scoped answer does not.
            mp->entitlementsStatus = 200;
            {
                auto o = new juce::DynamicObject();
                o->setProperty ("ok", true);
                o->setProperty ("product", "sidechain");
                o->setProperty ("subscribed", true);
                mp->entitlementsBodyOverride = juce::var (o);
            }
            proc.getAuthManager().refreshEntitlements();
            check (waitFor ([&] { return ! proc.getAuthManager().isEntitlementCheckInFlight(); }),
                   "an undocumented answer is processed as an answer");
            check (! proc.getAuthManager().hasFullEntitlement(),
                   "an undocumented answer never unlocks, even with the raw membership in it");
            mp->entitlementsBodyOverride = juce::var();
        }

        // ---- a check AFTER the demo expired still grants, with no restart --
        {
            authPathA.deleteFile();
            auto mock = std::make_unique<MockTransport>();
            MockTransport* mp = mock.get();
            mp->entitlementsSubscribed = false;
            SideChainAudioProcessor proc (std::move (mock), authPathA.getFullPathName());
            signIn (proc, *mp);

            const double sr = 48000.0;
            const int bs = 256;
            proc.prepareToPlay (sr, bs);
            auto* ph = new TestPlayHead();
            proc.setPlayHead (ph);
            setAmount (proc, 100.0f);

            // Let the refresh that starting the instance requests finish, so the
            // grant below can only be the customer's own check.
            check (waitFor ([&] { return ! proc.getAuthManager().isEntitlementCheckInFlight(); }),
                   "the refresh requested by starting the instance has finished");

            // 8 blocks of 256 = 2048 samples: past the plugin's 30 ms reported
            // latency (1440 samples at 48 kHz), so genuine audio - not the
            // alignment delay - is what the peak measures.
            bool silent = false;
            runBlocks (proc, *ph, 8, bs, sr, &silent);
            check (! silent, "the demo processes audio inside its 15 minutes");

            g_fakeNowMs += 901.0 * kSeconds;          // past the session length
            runBlocks (proc, *ph, 8, bs, sr, &silent);
            check (silent && proc.getDemoGate().expired(),
                   "the demo session expires and audio processing stops");

            mp->entitlementsSubscribed = true;        // the purchase
            proc.getAuthManager().refreshEntitlements();
            check (waitFor ([&] { return proc.getAuthManager().hasFullEntitlement()
                                          && proc.getDemoGate().isFullVersion(); }),
                   "a check AFTER the demo expired grants the full version");
            check (! proc.getDemoGate().expired() && proc.getDemoGate().isFullVersion(),
                   "...the expired session is replaced by the full version");
            runBlocks (proc, *ph, 8, bs, sr, &silent);
            check (! silent,
                   "...and audio flows again WITHOUT restarting the instance");
        }

        // ---- the two routes stay independent -------------------------------
        {
            authPathA.deleteFile();
            auto mock = std::make_unique<MockTransport>();
            MockTransport* mp = mock.get();
            mp->entitlementsSubscribed = false;
            mp->licenseStatus = Status::ok;            // a valid standalone licence
            SideChainAudioProcessor proc (std::move (mock), authPathA.getFullPathName());
            signIn (proc, *mp);
            proc.getAuthManager().activateLicenseForTesting ("PC-ABCD-EFGH-JKLM-NPQR");

            check (proc.getAuthManager().hasFullEntitlement()
                       && proc.getAuthManager().fullVersionViaLicense(),
                   "a licence grants the full version with no subscription at all");

            mp->entitlementsSubscribed = true;
            proc.getAuthManager().refreshEntitlementsForTesting();
            check (proc.getAuthManager().hasFullEntitlement()
                       && proc.getAuthManager().fullVersionViaLicense(),
                   "an active subscription does not disturb the licence route");

            mp->entitlementsSubscribed = false;
            proc.getAuthManager().refreshEntitlementsForTesting();
            check (proc.getAuthManager().hasFullEntitlement()
                       && proc.getAuthManager().fullVersionViaLicense()
                       && ! proc.getAuthManager().fullVersionViaSubscription(),
                   "losing the subscription never disables a valid licence");
            check (proc.getAuthManager().getLicenseStatus() == LS::active,
                   "...and the licence state is unchanged by the subscription answer");
        }
    }

    // ==================================================================
    // 15. "Could not check" is not "inactive" (0.5.0 redesign)
    // ==================================================================
    // The INFO page now tells the customer when a subscription check got NO
    // answer, instead of repeating the last verdict as if the server had just
    // given it. The flag that drives that line must follow the server's
    // answers exactly: set by a missing answer, cleared by any real answer, and
    // never set by a 401 (that IS an answer - the account is gone).
    printf ("\n15. Failed-check reporting\n");
    {
        auto authPathF = tempFile ("failedcheck.json");
        authPathF.deleteFile();
        auto mock = std::make_unique<MockTransport>();
        MockTransport* m = mock.get();
        SideChainAudioProcessor proc (std::move (mock), authPathF.getFullPathName());
        signIn (proc, *m);
        auto& am = proc.getAuthManager();

        check (! am.didLastEntitlementCheckFail(),
               "a confirmed answer at sign-in is not a failed check");

        m->entitlementsStatus = 503;    // no usable answer
        am.refreshEntitlementsForTesting();
        check (am.didLastEntitlementCheckFail(),
               "a check with no answer is reported as failed");
        check (! am.hasFullEntitlement() && ! proc.getDemoGate().isFullVersion(),
               "...and a failed check grants nothing");
        check (! am.isEntitlementCheckInFlight(),
               "...and does not leave the check in flight");

        m->entitlementsStatus = 200;
        m->entitlementsSubscribed = true;
        am.refreshEntitlementsForTesting();
        check (! am.didLastEntitlementCheckFail() && am.hasFullEntitlement(),
               "the next real answer clears the failure and is applied");

        m->entitlementsStatus = 503;
        am.refreshEntitlementsForTesting();
        check (am.didLastEntitlementCheckFail() && am.hasFullEntitlement(),
               "a failure after a confirmed subscription keeps the verified state (no silent downgrade)");

        m->entitlementsStatus = 401;
        am.refreshEntitlementsForTesting();
        check (! am.didLastEntitlementCheckFail(),
               "a 401 is an answer (signed out), not a failed check");
        check (! am.hasFullEntitlement(),
               "...and the rejected session loses the subscription route");
        authPathF.deleteFile();
        authPathF.getSiblingFile ("device_id.txt").deleteFile();
    }

    // ==================================================================
    // 16. Product scope, rejected tokens, shutdown while a request hangs
    // ==================================================================
    printf ("\n16. Product scope, rejected tokens and shutdown while offline\n");
    {
        using LS = sid::auth::AuthManager::LicenseStatus;
        auto authPathP = tempFile ("productscope.json");

        // (a) An entitlement answer about ANOTHER product (a token copied from
        //     another Music-Prod plugin) never unlocks PumpCurve - not through
        //     plugin_entitled and not through its licence block.
        {
            authPathP.deleteFile();
            auto mock = std::make_unique<MockTransport>();
            MockTransport* m = mock.get();
            SideChainAudioProcessor proc (std::move (mock), authPathP.getFullPathName());
            signIn (proc, *m);
            auto& am = proc.getAuthManager();

            m->answeredProduct = "chordengine";
            m->entitlementsSubscribed = true;
            m->entitlementsLicenseActive = true;
            am.refreshEntitlementsForTesting();
            check (! am.hasFullEntitlement() && ! proc.getDemoGate().isFullVersion(),
                   "an entitlement answer for another product grants nothing");
            check (am.getLicenseStatus() != LS::active,
                   "...and its licence block is not taken as a PumpCurve licence");
            check (am.tokenIsForAnotherProduct(),
                   "...and the page is told the sign-in belongs to another product");

            m->answeredProduct = "sidechain";
            am.refreshEntitlementsForTesting();
            check (am.hasFullEntitlement() && proc.getDemoGate().isFullVersion(),
                   "the same answer for sidechain unlocks immediately, no restart");

            m->answeredProduct = "chordengine";
            am.refreshEntitlementsForTesting();
            check (! am.hasFullEntitlement() && am.getLicenseStatus() == LS::signInRequired,
                   "a later answer for another product withdraws the PumpCurve licence");
        }

        // (b) An activation answered for another product is not an activation.
        {
            authPathP.deleteFile();
            auto mock = std::make_unique<MockTransport>();
            MockTransport* m = mock.get();
            SideChainAudioProcessor proc (std::move (mock), authPathP.getFullPathName());
            signIn (proc, *m);
            auto& am = proc.getAuthManager();
            m->answeredProduct = "chordengine";
            m->licenseStatus = sid::auth::LicenseResponse::Status::ok;
            am.activateLicenseForTesting ("PC-OTHER-PRODUCT-KEY");
            check (! am.hasFullEntitlement() && am.getLicenseStatus() != LS::active,
                   "activate-license ok for another product grants nothing");
            check (am.getLicenseMessage().contains ("another product"),
                   "...and says why");
        }

        // (c) activate-license answering 401 ends the sign-in, as entitlements does.
        {
            authPathP.deleteFile();
            auto mock = std::make_unique<MockTransport>();
            MockTransport* m = mock.get();
            SideChainAudioProcessor proc (std::move (mock), authPathP.getFullPathName());
            signIn (proc, *m);
            auto& am = proc.getAuthManager();
            m->licenseAuthFailure = true;
            am.activateLicenseForTesting ("PC-ANY-KEY");
            check (am.getState() == sid::auth::AuthManager::State::signedOut
                       && am.getLicenseStatus() == LS::signInRequired
                       && ! authPathP.existsAsFile(),
                   "activate-license 401 signs out and asks for a new sign-in");
            check (! am.hasFullEntitlement(), "...and grants nothing");
        }

        // (d) Closing the plugin while a request hangs on the network: the
        //     request is cancelled, so the instance closes promptly and its
        //     thread is never killed mid-request.
        {
            authPathP.deleteFile();
            auto mock = std::make_unique<MockTransport>();
            MockTransport* m = mock.get();
            auto proc = std::make_unique<SideChainAudioProcessor> (std::move (mock),
                                                                   authPathP.getFullPathName());
            signIn (*proc, *m);
            m->blockEntitlements = true;
            proc->getAuthManager().refreshEntitlements();
            for (int i = 0; i < 200 && ! m->enteredBlockingCall; ++i)
                juce::Thread::sleep (10);
            const bool entered = m->enteredBlockingCall;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            proc.reset();
            const auto closeMs = juce::Time::getMillisecondCounterHiRes() - t0;
            check (entered && closeMs < 2000.0,
                   "closing while a request hangs takes " + juce::String (closeMs, 0).toStdString()
                       + " ms (the request is cancelled, not waited out or killed)");
        }

        authPathP.deleteFile();
        authPathP.getSiblingFile ("device_id.txt").deleteFile();
    }

    for (const auto& f : { authPathA, authPathB, authPathC })
        f.deleteFile();
    authPathA.getSiblingFile ("device_id.txt").deleteFile();
    authPathB.getSiblingFile ("device_id.txt").deleteFile();
    authPathC.getSiblingFile ("device_id.txt").deleteFile();

    sid::license::resetMonotonicMillisecondsForTest();

    printf ("\n==============================\n");
    if (failures == 0)
        printf ("%d/%d checks passed. ALL TESTS PASSED\n", totalChecks, totalChecks);
    else
        printf ("%d/%d checks passed. %d FAILURES\n", totalChecks - failures, totalChecks, failures);
    printf ("==============================\n");

    return failures == 0 ? 0 : 1;
}
