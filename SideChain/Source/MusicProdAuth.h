/*
    SideChain - Music-Prod account authentication (Phase 8).

    Product-neutral C++ port of the existing Music-Prod plugin device-code
    flow (the same flow the VYRE plugin uses; identical request/response
    protocol against the identical Supabase edge function):

        1. POST { action: "start",  device_name, plugin_version }
           -> device_code, user_code, verification_url(_complete), expires_in, interval
        2. User opens the verification URL, signs in, approves the code
        3. POST { action: "poll", device_code } (repeatedly)
           -> { status: "pending" | "approved" | "denied" | "expired" | "invalid" }
              on "approved": token, display_name (token minted once)
        4. POST { action: "entitlements", token }
           -> { ok, subscribed, packs: [...] }
        5. POST { action: "logout", token }  (revokes the token server-side)

    PRODUCT IDENTITY: this plugin identifies itself as "SideChain"
    (device_name). No VYRE-specific identity is sent.

    VERIFICATION PAGE: the server returns music-prod.com/plugin/link with
    product=sidechain (the earlier VYRE-only page is no longer used), and the
    INFO page opens exactly the URL the server returned.

    THREADING: entitlement, licence, logout and approval-poll requests run on
    a dedicated background thread (or, in tests, synchronously via an injected
    AuthTransport). The one exception is `start`, sent when the user presses
    LOG IN: it runs on the message thread (bounded by a 10 s connect timeout). Never call
    anything here from processBlock. State is guarded by a single mutex;
    the audio thread never touches this class.

    TOKEN STORAGE: token + display name persist to a JSON file under
    <ApplicationDataDirectory>/Music-Prod/SideChain/auth.json, mirroring
    the existing plugin architecture (VYRE stores auth.json in its app
    data folder). The token is NEVER stored in plugin (DAW) state.

    TESTING: AuthTransport is an interface; production uses HttpAuthTransport
    (juce::WebInputStream). Tests inject a mock transport and drive the
    state machine deterministically without touching the network.
*/

#pragma once

#include <JuceHeader.h>
#include <functional>
#include <mutex>

// The entitlement-refresh INTERVALS live with the rest of the demo/entitlement
// policy (sid::license, LicenseGate.h), so this class's default debounce and the
// plugin's staleness rule cannot drift apart.
#include "LicenseGate.h"

namespace sid::auth
{

// ----------------------------------------------------------------------------

struct StartResponse
{
    bool    ok = false;
    juce::String deviceCode;
    juce::String userCode;
    juce::String verificationUrl;
    juce::String verificationUrlComplete;
    int     expiresInSeconds = 0;
    int     intervalSeconds  = 5;
};

struct PollResponse
{
    // "pending" | "approved" | "denied" | "expired" | "invalid" | "error"
    juce::String status;
    juce::String token;        // valid when status == "approved"
    juce::String displayName;  // may be empty
};

struct EntitlementsResponse
{
    bool ok          = false;
    /**
        The user's Music-Prod+ membership, as the server reports it. NOT the
        answer for this plugin: a membership only entitles a product the
        subscription actually covers (see productEntitled).
    */
    bool subscribed  = false;
    bool authFailure = false; // true when the server rejected the token (401)

    /**
        THE answer for THIS plugin's product, computed server-side as
        (subscription active AND subscription covers this product) OR (a licence
        key active for this user and this product). The server is the only place
        that combines the two routes, so the client never ORs independent flags
        and can never widen its own entitlement.
    */
    bool productEntitled = false;

    juce::String product;                  // echo of the product the TOKEN is for
    bool subscriptionActive = false;       // raw membership
    bool subscriptionCoversProduct = false;// ... and does it cover this product?

