/*
    SideChain - Music-Prod account authentication implementation (Phase 8).

    See MusicProdAuth.h for the protocol, threading and storage model.
    Implementation notes:

      - Every response field is read defensively (missing fields, wrong
        types, non-200 statuses), exactly as the existing plugin flow does:
          * start        : needs device_code + user_code (+ urls)
          * poll         : needs status; approved additionally needs token
          * entitlements : needs ok; reads subscribed; 401 => authFailure
          * logout       : best-effort; local state clears regardless
      - HTTP 401 on entitlements means revoked/expired token -> local
        sign-out (same rule as the existing plugin flow).
      - Network/server errors keep the last known state (cached sign-in),
        matching the existing architecture.
      - No token is ever logged, displayed or stored in plugin state.
*/

#include "MusicProdAuth.h"

namespace sid::auth
{

namespace
{
    // Defensive var readers (missing/wrong type -> fallback).
    juce::String varToString (const juce::var& v)
    {
        return v.isVoid() ? juce::String() : v.toString();
    }

    bool varToBool (const juce::var& v, bool fallback)
    {
        if (v.isVoid())  return fallback;
        if (v.isBool())  return (bool) v;
        return fallback;
    }

    int varToInt (const juce::var& v, int fallback)
    {
        if (v.isVoid() || ! v.isDouble())
            return fallback;
        return (int) (double) v;
    }

    juce::String encodeRequest (juce::DynamicObject::Ptr req)
    {
        return juce::JSON::toString (juce::var (req));
    }
}

//==============================================================================
// HttpAuthTransport
//==============================================================================

HttpAuthTransport::HttpAuthTransport (juce::String endpointUrl)
    : endpoint (std::move (endpointUrl)) {}

juce::var HttpAuthTransport::postJson (const juce::String& body, int* statusCode)
{
    if (statusCode != nullptr)
        *statusCode = 0;

    // JUCE 6.1.3 WebInputStream: POST body via URL; chained header/timeout.
    juce::WebInputStream stream (juce::URL (endpoint).withPOSTData (body), true);
    stream.withExtraHeaders ("Content-Type: application/json")
          .withConnectionTimeout (10000);
    if (! stream.connect (nullptr))
        return juce::var();

    const int status = stream.getStatusCode();
    if (statusCode != nullptr)
        *statusCode = status;

    const auto text = stream.readEntireStreamAsString();
    if (text.isEmpty())
        return juce::var();

    const auto parsed = juce::JSON::parse (text);
    return parsed.isVoid() ? juce::var() : parsed;
}

StartResponse HttpAuthTransport::start (const juce::String& productIn,
                                        const juce::String& deviceNameIn,
                                        const juce::String& pluginVersionIn)
{
    StartResponse out;

    juce::DynamicObject::Ptr req = new juce::DynamicObject();
    req->setProperty ("action", "start");
    req->setProperty ("product", productIn);      // Phase 8B: allowlist key
    req->setProperty ("device_name", deviceNameIn);
    req->setProperty ("plugin_version", pluginVersionIn);

    int status = 0;
    const auto resp = postJson (encodeRequest (req), &status);

    if (status != 200 || resp.isVoid())
        return out;

    out.deviceCode              = varToString (resp.getProperty ("device_code", juce::var()));
    out.userCode                = varToString (resp.getProperty ("user_code", juce::var()));
    out.verificationUrl         = varToString (resp.getProperty ("verification_url", juce::var()));
    out.verificationUrlComplete = varToString (resp.getProperty ("verification_url_complete", juce::var()));
    out.expiresInSeconds        = varToInt (resp.getProperty ("expires_in", juce::var()), 900);
    out.intervalSeconds         = varToInt (resp.getProperty ("interval", juce::var()), 5);

    out.ok = out.deviceCode.isNotEmpty() && out.userCode.isNotEmpty();
    return out;
}

PollResponse HttpAuthTransport::poll (const juce::String& deviceCodeIn)
{
    PollResponse out;

    juce::DynamicObject::Ptr req = new juce::DynamicObject();
    req->setProperty ("action", "poll");
    req->setProperty ("device_code", deviceCodeIn);

    int status = 0;
    const auto resp = postJson (encodeRequest (req), &status);

    if (resp.isVoid())
    {
        out.status = "error"; // network/server failure; retry later
        return out;
    }

    out.status      = varToString (resp.getProperty ("status", juce::var()));
    out.token       = varToString (resp.getProperty ("token", juce::var()));
    out.displayName = varToString (resp.getProperty ("display_name", juce::var()));

    if (out.status.isEmpty())
        out.status = "error";

    return out;
}

EntitlementsResponse HttpAuthTransport::entitlements (const juce::String& tokenIn)
{
    EntitlementsResponse out;

    juce::DynamicObject::Ptr req = new juce::DynamicObject();
    req->setProperty ("action", "entitlements");
    req->setProperty ("token", tokenIn);

    int status = 0;
    const auto resp = postJson (encodeRequest (req), &status);

    if (status == 200 && ! resp.isVoid())
    {
        out.ok         = varToBool (resp.getProperty ("ok", juce::var()), false);
        out.subscribed = varToBool (resp.getProperty ("subscribed", juce::var()), false);
    }
    else if (status == 401)
    {
        out.authFailure = true; // token revoked/expired -> caller signs out
    }
    // Other statuses / network errors: neither ok nor authFailure.

    return out;
}

bool HttpAuthTransport::logout (const juce::String& tokenIn)
{
    juce::DynamicObject::Ptr req = new juce::DynamicObject();
    req->setProperty ("action", "logout");
    req->setProperty ("token", tokenIn);

    int status = 0;
    postJson (encodeRequest (req), &status);
    return status == 200;
}

//==============================================================================
// AuthManager
//==============================================================================

AuthManager::AuthManager (std::unique_ptr<AuthTransport> transportIn,
                          juce::String authFilePathIn,
                          juce::String productIn,
                          juce::String deviceNameIn,
                          juce::String pluginVersionIn)
    : juce::Thread ("SideChainAuth"),
      transport (std::move (transportIn)),
      authFilePath (std::move (authFilePathIn)),
      product (std::move (productIn)),
      deviceName (std::move (deviceNameIn)),
      pluginVersion (std::move (pluginVersionIn))
{
    jassert (transport != nullptr);
}

AuthManager::~AuthManager()
{
    stopThread (4000);
}

AuthManager::State AuthManager::getState() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return state;
}

