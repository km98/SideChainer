/*
    PumpCurve - plugin update check (Plugin Versions).

    WHAT THIS IS
    The INFO page shows the INSTALLED version (compiled in) and the LATEST
    PUBLISHED version, and tells the user whether an update exists. That answer
    comes from the Music-Prod release catalog - the admin panel is the source of
    truth and nothing about the latest version is hardcoded in the plugin:

        GET {updates-endpoint}?product=sidechain&current=<installed>
                               &platform=macos|windows&architecture=arm64|x86_64

    The endpoint is the existing music-prod-studio-api function's PUBLIC
    /updates route (the same one Music-Prod Studio's self-update uses), so a
    draft or withdrawn release can never be reported as available: only
    `published` releases are served. No secret, key or token is sent, and none
    is stored.

    RESPONSE FIELDS USED
      latestVersion, updateAvailable, decision, status, reason, releaseNotes,
      publishedAt, mandatory, minimumSupportedVersion, downloadUrl,
      supportedPlatforms, artifacts[]

    STATE
      unknown           - nothing checked yet
      checking          - a request is in flight
      upToDate          - the installed version is the latest published one
      noPublishedRelease- the catalog has no published release for this product
                          at all, so there is nothing to compare against. The
                          page says exactly that and never "UP TO DATE" (which
                          would claim a comparison that cannot be made).
      updateAvailable   - a newer published version exists
      unableToCheck     - offline, unreachable, or an unusable answer

    THREADING
    All network I/O happens on this class's own background juce::Thread. The
    audio thread NEVER touches this class - processBlock reads nothing from it,
    and nothing here can delay, block or silence audio. A failed check cannot
    disable the plugin, change its licensing state, or stop demo processing; it
    only changes what the INFO page reports. The UI reads a cheap snapshot (a
    copy taken under a mutex) from its existing timer.

    POLICY
      - no request at construction, and none at all until the UI asks
        (refreshIfDue()); plugin scanning and headless use therefore never open
        a socket;
      - at most one check per CHECK_INTERVAL_SECONDS (6 hours), plus an explicit
        manual retry the user can always trigger;
      - the interval is measured with a MONOTONIC clock, so a system clock change
        cannot turn the interval into a flood or a permanent freeze;
      - a check is idempotent while one is already in flight.

    SECURITY / HONESTY
      - the installed version always comes from JucePlugin_VersionString (the
        .jucer project version), never from a label in the UI;
      - the download link is ONLY ever the https URL the SERVER returned (the
        product's reviewed public page). If the catalog has none, the plugin
        offers no link rather than sending the user somewhere invented;
      - the server's decision is cross-checked with an equivalent local semantic
        comparison that can only ever DOWNGRADE the claim (never invent an
        update), never add one.
*/

#pragma once

#include <JuceHeader.h>
#include <functional>
#include <mutex>

namespace sid::update
{

//==============================================================================
// State as the INFO page displays it (all text; never colour-only).
enum class Status
{
    unknown,
    checking,
    upToDate,
    noPublishedRelease,
    updateAvailable,
    unableToCheck
};

struct UpdateInfo
{
    Status       status = Status::unknown;
    juce::String installedVersion;
    juce::String latestVersion;
    juce::String releaseNotes;
    juce::String publishedAt;
    juce::String downloadUrl;      // server-provided official page (may be empty)
    juce::String detail;           // explanatory text for the current status
    bool         mandatory = false;

