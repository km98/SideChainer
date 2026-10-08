/*
    SideChain - INFO page implementation (Phase 8).

    Status display rules (all textual, no colour-only indicators):
      SIGNED OUT              - no token
      LINKING - CODE XXXX-XXXX- waiting for user approval (device code)
      SIGNED IN               - token present
      SIGNED IN (cached)      - token present, last refresh failed
      AUTHENTICATION ERROR    - last operation failed
      MUSIC-PROD+ ACTIVE / MUSIC-PROD+ NOT ACTIVE - entitlement state

    Phase 8B: the backend resolves the product's own verification page
    (SideChain -> /plugin/link, product-neutral Music-Prod page), so login
    auto-opens the browser with the device code pre-filled — the standard
    device-code UX. The URL comes from the server response; the page is
    Music-Prod/SideChain branded (never another product's).
*/

#include "InfoPage.h"
#include "Branding.h"

namespace
{
    // Shared palette additions (same language as PluginEditor.cpp; pure
    // black field, controls slightly lifted off it).
    constexpr uint32_t kBackground   = 0xff000000;
    constexpr uint32_t kTextPrimary  = 0xffe8ecf3;
    constexpr uint32_t kTextSubtle   = 0xff6b7687;
    constexpr uint32_t kAccent       = 0xff7fd1c0;
    constexpr uint32_t kControlBg    = 0xff15181e;
    constexpr uint32_t kControlEdge  = 0xff2c3342;
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
                                      const juce::String& version)
    : auth (authRef), pluginVersion (version)
{
    // Product branding is the shared "SideChainer" wordmark (drawn in
    // paint(); the label only reserves layout space).
    productTitle.setText ("", juce::dontSendNotification);
    productTitle.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (productTitle);

    productVersion.setText ("VERSION " + pluginVersion, juce::dontSendNotification);
    productVersion.setFont (juce::Font (12.0f, juce::Font::plain));
    productVersion.setColour (juce::Label::textColourId, juce::Colour (kTextSubtle));
    productVersion.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (productVersion);

    plusHeading.setText ("MUSIC-PROD+", juce::dontSendNotification);
    plusHeading.setFont (juce::Font (15.0f, juce::Font::bold));
    plusHeading.setColour (juce::Label::textColourId, juce::Colour (kAccent));
    plusHeading.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (plusHeading);

    accountStatus.setFont (juce::Font (14.0f, juce::Font::plain));
    accountStatus.setColour (juce::Label::textColourId, juce::Colour (kTextPrimary));
    accountStatus.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (accountStatus);

    subscriptionStatus.setFont (juce::Font (14.0f, juce::Font::plain));
    subscriptionStatus.setColour (juce::Label::textColourId, juce::Colour (kTextSubtle));
    subscriptionStatus.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (subscriptionStatus);

    detailLabel.setFont (juce::Font (12.0f, juce::Font::plain));
    detailLabel.setColour (juce::Label::textColourId, juce::Colour (kTextSubtle));
    detailLabel.setJustificationType (juce::Justification::centredLeft);
    detailLabel.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (detailLabel);

    actionButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (kControlBg));
    actionButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (kControlBg));
    actionButton.setColour (juce::TextButton::textColourOffId, juce::Colour (kAccent));
    actionButton.setColour (juce::TextButton::textColourOnId,  juce::Colour (kAccent));
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
    backButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (kControlBg));
    backButton.setColour (juce::TextButton::textColourOffId, juce::Colour (kTextPrimary));
    backButton.onClick = [this] { if (onBack) onBack(); };
    addAndMakeVisible (backButton);

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

void InfoPageComponent::refresh()
{
    using State = sid::auth::AuthManager::State;

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
            break;
        }
        case State::linking:
        {
            accountStatus.setText ("WAITING FOR APPROVAL", juce::dontSendNotification);
            actionButton.setButtonText ("REOPEN LINK PAGE");
            currentAction = Action::openLinkPage;
            break;
        }
        case State::error:
        {
            accountStatus.setText ("AUTHENTICATION ERROR", juce::dontSendNotification);
            actionButton.setButtonText ("LOG IN");
            currentAction = Action::login;
            break;
        }
        case State::signedOut:
        {
            accountStatus.setText ("SIGNED OUT", juce::dontSendNotification);
            actionButton.setButtonText ("LOG IN");
            currentAction = Action::login;
            break;
        }
    }

    // Entitlement state only meaningful when signed in (no invented values).
    if (state == State::signedIn)
        subscriptionStatus.setText (auth.isSubscribed()
                                        ? "MUSIC-PROD+ ACTIVE"
                                        : "MUSIC-PROD+ NOT ACTIVE",
                                    juce::dontSendNotification);
    else
        subscriptionStatus.setText ("", juce::dontSendNotification);

    // Detail line: device code while linking, error text on error.
    if (state == State::linking && auth.getUserCode().isNotEmpty())
        detailLabel.setText ("Approve this code on your Music-Prod account page "
                                 "(opened in your browser): "
                                 + auth.getUserCode(),
                             juce::dontSendNotification);
    else if (state == State::error && auth.getLastError().isNotEmpty())
        detailLabel.setText (auth.getLastError(), juce::dontSendNotification);
    else
        detailLabel.setText ("", juce::dontSendNotification);
}

void InfoPageComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (kBackground));

    // Product branding: shared SideChainer wordmark (same treatment as the
    // main header), drawn in the space reserved by productTitle.
    if (productTitle.getWidth() > 0)
        sid::branding::drawWordmark (g, productTitle.getBounds().toFloat(), 34.0f);

    // Section divider above the account block (only when the heading is
    // laid out; avoids drawing a stray line during the first pass).
    if (isShowing() || plusHeading.getWidth() > 0)
    {
        g.setColour (juce::Colour (kControlEdge));
        g.drawHorizontalLine (plusHeading.getY() - 8, 20.0f, (float) getWidth() - 20.0f);
    }
}

void InfoPageComponent::resized()
{
    auto area = getLocalBounds().reduced (20);

    backButton.setBounds (area.removeFromTop (26).removeFromLeft (90));

    area.removeFromTop (18);
    productTitle.setBounds (area.removeFromTop (40));
    productVersion.setBounds (area.removeFromTop (18));

    area.removeFromTop (28);
    plusHeading.setBounds (area.removeFromTop (22));
    area.removeFromTop (6);
    accountStatus.setBounds (area.removeFromTop (20));
    subscriptionStatus.setBounds (area.removeFromTop (20));
    area.removeFromTop (6);
    detailLabel.setBounds (area.removeFromTop (40));
    area.removeFromTop (10);
    actionButton.setBounds (area.removeFromTop (32).removeFromLeft (170));
}