    /**
        The LICENCE route for THIS computer (server-verified).

        A standalone licence is permanent and needs no subscription, and one key
        may be active on up to three computers at once, so the server reports the
        two facts separately:
          * licenseAccountActive - the ACCOUNT holds an active licence;
          * licenseDeviceActive  - THIS computer holds one of its slots.
        licenseActive is both, and is the licence route that actually grants.
        The pair is what lets the UI say "your licence is fine, this computer is
        simply not activated" instead of pretending the key is bad.
    */
    bool         licenseActive = false;
    bool         licenseAccountActive = false;
    bool         licenseDeviceActive = false;
    int          devicesUsed = 0;
    int          deviceLimit  = 0;         // 0 when an older server did not say
    juce::String licenseStatus;            // "none" | "active" | "revoked" | "expired"
    juce::String licenseExpiresAt;         // ISO date when the server sent one
};

// Result of verifying a Music-Prod licence key.
//
// The distinction that matters for security: a REJECTION (invalid / revoked /
// expired) is an answer from the server, and a network failure is NOT - it
// must never be treated as a rejection, and it must never be treated as a
// grant either. `unavailable` means no server contract exists (see below).
struct LicenseResponse
{
    enum class Status
    {
        ok,              // verified: this computer now holds a slot -> full version
        invalid,         // the server rejected the key (unknown, or bound elsewhere)
        revoked,         // the key was valid once; the entitlement has been revoked
        expired,         // the key's entitlement has expired
        deviceLimit,     // the key is FINE, but all activation slots are in use
        signInRequired,  // no account token: a key cannot be bound to nobody
        networkError,    // the server could not be reached: nothing was verified
        unavailable      // the server has no licence contract (fails closed)
    };

    Status       status = Status::unavailable;
    juce::String message;   // server-provided text when there is one

    /** The product the server answered for (the TOKEN's product). An `ok` for
        any other product is not a PumpCurve activation and is never honoured. */
    juce::String product;

    /** True when the server rejected the TOKEN (401): the caller signs out. */
    bool authFailure = false;

    /**
        Slot bookkeeping from the server, when it sent any. `deviceActivated`
        is the field that proves a slot was actually taken: `ok` is only
        believed when it is true, so a 200 that claims success without saying a
        device was activated cannot become a grant.
    */
    bool deviceActivated = false;
    int  deviceLimitUsed = 0;
    int  deviceLimit     = 0;
};

/**
    Turn an `activate-license` HTTP response into a LicenseResponse.

    Extracted from HttpAuthTransport so the ONE mapping that decides whether a
    key was judged (and how) is testable without a socket. This is where the
    safety properties live:

      * only the documented answers become a verdict on the key;
      * a 200 whose body is not one of those answers is a CONTRACT violation and
        is reported as `networkError` (unverified), never as a grant;
      * a 400 is a verdict ONLY when it is the documented `malformed_key`. An
        `unknown_action` 400 - a service that has not been deployed with this
        action yet - must not be shown to the customer as a rejected key;
      * anything else (401, 5xx, no response at all) is a TRANSPORT failure: it
        never grants, and it is never phrased as a rejection.

    `body` is the parsed JSON (a void var when nothing was received).
*/
LicenseResponse interpretLicenseResponse (int status, const juce::var& body);

/**
    Turn an `entitlements` HTTP response into an EntitlementsResponse.

    Extracted from HttpAuthTransport for the same reason as the licence mapping
    above: the deployed server's field names are the contract, and the ONE place
    that reads them has to be testable without a socket. It is the exact mirror of
    `entitlementsBody` in supabase/functions/vyre-plugin-auth/contract.ts.

    The fields that decide what the customer is TOLD, and that nothing else can
    substitute for:

      * `plugin_entitled`  - the product-scoped gate. Not the raw membership.
      * `license.active`   - the account holds a licence AND this device holds
                             one of its slots: the licence route that grants.
      * `license.account_active` / `license.device_active` - the two halves,
                             reported separately by the server. They are what
                             distinguishes "your licence is fine, this computer
                             is simply not activated" from "your licence is no
                             longer active" - `active` is false in both cases.
      * `license.devices_used` / `license.device_limit` - the "2 of 3 computers"
                             line in that message.

    Every field is read with a fail-closed default, so a missing or older-server
    answer grants nothing and never becomes a rejection.
*/
EntitlementsResponse interpretEntitlementsResponse (int status, const juce::var& body);

// ----------------------------------------------------------------------------

// Abstract HTTP boundary so the auth state machine is deterministically
// testable. Implementations must be called from the auth thread only.
class AuthTransport
{
public:
    virtual ~AuthTransport() = default;

