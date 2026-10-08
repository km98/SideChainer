/*
    SideChain - INFO / account page (Phase 8).

    A view swapped INSIDE the existing editor (no second window): the
    editor toggles between the main view and this component.

    Contents: Music-Prod branding, product name, plugin version, account
    section (Music-Prod+ status, sign-in state), login/logout controls and
    a back control. Auth state is displayed with TEXT (never ambiguous
    colour-only indicators). Version comes from JucePlugin_VersionString
    (the .jucer project version) - no hardcoded copy.

    The Music-Prod logo (Resources/music-prod-logo.png, the approved asset
    used by the web project) is drawn by the editor in both views.

    THREADING: the AuthManager is owned by the EDITOR; all calls happen on
    the message thread. The auth thread inside AuthManager does the
    network work. The audio thread is never involved.
*/

#pragma once

#include <JuceHeader.h>
#include "MusicProdAuth.h"

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
class InfoPageComponent : public juce::Component
{
public:
    InfoPageComponent (sid::auth::AuthManager& auth, const juce::String& version);

    void paint (juce::Graphics&) override;
    void resized() override;

    // Refresh displayed state from the AuthManager (called on editor timer).
    void refresh();

    std::function<void()> onBack;   // editor sets this to return to main view

private:
    void openVerificationPage();

    sid::auth::AuthManager& auth;
    const juce::String pluginVersion;

    juce::Label productTitle;      // "SideChain"
    juce::Label productVersion;    // "VERSION X.Y.Z"
    juce::Label plusHeading;       // "MUSIC-PROD+"
    juce::Label accountStatus;     // textual auth state (never colour-only)
    juce::Label subscriptionStatus;// textual Music-Prod+ state
    juce::Label detailLabel;       // user code / error / instructions
    juce::TextButton actionButton; // LOG IN / OPEN LINK PAGE / LOG OUT
    juce::TextButton backButton;   // "BACK"

    // Which flow the action button currently drives (updated in refresh()).
    enum class Action { login, openLinkPage, logout, none };
    Action currentAction = Action::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InfoPageComponent)
};
