/*
    SideChain - INFO page implementation (Phase 8).

    Status display rules (all textual, no colour-only indicators):
      SIGNED OUT              - no token
      LINKING - CODE XXXX-XXXX- waiting for user approval (device code)
      SIGNED IN               - token present
      SIGNED IN (cached)      - token present, last refresh failed
      AUTHENTICATION ERROR    - last operation failed
      MUSIC-PROD+ ACTIVE / MUSIC-PROD+ NOT ACTIVE - entitlement state

    Version mode (0.5.0):
      FULL VERSION            - a server-verified entitlement is active
      DEMO - MM:SS REMAINING  - 15-minute session for this plugin instance
      DEMO EXPIRED            - processing has stopped; restart the instance
      LICENCE <state>         - licence-key route: validating / active /
                                not accepted / expired or revoked / server
                                unreachable / not available (no endpoint)

    Phase 8B: the backend resolves the product's own verification page
    (SideChain -> /plugin/link, product-neutral Music-Prod page), so login
    auto-opens the browser with the device code pre-filled — the standard
    device-code UX. The URL comes from the server response; the page is
    Music-Prod/SideChain branded (never another product's).
*/

#include "InfoPage.h"
#include "Branding.h"
#include "PumpCurveTheme.h"

namespace
{
    // Palette aliases (the single source of truth is sid::theme).
    constexpr uint32_t kBackground   = sid::theme::kBase;
    constexpr uint32_t kTextPrimary  = sid::theme::kText;
    constexpr uint32_t kTextSubtle   = sid::theme::kTextMuted;
    constexpr uint32_t kAccent       = sid::theme::kAccent;
    constexpr uint32_t kControlBg    = sid::theme::kControl;
    constexpr uint32_t kControlEdge  = sid::theme::kEdge;

    using Tone = sid::theme::Tone;

    void setTone (juce::Label& label, Tone tone)
    {
        label.setColour (juce::Label::textColourId, sid::theme::toneColour (tone));
    }

    // Section heading: a quiet tracked caption. Mint is kept for states and
    // actions, so a heading never competes with the answer under it.
    void styleHeading (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (sid::theme::captionFont (10.5f));
        label.setColour (juce::Label::textColourId, juce::Colour (kTextSubtle));
        label.setJustificationType (juce::Justification::centredLeft);
        label.setBorderSize ({});
    }

    // Body copy that WRAPS onto as many lines as its bounds allow instead of
    // being squeezed sideways into one unreadable line.
    void styleParagraph (juce::Label& label, float size = 12.5f)
    {
        label.setFont (sid::theme::font (size));
        label.setColour (juce::Label::textColourId, juce::Colour (kTextSubtle));
        label.setJustificationType (juce::Justification::topLeft);
        label.setMinimumHorizontalScale (1.0f);
        label.setBorderSize ({});
    }

    void styleStatus (juce::Label& label, float size)
    {
        label.setFont (sid::theme::font (size, true));
        label.setColour (juce::Label::textColourId, juce::Colour (kTextPrimary));
        label.setJustificationType (juce::Justification::centredLeft);
        label.setMinimumHorizontalScale (0.85f);
        label.setBorderSize ({});
    }
}

//==============================================================================
// ProdToggle
//==============================================================================
ProdToggle::ProdToggle()
{
    setClickingTogglesState (true);
    setTooltip ("When OFF, the sidechain only ducks while the host transport plays or records.");
}

void ProdToggle::paintButton (juce::Graphics& g, bool /*over*/, bool /*down*/)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const float h = bounds.getHeight();
    const float w = juce::jlimit (h * 1.6f, h * 2.4f, bounds.getWidth());

    bounds.setWidth (w);
    bounds.setHeight (h);

    // Track (lit while ON; hover/down slightly brightened via alpha)
    g.setColour (juce::Colour (getToggleState() ? kAccent : kControlEdge));
    g.fillRoundedRectangle (bounds, h * 0.5f);

    // Knob
    const float knobD = h - 4.0f;
    const float knobX = getToggleState() ? bounds.getRight() - knobD - 2.0f
                                         : bounds.getX() + 2.0f;
    juce::Rectangle<float> knob (knobX, bounds.getY() + 2.0f, knobD, knobD);
    g.setColour (juce::Colour (kTextPrimary));
    g.fillEllipse (knob);
}