    // `product` is the validated product identity (Phase 8B): sent as the
    // `product` field of the start request; the server resolves it against
    // its allowlist and returns the product's own verification page.
    virtual StartResponse        start        (const juce::String& product,
                                               const juce::String& deviceName,
                                               const juce::String& pluginVersion) = 0;
    virtual PollResponse         poll         (const juce::String& deviceCode) = 0;
    virtual EntitlementsResponse entitlements (const juce::String& token) = 0;
    virtual bool                 logout       (const juce::String& token) = 0;

    // Aborts a request that is blocked on the network and refuses new ones.
    // Called once, from the AuthManager's destructor, so closing a project
    // offline never has to wait out (or kill the thread during) a 10 s connect.
    virtual void                 cancelPendingRequests() {}

    // Licence-key verification: `activate-license` on the SAME authenticated
    // service and the SAME device token as start/poll/entitlements.
    //
    // The key is bound to the ACCOUNT behind `token`, so the token is part of
    // the operation, not an optional extra: a key cannot be validated for nobody
    // and the server refuses to guess an identity. The product is NOT a
    // parameter - the server uses the product the token was issued for, so a
    // client can never activate a licence for a product it names.
    //
    // Contract (mirrors supabase/functions/vyre-plugin-auth/license.ts):
    //   200 { ok:true,  status:"ok", device:{ activated:true } }  -> ok
    //   200 { ok:false, status:"invalid"|"revoked"|"expired" } -> the matching
    //        answer; HTTP 200 because the SERVER ANSWERED about the key
    //   200 { ok:false, status:"device_limit" }             -> deviceLimit: the
    //        key is valid AND bound to this account, but all three activation
    //        slots are in use. A statement about the licence, never about the
    //        key, and the customer is told to release a device.
    //   400 malformed_key / device_id_required, 401 invalid_token,
    //   5xx / no response                                       -> networkError
    //
    // Implementations must never treat a transport failure as a rejection or as
    // an activation, and must never fall back to validating a key locally.
    virtual LicenseResponse activateLicense (const juce::String& token,
                                             const juce::String& licenceKey)
    {
        juce::ignoreUnused (token, licenceKey);
        return {};   // unavailable (fail closed)
    }

    // The DEVICE this installation is. Handed to the transport once the
    // AuthManager has resolved it (see AuthManager::getDeviceId), so the two
    // requests that involve a device - `start` and `activate-license` - can name
    // it without changing this interface's shape for every existing
    // implementation. A transport that ignores it simply does not send a device
    // id, and the server then refuses to take a slot
    // (400 device_id_required) rather than inventing one: activation fails
    // safely instead of silently weakening the three-device rule.
    virtual void setDeviceIdentity (const juce::String& deviceId,
                                    const juce::String& deviceLabel,
                                    const juce::String& platform)
    {
        juce::ignoreUnused (deviceId, deviceLabel, platform);
    }
};

// ----------------------------------------------------------------------------

// Production transport: POSTs JSON to the Music-Prod Supabase edge function.
// The endpoint origin is compiled in (same origin the existing plugin flow
// uses; not a secret). No tokens are logged.
class HttpAuthTransport : public AuthTransport
{
public:
    // endpoint: full URL of the plugin-auth edge function.
    explicit HttpAuthTransport (juce::String endpoint);

    StartResponse        start        (const juce::String& product,
                                       const juce::String& deviceName,
                                       const juce::String& pluginVersion) override;
    PollResponse         poll         (const juce::String& deviceCode) override;
    EntitlementsResponse entitlements (const juce::String& token) override;
    bool                 logout       (const juce::String& token) override;
    void                 cancelPendingRequests() override;

    // Production licence activation: POSTs `activate-license` to the same
    // plugin-auth endpoint with the device token. Never invoked from the audio
    // thread, never stores the key.
    LicenseResponse      activateLicense (const juce::String& token,
                                           const juce::String& licenceKey) override;

