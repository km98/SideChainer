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
#include <limits>

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
    static ProdKnob& amountKnob (SideChainAudioProcessorEditor& e) { return e.amountKnob; }
    static ProdKnob& duckLengthKnob (SideChainAudioProcessorEditor& e) { return e.duckLengthKnob; }
    static ProdKnob& releaseKnob (SideChainAudioProcessorEditor& e) { return e.releaseKnob; }
    static GraphComponent& graph (SideChainAudioProcessorEditor& e) { return e.graph; }
    static juce::ComboBox& presetBox (SideChainAudioProcessorEditor& e) { return e.presetBox; }
    static juce::Label& amountValueLabel (SideChainAudioProcessorEditor& e) { return e.amountValueLabel; }
    static juce::Label& duckLengthValueLabel (SideChainAudioProcessorEditor& e) { return e.duckLengthValueLabel; }
    static juce::Label& duckLengthCaption (SideChainAudioProcessorEditor& e) { return e.duckLengthCaption; }
    static juce::Slider& smoothSlider (SideChainAudioProcessorEditor& e) { return e.smoothSlider; }
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
        auto px = img.getPixelAt (400, 40); // header black field, clear of wordmark/buttons
        check (px == juce::Colour (0xff000000),
               "editor background is pure black outside the graph (got " + px.toString().toStdString() + ")");
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
                   "PumpCurve wordmark rendered in header (bright glyph pixels: "
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
