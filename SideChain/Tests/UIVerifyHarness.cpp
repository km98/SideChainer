/*
    SideChain - UI verification harness (real editor + InfoPage, offscreen).

    Verifies after the 0.2.1 defect fixes:
      1. Editor field and header use the theme colours (0.5.0 redesign).
      2. INFO page is in the hierarchy, full-size, every control visible
         (8 account controls + the 6 licensing controls added in 0.5.0 +
         the MUSIC-PROD+ CHECK SUBSCRIPTION control added for 0.5.0's
         subscription-refresh fix).
      3. INFO page uses the same theme field (same visual system).
      4. With the production background-polling setting, a mock-approved
         sign-in flips LINKING -> SIGNED IN with NO manual poll call - the
         exact defect seen in Logic (browser "Plugin connected" while the
         plugin stayed WAITING FOR APPROVAL).
    Build/run: see Tests/ui_verify.sh
*/

#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/GraphComponent.h"
#include "../Source/PumpCurveTheme.h"

#include <cstdio>
#include <string>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <vector>
#include <functional>

static int checks = 0, failures = 0;
static void check (bool ok, const std::string& label)
{
    ++checks;
    printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (! ok) ++failures;
}

// ---- test clock + INFO page text/layout helpers (0.5.0 licensing UI) ------
namespace
{
    double uiFakeNow = 0.0;
    double uiFakeNowFn() { return uiFakeNow; }

    // Every user-visible string in a component tree (labels + text editors),
    // so the assertions read the SAME text the user sees.
    juce::StringArray infoTexts (const juce::Component& root)
    {
        juce::StringArray out;
        if (auto* l = dynamic_cast<const juce::Label*> (&root))
            out.add (l->getText());
        if (auto* te = dynamic_cast<const juce::TextEditor*> (&root))
            out.add (te->getText().isEmpty() ? te->getTextToShowWhenEmpty() : te->getText());
        if (auto* b = dynamic_cast<const juce::Button*> (&root))
            out.add (b->getButtonText());
        if (auto* c = dynamic_cast<const juce::ComboBox*> (&root))
            out.add (c->getText());
        for (int i = 0; i < root.getNumChildComponents(); ++i)
            out.addArray (infoTexts (*root.getChildComponent (i)));
        return out;
    }

    // The first BUTTON whose label contains `text`: lets a test press exactly
    // the control the user presses instead of reaching into the class, so the
    // real click wiring is what gets exercised.
    juce::Button* findButton (juce::Component& root, const juce::String& text)
    {
        if (auto* b = dynamic_cast<juce::Button*> (&root))
            if (b->getButtonText().contains (text))
                return b;

        for (int i = 0; i < root.getNumChildComponents(); ++i)
            if (auto* found = findButton (*root.getChildComponent (i), text))
                return found;

        return nullptr;
    }

    // Deliver pending messages for `ms`: the harness has no host, so editor
    // timers and background notifications only run while the loop is pumped.
    void pumpMessages (int ms)
    {
        const auto deadline = juce::Time::getMillisecondCounter()
                              + (juce::uint32) juce::jmax (0, ms);
        while (juce::Time::getMillisecondCounter() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    // Bounded wait that keeps pumping the message loop, so an asynchronous
    // answer arriving on the auth thread can actually be observed.
    template <typename Predicate>
    bool waitForUi (Predicate pred, int timeoutMs = 5000)
    {
        const auto deadline = juce::Time::getMillisecondCounter()
                              + (juce::uint32) juce::jmax (0, timeoutMs);
        while (juce::Time::getMillisecondCounter() < deadline)
        {
            if (pred())
                return true;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }
        return pred();
    }

    // Substring search across every user-visible string (StringArray::contains
    // is an EXACT element match, which is not what these assertions mean).
    bool infoTextContains (const juce::Component& root, const juce::String& needle)
    {
        for (const auto& t : infoTexts (root))
            if (t.contains (needle))
                return true;
        return false;
    }

    // ---- mock version-check transport (Plugin Versions) ------------------
    // Deterministic and offline. The tests set the server's answer and then
    // drive the REAL checker synchronously, so no assertion depends on thread
    // timing and no socket is ever opened.
    struct MockUpdateTransport : public sid::update::UpdateTransport
    {
        juce::String latestVersion = "0.5.0";
        juce::String releaseNotes;
        juce::String downloadUrl = "https://music-prod.com/pumpcurve";
        // The catalog's own `status` field. "noPublishedRelease" is what the
        // deployed service answers for a product with nothing published, and it
        // is then sent with a NULL latestVersion and updateAvailable false.
        juce::String status = "updateAvailable";
        bool fail = false;

        bool fetch (const juce::String&, const juce::String& currentVersion,
                    const juce::String&, const juce::String&,
                    sid::update::UpdateInfo& out) override
        {
            if (fail)
                return false;

            const bool noPublished = (status == "noPublishedRelease");

            juce::DynamicObject::Ptr data = new juce::DynamicObject();
            if (noPublished)
                data->setProperty ("latestVersion", juce::var());  // null, as the service sends
            else
                data->setProperty ("latestVersion", latestVersion);
            data->setProperty ("updateAvailable", ! noPublished);
            data->setProperty ("releaseNotes", releaseNotes);
            data->setProperty ("publishedAt", "2026-10-13T09:00:00Z");
            data->setProperty ("downloadUrl", downloadUrl);
            data->setProperty ("status", status);
            data->setProperty ("mandatory", false);

            juce::DynamicObject::Ptr envelope = new juce::DynamicObject();
            envelope->setProperty ("success", true);
            envelope->setProperty ("data", juce::var (data.get()));

            return sid::update::parseUpdateResponse (juce::var (envelope.get()), currentVersion, out);
        }
    };

    bool infoHasTextEditorAndButton (const juce::Component& root)
    {
        bool editor = false, button = false;
        for (int i = 0; i < root.getNumChildComponents(); ++i)
        {
            auto* c = root.getChildComponent (i);
            if (dynamic_cast<juce::TextEditor*> (c) != nullptr) editor = true;
            if (dynamic_cast<juce::TextButton*> (c) != nullptr) button = true;
            if (infoHasTextEditorAndButton (*c)) { editor = true; button = true; }
        }
        return editor && button;
    }
}

// Friend accessor for layout/geometry assertions (see PluginEditor.h).
struct SideChainEditorTestAccess
{
    static juce::Rectangle<int> logoBand (SideChainAudioProcessorEditor& e) { return e.logoArea; }
    static juce::Rectangle<int> duckLengthKnobRect (SideChainAudioProcessorEditor& e) { return e.duckLengthKnob.getBounds(); }
    static juce::Rectangle<int> duckLengthValueRect (SideChainAudioProcessorEditor& e) { return e.duckLengthValueLabel.getBounds(); }
    static juce::Rectangle<int> duckLengthCaptionRect (SideChainAudioProcessorEditor& e) { return e.duckLengthCaption.getBounds(); }
    static juce::Rectangle<int> amountKnobRect (SideChainAudioProcessorEditor& e) { return e.amountKnob.getBounds(); }
    static juce::Rectangle<int> releaseKnobRect (SideChainAudioProcessorEditor& e) { return e.releaseKnob.getBounds(); }
    static ProdKnob& amountKnob (SideChainAudioProcessorEditor& e) { return e.amountKnob; }
    static ProdKnob& duckLengthKnob (SideChainAudioProcessorEditor& e) { return e.duckLengthKnob; }
    static ProdKnob& releaseKnob (SideChainAudioProcessorEditor& e) { return e.releaseKnob; }
    static GraphComponent& graph (SideChainAudioProcessorEditor& e) { return e.graph; }
    static juce::ComboBox& presetBox (SideChainAudioProcessorEditor& e) { return e.presetBox; }
    static juce::Rectangle<int> presetPrevRect (SideChainAudioProcessorEditor& e) { return e.presetPrevButton.getBounds(); }
    static juce::Rectangle<int> presetNextRect (SideChainAudioProcessorEditor& e) { return e.presetNextButton.getBounds(); }
    static void setPresetDirectory (SideChainAudioProcessorEditor& e, const juce::File& directory)
    { e.userPresetLibrary = sid::presets::UserPresetLibrary (directory); e.buildPresetMenu(); }
    static juce::TextButton& savePresetButton (SideChainAudioProcessorEditor& e) { return e.savePresetButton; }
    static juce::TextEditor& presetName (SideChainAudioProcessorEditor& e) { return e.presetNameEditor; }
    static void commitPreset (SideChainAudioProcessorEditor& e) { e.confirmPresetButton.onClick(); }
    static juce::Label& presetStatus (SideChainAudioProcessorEditor& e) { return e.presetStatusLabel; }
    static void cancelPreset (SideChainAudioProcessorEditor& e) { e.cancelPresetButton.onClick(); }
    static void openSavePresetForm (SideChainAudioProcessorEditor& e) { e.showSavePresetForm(); }
    static void agePresetStatus (SideChainAudioProcessorEditor& e)
    {
        e.presetStatusShownMs -= SideChainAudioProcessorEditor::kPresetStatusHoldMs + 1000;
        e.timerCallback();
    }
    static juce::Label& amountValueLabel (SideChainAudioProcessorEditor& e) { return e.amountValueLabel; }
    static juce::Label& duckLengthValueLabel (SideChainAudioProcessorEditor& e) { return e.duckLengthValueLabel; }
    static juce::Label& duckLengthCaption (SideChainAudioProcessorEditor& e) { return e.duckLengthCaption; }
    static juce::Slider& smoothSlider (SideChainAudioProcessorEditor& e) { return e.smoothSlider; }
    static juce::ToggleButton& transportGateToggle (SideChainAudioProcessorEditor& e) { return e.transportGateToggle; }
    static juce::Label& smoothCaption (SideChainAudioProcessorEditor& e) { return e.smoothCaption; }
    static juce::Label& smoothValueLabel (SideChainAudioProcessorEditor& e) { return e.smoothValueLabel; }
    static juce::Label& releaseValueLabel (SideChainAudioProcessorEditor& e) { return e.releaseValueLabel; }
    static bool setEditorSize (SideChainAudioProcessorEditor& e, int width, int height)
    {
        e.setSize (width, height);
        return e.getWidth() == width && e.getHeight() == height;
    }
    static void simulateWakeGap (SideChainAudioProcessorEditor& e, bool simulateClockRollback = false)
    {
        e.lastTimerCallbackTimeMs = juce::Time::currentTimeMillis()
                                    + (simulateClockRollback ? 2000 : -2000);
        e.timerCallback();
    }
    // Subscription-refresh surface (0.5.0): the two lifecycle switches the
    // host drives (the editor window coming back, the INFO page being opened)
    // and the page itself.
    static sid::auth::AuthManager& authManager (SideChainAudioProcessorEditor& e) { return e.authManager; }
    static InfoPageComponent& infoPage (SideChainAudioProcessorEditor& e) { return e.infoPage; }
    static void showInfoPage (SideChainAudioProcessorEditor& e) { e.showInfoView(); }
    static void showMainView (SideChainAudioProcessorEditor& e) { e.showMainView(); }
};

// Colour equality within rasteriser rounding (gradients are not bit-exact
// across the software and CoreGraphics renderers).
static bool nearColour (juce::Colour a, juce::Colour b, int tolerance = 2)
{
    return std::abs ((int) a.getRed()   - (int) b.getRed())   <= tolerance
        && std::abs ((int) a.getGreen() - (int) b.getGreen()) <= tolerance
        && std::abs ((int) a.getBlue()  - (int) b.getBlue())  <= tolerance;
}

// Renders a component onto a MAGENTA sentinel field and counts how many
// pixels the component actually painted. Catches the "knob graphics
// invisible" defect class (zero-height bounds, transparent painting, etc.).
static int paintedPixelCount (juce::Component& c)
{
    const auto b = c.getBounds();
    if (b.getWidth() <= 0 || b.getHeight() <= 0) return 0;
    juce::Image img (juce::Image::ARGB, b.getWidth(), b.getHeight(), true);
    {
        // Paint with the component positioned at (0,0) inside its own image
        // (setOrigin(-pos) leaves a null clip when the origin is negative on
        // this JUCE version - position the component via a transform instead).
        juce::Graphics g (img);
        g.fillAll (juce::Colours::magenta);      // sentinel: un-painted stays
        c.paint (g);
    }
    int painted = 0;
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x)
            if (img.getPixelAt (x, y) != juce::Colours::magenta)
                ++painted;
    return painted;
}

// Mock transport mirroring the server contract (approved on first poll).
struct MockTransport : public sid::auth::AuthTransport
{
    sid::auth::StartResponse start (const juce::String&, const juce::String&, const juce::String&) override
    {
        sid::auth::StartResponse r;
        r.ok = true;
        r.deviceCode = "ui-device-code";
        r.userCode = "UIVE-RIFY";
        r.verificationUrl = "https://music-prod.com/plugin/link";
        r.verificationUrlComplete = "https://music-prod.com/plugin/link?code=UIVE-RIFY";
        r.expiresInSeconds = 900;
        r.intervalSeconds = 1;
        return r;
    }
    sid::auth::PollResponse poll (const juce::String&) override
    {
        sid::auth::PollResponse r;
        r.status = "approved";
        r.token = "ui-mock-token";
        r.displayName = "UI Tester";
        return r;
    }
    sid::auth::EntitlementsResponse entitlements (const juce::String& token) override
    {
        ++entitlementsCalls;
        lastEntitlementsToken = token;

        // A transport failure: NO answer arrives (not an "inactive" answer).
        // The plugin must never turn this into a grant OR into a revocation.
        if (entitlementsFail)
        {
            sid::auth::EntitlementsResponse failed;
            return failed;   // ok == false, authFailure == false
        }

        sid::auth::EntitlementsResponse e;
        e.ok = true;
        // Raw membership and the PRODUCT-SCOPED answer are both reported, just
        // as the real service does: the product answer is what the plugin acts on.
        e.subscribed = subscriptionActive;
        e.subscriptionActive = subscriptionActive;
        e.subscriptionCoversProduct = subscriptionActive && coversProduct;
        e.product = answeredProduct;
        e.licenseActive = licenseActive;
        e.licenseStatus = licenseActive ? "active" : licenseStatusString;
        e.productEntitled = (subscriptionActive && coversProduct) || licenseActive;
        return e;
    }
    sid::auth::LicenseResponse activateLicense (const juce::String& token,
                                                const juce::String& licenceKey) override
    {
        ++licenseCalls;
        lastLicenceKey = licenceKey;
        lastLicenceToken = token;
        sid::auth::LicenseResponse r;
        r.product = answeredProduct;
        r.status = licenseStatus;
        r.message = licenseMessage;
        if (licenseStatus == sid::auth::LicenseResponse::Status::ok)
            licenseActive = true;   // the server binds the key to this account
        return r;
    }
    bool logout (const juce::String&) override { return true; }