    // Convenience for the UI: is there a link to offer?
    bool hasDownloadLink() const noexcept { return downloadUrl.startsWith ("https://"); }
};

//==============================================================================
// The monotonic clock seam. LicenseGate.h has its own seam for the demo session;
// this one is independent so an update-interval test never disturbs the demo
// countdown (and vice versa). Monotonic by contract: never wall-clock time.
double monotonicSeconds();

// Tests replace the clock so the interval policy is exercised deterministically.
void setMonotonicSecondsForTest (std::function<double()> fn);

//==============================================================================
// Semantic version handling. The server compares versions authoritatively; this
// equivalent local comparison exists so the plugin can never claim an update
// whose "latest" version is not actually newer than what is installed.
// Comparison is NUMERIC per component (0.10.0 > 0.9.0), never lexicographic, and
// pre-release identifiers follow semver.org section 11.
struct ParsedVersion
{
    int            major = 0;
    int            minor = 0;
    int            patch = 0;
    juce::StringArray prerelease;
    bool           valid = false;
};

inline ParsedVersion parseVersion (const juce::String& raw)
{
    ParsedVersion out;
    auto text = raw.trim();
    if (text.isEmpty())
        return out;

    juce::String prerelease;
    const int dash = text.indexOfChar ('-');
    if (dash >= 0)
    {
        prerelease = text.substring (dash + 1);
        text       = text.substring (0, dash);
    }

    juce::StringArray tokens;
    tokens.addTokens (text, ".", "");
    if (tokens.size() != 3)
        return out;

    for (const auto& token : tokens)
        if (token.isEmpty() || ! token.containsOnly ("0123456789"))
            return out;

    out.major = tokens[0].getIntValue();
    out.minor = tokens[1].getIntValue();
    out.patch = tokens[2].getIntValue();
    if (prerelease.isNotEmpty())
        out.prerelease.addTokens (prerelease, ".", "");
    out.valid = true;
    return out;
}

/** -1 / 0 / 1 for two pre-release identifier lists (semver.org section 11). */
inline int comparePrerelease (const juce::StringArray& a, const juce::StringArray& b)
{
    if (a.isEmpty() && b.isEmpty())  return 0;
    if (a.isEmpty())                 return 1;   // 1.0.0 > 1.0.0-beta
    if (b.isEmpty())                 return -1;

    const int shared = juce::jmin (a.size(), b.size());
    for (int i = 0; i < shared; ++i)
    {
        const auto x = a[i];
        const auto y = b[i];
        if (x == y)
            continue;

        const bool xNumeric = x.containsOnly ("0123456789");
        const bool yNumeric = y.containsOnly ("0123456789");
        if (xNumeric && yNumeric)
            return x.getIntValue() < y.getIntValue() ? -1 : 1;
        if (xNumeric) return -1;   // numeric identifiers rank below alphanumeric
        if (yNumeric) return 1;
        return x.compare (y) < 0 ? -1 : 1;
    }

    if (a.size() == b.size()) return 0;
    return a.size() < b.size() ? -1 : 1;
}

/**
 * Numeric semantic comparison. Returns false when either version cannot be
 * parsed, in which case `result` is untouched and the caller must fall back to
 * the server's own answer.
 */
inline bool compareVersions (const juce::String& one, const juce::String& two, int& result)
{
    const auto a = parseVersion (one);
    const auto b = parseVersion (two);
    if (! a.valid || ! b.valid)
        return false;

    if (a.major != b.major) { result = a.major < b.major ? -1 : 1; return true; }
    if (a.minor != b.minor) { result = a.minor < b.minor ? -1 : 1; return true; }
    if (a.patch != b.patch) { result = a.patch < b.patch ? -1 : 1; return true; }
    result = comparePrerelease (a.prerelease, b.prerelease);
    return true;
}

//==============================================================================
// Platform identity asked of the server: what THIS running build is, so the
// answer names an installer the user can actually run. The server matches an
// artifact whose architecture equals the request or is `universal`, which is why
// asking for the real architecture still finds a universal macOS build.
inline juce::String platformForThisBuild()
{
#if JUCE_MAC
    return "macos";
#elif JUCE_WINDOWS
    return "windows";
#else
    return "linux";
#endif
}

inline juce::String architectureForThisBuild()
{
#if defined (__aarch64__) || defined (_M_ARM64)
    return "arm64";
#elif defined (__x86_64__) || defined (_M_X64)
    return "x86_64";
#else
    return "universal";
#endif
}

//==============================================================================
/**
 * Parse one /updates response envelope into UpdateInfo. Pure: no I/O, no state,
 * fully unit-testable. Returns false when the answer is not usable at all (in
 * which case the caller reports "Unable to check" and keeps whatever it had).
 */
inline bool parseUpdateResponse (const juce::var& envelope,
                                 const juce::String& installedVersion,
                                 UpdateInfo& out)
{
    const auto asString = [] (const juce::var& v) { return v.isVoid() ? juce::String() : v.toString(); };

    out = {};
    out.installedVersion = installedVersion;

    if (! envelope.isObject())
        return false;
    if (! (bool) envelope.getProperty ("success", false))
        return false;

    const auto data = envelope.getProperty ("data", juce::var());
    if (! data.isObject())
        return false;

    out.releaseNotes = asString (data.getProperty ("releaseNotes", juce::var()));
    out.publishedAt  = asString (data.getProperty ("publishedAt", juce::var()));
    out.mandatory    = (bool) data.getProperty ("mandatory", false);

    // Only ever the https page the SERVER named. Anything else (empty, a
    // storage key, an http URL) simply yields no link.
    const auto link = asString (data.getProperty ("downloadUrl", juce::var()));
    if (link.startsWith ("https://"))
        out.downloadUrl = link;

    const auto latest  = asString (data.getProperty ("latestVersion", juce::var()));
    const auto status  = asString (data.getProperty ("status", juce::var()));
    const auto reason  = asString (data.getProperty ("reason", juce::var()));
    const bool serverSaysUpdate = (bool) data.getProperty ("updateAvailable", false);

    // NOTHING PUBLISHED is its own state and never "up to date". The service
    // reports it with a null latestVersion (live shape, 2026-10-14):
    //   latestVersion: null, decision: "upToDate", status: "noPublishedRelease"
    // Reporting that as Status::upToDate is what made the INFO page read
    // "UP TO DATE" next to "LATEST VERSION -": it claimed the user was running
    // the latest published version when the catalog had nothing published at
    // all. A null latestVersion is enough on its own, whatever `status` says,
    // because no version can be compared against.
    if (latest.isEmpty() || status == "noPublishedRelease")
    {
        // No version AND no link: with nothing published there is nothing to
        // download, so any URL sent alongside this answer is not a release link
        // and is not kept (the button only offers a download for
        // Status::updateAvailable, and the state must not disagree with itself).
        out.latestVersion.clear();
        out.downloadUrl.clear();
        out.status = Status::noPublishedRelease;
        out.detail = "No published release is available yet.";
        return true;
    }

    out.latestVersion = latest;

    int order = 0;
    if (compareVersions (installedVersion, latest, order))
    {
        // A valid comparison is the gate: the server's claim is never allowed to
        // create an update the versions do not support.
        if (order >= 0)
        {
            out.status = Status::upToDate;
            out.detail = "You are running the latest published version.";
            return true;
        }

        if (status == "noCompatibleArtifact")
        {
            out.status = Status::updateAvailable;
            out.detail = "A newer version exists, but no installer is published for this platform yet.";
            return true;
        }

        out.status = Status::updateAvailable;
        out.detail = out.mandatory
                       ? "This update is required."
                       : "A newer version is available to download.";
        return true;
    }

    // The installed version is not a plain version we can compare (a host-built
    // or development build). Fall back to the server's own answer, which is the
    // authority on whether a newer published release exists.
    out.status = serverSaysUpdate ? Status::updateAvailable : Status::upToDate;
    out.detail = reason.isNotEmpty() ? reason
                                     : (serverSaysUpdate ? "A newer version is available to download."
                                                         : "You are running the latest published version.");
    return true;
}

//==============================================================================
// Abstract HTTP boundary so the checker is deterministically testable: tests
// inject a transport and drive the state machine without any network.
class UpdateTransport
{
public:
    virtual ~UpdateTransport() = default;

