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
#include "LicenseGate.h"

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

    // Tolerant integer reader for the server's numeric fields (slot counts).
    // Accepts every numeric representation on purpose: a parsed JSON body gives
    // `double`, but a var built in C++ (or a future transport that fills the
    // fields without going through JSON) may hold an int - and a wrong-typed
    // field must fall back to the caller's default rather than silently read as 0.
    int varToInt (const juce::var& v, int fallback)
    {
        if (v.isVoid())
            return fallback;
        if (v.isInt() || v.isInt64() || v.isDouble() || v.isBool())
            return (int) (double) v;
        return fallback;
    }

    // Tolerant numeric reader for the persisted entitlement cache: a missing,
    // string or otherwise wrong-typed field is treated as "never verified",
    // which can only ever REMOVE trust, never add it.
    double varToDouble (const juce::var& v, double fallback)
    {
        if (v.isVoid())
            return fallback;
        if (v.isDouble() || v.isInt() || v.isInt64() || v.isBool())
            return (double) v;
        return fallback;
    }

    juce::String encodeRequest (juce::DynamicObject::Ptr req)
    {
        return juce::JSON::toString (juce::var (req));
    }
}

//==============================================================================
// activate-license response interpretation
//==============================================================================

LicenseResponse interpretLicenseResponse (int status, const juce::var& resp)
{
    LicenseResponse out;

    if (status == 200 && ! resp.isVoid())
    {
        const auto st = varToString (resp.getProperty ("status", juce::var()));
        out.message = varToString (resp.getProperty ("message", juce::var()));
        out.product = varToString (resp.getProperty ("product", juce::var()));

        // Slot bookkeeping, when the server sent it. Read BEFORE the status is
        // interpreted, because the `ok` verdict depends on it.
        const auto device = resp.getProperty ("device", juce::var());
        out.deviceActivated = varToBool (device.getProperty ("activated", juce::var()), false);
        out.deviceLimitUsed = varToInt  (device.getProperty ("used",      juce::var()), 0);
        out.deviceLimit     = varToInt  (device.getProperty ("limit",     juce::var()), 0);

        if (st == "ok")
        {
            // `ok` is only believed when the server ALSO says a device was
            // activated. A licence entitles a DEVICE (three per key), so an
            // `ok` that does not say which device it is for cannot be honoured
            // without silently ignoring that policy - an older server, a
            // truncated body or a rewriting proxy must not become a grant.
            if (out.deviceActivated)
            {
                out.status = LicenseResponse::Status::ok;
            }
            else
            {
                out.status  = LicenseResponse::Status::networkError;
                out.message = "The licence service returned an unexpected answer.";
            }
        }
        else if (st == "device_limit")
        {
            // The key is valid AND bound to this account; every activation slot
            // is in use. A statement about the licence, never about the key.
            out.status = LicenseResponse::Status::deviceLimit;
            if (out.message.isEmpty())
                out.message = "This licence is already active on the maximum number of "
                              "computers. Deactivate one in your Music-Prod account, then try again.";
        }
        else if (st == "revoked")      out.status = LicenseResponse::Status::revoked;
        else if (st == "expired")      out.status = LicenseResponse::Status::expired;
        else if (st == "invalid")      out.status = LicenseResponse::Status::invalid;
        else
        {
            // A 200 that is not one of the documented statuses is a contract
            // violation. Treat it as unverified, never as a grant.
            out.status  = LicenseResponse::Status::networkError;
            out.message = "The licence service returned an unexpected answer.";
        }

        return out;
    }

    if (status == 401)
    {
        out.authFailure = true;
        out.status      = LicenseResponse::Status::signInRequired;
        out.message     = "Sign in to Music-Prod to activate a licence key.";
        return out;
    }

    if (status == 400)
    {
        // A 400 is a server answer about the KEY only when it is the documented
        // `malformed_key`. Any other 400 - most importantly `unknown_action`
        // from a service that does not implement this action yet - is NOT a
        // verdict on the customer's key and must not be shown as one. Telling
        // somebody their valid key is "not accepted" because the backend has
        // not been deployed is exactly the false report this distinction
        // exists to prevent, so it fails closed as unverified instead.
        const auto code = varToString (resp.getProperty ("error", juce::var()));
        if (code == "malformed_key")
        {
            out.status  = LicenseResponse::Status::invalid;
            out.message = varToString (resp.getProperty ("message", juce::var()));
            if (out.message.isEmpty())
                out.message = "That does not look like a Music-Prod licence key.";
        }
        else if (code == "device_id_required")
        {
            // The server refused because this build did not say WHICH computer
            // is being activated. That is a verdict on the BUILD, not on the
            // customer's key, so it is reported as "could not be checked" rather
            // than as a rejected key.
            out.status  = LicenseResponse::Status::networkError;
            out.message = varToString (resp.getProperty ("message", juce::var()));
            if (out.message.isEmpty())
                out.message = "This version of PumpCurve cannot report which computer it is "
                              "running on. Update PumpCurve, then enter your key again.";
        }
        else
        {
            out.status  = LicenseResponse::Status::networkError;
            out.message = "The licence service did not accept the request "
                          "(was it deployed?). Your key was not checked.";
        }

        return out;
    }

    // No answer, or an error we cannot interpret: nothing was verified. This is
    // NOT a rejection and must never be reported as one.
    out.status  = LicenseResponse::Status::networkError;
    out.message = "Could not reach the licence service. Your key was not checked.";
    return out;
}

