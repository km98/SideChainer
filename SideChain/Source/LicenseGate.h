/*
    PumpCurve - demo session gate and entitlement policy (0.5.0).

    PRODUCT BEHAVIOUR (the whole model, deliberately simple):

      * FULL version  - the user has a server-verified entitlement: either an
                        active, paid Music-Prod+ subscription (verified with
                        the existing Music-Prod account contract) or a valid
                        Music-Prod licence key ACTIVATED ON THIS COMPUTER (see
                        the contract note below). A standalone licence is
                        permanent and needs no subscription, and each key may be
                        active on up to three computers at once; the owner can
                        release one of those slots in their Music-Prod account,
                        which withdraws that computer's entitlement.
                        Audio processing is never time-limited.
      * DEMO version  - no verified entitlement: the plugin instance processes
                        audio for 15 minutes, measured from the moment that
                        instance entered demo mode. When the session expires,
                        audio processing stops and the plugin outputs silence
                        until the instance is restarted; a new instance starts
                        a fresh 15-minute session.

    There is deliberately NO permanent trial, NO daily allowance and NO
    persistent lifetime countdown. Restarting the instance is the documented
    way to get another 15 minutes.

    WHAT IS NEVER DONE (anti-bypass):
      * The remaining session time is never serialized into plugin state, so
        saving/reloading a DAW project cannot extend, reset or bypass a live
        session, and a hand-edited state cannot grant the full version.
      * The demo session is measured with a monotonic clock, never with the
        wall clock, so moving the system clock/date cannot extend a session.
      * The audio thread only ever reads two atomics (below). Entitlement
        verification is network work and lives on the auth thread; nothing in
        this file performs I/O, takes a lock or allocates.

    CLOCK CHOICE: juce::Time::getMillisecondCounterHiRes() - on macOS
    mach_absolute_time(). It only moves forward and ignores wall-clock
    changes, timezone changes and NTP steps. Documented consequence: it does
    not advance while the machine is asleep, so a sleeping host pauses an
    active demo session rather than consuming it. That is intentional - the
    session measures time the instance was actually running.

    ENTITLEMENT CACHE POLICY (both routes): the Music-Prod plugin
    documentation specifies caching the entitlement response locally with a
    timestamp so the plugin keeps working offline, refreshed on every launch.
    This file holds the conservative bound for that cache
    (kEntitlementGraceSeconds). What is cached is the SERVER's answer - the
    product-scoped entitlement and the licence verdict that came with it - so a
    verified subscriber or licence holder is never silently downgraded by a
    failed network request, and a stale cache eventually stops granting the
    full version. Nothing is ever inferred locally. See MusicProdAuth for the
    implementation and for the residual risk note.

    LICENCE-KEY CONTRACT STATUS: the Music-Prod backend this plugin
    authenticates against (supabase/functions/vyre-plugin-auth) implements the
    `activate-license` action on the SAME authenticated service and the SAME
    device token as start/poll/entitlements, backed by `plugin_licenses`
    (looked up by SHA-256 hash, never by plaintext) and
    `plugin_license_activations` (the device slots). The product is taken from
    the TOKEN, so a client can never name the product it is activating for. The
    client treats only the documented licence answers as verified information
    and fails closed on everything else: an unexpected 200, a malformed body, a
    rejected token or any transport failure yields `networkError`/
    `signInRequired`, which never grants the full version. The contract is
    documented in one place - supabase/functions/vyre-plugin-auth/license.ts -
    and mirrored in MusicProdAuth.h.

    WHY A LICENCE NEEDS ITS OWN (SHORTER) OFFLINE WINDOW: entitlement cache
    policy, both routes:
      * the SUBSCRIPTION route may be cached for kEntitlementGraceSeconds (14
        days). A lapsed subscription is a billing event that the next renewal
        attempt surfaces anyway, so a long offline window costs nothing real.
      * the LICENCE route may only be cached for
        kLicenseActivationGraceSeconds (7 days), because it is DEVICE-bound: a
        deactivated device has to stop working "at the next server answer", and
        a machine that never reconnects must not hold the full version
        indefinitely on the strength of a local file. The shorter window is the
        documented bound on that.

    DEACTIVATION, IN ONE SENTENCE: releasing a device in the account area is
    effective at that device's next successful entitlement check - every launch
    and every hour while online, and no later than 7 days after the last one if
    it never comes back online, whether or not the DAW is restarted. The end of
    a Music-Prod+ membership is handled the same way, within 14 days offline.
    Nothing local can restore either, because the grant is re-derived from the
    server's answer rather than from a stored flag.
*/