    /**
     * Fetch the latest published release for `product`. Returns false when the
     * server could not be reached or answered with something unusable; `out` is
     * only meaningful when it returns true.
     */
    virtual bool fetch (const juce::String& product,
                        const juce::String& currentVersion,
                        const juce::String& platform,
                        const juce::String& architecture,
                        UpdateInfo& out) = 0;

    /** Aborts a request blocked on the network and refuses new ones (called
        from the checker's destructor, so closing offline never waits it out). */
    virtual void cancelPendingRequests() {}
};

//==============================================================================
// Production transport: a plain GET against the public release catalog. The
// endpoint origin is compiled in (the same public functions origin the existing
// plugin flow uses; it contains no secret).
class HttpUpdateTransport : public UpdateTransport
{
public:
    explicit HttpUpdateTransport (juce::String endpointUrl);

    bool fetch (const juce::String& product,
                const juce::String& currentVersion,
                const juce::String& platform,
                const juce::String& architecture,
                UpdateInfo& out) override;
    void cancelPendingRequests() override;

    // Exposed for tests: the exact request URL, so the query is pinned.
    juce::String buildUrl (const juce::String& product,
                           const juce::String& currentVersion,
                           const juce::String& platform,
                           const juce::String& architecture) const;

private:
    juce::String endpoint;
    std::mutex streamMutex;
    juce::WebInputStream* activeStream = nullptr;
    bool requestsCancelled = false;
};

//==============================================================================
/**
 * Owns the background check. Owned by the PROCESSOR (like the auth manager) so
 * the interval survives opening and closing the editor, but no request is ever
 * issued until the UI asks for one.
 */
class UpdateChecker : private juce::Thread
{
public:
    static constexpr double CHECK_INTERVAL_SECONDS = 6.0 * 60.0 * 60.0;   // 6 hours

    UpdateChecker (std::unique_ptr<UpdateTransport> transport,
                   juce::String product,
                   juce::String installedVersion,
                   juce::String platform,
                   juce::String architecture);
    ~UpdateChecker() override;

