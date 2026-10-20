/*
    SideChain - INFO / account page (Phase 8).

    A view swapped INSIDE the existing editor (no second window): the
    editor toggles between the main view and this component.

    Contents: Music-Prod branding, product name, plugin version, the LICENCE
    section (demo countdown / full version / licence-key entry), the UPDATES
    section (installed version, latest published version, update status and the
    official download link), the account section (Music-Prod+ status, the
    CHECK SUBSCRIPTION control, sign-in state), login/logout controls and a back
    control. Every state is displayed with TEXT (never ambiguous colour-only
    indicators). Both the installed
    version and the latest version come from their real sources: the former from
    JucePlugin_VersionString (the .jucer project version), the latter from the
    Music-Prod release catalog (see UpdateCheck.h) - neither is hardcoded here.

    The version CHECK never runs on this class: the processor owns the
    UpdateChecker, and refresh() only reads its snapshot and asks for a check
    when the interval has elapsed. No network call can block a repaint.

    The Music-Prod logo (Resources/music-prod-logo.png, the approved asset
    used by the web project) is drawn by the editor in both views.

    THREADING: the AuthManager is owned by the EDITOR; all calls happen on
    the message thread. The auth thread inside AuthManager does the
    network work. The audio thread is never involved.
*/

#pragma once

#include <JuceHeader.h>
#include "MusicProdAuth.h"
#include "LicenseGate.h"
#include "UpdateCheck.h"

//==============================================================================
// Compact iOS-style toggle used for SIDECHAIN WHILE STOPPED (main view).
// Draws its own pill/track/knob in the shared dark palette; a ToggleButton
// subclass keeps the standard attachment + keyboard behaviour.
class ProdToggle : public juce::ToggleButton
{
public:
    ProdToggle();

    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProdToggle)
};

//==============================================================================
// INFO / licensing page.
//
// Shows the two operating modes plainly:
//   FULL VERSION - a server-verified entitlement (paid Music-Prod+
//                  subscription, or a valid Music-Prod licence key)
//   DEMO         - 15 minutes for this plugin instance, with the remaining
//                  time in MM:SS, then an explicit expiry message telling the
//                  user that restarting the instance starts a new session
// plus the licence-key entry and the Music-Prod+ account section.
//==============================================================================
class InfoPageComponent : public juce::Component
{
public:
    InfoPageComponent (sid::auth::AuthManager& auth,
                       const sid::license::DemoGate& demoGate,
                       sid::update::UpdateChecker& updates,
                       const juce::String& version);

    void paint (juce::Graphics&) override;
    void resized() override;

    // Refresh displayed state from the AuthManager (called on editor timer).
    void refresh();

    std::function<void()> onBack;   // editor sets this to return to main view

private:
    void openVerificationPage();
    void submitLicenceKey();
    void onUpdateButton();

    /**
        The manual entitlement check (MUSIC-PROD+ -> CHECK SUBSCRIPTION).

        The reason this control exists: a customer who buys Music-Prod+ while
        PumpCurve is already open must be able to get the full version without
        logging out, restarting the plugin or restarting their host. It asks the
        AuthManager for a FORCED check (no staleness gate, no lifecycle
        debounce) and shows a checking state until the server answers. Nothing is
        granted by the click itself.
    */
    void onRefreshButton();

    static constexpr int kHeaderHeight = 56;   // matches the main view's header band
    juce::Rectangle<int> updatesStrip;         // recessed UPDATES status strip
    bool accountLineCollapsed = false;         // signed out: account state is the headline

    sid::auth::AuthManager& auth;
    const sid::license::DemoGate& demoGate;
    sid::update::UpdateChecker& updateChecker;
    const juce::String pluginVersion;

    // Last update state applied to the controls, so a 30 Hz timer cannot
    // fight the user's click and an in-flight check is not restarted.
    sid::update::Status lastUpdateStatus = sid::update::Status::unknown;

    juce::Label productTitle;      // "SideChain"
    juce::Label productVersion;    // "VERSION X.Y.Z"
    juce::Label licenceHeading;    // "LICENCE"
    juce::Label versionStatus;     // FULL VERSION / DEMO mm:ss / DEMO EXPIRED
    juce::Label versionDetail;     // expiry instruction / demo explanation
    juce::TextEditor licenceKeyEditor;
    juce::TextButton activateButton;  // "ACTIVATE"
    juce::Label licenceStatus;     // textual licence state (never colour-only)
    juce::Label updateHeading;     // "UPDATES"
    juce::Label installedVersionLabel; // "INSTALLED VERSION 0.5.0"
    juce::Label latestVersionLabel;    // "LATEST VERSION 0.5.1"
    juce::Label updateStatusLabel;     // UP TO DATE / NO PUBLISHED RELEASE / UPDATE AVAILABLE / UNABLE TO CHECK
    juce::Label updateDetailLabel;     // release notes and the reason for the state
    juce::TextButton updateButton;     // DOWNLOAD UPDATE / CHECK AGAIN / TRY AGAIN
    juce::Label plusHeading;       // "MUSIC-PROD+"
    juce::Label accountStatus;     // textual auth state (never colour-only)
    juce::Label subscriptionStatus;// textual Music-Prod+ state
    juce::TextButton refreshButton; // "CHECK SUBSCRIPTION" / "CHECKING..."
    juce::Label detailLabel;       // user code / error / instructions
    juce::TextButton actionButton; // LOG IN / OPEN LINK PAGE / LOG OUT
    juce::TextButton backButton;   // "BACK"

    // Which flow the action button currently drives (updated in refresh()).
    enum class Action { login, openLinkPage, logout, none };
    Action currentAction = Action::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InfoPageComponent)
};
