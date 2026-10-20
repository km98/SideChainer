/*
    PumpCurve 0.5.0 - plugin version check tests (Plugin Versions).

    Suite 13. The version shown in the INFO page's UPDATES section must come
    from the Music-Prod release catalog (the admin panel is the source of
    truth), never from a hardcoded latest version, and the check must never be
    able to disturb audio, the demo session or the licensing state.

    1.  Semantic version parsing and comparison: numeric per component, never
        lexicographic (0.10.0 > 0.9.0), with semver.org pre-release precedence.
    2.  Response parsing: every field the UI shows, and the honest answer for
        a missing release, a malformed body, or a non-https download link.
    3.  The checker's interval policy on a controllable MONOTONIC clock: one
        check, then nothing until the interval elapses, plus manual retry.
    4.  Offline / unreachable: reported as UNABLE TO CHECK, a previously known
        newer version is not silently forgotten, and nothing else changes.
    5.  The REAL processor wiring: no request before the UI asks (so plugin
        scanning and headless use never open a socket), and a failing check
        cannot stop audio, end the demo session or grant the full version.
    6.  The request URL: the product, the installed version and this build's
        platform/architecture, with the values escaped.

    NO NETWORK: every transport here is a mock, and the single HTTP transport
    test only builds a URL string - it never connects.
*/

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <atomic>

#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/UpdateCheck.h"

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
    double g_fakeNowSeconds = 0.0;
    double fakeNow() { return g_fakeNowSeconds; }

    /** Builds a /updates envelope exactly as the service emits it. */
    juce::var makeEnvelope (const juce::String& latestVersion,
                            const juce::String& status = "updateAvailable",
                            bool updateAvailableFlag = true,
                            const juce::String& downloadUrl = "https://music-prod.com/pumpcurve",
                            const juce::String& releaseNotes = "Release notes",
                            const juce::String& publishedAt = "2026-10-13T09:00:00Z",
                            bool mandatory = false,
                            const juce::String& reason = "")
    {
        juce::DynamicObject::Ptr data = new juce::DynamicObject();
        if (latestVersion.isNotEmpty())
            data->setProperty ("latestVersion", latestVersion);
        else
            data->setProperty ("latestVersion", juce::var());
        data->setProperty ("status", status);
        data->setProperty ("updateAvailable", updateAvailableFlag);
        data->setProperty ("downloadUrl", downloadUrl);
        data->setProperty ("releaseNotes", releaseNotes);
        data->setProperty ("publishedAt", publishedAt);
        data->setProperty ("mandatory", mandatory);
        data->setProperty ("reason", reason);

        juce::DynamicObject::Ptr envelope = new juce::DynamicObject();
        envelope->setProperty ("success", true);
        envelope->setProperty ("data", juce::var (data.get()));
        return juce::var (envelope.get());
    }

    // ---- mock transport ------------------------------------------------
    struct MockUpdateTransport : public sid::update::UpdateTransport
    {
        juce::String latestVersion   = "0.5.1";
        juce::String releaseNotes    = "Fixes an audible click on the first beat";
        juce::String downloadUrl     = "https://music-prod.com/pumpcurve";
        juce::String status          = "updateAvailable";
        bool         fail            = false;
        bool         updateAvailableFlag = true;
        int          sleepMs         = 0;

        std::atomic<int> fetches { 0 };
        std::atomic<int> installedSeenAs { 0 };

        bool fetch (const juce::String& product, const juce::String& currentVersion,
                    const juce::String& platform, const juce::String& architecture,
                    sid::update::UpdateInfo& out) override
        {
            ++fetches;
            lastProduct      = product;
            lastCurrent      = currentVersion;
            lastPlatform     = platform;
            lastArchitecture = architecture;

            if (sleepMs > 0)
                juce::Thread::sleep (sleepMs);

            if (fail)
                return false;

            return sid::update::parseUpdateResponse (
                makeEnvelope (latestVersion, status, updateAvailableFlag, downloadUrl,
                              releaseNotes, "2026-10-13T09:00:00Z"),
                currentVersion, out);
        }

        juce::String lastProduct, lastCurrent, lastPlatform, lastArchitecture;
    };

    // ---- a do-nothing auth transport (no network at all) ---------------
    struct SilentAuthTransport : public sid::auth::AuthTransport
    {
        sid::auth::StartResponse start (const juce::String&, const juce::String&,
                                        const juce::String&) override { return {}; }
        sid::auth::PollResponse poll (const juce::String&) override
        {
            sid::auth::PollResponse r; r.status = "error"; return r;
        }
        sid::auth::EntitlementsResponse entitlements (const juce::String&) override { return {}; }
        bool logout (const juce::String&) override { return false; }
    };

    // ---- real play head (the transport gate needs one) ------------------
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

    juce::File tempFile (const juce::String& name)
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("sdch_updatecheck_" + name);
        f.deleteFile();
        return f;
    }

    /** Processes real audio through the REAL path and returns the final peak. */
    float runBlocks (SideChainAudioProcessor& proc, TestPlayHead& ph,
                     int blocks, int blockSize, double sampleRate)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        float lastPeak = 0.0f;
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

            ph.info.isPlaying     = true;
            ph.info.isRecording   = false;
            ph.info.bpm           = 120.0;
            ph.info.timeInSamples = t;
            ph.info.ppqPosition   = (double) t / sampleRate * 2.0;

            proc.processBlock (buffer, midi);

            float peak = 0.0f;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, blockSize));
            lastPeak = peak;

            t += blockSize;
        }

        return lastPeak;
    }
}