//==============================================================================
// InfoPageComponent
//==============================================================================
InfoPageComponent::InfoPageComponent (sid::auth::AuthManager& authRef,
                                      const sid::license::DemoGate& demoGateRef,
                                      sid::update::UpdateChecker& updatesRef,
                                      const juce::String& version)
    : auth (authRef), demoGate (demoGateRef), updateChecker (updatesRef), pluginVersion (version)
{
    // Product branding is the shared "PumpCurve" wordmark (drawn in
    // paint(); the label only reserves layout space).
    productTitle.setText ("", juce::dontSendNotification);
    productTitle.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (productTitle);

    productVersion.setText ("VERSION " + pluginVersion, juce::dontSendNotification);
    productVersion.setFont (sid::theme::captionFont (10.5f));
    productVersion.setColour (juce::Label::textColourId, juce::Colour (kTextSubtle));
    productVersion.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (productVersion);

    // ---- version mode + licence key (0.5.0) -----------------------------
    styleHeading (licenceHeading, "LICENCE");
    addAndMakeVisible (licenceHeading);

    styleStatus (versionStatus, 16.0f);
    addAndMakeVisible (versionStatus);

    styleParagraph (versionDetail);
    addAndMakeVisible (versionDetail);

    licenceKeyEditor.setTextToShowWhenEmpty ("Licence key", juce::Colour (kTextSubtle));
    licenceKeyEditor.setInputRestrictions (64);
    licenceKeyEditor.setFont (sid::theme::font (13.5f));
    licenceKeyEditor.setIndents (8, 7);
    licenceKeyEditor.setColour (juce::TextEditor::textColourId, juce::Colour (kTextPrimary));
    licenceKeyEditor.onReturnKey = [this] { submitLicenceKey(); };
    addAndMakeVisible (licenceKeyEditor);

    activateButton.setButtonText ("ACTIVATE");
    activateButton.setTooltip ("Verify this licence key with Music-Prod and activate this computer");
    activateButton.onClick = [this] { submitLicenceKey(); };
    addAndMakeVisible (activateButton);

    styleParagraph (licenceStatus);
    addAndMakeVisible (licenceStatus);

    // ---- version check (Plugin Versions) --------------------------------
    styleHeading (updateHeading, "UPDATES");
    addAndMakeVisible (updateHeading);

    // Both versions are factual readouts: the installed one is the version this
    // binary was built as, the latest one is what the release catalog serves.
    for (auto* l : { &installedVersionLabel, &latestVersionLabel })
    {
        l->setFont (sid::theme::captionFont (10.5f));
        l->setColour (juce::Label::textColourId, juce::Colour (kTextSubtle));
        l->setJustificationType (juce::Justification::centredLeft);
        l->setBorderSize ({});
    }
    addAndMakeVisible (installedVersionLabel);

    addAndMakeVisible (latestVersionLabel);

    styleStatus (updateStatusLabel, 14.0f);
    addAndMakeVisible (updateStatusLabel);

    styleParagraph (updateDetailLabel);
    addAndMakeVisible (updateDetailLabel);

    updateButton.onClick = [this] { onUpdateButton(); };
    addAndMakeVisible (updateButton);

    styleHeading (plusHeading, "MUSIC-PROD+ ACCOUNT");
    addAndMakeVisible (plusHeading);

    styleStatus (accountStatus, 13.5f);
    accountStatus.setFont (sid::theme::font (13.5f));
    addAndMakeVisible (accountStatus);

    styleStatus (subscriptionStatus, 16.0f);
    addAndMakeVisible (subscriptionStatus);

    // The manual check. A customer who buys Music-Prod+ while this instance is
    // already open previously had no way to make the plugin look again: the
    // subscription answer was only re-fetched on sign-in, on a new plugin
    // instance, or when the 6-hour staleness rule came due. It stays visible in
    // every state (disabled while signed out or while a check is running) so the
    // layout never jumps; it is only ever enabled when there is something to
    // check.
    sid::theme::setButtonStyle (refreshButton, sid::theme::ButtonStyle::accent);
    refreshButton.setTooltip ("Ask Music-Prod again now - use this right after subscribing");
    refreshButton.onClick = [this] { onRefreshButton(); };
    addAndMakeVisible (refreshButton);

    styleParagraph (detailLabel);
    addAndMakeVisible (detailLabel);

    actionButton.onClick = [this]
    {
        switch (currentAction)
        {
            case Action::login:
                auth.startLinking();
                // Standard device-code UX (Phase 8B): the server returned the
                // product's own verification page; open it with the code
                // pre-filled. Only ever the server-provided URL.
                if (auth.getVerificationUrl().isNotEmpty())
                    juce::URL (auth.getVerificationUrl()).launchInDefaultBrowser();
                break;
            case Action::openLinkPage:
                openVerificationPage();
                break;
            case Action::logout:
                auth.logout();
                break;
            case Action::none:
                break;
        }
        refresh();
    };
    addAndMakeVisible (actionButton);

    backButton.setButtonText ("BACK");
    backButton.setTooltip ("Back to the curve editor");
    backButton.onClick = [this] { if (onBack) onBack(); };
    addAndMakeVisible (backButton);

    refresh();
}