//==============================================================================
// entitlements response interpretation
//==============================================================================

EntitlementsResponse interpretEntitlementsResponse (int status, const juce::var& resp)
{
    EntitlementsResponse out;

    if (status == 200 && ! resp.isVoid())
    {
        out.ok         = varToBool (resp.getProperty ("ok", juce::var()), false);
        out.subscribed = varToBool (resp.getProperty ("subscribed", juce::var()), false);

        // The product-scoped answer. Sent by the current server; absent from an
        // older server, in which case it defaults to false and the plugin
        // simply stays in demo mode - it never infers entitlement locally.
        out.productEntitled            = varToBool (resp.getProperty ("plugin_entitled", juce::var()), false);
        out.product                    = varToString (resp.getProperty ("product", juce::var()));
        out.subscriptionActive         = varToBool (resp.getProperty ("subscribed", juce::var()), false);
        out.subscriptionCoversProduct  = varToBool (resp.getProperty ("subscription", juce::var())
                                                        .getProperty ("covers_product", juce::var()), false);

        const auto license = resp.getProperty ("license", juce::var());
        out.licenseActive    = varToBool (license.getProperty ("active", juce::var()), false);
        out.licenseStatus    = varToString (license.getProperty ("status", juce::var()));
        out.licenseExpiresAt = varToString (license.getProperty ("expires_at", juce::var()));

        // The two halves of the DEVICE-bound route. The server reports them
        // SEPARATELY, and they are the only thing that can tell a computer the
        // owner released apart from a revoked licence: `license.active` is false
        // in both cases, and only the account half says which one happened.
        // Without them a deactivated computer is reported to the customer as a
        // licence that "is no longer active on your account" - a false and
        // alarming statement about a key that is still perfectly valid.
        out.licenseAccountActive = varToBool (license.getProperty ("account_active", juce::var()), false);
        out.licenseDeviceActive  = varToBool (license.getProperty ("device_active",  juce::var()), false);

        // Slot bookkeeping, for the "active on N of 3 computers" line.
        out.devicesUsed = varToInt (license.getProperty ("devices_used", juce::var()), 0);
        out.deviceLimit = varToInt (license.getProperty ("device_limit", juce::var()), 0);
    }
    else if (status == 401)
    {
        out.authFailure = true; // token revoked/expired -> caller signs out
    }
    // Other statuses / network errors: neither ok nor authFailure.

    return out;
}

//==============================================================================
// HttpAuthTransport
//==============================================================================

HttpAuthTransport::HttpAuthTransport (juce::String endpointUrl)
    : endpoint (std::move (endpointUrl)) {}

void HttpAuthTransport::cancelPendingRequests()
{
    const std::lock_guard<std::mutex> lock (streamMutex);
    requestsCancelled = true;
    if (activeStream != nullptr)
        activeStream->cancel();
}

juce::var HttpAuthTransport::postJson (const juce::String& body, int* statusCode)
{
    if (statusCode != nullptr)
        *statusCode = 0;

    // JUCE 6.1.3 WebInputStream: POST body via URL; chained header/timeout.
    juce::WebInputStream stream (juce::URL (endpoint).withPOSTData (body), true);
    stream.withExtraHeaders ("Content-Type: application/json")
          .withConnectionTimeout (10000);

    // Registered so cancelPendingRequests() can abort it from another thread.
    {
        const std::lock_guard<std::mutex> lock (streamMutex);
        if (requestsCancelled)
            return juce::var();
        activeStream = &stream;
    }
    struct Unregister
    {
        HttpAuthTransport& t;
        ~Unregister() { const std::lock_guard<std::mutex> lock (t.streamMutex); t.activeStream = nullptr; }
    } unregister { *this };

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

    // The device identity travels with the pairing code into the token, so a
    // later entitlement check knows WHICH computer is asking without the plugin
    // repeating it on every request. Omitted when unknown (an uncached install
    // whose app-data folder is not writable); the server then stores no device id
    // and `activate-license` fills it in.
    if (deviceId.isNotEmpty())
        req->setProperty ("device_id", deviceId);
    if (deviceLabel.isNotEmpty())
        req->setProperty ("device_label", deviceLabel);
    if (platform.isNotEmpty())
        req->setProperty ("platform", platform);

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
    juce::DynamicObject::Ptr req = new juce::DynamicObject();
    req->setProperty ("action", "entitlements");
    req->setProperty ("token", tokenIn);

    int status = 0;
    const auto resp = postJson (encodeRequest (req), &status);

    // The mapping itself lives in interpretEntitlementsResponse, so the deployed
    // contract's field names are exercised by a test rather than only by a live
    // server: the mock transports used elsewhere build the response struct
    // directly and therefore could never catch a field this client forgot to
    // read.
    return interpretEntitlementsResponse (status, resp);
}