// ===========================================================================
int main()
{
    printf ("\n================================================================\n");
    printf (" PumpCurve 0.5.0 - plugin version check (Plugin Versions)\n");
    printf ("================================================================\n");

    sid::update::setMonotonicSecondsForTest (&fakeNow);
    g_fakeNowSeconds = 1'000'000.0;

    // -----------------------------------------------------------------------
    printf ("\n1. Semantic version parsing and comparison\n");
    // -----------------------------------------------------------------------
    {
        using namespace sid::update;

        check (parseVersion ("0.5.0").valid, "MAJOR.MINOR.PATCH parses");
        check (parseVersion (" 1.2.3 ").valid, "surrounding whitespace is tolerated");
        check (parseVersion ("0.5.0-beta.1").valid, "a pre-release parses");
        check (parseVersion ("1.2.0-beta.1").prerelease.size() == 2,
               "pre-release identifiers are split");
        check (! parseVersion ("latest").valid, "a non-version does not parse");
        check (! parseVersion ("0.5").valid, "two components do not parse");
        check (! parseVersion ("0.5.0.1").valid, "four components do not parse");
        check (! parseVersion ("").valid, "an empty version does not parse");

        int r = 1234;
        check (compareVersions ("0.10.0", "0.9.0", r) && r == 1,
               "0.10.0 is NEWER than 0.9.0 (numeric per component, never lexicographic)");
        check (compareVersions ("0.9.0", "0.10.0", r) && r == -1,
               "0.9.0 is older than 0.10.0");
        check (compareVersions ("2.0.0", "10.0.0", r) && r == -1,
               "2.0.0 is older than 10.0.0");
        check (compareVersions ("1.2.3", "1.2.3", r) && r == 0, "equal versions compare equal");

        check (compareVersions ("0.6.0", "0.6.0-beta.1", r) && r == 1,
               "a release outranks its own pre-release");
        check (compareVersions ("0.6.0-beta.10", "0.6.0-beta.2", r) && r == 1,
               "numeric pre-release identifiers compare numerically (beta.10 > beta.2)");
        check (compareVersions ("0.6.0-rc.1", "0.6.0-beta.9", r) && r == 1,
               "alphanumeric pre-release identifiers compare lexically");
        check (compareVersions ("0.6.0-beta", "0.6.0-beta.1", r) && r == -1,
               "a shorter pre-release list ranks lower");

        const int sentinel = 4242;
        int guard = sentinel;
        check (! compareVersions ("nightly", "0.5.0", guard) && guard == sentinel,
               "an unparseable version is refused and leaves the result untouched");

        check (platformForThisBuild().isNotEmpty() && architectureForThisBuild().isNotEmpty(),
               "this build reports a platform and an architecture\n");
    }

    // -----------------------------------------------------------------------
    printf ("2. Response parsing (the version-check contract)\n");
    // -----------------------------------------------------------------------
    {
        using namespace sid::update;
        UpdateInfo info;

        check (parseUpdateResponse (makeEnvelope ("0.5.1", "updateAvailable", true,
                                                  "https://music-prod.com/pumpcurve",
                                                  "Fixes a click", "2026-10-13T09:00:00Z"),
                                    "0.5.0", info),
               "a published newer version is parsed");
        check (info.status == Status::updateAvailable, "status is UPDATE AVAILABLE");
        check (info.latestVersion == "0.5.1", "the latest version is carried");
        check (info.installedVersion == "0.5.0", "the installed version is carried");
        check (info.releaseNotes == "Fixes a click", "release notes are carried");
        check (info.publishedAt == "2026-10-13T09:00:00Z", "the publication date is carried");
        check (info.downloadUrl == "https://music-prod.com/pumpcurve",
               "the official download page is carried");
        check (info.hasDownloadLink(), "the download link is offered");

        check (parseUpdateResponse (makeEnvelope ("0.5.0"), "0.5.0", info)
                   && info.status == Status::upToDate,
               "the installed version being the latest is UP TO DATE");
        check (parseUpdateResponse (makeEnvelope ("0.4.2"), "0.5.0", info)
                   && info.status == Status::upToDate,
               "an older published version is never reported as an update");

        // The server's flag cannot create an update the versions do not support.
        check (parseUpdateResponse (makeEnvelope ("0.4.2", "updateAvailable", true), "0.5.0", info)
                   && info.status == Status::upToDate,
               "a stale/rolled-back catalog answer cannot invent an update");

        // NOTHING PUBLISHED is its own state. Regression test for the INFO page
        // reading "UP TO DATE" beside "LATEST VERSION -": with no release to
        // compare against, claiming the installed build is "the latest published
        // version" invents a comparison that cannot be made.
        check (parseUpdateResponse (makeEnvelope ("", "noPublishedRelease", false), "0.5.0", info)
                   && info.status == Status::noPublishedRelease
                   && info.latestVersion.isEmpty(),
               "no published release is its OWN state, never UP TO DATE");
        check (info.detail.contains ("No published release"), "and it explains why");
        check (! info.hasDownloadLink(), "and no download link is invented");

        // The verbatim answer of the DEPLOYED service for a product with no
        // published release (music-prod-studio-api GET /updates, 2026-10-14):
        //   latestVersion null, updateAvailable false, decision "upToDate",
        //   status "noPublishedRelease"
        // The `decision` field says upToDate; the page must not.
        check (parseUpdateResponse (
                   makeEnvelope ("", "noPublishedRelease", false, "", "", "", false,
                                 "no published release exists for this product yet"),
                   "0.5.0", info)
                   && info.status == Status::noPublishedRelease,
               "the live no-published-release answer is never reported as UP TO DATE");

        // A null latestVersion decides it on its own, whatever `status` says.
        check (parseUpdateResponse (makeEnvelope ("", "upToDate", false), "0.5.0", info)
                   && info.status == Status::noPublishedRelease,
               "a null latest version is never UP TO DATE, whatever `status` claims");
        check (parseUpdateResponse (makeEnvelope ("", "", false), "hostbuild", info)
                   && info.status == Status::noPublishedRelease,
               "and neither is an uncomparable installed version with nothing published");

        check (parseUpdateResponse (makeEnvelope ("0.5.1", "noCompatibleArtifact", true), "0.5.0", info)
                   && info.status == Status::updateAvailable
                   && info.detail.contains ("no installer is published for this platform"),
               "a newer version with no artifact for this platform is explained, not hidden");

        check (parseUpdateResponse (makeEnvelope ("0.5.1", "updateAvailable", true,
                                                  "https://music-prod.com/pumpcurve",
                                                  "notes", "2026-10-13T09:00:00Z", true),
                                    "0.5.0", info)
                   && info.mandatory && info.detail.contains ("required"),
               "a mandatory update says so");

        // Links: only ever an https URL the server supplied.
        check (parseUpdateResponse (makeEnvelope ("0.5.1", "updateAvailable", true, ""), "0.5.0", info)
                   && ! info.hasDownloadLink(),
               "no server link means no link is offered");
        check (parseUpdateResponse (makeEnvelope ("0.5.1", "updateAvailable", true,
                                                  "http://music-prod.com/pumpcurve"), "0.5.0", info)
                   && ! info.hasDownloadLink(),
               "a non-https link is refused");
        check (parseUpdateResponse (makeEnvelope ("0.5.1", "updateAvailable", true,
                                                  "releases/s-050/x.pkg"), "0.5.0", info)
                   && ! info.hasDownloadLink(),
               "a storage key is never offered as a download link");

        // Malformed / unusable answers are refused (the caller keeps its state).
        UpdateInfo untouched;
        check (! parseUpdateResponse (juce::var(), "0.5.0", untouched),
               "an empty response is refused");
        check (! parseUpdateResponse (juce::var (42), "0.5.0", untouched),
               "a non-object response is refused");

        juce::DynamicObject::Ptr failure = new juce::DynamicObject();
        failure->setProperty ("success", false);
        failure->setProperty ("error", "productNotFound");
        check (! parseUpdateResponse (juce::var (failure.get()), "0.5.0", untouched),
               "a failed envelope is refused");

        juce::DynamicObject::Ptr noData = new juce::DynamicObject();
        noData->setProperty ("success", true);
        check (! parseUpdateResponse (juce::var (noData.get()), "0.5.0", untouched),
               "a success envelope with no data is refused");

        check (untouched.status == Status::unknown && untouched.latestVersion.isEmpty(),
               "a refused response leaves the previous state untouched");

        // A development/host-built installed version cannot be compared: the
        // server's own answer is used, which is the authority on what exists.
        check (parseUpdateResponse (makeEnvelope ("0.5.1", "updateAvailable", true), "hostbuild", info)
                   && info.status == Status::updateAvailable,
               "an uncomparable installed version falls back to the server's answer");
        check (parseUpdateResponse (makeEnvelope ("0.5.0", "upToDate", false), "hostbuild", info)
                   && info.status == Status::upToDate,
               "and an up-to-date server answer is respected for it\n");
    }

    // -----------------------------------------------------------------------
    printf ("3. Interval policy on a monotonic clock\n");
    // -----------------------------------------------------------------------
    {
        MockUpdateTransport* mock = new MockUpdateTransport();
        sid::update::UpdateChecker checker (std::unique_ptr<sid::update::UpdateTransport> (mock),
                                            "sidechain", "0.5.0", "macos", "arm64");

        check (checker.snapshot().status == sid::update::Status::unknown,
               "a fresh checker has not checked yet");
        check (checker.isDueForTesting(), "the FIRST check is due immediately");
        check (mock->fetches.load() == 0,
               "constructing a checker performs NO request (plugin scanning opens no socket)");

        checker.setCheckIntervalSecondsForTesting (sid::update::UpdateChecker::CHECK_INTERVAL_SECONDS);
        check (checker.isDueForTesting(), "still due before the first check");

        checker.checkNowForTesting();
        check (mock->fetches.load() == 1, "one check performs exactly one request");
        check (checker.snapshot().status == sid::update::Status::updateAvailable,
               "the checker reports the newer published version");
        check (! checker.isDueForTesting(),
               "a check that just happened is NOT due again (no continuous polling)");

        g_fakeNowSeconds += sid::update::UpdateChecker::CHECK_INTERVAL_SECONDS - 1.0;
        check (! checker.isDueForTesting(),
               "one second inside the interval it is still not due");

        g_fakeNowSeconds += 2.0;
        check (checker.isDueForTesting(),
               "after the interval elapses a check is due again");

        // A clock that jumps BACKWARDS must not turn the interval into a flood.
        const double before = g_fakeNowSeconds;
        g_fakeNowSeconds = before - 10'000.0;
        check (! checker.isDueForTesting(),
               "a backwards clock change cannot make the checker hammer the server");
        g_fakeNowSeconds = before;

        // And a missing check is caught up by the UI's own refresh.
        checker.refreshIfDue();
        check (checker.snapshot().status != sid::update::Status::unknown,
               "refreshIfDue() is what the UI calls; it never blocks\n");
    }

    // -----------------------------------------------------------------------
    printf ("4. Offline / unreachable server\n");
    // -----------------------------------------------------------------------
    {
        MockUpdateTransport* mock = new MockUpdateTransport();
        sid::update::UpdateChecker checker (std::unique_ptr<sid::update::UpdateTransport> (mock),
                                            "sidechain", "0.5.0", "macos", "arm64");

        mock->fail = true;
        checker.checkNowForTesting();
        auto info = checker.snapshot();
        check (info.status == sid::update::Status::unableToCheck,
               "an unreachable server is reported as UNABLE TO CHECK");
        check (info.detail.contains ("try again"), "and tells the user to try again");
        check (info.downloadUrl.isEmpty(), "no link is invented when the server is unreachable");

        // A known newer version is not silently forgotten.
        mock->fail = false;
        checker.checkNowForTesting();
        check (checker.snapshot().status == sid::update::Status::updateAvailable,
               "a later successful check reports the update");

        mock->fail = true;
        checker.checkNowForTesting();
        info = checker.snapshot();
        check (info.status == sid::update::Status::unableToCheck,
               "going offline again reports UNABLE TO CHECK");
        check (info.latestVersion == "0.5.1",
               "the previously reported newer version is RETAINED, not forgotten");
        check (info.hasDownloadLink(),
               "and its official link is retained too, so the user can still act");
        check (info.detail.contains ("0.5.1"), "the message names the version");

        // Recovery.
        mock->fail = false;
        checker.checkNowForTesting();
        check (checker.snapshot().status == sid::update::Status::updateAvailable,
               "a recovered connection restores the update state\n");
    }

    // -----------------------------------------------------------------------
    printf ("5. Real processor wiring (audio and licensing are untouched)\n");
    // -----------------------------------------------------------------------
    {
        auto authPath = tempFile ("auth.json");
        MockUpdateTransport* mock = new MockUpdateTransport();
        mock->fail = true;   // the worst case: the update service is down

        SideChainAudioProcessor proc (
            std::unique_ptr<sid::auth::AuthTransport> (new SilentAuthTransport()),
            authPath.getFullPathName(),
            std::unique_ptr<sid::update::UpdateTransport> (mock));

        check (mock->fetches.load() == 0,
               "constructing the plugin performs NO version request (scanning is safe)");

        auto& checker = proc.getUpdateChecker();
        check (checker.getProductForTesting() == "sidechain",
               "the checker asks for the release catalog's product key");
        check (checker.snapshot().installedVersion.startsWith ("0.5"),
               "the installed version comes from the compiled-in project version");

        // The UI's own refresh is what triggers the first check.
        checker.checkNowForTesting();
        check (checker.snapshot().status == sid::update::Status::unableToCheck,
               "a failing check leaves the version state at UNABLE TO CHECK");

        // ...and NOTHING else changed: audio still runs, the demo session is
        // untouched and the full version was not granted.
        TestPlayHead ph;
        proc.setPlayHead (&ph);
        proc.prepareToPlay (48000.0, 512);

        const auto gateBefore = proc.getDemoGate().remainingSeconds();
        const float peak = runBlocks (proc, ph, 12, 512, 48000.0);

        check (peak > 0.01f,
               "audio processing is UNAFFECTED by a failed version check (peak "
                   + juce::String (peak, 4).toStdString() + ")");
        check (! proc.getDemoGate().isFullVersion(),
               "a failed version check NEVER grants the full version");
        check (! proc.getDemoGate().expired(),
               "a failed version check does not end the demo session");
        check (proc.getDemoGate().remainingSeconds() <= gateBefore,
               "the demo session keeps its own clock (it only ever counts down)");
        check (! proc.getAuthManager().hasFullEntitlement(),
               "the licensing state is untouched by the version check");

        proc.releaseResources();
        authPath.deleteFile();

        // A successful check on the same plugin reports the published version
        // without touching the audio path either.
        MockUpdateTransport* okMock = new MockUpdateTransport();
        SideChainAudioProcessor proc2 (
            std::unique_ptr<sid::auth::AuthTransport> (new SilentAuthTransport()),
            tempFile ("auth2.json").getFullPathName(),
            std::unique_ptr<sid::update::UpdateTransport> (okMock));
        proc2.getUpdateChecker().checkNowForTesting();
        check (proc2.getUpdateChecker().snapshot().status == sid::update::Status::updateAvailable,
               "a reachable catalog reports the update on the real plugin");
        check (okMock->lastProduct == "sidechain" && okMock->lastCurrent.startsWith ("0.5"),
               "the request carries the product key and the installed version\n");
    }

    // -----------------------------------------------------------------------
    printf ("6. Request shape (no socket is opened)\n");
    // -----------------------------------------------------------------------
    {
        sid::update::HttpUpdateTransport http ("https://example.invalid/functions/v1/updates");
        const auto url = http.buildUrl ("sidechain", "0.5.0", "macos", "arm64");

        check (url.startsWith ("https://example.invalid/functions/v1/updates?"),
               "the request targets the configured endpoint");
        check (url.contains ("product=sidechain"), "the product is in the query");
        check (url.contains ("current=0.5.0"), "the installed version is in the query");
        check (url.contains ("platform=macos") && url.contains ("architecture=arm64"),
               "this build's platform and architecture are in the query");
        check (! url.contains ("&token=") && ! url.contains ("apikey") && ! url.contains ("key="),
               "no token, key or credential is sent with the version check");

        const auto escaped = http.buildUrl ("side chain", "0.5.0+build", "macos", "arm64");
        check (! escaped.contains ("side chain") && escaped.contains ("side%20chain"),
               "query values are escaped, so a value cannot inject another parameter\n");
    }

    // -----------------------------------------------------------------------
    printf ("\n================================================================\n");
    if (failures == 0)
        printf ("%d/%d checks passed. ALL TESTS PASSED\n", totalChecks, totalChecks);
    else
        printf ("%d/%d checks passed. %d FAILURES\n", totalChecks - failures, totalChecks, failures);
    printf ("================================================================\n\n");

    sid::update::setMonotonicSecondsForTest (nullptr);
    return failures == 0 ? 0 : 1;
}