    // Records the device id/label the AuthManager resolved. Sent (hashed by the
    // server, never stored raw) with `start` and `activate-license`; never part
    // of any log line.
    void setDeviceIdentity (const juce::String& deviceId,
                            const juce::String& deviceLabel,
                            const juce::String& platform) override;

private:
    juce::var postJson (const juce::String& body, int* statusCode);

    juce::String endpoint;

    // The request in progress (if any), so cancelPendingRequests() can abort it.
    std::mutex streamMutex;
    juce::WebInputStream* activeStream = nullptr;
    bool requestsCancelled = false;

    // The device identity, set by AuthManager before the first request that
    // needs it. Empty until then; the requests that use it are only ever issued
    // after it has been resolved.
    juce::String deviceId;
    juce::String deviceLabel;
    juce::String platform;
};

// ----------------------------------------------------------------------------

// Auth state machine + token persistence. Network calls run on the internal
// background thread; tests can drive the machine deterministically.
class AuthManager : private juce::Thread
{
public:
    enum class State
    {
        signedOut,   // no token
        linking,     // device code issued, waiting for user approval
        signedIn,    // token present (entitlement state separate)
        error        // last operation failed (network/server)
    };

    AuthManager (std::unique_ptr<AuthTransport> transport,
                 juce::String authFilePath,
                 juce::String product,
                 juce::String deviceName,
                 juce::String pluginVersion);
    ~AuthManager() override;

    // ---- actions (message thread) --------------------------------------

    void startLinking();          // signed out -> linking
    void cancelLinking();         // linking -> signed out
    void logout();                // revokes server-side (async), clears local

    // Enable the background approval poller (production). While in the
    // linking state the auth thread polls the server at the server-advised
    // interval, so a browser approval automatically flips the plugin to
    // SIGNED IN - no DAW restart, no manual refresh. Tests leave this OFF
    // and drive polls deterministically via pollOnceForTesting().
    void setBackgroundPolling (bool enable);

    // Refresh entitlements for the stored token (async). Safe when signed out.
    //
    // FORCED: this is the manual CHECK SUBSCRIPTION path and the new-instance
    // path. It is not held off by the staleness rule or by the lifecycle
    // debounce, because the reason to ask is that something changed on the
    // account (a purchase) and the customer should not have to wait - or log
    // out and back in - to see it.
    void refreshEntitlements();

    /** Ask for a fresh entitlement answer, at most once per `minIntervalSeconds`.

        For LIFECYCLE events (the INFO page was opened, the editor became visible
        again), which repeat far more often than the server needs to be asked.
        Held off while a check is already in flight, and ignored while the
        previous attempt is younger than the interval. Never grants anything by
        itself: it only asks, and only the server's answer is applied. */
    void refreshEntitlementsIfStale (double minIntervalSeconds =
                                         sid::license::kEntitlementAutoRefreshMinIntervalSeconds);

    /** True between queueing an entitlement check and the server's answer to it

        (any answer, including a failure). The UI uses it to show a checking
        state instead of a stale verdict while a request is running. */
    bool isEntitlementCheckInFlight() const;

    /** True when the most recent entitlement check got NO answer (network or
        server failure), as opposed to a confirmed inactive answer. The last
        verified state is kept either way - a failure never grants and never
        revokes - but the UI needs to say "could not check, try again" rather
        than repeat a verdict the server did not just give. Cleared by the next
        real answer and by signing out. */
    bool didLastEntitlementCheckFail() const;

    /** True when the server confirmed an active Music-Prod+ membership but did
        NOT confirm it for THIS product (plugin_entitled false). The UI must not
        present that membership as having unlocked anything. The common cause is
        a sign-in token minted for another Music-Prod plugin (or before the
        server recorded the product on tokens): signing in again from PumpCurve
        mints a token for the right product. */
    bool subscriptionNotValidForProduct() const;

    /** True when the server's last answer named a DIFFERENT product than the
        one this manager signs in for (the token belongs to another plugin). */
    bool tokenIsForAnotherProduct() const;

    // ---- state access (any thread; copies under lock) -------------------

    State        getState() const;
    juce::String getDisplayName() const;
    bool         isSubscribed() const;
    juce::String getUserCode() const;        // while linking
    juce::String getVerificationUrl() const; // while linking
    juce::String getLastError() const;