LicenseResponse HttpAuthTransport::activateLicense (const juce::String& tokenIn,
                                                    const juce::String& licenceKeyIn)
{
    LicenseResponse out;

    juce::DynamicObject::Ptr req = new juce::DynamicObject();
    req->setProperty ("action", "activate-license");
    req->setProperty ("token", tokenIn);
    req->setProperty ("license_key", licenceKeyIn);
    // NOTE: the product is deliberately NOT sent. The server uses the product the
    // token was issued for, so this build cannot ask for another product's
    // licence to be activated.
    //
    // The device IS sent: this request consumes one of the licence's three
    // activation slots, and the server must be told which computer is taking it.
    // Without it the server refuses (`device_id_required`) rather than inventing
    // a device, which is why the id is resolved before this call is made.
    if (deviceId.isNotEmpty())
        req->setProperty ("device_id", deviceId);
    if (deviceLabel.isNotEmpty())
        req->setProperty ("device_label", deviceLabel);
    if (platform.isNotEmpty())
        req->setProperty ("platform", platform);

    int status = 0;
    const auto resp = postJson (encodeRequest (req), &status);

    out = interpretLicenseResponse (status, resp);
    return out;
}

void HttpAuthTransport::setDeviceIdentity (const juce::String& deviceIdIn,
                                           const juce::String& deviceLabelIn,
                                           const juce::String& platformIn)
{
    deviceId    = deviceIdIn;
    deviceLabel = deviceLabelIn;
    platform    = platformIn;
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
    // Abort a request blocked on the network first: stopThread's 4 s grace is
    // shorter than the 10 s connect timeout, and JUCE would otherwise kill the
    // thread mid-request (a hang or crash risk when a project closes offline).
    signalThreadShouldExit();
    notify();
    transport->cancelPendingRequests();
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

//==============================================================================
// Device identity (see the header for the design rationale)
//==============================================================================

juce::String AuthManager::getDeviceId()
{
    ensureDeviceIdentity();

    const std::lock_guard<std::mutex> lock (mutex);
    return deviceId;
}

juce::String AuthManager::getDeviceLabel() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return deviceLabel;
}

