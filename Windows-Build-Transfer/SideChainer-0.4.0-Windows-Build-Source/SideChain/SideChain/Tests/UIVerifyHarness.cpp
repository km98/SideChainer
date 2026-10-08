/*
    SideChain - UI verification harness (real editor + InfoPage, offscreen).

    Verifies after the 0.2.1 defect fixes:
      1. Editor background is pure black.
      2. INFO page is in the hierarchy, full-size, all 8 controls visible.
      3. INFO page background is pure black (same visual system).
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

#include <cstdio>
#include <string>
#include <cstdlib>

static int checks = 0, failures = 0;
static void check (bool ok, const std::string& label)
{
    ++checks;
    printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (! ok) ++failures;
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
    static juce::Component& amountKnob (SideChainAudioProcessorEditor& e) { return e.amountKnob; }
    static juce::Component& duckLengthKnob (SideChainAudioProcessorEditor& e) { return e.duckLengthKnob; }
    static juce::Component& releaseKnob (SideChainAudioProcessorEditor& e) { return e.releaseKnob; }
};

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
    sid::auth::EntitlementsResponse entitlements (const juce::String&) override
    {
        sid::auth::EntitlementsResponse e;
        e.ok = true;
        e.subscribed = false;
        return e;
    }
    bool logout (const juce::String&) override { return true; }
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
        auto px = img.getPixelAt (400, 250); // mid-field, clear of controls
        check (px == juce::Colour (0xff000000),
               "editor background is pure black (got " + px.toString().toStdString() + ")");
        auto px2 = img.getPixelAt (10, 510);
        check (px2 == juce::Colour (0xff000000),
               "editor bottom area is pure black (logo strip)");

        // Logo strip: rendered artwork pixels must be white/neutral and the
        // strip must contain NO red/orange (regression for the HSB-colour
        // mask bug) and no opaque image box. The logo band is the bottom 26px
        // of the content area (y 462..488 at 800x520).
        {
            int logoPixels = 0, redPixels = 0;
            int minX = 800, maxX = 0, minY = 520, maxY = 0;
            for (int y = 465; y < 495; ++y)      // logo band (26px strip sits
                                                 // above the caption rows)
                for (int x = 300; x < 500; ++x)  // centred region
                {
                    const auto p = img.getPixelAt (x, y);
                    const float r = p.getFloatRed(), gr = p.getFloatGreen(), b = p.getFloatBlue();
                    if (p.getFloatAlpha() > 0.1f && (r > 0.05f || gr > 0.05f || b > 0.05f))
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
            // a 110px draw width -> ~95px of visible artwork, centred on x=400.
            const int drawnW = maxX - minX + 1;
            const int centre = (minX + maxX) / 2;
            check (drawnW > 80 && drawnW <= 112,
                   "logo artwork width proportional to the 110px draw rect (got "
                       + juce::String (drawnW).toStdString() + "px)");
            check (std::abs (centre - 400) <= 6, "logo centred horizontally");
        }

        // SideChainer wordmark present in the header (left), INFO button area
        // clear of text: sample a few glyph pixels in the wordmark rect.
        {
            int glyphPixels = 0;
            for (int y = 8; y < 42; ++y)
                for (int x = 14; x < 200; ++x)
                {
                    const auto p = img.getPixelAt (x, y);
                    if (p.getFloatAlpha() > 0.2f && p.getFloatRed() > 0.5f)
                        ++glyphPixels;
                }
            check (glyphPixels > 100,
                   "SideChainer wordmark rendered in header (bright glyph pixels: "
                       + juce::String (glyphPixels).toStdString() + ")");
        }

        // Preset arrows: both triangle glyphs must be visible beside the
        // selector (group is centred at x=400, buttons 22px, strip y~50..72).
        {
            auto litIn = [&] (int x0, int x1)
            {
                int n = 0;
                for (int y = 48; y < 76; ++y)
                    for (int x = x0; x < x1; ++x)
                    {
                        const auto p = img.getPixelAt (x, y);
                        if (p.getFloatAlpha() > 0.2f
                            && p.getFloatRed() + p.getFloatGreen() + p.getFloatBlue() > 0.9f)
                            ++n;
                    }
                return n;
            };
            check (litIn (266, 294) > 20, "preset prev-arrow glyph rendered");
            check (litIn (506, 534) > 20, "preset next-arrow glyph rendered");
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
                   "DUCK LENGTH knob/value/caption do NOT overlap the Music-Prod logo band");
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
        }
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

        InfoPageComponent info (authMgr, "0.2.1");
        info.setBounds (0, 0, 800, 520);

        juce::Component host;
        host.addAndMakeVisible (info);
        host.setBounds (0, 0, 800, 520);

        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

        check (info.getWidth() == 800 && info.getHeight() == 520,
               "INFO page laid out at full editor size");
        check (info.getNumChildComponents() == 8,
               "INFO page has 8 controls (got "
                   + juce::String (info.getNumChildComponents()).toStdString() + ")");

        int visible = 0;
        for (auto* c : info.getChildren())
            if (c->isVisible() && ! c->getBounds().isEmpty())
                ++visible;
        check (visible == 8, "all 8 INFO controls visible with non-empty bounds");

        juce::Image img (juce::Image::ARGB, 800, 520, true);
        {
            juce::Graphics g (img);
            info.paint (g);
        }
        auto px = img.getPixelAt (400, 510);
        check (px == juce::Colour (0xff000000),
               "INFO page background is pure black");

        // SideChainer wordmark rendered on the INFO page: sample bright
        // glyph pixels in the top-left region where the wordmark is drawn.
        {
            int glyphPixels = 0;
            for (int y = 40; y < 110; ++y)
                for (int x = 20; x < 300; ++x)
                {
                    const auto p = img.getPixelAt (x, y);
                    if (p.getFloatAlpha() > 0.2f && p.getFloatRed() > 0.5f)
                        ++glyphPixels;
                }
            check (glyphPixels > 100,
                   "SideChainer wordmark rendered on INFO page (glyph pixels: "
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

        InfoPageComponent info (authMgr, "0.2.1");
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

    printf ("\n==============================\n");
    if (failures == 0)
        printf ("%d/%d checks passed. UI VERIFICATION PASSED\n", checks, checks);
    else
        printf ("%d/%d checks passed. %d FAILURES\n", checks - failures, checks, failures);
    printf ("==============================\n");
    return failures == 0 ? 0 : 1;
}