#pragma once

#include <JuceHeader.h>

#include <atomic>

namespace sid::license
{

//==============================================================================
// Monotonic clock seam.
//
// Production uses JUCE's high-resolution counter (see the clock note above).
// Test builds install a controllable source so a 15-minute session can be
// exercised without waiting 15 real minutes.
//==============================================================================

using MonotonicMsFn = double (*)();

inline MonotonicMsFn& monotonicMsFnStorage() noexcept
{
    static MonotonicMsFn fn = nullptr;
    return fn;
}

inline double monotonicMilliseconds() noexcept
{
    if (auto* fn = monotonicMsFnStorage())
        return fn();

    return juce::Time::getMillisecondCounterHiRes();
}

/** Install a controllable monotonic clock (tests only). */
inline void setMonotonicMillisecondsForTest (MonotonicMsFn fn) noexcept
{
    monotonicMsFnStorage() = fn;
}

/** Restore the production monotonic clock. */
inline void resetMonotonicMillisecondsForTest() noexcept
{
    monotonicMsFnStorage() = nullptr;
}

//==============================================================================
// Policy constants.
//==============================================================================

/** Demo session length: 15 minutes, per plugin instance. */
inline constexpr double kDemoSessionSeconds = 15.0 * 60.0;

/** How long a server-verified subscription answer may keep granting the full
    version without a fresh server answer.

    Chosen conservatively (14 days): long enough that a subscriber who is away
    from the internet keeps working, short enough that a cached or forged
    local answer cannot grant the full version indefinitely. The cache is
    refreshed on every launch and after every successful sign-in, so while
    online the server's answer always wins. */
inline constexpr double kEntitlementGraceSeconds = 14.0 * 24.0 * 60.0 * 60.0;

/** How long a server-verified LICENCE answer (a device activation) may keep
    granting the full version without a fresh server answer.

    Deliberately shorter than kEntitlementGraceSeconds: the licence route is
    bound to a DEVICE, so it is the route a deactivation has to be able to
    withdraw. Seven days is long enough that a licence holder who is away from
    the internet keeps working through a normal absence, and short enough that a
    released device cannot sit on the full version indefinitely. Must never
    exceed kEntitlementGraceSeconds - a test asserts that. */
inline constexpr double kLicenseActivationGraceSeconds = 7.0 * 24.0 * 60.0 * 60.0;

/** How often a signed-in instance that is RUNNING re-checks its entitlement
    with the server. A launch always re-checks; this bounds the staleness of a
    DAW session that stays open for days, which is what makes a deactivation -
    or the end of a Music-Prod+ membership - land without waiting for the next
    launch. Hourly: an ended membership returns the plugin to the demo within
    the hour while online, at one small request per hour per signed-in
    instance. The request is made on the auth thread, never on the audio
    thread. */
inline constexpr double kEntitlementRefreshIntervalSeconds = 60.0 * 60.0;

//==============================================================================
/** "MM:SS" for the demo countdown (hours roll into minutes, so a value above
    59 minutes still reads correctly rather than truncating). */
inline juce::String formatClock (double seconds) noexcept
{
    const auto total = (int) juce::jmax (0.0, std::floor (seconds + 0.5));
    const int minutes = total / 60;
    const int secs    = total % 60;

    return juce::String (minutes).paddedLeft ('0', 2) + ":"
         + juce::String (secs).paddedLeft ('0', 2);
}

//==============================================================================
/** One plugin instance's demo session.

    The instance starts in demo mode with a fresh 15-minute session. It leaves
    demo mode only through setFullVersion(true), which is driven exclusively by
    a server-verified entitlement (never by plugin state, never by the user's
    machine). Losing the entitlement re-enters demo mode with a fresh session
    from that moment.

    Audio-thread contract: processingAllowed(), expired(), elapsedSeconds() and
    remainingSeconds() are lock-free, allocation-free and I/O-free. They read
    at most two atomics plus the monotonic counter.
*/
class DemoGate
{
public:
    explicit DemoGate (double sessionSeconds = kDemoSessionSeconds) noexcept
        : sessionSeconds_ (sessionSeconds)
    {
        sessionStartMs_.store (monotonicMilliseconds(), std::memory_order_relaxed);
    }