    // Script knobs (mirror the licence contract in MusicProdAuth.h).
    bool subscriptionActive = false;
    bool coversProduct = true;                 // false: membership not confirmed for this product
    juce::String answeredProduct = "sidechain";// the product the server says the token is for
    bool licenseActive = false;
    juce::String licenseStatusString = "none";
    sid::auth::LicenseResponse::Status licenseStatus =
        sid::auth::LicenseResponse::Status::ok;
    juce::String licenseMessage;
    int licenseCalls = 0;
    juce::String lastLicenceKey;
    juce::String lastLicenceToken;
    juce::String lastEntitlementsToken;

    // Every entitlement REQUEST the plugin made (the tests below count them to
    // tell "asked the server again" from "re-read the cached answer").
    std::atomic<int> entitlementsCalls { 0 };
    bool entitlementsFail = false;
};

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    printf ("SideChain UI verification (0.2.1 fixes)\n");

    // ---- 1. Editor construction + background colour -----------------------
    {
        SideChainAudioProcessor proc;
        SideChainAudioProcessorEditor editor (proc);

        check (editor.getWidth() == 800 && editor.getHeight() == 520,
               "editor size 800x520");

        juce::Image img = editor.createComponentSnapshot ({ 800, 520 }, true, 1.0f);
        check (! img.isNull() && img.getWidth() == 800,
               "editor snapshot rendered");
        if (img.isNull())
        {
            printf ("  [FAIL] snapshot unavailable\n");
            return 1;
        }
        {
            // Dump the editor snapshot once for visual diagnosis (harness dev).
            const char* dumpPath = getenv ("SDCH_DUMP");
            if (dumpPath != nullptr)
            {
                std::unique_ptr<juce::FileOutputStream> out (
                    juce::File (juce::String (dumpPath)).createOutputStream());
                if (out != nullptr)
                {
                    juce::PNGImageFormat png;
                    png.writeImageToStream (img, *out);
                    out->flush();
                }
            }
        }
        // 0.5.0 redesign: graphite theme. The header band and the field use
        // the theme's exact colours (no stray fills, no image box), sampled
        // where no control or text can sit.
        // The field is ONE vertical gradient lit from the top (MP Gain
        // construction): sampled pixels must equal the gradient at their
        // height (+/-2 per channel for rasteriser rounding), and the top must
        // be lighter than the bottom.
        auto px = img.getPixelAt (400, 6); // header, above the preset row
        check (nearColour (px, sid::theme::fieldColourAt (6.0f / 520.0f)),
               "editor header is the top of the field gradient (got " + px.toString().toStdString() + ")");
        auto px2 = img.getPixelAt (6, 510);
        check (nearColour (px2, sid::theme::fieldColourAt (510.0f / 520.0f))
                   && px.getBrightness() > px2.getBrightness(),
               "editor field outside the panels is the field gradient, lit from the top (logo strip)");

        // Logo strip: rendered artwork pixels must be white/neutral and the
        // strip must contain NO red/orange (regression for the HSB-colour
        // mask bug) and no opaque image box. Scanned inside the editor's own
        // logo band, so the check follows the layout instead of pinning it.
        {
            const auto band = SideChainEditorTestAccess::logoBand (editor).expanded (20, 4)
                                  .getIntersection (editor.getLocalBounds());
            int logoPixels = 0, redPixels = 0;
            int minX = 800, maxX = 0, minY = 520, maxY = 0;
            for (int y = band.getY(); y < band.getBottom(); ++y)
                for (int x = band.getX(); x < band.getRight(); ++x)
                {
                    const auto p = img.getPixelAt (x, y);
                    const float r = p.getFloatRed(), gr = p.getFloatGreen(), b = p.getFloatBlue();
                    // Brighter than the graphite field = artwork.
                    if (p.getFloatAlpha() > 0.1f && (r > 0.18f || gr > 0.18f || b > 0.18f))
                    {
                        ++logoPixels;
                        minX = juce::jmin (minX, x); maxX = juce::jmax (maxX, x);
                        minY = juce::jmin (minY, y); maxY = juce::jmax (maxY, y);
                        if (r > gr * 1.4f && r > b * 1.4f && r > 0.1f)
                            ++redPixels;
                    }
                }
            check (logoPixels > 50, "Music-Prod logo artwork is rendered in the band");
            check (redPixels == 0,
                   "logo contains ZERO red/orange pixels (got "
                       + juce::String (redPixels).toStdString() + ")");
            // White-ish artwork: every rendered pixel must be neutral
            // (r ~ g ~ b), never a saturated colour.
            bool neutral = true;
            for (int y = minY; y <= maxY && neutral; ++y)
                for (int x = minX; x <= maxX; ++x)
                {
                    const auto p = img.getPixelAt (x, y);
                    const float r = p.getFloatRed(), gr = p.getFloatGreen(), b = p.getFloatBlue();
                    const float mx = juce::jmax (r, juce::jmax (gr, b));
                    const float mn = juce::jmin (r, juce::jmin (gr, b));
                    if (p.getFloatAlpha() > 0.1f && mx > 0.05f && (mx - mn) > 0.12f)
                    { neutral = false; break; }
                }
            check (neutral, "logo artwork is pure white/neutral (no colourization)");
            // Aspect/size: the asset's white artwork spans x 15..238 of 260 at
            // a 96px draw width -> ~82px of visible artwork, centred on x=400.
            const int drawnW = maxX - minX + 1;
            const int centre = (minX + maxX) / 2;
            check (drawnW > 70 && drawnW <= 98,
                   "logo artwork width proportional to the 96px draw rect (got "
                       + juce::String (drawnW).toStdString() + "px)");
            check (std::abs (centre - 400) <= 6, "logo centred horizontally");
        }

        // PumpCurve wordmark present in the header (left): sample glyph
        // pixels in the wordmark rect.
        {
            int glyphPixels = 0;
            for (int y = 8; y < 44; ++y)
                for (int x = 14; x < 190; ++x)
                {
                    const auto p = img.getPixelAt (x, y);
                    if (p.getFloatAlpha() > 0.2f && p.getFloatRed() > 0.5f)
                        ++glyphPixels;
                }
            check (glyphPixels > 100,
                   "PumpCurve wordmark rendered in header (bright glyph pixels: "
                       + juce::String (glyphPixels).toStdString() + ")");
        }

        // Preset arrows: both triangle glyphs must be visible inside their
        // own buttons (bounds read from the real layout).
        {
            auto litIn = [&] (juce::Rectangle<int> r)
            {
                int n = 0;
                for (int y = r.getY(); y < r.getBottom(); ++y)
                    for (int x = r.getX(); x < r.getRight(); ++x)
                    {
                        const auto p = img.getPixelAt (x, y);
                        if (p.getFloatAlpha() > 0.2f
                            && p.getFloatRed() + p.getFloatGreen() + p.getFloatBlue() > 0.9f)
                            ++n;
                    }
                return n;
            };
            check (litIn (SideChainEditorTestAccess::presetPrevRect (editor)) > 20,
                   "preset prev-arrow glyph rendered");
            check (litIn (SideChainEditorTestAccess::presetNextRect (editor)) > 20,
                   "preset next-arrow glyph rendered");
        }

        // No-signal preview curve: mint pixels present in the plot area while
        // the history is silent.
        {
            int mint = 0;
            for (int y = 120; y < 360; ++y)
                for (int x = 60; x < 740; ++x)
                {
                    const auto p = img.getPixelAt (x, y);
                    if (p.getFloatAlpha() > 0.2f
                        && p.getFloatGreen() > 0.25f
                        && p.getFloatGreen() >= p.getFloatRed()
                        && p.getFloatGreen() > p.getFloatBlue())
                        ++mint;
                }
            check (mint > 50,
               "ducking-curve preview visible while graph is silent (mint px: "
                   + juce::String (mint).toStdString() + ")");

        // ---- Layout defect regression: knobs/value labels/captions must
        // not overlap each other or the Music-Prod logo band ----
        {
            auto rectsOverlap = [] (juce::Rectangle<int> a, juce::Rectangle<int> b)
            {
                return a.intersects (b);
            };
            // Real component bounds from the live editor (not recomputed
            // maths): knobs vs logo band, and value labels vs the band.
            const juce::Rectangle<int> logoBand = SideChainEditorTestAccess::logoBand (editor);
            check (! rectsOverlap (SideChainEditorTestAccess::duckLengthKnobRect (editor), logoBand)
                       && ! rectsOverlap (SideChainEditorTestAccess::duckLengthValueRect (editor), logoBand)
                       && ! rectsOverlap (SideChainEditorTestAccess::duckLengthCaptionRect (editor), logoBand),
                   "TIME knob/value/caption do NOT overlap the Music-Prod logo band");
            check (SideChainEditorTestAccess::smoothSlider (editor).isVisible()
                       && SideChainEditorTestAccess::smoothCaption (editor).getBounds().getHeight() > 0
                       && SideChainEditorTestAccess::smoothSlider (editor).getBounds().getWidth() > 100,
                   "SMOOTH control is visible beneath the graph");
            check (! rectsOverlap (SideChainEditorTestAccess::amountKnobRect (editor), logoBand),
                   "AMOUNT knob does not overlap the logo band");
            check (! rectsOverlap (SideChainEditorTestAccess::releaseKnobRect (editor), logoBand),
                   "RELEASE knob does not overlap the logo band");
            check (! rectsOverlap (SideChainEditorTestAccess::duckLengthKnobRect (editor), SideChainEditorTestAccess::amountKnobRect (editor)),
                   "DUCK LENGTH knob does not overlap AMOUNT knob");
            check (! rectsOverlap (SideChainEditorTestAccess::duckLengthKnobRect (editor), SideChainEditorTestAccess::releaseKnobRect (editor)),
                   "DUCK LENGTH knob does not overlap RELEASE knob");
            // Knobs fully inside the editor.
            const auto all = editor.getLocalBounds();
            check (all.contains (SideChainEditorTestAccess::duckLengthKnobRect (editor))
                       && all.contains (SideChainEditorTestAccess::duckLengthValueRect (editor)),
                   "DUCK LENGTH control fully inside the editor");

            // ---- Knob-rendering regression (the "invisible knobs" defect):
            // all three knobs must have NON-ZERO bounds and actually paint
            // their body/arcs/pointer.
            {
                const auto ak = SideChainEditorTestAccess::amountKnobRect (editor);
                const auto dk = SideChainEditorTestAccess::duckLengthKnobRect (editor);
                const auto rk = SideChainEditorTestAccess::releaseKnobRect (editor);
                check (ak.getWidth() > 0 && ak.getHeight() > 0,
                       "AMOUNT knob bounds are non-zero (got "
                           + juce::String (ak.getWidth()).toStdString() + "x"
                           + juce::String (ak.getHeight()).toStdString() + ")");
                check (dk.getWidth() > 0 && dk.getHeight() > 0,
                       "DUCK LENGTH knob bounds are non-zero (got "
                           + juce::String (dk.getWidth()).toStdString() + "x"
                           + juce::String (dk.getHeight()).toStdString() + ")");
                check (rk.getWidth() > 0 && rk.getHeight() > 0,
                       "RELEASE knob bounds are non-zero (got "
                           + juce::String (rk.getWidth()).toStdString() + "x"
                           + juce::String (rk.getHeight()).toStdString() + ")");

                const int amountPainted = paintedPixelCount (SideChainEditorTestAccess::amountKnob (editor));
                const int duckPainted   = paintedPixelCount (SideChainEditorTestAccess::duckLengthKnob (editor));
                const int releasePainted = paintedPixelCount (SideChainEditorTestAccess::releaseKnob (editor));
                check (amountPainted > 200, "AMOUNT knob renders its graphics ("
                           + juce::String (amountPainted).toStdString() + " px)");
                check (duckPainted > 200, "DUCK LENGTH knob renders its graphics ("
                           + juce::String (duckPainted).toStdString() + " px)");
                check (releasePainted > 200, "RELEASE knob renders its graphics ("
                           + juce::String (releasePainted).toStdString() + " px)");
            }
        }

        // ---- 0.4.2 TRANSPORT SWITCH: ACTIVE ONLY DURING PLAY/REC ----------
        {
            auto& toggle = SideChainEditorTestAccess::transportGateToggle (editor);
            auto* gateParam = dynamic_cast<juce::AudioParameterBool*> (
                proc.getParameters().getParameter ("transportGate"));

            check (toggle.isVisible() && toggle.getBounds().getWidth() > 120
                       && toggle.getBounds().getHeight() > 0,
                   "the ACTIVE ONLY DURING PLAY/REC switch is visible with real bounds");
            check (toggle.getButtonText() == "ACTIVE ONLY DURING PLAY/REC",
                   "the switch carries the requested label");
            check (gateParam != nullptr && gateParam->get() && toggle.getToggleState(),
                   "the switch and its parameter are both ON for a new instance");

            // A real click (the user path through the APVTS attachment).
            toggle.triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
            check (gateParam != nullptr && ! gateParam->get() && ! toggle.getToggleState(),
                   "clicking the switch off writes the parameter");

            // Host automation / state restore moves the control back.
            gateParam->setValueNotifyingHost (1.0f);
            check (toggle.getToggleState(),
                   "automating the parameter moves the switch back on");

            const auto knobTop = SideChainEditorTestAccess::amountKnobRect (editor).getY();
            check (! toggle.getBounds().intersects (SideChainEditorTestAccess::logoBand (editor))
                       && toggle.getBounds().getBottom() < knobTop
                       && editor.getLocalBounds().contains (toggle.getBounds()),
                   "the switch collides with nothing (logo band / knobs / editor edge)");
        }

        // ---- 0.4.2 LIVE WAVEFORM LAYER -----------------------------------
        {
            auto& graph = SideChainEditorTestAccess::graph (editor);
            check (graph.isWaveformDisplayEnabled(),
                   "the live waveform layer is enabled by default");

            // Real audio through the real processor, then the editor's own
            // timer drains the scope FIFO exactly as in production.
            if (auto* amount = dynamic_cast<juce::AudioParameterFloat*> (
                    proc.getParameters().getParameter ("sidechainAmount")))
                amount->setValueNotifyingHost (amount->convertTo0to1 (75.0f));

            proc.prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            std::vector<float> tone ((size_t) 12000, 0.0f);
            for (size_t i = 0; i < tone.size(); ++i)
                tone[i] = 0.8f * (float) std::sin (2.0 * 3.14159265358979 * 220.0
                                                   * (double) i / 48000.0);
            for (int start = 0; start + 512 <= (int) tone.size(); start += 512)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        buffer.getWritePointer (ch)[i] = tone[(size_t) (start + i)];
                proc.processBlock (buffer, midi);
            }
            SideChainEditorTestAccess::simulateWakeGap (editor);   // runs the editor timer
            check (graph.hasDisplayableWaveform(),
                   "real audio produces a displayable waveform in the graph");

            juce::Image withLayer (juce::Image::ARGB, graph.getWidth(), graph.getHeight(), true);
            {
                juce::Graphics g (withLayer);
                graph.paint (g);
            }
            graph.setWaveformDisplayEnabled (false);
            juce::Image withoutLayer (juce::Image::ARGB, graph.getWidth(), graph.getHeight(), true);
            {
                juce::Graphics g (withoutLayer);
                graph.paint (g);
            }
            graph.setWaveformDisplayEnabled (true);

            auto countExact = [] (const juce::Image& image, juce::uint32 argb)
            {
                const auto wanted = juce::Colour (argb);
                int n = 0;
                for (int y = 0; y < image.getHeight(); ++y)
                    for (int x = 0; x < image.getWidth(); ++x)
                        if (image.getPixelAt (x, y) == wanted)
                            ++n;
                return n;
            };

            int changed = 0;
            for (int y = 0; y < withLayer.getHeight(); ++y)
                for (int x = 0; x < withLayer.getWidth(); ++x)
                    if (withLayer.getPixelAt (x, y) != withoutLayer.getPixelAt (x, y))
                        ++changed;
            check (changed > 500,
                   "the waveform layer paints a substantial trace behind the curve ("
                       + juce::String (changed).toStdString() + " px)");

            // Z-order proof 1: the opaque curve stroke must survive the new
            // layer (allow the handful of anti-aliased edge pixels whose blend
            // changes because the curve's translucent fill now sits on top of
            // a trace instead of the bare background).
            const int curveWith = countExact (withLayer, 0xff7fd1c0);
            const int curveWithout = countExact (withoutLayer, 0xff7fd1c0);
            check (curveWith > 200 && curveWith * 100 >= curveWithout * 95,
                   "the curve keeps its opaque stroke pixels above the waveform ("
                       + juce::String (curveWith).toStdString() + " px vs "
                       + juce::String (curveWithout).toStdString() + " px)");

            // Z-order proof 2 (exact): every control point is an opaque disc
            // drawn after the waveform, so the pixel at each point's centre
            // must be byte-identical with and without the layer, and must be
            // that point's own colour.
            {
                const auto curve = graph.getCurve();
                const auto plot = graph.plotBoundsForTest();
                int identical = 0, correct = 0;
                for (std::size_t i = 0; i < curve.size(); ++i)
                {
                    const int px = juce::roundToInt (plot.getX() + (float) curve[i].x * plot.getWidth());
                    const int py = juce::roundToInt (plot.getY() + (float) (1.0 - curve[i].y) * plot.getHeight());
                    const auto withPoint = withLayer.getPixelAt (px, py);
                    if (withPoint == withoutLayer.getPixelAt (px, py))
                        ++identical;
                    if (withPoint == juce::Colour (i == 0 || i + 1 == curve.size()
                                                       ? sid::theme::kText : sid::theme::kAccent))
                        ++correct;
                }
                check (identical == (int) curve.size() && correct == (int) curve.size(),
                       "every control point renders on top of the waveform unchanged ("
                           + juce::String (identical).toStdString() + "/"
                           + juce::String ((int) curve.size()).toStdString()
                           + " identical, " + juce::String (correct).toStdString()
                           + " in the point's own colour)");
            }
        }

        // ---- Lifecycle refresh regression: a sleep-sized wall-clock gap
        // followed by the editor timer must invalidate existing controls,
        // without replacing children or changing attached parameter values.
        {
            const int childCountBefore = editor.getNumChildComponents();
            const auto presetTextBefore = SideChainEditorTestAccess::presetBox (editor).getText();
            const auto amountBefore = SideChainEditorTestAccess::amountKnob (editor).getBounds();
            const auto duckBefore = SideChainEditorTestAccess::duckLengthKnob (editor).getBounds();
            const auto releaseBefore = SideChainEditorTestAccess::releaseKnob (editor).getBounds();
            const auto amountValueBefore = SideChainEditorTestAccess::amountKnob (editor).getValue();
            const auto duckValueBefore = SideChainEditorTestAccess::duckLengthKnob (editor).getValue();
            const auto releaseValueBefore = SideChainEditorTestAccess::releaseKnob (editor).getValue();
            const auto amountParameterBefore = proc.getParameters().getParameter ("sidechainAmount")->getValue();
            const auto duckParameterBefore = proc.getParameters().getParameter ("duckLength")->getValue();
            const auto releaseParameterBefore = proc.getParameters().getParameter ("release")->getValue();

            editor.setVisible (false);
            editor.setVisible (true);
            SideChainEditorTestAccess::simulateWakeGap (editor);

            check (editor.getNumChildComponents() == childCountBefore,
                   "lifecycle refresh does not create duplicate editor children");
            check (SideChainEditorTestAccess::amountKnob (editor).isVisible()
                       && SideChainEditorTestAccess::duckLengthKnob (editor).isVisible()
                       && SideChainEditorTestAccess::releaseKnob (editor).isVisible()
                       && SideChainEditorTestAccess::amountKnob (editor).getParentComponent() == &editor
                       && SideChainEditorTestAccess::duckLengthKnob (editor).getParentComponent() == &editor
                       && SideChainEditorTestAccess::releaseKnob (editor).getParentComponent() == &editor,
                   "all knobs remain visible in the same component hierarchy after hide/show and simulated wake gap");
            check (SideChainEditorTestAccess::graph (editor).isVisible()
                       && SideChainEditorTestAccess::presetBox (editor).isVisible()
                       && SideChainEditorTestAccess::presetBox (editor).getText() == presetTextBefore
                       && paintedPixelCount (SideChainEditorTestAccess::graph (editor)) > 1000,
                   "graph rendering and preset display remain intact after lifecycle refresh");
            check (SideChainEditorTestAccess::amountKnob (editor).getBounds() == amountBefore
                       && SideChainEditorTestAccess::duckLengthKnob (editor).getBounds() == duckBefore
                       && SideChainEditorTestAccess::releaseKnob (editor).getBounds() == releaseBefore,
                   "lifecycle refresh preserves knob layout");
            check (SideChainEditorTestAccess::amountKnob (editor).getValue() == amountValueBefore
                       && SideChainEditorTestAccess::duckLengthKnob (editor).getValue() == duckValueBefore
                       && SideChainEditorTestAccess::releaseKnob (editor).getValue() == releaseValueBefore
                       && proc.getParameters().getParameter ("sidechainAmount")->getValue() == amountParameterBefore
                       && proc.getParameters().getParameter ("duckLength")->getValue() == duckParameterBefore
                       && proc.getParameters().getParameter ("release")->getValue() == releaseParameterBefore,
                   "lifecycle refresh preserves knob values and APVTS parameter state");
            check (SideChainEditorTestAccess::amountValueLabel (editor).getText().isNotEmpty()
                       && SideChainEditorTestAccess::duckLengthValueLabel (editor).getText().isNotEmpty()
                       && SideChainEditorTestAccess::smoothValueLabel (editor).getText().isNotEmpty()
                       && SideChainEditorTestAccess::releaseValueLabel (editor).getText().isNotEmpty(),
                   "all parameter value readouts remain populated after lifecycle refresh");
            check (paintedPixelCount (SideChainEditorTestAccess::graph (editor)) > 1000,
                   "PumpCurve remains rendered after lifecycle refresh");

            auto* amountParam = dynamic_cast<juce::AudioParameterFloat*> (
                proc.getParameters().getParameter ("sidechainAmount"));
            const auto changedAmount = amountValueBefore + 1.0;
            SideChainEditorTestAccess::amountKnob (editor).setValue (
                changedAmount, juce::sendNotificationSync);
            check (std::abs (proc.getParameters().getParameter ("sidechainAmount")->getValue()
                             - amountParam->convertTo0to1 ((float) changedAmount)) < 0.001f,
                   "amount slider attachment still updates the APVTS parameter after refresh");
            SideChainEditorTestAccess::amountKnob (editor).setValue (
                amountValueBefore, juce::sendNotificationSync);

            check (paintedPixelCount (SideChainEditorTestAccess::amountKnob (editor)) > 200
                       && paintedPixelCount (SideChainEditorTestAccess::duckLengthKnob (editor)) > 200
                       && paintedPixelCount (SideChainEditorTestAccess::releaseKnob (editor)) > 200,
                   "all knob painters still render after lifecycle refresh");

            const auto refreshedEditorImage = editor.createComponentSnapshot (
                editor.getLocalBounds(), true, 1.0f);
            auto hasNonBlackPixels = [&] (juce::Rectangle<int> bounds)
            {
                for (int y = bounds.getY(); y < bounds.getBottom(); ++y)
                    for (int x = bounds.getX(); x < bounds.getRight(); ++x)
                        if (refreshedEditorImage.getPixelAt (x, y) != juce::Colour (0xff000000))
                            return true;
                return false;
            };
            check (hasNonBlackPixels (SideChainEditorTestAccess::amountKnobRect (editor))
                       && hasNonBlackPixels (SideChainEditorTestAccess::duckLengthKnob (editor).getBounds())
                       && hasNonBlackPixels (SideChainEditorTestAccess::releaseKnobRect (editor)),
                   "full editor snapshot contains all three knob graphics after lifecycle refresh");

            SideChainEditorTestAccess::simulateWakeGap (editor, true);
            check (SideChainEditorTestAccess::amountKnob (editor).getBounds() == amountBefore
                       && SideChainEditorTestAccess::duckLengthKnob (editor).getBounds() == duckBefore
                       && SideChainEditorTestAccess::releaseKnob (editor).getBounds() == releaseBefore
                       && paintedPixelCount (SideChainEditorTestAccess::amountKnob (editor)) > 200,
                   "clock rollback (sleep counter reset) also triggers safe lifecycle refresh");
        }

        // Each lifecycle hook should reach the same harmless refresh path.
        {
            const int childCountBefore = editor.getNumChildComponents();
            editor.moved();
            editor.visibilityChanged();
            editor.parentHierarchyChanged();
            editor.setScaleFactor (1.0f);
            check (editor.getNumChildComponents() == childCountBefore
                       && SideChainEditorTestAccess::amountKnob (editor).getBounds().getWidth() > 0
                       && SideChainEditorTestAccess::duckLengthKnob (editor).getBounds().getWidth() > 0
                       && SideChainEditorTestAccess::releaseKnob (editor).getBounds().getWidth() > 0,
                   "move/visibility/hierarchy/scale callbacks refresh existing control layout safely");
        }

        // Leave the unit-test editor at its original identity scale for later checks.
        editor.setScaleFactor (1.0f);
        }
    }

    // ---- 1b. PumpCurve graph editing, constraints, persistence, controls --
    {
        SideChainAudioProcessor proc;
        SideChainAudioProcessorEditor editor (proc);
        auto& graph = SideChainEditorTestAccess::graph (editor);
        auto plot = graph.plotBoundsForTest();
        auto curveEquals = [] (const sid::curve::PumpCurve& a, const sid::curve::PumpCurve& b)
        {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (a[i].x != b[i].x || a[i].y != b[i].y) return false;
            return true;
        };
        check (graph.getCurve().size() == 6 && paintedPixelCount (graph) > 1000,
               "default PumpCurve renders in the primary graph");
        check (graph.getWidth() > 300 && graph.getHeight() > 120
                   && plot.getWidth() > 250.0f && plot.getHeight() > 70.0f,
               "curve plot uses a large deterministic resize-safe graph area");

        const auto defaultCurve = graph.getCurve();
        const auto leftEnd = juce::Point<float> (plot.getX(), plot.getY());
        check (graph.hitTestPoint (leftEnd) == 0
                   && ! graph.selectPointAt ({ plot.getX() + plot.getWidth() * 0.5f,
                                               plot.getY() + plot.getHeight() * 0.5f }),
               "point hit-testing distinguishes handles from empty graph area");
        graph.selectPointAt ({ plot.getX() + (float) defaultCurve[2].x * plot.getWidth(),
                               plot.getY() + (float) (1.0 - defaultCurve[2].y) * plot.getHeight() });
        const int selectedInterior = graph.getSelectedPointIndex();
        check (selectedInterior == 2, "interior curve control point can be selected");
        auto renderGraph = [&]
        {
            juce::Image image (juce::Image::ARGB, graph.getWidth(), graph.getHeight(), true);
            juce::Graphics graphics (image);
            graph.paint (graphics);
            return image;
        };
        graph.selectPointAt ({ -50.0f, -50.0f });
        const auto unselectedImage = renderGraph();
        graph.selectPointAt ({ plot.getX() + (float) defaultCurve[2].x * plot.getWidth(),
                               plot.getY() + (float) (1.0 - defaultCurve[2].y) * plot.getHeight() });
        const auto selectedImage = renderGraph();
        const auto selectionCentre = graph.getCurve()[2];
        const int haloX = juce::roundToInt (plot.getX() + (float) selectionCentre.x * plot.getWidth()) + 8;
        const int haloY = juce::roundToInt (plot.getY() + (float) (1.0 - selectionCentre.y) * plot.getHeight());
        check (unselectedImage.getPixelAt (haloX, haloY) != selectedImage.getPixelAt (haloX, haloY),
               "selected control point has distinct visible feedback");

        if (selectedInterior == 2)
        {
            const double spacing = sid::curve::PumpCurve::kMinimumSpacing;
            const auto before = graph.getCurve();
            const bool moved = graph.moveSelectedPointTo (1.0, -0.25);
            const auto& movedCurve = graph.getCurve();
            check (moved && movedCurve[2].x <= movedCurve[3].x - spacing + 1.0e-12
                       && movedCurve[2].x > before[2].x
                       && movedCurve[2].y == 0.0,
                   "interior drag clamps x to neighbor spacing and y to normalized range");
            check (graph.getCurveStateMode() == sid::curve::StateMode::pumpCurve,
                   "interior curve edit updates persisted processor curve state");
        }
        const auto endpointBefore = graph.getCurve()[0];
        graph.selectPointAt ({ plot.getX(), plot.getY() });
        check (graph.getSelectedPointIndex() == 0
                   && ! graph.moveSelectedPointTo (0.8, 0.3)
                   && graph.getCurve()[0].x == endpointBefore.x
                   && graph.getCurve()[0].y == endpointBefore.y
                   && ! graph.deleteSelectedPoint(),
               "endpoint is selectable but cannot move or delete");

        const auto current = graph.getCurve();
        const auto curveBeforeInsert = graph.getCurve();
        const double insertX = (current[1].x + current[2].x) * 0.5;
        const double insertY = 0.42;
        const bool inserted = graph.insertPointAt (insertX, insertY);
        check (inserted && graph.getCurve().size() == current.size() + 1
                   && graph.getSelectedPointIndex() == 2,
               "insertion adds a selected point in sorted order");
        if (inserted)
        {
            check (graph.deleteSelectedPoint() && curveEquals (graph.getCurve(), curveBeforeInsert),
                   "deleting selected interior point restores the prior valid curve");
        }
        const auto beforeInvalid = graph.getCurve();
        check (! graph.insertPointAt (beforeInvalid[1].x + 0.5 * sid::curve::PumpCurve::kMinimumSpacing, 0.5)
                   && ! graph.insertPointAt (std::numeric_limits<double>::quiet_NaN(), 0.5)
                   && curveEquals (graph.getCurve(), beforeInvalid),
               "invalid and too-close point insertions are rejected without mutation");

        // Exercise 2/16 point limits on-screen and reject point 17.
        const sid::curve::Point twoPoints[] = {{ 0.0, 1.0 }, { 1.0, 1.0 }};
        sid::curve::PumpCurve twoPointCurve;
        check (twoPointCurve.trySetPoints (twoPoints, 2), "two-point graph fixture is valid");
        graph.setCurve (twoPointCurve);
        check (graph.getCurve().size() == 2 && paintedPixelCount (graph) > 1000,
               "graph remains rendered with the minimum two points");
        sid::curve::Point sixteenPoints[16] {};
        for (int i = 0; i < 16; ++i)
            sixteenPoints[i] = { (double) i / 15.0, (double) (i % 3) / 2.0 };
        sixteenPoints[0].y = sixteenPoints[15].y = 1.0;
        sid::curve::PumpCurve sixteenPointCurve;
        check (sixteenPointCurve.trySetPoints (sixteenPoints, 16), "sixteen-point graph fixture is valid");
        graph.setCurve (sixteenPointCurve);
        check (graph.getCurve().size() == 16 && ! graph.insertPointAt (0.5, 0.5),
               "graph supports 16 points and rejects a seventeenth");

        // Reset is a visible button and a deterministic state/model operation.
        graph.resetButtonForTest().onClick();
        check (curveEquals (graph.getCurve(), sid::curve::PumpCurve())
                   && curveEquals (proc.getPumpCurve(), sid::curve::PumpCurve()),
               "RESET restores and persists the deterministic default curve");

        // Editing survives processor state serialization and reload.
        auto persisted = graph.getCurve();
        graph.selectPointAt ({ plot.getX() + (float) persisted[1].x * plot.getWidth(),
                               plot.getY() + (float) (1.0 - persisted[1].y) * plot.getHeight() });
        const bool edited = graph.moveSelectedPointTo (persisted[1].x + 0.03, persisted[1].y - 0.05);
        const auto editedCurve = graph.getCurve();
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        SideChainAudioProcessor reloaded;
        reloaded.setStateInformation (state.getData(), (int) state.getSize());
        check (edited && curveEquals (graph.getCurve(), reloaded.getPumpCurve()),
               "edited curve survives save and state reload");

        // TIME/SMOOTH captions and attachments retain their expected IDs.
        auto* timeParam = proc.getParameters().getParameter ("duckLength");
        auto* smoothParam = proc.getParameters().getParameter ("smooth");
        auto& timeKnob = SideChainEditorTestAccess::duckLengthKnob (editor);
        timeKnob.setValue (400.0, juce::sendNotificationSync);
        check (timeParam != nullptr && smoothParam != nullptr
                   && SideChainEditorTestAccess::duckLengthCaption (editor).getText() == "TIME"
                   && SideChainEditorTestAccess::smoothCaption (editor).getText() == "SMOOTH"
                   && std::abs (timeParam->getValue() - timeParam->convertTo0to1 (400.0f)) < 0.001f,
               "TIME keeps duckLength attachment and SMOOTH uses the smooth parameter");
        SideChainEditorTestAccess::smoothSlider (editor).setValue (72.0, juce::sendNotificationSync);
        check (std::abs (smoothParam->getValue() - smoothParam->convertTo0to1 (72.0f)) < 0.001f,
               "SMOOTH slider attachment updates only the smooth parameter");
        auto* releaseParam = proc.getParameters().getParameter ("release");
        const float releaseBefore = releaseParam->getValue();

        // Resize and hide/show/wake retain the edited curve and repaint path.
        SideChainEditorTestAccess::setEditorSize (editor, 900, 600);
        const auto resizedPlot = graph.plotBoundsForTest();
        const bool resizedBounds = graph.getWidth() > 500 && graph.getHeight() > 200
            && resizedPlot.getWidth() > plot.getWidth();
        editor.setVisible (false);
        editor.setVisible (true);
        SideChainEditorTestAccess::simulateWakeGap (editor);
        const bool releaseWasNotRepurposed = std::abs (releaseParam->getValue() - releaseBefore) < 1.0e-6f;
        check (resizedBounds && curveEquals (graph.getCurve(), editedCurve)
                   && paintedPixelCount (graph) > 1000
                   && std::abs (releaseParam->getValue() - releaseBefore) < 1.0e-6f,
               "resize and lifecycle repaint preserve curve and legacy Release parameter");
    }

    // ---- 2/3. INFO page controls in default (signed-out) state ------------
    {
        auto authPath = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("sdch_ui_verify_auth.json");
        authPath.deleteFile();

        sid::auth::AuthManager authMgr (
            std::unique_ptr<sid::auth::AuthTransport> (new MockTransport()),
            authPath.getFullPathName(), "sidechain", "SideChain", "0.2.1");
        // Background polling deliberately OFF here; section 4 covers it.

        sid::license::DemoGate demoGateForInfo;

        // Version check: a mock transport, driven once BEFORE the page exists
        // so the page's own refresh() cannot start a background request during
        // this assertion (see the interval note in UpdateCheck.h).
        sid::update::UpdateChecker updChecker (
            std::unique_ptr<sid::update::UpdateTransport> (new MockUpdateTransport()),
            "sidechain", "0.2.1", "macos", "arm64");
        updChecker.setCheckIntervalSecondsForTesting (1.0e9);
        updChecker.checkNowForTesting();

        InfoPageComponent info (authMgr, demoGateForInfo, updChecker, "0.2.1");
        info.setBounds (0, 0, 800, 520);

        juce::Component host;
        host.addAndMakeVisible (info);
        host.setBounds (0, 0, 800, 520);

        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

        check (info.getWidth() == 800 && info.getHeight() == 520,
               "INFO page laid out at full editor size");
        // 0.5.0 adds the licensing block (heading, version-mode status, detail
        // line, key field, ACTIVATE, licence status) to the 8 account controls;
        // the version check adds the UPDATES block (heading, installed version,
        // latest version, status, detail, action button); the
        // subscription-refresh fix adds the account section's CHECK
        // SUBSCRIPTION control.
        check (info.getNumChildComponents() == 21,
               "INFO page has 21 controls (8 account + 6 licensing + 6 updates + CHECK SUBSCRIPTION) (got "
                   + juce::String (info.getNumChildComponents()).toStdString() + ")");

        // Signed out (as here) the account line folds into the headline, so
        // exactly one control - that now-empty label - takes no space.
        int visible = 0, collapsedEmptyLabels = 0;
        for (auto* c : info.getChildren())
        {
            if (c->isVisible() && ! c->getBounds().isEmpty())
                ++visible;
            else if (auto* l = dynamic_cast<juce::Label*> (c); l != nullptr && l->getText().isEmpty())
                ++collapsedEmptyLabels;
        }
        check (visible == 20 && collapsedEmptyLabels == 1,
               "signed out: 20 INFO controls laid out, the empty account line collapsed (got "
                   + juce::String (visible).toStdString() + " + " + juce::String (collapsedEmptyLabels).toStdString() + ")");

        // The count is meaningful only with the named sections present.
        check (infoTextContains (info, "LICENCE")
                   && infoTextContains (info, "MUSIC-PROD+")
                   && infoTextContains (info, "UPDATES"),
               "INFO page carries the LICENCE, UPDATES and MUSIC-PROD+ sections");

        juce::Image img (juce::Image::ARGB, 800, 520, true);
        {
            juce::Graphics g (img);
            info.paint (g);
        }
        auto px = img.getPixelAt (400, 510);
        check (nearColour (px, sid::theme::fieldColourAt (510.0f / 520.0f)),
               "INFO page uses the same field gradient as the main view");

        // SideChainer wordmark rendered on the INFO page: sample bright
        // glyph pixels in the top-left region where the wordmark is drawn.
        {
            int glyphPixels = 0;
            for (int y = 4; y < 50; ++y)            // header band
                for (int x = 100; x < 320; ++x)     // right of BACK
                {
                    const auto p = img.getPixelAt (x, y);
                    if (p.getFloatAlpha() > 0.2f && p.getFloatRed() > 0.5f)
                        ++glyphPixels;
                }
            check (glyphPixels > 100,
                   "PumpCurve wordmark rendered on INFO page header (glyph pixels: "
                       + juce::String (glyphPixels).toStdString() + ")");
        }

        authPath.deleteFile();
    }

    // ---- 4. Production background polling (the manual-test defect) --------
    {
        auto authPath = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("sdch_ui_verify_auth2.json");
        authPath.deleteFile();

        sid::auth::AuthManager authMgr (
            std::unique_ptr<sid::auth::AuthTransport> (new MockTransport()),
            authPath.getFullPathName(), "sidechain", "SideChain", "0.2.1");
        authMgr.setBackgroundPolling (true);   // production setting

        sid::license::DemoGate demoGateForInfo;

        // Version check: a mock transport, driven once BEFORE the page exists
        // so the page's own refresh() cannot start a background request during
        // this assertion (see the interval note in UpdateCheck.h).
        sid::update::UpdateChecker updChecker (
            std::unique_ptr<sid::update::UpdateTransport> (new MockUpdateTransport()),
            "sidechain", "0.2.1", "macos", "arm64");
        updChecker.setCheckIntervalSecondsForTesting (1.0e9);
        updChecker.checkNowForTesting();

        InfoPageComponent info (authMgr, demoGateForInfo, updChecker, "0.2.1");
        info.setBounds (0, 0, 800, 520);

        authMgr.startLinking();
        check (authMgr.getState() == sid::auth::AuthManager::State::linking,
               "startLinking -> LINKING (WAITING FOR APPROVAL)");

        // No pollOnceForTesting() call: the background poller must do it.
        const auto deadline = juce::Time::getMillisecondCounter() + 10000;
        while (authMgr.getState() == sid::auth::AuthManager::State::linking
               && juce::Time::getMillisecondCounter() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        check (authMgr.getState() == sid::auth::AuthManager::State::signedIn,
               "background approval -> SIGNED IN without any manual poll");
        check (authMgr.getDisplayName() == "UI Tester",
               "display name delivered from approval");

        info.refresh();
        authMgr.logout();
        authPath.deleteFile();
    }

    // ---- 4b. Demo session + licence UI (0.5.0) ---------------------------
    {
        auto authPath = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("sdch_ui_verify_licence.json");
        authPath.deleteFile();

        // MockTransport reports subscribed=false, so this starts where a user
        // with no entitlement starts: demo mode. Licence answers are scripted
        // through the same contract the real service implements.
        MockTransport* authMock = new MockTransport();
        sid::auth::AuthManager authMgr (
            std::unique_ptr<sid::auth::AuthTransport> (authMock),
            authPath.getFullPathName(), "sidechain", "SideChain", "0.5.0");

        // Controllable monotonic clock: the 15-minute session and its expiry
        // are exercised without waiting 15 minutes.
        sid::license::setMonotonicMillisecondsForTest (&uiFakeNowFn);
        uiFakeNow = 1'000'000.0;

        // Version check, on its own controllable state machine.
        MockUpdateTransport* updMock = new MockUpdateTransport();
        sid::update::UpdateChecker updChecker (
            std::unique_ptr<sid::update::UpdateTransport> (updMock),
            "sidechain", "0.5.0", "macos", "arm64");
        updChecker.setCheckIntervalSecondsForTesting (1.0e9);

        sid::license::DemoGate demoGate;
        InfoPageComponent info (authMgr, demoGate, updChecker, "0.5.0");
        info.setBounds (0, 0, 800, 520);
        info.refresh();

        check (infoTextContains (info, "DEMO - 15:00 REMAINING"),
               "INFO shows the demo session as DEMO - MM:SS REMAINING at 15:00");

        // Five minutes in.
        uiFakeNow += 300.0 * 1000.0;
        info.refresh();
        check (infoTextContains (info, "DEMO - 10:00 REMAINING"),
               "the demo countdown reads DEMO - 10:00 REMAINING after 5 minutes");

        // Past the limit: the page must explain what happened and what to do.
        uiFakeNow += 601.0 * 1000.0;
        info.refresh();
        check (infoTextContains (info, "DEMO EXPIRED"),
               "INFO reports DEMO EXPIRED once the session ends");
        check (infoTextContains (info, "Restart the PumpCurve instance"),
               "INFO tells the user to restart the instance for another 15 minutes");

        // Full version (a verified entitlement) replaces the countdown.
        demoGate.setFullVersion (true);
        info.refresh();
        check (infoTextContains (info, "FULL VERSION"),
               "INFO reports FULL VERSION when the entitlement is verified");
        check (! infoTextContains (info, "DEMO EXPIRED"),
               "the demo expiry notice is not shown to an entitled user");

        // Withdrawing it returns to a FRESH demo session, not silence.
        demoGate.setFullVersion (false);
        info.refresh();
        check (infoTextContains (info, "DEMO - 15:00 REMAINING"),
               "losing the entitlement shows a fresh 15:00 demo session");

        // Licence-key route: the UI must carry the entry controls and state
        // the real answer instead of faking success.
        check (infoHasTextEditorAndButton (info),
               "INFO offers the licence-key field and its ACTIVATE control");

        // Signed OUT: a key is bound to an account, so the UI must ask for the
        // sign-in first rather than reporting the key as rejected or accepted.
        authMgr.activateLicense ("PC-ABCD-EFGH-JKLM-NPQR");
        info.refresh();
        check (infoTextContains (info, "SIGN IN TO USE A LICENCE KEY"),
               "a key entered while signed out asks the user to sign in first");
        check (! infoTextContains (info, "LICENCE ACTIVE"),
               "...and is never shown as ACTIVE");
        check (authMock->licenseCalls == 0,
               "...and no licence request was sent without an account");

        // Signed IN with a valid key: the licence row reports the full version.
        authMgr.startLinking();
        authMgr.pollOnceForTesting();
        authMgr.refreshEntitlementsForTesting();
        authMock->licenseStatus = sid::auth::LicenseResponse::Status::ok;
        authMgr.activateLicenseForTesting ("PC-ABCD-EFGH-JKLM-NPQR");
        info.refresh();
        check (infoTextContains (info, "LICENCE ACTIVE - FULL VERSION"),
               "a verified key is shown as LICENCE ACTIVE - FULL VERSION");
        check (authMock->lastLicenceToken.isNotEmpty(),
               "the key was sent with the account token");

        // A rejection from the server is shown as such (and grants nothing).
        authMock->licenseStatus = sid::auth::LicenseResponse::Status::invalid;
        authMock->licenseActive = false;
        authMgr.activateLicenseForTesting ("PC-ABCD-EFGH-JKLM-NPQR");
        info.refresh();
        check (infoTextContains (info, "LICENCE KEY NOT ACCEPTED"),
               "a rejected key is reported as NOT ACCEPTED");
        check (! infoTextContains (info, "LICENCE ACTIVE"),
               "...and is never shown as ACTIVE");

        // ---- UPDATES section (Plugin Versions) ---------------------------
        // The installed version is the version COMPILED INTO this build; the
        // latest version comes from the release catalog. Driven through the real
        // checker with a mock transport, so nothing here depends on timing or on
        // the network being reachable.
        check (infoTextContains (info, "INSTALLED VERSION 0.5.0"),
               "INFO shows the version actually compiled into the plugin");

        updMock->latestVersion = "0.5.1";
        updMock->releaseNotes = "Fixes an audible click on the first beat";
        updChecker.checkNowForTesting();
        info.refresh();
        check (infoTextContains (info, "LATEST VERSION 0.5.1"),
               "INFO shows the latest PUBLISHED version reported by the release catalog");
        check (infoTextContains (info, "UPDATE AVAILABLE"),
               "INFO reports UPDATE AVAILABLE when a newer version is published");
        check (infoTextContains (info, "Fixes an audible click"),
               "INFO carries brief release information for the offered update");
        check (infoTextContains (info, "DOWNLOAD UPDATE"),
               "the update action offers the official download page");

        // Up to date: the plugin is not nagged, and the action stays available.
        updMock->latestVersion = "0.5.0";
        updChecker.checkNowForTesting();
        info.refresh();
        check (infoTextContains (info, "UP TO DATE"),
               "INFO reports UP TO DATE when the installed version is the latest published one");
        check (! infoTextContains (info, "UPDATE AVAILABLE"),
               "an up-to-date plugin is not told that an update is available");

        // NOTHING PUBLISHED at all: the catalog says so (status
        // "noPublishedRelease" with a null latestVersion) and the page must say
        // so too. It must NEVER show UP TO DATE here - that claims the installed
        // build is the latest published version when nothing is published, which
        // is the exact contradiction this test exists to prevent.
        updMock->status = "noPublishedRelease";
        updChecker.checkNowForTesting();
        info.refresh();
        check (infoTextContains (info, "NO PUBLISHED RELEASE"),
               "with nothing published, INFO says NO PUBLISHED RELEASE");
        check (! infoTextContains (info, "UP TO DATE"),
               "...and never UP TO DATE, which would claim a comparison that cannot be made");
        check (infoTextContains (info, "LATEST VERSION -"),
               "the latest-version line shows no version rather than an invented one");
        check (! infoTextContains (info, "UPDATE AVAILABLE"),
               "and no update is claimed either");
        check (! infoTextContains (info, "DOWNLOAD UPDATE"),
               "and no download is offered");

        updMock->status = "updateAvailable";

        // A newer MINOR version must be detected (0.10.0 > 0.5.0): the comparison
        // is numeric per component, never lexicographic.
        updMock->latestVersion = "0.10.0";
        updChecker.checkNowForTesting();
        info.refresh();
        check (infoTextContains (info, "UPDATE AVAILABLE")
                   && infoTextContains (info, "LATEST VERSION 0.10.0"),
               "a numerically newer version is detected (0.10.0 after 0.5.0)");

        // Unreachable server: report it, offer a retry, and change NOTHING else.
        updMock->fail = true;
        updChecker.checkNowForTesting();
        info.refresh();
        check (infoTextContains (info, "UNABLE TO CHECK"),
               "an unreachable update server is reported as UNABLE TO CHECK");
        check (infoTextContains (info, "TRY AGAIN"),
               "a failed update check offers a manual retry");
        check (! infoTextContains (info, "UPDATE AVAILABLE"),
               "a failed update check never claims an update is available");

        // ...and a failed check must not disturb the licensing state: the demo
        // session and the full version both still render from the demo gate.
        demoGate.setFullVersion (true);
        info.refresh();
        check (infoTextContains (info, "FULL VERSION"),
               "a failed update check leaves the licensing state untouched");
        demoGate.setFullVersion (false);
        info.refresh();
        check (infoTextContains (info, "DEMO - 15:00 REMAINING"),
               "a failed update check leaves the demo session running");

        // Geometry: the added licensing block must not overflow the page.
        int maxBottom = 0, maxRight = 0;
        for (int i = 0; i < info.getNumChildComponents(); ++i)
        {
            const auto b = info.getChildComponent (i)->getBounds();
            maxBottom = juce::jmax (maxBottom, b.getBottom());
            maxRight  = juce::jmax (maxRight,  b.getRight());
        }
        check (maxBottom <= info.getHeight() && maxRight <= info.getWidth(),
               "every INFO/licensing control fits inside the 800x520 page");

        sid::license::resetMonotonicMillisecondsForTest();
        authPath.deleteFile();
    }

    // ---- 4c. MUSIC-PROD+ refresh from the REAL editor (0.5.0) ------------
    //
    // The acceptance case this exists for: a customer who buys Music-Prod+
    // while already signed in must be able to unlock the RUNNING instance from
    // the INFO page - no log-out, no log-in, no plugin or DAW restart. The
    // refresh control is pressed through its real click wiring (a Button found
    // by the label the user reads), not by calling the handler directly.
    {
        auto authPath = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("sdch_ui_verify_refresh.json");
        authPath.deleteFile();

        sid::license::setMonotonicMillisecondsForTest (&uiFakeNowFn);
        uiFakeNow = 60'000'000.0;

        MockTransport* authMock = new MockTransport();
        authMock->subscriptionActive = false;   // the account has no Music-Prod+ yet
        SideChainAudioProcessor proc (
            std::unique_ptr<sid::auth::AuthTransport> (authMock),
            authPath.getFullPathName());

        SideChainAudioProcessorEditor editor (proc);
        editor.setSize (800, 520);

        auto& auth = SideChainEditorTestAccess::authManager (editor);
        auto& page = SideChainEditorTestAccess::infoPage (editor);

        // Sign in through the real device-code machine, as a customer would:
        // start the link, the browser approves, the plugin picks it up.
        auth.startLinking();
        auth.pollOnceForTesting();
        auth.refreshEntitlementsForTesting();   // server: not subscribed
        pumpMessages (300);                     // let the editor's own timer settle
        SideChainEditorTestAccess::showInfoPage (editor);
        page.refresh();
        check (waitForUi ([&] { return ! auth.isEntitlementCheckInFlight(); }),
               "the check requested when the editor appeared has finished");
        pumpMessages (200);

        check (page.getBounds() == editor.getLocalBounds(),
               "the INFO page covers the whole editor (no second window)");

        auto* refreshButton = findButton (editor, "CHECK SUBSCRIPTION");
        check (refreshButton != nullptr,
               "the INFO page carries a CHECK SUBSCRIPTION control");
        if (refreshButton != nullptr)
        {
            check (refreshButton->isVisible() && ! refreshButton->getBounds().isEmpty(),
                   "the refresh control is visible and laid out on the page");
            check (refreshButton->getBounds().getRight() <= page.getWidth()
                       && refreshButton->getBounds().getBottom() <= page.getHeight(),
                   "the refresh control fits inside the page");
            check (refreshButton->isEnabled(),
                   "with a signed-in account and no check running, it is ready to press");
        }

        check (infoTextContains (editor, "SIGNED IN AS UI Tester"),
               "the page names the signed-in account");
        check (infoTextContains (editor, "MUSIC-PROD+ NOT ACTIVE"),
               "a signed-in account with no Music-Prod+ is reported as NOT ACTIVE");
        check (infoTextContains (editor, "press CHECK SUBSCRIPTION"),
               "...and the page says exactly what to do about it");
        check (! proc.getDemoGate().isFullVersion(),
               "...while the instance is still the demo");

        // ---- the purchase, pressed from the page --------------------------
        const int before = authMock->entitlementsCalls.load();
        authMock->subscriptionActive = true;    // bought Music-Prod+ in the browser
        const int beforeCheck = authMock->entitlementsCalls.load();
        if (refreshButton != nullptr)
            refreshButton->triggerClick();
        check (waitForUi ([&] { return proc.getDemoGate().isFullVersion(); }),
               "pressing CHECK SUBSCRIPTION unlocks the running instance");
        page.refresh();
        check (authMock->entitlementsCalls.load() > beforeCheck,
               "...by asking Music-Prod again, not by re-reading local state");
        check (authMock->entitlementsCalls.load() > before,
               "...which is a REAL request");
        check (proc.getAuthManager().fullVersionViaSubscription(),
               "...and the SUBSCRIPTION is named as the route");
        check (infoTextContains (editor, "MUSIC-PROD+ ACTIVE")
                   && infoTextContains (editor, "FULL VERSION"),
               "...the page reports MUSIC-PROD+ ACTIVE and FULL VERSION");
        check (! infoTextContains (editor, "REMAINING")
                   && ! infoTextContains (editor, "DEMO EXPIRED"),
               "...the demo countdown is gone from the page");
        check (auth.getState() == sid::auth::AuthManager::State::signedIn,
               "no log-out and no log-in was needed");

        // ---- a failed check: never a grant, and always retryable ----------
        authMock->subscriptionActive = false;
        auth.refreshEntitlementsForTesting();            // confirmed inactive again
        page.refresh();
        check (! proc.getDemoGate().isFullVersion()
                   && infoTextContains (editor, "MUSIC-PROD+ ENDED - DEMO VERSION")
                   && ! infoTextContains (editor, "MUSIC-PROD+ ACTIVE"),
               "an inactive answer withdraws the full version and says the membership ENDED");
        check (infoTextContains (editor, "membership has ended"),
               "...and the page explains that the demo is back because the membership ended");

        authMock->subscriptionActive = true;             // the server would say yes
        authMock->entitlementsFail = true;               // ...but never answers
        if (refreshButton != nullptr)
            refreshButton->triggerClick();
        check (waitForUi ([&] { return ! auth.isEntitlementCheckInFlight(); }),
               "the checking state clears when the request fails");
        page.refresh();
        check (! proc.getDemoGate().isFullVersion(),
               "a network failure never unlocks the full version");
        check (infoTextContains (editor, "MUSIC-PROD+ ENDED - DEMO VERSION")
                   && ! infoTextContains (editor, "MUSIC-PROD+ ACTIVE"),
               "...and the page never claims the subscription is active");
        check (refreshButton != nullptr && refreshButton->isEnabled(),
               "...and the control is usable again so the user can retry");

        authMock->entitlementsFail = false;
        if (refreshButton != nullptr)
            refreshButton->triggerClick();
        check (waitForUi ([&] { return proc.getDemoGate().isFullVersion(); }),
               "the retry after a failed check unlocks the instance");

        // ---- automatic refresh, and its debounce ---------------------------
        {
            // The press above recorded an attempt; a lifecycle event straight
            // after it must not become another request.
            const int afterManual = authMock->entitlementsCalls.load();
            SideChainEditorTestAccess::showMainView (editor);
            SideChainEditorTestAccess::showInfoPage (editor);   // page reopened
            pumpMessages (250);
            check (authMock->entitlementsCalls.load() == afterManual,
                   "reopening the INFO page inside the debounce sends no request");

            // Once the interval has passed it DOES re-check, so a purchase made
            // while the plugin sat idle is picked up without a press.
            auth.backdateEntitlementAttemptForTesting (120.0 * 1000.0);
            SideChainEditorTestAccess::showMainView (editor);
            const int beforeOpen = authMock->entitlementsCalls.load();
            SideChainEditorTestAccess::showInfoPage (editor);
            check (waitForUi ([&] { return authMock->entitlementsCalls.load() > beforeOpen; }),
                   "opening the INFO page re-checks the subscription once it is due");

            // The same holds for the editor window coming back after the host
            // closed it: another lifecycle point, with the same debounce.
            auth.backdateEntitlementAttemptForTesting (120.0 * 1000.0);
            const int beforeShow = authMock->entitlementsCalls.load();
            editor.setVisible (false);
            editor.setVisible (true);
            check (waitForUi ([&] { return authMock->entitlementsCalls.load() > beforeShow; }),
                   "showing the editor again re-checks the subscription once it is due");

            editor.setVisible (false);
            editor.setVisible (true);
            pumpMessages (250);
            check (authMock->entitlementsCalls.load() == beforeShow + 1,
                   "repeatedly showing the editor inside the debounce is not a request each time");
        }

        // ---- the demo has already expired ---------------------------------
        authMock->subscriptionActive = false;
        auth.refreshEntitlementsForTesting();    // server: inactive again
        uiFakeNow += 901.0 * 1000.0;             // past the 15-minute session
        page.refresh();
        check (proc.getDemoGate().expired() && infoTextContains (editor, "DEMO EXPIRED"),
               "the expired demo session is reported on the page");
        check (infoTextContains (editor, "Restart the PumpCurve instance"),
               "...with what the user can do next");
        check (refreshButton != nullptr && refreshButton->isEnabled(),
               "the check stays available while the demo is expired");

        authMock->subscriptionActive = true;     // the customer subscribes
        if (refreshButton != nullptr)
            refreshButton->triggerClick();
        check (waitForUi ([&] { return proc.getDemoGate().isFullVersion(); }),
               "a check AFTER the demo expired grants the full version");
        check (! proc.getDemoGate().expired(),
               "...the expired session is over, with no instance restart");
        page.refresh();
        check (infoTextContains (editor, "FULL VERSION")
                   && ! infoTextContains (editor, "DEMO EXPIRED"),
               "...and the page shows the full version instead of the expiry notice");

        editor.setVisible (false);               // stop the editor timer first
        sid::license::resetMonotonicMillisecondsForTest();
        authPath.deleteFile();
    }

    // ---- 5. No-signal ducking-curve preview -------------------------------
    {
        // Depth must vary with Amount; shape must vary with Release.
        const float d0 = GraphComponent::settledDepthDb (0.0f);
        const float d50 = GraphComponent::settledDepthDb (50.0f);
        const float d100 = GraphComponent::settledDepthDb (100.0f);
        check (d0 < 0.5f, "preview depth at 0% Amount is flat (got "
                              + juce::String (d0, 1).toStdString() + " dB)");
        check (d50 > 5.0f && d50 < 40.0f, "preview depth at 50% Amount is moderate ("
                              + juce::String (d50, 1).toStdString() + " dB)");
        check (d100 > d50 * 1.5f, "preview depth at 100% Amount is deepest ("
                              + juce::String (d100, 1).toStdString() + " dB)");
        check (std::abs (GraphComponent::settledDepthDb (15.0f)
                         - GraphComponent::settledDepthDb (16.0f)) < 2.0f,
               "preview depth varies smoothly (shallow presets remain smooth)");
    }

    // ---- 6. Preset previous/next arrows (live processor) ------------------
    {
        SideChainAudioProcessor proc;
        auto* amount = dynamic_cast<juce::AudioParameterFloat*> (
            proc.getParameters().getParameter ("sidechainAmount"));
        auto* release = dynamic_cast<juce::AudioParameterFloat*> (
            proc.getParameters().getParameter ("release"));

        const auto& table = sid::presets::factoryPresets();
        const auto values = [&] { return std::pair<float,float> (amount->get(), release->get()); };
        // Tolerance matches the preset system's own match tolerance (0.051):
        // parameter values round-trip through the host normalisation.
        auto close = [] (float a, float b) { return std::abs (a - b) < 0.051f; };
        auto isPreset = [&] (int idx, float a, float r)
        {
            return close (a, table.getReference (idx).amountPercent)
                && close (r, table.getReference (idx).releaseMs);
        };

        // From defaults ("Default" anchor): next -> first preset (Micro Kick).
        proc.stepPreset (+1);
        auto [a1, r1] = values();
        check (isPreset (0, a1, r1),
               "arrows: next from Default lands on first preset (Micro Kick)");

        // Step forward through three presets; values must follow the table.
        proc.stepPreset (+1); proc.stepPreset (+1);
        auto [a3, r3] = values();
        check (isPreset (2, a3, r3),
               "arrows: three nexts land on the third preset (Vocal Duck)");

        // Wrap: next from the last preset returns to the first.
        proc.applyFactoryPreset (table.getReference (table.size() - 1).name);
        proc.stepPreset (+1);
        auto [aw, rw] = values();
        check (isPreset (0, aw, rw),
               "arrows: next from last preset wraps to first");

        // Backwards from the first wraps to the last.
        proc.stepPreset (-1);
        auto [ab, rb] = values();
        check (isPreset (table.size() - 1, ab, rb),
               "arrows: prev from first preset wraps to last");

        // Custom anchor: off-values step to the closest preset by Amount.
        amount->setValueNotifyingHost (amount->convertTo0to1 (47.0f));
        release->setValueNotifyingHost (release->convertTo0to1 (321.0f));
        proc.stepPreset (+1);
        auto [ac, rc] = values();
        bool matchesTable = false;
        for (const auto& p : table)
            if (close (p.amountPercent, ac) && close (p.releaseMs, rc))
            { matchesTable = true; break; }
        check (matchesTable, "arrows: stepping from Custom lands on a factory preset");

        // Display identity stays in sync (no second state mechanism).
        proc.applyFactoryPreset ("Classic Kick"); // 0.4.0 factory table
        check (proc.getCurrentPresetDisplayName() == "Classic Kick",
               "arrows/dropdown share one identity source (Classic Kick exact match)");
    }

    // Real editor Save Preset -> name -> disk -> selector -> reopened editor.
    {
        const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getNonexistentChildFile ("pumpcurve-ui-user-presets", "", false);
        SideChainAudioProcessor proc;
        SideChainAudioProcessorEditor editor (proc);
        SideChainEditorTestAccess::setPresetDirectory (editor, directory);
        auto& save = SideChainEditorTestAccess::savePresetButton (editor);
        check (save.isVisible() && ! save.getBounds().isEmpty()
            && ! save.getBounds().intersects (SideChainEditorTestAccess::presetBox (editor).getBounds()),
            "SAVE PRESET is visible, clickable and separate from selector");
        SideChainEditorTestAccess::amountKnob (editor).setValue (81.3, juce::sendNotificationSync);
        save.onClick();
        auto& name = SideChainEditorTestAccess::presetName (editor);
        check (name.isVisible(), "Save Preset opens in-editor name entry");
        name.setText ("UI saved sound", false);
        SideChainEditorTestAccess::commitPreset (editor);
        check (! name.isVisible() && SideChainEditorTestAccess::presetBox (editor).getText() == "UI saved sound",
            "save commits and immediately selects user preset");
        auto& status = SideChainEditorTestAccess::presetStatus (editor);
        check (status.isVisible() && status.getText() == "Preset saved.",
            "save confirms with a visible 'Preset saved.'");
        SideChainEditorTestAccess::agePresetStatus (editor);
        check (! status.isVisible() && status.getText().isEmpty(),
            "the 'Preset saved.' confirmation clears itself after the hold time");
        check (sid::presets::UserPresetLibrary (directory).list().size() == 1,
            "real editor flow writes persistent sound file");
        save.onClick(); name.setText ("UI saved sound", false);
        SideChainEditorTestAccess::commitPreset (editor);
        check (name.isVisible() && SideChainEditorTestAccess::presetStatus (editor).getText().contains ("already exists"),
            "duplicate UI save reports error without overwrite");
        SideChainEditorTestAccess::cancelPreset (editor);
        check (! name.isVisible() && ! status.isVisible() && status.getText().isEmpty(),
            "CANCEL removes the naming error/instruction from the toolbar");
        save.onClick();
        SideChainEditorTestAccess::agePresetStatus (editor);
        check (name.isVisible() && status.isVisible() && status.getText().isNotEmpty(),
            "the naming instruction stays while the name form is open");
        SideChainEditorTestAccess::cancelPreset (editor);
        SideChainAudioProcessor reopened;
        SideChainAudioProcessorEditor reopenedEditor (reopened);
        SideChainEditorTestAccess::setPresetDirectory (reopenedEditor, directory);
        auto& selector = SideChainEditorTestAccess::presetBox (reopenedEditor);
        selector.setSelectedId (1000, juce::dontSendNotification); selector.onChange();
        check (std::abs (reopened.getParameters().getRawParameterValue ("sidechainAmount")->load() - 81.3f) < 0.0001f
            && selector.getText() == "UI saved sound", "reopened editor discovers and loads user preset through selector");
        directory.deleteRecursively();
    }

    // ---- 7. 0.5.0 redesign: legibility, layout and the honest failed check --
    //
    // The redesign must not trade correctness for looks: every INFO text in
    // every state fits its bounds (wraps, never clipped or squeezed off the
    // page), main-view controls never overlap, the page renders at 2x
    // (Retina), a check that got NO answer says so instead of repeating a
    // verdict, and the device-limit line says its advice exactly once.
    printf ("\n7. Redesign legibility and layout\n");
    {
        // A label "fits" when its text, at its font, needs no more lines than
        // its height holds (the same rule juce::Label uses to wrap).
        std::function<void (juce::Component&, juce::StringArray&)> findClipped;
        findClipped = [&findClipped] (juce::Component& root, juce::StringArray& out)
        {
            for (auto* c : root.getChildren())
            {
                if (auto* l = dynamic_cast<juce::Label*> (c))
                {
                    if (l->isVisible() && l->getText().isNotEmpty() && l->getWidth() > 0)
                    {
                        const auto font = l->getFont();
                        const auto area = l->getBorderSize().subtractedFrom (l->getLocalBounds());
                        const int maxLines = juce::jmax (1, (int) ((float) area.getHeight() / font.getHeight()));
                        juce::GlyphArrangement ga;
                        ga.addFittedText (font, l->getText(), 0.0f, 0.0f, (float) area.getWidth(),
                                          (float) area.getHeight(), l->getJustificationType(),
                                          maxLines, l->getMinimumHorizontalScale());
                        const auto bb = ga.getBoundingBox (0, -1, true);
                        if (bb.getRight() > (float) area.getWidth() + 1.0f
                            || bb.getBottom() > (float) area.getHeight() + 1.0f
                            || ga.getNumGlyphs() < l->getText().removeCharacters (" \n").length())
                            out.add (l->getText().substring (0, 40));
                    }
                }
                findClipped (*c, out);
            }
        };

        auto authPath = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("sdch_ui_verify_redesign.json");
        authPath.deleteFile();
        sid::license::setMonotonicMillisecondsForTest (&uiFakeNowFn);
        uiFakeNow = 90'000'000.0;

        MockTransport* authMock = new MockTransport();
        SideChainAudioProcessor proc (std::unique_ptr<sid::auth::AuthTransport> (authMock),
                                      authPath.getFullPathName());
        SideChainAudioProcessorEditor editor (proc);
        editor.setSize (800, 520);
        auto& auth = SideChainEditorTestAccess::authManager (editor);
        auto& page = SideChainEditorTestAccess::infoPage (editor);

        // Main view: no two visible controls overlap, all inside the editor.
        {
            juce::Array<juce::Component*> visible;
            for (auto* c : editor.getChildren())
                if (c->isVisible() && ! c->getBounds().isEmpty()
                    && dynamic_cast<juce::TooltipWindow*> (c) == nullptr)
                    visible.add (c);
            int overlaps = 0, outside = 0;
            for (int i = 0; i < visible.size(); ++i)
            {
                if (! editor.getLocalBounds().contains (visible[i]->getBounds()))
                    ++outside;
                for (int j = i + 1; j < visible.size(); ++j)
                    if (visible[i]->getBounds().intersects (visible[j]->getBounds()))
                    {
                        ++overlaps;
                        printf ("    overlap: %s / %s\n", visible[i]->getName().toRawUTF8(),
                                visible[j]->getName().toRawUTF8());
                    }
            }
            check (visible.size() >= 20 && overlaps == 0 && outside == 0,
                   "main view: " + juce::String (visible.size()).toStdString()
                       + " visible controls, none overlapping, all inside 800x520");
            juce::StringArray clipped;
            findClipped (editor, clipped);
            check (clipped.isEmpty(), "main view: every label fits its bounds ("
                                          + clipped.joinIntoString (" | ").toStdString() + ")");
        }

        // The preset status line wraps (never squeezes or clips) the longest
        // message it can carry, and fits beside the toolbar controls.
        {
            SideChainEditorTestAccess::openSavePresetForm (editor);
            auto& status = SideChainEditorTestAccess::presetStatus (editor);
            const juce::StringArray messages {
                "Type a name for the new preset. Nothing is overwritten.",
                "Use a name of 1-80 characters without leading/trailing spaces or a trailing dot.",
                "A preset file with that name already exists. Nothing was overwritten.",
                "Could not commit the preset file. The sound has not been changed." };
            juce::StringArray clipped;
            for (const auto& m : messages)
            {
                status.setText (m, juce::dontSendNotification);
                findClipped (editor, clipped);
            }
            const bool besideToggle = ! status.getBounds().intersects (
                SideChainEditorTestAccess::transportGateToggle (editor).getBounds());
            check (clipped.isEmpty() && status.getMinimumHorizontalScale() >= 1.0f && besideToggle,
                   "preset status: longest messages wrap unsqueezed and clear of the toolbar ("
                       + clipped.joinIntoString (" | ").toStdString() + ")");
            SideChainEditorTestAccess::cancelPreset (editor);
        }

        // Pure UI navigation must never move an audio parameter: open/close
        // INFO, open/cancel the preset name form, step the INFO page.
        {
            juce::Array<float> before;
            for (auto* p : proc.getParameters().processor.getParameters())
                before.add (p->getValue());
            SideChainEditorTestAccess::showInfoPage (editor);
            pumpMessages (60);
            SideChainEditorTestAccess::showMainView (editor);
            SideChainEditorTestAccess::savePresetButton (editor).onClick();
            if (auto* cancel = findButton (editor, "CANCEL"))
                cancel->triggerClick();
            pumpMessages (60);
            bool unchanged = true;
            int i = 0;
            for (auto* p : proc.getParameters().processor.getParameters())
                unchanged = unchanged && p->getValue() == before[i++];
            check (unchanged && i > 0,
                   "UI navigation (INFO, back, save form, cancel) leaves all "
                       + juce::String (i).toStdString() + " parameters unchanged");
        }

        SideChainEditorTestAccess::showInfoPage (editor);

        auto infoLayoutOk = [&] (const std::string& stateName)
        {
            page.refresh();
            juce::StringArray clipped;
            findClipped (page, clipped);
            int overlaps = 0;
            auto kids = page.getChildren();
            for (int i = 0; i < kids.size(); ++i)
                for (int j = i + 1; j < kids.size(); ++j)
                    if (kids[i]->isVisible() && kids[j]->isVisible()
                        && kids[i]->getBounds().intersects (kids[j]->getBounds()))
                        ++overlaps;
            check (clipped.isEmpty() && overlaps == 0,
                   "INFO " + stateName + ": no clipped text, no overlapping controls ("
                       + clipped.joinIntoString (" | ").toStdString() + ")");
        };

        infoLayoutOk ("signed out");

        // Signed out, the account column leads with its state (no empty
        // headline row) and claims nothing about Music-Prod+.
        {
            int headlines = 0;
            bool claimsPlus = false;
            for (auto* c : page.getChildren())
                if (auto* l = dynamic_cast<juce::Label*> (c))
                {
                    headlines += (l->getText() == "SIGNED OUT" && l->getFont().getHeight() >= 15.0f) ? 1 : 0;
                    claimsPlus = claimsPlus || l->getText().contains ("MUSIC-PROD+ ACTIVE");
                }
            check (headlines == 1 && ! claimsPlus,
                   "INFO signed out: SIGNED OUT is the account headline, no Music-Prod+ claim");
        }

        auth.startLinking();
        auth.pollOnceForTesting();
        auth.refreshEntitlementsForTesting();
        infoLayoutOk ("signed in, not subscribed");

        // A check that gets NO answer: said plainly, never a verdict.
        authMock->entitlementsFail = true;
        auth.refreshEntitlementsForTesting();
        page.refresh();
        check (auth.didLastEntitlementCheckFail()
                   && infoTextContains (page, "COULD NOT REACH MUSIC-PROD")
                   && ! infoTextContains (page, "Music-Prod reports no active"),
               "a failed check says it could not reach Music-Prod (not 'reports no subscription')");
        check (! proc.getDemoGate().isFullVersion(),
               "...and still grants nothing");
        infoLayoutOk ("subscription check failed");

        authMock->entitlementsFail = false;
        authMock->subscriptionActive = true;
        auth.refreshEntitlementsForTesting();
        page.refresh();
        check (! auth.didLastEntitlementCheckFail()
                   && ! infoTextContains (page, "COULD NOT REACH MUSIC-PROD")
                   && infoTextContains (page, "MUSIC-PROD+ ACTIVE"),
               "the next real answer clears the failure notice");
        infoLayoutOk ("full version via Music-Prod+");

        // Device limit: the advice appears ONCE (it used to be printed twice).
        authMock->subscriptionActive = false;
        auth.refreshEntitlementsForTesting();
        authMock->licenseStatus = sid::auth::LicenseResponse::Status::deviceLimit;
        auth.activateLicenseForTesting ("PC-TEST-LIMIT-0000");
        page.refresh();
        {
            int mentions = 0;
            for (const auto& t : infoTexts (page))
                for (int at = t.indexOfIgnoreCase ("maximum number of computers"); at >= 0;
                     at = t.indexOfIgnoreCase (at + 1, "maximum number of computers"))
                    ++mentions;
            check (infoTextContains (page, "TOO MANY COMPUTERS") && mentions == 1
                       && infoTextContains (page, "music-prod.com/profile"),
                   "device limit: TOO MANY COMPUTERS, the advice once, and where to manage computers ("
                       + juce::String (mentions).toStdString() + " mention(s))");
        }
        infoLayoutOk ("licence device limit");

        // Demo expired + offline licence server.
        uiFakeNow += (sid::license::kDemoSessionSeconds + 1.0) * 1000.0;
        authMock->licenseStatus = sid::auth::LicenseResponse::Status::networkError;
        auth.activateLicenseForTesting ("PC-TEST-OFFLINE-00");
        infoLayoutOk ("demo expired, licence server offline");

        // The field case seen on the test Mac (2026-10-17): Music-Prod+ is
        // active on the account but the token was minted for another plugin,
        // so the server says plugin_entitled false. The page must not present
        // the membership as having unlocked PumpCurve.
        authMock->licenseStatus = sid::auth::LicenseResponse::Status::ok;
        authMock->licenseActive = false;
        authMock->subscriptionActive = true;
        authMock->coversProduct = false;
        authMock->answeredProduct = "vyre";
        auth.refreshEntitlementsForTesting();
        page.refresh();
        check (! proc.getDemoGate().isFullVersion() && auth.subscriptionNotValidForProduct()
                   && auth.tokenIsForAnotherProduct(),
               "membership not confirmed for PumpCurve: no full version, and the state is detected");
        check (infoTextContains (page, "NOT CONFIRMED FOR PUMPCURVE")
                   && infoTextContains (page, "LOG OUT")
                   && ! infoTextContains (page, "subscription was confirmed by Music-Prod"),
               "...the page says it is not confirmed for PumpCurve and how to fix it, never 'confirmed'");
        infoLayoutOk ("membership not confirmed for PumpCurve");

        authMock->coversProduct = true;
        authMock->answeredProduct = "sidechain";
        auth.refreshEntitlementsForTesting();
        page.refresh();
        check (proc.getDemoGate().isFullVersion() && ! auth.subscriptionNotValidForProduct()
                   && infoTextContains (page, "MUSIC-PROD+ ACTIVE")
                   && infoTextContains (page, "subscription was confirmed by Music-Prod"),
               "...and a sign-in for PumpCurve that IS covered unlocks and says confirmed");

        // Retina: the whole editor (INFO page shown) renders at 2x.
        const auto hiDpi = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
        check (hiDpi.getWidth() == 1600 && hiDpi.getHeight() == 1040,
               "the editor renders at 2x scale (1600x1040) for high-DPI displays");

        sid::license::resetMonotonicMillisecondsForTest();
        authPath.deleteFile();
    }

    printf ("\n==============================\n");
    if (failures == 0)
        printf ("%d/%d checks passed. UI VERIFICATION PASSED\n", checks, checks);
    else
        printf ("%d/%d checks passed. %d FAILURES\n", checks - failures, checks, failures);
    printf ("==============================\n");
    return failures == 0 ? 0 : 1;
}
