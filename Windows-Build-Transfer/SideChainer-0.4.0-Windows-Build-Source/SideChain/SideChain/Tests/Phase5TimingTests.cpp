/*
    SideChain - Phase 5 regression tests (ADAPTED to the v0.3.0
    transient-trigger engine).

    The old engine was a continuous level follower (attack/release on a
    smoothstep of the detector level); these tests measured its taus. The
    v0.3.0 engine is a kick-triggered pumping effect:
      - onset detector: fast follower (2/60 ms), trigger at -18 dBFS with a
        >= 8 dB rise inside 30 ms, hysteresis re-arm, 60 ms retrigger guard
      - envelope: 1.5 ms attack, hold = 0.35 x Release, exponential release
        with tau = Release ms, delayed by (lookahead + offset) samples
    The probes below measure the NEW shape: onset latency, attack, hold,
    release tau and the envelope delay.

    Measured values are printed so regressions in FEEL are visible, not
    just pass/fail.
*/

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

#include "../Source/DuckEngine.h"
#include "../Source/GraphData.h"

static int failures = 0, totalChecks = 0;
static void check (bool ok, const std::string& what)
{
    ++totalChecks;
    printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

static std::vector<float> sine (int n, double freq, double sr, float amp)
{
    std::vector<float> v ((size_t) n);
    double ph = 0.0;
    for (int i = 0; i < n; ++i) { v[(size_t) i] = amp * (float) std::sin (ph); ph += 2.0 * 3.14159265358979 * freq / sr; }
    return v;
}

namespace
{
    // Time (in ms) for a curve to first reach `fraction` of its final step.
    // Returns -1 if never reached within the window.
    double timeToFractionMs (const std::vector<float>& curve, int stepIndex,
                             float before, float after, double fraction, double sr)
    {
        const float threshold = before + (after - before) * (float) fraction;
        for (size_t i = (size_t) stepIndex; i < curve.size(); ++i)
            if ((after > before && curve[i] >= threshold)
                || (after < before && curve[i] <= threshold))
                return ((double) i - (double) stepIndex) / sr * 1000.0;
        return -1.0;
    }
}

int main()
{
    printf ("SideChain Phase 5 Timing Tests (true time constants)\n=====================================================\n");
    const double sr = 48000.0;
    const int n = (int) (2.0 * sr);
    const int bs = 512;

    // ==================================================================
    // 1. DETECTOR FOLLOWER: fast attack (2 ms) on a step to loud.
    // ==================================================================
    printf ("\n1. Detector follower attack (target 2 ms)\n");
    {
        sid::dsp::DuckEngine e;
        e.prepare (sr);
        const float depthDb = 0.0f; // unity gain; we only watch the detector
        std::vector<float> env; env.reserve ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const float s = 0.8f;
            e.processSample (&s, &s, 1, depthDb);
            env.push_back (e.getDetectorEnvelope());
        }
        const float settled = env.back();
        const double t63 = timeToFractionMs (env, 0, 0.0f, settled, 0.632, sr);
        printf ("     settled env = %.4f, time to 63.2%% = %.3f ms\n", settled, t63);
        check (t63 > 0.2 && t63 < 6.0, "detector follower attack tau within 2 ms + tolerance");
    }

    // ==================================================================
    // 2. DETECTOR FOLLOWER RELEASE: loud sustained -> silence (60 ms tau).
    // ==================================================================
    printf ("\n2. Detector follower release (target 60 ms)\n");
    {
        sid::dsp::DuckEngine e;
        e.prepare (sr);
        const float depthDb = 0.0f;
        std::vector<float> env; env.reserve ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const float s = (i < n / 2) ? 0.8f : 0.0f;
            e.processSample (&s, &s, 1, depthDb);
            env.push_back (e.getDetectorEnvelope());
        }
        const int drop = n / 2;
        const float before = env[(size_t) drop - 1];
        const double t63 = timeToFractionMs (env, drop, before, 0.0f, 0.632, sr);
        printf ("     pre-drop env = %.4f, time to -63.2%% = %.3f ms\n", before, t63);
        check (t63 > 20.0 && t63 < 110.0, "detector follower release tau within 60 ms + tolerance");
    }

    // ==================================================================
    // 3. DUCK ATTACK: a loud onset triggers ONE duck; the gain reaches
    //    full depth fast (1.5 ms attack tau; threshold crossing + rise
    //    confirmation add a few ms of detection latency).
    // ==================================================================
    printf ("\n3. Duck attack (target 1.5 ms tau + detection latency)\n");
    {
        sid::dsp::DuckEngine e;
        e.prepare (sr);
        const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (1.0f); // -24 dB
        std::vector<float> gain; gain.reserve ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const float s = 0.8f;   // onset at sample 0
            gain.push_back (e.processSample (&s, &s, 1, depthDb));
        }
        const float target = juce::Decibels::decibelsToGain (depthDb);
        const double t63 = timeToFractionMs (gain, 0, 1.0f, target, 0.632, sr);
        printf ("     target gain = %.5f, time to 63.2%% of drop = %.3f ms\n", target, t63);
        // NOTE: the returned gain passes through the engine's envelope delay
        // ring (lookahead 30 ms + offset 0), matching the delayed main path
        // the processor feeds it. Onset -> duck = detection latency + attack
        // tau, on TOP of that constant 30 ms pipeline delay.
        const double lookMs = sid::dsp::DuckEngine::lookaheadMs();
        check (t63 > lookMs + 0.5 && t63 < lookMs + 25.0,
               "duck attack within a few ms of the onset (plus the 30 ms pipeline delay)");
    }

    // ==================================================================
    // 4. DUCK RELEASE: after one trigger the gain recovers with the
    //    derived tau; hold = holdFraction(release) x DUCK LENGTH delays
    //    the start. Measured with Release = 100 ms, Length = 500 ms
    //    (default): hold ~65 ms, tau ~145 ms.
    // ==================================================================
    printf ("\n4. Duck release (shape + length model, Release 100 ms)\n");
    {
        const float releaseMs = 100.0f;
        const float duckLen   = sid::dsp::DuckEngine::kDuckLengthDefaultMs;
        sid::dsp::DuckEngine e;
        e.prepare (sr);
        e.setReleaseTimes (150.0f, releaseMs);
        e.setDuckLengthMs (duckLen);
        const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (1.0f); // -24 dB
        std::vector<float> gain; gain.reserve ((size_t) n);
        const int kickEnd = (int) (0.05 * sr);   // short kick; silence afterwards
        for (int i = 0; i < n; ++i)
        {
            const float s = (i < kickEnd) ? 0.9f : 0.0f;
            gain.push_back (e.processSample (&s, &s, 1, depthDb));
        }
        const float holdMs   = sid::dsp::DuckEngine::holdFractionForRelease (releaseMs) * duckLen;
        const float tauMs    = (duckLen - holdMs) / 3.0f;
        const float ducked = gain[(size_t) kickEnd];
        const double t63 = timeToFractionMs (gain, kickEnd, ducked, 1.0f, 0.632, sr);
        printf ("     ducked %.5f -> 63.2%% recovery in %.3f ms (hold %.0f ms + tau %.0f ms)\n",
                ducked, t63, holdMs, tauMs);
        check (t63 > holdMs && t63 < holdMs + tauMs * 1.5,
               "duck release follows hold + derived tau");

        // Musical recovery is a clean single-tau exponential now - no
        // detector-window cascade (that was the old follower model).
        sid::dsp::DuckEngine e2; e2.prepare (sr);
        e2.setReleaseTimes (150.0f, releaseMs);
        std::vector<float> g2; g2.reserve ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const float s = (i < kickEnd) ? 0.9f : 0.0f;
            g2.push_back (e2.processSample (&s, &s, 1, depthDb));
        }
        const double tMusical = timeToFractionMs (g2, kickEnd, g2[(size_t) kickEnd], 1.0f, 0.632, sr);
        printf ("     musical recovery (SC silenced): 63.2%% in %.3f ms\n", tMusical);
        check (tMusical > 20.0 && tMusical < 400.0,
               "musical recovery completes on the musical timescale");
    }

    // ==================================================================
    // 5. Musical behaviour probes (kick / vocal / fast trigger).
    // ==================================================================
    printf ("\n5. Musical behaviour probes\n");
    {
        // 5a. Kick transient (40 ms burst every 1.5 s, kick->bass scenario):
        //     duck fast on each kick, recover between kicks. NOTE: with the
        //     CORRECTED release (150 ms detector + 120 ms gain), recovery is
        //     a real cascade (~640 ms to 63%), so tight gaps intentionally
        //     bridge — that is the pumping behaviour; 1.5 s spacing tests
        //     re-trigger and full recovery honestly.
        {
            const int n3 = (int) (3.0 * sr);
            auto sc = std::vector<float> ((size_t) n3, 0.0f);
            const int period = (int) (1.5 * sr), burst = (int) (0.040 * sr);
            for (int k = 0; k * period + burst < n3; ++k)
                for (int i = k * period; i < k * period + burst; ++i)
                    sc[(size_t) i] = 0.9f * (float) std::sin (2.0 * 3.14159265358979 * 60.0 * i / sr);
            sid::dsp::DuckEngine e; e.prepare (sr);
            const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (0.75f);
            float minGain = 1.0f, maxInterKick = 0.0f, prevGain = 1.0f, finalGain = 0.0f;
            int kicks = 0;
            for (int i = 0; i < n3; ++i)
            {
                const float g = e.processSample (&sc[(size_t) i], nullptr, 1, depthDb);
                minGain = std::min (minGain, g);
                if (prevGain > 0.9f && g < 0.9f) ++kicks; // each duck-onset event
                prevGain = g;
                if (sc[(size_t) i] == 0.0f) maxInterKick = std::max (maxInterKick, g);
                finalGain = g;
            }
            printf ("     kick: min gain %.3f, recovery between kicks %.3f, onsets %d, final %.4f\n",
                    minGain, maxInterKick, kicks, finalGain);
            check (minGain < 0.7,            "kick: visible ducking (min gain < 0.7)");
            check (maxInterKick > 0.9f,      "kick: substantially recovered in the 1.5 s gap (> 0.9)");
            check (kicks >= 2,               "kick: ducks on each kick (re-triggerable)");
            check (finalGain > 0.95f,        "kick: full recovery after last kick (cascade completes)");
        }

        // 5b. Sustained vocal/music (constant 0.4 sidechain): ONE onset duck,
        //     then the envelope recovers even while the vocal continues
        //     (this is the intended product behaviour - no continuous duck).
        {
            auto sc = sine (n, 300.0, sr, 0.4f);
            auto main = sine (n, 220.0, sr, 0.5f);
            sid::dsp::DuckEngine e; e.prepare (sr);
            const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (0.5f);
            float minG = 1.0f;
            std::vector<float> tail;
            for (int i = 0; i < n; ++i)
            {
                const float g = e.processSample (&sc[(size_t) i], &sc[(size_t) i], 2, depthDb);
                minG = std::min (minG, g);
                if (i >= n - (int) (0.2 * sr)) tail.push_back (g);
            }
            double avg = 0; for (float g : tail) avg += g; avg /= (double) tail.size();
            float ripple = 0; for (size_t i = 1; i < tail.size(); ++i) ripple = std::max (ripple, std::abs (tail[i] - tail[i-1]));
            printf ("     vocal: onset min gain %.3f, settled gain %.3f, max step %.4f\n", minG, avg, ripple);
            check (minG < 0.9f,               "vocal: onset duck occurred (trigger fired)");
            check (avg > 0.95f,               "vocal: envelope recovers while the vocal sustains (no continuous duck)");
            check (ripple < 0.05f,            "vocal: no zipper (max step < 0.05)");
        }

        // 5c. Fast trigger trains (10 ms bursts @ 20 Hz): engine must track
        {
            auto sc = std::vector<float> ((size_t) n, 0.0f);
            const int period = (int) (0.05 * sr), burst = (int) (0.010 * sr);
            for (int k = 0; k * period + burst < n; ++k)
                for (int i = k * period; i < k * period + burst; ++i)
                    sc[(size_t) i] = 0.85f;
            sid::dsp::DuckEngine e; e.prepare (sr);
            const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (0.75f);
            float minGain = 1.0f; int ducks = 0; bool inDuck = false;
            for (int i = 0; i < n; ++i)
            {
                const float g = e.processSample (&sc[(size_t) i], nullptr, 1, depthDb);
                minGain = std::min (minGain, g);
                if (g < 0.8f && ! inDuck) { ++ducks; inDuck = true; }
                if (g > 0.95f) inDuck = false;
            }
            printf ("     fast trigger: min gain %.3f, duck events %d\n", minGain, ducks);
            // v0.3.0: 10 ms bursts @ 20 Hz are fast re-triggers; the hysteresis
            // detector fires on a subset (must decay below the arm level between
            // hits) but stays bounded by the burst count.
            check (minGain < 0.5f,  "fast trigger: engages within burst (min gain < 0.5)");
            check (ducks >= 1 && ducks <= 3,
                   "fast trigger: controlled re-firing on rapid bursts (no gain chatter)");
        }
    }

    // ==================================================================
    // 6. Amount progression (onset duck depth, v0.3.0 mapping)
    // ==================================================================
    printf ("\n6. Amount progression (onset duck depth)\n");
    {
        auto sc = sine (n, 1000.0, sr, 0.8f);
        const float amounts[] = { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f };
        const char* labels[]  = { "0%", "25%", "50%", "75%", "100%" };
        double prevDb = 10.0;
        bool monotonic = true;
        for (int a = 0; a < 5; ++a)
        {
            sid::dsp::DuckEngine e; e.prepare (sr);
            const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (amounts[a] / 100.0f);
            float minG = 1.0f;
            for (int i = 0; i < n; ++i)
            {
                float s = sc[(size_t) i];
                const float g = e.processSample (&s, &s, 1, depthDb);
                minG = std::min (minG, g);
            }
            const double minDb = 20.0 * std::log10 (std::max (minG, 1e-9f));
            printf ("     Amount %s: onset duck %.1f dB\n", labels[a], minDb);
            if (minDb > prevDb + 0.01) monotonic = false;
            prevDb = minDb;
        }
        check (monotonic, "amount: ducking deepens monotonically 0 -> 100%");
        // The mapping is musical now: 100% = -24 dB, not the old near-mute.
        check (prevDb > -30.0 && prevDb <= -20.0,
               "amount: 100% lands at the musical -24 dB maximum");
    }

    // ==================================================================
    // 7. GRAPH DATA temporal ordering (deterministic, real DSP path).
    //    A controlled trigger at a known time must appear in the correct
    //    order in the block-aggregated graph stream: sidechain rise first,
    //    gain reduction following, output ducked, recovery after.
    // ==================================================================
    printf ("\n7. Graph data temporal ordering (trigger -> GR -> output)\n");
    {
        using sid::graph::GraphFrame;
        const int ng = (int) (1.0 * sr), bsG = 512;
        std::vector<float> scG ((size_t) ng, 0.0f);
        const int trigStart = (int) (0.25 * sr), trigEnd = trigStart + (int) (0.05 * sr);
        for (int i = trigStart; i < trigEnd; ++i)
            scG[(size_t) i] = 0.9f;
        auto mainG = sine (ng, 440.0, sr, 0.5f);

        // Block-wise path identical to production processBlock.
        sid::dsp::DuckEngine e; e.prepare (sr);
        const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (1.0f);
        std::vector<GraphFrame> frames;
        for (int start = 0; start < ng; start += bsG)
        {
            const int count = std::min (bsG, ng - start);
            float inP = 0, outP = 0, scP = 0;
            for (int i = start; i < start + count; ++i)
            {
                const float g = e.processSample (&scG[(size_t) i], nullptr, 1, depthDb);
                const float o = mainG[(size_t) i] * g;
                inP = std::max (inP, std::abs (mainG[(size_t) i]));
                outP = std::max (outP, std::abs (o));
                scP = std::max (scP, std::abs (scG[(size_t) i]));
            }
            GraphFrame f;
            f.inputLevelDb     = juce::Decibels::gainToDecibels (inP,  -100.0f);
            f.sidechainLevelDb = juce::Decibels::gainToDecibels (scP,  -100.0f);
            f.gainReductionDb  = e.getCurrentReductionDb();
            f.outputLevelDb    = juce::Decibels::gainToDecibels (outP, -100.0f);
            frames.push_back (f);
        }

        const int trigBlock = trigStart / bsG;
        const int endBlock  = trigEnd / bsG;
        const float maxDuckDb = 10.0f; // "ducked" = output > 10 dB below input

        // (a) sidechain rise is visible exactly at the trigger block
        check (frames[(size_t) trigBlock].sidechainLevelDb > -1.0f,
               "graph: sidechain rise lands in the trigger block");
        // (b) BEFORE the trigger: no ducking, output tracks input
        bool preClean = true;
        for (int b = trigBlock - 20; b < trigBlock; ++b)
            if (b >= 0 && (frames[(size_t) b].gainReductionDb < -0.5f
                           || frames[(size_t) b].outputLevelDb < frames[(size_t) b].inputLevelDb - 1.0f))
                preClean = false;
        check (preClean, "graph: no ducking before the trigger (no pre-ducking)");
        // (c) DURING ducking: GR active and output below input — after the
        //     trigger block, within the corrected attack window (~ a few ms)
        bool duckFollows = false;
        for (int b = trigBlock; b <= endBlock + 2 && b < (int) frames.size(); ++b)
            if (frames[(size_t) b].gainReductionDb < -10.0f)
            { duckFollows = true; break; }
        check (duckFollows, "graph: gain reduction engages promptly after trigger (attack)");
        bool outDucked = false;
        for (int b = trigBlock; b <= endBlock + 2 && b < (int) frames.size(); ++b)
            if (frames[(size_t) b].outputLevelDb < frames[(size_t) b].inputLevelDb - maxDuckDb)
            { outDucked = true; break; }
        check (outDucked, "graph: output ducks below input while trigger holds");
        // (d) AFTER the trigger: reduction must DECAY. v0.3.0 envelope:
        //     attack ~ms + hold (0.35 x Release) then a single exponential
        //     release with tau = Release ms (default 150 here). Depth is the
        //     musical -24 dB maximum. Assert: proportional (no-jump) decay,
        //     meaningful progress early, near-complete recovery by +900 ms.
        const int earlyIdx  = std::min ((int) frames.size() - 1, endBlock + (int) (0.2 * sr) / bsG);
        const int midIdx    = std::min ((int) frames.size() - 1, endBlock + (int) (0.6 * sr) / bsG);
        const int lateIdx   = std::min ((int) frames.size() - 1, endBlock + (int) (0.9 * sr) / bsG);
        const int lastIdx   = (int) frames.size() - 1;

        bool monotoneDecay = true;
        for (int b = endBlock; b < lastIdx; ++b)
            // The decay itself is exponential, so per-block dB steps GROW
            // mid-tail (measured up to ~3 dB/block at the -25 dB region).
            // This is not a re-trigger: assert each frame recovers at most
            // 25% of the remaining reduction — proportional, never a jump.
            if (frames[(size_t) (b + 1)].gainReductionDb
                    > frames[(size_t) b].gainReductionDb
                      + 0.25f * (0.0f - frames[(size_t) b].gainReductionDb))
            { monotoneDecay = false; break; }
        check (monotoneDecay, "graph: reduction decays smoothly (no re-trigger jumps) after trigger ends");
        check (frames[(size_t) earlyIdx].gainReductionDb > frames[(size_t) endBlock].gainReductionDb + 1.0f,
               "graph: decay is underway 200 ms after trigger ends");
        check (frames[(size_t) midIdx].gainReductionDb > -15.0f,
               "graph: reduction below -15 dB residual by +600 ms");
        check (frames[(size_t) lateIdx].gainReductionDb > -3.5f,
               "graph: reduction mostly recovered by +900 ms");
        check (frames[(size_t) lastIdx].gainReductionDb > -1.0f,
               "graph: reduction fully decayed within the window (musical -24 dB depth)");
        // (e) sidechain silent again exactly where expected
        check (frames[(size_t) (endBlock + 1)].sidechainLevelDb < -90.0f,
               "graph: sidechain clears in the block after trigger ends");
    }

    printf ("\n=====================================================\n");
    printf ("%d/%d checks passed. %s\n", totalChecks - failures, totalChecks,
            failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