void InfoPageComponent::submitLicenceKey()
{
    const auto key = licenceKeyEditor.getText().trim();

    if (key.isEmpty())
    {
        licenceStatus.setText ("Enter a licence key first.", juce::dontSendNotification);
        return;
    }

    // The key is handed to the auth layer, which verifies it against the
    // Music-Prod server-side contract (`activate-license` on the plugin-auth
    // service, bound to this instance's device token). It is never verified
    // locally and never stored: see AuthManager::activateLicense. Every
    // non-answer - no contract, no token, an unreachable server - fails closed
    // and never grants the full version.
    auth.activateLicense (key);
    refresh();
}

void InfoPageComponent::onUpdateButton()
{
    const auto info = updateChecker.snapshot();

    // A newer version WITH an official page: open exactly the URL the SERVER
    // supplied (UpdateCheck only ever accepts an https link from the catalog).
    // With no reviewed page the button is a retry instead - the plugin never
    // sends the user to a URL it invented, and never to a storage key.
    if (info.status == sid::update::Status::updateAvailable && info.hasDownloadLink())
    {
        juce::URL (info.downloadUrl).launchInDefaultBrowser();
        return;
    }

    updateChecker.checkNow();
    refresh();
}

void InfoPageComponent::openVerificationPage()
{
    // Only ever opens the URL the SERVER returned for THIS sign-in attempt;
    // shown to the user first (see backend-limitation note above).
    const auto url = auth.getVerificationUrl();
    if (url.isNotEmpty())
        juce::URL (url).launchInDefaultBrowser();
}

void InfoPageComponent::onRefreshButton()
{
    // FORCED check: the AuthManager asks Music-Prod again with the stored token
    // and applies only what the server answers. Deliberately not gated by the
    // staleness rule or by the lifecycle debounce - the reason to press this is
    // that the account changed a moment ago. Nothing is unlocked here: the
    // click sends a request, and the entitlement (and therefore the full
    // version) comes from the response.
    auth.refreshEntitlements();
    refresh();
}

