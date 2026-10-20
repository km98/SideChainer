/*
    PumpCurve UI snapshot tool (visual review evidence, not a pass/fail suite).

    Renders the REAL editor and the REAL INFO page - the same classes the AU
    and VST3 ship - in the states a customer actually meets, at 1x and 2x
    (Retina) scale, and writes one PNG per state into the output directory:

        main-idle            default sound, no signal, demo running
        main-signal          real audio through the real processor (waveform)
        main-save-preset     the in-editor Save Preset name form
        main-expired         the demo has run out (header alarm state)
        info-signed-out      INFO page, signed out, no published release
        info-not-active      signed in, Music-Prod+ not active, demo running
        info-checking        CHECK SUBSCRIPTION pressed, request in flight
        info-full-plus       full version via a confirmed Music-Prod+ answer
        info-licence-limit   demo expired + licence key at its device limit
        info-update          an update is available with release notes
        info-offline         version check failed, licence server unreachable

    Nothing here touches the network: the auth and update transports are
    scripted stand-ins with the deployed response shapes, exactly as in
    UIVerifyHarness.cpp. Usage: ./Tests/ui_snapshot.sh [--out-dir DIR]
*/

#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/InfoPage.h"
#include "../Source/LicenseGate.h"
#include "../Source/PumpCurveTheme.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    double fakeNowMs = 1.0e6;
    double fakeNow() { return fakeNowMs; }

    struct ScriptedAuth : public sid::auth::AuthTransport
    {
        bool subscribed = false;
        bool coversProduct = true;          // false: membership not confirmed for this product
        juce::String answeredProduct = "sidechain";
        bool licenseActive = false;
        bool failEntitlements = false;
        bool holdEntitlements = false;     // keeps a check "in flight"
        std::atomic<bool> release { false };
        sid::auth::LicenseResponse::Status licenceAnswer = sid::auth::LicenseResponse::Status::ok;

        sid::auth::StartResponse start (const juce::String&, const juce::String&, const juce::String&) override
        {
            sid::auth::StartResponse r;
            r.ok = true;
            r.deviceCode = "snap-device";
            r.userCode = "SNAP-SHOT";
            r.verificationUrl = "https://music-prod.com/plugin/link";
            r.expiresInSeconds = 900;
            r.intervalSeconds = 1;
            return r;
        }
        sid::auth::PollResponse poll (const juce::String&) override
        {
            sid::auth::PollResponse r;
            r.status = "approved";
            r.token = "snap-token";
            r.displayName = "producer@example.com";
            return r;
        }
        sid::auth::EntitlementsResponse entitlements (const juce::String&) override
        {
            while (holdEntitlements && ! release.load())
                juce::Thread::sleep (5);
            sid::auth::EntitlementsResponse e;
            if (failEntitlements)
                return e;
            e.ok = true;
            e.subscribed = e.subscriptionActive = subscribed;
            e.subscriptionCoversProduct = subscribed && coversProduct;
            e.product = answeredProduct;
            e.licenseActive = licenseActive;
            e.licenseStatus = licenseActive ? "active" : "none";
            e.productEntitled = (subscribed && coversProduct) || licenseActive;
            return e;
        }
        sid::auth::LicenseResponse activateLicense (const juce::String&, const juce::String&) override
        {
            sid::auth::LicenseResponse r;
            r.product = "sidechain";
            r.status = licenceAnswer;
            if (licenceAnswer == sid::auth::LicenseResponse::Status::deviceLimit)
            {
                r.deviceLimitUsed = 3;
                r.deviceLimit = 3;
            }
            return r;
        }
        bool logout (const juce::String&) override { return true; }
    };

    struct ScriptedUpdates : public sid::update::UpdateTransport
    {
        juce::String status = "noPublishedRelease";
        bool fail = false;

        bool fetch (const juce::String&, const juce::String& currentVersion,
                    const juce::String&, const juce::String&,
                    sid::update::UpdateInfo& out) override
        {
            if (fail)
                return false;
            const bool none = (status == "noPublishedRelease");
            juce::DynamicObject::Ptr data = new juce::DynamicObject();
            data->setProperty ("latestVersion", none ? juce::var() : juce::var ("0.5.1"));
            data->setProperty ("updateAvailable", ! none);
            data->setProperty ("releaseNotes", none ? "" : "Smoother curve handles and a fix for an audible click on very short TIME settings.");
            data->setProperty ("publishedAt", "2026-10-20T09:00:00Z");
            data->setProperty ("downloadUrl", "https://music-prod.com/pumpcurve");
            data->setProperty ("status", status);
            data->setProperty ("mandatory", false);
            juce::DynamicObject::Ptr env = new juce::DynamicObject();
            env->setProperty ("success", true);
            env->setProperty ("data", juce::var (data.get()));
            return sid::update::parseUpdateResponse (juce::var (env.get()), currentVersion, out);
        }
    };

    void pump (int ms)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
        while (juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    template <typename P> bool waitFor (P pred, int ms = 4000)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
        while (juce::Time::getMillisecondCounter() < end)
        {
            if (pred()) return true;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }
        return pred();
    }

    juce::File outDir;
    int written = 0;

    void dump (juce::Component& c, const juce::String& name)
    {
        for (float scale : { 1.0f, 2.0f })
        {
            auto img = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
            auto file = outDir.getChildFile (name + (scale > 1.5f ? "@2x" : "") + ".png");
            file.deleteFile();
            if (auto out = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (img, *out);
                ++written;
                std::printf ("  wrote %s (%dx%d)\n", file.getFileName().toRawUTF8(),
                             img.getWidth(), img.getHeight());
            }
        }
    }

    // A kick-and-bass style loop: low tone with a decaying envelope every
    // half second, so the processed trace shows the duck against the input.
    void feedAudio (SideChainAudioProcessor& proc, juce::Component& editor, double seconds)
    {
        proc.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        const int total = (int) (seconds * 48000.0);
        for (int start = 0; start + 512 <= total; start += 512)
        {
            for (int i = 0; i < 512; ++i)
            {
                const int n = start + i;
                const double t = n / 48000.0;
                const double phase = std::fmod (t, 0.5);
                const float env = (float) (0.35 + 0.55 * std::exp (-phase * 7.0));
                const float s = env * (float) (0.8 * std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * t)
                                               + 0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t));
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }
            proc.processBlock (buffer, midi);
            if ((start / 512) % 8 == 0)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
        juce::ignoreUnused (editor);
        pump (120);
    }

    juce::File authFile (const juce::String& tag)
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("pumpcurve_snapshot_auth_" + tag + ".json");
        f.deleteFile();
        return f;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    outDir = juce::File::getCurrentWorkingDirectory().getChildFile ("ui-snapshots");
    for (int i = 1; i + 1 < argc; ++i)
        if (juce::String (argv[i]) == "--out-dir")
            outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[i + 1]);
    outDir.createDirectory();
    std::printf ("PumpCurve UI snapshots -> %s\n", outDir.getFullPathName().toRawUTF8());

    sid::license::setMonotonicMillisecondsForTest (&fakeNow);

    // ---- main view states ------------------------------------------------
    {
        auto* auth = new ScriptedAuth();
        auto* upd = new ScriptedUpdates();
        SideChainAudioProcessor proc (std::unique_ptr<sid::auth::AuthTransport> (auth),
                                      authFile ("main").getFullPathName(),
                                      std::unique_ptr<sid::update::UpdateTransport> (upd));
        SideChainAudioProcessorEditor editor (proc);
        pump (150);
        dump (editor, "main-idle");

        feedAudio (proc, editor, 3.0);
        dump (editor, "main-signal");

        if (auto* b = dynamic_cast<juce::Button*> (editor.findChildWithID ("savePreset")))
            b->triggerClick();
        else
            for (auto* child : editor.getChildren())
                if (auto* tb = dynamic_cast<juce::TextButton*> (child))
                    if (tb->getButtonText().containsIgnoreCase ("SAVE") && tb->isVisible())
                    { tb->triggerClick(); break; }
        pump (120);
        dump (editor, "main-save-preset");
        for (auto* child : editor.getChildren())
            if (auto* tb = dynamic_cast<juce::TextButton*> (child))
                if (tb->getButtonText() == "CANCEL" && tb->isVisible())
                { tb->triggerClick(); break; }

        fakeNowMs += (sid::license::kDemoSessionSeconds + 5.0) * 1000.0;
        pump (1100);
        editor.repaint();
        pump (60);
        dump (editor, "main-expired");
        fakeNowMs = 1.0e6 + 10.0;
    }

    // ---- INFO page states ------------------------------------------------
    auto openInfo = [] (SideChainAudioProcessorEditor& editor) -> juce::Component*
    {
        for (auto* child : editor.getChildren())
            if (auto* tb = dynamic_cast<juce::TextButton*> (child))
                if (tb->getButtonText() == "INFO" && tb->isVisible())
                {
                    tb->triggerClick();
                    break;
                }
        pump (150);
        return &editor;
    };

    {
        auto* auth = new ScriptedAuth();
        auto* upd = new ScriptedUpdates();
        SideChainAudioProcessor proc (std::unique_ptr<sid::auth::AuthTransport> (auth),
                                      authFile ("so").getFullPathName(),
                                      std::unique_ptr<sid::update::UpdateTransport> (upd));
        proc.getUpdateChecker().setCheckIntervalSecondsForTesting (1.0e9);
        proc.getUpdateChecker().checkNowForTesting();
        SideChainAudioProcessorEditor editor (proc);
        openInfo (editor);
        dump (editor, "info-signed-out");

        auto& am = proc.getAuthManager();
        am.startLinking();
        am.pollOnceForTesting();
        am.refreshEntitlementsForTesting();
        waitFor ([&] { return ! am.isEntitlementCheckInFlight(); });
        pump (150);
        dump (editor, "info-not-active");

        auth->holdEntitlements = true;
        am.refreshEntitlements();
        waitFor ([&] { return am.isEntitlementCheckInFlight(); });
        pump (100);
        dump (editor, "info-checking");
        auth->subscribed = true;
        auth->release = true;
        waitFor ([&] { return proc.getDemoGate().isFullVersion(); });
        auth->holdEntitlements = false;
        pump (150);
        dump (editor, "info-full-plus");
    }

    {
        auto* auth = new ScriptedAuth();
        auth->licenceAnswer = sid::auth::LicenseResponse::Status::deviceLimit;
        auto* upd = new ScriptedUpdates();
        upd->status = "updateAvailable";
        SideChainAudioProcessor proc (std::unique_ptr<sid::auth::AuthTransport> (auth),
                                      authFile ("lim").getFullPathName(),
                                      std::unique_ptr<sid::update::UpdateTransport> (upd));
        proc.getUpdateChecker().setCheckIntervalSecondsForTesting (1.0e9);
        proc.getUpdateChecker().checkNowForTesting();
        SideChainAudioProcessorEditor editor (proc);
        auto& am = proc.getAuthManager();
        am.startLinking();
        am.pollOnceForTesting();
        am.refreshEntitlementsForTesting();
        waitFor ([&] { return ! am.isEntitlementCheckInFlight(); });
        openInfo (editor);
        dump (editor, "info-update");
        am.activateLicenseForTesting ("PC-TEST-0000-0000");
        fakeNowMs += (sid::license::kDemoSessionSeconds + 5.0) * 1000.0;
        pump (200);
        dump (editor, "info-licence-limit");
        fakeNowMs = 1.0e6 + 20.0;
    }

    {
        auto* auth = new ScriptedAuth();
        auth->failEntitlements = true;
        auth->licenceAnswer = sid::auth::LicenseResponse::Status::networkError;
        auto* upd = new ScriptedUpdates();
        upd->fail = true;
        SideChainAudioProcessor proc (std::unique_ptr<sid::auth::AuthTransport> (auth),
                                      authFile ("off").getFullPathName(),
                                      std::unique_ptr<sid::update::UpdateTransport> (upd));
        proc.getUpdateChecker().setCheckIntervalSecondsForTesting (1.0e9);
        proc.getUpdateChecker().checkNowForTesting();
        SideChainAudioProcessorEditor editor (proc);
        auto& am = proc.getAuthManager();
        am.startLinking();
        am.pollOnceForTesting();
        am.refreshEntitlementsForTesting();
        am.activateLicenseForTesting ("PC-TEST-0000-0000");
        openInfo (editor);
        pump (150);
        dump (editor, "info-offline");
    }

    // ---- the field case from the test Mac: membership, token for "vyre" ------
    {
        auto* auth = new ScriptedAuth();
        auth->subscribed = true;
        auth->coversProduct = false;
        auth->answeredProduct = "vyre";
        auto* upd = new ScriptedUpdates();
        SideChainAudioProcessor proc (std::unique_ptr<sid::auth::AuthTransport> (auth),
                                      authFile ("vyre").getFullPathName(),
                                      std::unique_ptr<sid::update::UpdateTransport> (upd));
        proc.getUpdateChecker().setCheckIntervalSecondsForTesting (1.0e9);
        proc.getUpdateChecker().checkNowForTesting();
        SideChainAudioProcessorEditor editor (proc);
        auto& am = proc.getAuthManager();
        am.startLinking();
        am.pollOnceForTesting();
        am.refreshEntitlementsForTesting();
        openInfo (editor);
        dump (editor, "info-not-confirmed-for-pumpcurve");
    }

    // ---- full version: main view and INFO (licence + subscription) ----------
    {
        auto* auth = new ScriptedAuth();
        auth->subscribed = true;
        auth->licenseActive = true;
        auto* upd = new ScriptedUpdates();
        SideChainAudioProcessor proc (std::unique_ptr<sid::auth::AuthTransport> (auth),
                                      authFile ("full").getFullPathName(),
                                      std::unique_ptr<sid::update::UpdateTransport> (upd));
        proc.getUpdateChecker().setCheckIntervalSecondsForTesting (1.0e9);
        proc.getUpdateChecker().checkNowForTesting();
        SideChainAudioProcessorEditor editor (proc);
        auto& am = proc.getAuthManager();
        am.startLinking();
        am.pollOnceForTesting();
        am.refreshEntitlementsForTesting();
        waitFor ([&] { return proc.getDemoGate().isFullVersion(); });
        feedAudio (proc, editor, 2.0);
        dump (editor, "main-full-version");
        openInfo (editor);
        dump (editor, "info-full-licence-and-plus");
    }

    // ---- control state sheet: every weight in every state --------------------
    // An offscreen render cannot hover a mouse, so the LookAndFeel is asked to
    // draw each state explicitly - the same code paths the live controls use.
    {
        sid::theme::LookAndFeel lf;
        juce::Image sheet (juce::Image::ARGB, 800, 330, true);
        juce::Graphics g (sheet);
        sid::theme::fillField (g, { 0, 0, 800, 330 });
        g.setFont (sid::theme::captionFont (10.0f));

        const char* columns[] = { "DEFAULT", "HOVER", "PRESSED", "DISABLED", "FOCUSED" };
        for (int c = 0; c < 5; ++c)
        {
            g.setColour (juce::Colour (sid::theme::kTextMuted));
            g.drawText (columns[c], 120 + c * 134, 10, 120, 14, juce::Justification::centred);
        }

        struct Row { const char* name; sid::theme::ButtonStyle style; const char* text; };
        const Row rows[] = { { "SECONDARY", sid::theme::ButtonStyle::secondary, "SAVE PRESET" },
                             { "ACCENT",    sid::theme::ButtonStyle::accent,    "CHECK SUBSCRIPTION" },
                             { "PRIMARY",   sid::theme::ButtonStyle::primary,   "SAVE" },
                             { "QUIET",     sid::theme::ButtonStyle::quiet,     "RESET" } };
        int y = 32;
        for (const auto& row : rows)
        {
            g.setColour (juce::Colour (sid::theme::kTextMuted));
            g.drawText (row.name, 14, y, 100, 30, juce::Justification::centredLeft);
            for (int c = 0; c < 5; ++c)
            {
                juce::TextButton b (row.text);
                b.setLookAndFeel (&lf);
                sid::theme::setButtonStyle (b, row.style);
                b.setBounds (0, 0, 120, 30);
                b.setEnabled (c != 3);
                if (c == 4) b.setWantsKeyboardFocus (true);
                juce::Image cell (juce::Image::ARGB, 124, 34, true);
                {
                    juce::Graphics cg (cell);
                    cg.setOrigin (2, 2);
                    lf.drawButtonBackground (cg, b, {}, c == 1, c == 2);
                    if (c == 4)
                    {
                        cg.setColour (juce::Colour (sid::theme::kAccent).withAlpha (0.7f));
                        cg.drawRoundedRectangle (b.getLocalBounds().toFloat().expanded (0.5f), 4.5f, 1.2f);
                    }
                    lf.drawButtonText (cg, b, c == 1, c == 2);
                }
                g.drawImageAt (cell, 120 + c * 134 - 2, y - 2);
                b.setLookAndFeel (nullptr);
            }
            y += 44;
        }

        // Knobs: 0 %, 50 %, 100 %, hover, focused, disabled.
        g.setColour (juce::Colour (sid::theme::kTextMuted));
        g.drawText ("KNOB", 14, y + 30, 100, 30, juce::Justification::centredLeft);
        const float fractions[] = { 0.0f, 0.5f, 1.0f, 0.62f, 0.38f, 0.5f };
        const char* knobLabels[] = { "0%", "50%", "100%", "HOVER", "FOCUSED", "DISABLED" };
        for (int k = 0; k < 6; ++k)
        {
            const auto r = juce::Rectangle<float> (120.0f + k * 110.0f, (float) y, 86.0f, 86.0f);
            sid::theme::drawKnob (g, r, fractions[k], juce::Colour (sid::theme::kAccent),
                                  k == 3, k == 4, k != 5);
            g.setColour (juce::Colour (sid::theme::kTextMuted));
            g.drawText (knobLabels[k], r.translated (0, 88).withHeight (14).toNearestInt(),
                        juce::Justification::centred);
        }

        auto file = outDir.getChildFile ("controls-state-sheet.png");
        file.deleteFile();
        if (auto out = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (sheet, *out);
            ++written;
            std::printf ("  wrote %s (800x330)\n", file.getFileName().toRawUTF8());
        }
    }

    sid::license::resetMonotonicMillisecondsForTest();
    std::printf ("%d snapshot files written\n", written);
    return written > 0 ? 0 : 1;
}