void AuthManager::ensureDeviceIdentity()
{
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (deviceId.isNotEmpty())
            return;
    }

    // Filesystem work happens OUTSIDE the state lock: this can be reached from
    // the message thread (a click on Activate) and must never block the editor's
    // paint. Nothing here touches the audio thread, and nothing here is a
    // network request, so plugin scanning stays fast.
    const auto path = juce::File (authFilePath).getSiblingFile ("device_id.txt");
    juce::String id;

    if (path.existsAsFile())
        id = path.loadFileAsString().trim().toLowerCase()
                 .retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789-");

    // Shorter than the documented minimum means the file is missing, empty or
    // damaged; minting a new id is the only safe answer, because reusing a
    // partial id could collide with another computer and hand it this slot.
    if (id.length() < 16)
    {
        id = juce::Uuid().toDashedString().toLowerCase();
        path.getParentDirectory().createDirectory();
        path.replaceWithText (id);
    }

    const auto computerName = juce::SystemStats::getComputerName().trim();
    const auto osName       = juce::SystemStats::getOperatingSystemName().trim();

    juce::String publishedId, publishedLabel, publishedOs;

    {
        const std::lock_guard<std::mutex> lock (mutex);

        if (deviceId.isEmpty())
        {
            deviceId    = id;
            deviceLabel = (computerName.isNotEmpty() ? computerName : juce::String ("Unnamed computer"))
                              .substring (0, 80);
            platformName = osName.substring (0, 60);
        }

        if (! deviceIdentityPublished)
        {
            deviceIdentityPublished = true;
            publishedId    = deviceId;
            publishedLabel = deviceLabel;
            publishedOs    = platformName;
        }
    }

    // Called with the lock RELEASED: the transport is not ours to call while
    // holding the state mutex.
    if (publishedId.isNotEmpty())
        transport->setDeviceIdentity (publishedId, publishedLabel, publishedOs);
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

    // Offline entitlement policy (see LicenseGate.h): the cached entitlement
    // answer is honoured ONLY when the file records that the SERVER verified it
    // and only while that answer is still fresh. Missing, malformed, or stale
    // cache data can therefore only remove trust, never add it.
    //
    // Both parts of the answer are cached: the raw Music-Prod+ membership (so
    // the account area says the truth offline) and the PRODUCT-SCOPED
    // entitlement (so a subscriber, or a licence holder, keeps working offline
    // within the documented window). Neither is a local guess - both were
    // written from a server answer, with the moment the server gave it.
    const bool   cachedSubscribed  = varToBool (parsed.getProperty ("subscribed", juce::var()), false);
    const bool   cachedEntitled    = varToBool (parsed.getProperty ("product_entitled", juce::var()), false);
    const bool   cachedLicensed    = varToBool (parsed.getProperty ("license_active", juce::var()), false);
    const double verifiedAt      = varToDouble (parsed.getProperty ("entitlements_verified_at_ms", juce::var()), 0.0);
    const double nowMs           = (double) juce::Time::currentTimeMillis();
    const bool   cacheIsFresh    = sid::license::cachedEntitlementIsFresh (verifiedAt, nowMs);

    // The LICENCE route has its own, shorter window, because it is DEVICE-bound:
    // a computer deactivated in the account area must stop working within days
    // even if it never reaches the network again. The overall `product_entitled`
    // flag is therefore only honoured while BOTH the general window and - when a
    // licence was part of that answer - the licence window are still open.
    const double licenseVerifiedAt = varToDouble (
                                        parsed.getProperty ("license_verified_at_ms", juce::var()), 0.0);
    const bool   licenceIsFresh    = sid::license::cachedLicenseActivationIsFresh (licenseVerifiedAt, nowMs);
    const bool   entitledIsFresh   = cachedEntitled && cacheIsFresh
                                        && (! cachedLicensed || licenceIsFresh);

    {
        const std::lock_guard<std::mutex> lock (mutex);
        token       = savedToken;
        displayName = varToString (parsed.getProperty ("display_name", juce::var()));
        state       = State::signedIn;
        subscribed               = cachedSubscribed && cacheIsFresh;
        productEntitled          = entitledIsFresh;
        entitlementsVerifiedAtMs = (subscribed || productEntitled) ? verifiedAt : 0.0;
        licenseVerifiedAtMs      = (cachedLicensed && licenceIsFresh) ? licenseVerifiedAt : 0.0;

        // A Music-Prod+ full version that the cache WOULD have held, had it not
        // aged past its window, is reported as unconfirmed rather than as a
        // plain "not active": the account may well still be a member.
        membershipLapse = (cachedSubscribed && cachedEntitled && ! entitledIsFresh
                           && ! (cachedLicensed && licenceIsFresh))
                              ? MembershipLapse::unconfirmed
                              : MembershipLapse::none;

        // A licence the SERVER confirmed for this ACCOUNT AND THIS DEVICE may be
        // reflected in the UI after a restart, within its own window, so the
        // user sees which route is holding the full version. It is only ever set
        // from a server answer - never from the user's own typing, never from
        // DAW state, and never restored once its window has closed.
        if (cachedLicensed && licenceIsFresh)
            licenseStatus = LicenseStatus::active;
        else if (cachedLicensed)
            licenseStatus = LicenseStatus::notChecked;   // stale: re-verified on launch
    }

    fireEntitlementChanged();
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

    // The device identity is resolved (and handed to the transport) BEFORE the
    // pairing starts, so the device id travels with the code into the token.
    ensureDeviceIdentity();

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
    // Cached subscription answer plus the time the SERVER verified it. Only
    // written from a real server answer (see applyEntitlements), and only
    // trusted while fresh (see loadPersistedToken).
    obj->setProperty ("subscribed", subscribed);
    obj->setProperty ("product_entitled", productEntitled);
    obj->setProperty ("license_active", licenseStatus == LicenseStatus::active);
    obj->setProperty ("entitlements_verified_at_ms", entitlementsVerifiedAtMs);
    // The licence route's own timestamp, so the shorter device-bound window can
    // be applied to it (see loadPersistedToken). Written only from a server
    // answer, exactly like the rest of the cache.
    obj->setProperty ("license_verified_at_ms", licenseVerifiedAtMs);
    file.replaceWithText (juce::JSON::toString (juce::var (obj)));
}