void InfoPageComponent::refresh()
{
    using State = sid::auth::AuthManager::State;

    // ---- version mode: full version or the demo session (0.5.0) ---------
    //
    // The FULL VERSION banner names the ROUTE that granted it. The two routes are
    // independent - a licence key works without Music-Prod+, a subscription works
    // without a key - and both can be true at once, so the user is told which one
    // is doing the work rather than being left to guess.
    if (demoGate.isFullVersion())
    {
        const bool viaLicense      = auth.fullVersionViaLicense();
        const bool viaSubscription = auth.fullVersionViaSubscription();

        versionStatus.setText ("FULL VERSION", juce::dontSendNotification);
        setTone (versionStatus, Tone::good);

        if (viaLicense && viaSubscription)
            versionDetail.setText ("Unlocked by your PumpCurve licence key and your Music-Prod+ "
                                   "subscription.",
                                   juce::dontSendNotification);
        else if (viaLicense)
            versionDetail.setText ("Unlocked by a verified PumpCurve licence key. A Music-Prod+ "
                                   "subscription is not required.",
                                   juce::dontSendNotification);
        else if (viaSubscription)
            versionDetail.setText ("Unlocked by your active Music-Prod+ subscription.",
                                   juce::dontSendNotification);
        else
            versionDetail.setText ("A verified entitlement unlocks unlimited use on this machine.",
                                   juce::dontSendNotification);
    }
    else if (demoGate.expired())
    {
        versionStatus.setText ("DEMO EXPIRED", juce::dontSendNotification);
        setTone (versionStatus, Tone::warn);
        versionDetail.setText ("The 15-minute demo has ended and audio processing has stopped. "
                               "Restart the PumpCurve instance (remove and re-add it, or reload "
                               "the project) to start another 15 minutes.",
                               juce::dontSendNotification);
    }
    else
    {
        versionStatus.setText ("DEMO - " + sid::license::formatClock (demoGate.remainingSeconds())
                                   + " REMAINING",
                               juce::dontSendNotification);
        setTone (versionStatus, Tone::neutral);
        versionDetail.setText ("15 minutes per plugin instance. Add a licence key, or sign in "
                               "with an active Music-Prod+ subscription, for unlimited use.",
                               juce::dontSendNotification);
    }

    // ---- licence-key route ----------------------------------------------
    //
    // One line per state: a short uppercase verdict the user can scan, then
    // ONE explanation. The explanation is the AuthManager's message when it
    // carries the server's (or the manager's own, more specific) wording, and
    // the page's fallback otherwise - never both, so the same sentence can no
    // longer appear twice. States whose verdict already IS the whole story
    // (validating, offline, unavailable, signed out) show the page text only.
    using LS = sid::auth::AuthManager::LicenseStatus;
    const auto licenceState = auth.getLicenseStatus();
    const auto managerMessage = auth.getLicenseMessage();
    juce::String verdict, explanation;
    bool useManagerMessage = true;
    Tone licenceTone = Tone::muted;

    switch (licenceState)
    {
        case LS::notChecked:
            useManagerMessage = false;
            break;
        case LS::validating:
            verdict = "VALIDATING LICENCE KEY...";
            useManagerMessage = false;
            break;
        case LS::active:
            verdict = "LICENCE ACTIVE - FULL VERSION";
            licenceTone = Tone::good;
            break;
        case LS::invalid:
            verdict = "LICENCE KEY NOT ACCEPTED.";
            explanation = "Check the key and try again.";
            licenceTone = Tone::bad;
            break;
        case LS::deviceLimit:
            // The key is FINE and belongs to this account: every activation slot
            // is in use. Saying "not accepted" here would be false, so the line
            // names the actual next step instead.
            verdict = "TOO MANY COMPUTERS.";
            explanation = "This key is already active on the maximum number of computers. "
                          "Deactivate one at music-prod.com/profile, then activate this "
                          "computer again.";
            useManagerMessage = false;   // the manager's default says the same thing
            licenceTone = Tone::warn;
            break;
        case LS::deviceInactive:
            // The account holds the licence; THIS computer holds no activation
            // slot. Always true, and actionable either way.
            verdict = "NOT ACTIVATED ON THIS COMPUTER.";
            explanation = "Your licence is on your Music-Prod account but not activated here.";
            licenceTone = Tone::warn;
            break;
        case LS::revoked:
            verdict = "THIS LICENCE HAS EXPIRED OR BEEN REVOKED.";
            licenceTone = Tone::bad;
            break;
        case LS::networkError:
            verdict = "COULD NOT REACH THE LICENCE SERVER - NOTHING WAS VERIFIED.";
            explanation = "Check your connection and try again.";
            useManagerMessage = false;
            licenceTone = Tone::warn;
            break;
        case LS::unavailable:
            verdict = "LICENCE ACTIVATION IS UNAVAILABLE.";
            explanation = "No key has been verified. Contact Music-Prod support.";
            useManagerMessage = false;
            licenceTone = Tone::warn;
            break;
        case LS::signInRequired:
            // A key is bound to an ACCOUNT, so it cannot be activated signed out.
            verdict = "SIGN IN TO USE A LICENCE KEY:";
            explanation = "a key is activated on your Music-Prod account. Sign in under "
                          "MUSIC-PROD+ ACCOUNT, then enter the key.";
            useManagerMessage = false;
            break;
    }

    if (useManagerMessage && managerMessage.isNotEmpty())
        explanation = managerMessage;

    if (licenceState == LS::deviceInactive)
        explanation << (auth.isSubscribed()
                            ? " Your Music-Prod+ subscription unlocks PumpCurve here anyway."
                            : " Enter your licence key to activate this computer (up to three "
                              "can be active).");

    if ((licenceState == LS::deviceLimit || licenceState == LS::deviceInactive)
        && ! explanation.containsIgnoreCase ("music-prod.com/profile"))
        explanation << " Manage computers at music-prod.com/profile.";

    // A verdict always ends as a sentence before its explanation starts.
    if (verdict.isNotEmpty() && explanation.isNotEmpty() && ! juce::String (".:").containsChar (verdict.getLastCharacter()))
        verdict << ".";
    licenceStatus.setText ((verdict + " " + explanation).trim(), juce::dontSendNotification);
    setTone (licenceStatus, licenceTone);

    activateButton.setEnabled (auth.getLicenseStatus() != LS::validating);

    // ---- version check (Plugin Versions) --------------------------------
    // The PROCESSOR owns the checker, so its interval belongs to the plugin
    // instance and survives opening and closing this page. Asking here is a
    // no-op until the interval has elapsed (six hours) or until the first time
    // the page is shown, so displaying the page cannot hammer the server. The
    // request itself runs on the checker's own thread - this call never blocks,
    // and a failure here can never stop audio, change the licence state or end
    // a demo session.
    updateChecker.refreshIfDue();

    const auto update = updateChecker.snapshot();
    using US = sid::update::Status;

    installedVersionLabel.setText ("INSTALLED VERSION " + update.installedVersion,
                                   juce::dontSendNotification);
    latestVersionLabel.setText (update.latestVersion.isNotEmpty()
                                    ? "LATEST VERSION " + update.latestVersion
                                    : "LATEST VERSION -",
                                juce::dontSendNotification);

    switch (update.status)
    {
        case US::unknown:         updateStatusLabel.setText ("NOT CHECKED YET", juce::dontSendNotification); break;
        // (tone applied below, from the same status)
        case US::checking:        updateStatusLabel.setText ("CHECKING...",        juce::dontSendNotification); break;
        case US::upToDate:        updateStatusLabel.setText ("UP TO DATE",         juce::dontSendNotification); break;
        // Nothing published to compare against. Deliberately NOT "UP TO DATE":
        // that would claim the installed build is the latest published version
        // while the line above it reads "LATEST VERSION -", which is the
        // contradiction this state exists to remove. The catalog itself reports
        // status "noPublishedRelease" for this case.
        case US::noPublishedRelease:
                                  updateStatusLabel.setText ("NO PUBLISHED RELEASE", juce::dontSendNotification); break;
        case US::updateAvailable: updateStatusLabel.setText ("UPDATE AVAILABLE",   juce::dontSendNotification); break;
        case US::unableToCheck:   updateStatusLabel.setText ("UNABLE TO CHECK",    juce::dontSendNotification); break;
    }

    setTone (updateStatusLabel, update.status == US::updateAvailable ? Tone::good
                              : update.status == US::unableToCheck   ? Tone::warn
                              : update.status == US::upToDate        ? Tone::neutral
                                                                     : Tone::muted);

    juce::String updateDetail = update.detail;
    // Brief release information for the version being offered. Trimmed hard so
    // the line stays inside the fixed INFO layout.
    if (update.status == US::updateAvailable && update.releaseNotes.isNotEmpty())
    {
        auto notes = update.releaseNotes.replaceCharacter ('\n', ' ').trim();
        if (notes.length() > 150)
            notes = notes.substring (0, 150).trim() + "...";
        updateDetail += "  " + notes;
    }
    updateDetailLabel.setText (updateDetail, juce::dontSendNotification);

    if (update.status == US::checking)
    {
        updateButton.setButtonText ("CHECKING...");
        updateButton.setEnabled (false);
    }
    else if (update.status == US::updateAvailable && update.hasDownloadLink())
    {
        updateButton.setButtonText ("DOWNLOAD UPDATE");
        updateButton.setEnabled (true);
        sid::theme::setButtonStyle (updateButton, sid::theme::ButtonStyle::primary);
    }
    else if (update.status == US::unableToCheck)
    {
        updateButton.setButtonText ("TRY AGAIN");
        updateButton.setEnabled (true);
    }
    else
    {
        updateButton.setButtonText ("CHECK FOR UPDATES");
        updateButton.setEnabled (true);
    }

    if (! (update.status == US::updateAvailable && update.hasDownloadLink()))
        sid::theme::setButtonStyle (updateButton, sid::theme::ButtonStyle::secondary);

    lastUpdateStatus = update.status;

    const auto state = auth.getState();
    const juce::String name = auth.getDisplayName();

    switch (state)
    {
        case State::signedIn:
        {
            accountStatus.setText (name.isNotEmpty()
                                       ? "SIGNED IN AS " + name
                                       : "SIGNED IN",
                                   juce::dontSendNotification);
            actionButton.setButtonText ("LOG OUT");
            currentAction = Action::logout;
            setTone (accountStatus, Tone::neutral);
            break;
        }
        case State::linking:
        {
            accountStatus.setText ("WAITING FOR APPROVAL", juce::dontSendNotification);
            actionButton.setButtonText ("REOPEN LINK PAGE");
            currentAction = Action::openLinkPage;
            setTone (accountStatus, Tone::warn);
            break;
        }
        case State::error:
        {
            accountStatus.setText ("AUTHENTICATION ERROR", juce::dontSendNotification);
            actionButton.setButtonText ("LOG IN");
            currentAction = Action::login;
            setTone (accountStatus, Tone::bad);
            break;
        }
        case State::signedOut:
        {
            accountStatus.setText ("SIGNED OUT", juce::dontSendNotification);
            actionButton.setButtonText ("LOG IN");
            currentAction = Action::login;
            setTone (accountStatus, Tone::muted);
            break;
        }
    }

    // Signed out, the account action is the thing to do; signed in, it is not.
    sid::theme::setButtonStyle (actionButton, currentAction == Action::login
                                                  ? sid::theme::ButtonStyle::accent
                                                  : sid::theme::ButtonStyle::secondary);

    // Entitlement state only meaningful when signed in (no invented values).
    // A membership the server did NOT confirm for this product is shown as
    // exactly that - never as an active subscription that unlocks PumpCurve.
    const bool notValidHere = auth.subscriptionNotValidForProduct();
    // A Music-Prod+ full version this instance HAD and lost: named as such, so
    // the customer knows why the demo is back. Shown only while in the demo.
    using Lapse = sid::auth::AuthManager::MembershipLapse;
    const Lapse lapse = demoGate.isFullVersion() ? Lapse::none : auth.getMembershipLapse();
    if (state == State::signedIn && lapse != Lapse::none)
    {
        subscriptionStatus.setText (lapse == Lapse::ended ? "MUSIC-PROD+ ENDED - DEMO VERSION"
                                                          : "MUSIC-PROD+ NOT CONFIRMED - DEMO VERSION",
                                    juce::dontSendNotification);
        setTone (subscriptionStatus, Tone::warn);
    }
    else if (state == State::signedIn)
    {
        subscriptionStatus.setText (! auth.isSubscribed() ? "MUSIC-PROD+ NOT ACTIVE"
                                    : notValidHere        ? "MUSIC-PROD+ NOT CONFIRMED FOR PUMPCURVE"
                                                          : "MUSIC-PROD+ ACTIVE",
                                    juce::dontSendNotification);
        setTone (subscriptionStatus, auth.isSubscribed() && ! notValidHere ? Tone::good : Tone::warn);
    }
    else
    {
        // No account, so no Music-Prod+ verdict to show: the headline carries
        // the account state itself (with its dot) instead of an empty row, and
        // the line below it collapses. Nothing about entitlement is implied.
        subscriptionStatus.setText (accountStatus.getText(), juce::dontSendNotification);
        subscriptionStatus.setColour (juce::Label::textColourId,
                                      accountStatus.findColour (juce::Label::textColourId));
        accountStatus.setText ({}, juce::dontSendNotification);
    }

    // The account line is present only when signed in; re-lay the column when
    // that changes so the explanation sits directly under the headline.
    if (accountStatus.getText().isEmpty() != accountLineCollapsed)
        resized();

    // ---- the manual check ------------------------------------------------
    // Enabled only when there is a signed-in account to ask about and no check
    // is already running; the text says which of those states it is in, so the
    // result of a click is never in doubt while the request is on the wire.
    const bool checkingSubscription = auth.isEntitlementCheckInFlight();
    const bool canCheck = (state == State::signedIn);
    refreshButton.setButtonText (checkingSubscription ? "CHECKING..." : "CHECK SUBSCRIPTION");
    refreshButton.setEnabled (canCheck && ! checkingSubscription);

    // Detail line: device code while linking, error text on error, the state of
    // a running check, and - for a signed-in account with nothing active - what
    // to do next. The last case exists because "MUSIC-PROD+ NOT ACTIVE" on its
    // own tells a customer who has just subscribed nothing at all.
    if (state == State::linking && auth.getUserCode().isNotEmpty())
        detailLabel.setText ("Approve this code on your Music-Prod account page "
                                 "(opened in your browser): "
                                 + auth.getUserCode(),
                             juce::dontSendNotification);
    else if (state == State::error && auth.getLastError().isNotEmpty())
        detailLabel.setText (auth.getLastError(), juce::dontSendNotification);
    else if (state == State::signedIn && checkingSubscription)
        detailLabel.setText ("Checking your subscription with Music-Prod...",
                             juce::dontSendNotification);
    else if (state == State::signedIn && lapse == Lapse::ended)
        detailLabel.setText ("Music-Prod reports that your Music-Prod+ membership has ended, so "
                             "PumpCurve is back in the 15-minute demo. Renew Music-Prod+ or enter "
                             "a PumpCurve licence key to unlock the full version again.",
                             juce::dontSendNotification);
    else if (state == State::signedIn && lapse == Lapse::unconfirmed)
        detailLabel.setText ("PumpCurve could not confirm your Music-Prod+ membership with "
                             "Music-Prod for 14 days, so it is back in the 15-minute demo. Connect "
                             "to the internet and press CHECK SUBSCRIPTION.",
                             juce::dontSendNotification);
    else if (state == State::signedIn && auth.didLastEntitlementCheckFail())
        // No answer is not an answer: say so, keep the last verified state
        // (shown above), and point at the retry - never a verdict the server
        // did not just give.
        detailLabel.setText ("COULD NOT REACH MUSIC-PROD - the subscription was not checked "
                             "and nothing changed. Check your connection, then press CHECK "
                             "SUBSCRIPTION to try again.",
                             juce::dontSendNotification);
    else if (state == State::signedIn && notValidHere)
        // The server confirmed the membership but answered "not entitled" for
        // THIS product. Seen in the field with a sign-in token minted for
        // another plugin (or before tokens recorded their product): signing in
        // again from PumpCurve mints a PumpCurve token.
        detailLabel.setText (auth.tokenIsForAnotherProduct()
                                 ? "This sign-in was made for another Music-Prod plugin, so "
                                   "Music-Prod did not confirm Music-Prod+ for PumpCurve. Press "
                                   "LOG OUT, sign in again here, then CHECK SUBSCRIPTION."
                                 : "Music-Prod confirmed your Music-Prod+ membership but not for "
                                   "PumpCurve on this sign-in. Press LOG OUT and sign in again "
                                   "here; if it persists, contact Music-Prod support.",
                             juce::dontSendNotification);
    else if (state == State::signedIn && auth.isSubscribed())
        detailLabel.setText ("Your Music-Prod+ subscription was confirmed by Music-Prod.",
                             juce::dontSendNotification);
    else if (state == State::signedIn && ! demoGate.isFullVersion())
        detailLabel.setText ("Music-Prod reports no active Music-Prod+ subscription for this "
                             "account. If you have just subscribed, press CHECK SUBSCRIPTION "
                             "- there is no need to log out. A PumpCurve licence key unlocks "
                             "the full version too.",
                             juce::dontSendNotification);
    else if (state == State::signedOut)
        detailLabel.setText ("Sign in with your Music-Prod account to use your Music-Prod+ "
                             "subscription or to activate a PumpCurve licence key.",
                             juce::dontSendNotification);
    else
        detailLabel.setText ("", juce::dontSendNotification);

    setTone (detailLabel, state == State::signedIn && ! checkingSubscription
                              && (auth.didLastEntitlementCheckFail() || notValidHere
                                  || lapse != Lapse::none) ? Tone::warn
                        : state == State::error ? Tone::bad
                                                : Tone::muted);
}