    //--------------------------------------------------------------------------
    // Audio thread.

    /** True while audio processing is permitted. */
    bool processingAllowed() const noexcept
    {
        return fullVersion_.load (std::memory_order_relaxed) || ! expired();
    }

    //--------------------------------------------------------------------------
    // Any thread.

    bool isFullVersion() const noexcept
    {
        return fullVersion_.load (std::memory_order_relaxed);
    }

    double elapsedSeconds() const noexcept
    {
        const double start = sessionStartMs_.load (std::memory_order_relaxed);
        return juce::jmax (0.0, (monotonicMilliseconds() - start) / 1000.0);
    }

    double remainingSeconds() const noexcept
    {
        return juce::jmax (0.0, sessionSeconds_ - elapsedSeconds());
    }

    bool expired() const noexcept
    {
        if (fullVersion_.load (std::memory_order_relaxed))
            return false;

        return elapsedSeconds() >= sessionSeconds_;
    }

    /** Grant or withdraw the full version.

        Called by the auth layer (never by the audio thread and never from
        plugin state). Entering demo mode starts a fresh session, so an
        entitlement that disappears mid-session leaves the user with the full
        15 minutes from that moment instead of an already-expired session. */
    void setFullVersion (bool full) noexcept
    {
        const bool was = fullVersion_.load (std::memory_order_relaxed);
        if (full == was)
            return;

        fullVersion_.store (full, std::memory_order_relaxed);

        if (! full)
            sessionStartMs_.store (monotonicMilliseconds(), std::memory_order_relaxed);
    }

    double sessionSeconds() const noexcept { return sessionSeconds_; }

private:
    const double sessionSeconds_;
    std::atomic<double> sessionStartMs_ { 0.0 };
    std::atomic<bool>   fullVersion_ { false };