void AuthManager::applyPollResult (const PollResponse& r)
{
    {
        const std::lock_guard<std::mutex> lock (mutex);

        if (r.status == "approved" && r.token.isNotEmpty())
        {
            token       = r.token;
            displayName = r.displayName;
            subscribed  = false;
            productEntitled = false;
            // A new token has not been entitlement-checked yet; the cache must
            // not keep a previous answer alive through the sign-in change.
            entitlementsVerifiedAtMs = 0.0;
            membershipLapse = MembershipLapse::none;   // a new account, nothing withdrawn yet
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

    // No entitlement can change from a poll alone (a fresh token is always
    // unverified), so this can only publish "still demo".
    fireEntitlementChanged();
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
    {
        const std::lock_guard<std::mutex> lock (mutex);

        // The check that produced this answer is over - whatever the answer was.
        // A failure must clear it too, or the UI would sit on "CHECKING..." for
        // ever and the debounce would refuse to try again.
        entitlementCheckInFlight = false;

        // Only a missing answer counts as a failed check: a 200 (any verdict)
        // and a 401 (the account is gone) are both real answers.
        lastEntitlementCheckFailed = ! e.ok && ! e.authFailure;

        if (e.ok)
        {
            // Whether Music-Prod+ was what held the full version before this
            // answer - so an answer that withdraws it can be named as such.
            const bool membershipHeldFull = state == State::signedIn && subscribed && productEntitled;

            // The PRODUCT-SCOPED answer, not the raw membership. The server has
            // already combined "subscription active" with "subscription covers
            // this product" and "a licence key is active for this product".
            // Caching it is the same offline-grace policy as before; the value
            // is simply the correct one for THIS plugin now.
            subscribed      = e.subscribed;      // raw membership (what the account area shows)
            answeredProduct = e.product;

            // The server answers for the product the TOKEN was issued for. An
            // answer about any other product (a token copied from another
            // Music-Prod plugin, or one minted before tokens recorded their
            // product) says nothing about PumpCurve and can never unlock it.
            const bool answerIsForThisProduct = (e.product == product);
            productEntitled = answerIsForThisProduct && e.productEntitled; // THE gate answer

            // The licence verdict is authoritative TOO, and it is scoped to this
            // DEVICE: `licenseActive` means the account holds the licence AND
            // this computer holds one of its activation slots. A key activated on
            // another machine, a deactivated computer, or a revoked licence is
            // all reflected here rather than only in the issuing instance.
            if (! answerIsForThisProduct)
            {
                // A licence the plugin believed in cannot be confirmed through a
                // sign-in made for another product: withdraw it until a
                // PumpCurve sign-in confirms it again.
                if (licenseStatus == LicenseStatus::active)
                {
                    licenseStatus  = LicenseStatus::signInRequired;
                    licenseMessage = "Sign in again from PumpCurve to verify your licence.";
                }
            }
            else if (e.licenseActive)
            {
                licenseStatus  = LicenseStatus::active;
                licenseMessage = "This computer is activated with your PumpCurve licence.";
            }
            else if (e.licenseAccountActive && ! e.licenseDeviceActive)
            {
                // The key is FINE and the account owns it; this computer holds no
                // slot. Usually that means the owner deactivated it, activated
                // other computers instead, or the key was bound to the account
                // without ever being entered on this machine - so the message
                // says exactly that instead of implying a bad key.
                licenseStatus  = LicenseStatus::deviceInactive;
                licenseMessage = e.devicesUsed > 0 && e.deviceLimit > 0
                                     ? "Your licence is active on " + juce::String (e.devicesUsed)
                                           + " of " + juce::String (e.deviceLimit)
                                           + " computers, but not on this one."
                                     : "Your licence is active on your Music-Prod account but not "
                                       "activated on this computer.";
            }
            else if (e.licenseStatus == "revoked" || e.licenseStatus == "expired")
            {
                licenseStatus = LicenseStatus::revoked;
                licenseMessage = e.licenseStatus == "expired"
                                     ? "This licence has expired."
                                     : "This licence has been revoked.";
            }
            else if (licenseStatus == LicenseStatus::active
                     || licenseStatus == LicenseStatus::deviceInactive)
            {
                // We were told this instance was licensed and the server no
                // longer confirms it: believe the server, never the local copy.
                licenseStatus  = LicenseStatus::revoked;
                licenseMessage = "This licence is no longer active on your account.";
            }

            // A full version that Music-Prod+ held (or held until the offline
            // window closed) and that the server now withdraws BECAUSE there is
            // no membership any more is an ENDED membership: the INFO page says
            // so. A membership that is still active but not confirmed for
            // PumpCurve is a different state with its own guidance (see
            // subscriptionNotValidForProduct). Any grant clears it.
            if (productEntitled || isFullEntitlementLocked() || subscribed)
                membershipLapse = MembershipLapse::none;
            else if (membershipHeldFull || membershipLapse != MembershipLapse::none)
                membershipLapse = MembershipLapse::ended;

            // Only a server answer may refresh the cache, and it records the
            // moment the SERVER answered - never a local guess. The licence route
            // gets its own stamp because it is cached for a shorter window.
            const double nowMs = (double) juce::Time::currentTimeMillis();
            entitlementsVerifiedAtMs = nowMs;
            if (answerIsForThisProduct && (e.licenseActive || e.licenseAccountActive))
                licenseVerifiedAtMs = nowMs;
            persistTokenLocked();
        }
        else if (e.authFailure)
        {
            // Server rejected the token (401: revoked/expired) -> sign out
            // locally, matching the existing plugin flow's behaviour.
            token.clear();
            displayName.clear();
            // The request in flight is over (this answer WAS the answer), and the
            // account it was asked about is gone with the token.
            entitlementCheckInFlight = false;
            // The ACCOUNT entitlement is gone with the token; a licence that was
            // granted through the account cannot be re-verified without it.
            if (licenseStatus == LicenseStatus::active)
            {
                licenseStatus  = LicenseStatus::signInRequired;
                licenseMessage = "Sign in again to re-verify your licence.";
            }
            subscribed = false;
            productEntitled = false;
            entitlementsVerifiedAtMs = 0.0;
            membershipLapse = MembershipLapse::none;
            state      = State::signedOut;
            clearLocalToken();
        }
        else
        {
            // Network/server error: keep the last known cached state while it is
            // inside its offline window - a verified subscriber is never
            // downgraded by one failed request - and withdraw it once the window
            // has closed. If nothing was ever verified, nothing is granted.
            enforceOfflineWindowsLocked ((double) juce::Time::currentTimeMillis());
        }
    }

    fireEntitlementChanged();
}

//==============================================================================
// Licence-key entitlement
//==============================================================================

bool AuthManager::isFullEntitlementLocked() const
{
    // A) the server said this PRODUCTION is entitled (a subscription that covers
    //    it, or a licence active for the account), or
    // B) this instance verified a licence key and has not been told otherwise.
    return (state == State::signedIn && productEntitled)
        || licenseStatus == LicenseStatus::active;
}

bool AuthManager::hasFullEntitlement() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return isFullEntitlementLocked();
}

bool AuthManager::fullVersionViaLicense() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return licenseStatus == LicenseStatus::active;
}

