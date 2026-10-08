/*
    SideChain 0.3.0 DSP tests - transient-trigger redesign.

    Test-driven validation of the NEW kick-triggered pumping model:
      detector  : isolated kick -> ONE trigger; sustained tone -> none;
                  kick + tail -> one; separated kicks -> two; fast repeats ->
                  controlled retriggering; stereo; silence; rates; blocks
      envelope  : attack timing, hold+release shape, depth, no continuous duck
      offset    : negative/zero/positive steps, bounds, audible timing,
                  latency reporting
      presets   : materially different depths and recoveries for all presets

    Build/run: Tests/run_all.sh (suite 7: TriggerDSPTests)
*/

#include <JuceHeader.h>
#include "../Source/DuckEngine.h"
#include "../Source/PresetManager.h"

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

static int totalChecks = 0, failures = 0;
static void check (bool ok, const std::string& what)
{
    ++totalChecks;
    printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

// A kick: fast-decaying low sine burst. The returned buffer extends from
// t=0 to atSec + 2.0 s of tail so later kicks exist AND the envelope has
// room to fully recover (release tau 150 ms needs ~9 taus to settle).
static std::vector<float> kick (double sr, double atSec, float amp = 0.95f,
                                double burstSec = 0.12)
{
    const int len = (int) ((atSec + 2.0) * sr);
    std::vector<float> v ((size_t) len, 0.0f);
    const int start = (int) (atSec * sr);
    const int kickLen = (int) (burstSec * sr);
    for (int i = 0; i < kickLen && start + i < len; ++i)
    {
        const double env = std::exp (-6.0 * (double) i / kickLen);
        v[(size_t) (start + i)] =
            amp * env * (float) std::sin (2.0 * 3.14159265358979 * 55.0 * (start + i) / sr);
    }
    return v;
}

// Run the engine over a sidechain signal; record gain + trigger count.
struct Run
{
    std::vector<float> gain;
    int triggers = 0;
};

static Run runEngine (double sr, const std::vector<float>& sc,
                      float amountPercent = 75.0f, float releaseMs = 150.0f,
                      int offsetMs = 0, int numChannels = 1)
{
    sid::dsp::DuckEngine e;
    e.prepare (sr);
    e.setReleaseTimes (150.0f, releaseMs);
    e.setOffsetMs (offsetMs);

    Run r;
    r.gain.reserve (sc.size());
    const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (amountPercent / 100.0f);

    for (size_t i = 0; i < sc.size(); ++i)
    {
        const float s = sc[i];
        r.gain.push_back (e.processSample (&s, &s, numChannels, depthDb));
    }
    r.triggers = e.getTriggerCount();
    return r;
}

static double avgGain (const std::vector<float>& g, double fromSec, double toSec, double sr)
{
    const int a = (int) (fromSec * sr), b = std::min ((int) g.size(), (int) (toSec * sr));
    if (b <= a) return 1.0;
    double s = 0;
    for (int i = a; i < b; ++i) s += g[(size_t) i];
    return s / (b - a);
}

static double minGain (const std::vector<float>& g)
{
    float m = 1.0f;
    for (float x : g) m = std::min (m, x);
    return m;
}

// Overlay a kick buffer onto sc without overrunning either buffer.
static void overlay (std::vector<float>& sc, const std::vector<float>& k)
{
    const size_t n = std::min (sc.size(), k.size());
    for (size_t i = 0; i < n; ++i) sc[i] = std::max (sc[i], k[i]);
}

int main()
{
    printf ("SideChain 0.3.0 Trigger-DSP Tests\n=================================\n");
    const double sr = 48000.0;

    // ==================================================================
    printf ("\n1. DETECTOR\n");
    {
        // 1.1 isolated kick -> exactly one trigger; recovers fully.
        {
            auto sc = kick (sr, 0.10);
            auto r = runEngine (sr, sc);
            check (r.triggers == 1,
                   "isolated kick -> exactly ONE trigger (got " + std::to_string (r.triggers) + ")");
            check (r.gain.back() > 0.999f, "isolated kick -> full recovery after the envelope");
            check (minGain (r.gain) < 0.75f, "isolated kick -> actual ducking occurred");
        }

        // 1.2 sustained tone -> NO continuous ducking. A tone stepping up from
        // silence IS an onset (one initial trigger is correct per the model);
        // what must never happen is sustained/continuous retriggering.
        {
            std::vector<float> sc ((size_t) (2.0 * sr));
            double ph = 0.0;
            for (auto& s : sc) { s = 0.7f * (float) std::sin (ph); ph += 2.0 * 3.14159265358979 * 1000.0 / sr; }
            auto r = runEngine (sr, sc);
            check (r.triggers <= 1,
                   "sustained tone -> at most ONE initial trigger (got " + std::to_string (r.triggers) + ")");
            check (r.gain.back() > 0.999f,
                   "sustained tone -> gain recovers to 1.0 (no continuous ducking)");
        }

        // 1.3 kick + sustained tail -> ONE trigger only.
        {
            auto sc = kick (sr, 0.10);
            double ph = 0.0;
            for (size_t i = (size_t) (0.13 * sr); i < sc.size(); ++i)
            {
                sc[i] = 0.6f * (float) std::sin (ph);   // sustained body after the kick
                ph += 2.0 * 3.14159265358979 * 800.0 / sr;
            }
            auto r = runEngine (sr, sc);
            check (r.triggers == 1,
                   "kick + sustained tail -> ONE trigger (got " + std::to_string (r.triggers) + ")");
        }

        // 1.4 two separated kicks -> two triggers.
        {
            auto k1 = kick (sr, 0.10), k2 = kick (sr, 0.60);
            std::vector<float> sc (std::max (k1.size(), k2.size()), 0.0f);
            overlay (sc, k1);
            overlay (sc, k2);
            auto r = runEngine (sr, sc);
            check (r.triggers == 2,
                   "two separated kicks -> TWO triggers (got " + std::to_string (r.triggers) + ")");
        }

        // 1.5 very fast repeated transients -> controlled retriggering.
        //     Short (25 ms) hits every 62.5 ms (16th notes at 240 BPM); the
        //     detector's hysteresis physically limits the rate (the follower
        //     must decay below the arm level between hits), so the assertion
        //     is: several re-fires, strictly bounded by the hit count, and no
        //     runaway/continuous ducking.
        {
            std::vector<float> sc ((size_t) (1.2 * sr), 0.0f);
            int hitCount = 0;
            for (double t = 0.10; t < 1.1; t += 0.0625)
            {
                overlay (sc, kick (sr, t, 0.9f, 0.025));
                ++hitCount;
            }
            auto r = runEngine (sr, sc);
            check (r.triggers >= 2 && r.triggers <= hitCount,
                   "fast 62.5 ms transients -> controlled retriggering (got "
                       + std::to_string (r.triggers) + " of " + std::to_string (hitCount) + ")");
        }

        // 1.6 stereo sidechain: max(L,R) drives the detector.
        {
            auto loud = kick (sr, 0.10, 0.95f);
            auto quiet = kick (sr, 0.10, 0.30f);
            sid::dsp::DuckEngine e;
            e.prepare (sr);
            e.setReleaseTimes (150.0f, 150.0f);
            const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (0.75f);
            std::vector<float> gain;
            for (size_t i = 0; i < loud.size(); ++i)
                gain.push_back (e.processSample (&loud[i], &quiet[i], 2, depthDb));
            check (e.getTriggerCount() == 1, "stereo sidechain triggers from the loud channel");
            check (minGain (gain) < 0.75f, "stereo sidechain produced a real duck");
        }

        // 1.7 silence -> no triggers, gain exactly 1.
        {
            std::vector<float> sc ((size_t) (0.5 * sr), 0.0f);
            auto r = runEngine (sr, sc);
            check (r.triggers == 0, "silence -> no triggers");
            bool allUnity = true;
            for (float g : r.gain) if (g != 1.0f) { allUnity = false; break; }
            check (allUnity, "silence -> gain exactly 1.0 at every sample");
        }

        // 1.8 low-level noise floor -> no spurious triggers.
        {
            std::vector<float> sc ((size_t) (1.0 * sr));
            double ph = 0.0;
            for (auto& s : sc) { s = 1.0e-4f * (float) std::sin (ph); ph += 0.13; }
            auto r = runEngine (sr, sc);
            check (r.triggers == 0, "-80 dBFS noise floor -> no spurious triggers");
        }

        // 1.9 sample-rate robustness (44.1/48/96): same kick -> same behaviour.
        {
            bool oneTriggerAtAllRates = true, duckedAtAllRates = true;
            for (double rate : { 44100.0, 48000.0, 96000.0 })
            {
                auto sc = kick (rate, 0.10);
                auto r = runEngine (rate, sc);
                if (r.triggers != 1) oneTriggerAtAllRates = false;
                if (minGain (r.gain) >= 0.75f) duckedAtAllRates = false;
            }
            check (oneTriggerAtAllRates, "sample rates 44.1/48/96 kHz -> one trigger each");
            check (duckedAtAllRates, "sample rates -> ducking occurs at each rate");
        }

        // 1.10 block-size independence is inherent (per-sample engine), but
        //      verify processing in chunks equals continuous processing.
        {
            auto sc = kick (sr, 0.10);
            auto continuous = runEngine (sr, sc);
            // chunked: same engine driven via the same per-sample API is
            // identical by construction; assert the property explicitly.
            sid::dsp::DuckEngine e;
            e.prepare (sr);
            e.setReleaseTimes (150.0f, 150.0f);
            const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (0.75f);
            std::vector<float> chunked;
            for (size_t start = 0; start < sc.size(); start += 512)
            {
                const size_t end = std::min (sc.size(), start + 512);
                for (size_t i = start; i < end; ++i)
                    chunked.push_back (e.processSample (&sc[i], &sc[i], 1, depthDb));
            }
            bool identical = chunked.size() == continuous.gain.size();
            for (size_t i = 0; identical && i < chunked.size(); ++i)
                if (std::abs (chunked[i] - continuous.gain[i]) > 1.0e-6f) identical = false;
            check (identical, "block-size independence: chunked == continuous");
        }
    }

    // ==================================================================
    printf ("\n2. ENVELOPE\n");
    {
        // 2.1 attack timing: gain reaches full depth within ~10 ms of trigger.
        {
            auto sc = kick (sr, 0.05);
            auto r = runEngine (sr, sc, 75.0f, 300.0f);
            const int triggerSample = (int) r.gain.size() * 0; // find first dip start
            int dipStart = -1;
            for (size_t i = 1; i < r.gain.size(); ++i)
                if (r.gain[i] < r.gain[(size_t) (i - 1)] - 1.0e-4f) { dipStart = (int) i; break; }
            check (dipStart > 0, "envelope: dip begins after the kick");
            (void) triggerSample;
            const float depthTarget = juce::Decibels::decibelsToGain (
                sid::dsp::DuckEngine::amountToDepthDb (0.75f));   // already <= 0 dB
            int reached = -1;
            for (size_t i = (size_t) dipStart; i < r.gain.size(); ++i)
                if (r.gain[i] <= depthTarget * 1.2f) { reached = (int) i; break; }
            check (reached > 0 && reached - dipStart <= (int) (0.010 * sr),
                   "envelope: reaches full depth within ~10 ms (attack)");
        }

        // 2.2 release: after the kick, gain returns above 90 % of unity.
        {
            auto sc = kick (sr, 0.05);
            auto r = runEngine (sr, sc, 75.0f, 150.0f);
            // Release tau = 150 ms; find the sample of min gain then measure recovery.
            size_t minPos = 0;
            for (size_t i = 0; i < r.gain.size(); ++i)
                if (r.gain[i] < r.gain[minPos]) minPos = i;
            const double recoveredWithinSec = 0.9;  // 0.9 s >> 5 tau
            const auto idx = std::min (minPos + (size_t) (recoveredWithinSec * sr),
                                       r.gain.size() - 1);
            check (r.gain[idx] > 0.95f,
                   "envelope: smooth recovery to >95%% after the duck");
        }

        // 2.3 depth: 100 % Amount = -24 dB (musical), not near-mute.
        {
            auto sc = kick (sr, 0.05);
            auto r = runEngine (sr, sc, 100.0f, 300.0f);
            const double minDb = 20.0 * std::log10 (std::max ((double) minGain (r.gain), 1e-9));
            check (minDb > -30.0 && minDb < -15.0,
                   "envelope: 100%% depth lands in the musical pump range ("
                       + std::to_string (minDb) + " dB)");
        }

        // 2.4 Amount = 0 -> no ducking even with kicks.
        {
            auto sc = kick (sr, 0.05);
            auto r = runEngine (sr, sc, 0.0f, 150.0f);
            check (minGain (r.gain) > 0.999f, "envelope: Amount 0% -> strictly unity gain");
        }

        // 2.5 no continuous reduction: long silence after a kick fully recovers.
        {
            auto sc = kick (sr, 0.05);
            auto r = runEngine (sr, sc, 75.0f, 150.0f);
            check (r.gain.back() > 0.999f,
                   "envelope: no residual reduction long after the kick");
        }
    }

    // ==================================================================
    printf ("\n3. OFFSET + LATENCY\n");
    {
        const double rate = 48000.0;
        // 3.1 latency reporting: constant lookahead (30 ms * 48 kHz = 1440).
        {
            sid::dsp::DuckEngine e;
            e.prepare (rate);
            check (sid::dsp::DuckEngine::lookaheadSamples (rate) == 1440,
                   "latency: lookahead at 48 kHz is 1440 samples (30 ms)");
            check (sid::dsp::DuckEngine::lookaheadSamples (44100.0) == 1323,
                   "latency: lookahead at 44.1 kHz is 1323 samples");
        }

        // 3.2 offset bounds clamp.
        {
            sid::dsp::DuckEngine e;
            e.prepare (rate);
            e.setOffsetMs (999);
            check (e.getOffsetMs() == sid::dsp::DuckEngine::kOffsetMaxMs, "offset: clamped to +30 ms");
            e.setOffsetMs (-999);
            check (e.getOffsetMs() == sid::dsp::DuckEngine::kOffsetMinMs, "offset: clamped to -30 ms");
            e.setOffsetMs (0);
            check (e.getOffsetMs() == 0, "offset: zero is exact");
        }

        // 3.3 positive offset delays the audible duck.
        {
            auto sc = kick (sr, 0.05);
            auto zero = runEngine (sr, sc, 75.0f, 150.0f, 0);
            auto plus = runEngine (sr, sc, 75.0f, 150.0f, 10);
            // find dip start in each
            auto dipStart = [] (const std::vector<float>& g)
            {
                for (size_t i = 1; i < g.size(); ++i)
                    if (g[i] < g[i - 1] - 1.0e-4f) return (int) i;
                return -1;
            };
            const int d0 = dipStart (zero.gain), dP = dipStart (plus.gain);
            check (d0 > 0 && dP > 0, "offset: both runs duck");
            const int expectedDelay = (int) std::lround (10 * rate / 1000.0);
            check (std::abs ((dP - d0) - expectedDelay) <= (int) (0.002 * rate),
                   "offset: +10 ms delays the audible duck by ~10 ms (delta "
                       + std::to_string (dP - d0) + " samples)");
        }

        // 3.4 negative offset ducks earlier on the delayed timeline (lookahead):
        // the output envelope appears before the +0 version by ~lookahead+|off|...:
        // observable behaviour: with -10 ms, duck starts ~10 ms EARLIER than 0.
        {
            auto sc = kick (sr, 0.05);
            auto zero = runEngine (sr, sc, 75.0f, 150.0f, 0);
            auto minus = runEngine (sr, sc, 75.0f, 150.0f, -10);
            auto dipStart = [] (const std::vector<float>& g)
            {
                for (size_t i = 1; i < g.size(); ++i)
                    if (g[i] < g[i - 1] - 1.0e-4f) return (int) i;
                return -1;
            };
            const int d0 = dipStart (zero.gain), dM = dipStart (minus.gain);
            const int expectedShift = (int) std::lround (10 * rate / 1000.0);
            check (dM < d0 && std::abs ((d0 - dM) - expectedShift) <= (int) (0.002 * rate),
                   "offset: -10 ms pulls the duck ~10 ms earlier (delta "
                       + std::to_string (d0 - dM) + " samples)");
        }

        // 3.5 stepped offsets: one step = exactly 1 ms shift.
        {
            auto sc = kick (sr, 0.05);
            auto zero = runEngine (sr, sc, 75.0f, 150.0f, 0);
            auto one  = runEngine (sr, sc, 75.0f, 150.0f, 1);
            auto dipStart = [] (const std::vector<float>& g)
            {
                for (size_t i = 1; i < g.size(); ++i)
                    if (g[i] < g[i - 1] - 1.0e-4f) return (int) i;
                return -1;
            };
            const int shift = dipStart (one.gain) - dipStart (zero.gain);
            check (std::abs (shift - (int) std::lround (rate / 1000.0)) <= 2,
                   "offset: one 1 ms step shifts the duck ~1 ms (delta "
                       + std::to_string (shift) + " samples)");
        }

        // 3.6 no discontinuities: max single-sample gain step is small.
        {
            auto sc = kick (sr, 0.05);
            auto r = runEngine (sr, sc, 100.0f, 150.0f, -30);
            float maxStep = 0;
            for (size_t i = 1; i < r.gain.size(); ++i)
                maxStep = std::max (maxStep, (float) std::abs (r.gain[i] - r.gain[i - 1]));
            check (maxStep < 0.05f, "offset -30 ms: no clicks (max single-sample step "
                                        + std::to_string (maxStep) + ")");
        }
    }

    // ==================================================================
    printf ("\n4. PRESETS (musical pumping values)\n");
    {
        const auto& table = sid::presets::factoryPresets();
        check ((int) table.size() == 10, "preset table intact (10 factory presets)");

        auto sc = kick (sr, 0.05);
        double prevDepth = -1e9;
        bool monotonic = true;
        std::string detail;

        for (const auto& p : table)
        {
            auto r = runEngine (sr, sc, p.amountPercent, p.releaseMs);
            const double depthDb = -20.0 * std::log10 (std::max ((double) minGain (r.gain), 1e-9));
            detail += "\n    " + p.name.toStdString() + ": depth "
                          + std::to_string (depthDb) + " dB";

            // Table is ordered shallow->deep; a DECREASE breaks the ordering.
            if (depthDb < prevDepth - 0.5) monotonic = false;
            prevDepth = depthDb;

            // Every preset must stay in the musical range (never near-mute).
            check (depthDb < 30.0,
                   "preset " + p.name.toStdString() + " stays musical (< 30 dB)");
            // Every preset (except Subtle) must duck audibly.
            if (p.name != "Subtle")
                check (depthDb > 2.0,
                       "preset " + p.name.toStdString() + " ducks audibly (> 2 dB)");
        }
        printf ("%s\n", detail.c_str());
        check (monotonic, "preset depths increase monotonically through the table");

        // Extreme must be the deepest.
        {
            auto rE = runEngine (sr, sc, 100.0f, 700.0f);
            auto rS = runEngine (sr, sc, 15.0f, 150.0f);
            const double dE = -20.0 * std::log10 (std::max ((double) minGain (rE.gain), 1e-9));
            const double dS = -20.0 * std::log10 (std::max ((double) minGain (rS.gain), 1e-9));
            check (dE > dS + 15.0, "Extreme ducks much deeper than Subtle");
        }

        // Release differences: longer release = slower recovery.
        {
            auto shortR = runEngine (sr, sc, 75.0f, 100.0f);
            auto longR  = runEngine (sr, sc, 75.0f, 600.0f);
            // average gain 150 ms after kick start should be lower with long release
            const double a1 = avgGain (shortR.gain, 0.10, 0.30, sr);
            const double a2 = avgGain (longR.gain, 0.10, 0.30, sr);
            check (a2 < a1, "longer Release -> slower recovery (lower avg gain in the tail)");
        }
    }

    // ==================================================================
    printf ("\n5. CPU sanity (10 s of kicks at 48 kHz)\n");
    {
        std::vector<float> sc ((size_t) (10.0 * sr), 0.0f);
        for (double t = 0.05; t < 10.0; t += 0.5)
            overlay (sc, kick (sr, t));
        auto t0 = juce::Time::getHighResolutionTicks();
        auto r = runEngine (sr, sc);
        const double ms = 1000.0 * juce::Time::highResolutionTicksToSeconds (
            juce::Time::getHighResolutionTicks() - t0);
        check (ms < 500.0, "10 s of DSP processes in < 500 ms (took "
                               + std::to_string (ms) + " ms)");
        check (r.triggers >= 18, "kick train triggers consistently (got "
                                     + std::to_string (r.triggers) + ")");
    }

    printf ("\n==============================\n");
    if (failures == 0)
        printf ("%d/%d checks passed. ALL TESTS PASSED\n", totalChecks, totalChecks);
    else
        printf ("%d/%d checks passed. %d FAILURES\n", totalChecks - failures, totalChecks, failures);
    printf ("==============================\n");
    return failures == 0 ? 0 : 1;
}