void InfoPageComponent::paint (juce::Graphics& g)
{
    // Same field and the same single hairline as the main view, so INFO reads
    // as a page of the same instrument rather than a settings dialog.
    sid::theme::fillField (g, getLocalBounds());
    g.setColour (juce::Colour (sid::theme::kSeparator));
    g.fillRect (0, kHeaderHeight - 1, getWidth(), 1);

    if (productTitle.getWidth() > 0)
        sid::branding::drawWordmark (g, productTitle.getBounds().toFloat(), 20.0f);

    // UPDATES lives in a recessed status strip (depth, not a ruled box).
    if (! updatesStrip.isEmpty())
        sid::theme::drawWell (g, updatesStrip.toFloat(), 6.0f);

    // State dots beside the three status lines. The dot takes the line's own
    // colour; a neutral line gets a quiet grey dot.
    for (auto* status : { &versionStatus, &subscriptionStatus, &updateStatusLabel })
    {
        if (status->getText().isEmpty() || status->getWidth() <= 0)
            continue;
        auto c = status->findColour (juce::Label::textColourId);
        if (c == juce::Colour (kTextPrimary))
            c = juce::Colour (kTextSubtle);
        const juce::Point<float> centre ((float) status->getX() - 11.0f,
                                         (float) status->getBounds().getCentreY());
        g.setColour (c.withAlpha (0.18f));
        g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (centre));
        g.setColour (c);
        g.fillEllipse (juce::Rectangle<float> (6.5f, 6.5f).withCentre (centre));
    }
}