bool AuthManager::fullVersionViaSubscription() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    // The RAW membership, so naming a style "Music-Prod+" cannot be claimed by a
    // licence-only user who happens to be signed in.
    return state == State::signedIn && subscribed && productEntitled;
}

AuthManager::MembershipLapse AuthManager::getMembershipLapse() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return membershipLapse;
}

bool AuthManager::enforceOfflineWindowsLocked (double nowMs)
{
    // The launch rule (loadPersistedToken) applied to a running instance: each
    // grant is honoured only while the server's answer behind it is inside its
    // window. Only ever removes trust; a later server answer restores it.
    const bool membershipHeldFull = state == State::signedIn && subscribed && productEntitled;
    const bool licensed           = licenseStatus == LicenseStatus::active;
    const bool answerExpired      = sid::license::runningGrantExpired (
                                        entitlementsVerifiedAtMs, nowMs, sid::license::kEntitlementGraceSeconds);
    const bool licenceExpired     = sid::license::runningGrantExpired (
                                        licenseVerifiedAtMs, nowMs, sid::license::kLicenseActivationGraceSeconds);
    bool changed = false;

    // A licence - verified at launch, in this session or through the account -
    // is DEVICE-bound and has the shorter window.
    if (licensed && licenceExpired)
    {
        licenseStatus  = LicenseStatus::notChecked;
        licenseMessage = "PumpCurve could not re-check your licence with Music-Prod for 7 days, "
                         "so it is back in demo mode. Connect to the internet and it is checked "
                         "again automatically.";
        changed = true;
    }

    // The product answer lapses with its own window, and - exactly as at launch
    // - with the licence window when a licence was part of that answer.
    if (productEntitled && (answerExpired || (licensed && licenceExpired)))
    {
        productEntitled = false;
        changed = true;
    }

    if (subscribed && answerExpired)
    {
        subscribed = false;
        changed = true;
    }

    if (membershipHeldFull && ! isFullEntitlementLocked())
        membershipLapse = MembershipLapse::unconfirmed;

    return changed;
}

void AuthManager::enforceOfflineWindows()
{
    bool withdrawn = false;
    {
        const std::lock_guard<std::mutex> lock (mutex);
        withdrawn = enforceOfflineWindowsLocked ((double) juce::Time::currentTimeMillis());
    }
    if (withdrawn)
        fireEntitlementChanged();
}

void AuthManager::backdateVerificationForTesting (double entitlementMillisecondsAgo,
                                                  double licenceMillisecondsAgo)
{
    const std::lock_guard<std::mutex> lock (mutex);
    const double nowMs = (double) juce::Time::currentTimeMillis();
    entitlementsVerifiedAtMs = nowMs - juce::jmax (0.0, entitlementMillisecondsAgo);
    if (licenceMillisecondsAgo >= 0.0)
        licenseVerifiedAtMs = nowMs - licenceMillisecondsAgo;
}

AuthManager::LicenseStatus AuthManager::getLicenseStatus() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return licenseStatus;
}

juce::String AuthManager::getLicenseMessage() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return licenseMessage;
}

void AuthManager::fireEntitlementChanged()
{
    bool full = false;
    bool changed = false;

    {
        const std::lock_guard<std::mutex> lock (mutex);
        full    = isFullEntitlementLocked();
        changed = (full != lastPublishedFull);
        lastPublishedFull = full;
    }

    // Called OUTSIDE the lock. The consumer (the processor) performs a single
    // atomic store, so it can never re-enter this class while locked.
    if (changed && onEntitlementChanged)
        onEntitlementChanged (full);
}