    JUCE_DECLARE_NON_COPYABLE (DemoGate)
};

//==============================================================================
/** True when a stored (server-verified) subscription answer may still be
    trusted without a fresh server answer.

    `verifiedAtMs` is wall-clock (juce::Time::currentTimeMillis()) because the
    cache has to survive plugin restarts, which a monotonic counter cannot do.

    Documented residue: a wall-clock cache can be stretched by moving the
    system clock back. The window is therefore bounded by
    kEntitlementGraceSeconds, every online launch overwrites the cache with the
    server's answer, and anything older than the window is ignored. A cache
    that is missing, malformed or not fresh is never trusted. */
inline bool cachedEntitlementIsFresh (double verifiedAtMs, double nowMs) noexcept
{
    if (! (verifiedAtMs > 0.0))
        return false;

    const double ageSeconds = (nowMs - verifiedAtMs) / 1000.0;

    return ageSeconds >= 0.0 && ageSeconds <= kEntitlementGraceSeconds;
}

/** The same freshness question for the LICENCE route, with its own shorter
    window (see the policy note at the top of this file): a DEVICE-bound grant
    has to be revocable by a deactivation in the account area, so it may not be
    trusted offline for as long as a subscription. */
inline bool cachedLicenseActivationIsFresh (double verifiedAtMs, double nowMs) noexcept
{
    if (! (verifiedAtMs > 0.0))
        return false;

    const double ageSeconds = (nowMs - verifiedAtMs) / 1000.0;

    return ageSeconds >= 0.0 && ageSeconds <= kLicenseActivationGraceSeconds;
}

/** Has a grant held by a RUNNING instance outlived its offline window?

    The running counterpart of cachedEntitlementIsFresh: the auth thread asks it
    on every wake and after every failed check, so an instance that stays open
    offline loses the full version when the window closes instead of keeping it
    until the next launch. `verifiedAtMs <= 0` (never verified) has expired.

    Unlike the launch rule, an answer stamped LATER than now does not expire
    here: the instance received that answer itself, so a negative age only
    means the clock was corrected backwards since (an NTP step right after a
    check would otherwise send a member to the demo for an hour). Winding the
    clock back to stretch the window is the residue documented above, and the
    next launch still applies the strict rule. */
inline bool runningGrantExpired (double verifiedAtMs, double nowMs, double windowSeconds) noexcept
{
    if (! (verifiedAtMs > 0.0))
        return true;

    return (nowMs - verifiedAtMs) / 1000.0 > windowSeconds;
}

/** How long an AUTOMATIC (lifecycle-triggered) entitlement refresh is held off
after the previous attempt.

    Separate from both of the other intervals, because it answers a different
    question:
      * `kEntitlementRefreshIntervalSeconds` (1h) bounds how STALE a running
        instance's answer may become;
      * this one bounds how often the plugin may ASK because the user did
        something in the UI (opened the page, brought the editor back).

    Reopening the INFO page, or bringing the editor to the front, must re-check
    the subscription - and must not turn a fidgety user into a request flood, so
    a repeat within a minute is ignored.

    The manual CHECK SUBSCRIPTION control is deliberately NOT subject to this:
    the whole point of pressing it is that the customer has just changed
    something on their account (typically a purchase), and they should not be
    told to wait a minute for the truth. */
inline constexpr double kEntitlementAutoRefreshMinIntervalSeconds = 60.0;

/** Should a LIFECYCLE-triggered automatic refresh run now?

    `lastAttemptMs` is when the last entitlement request was issued (wall clock,
    0 = none in this session). A clock that moved BACKWARDS is not evidence that
    the interval elapsed, so it does not authorise a request - unlike
    `entitlementRefreshDue` below, where the fail-safe direction is to refresh:
    missing a refresh costs nothing (the hourly rule and the manual control both
    still apply), while trusting a rewound clock would only cause requests. */
inline bool entitlementAutoRefreshDue (double lastAttemptMs, double nowMs,
                                       double minIntervalSeconds =
                                           kEntitlementAutoRefreshMinIntervalSeconds) noexcept
{
    if (! (lastAttemptMs > 0.0))
        return true;

    const double ageSeconds = (nowMs - lastAttemptMs) / 1000.0;

    return ageSeconds >= minIntervalSeconds;
}

/** Should a RUNNING, signed-in instance re-check its entitlement with the
    server now?

    Bounds how stale the cached answer can become while the plugin stays open:
    a launch always checks, and a long session checks again after
    kEntitlementRefreshIntervalSeconds. Without this, a computer that is
    deactivated while the DAW is running would keep the full version until the
    next launch (and, offline, until the grace window expired).

    Pure so the timing rule is tested without waiting an hour.
    `verifiedAtMs <= 0` means "never verified", which is always due. */
inline bool entitlementRefreshDue (double verifiedAtMs, double nowMs,
                                   double intervalSeconds = kEntitlementRefreshIntervalSeconds) noexcept
{
    if (! (verifiedAtMs > 0.0))
        return true;

    const double ageSeconds = (nowMs - verifiedAtMs) / 1000.0;

    // A clock moved BACKWARDS is not evidence of freshness, but it is also not
    // evidence of staleness; treating it as due is the fail-safe choice because
    // a refresh can only ever replace a cached answer with the server's.
    return ! (ageSeconds >= 0.0) || ageSeconds >= intervalSeconds;
}

} // namespace sid::license