    // Test hook: run one poll synchronously (production polls on the thread).
    void pollOnceForTesting();

    // Test hook: synchronously fetch entitlements (production: async).
    void refreshEntitlementsForTesting();

    /** Test hook: pretend the last entitlement ATTEMPT was made

        `millisecondsAgo` milliseconds ago, so the lifecycle debounce
        (refreshEntitlementsIfStale) can be exercised without a test waiting a
        minute. Only ever called by tests; production never moves the clock
        backwards or forwards on this record.
    */
    void backdateEntitlementAttemptForTesting (double millisecondsAgo);

    // ---- licence-key entitlement (message thread) ------------------------
    //
    // The deployed Music-Prod service implements the `activate-license` action
    // (supabase/functions/vyre-plugin-auth/license.ts), and
    // HttpAuthTransport::activateLicense is the production client for it, so a
    // verified key does grant the full version. `unavailable` remains the
    // fail-closed answer for any transport that does NOT implement the operation
    // (the AuthTransport default), and for a service that answers an action it
    // does not know - a key is never granted on a response nobody documented.
    //
    // A licence activation is deliberately never persisted. The entitlement has
    // to be re-derived from the server's answer on every launch, because it is
    // scoped to an ACTIVATION SLOT: a stored "active" flag would keep granting
    // the full version after the owner released this computer - exactly what
    // must not ship.
    void activateLicense (const juce::String& licenceKey);            // async
    void activateLicenseForTesting (const juce::String& licenceKey);  // sync

    enum class LicenseStatus
    {
        notChecked,     // no key submitted in this instance
        validating,     // request in flight
        active,         // server verified the key AND this device holds a slot
        invalid,        // server rejected the key
        revoked,        // previously valid; entitlement revoked or expired
        deviceLimit,    // the key is valid, but all device slots are in use
        deviceInactive, // the account holds the licence; THIS computer is not activated
        unavailable,    // no server licence contract (fails closed)
        networkError,   // nothing was verified; NOT a rejection
        signInRequired  // no account token: there is nothing to bind a key to
    };

    LicenseStatus getLicenseStatus() const;
    juce::String  getLicenseMessage() const;

    /**
        Which route is granting the full version right now, for the UI to name
        it honestly. Both may be true at once (a subscriber who also holds a
        key); neither being true means the instance is in demo mode.
    */
    bool fullVersionViaLicense() const;
    bool fullVersionViaSubscription() const;

    // The single question the audio gate cares about: does EITHER route grant
    // the full version?
    //   A) a server-verified licence key, or
    //   B) a server-verified, active paid Music-Prod+ subscription.
    bool hasFullEntitlement() const;

    // Fired OUTSIDE the lock whenever hasFullEntitlement() may have changed.
    // The processor mirrors the answer into its audio gate with a single
    // atomic store, so the audio thread never touches this class.
    std::function<void (bool fullVersion)> onEntitlementChanged;

    // Token file present from a previous session? Called once at startup
    // BEFORE the manager is used; loads token/display name (no network).
    bool loadPersistedToken();

    // ---- this installation's device identity -----------------------------
    //
    // A standalone licence entitles a DEVICE (up to three per key), so the
    // plugin has to be able to name the computer it runs on. The identity is:
    //
    //   * a random, per-INSTALLATION id created once and kept beside auth.json
    //     (`device_id.txt`). It is deliberately NOT derived from hardware - no
    //     serial, no MAC address, nothing that identifies who the user is -
    //     because the thing being activated and deactivated is "this install of
    //     PumpCurve", not the machine and not the person;
    //   * stable across restarts and DAW sessions, so a computer keeps its slot
    //     instead of silently consuming a new one every launch;
    //   * NEVER stored in plugin (DAW) state, exactly like the token: a saved
    //     project must not be able to carry an identity to another computer;
    //   * hashed by the server before storage, so the raw id never leaves this
    //     machine's own app-data folder in a readable form anywhere else.
    //
    // Resolved lazily (the file is created on first use, not while the plugin is
    // being scanned) and safe to call from any thread.
    juce::String getDeviceId();