void AuthManager::applyLicenseResult (const LicenseResponse& answer)
{
    // An activation the server made for another product (the token's) is not a
    // PumpCurve activation: treated like any other unexpected answer.
    LicenseResponse r = answer;
    if (r.status == LicenseResponse::Status::ok && r.product != product)
    {
        r.status  = LicenseResponse::Status::networkError;
        r.message = "The licence service answered for another product. Log out, sign in "
                    "again from PumpCurve, then activate the key.";
    }

    {
        const std::lock_guard<std::mutex> lock (mutex);

        // A rejected TOKEN (401) ends the sign-in, exactly as it does for the
        // entitlement check: the account area must not keep claiming a session
        // the server no longer accepts.
        if (r.authFailure)
        {
            token.clear();
            displayName.clear();
            subscribed = false;
            productEntitled = false;
            entitlementsVerifiedAtMs = 0.0;
            entitlementCheckInFlight = false;
            membershipLapse = MembershipLapse::none;
            state = State::signedOut;
            clearLocalToken();
        }

        switch (r.status)
        {
            case LicenseResponse::Status::ok:             licenseStatus = LicenseStatus::active;         break;
            case LicenseResponse::Status::invalid:        licenseStatus = LicenseStatus::invalid;        break;
            case LicenseResponse::Status::revoked:
            case LicenseResponse::Status::expired:        licenseStatus = LicenseStatus::revoked;        break;
            case LicenseResponse::Status::deviceLimit:    licenseStatus = LicenseStatus::deviceLimit;    break;
            case LicenseResponse::Status::networkError:   licenseStatus = LicenseStatus::networkError;  break;
            case LicenseResponse::Status::unavailable:    licenseStatus = LicenseStatus::unavailable;    break;
            case LicenseResponse::Status::signInRequired: licenseStatus = LicenseStatus::signInRequired; break;
        }

        // A verified activation starts the LICENCE route's own offline window.
        // Nothing else does: a rejection must not refresh a grant's freshness,
        // and a transport failure has verified nothing at all.
        if (r.status == LicenseResponse::Status::ok)
            licenseVerifiedAtMs = (double) juce::Time::currentTimeMillis();

        // The INFO page must say something TRUE about the current state, so the
        // message is always written - never left over from the previous attempt,
        // and never blank just because a transport supplied no text. A message
        // from the server wins when there is one: it is more specific ("already
        // activated on another account") and it is the server's own wording.
        if (r.message.isNotEmpty())
            licenseMessage = r.message;
        else
            switch (r.status)
            {
                case LicenseResponse::Status::ok:
                    licenseMessage = "Licence activated.";
                    break;
                case LicenseResponse::Status::invalid:
                    licenseMessage = "That licence key was not accepted.";
                    break;
                case LicenseResponse::Status::revoked:
                    licenseMessage = "This licence has been revoked.";
                    break;
                case LicenseResponse::Status::expired:
                    licenseMessage = "This licence has expired.";
                    break;
                case LicenseResponse::Status::deviceLimit:
                    // The key is valid AND bound to this account: every activation
                    // slot is in use. Phrased as a next step, never as a refusal.
                    licenseMessage = "This licence is already active on the maximum number of "
                                     "computers. Deactivate one in your Music-Prod account "
                                     "(Profile), then try again.";
                    break;
                case LicenseResponse::Status::unavailable:
                    licenseMessage = "Licence activation is not available from this service.";
                    break;
                case LicenseResponse::Status::networkError:
                    // Deliberately NOT a rejection: the key was never judged.
                    licenseMessage = "Could not reach the licence service. Your key was not checked.";
                    break;
                case LicenseResponse::Status::signInRequired:
                    licenseMessage = "Sign in to Music-Prod to activate a licence key.";
                    break;
            }
    }

    fireEntitlementChanged();
}

/** A key is bound to an ACCOUNT: with no token there is nothing to bind it to. */
bool AuthManager::licenseActivationPossibleLocked() const
{
    return token.isNotEmpty();
}

void AuthManager::activateLicenseForTesting (const juce::String& licenceKey)
{
    juce::String currentToken;
    {
        const std::lock_guard<std::mutex> lock (mutex);
        currentToken = token;
    }

    applyLicenseResult (transport->activateLicense (currentToken, licenceKey));
}

void AuthManager::activateLicense (const juce::String& licenceKey)
{
    bool rejectedForNoAccount = false;

    {
        const std::lock_guard<std::mutex> lock (mutex);

        if (! licenseActivationPossibleLocked())
        {
            // Refuse locally instead of sending a request that cannot succeed,
            // and never report it as a rejection of the key.
            licenseStatus     = LicenseStatus::signInRequired;
            licenseMessage    = "Sign in to Music-Prod to activate a licence key.";
            pendingLicenseKey.clear();
            licenseRequested  = false;
            rejectedForNoAccount = true;
        }
        else
        {
            pendingLicenseKey = licenceKey;
            licenseStatus     = LicenseStatus::validating;
            licenseMessage.clear();
            licenseRequested  = true;
        }
    }

    // Resolve (and publish) this installation's device id OUTSIDE the lock and
    // before the request is issued: this request takes one of the licence's three
    // device slots, and the server has to be told which computer is taking it.
    if (! rejectedForNoAccount)
        ensureDeviceIdentity();

    if (rejectedForNoAccount)
    {
        fireEntitlementChanged();
        return;
    }

    if (! isThreadRunning())
        startThread();
    notify();
}

void AuthManager::refreshEntitlementsForTesting()
{
    juce::String currentToken;
    {
        const std::lock_guard<std::mutex> lock (mutex);
        currentToken = token;
        if (currentToken.isEmpty())
            return;

        // Recorded the same way the production paths record it, so a test can
        // observe the debounce rule rather than only the request itself.
        entitlementCheckInFlight  = true;
        lastEntitlementAttemptMs  = (double) juce::Time::currentTimeMillis();
    }

    applyEntitlements (transport->entitlements (currentToken));
}

void AuthManager::backdateEntitlementAttemptForTesting (double millisecondsAgo)
{
    const std::lock_guard<std::mutex> lock (mutex);
    lastEntitlementAttemptMs = (double) juce::Time::currentTimeMillis()
                               - juce::jmax (0.0, millisecondsAgo);
}

void AuthManager::refreshEntitlements()
{
    // Async, FORCED variant (production path): performed on the auth thread.
    // Never gated by the staleness rule or by the lifecycle debounce - see the
    // header. Queued unconditionally (while signed in) so the customer's own
    // "check now" always produces a real request.
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (token.isEmpty())
            return;
        entitlementsRequested     = true;
        entitlementCheckInFlight  = true;
        lastEntitlementAttemptMs  = (double) juce::Time::currentTimeMillis();
    }
    if (! isThreadRunning())
        startThread();
    notify();
}