juce::String AuthManager::getDisplayName() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return displayName;
}

bool AuthManager::isSubscribed() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return subscribed;
}

juce::String AuthManager::getUserCode() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return userCode;
}

juce::String AuthManager::getVerificationUrl() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return verificationUrl;
}

juce::String AuthManager::getLastError() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return lastError;
}

bool AuthManager::loadPersistedToken()
{
    juce::File file (authFilePath);
    if (! file.existsAsFile())
        return false;

    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (parsed.isVoid() || ! parsed.isObject())
        return false;

    const auto savedToken = varToString (parsed.getProperty ("token", juce::var()));
    if (savedToken.isEmpty())
        return false;

    const std::lock_guard<std::mutex> lock (mutex);
    token       = savedToken;
    displayName = varToString (parsed.getProperty ("display_name", juce::var()));
    state       = State::signedIn;
    return true;
}

void AuthManager::clearLocalToken()
{
    juce::File file (authFilePath);
    if (file.existsAsFile())
        file.deleteFile();
}

void AuthManager::setBackgroundPolling (bool enable)
{
    const std::lock_guard<std::mutex> lock (mutex);
    backgroundPolling = enable;
}

void AuthManager::startLinking()
{
    {
        const std::lock_guard<std::mutex> lock (mutex);
        lastError.clear();
        state = State::linking;
        userCode.clear();
        deviceCode.clear();
        verificationUrl.clear();
    }

    const auto startResp = transport->start (product, deviceName, pluginVersion);

    const std::lock_guard<std::mutex> lock (mutex);
    if (startResp.ok && state == State::linking)
    {
        deviceCode      = startResp.deviceCode;
        userCode        = startResp.userCode;
        verificationUrl = startResp.verificationUrlComplete.isNotEmpty()
                              ? startResp.verificationUrlComplete
                              : startResp.verificationUrl;
        if (startResp.intervalSeconds > 0)
            pollIntervalMs = startResp.intervalSeconds * 1000;
    }
    else
    {
        state     = State::error;
        lastError = "Could not start sign-in. Check your internet connection.";
    }

    // With background polling enabled (production), wake the auth thread so
    // it begins polling for approval immediately.
    if (! isThreadRunning())
        startThread();
    notify();
}

void AuthManager::cancelLinking()
{
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (state == State::linking)
        {
            deviceCode.clear();
            userCode.clear();
            verificationUrl.clear();
            state = State::signedOut;
        }
    }

    // Wake the auth thread so a live poll loop exits immediately.
    notify();
}

