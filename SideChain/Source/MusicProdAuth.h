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

    KNOWN BACKEND LIMITATION (see PHASE8_REPORT.md): the edge function's
    verification URL is music-prod.com/vyre/link and the approval page is
    branded "Connect VYRE". Until the backend provides a SideChain (or
    product-neutral) verification page, the UI does NOT auto-open a
    VYRE-branded page and shows the URL for review instead. The protocol
    itself is generic and ready.

    THREADING: all network I/O happens on a dedicated background thread
    (or in tests, synchronously via an injected AuthTransport). Never call
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
#include <mutex>

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
    bool subscribed  = false;
    bool authFailure = false; // true when the server rejected the token (401)
};

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

private:
    juce::var postJson (const juce::String& body, int* statusCode);

    juce::String endpoint;
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
    void refreshEntitlements();

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

    // Token file present from a previous session? Called once at startup
    // BEFORE the manager is used; loads token/display name (no network).
    bool loadPersistedToken();

private:
    void run() override;
    void clearLocalToken();
    void persistTokenLocked();          // caller holds mutex
    void applyPollResult (const PollResponse& r);
    void applyEntitlements (const EntitlementsResponse& e);

    std::unique_ptr<AuthTransport> transport;
    const juce::String authFilePath;
    const juce::String product;      // validated server-side against its allowlist
    const juce::String deviceName;
    const juce::String pluginVersion;

    mutable std::mutex mutex;
    State        state = State::signedOut;
    juce::String token;
    juce::String displayName;
    juce::String deviceCode;   // while linking
    juce::String userCode;     // while linking
    juce::String verificationUrl;
    bool         subscribed = false;
    juce::String lastError;

    // Background-work flags (guarded by mutex; consumed by run()).
    bool logoutRequested        = false;
    bool entitlementsRequested  = false;

    // Live approval polling (production only; see setBackgroundPolling).
    bool backgroundPolling = false;
    int  pollIntervalMs    = 5000;   // server-advised poll interval (start response)

    JUCE_DECLARE_NON_COPYABLE (AuthManager)
};

} // namespace sid::auth