void AuthManager::refreshEntitlementsIfStale (double minIntervalSeconds)
{
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (token.isEmpty())
            return;

        // A second request while one is running cannot tell us anything new, and
        // a lifecycle event that repeats (bringing the window forward, opening
        // the page again) must not become a request per event.
        if (entitlementCheckInFlight)
            return;

        if (! sid::license::entitlementAutoRefreshDue (lastEntitlementAttemptMs,
                                                      (double) juce::Time::currentTimeMillis(),
                                                      minIntervalSeconds))
            return;

        entitlementsRequested     = true;
        entitlementCheckInFlight  = true;
        lastEntitlementAttemptMs  = (double) juce::Time::currentTimeMillis();
    }
    if (! isThreadRunning())
        startThread();
    notify();
}

bool AuthManager::isEntitlementCheckInFlight() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return entitlementCheckInFlight;
}

bool AuthManager::subscriptionNotValidForProduct() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return state == State::signedIn && subscribed && ! productEntitled;
}

bool AuthManager::tokenIsForAnotherProduct() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return state == State::signedIn && answeredProduct.isNotEmpty()
           && answeredProduct != product;
}

bool AuthManager::didLastEntitlementCheckFail() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return lastEntitlementCheckFailed && token.isNotEmpty();
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
    // A BOUNDED wait rather than an indefinite one, so a session that stays open
    // for days can still re-check its entitlement (see
    // sid::license::entitlementRefreshDue below). Waking once a minute costs
    // nothing: no request is made unless the cached answer has aged past the
    // interval, and a licence is DEVICE-bound, so a deactivation must be able to
    // land without the user restarting their DAW.
    constexpr int kRunWakeIntervalMs = 60 * 1000;

    while (! threadShouldExit())
    {
        wait (kRunWakeIntervalMs);

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
        bool doLicense = false;
        bool withdrawn = false;
        juce::String licenseKey;

        {
            const std::lock_guard<std::mutex> lock (mutex);

            // Every wake (at least once a minute) re-applies the offline windows,
            // so an instance that stays open without reaching Music-Prod loses a
            // grant when its window closes - not only at the next launch.
            withdrawn = enforceOfflineWindowsLocked ((double) juce::Time::currentTimeMillis());

            doLogout              = logoutRequested;
            logoutRequested       = false;
            doEntitlements        = entitlementsRequested;
            entitlementsRequested = false;
            doLicense             = licenseRequested;
            licenseRequested      = false;
            licenseKey            = pendingLicenseKey;
            currentToken          = token;

            // The periodic re-check: only for a signed-in instance, only when
            // the cached answer has actually aged out, and never while the user
            // is mid-activation (that path refreshes on its own).
            if (! doLogout && ! doEntitlements && ! doLicense && currentToken.isNotEmpty()
                && state == State::signedIn
                && sid::license::entitlementRefreshDue (entitlementsVerifiedAtMs,
                                                        (double) juce::Time::currentTimeMillis()))
                doEntitlements = true;
        }

        if (withdrawn)
            fireEntitlementChanged();

        if (doLogout)
        {
            if (currentToken.isNotEmpty())
                transport->logout (currentToken);

            {
                const std::lock_guard<std::mutex> lock (mutex);
                token.clear();
                displayName.clear();
                subscribed = false;
                productEntitled = false;
                entitlementsVerifiedAtMs = 0.0;
                entitlementCheckInFlight = false;   // nothing is being checked now
                membershipLapse = MembershipLapse::none;
                state      = State::signedOut;
                clearLocalToken();
            }

            // Signing out withdraws the subscription route. The licence-key
            // route is an independent entitlement and is left alone.
            fireEntitlementChanged();
        }
        else if (doEntitlements && currentToken.isNotEmpty())
        {
            applyEntitlements (transport->entitlements (currentToken));
        }

        // Its own branch: a licence request that arrives together with an
        // entitlement refresh must still run (it used to be dropped, leaving
        // the key field stuck on "validating"). After a logout the key has no
        // account to bind to, so it is answered as such instead.
        if (doLicense && doLogout)
        {
            LicenseResponse noAccount;
            noAccount.status = LicenseResponse::Status::signInRequired;
            applyLicenseResult (noAccount);
        }
        else if (doLicense)
        {
            {
                const std::lock_guard<std::mutex> lock (mutex);
                currentToken = token;   // the refresh above may have signed out
            }
            applyLicenseResult (transport->activateLicense (currentToken, licenseKey));

            // Activation binds the key to the ACCOUNT, so the entitlement is now
            // an account entitlement. Confirm it with the server straight away
            // instead of trusting the activation answer alone: the refreshed,
            // cached answer is what keeps the full version working offline later,
            // and it is also what refutes a grant the server does not confirm.
            if (hasFullEntitlement() && currentToken.isNotEmpty())
                applyEntitlements (transport->entitlements (currentToken));
        }
    }
}

} // namespace sid::auth