void AuthManager::persistTokenLocked()
{
    // Caller holds the mutex. Token is written ONLY to the auth file;
    // never logged, never placed in plugin (DAW) state.
    auto file = juce::File (authFilePath);
    file.getParentDirectory().createDirectory();

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty ("token", token);
    obj->setProperty ("display_name", displayName);
    file.replaceWithText (juce::JSON::toString (juce::var (obj)));
}

void AuthManager::applyPollResult (const PollResponse& r)
{
    const std::lock_guard<std::mutex> lock (mutex);

    if (r.status == "approved" && r.token.isNotEmpty())
    {
        token       = r.token;
        displayName = r.displayName;
        subscribed  = false;
        state       = State::signedIn;
        deviceCode.clear();
        userCode.clear();
        verificationUrl.clear();
        persistTokenLocked();
    }
    else if (r.status == "denied" || r.status == "expired" || r.status == "invalid")
    {
        deviceCode.clear();
        userCode.clear();
        verificationUrl.clear();
        state     = State::error;
        lastError = "Sign-in " + r.status + ". Generate a new code and try again.";
    }
    else if (r.status == "error")
    {
        // Transient network error: stay in linking, surface the condition.
        lastError = "Network error while waiting for approval...";
    }
    // "pending": keep waiting.
}

void AuthManager::pollOnceForTesting()
{
    juce::String code;
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (deviceCode.isEmpty())
            return;
        code = deviceCode;
    }

    applyPollResult (transport->poll (code));
}

void AuthManager::applyEntitlements (const EntitlementsResponse& e)
{
    const std::lock_guard<std::mutex> lock (mutex);

    if (e.ok)
    {
        subscribed = e.subscribed;
    }
    else if (e.authFailure)
    {
        // Server rejected the token (401: revoked/expired) -> sign out
        // locally, matching the existing plugin flow's behaviour.
        token.clear();
        displayName.clear();
        subscribed = false;
        state      = State::signedOut;
        clearLocalToken();
    }
    // Network/server error: keep the last known cached state.
}

void AuthManager::refreshEntitlementsForTesting()
{
    juce::String currentToken;
    {
        const std::lock_guard<std::mutex> lock (mutex);
        currentToken = token;
    }
    if (currentToken.isEmpty())
        return;

    applyEntitlements (transport->entitlements (currentToken));
}

void AuthManager::refreshEntitlements()
{
    // Async variant (production path): performed on the auth thread.
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (token.isEmpty())
            return;
        entitlementsRequested = true;
    }
    if (! isThreadRunning())
        startThread();
    notify();
}

void AuthManager::logout()
{
    // Fire-and-forget revocation on the auth thread; local state clears when
    // the request completes (the editor refreshes its display on a timer).
    {
        const std::lock_guard<std::mutex> lock (mutex);
        logoutRequested = true;
    }
    if (! isThreadRunning())
        startThread();
    notify();
}

void AuthManager::run()
{
    // Background worker: performs pending entitlement refresh / logout
    // requests (triggered via notify()) and - when enabled - live approval
    // polling while in the linking state. Never touches the audio thread.
    while (! threadShouldExit())
    {
        wait (-1);

        if (threadShouldExit())
            return;

        // ---- live approval polling (production linking state) ----------
        // Poll at the server-advised interval until the code is approved,
        // denied, expired or cancelled. applyPollResult() updates the shared
        // state under the mutex; the editor timer picks up the transition.
        bool keepPolling = true;
        while (keepPolling)
        {
            int interval = 5000;
            juce::String code;
            {
                const std::lock_guard<std::mutex> lock (mutex);
                if (! backgroundPolling || state != State::linking
                    || deviceCode.isEmpty() || threadShouldExit())
                    keepPolling = false;
                else
                {
                    interval = pollIntervalMs;
                    code     = deviceCode;
                }
            }

            if (! keepPolling)
                break;

            applyPollResult (transport->poll (code));

            wait (interval); // server-advised interval; notify() ends it early
        }

        if (threadShouldExit())
            return;

        juce::String currentToken;
        bool doLogout = false;
        bool doEntitlements = false;

        {
            const std::lock_guard<std::mutex> lock (mutex);
            doLogout              = logoutRequested;
            logoutRequested       = false;
            doEntitlements        = entitlementsRequested;
            entitlementsRequested = false;
            currentToken          = token;
        }

        if (doLogout)
        {
            if (currentToken.isNotEmpty())
                transport->logout (currentToken);

            const std::lock_guard<std::mutex> lock (mutex);
            token.clear();
            displayName.clear();
            subscribed = false;
            state      = State::signedOut;
            clearLocalToken();
        }
        else if (doEntitlements)
        {
            applyEntitlements (transport->entitlements (currentToken));
        }
    }
}

} // namespace sid::auth