    // ---- UI thread ------------------------------------------------------

    /** Ask for a check only when the interval has elapsed (no-op otherwise). */
    void refreshIfDue();

    /** Explicit user retry: always checks, and is never blocked by the interval. */
    void checkNow();

    /** Snapshot for display; safe from any thread (a copy under the lock). */
    UpdateInfo snapshot() const;

    // ---- test seams -----------------------------------------------------

    /** Runs one check synchronously on the calling thread. */
    void checkNowForTesting();

    /** True when a check would be issued right now (interval elapsed or first run). */
    bool isDueForTesting() const;

    void setCheckIntervalSecondsForTesting (double seconds);

    juce::String getProductForTesting() const { return product; }

private:
    void run() override;
    void performCheck();
    bool isDueLocked() const;
    void requestCheck();

    std::unique_ptr<UpdateTransport> transport;
    const juce::String product;
    const juce::String installedVersion;
    const juce::String platform;
    const juce::String architecture;

    mutable std::mutex mutex;
    UpdateInfo info;
    bool   checkRequested   = false;
    bool   inFlight         = false;
    double lastCheckAt      = 0.0;      // monotonic seconds; 0 = never checked
    double checkInterval    = CHECK_INTERVAL_SECONDS;
    bool   everChecked      = false;

    JUCE_DECLARE_NON_COPYABLE (UpdateChecker)
};

//==============================================================================
// Inline implementation
//==============================================================================

namespace detail
{
    inline std::function<double()>& clockOverride()
    {
        static std::function<double()> fn;
        return fn;
    }
}

inline double monotonicSeconds()
{
    if (auto& fn = detail::clockOverride())
        return fn();

    // Steady, sleep-aware enough for an interval and immune to wall-clock edits.
    return (double) juce::Time::getMillisecondCounterHiRes() / 1000.0;
}

inline void setMonotonicSecondsForTest (std::function<double()> fn)
{
    detail::clockOverride() = std::move (fn);
}

inline HttpUpdateTransport::HttpUpdateTransport (juce::String endpointUrl)
    : endpoint (std::move (endpointUrl)) {}

inline juce::String HttpUpdateTransport::buildUrl (const juce::String& productIn,
                                                   const juce::String& currentVersionIn,
                                                   const juce::String& platformIn,
                                                   const juce::String& architectureIn) const
{
    // The platform and architecture are appended, not passed as "universal":
    // they are simple identifiers, and percent-encoding them here keeps the
    // request valid even if a caller passes something unexpected.
    juce::String url (endpoint);
    url << (url.containsChar ('?') ? "&" : "?")
        << "product=" << juce::URL::addEscapeChars (productIn, true)
        << "&current=" << juce::URL::addEscapeChars (currentVersionIn, true)
        << "&platform=" << juce::URL::addEscapeChars (platformIn, true)
        << "&architecture=" << juce::URL::addEscapeChars (architectureIn, true);
    return url;
}

inline bool HttpUpdateTransport::fetch (const juce::String& productIn,
                                        const juce::String& currentVersionIn,
                                        const juce::String& platformIn,
                                        const juce::String& architectureIn,
                                        UpdateInfo& out)
{
    const auto requestUrl = buildUrl (productIn, currentVersionIn, platformIn, architectureIn);

    juce::WebInputStream stream (juce::URL (requestUrl), false);
    stream.withConnectionTimeout (10000);

    {
        const std::lock_guard<std::mutex> lock (streamMutex);
        if (requestsCancelled)
            return false;
        activeStream = &stream;
    }
    struct Unregister
    {
        HttpUpdateTransport& t;
        ~Unregister() { const std::lock_guard<std::mutex> lock (t.streamMutex); t.activeStream = nullptr; }
    } unregister { *this };

    if (! stream.connect (nullptr))
        return false;

    const int status = stream.getStatusCode();
    const auto text = stream.readEntireStreamAsString();
    if (status != 200 || text.isEmpty())
        return false;

    const auto parsed = juce::JSON::parse (text);
    if (parsed.isVoid())
        return false;

    return parseUpdateResponse (parsed, currentVersionIn, out);
}

//==============================================================================

inline UpdateChecker::UpdateChecker (std::unique_ptr<UpdateTransport> transportIn,
                                     juce::String productIn,
                                     juce::String installedVersionIn,
                                     juce::String platformIn,
                                     juce::String architectureIn)
    : juce::Thread ("PumpCurveUpdateCheck"),
      transport (std::move (transportIn)),
      product (std::move (productIn)),
      installedVersion (std::move (installedVersionIn)),
      platform (std::move (platformIn)),
      architecture (std::move (architectureIn))
{
    jassert (transport != nullptr);

    info.installedVersion = installedVersion;
    if (! parseVersion (installedVersion).valid)
        info.detail = "This build does not report a comparable version.";
}

inline void HttpUpdateTransport::cancelPendingRequests()
{
    const std::lock_guard<std::mutex> lock (streamMutex);
    requestsCancelled = true;
    if (activeStream != nullptr)
        activeStream->cancel();
}

inline UpdateChecker::~UpdateChecker()
{
    // Never leave a request running past destruction - and never make JUCE kill
    // the thread mid-request: abort the network wait first (the 4 s grace is
    // shorter than the 10 s connect timeout).
    signalThreadShouldExit();
    transport->cancelPendingRequests();
    stopThread (4000);
}

inline bool UpdateChecker::isDueLocked() const
{
    if (inFlight)
        return false;
    if (! everChecked)
        return true;
    return monotonicSeconds() - lastCheckAt >= checkInterval;
}

inline bool UpdateChecker::isDueForTesting() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return isDueLocked();
}