    /** The human label the OWNER sees in their account (the computer's name). */
    juce::String getDeviceLabel() const;

private:
    void run() override;
    void clearLocalToken();
    void persistTokenLocked();          // caller holds mutex
    void applyPollResult (const PollResponse& r);
    void applyEntitlements (const EntitlementsResponse& e);
    void applyLicenseResult (const LicenseResponse& r);
    /** Caller holds the mutex. False when there is no account token to bind a key to. */
    bool licenseActivationPossibleLocked() const;
    bool isFullEntitlementLocked() const;
    void fireEntitlementChanged();

    /**
        Resolve this installation's device id, creating it on first use, and
        hand it to the transport so `start` and `activate-license` can name the
        computer. Caller must NOT hold the mutex (this touches the filesystem).

        A failure to create or read the file is not fatal to the plugin: the id
        stays empty, the server then refuses the activation with the documented
        `device_id_required`, and the customer is told to update rather than
        silently getting an entitlement that ignores the device policy.
    */
    void ensureDeviceIdentity();

    std::unique_ptr<AuthTransport> transport;
    const juce::String authFilePath;
    const juce::String product;      // validated server-side against its allowlist
    const juce::String deviceName;
    const juce::String pluginVersion;

    // This installation's device identity (see getDeviceId / ensureDeviceIdentity).
    // Guarded by `mutex`.
    juce::String deviceId;
    juce::String deviceLabel;
    juce::String platformName;

    mutable std::mutex mutex;
    State        state = State::signedOut;
    juce::String token;
    juce::String displayName;
    juce::String deviceCode;   // while linking
    juce::String userCode;     // while linking
    juce::String verificationUrl;
    bool         subscribed = false;

    // The PRODUCT-SCOPED gate answer for this plugin, straight from the server
    // (subscription that covers this product, OR a licence active for this
    // account). Never derived locally, never inferred from `subscribed`.
    bool         productEntitled = false;

    juce::String lastError;

    // Licence-key route (never persisted; see activateLicense notes).
    LicenseStatus licenseStatus = LicenseStatus::notChecked;
    juce::String  licenseMessage;

    // When the overall answer was last VERIFIED by the server (wall clock,
    // 0 = never). Only then may the cached answer be trusted offline (see
    // sid::license::cachedEntitlementIsFresh).
    double entitlementsVerifiedAtMs = 0.0;

    // When an entitlement request was last ISSUED (wall clock, 0 = none in this
    // session). Distinct from the VERIFIED stamp above on purpose: the debounce
    // has to count attempts, including the ones that fail, or a server that is
    // down would be retried on every lifecycle event.
    double lastEntitlementAttemptMs = 0.0;

    // True while a check has been asked for and no answer has arrived yet.
    bool entitlementCheckInFlight = false;

    // True when the last finished check produced no answer (see
    // didLastEntitlementCheckFail()).
    bool lastEntitlementCheckFailed = false;

    // The product the server says the TOKEN was issued for (echoed in every
    // entitlements answer; empty when the server did not say).
    juce::String answeredProduct;

    // When the LICENCE route was last verified by the server. Tracked
    // separately because that route is DEVICE-bound and therefore cached for a
    // SHORTER window (kLicenseActivationGraceSeconds): a deactivated computer
    // must stop working within days, not weeks.
    double licenseVerifiedAtMs = 0.0;

    // Last value handed to onEntitlementChanged, so the callback only fires
    // on a real change.
    bool lastPublishedFull = false;

    // Pending licence activation (consumed by run()).
    bool         licenseRequested = false;
    juce::String pendingLicenseKey;

    // Background-work flags (guarded by mutex; consumed by run()).
    bool logoutRequested        = false;
    bool entitlementsRequested  = false;

    // True once the device identity has been handed to the transport.
    bool deviceIdentityPublished = false;

    // Live approval polling (production only; see setBackgroundPolling).
    bool backgroundPolling = false;
    int  pollIntervalMs    = 5000;   // server-advised poll interval (start response)

    JUCE_DECLARE_NON_COPYABLE (AuthManager)
};

} // namespace sid::auth