void InfoPageComponent::resized()
{
    // FIXED 800x520 PAGE (it covers the editor). Two columns - LICENCE and the
    // MUSIC-PROD+ ACCOUNT - over an UPDATES status strip. Rows are sized from
    // the content (paragraphs get the lines they need to WRAP, never squeeze)
    // and the whole block is centred between the header and the Music-Prod
    // signature, so there is no dead band at the bottom. Every control fits:
    // asserted by Tests/ui_verify.sh.
    constexpr int margin = 24, gutter = 40, capH = 30, dotIndent = 20;
    const int rowY = (kHeaderHeight - capH) / 2;

    // ---- header: BACK | wordmark .................. VERSION x.y.z ----------
    backButton.setBounds (20, rowY, 76, capH);
    productTitle.setBounds (20 + 76 + 18, 9, 180, 36);
    productVersion.setBounds (getWidth() - 20 - 200, rowY, 200, capH);

    const int columnW = (getWidth() - margin * 2 - gutter) / 2;
    const int leftX = margin, rightX = margin + columnW + gutter;
    constexpr int stripH = 12 + capH + 6 + 16 + 6 + 34 + 12, blockGap = 24;
    constexpr int columnsH = 16 + 8 + 24 + 6 + 52 + 12 + capH + 10 + 52;   // tallest column
    const int top = kHeaderHeight, bottom = getHeight() - 36;               // signature below
    const int blockH = columnsH + blockGap + stripH;
    int y = top + juce::jmax (18, (bottom - top - blockH) / 2);

    // ---- LICENCE --------------------------------------------------------
    {
        auto left = juce::Rectangle<int> (leftX, y, columnW, columnsH);
        licenceHeading.setBounds (left.removeFromTop (16));
        left.removeFromTop (8);
        versionStatus.setBounds (left.removeFromTop (24).withTrimmedLeft (dotIndent));
        left.removeFromTop (6);
        versionDetail.setBounds (left.removeFromTop (52));   // three lines
        left.removeFromTop (12);
        {
            auto keyRow = left.removeFromTop (capH);
            activateButton.setBounds (keyRow.removeFromRight (110));
            keyRow.removeFromRight (8);
            licenceKeyEditor.setBounds (keyRow);
        }
        left.removeFromTop (10);
        licenceStatus.setBounds (left);
    }

    // ---- MUSIC-PROD+ ACCOUNT --------------------------------------------
    {
        auto right = juce::Rectangle<int> (rightX, y, columnW, columnsH);
        plusHeading.setBounds (right.removeFromTop (16));
        right.removeFromTop (8);
        subscriptionStatus.setBounds (right.removeFromTop (24).withTrimmedLeft (dotIndent));
        right.removeFromTop (6);
        accountLineCollapsed = accountStatus.getText().isEmpty();
        if (accountLineCollapsed)
            accountStatus.setBounds ({});
        else
        {
            accountStatus.setBounds (right.removeFromTop (18));
            right.removeFromTop (4);
        }
        detailLabel.setBounds (right.removeFromTop (52));
        right.removeFromTop (12);
        auto buttons = right.removeFromTop (capH);
        refreshButton.setBounds (buttons.removeFromLeft (184));
        buttons.removeFromLeft (8);
        actionButton.setBounds (buttons.removeFromLeft (128));
    }

    // ---- UPDATES strip (full width) --------------------------------------
    y += columnsH + blockGap;
    updatesStrip = juce::Rectangle<int> (margin - 8, y, getWidth() - (margin - 8) * 2, stripH);
    {
        auto strip = updatesStrip.reduced (16, 12);
        auto headingRow = strip.removeFromTop (capH);
        updateButton.setBounds (headingRow.removeFromRight (180));
        updateHeading.setBounds (headingRow.removeFromLeft (90));
        updateStatusLabel.setBounds (headingRow.withTrimmedLeft (dotIndent).withTrimmedRight (12));
        strip.removeFromTop (6);
        auto versionsRow = strip.removeFromTop (16);
        installedVersionLabel.setBounds (versionsRow.removeFromLeft (190));
        latestVersionLabel.setBounds (versionsRow.removeFromLeft (190));
        strip.removeFromTop (6);
        updateDetailLabel.setBounds (strip);
    }
}