inline UpdateInfo UpdateChecker::snapshot() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return info;
}

inline void UpdateChecker::setCheckIntervalSecondsForTesting (double seconds)
{
    const std::lock_guard<std::mutex> lock (mutex);
    checkInterval = juce::jmax (0.0, seconds);
}

inline void UpdateChecker::requestCheck()
{
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (inFlight)
            return;                 // one at a time; a duplicate request is a no-op
        checkRequested = true;
    }

    if (! isThreadRunning())
        startThread();
    notify();
}

inline void UpdateChecker::refreshIfDue()
{
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (! isDueLocked())
            return;
    }
    requestCheck();
}

inline void UpdateChecker::checkNow()
{
    {
        // Manual retry: report the in-flight state straight away so the UI
        // shows something truthful while the request runs.
        const std::lock_guard<std::mutex> lock (mutex);
        if (inFlight)
            return;
        info.status = Status::checking;
        info.detail = "Checking for updates...";
    }
    requestCheck();
}

inline void UpdateChecker::checkNowForTesting()
{
    performCheck();
}

inline void UpdateChecker::run()
{
    while (! threadShouldExit())
    {
        wait (-1);

        if (threadShouldExit())
            return;

        bool requested = false;
        {
            const std::lock_guard<std::mutex> lock (mutex);
            requested     = checkRequested;
            checkRequested = false;
        }

        if (requested)
            performCheck();
    }
}

inline void UpdateChecker::performCheck()
{
    // The state the last COMPLETED check left behind. It is captured before the
    // in-flight marker is set, because that marker overwrites `status` and the
    // failure path below has to reason about what was known BEFORE this attempt.
    UpdateInfo previous;
    {
        const std::lock_guard<std::mutex> lock (mutex);
        previous = info;
        inFlight = true;
        info.status = Status::checking;
        if (info.detail.isEmpty())
            info.detail = "Checking for updates...";
    }

    UpdateInfo result;
    const bool ok = transport->fetch (product, installedVersion, platform, architecture, result);

    {
        const std::lock_guard<std::mutex> lock (mutex);
        inFlight    = false;
        everChecked = true;
        lastCheckAt = monotonicSeconds();

        if (ok)
        {
            // Keep the installed version the plugin knows it is, whatever the
            // transport reported, and never inherit a stale link.
            result.installedVersion = installedVersion;
            info = result;
        }
        else
        {
            // Offline or unusable answer. This changes ONLY what the INFO page
            // reports: the plugin keeps processing audio, the demo session and
            // the licensing state are untouched, and a previously known newer
            // version is not silently forgotten.
            info.status = Status::unableToCheck;

            if (previous.latestVersion.isNotEmpty() && previous.status == Status::updateAvailable)
            {
                info.latestVersion = previous.latestVersion;
                info.downloadUrl   = previous.downloadUrl;
                info.releaseNotes  = previous.releaseNotes;
                info.publishedAt   = previous.publishedAt;
                info.mandatory     = previous.mandatory;
                info.detail = "Could not reach the update server. An update to " + previous.latestVersion
                                + " was reported earlier; check your connection and try again.";
            }
            else
            {
                // Nothing was known, so nothing is claimed: no version, no link.
                info.latestVersion.clear();
                info.downloadUrl.clear();
                info.detail = "Could not reach the update server. Check your connection and try again.";
            }
        }
    }
}

} // namespace sid::update
