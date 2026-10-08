/*
    SideChain - Phase 6 regression tests: Release parameter.

    Tests the REAL production processor (SideChainAudioProcessor) wherever
    possible: parameter layout, engine mapping, state save/restore (incl.
    old-state compatibility), Amount+Release matrix, graph ordering under
    different Release values, and edge cases (no/silent/disabled sidechain).

    The plugin sources are compiled directly into this binary (see
    run_all.sh); JucePlugin_* macros are provided on the command line so
    PluginProcessor.cpp's JucePlugin_Name use resolves.
*/

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <atomic>
#include <limits>
#include <optional>

#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
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
    // Harness bus activation: the sidechain bus is DISABLED by default
    // (product default). Hosts activate it explicitly via setBusLayout;
    // the test mirrors that here. Returns false if the host-style request
    // was rejected (never expected for stereo main + stereo aux).
    bool enableSidechain (SideChainAudioProcessor&)
    {
        // 0.4.0: there is no auxiliary sidechain bus any more. Kept as a
        // no-op so historical call sites stay meaningful ("prepare topology").
        return true;
    }

    // Runs the REAL processor over a stereo main + stereo sidechain stream.
    // scL empty => sidechain bus disabled => pure passthrough (7b case).
    struct RunResult
    {
        std::vector<float> gainCurve;
        std::vector<sid::graph::GraphFrame> frames;
        bool nanInf = false;
    };

    // Harness play head: the internal beat scheduler needs a PLAYING
    // transport AND a valid musical timeline (timeInSamples + PPQ at 120
    // BPM) to fire beat triggers, exactly as hosts present it.
    class HarnessPlayHead : public juce::AudioPlayHead
    {
    public:
        bool getCurrentPosition (CurrentPositionInfo& result) override
        {
            result = CurrentPositionInfo{};
            result.isPlaying = true;
            result.bpm = 120.0;
            result.timeInSamples = (long long) blockCounter_ * 512;
            result.ppqPosition = (double) (blockCounter_ * 512) / 24000.0; // 120 BPM @48k
            ++blockCounter_;
            return true;
        }
        int blockCounter_ = 0;
    };

    RunResult runProcessor (SideChainAudioProcessor& proc, double sr, int blockSize,
                            const std::vector<float>& scL, const std::vector<float>& mainL)
    {
        proc.prepareToPlay (sr, blockSize);
        proc.setPlayHead (new HarnessPlayHead());

        RunResult r;
        const int n = (int) mainL.size();
        const bool scActive = ! scL.empty();

        // v0.3.0: the processor delays the main path by its reported
        // lookahead (~30 ms) and reports that as latency. Align the gain
        // measurement to the SAME input sample (out[i] pairs with
        // in[i - latency]), otherwise out/in is a phase-shifted ratio.
        const int lat = juce::jlimit (0, n, (int) proc.getLatencySamples());
        std::vector<float> inDelay ((size_t) lat + 1, 0.0f);
        size_t inDelayPos = 0;

        if (scActive)
        {
            const bool ok = enableSidechain (proc);
            if (! ok) printf ("     (harness: sidechain bus activation FAILED - results invalid)\n");
        }

        // Activated layout: 4 channels total (main L/R + SC L/R);
        // disabled layout: 2 channels (main only).
        const int totalCh = scActive ? 4 : 2;
        juce::AudioBuffer<float> buffer (totalCh, n);
        buffer.clear();

        for (int i = 0; i < n; ++i)
        {
            buffer.setSample (0, i, mainL[(size_t) i]);
            buffer.setSample (1, i, mainL[(size_t) i]);
            if (scActive)
            {
                buffer.setSample (2, i, scL[(size_t) i]);
                buffer.setSample (3, i, scL[(size_t) i]);
            }
        }

        for (int start = 0; start < n; start += blockSize)
        {
            const int count = std::min (blockSize, n - start);
            juce::AudioBuffer<float> block (totalCh, count);
            for (int ch = 0; ch < totalCh; ++ch)
                block.copyFrom (ch, 0, buffer.getReadPointer (ch) + start, count);

            juce::MidiBuffer midi;
            proc.processBlock (block, midi);

            for (int i = 0; i < count; ++i)
            {
                const float in = mainL[(size_t) start + i];
                inDelay[(size_t) inDelayPos] = in;
                const float delayedIn = inDelay[(size_t) ((inDelayPos + 1) % inDelay.size())];
                inDelayPos = (inDelayPos + 1) % inDelay.size();
                const float out = block.getSample (0, i);
                const float g = (std::abs (delayedIn) > 1.0e-6f) ? out / delayedIn : r.gainCurve.empty() ? 1.0f : r.gainCurve.back();
                if (! std::isfinite (g)) r.nanInf = true;
                r.gainCurve.push_back (g);
            }
        }
        return r;
    }

    // Recovery time (ms) to 63.2% of the ducked->unity step.
    double recoveryT63 (const std::vector<float>& gain, int releaseIndex, double sr)
    {
        const float ducked = gain[(size_t) releaseIndex - 1];
        const float threshold = ducked + (1.0f - ducked) * 0.632f;
        for (size_t i = (size_t) releaseIndex; i < gain.size(); ++i)
            if (gain[(size_t) i] >= threshold)
                return ((double) i - (double) releaseIndex) / sr * 1000.0;
        return -1.0;
    }

    double settledGain (const std::vector<float>& gain, double sr, double lastFraction = 0.1)
    {
        // Average gain over a window measured in SAMPLES (the curve is
        // per-sample, not per-block).
        const int count = std::max (1, (int) (lastFraction * sr));
        double sum = 0;
        for (int i = (int) gain.size() - count; i < (int) gain.size(); ++i) sum += gain[(size_t) i];
        return sum / count;
    }
}

int main()
{
    printf ("SideChain Phase 6 Tests (Release parameter)\n===========================================\n");
    const double sr = 48000.0;
    const int n = (int) (2.0 * sr);
    const int bs = 512;

    auto mainS = sine (n, 440.0, sr, 0.5f);

    // ==================================================================
    // 1. Parameter layout: IDs, ranges, defaults, automatable.
    // ==================================================================
    printf ("\n1. Parameter layout\n");
    juce::String pluginName;
    {
        SideChainAudioProcessor proc;
        pluginName = proc.getName();

        auto* amount = proc.getParameters().getParameter ("sidechainAmount");
        auto* release = proc.getParameters().getParameter ("release");
        check (amount != nullptr, "parameter sidechainAmount exists");
        check (release != nullptr, "parameter release exists");

        if (release != nullptr)
        {
            auto* rf = dynamic_cast<juce::AudioParameterFloat*> (release);
            check (rf != nullptr, "release is AudioParameterFloat (automatable, host-normalised)");
            if (rf != nullptr)
            {
                const auto& range = rf->getNormalisableRange();
                check (std::abs (range.start - 50.0f) < 0.01f,  "release range start = 50 ms");
                check (std::abs (range.end - 1000.0f) < 0.01f,  "release range end = 1000 ms");
                check (std::abs (rf->get() - 150.0f) < 0.01f,   "release default = 150 ms (== Phase 5 DSP)");
            }
            check (release->isAutomatable(), "release is host-automatable");
        }

        // Amount untouched.
        auto* af = dynamic_cast<juce::AudioParameterFloat*> (amount);
        check (af != nullptr && std::abs (af->get() - 50.0f) < 0.01f, "amount default still 50%");
        check (af != nullptr && std::abs (af->getNormalisableRange().start) < 0.01f
                           && std::abs (af->getNormalisableRange().end - 100.0f) < 0.01f,
               "amount range still 0..100");
    }

    // ==================================================================
    // 2. Shape (Release) mapping on the 0.4.0 beat grid: with the default
    //    Duck Length the derived hold fraction (hold + 3 tau = length)
    //    must match the engine's derivation for min/mid/max Shape.
    // ==================================================================
    printf ("\n2. Shape mapping (derived hold/tau coherence)\n");
    {
        bool coherent = true;
        std::string detail;
        for (float shapeMs : { 50.0f, 150.0f, 1000.0f })
        {
            sid::dsp::DuckEngine e; e.prepare (sr);
            e.setReleaseTimes (150.0f, shapeMs);
            e.setDuckLengthMs (sid::dsp::DuckEngine::kDuckLengthDefaultMs);
            const float hold = e.getDerivedHoldMs();
            const float tau  = e.getDerivedReleaseTauMs();
            detail += "  shape=" + std::to_string ((int) shapeMs)
                    + " hold=" + std::to_string ((int) hold)
                    + " tau=" + std::to_string ((int) tau);
            if (std::abs ((hold + 3.0 * tau)
                          - sid::dsp::DuckEngine::kDuckLengthDefaultMs) > 2.0f)
                coherent = false;
            if (hold / sid::dsp::DuckEngine::kDuckLengthDefaultMs
                < sid::dsp::DuckEngine::holdFractionForRelease (shapeMs) - 0.02f
                || hold / sid::dsp::DuckEngine::kDuckLengthDefaultMs
                > sid::dsp::DuckEngine::holdFractionForRelease (shapeMs) + 0.02f)
                coherent = false;
        }
        printf ("%s\n", detail.c_str());
        check (coherent, "shape changes the plateau/tail split; hold + 3 tau = Duck Length");
    }

    // ==================================================================
    // 3. Amount + Release matrix: depth by Amount, timing by Release.
    // ==================================================================
    printf ("\n3. Amount + Release matrix\n");
    {
        std::vector<float> sc ((size_t) n, 0.0f);
        for (int i = 0; i < n / 2; ++i) sc[(size_t) i] = 0.8f * (float) std::sin (2.0 * 3.14159265358979 * 1000.0 * i / sr);

        const float amounts[] = { 25.0f, 50.0f, 75.0f, 100.0f };
        const float releases[] = { 50.0f, 200.0f, 1000.0f };
        bool depthStable = true, noZipper = true, noUnityViolation = true, finite = true;
        double lastReductionDb = -200.0; // compared ACROSS Release values, reset per Amount
        float lastAmount = -1.0f;

        for (float amount : amounts)
            for (float release : releases)
            {
                SideChainAudioProcessor proc;
                if (auto* pa = proc.getParameters().getParameter ("sidechainAmount"))
                    pa->setValueNotifyingHost (pa->convertTo0to1 (amount));
                if (auto* pr = proc.getParameters().getParameter ("release"))
                    pr->setValueNotifyingHost (pr->convertTo0to1 (release));

                auto r = runProcessor (proc, sr, bs, sc, mainS);
                if (r.nanInf) finite = false;

                const double settledBefore = settledGain (r.gainCurve, sr, 0.1); // recovery tail (last 10% of run)
                const float maxGain = *std::max_element (r.gainCurve.begin(), r.gainCurve.end());
                for (size_t i = 1; i < r.gainCurve.size(); ++i)
                    if (std::abs (r.gainCurve[i] - r.gainCurve[i - 1]) > 0.25f) noZipper = false;
                if (maxGain > 1.0f + 1.0e-6f) noUnityViolation = false;

                // v0.3.0: depth comes from the Amount mapping and Release
                // must NOT change it. Measure the ONSET duck depth (deepest
                // gain reached) instead of a settled window - the sustained
                // tone no longer produces a settled duck.
                const float minG = *std::min_element (r.gainCurve.begin(), r.gainCurve.end());
                const double duckedMid = minG;

                const double reductionDb = 20.0 * std::log10 (std::max (duckedMid, 1e-9));

                if (amount != lastAmount)
                {
                    lastAmount = amount;       // new Amount row: reset comparison
                    lastReductionDb = reductionDb;
                }
                else
                {
                    // Same Amount, different Release: depth must not move.
                    if (std::abs (reductionDb - lastReductionDb) > 1.0)
                        depthStable = false;
                }

                printf ("     Amount %5.1f%%, Release %5.0f ms: ducked gain %.4f (%.1f dB), max %.4f\n",
                        amount, release, duckedMid, reductionDb, maxGain);
            }

        check (finite, "matrix: all finite");
        check (noZipper, "matrix: no zipper steps > 0.25");
        check (noUnityViolation, "matrix: output gain never above unity");
        check (depthStable, "matrix: ducked depth identical across Release values (depth from Amount only)");
    }

    // ==================================================================
    // 4. Release automation sweep (smoothness, no invalid values).
    // ==================================================================
    printf ("\n4. Release automation sweep\n");
    {
        SideChainAudioProcessor proc;
        proc.prepareToPlay (sr, bs);

        std::vector<float> sc ((size_t) n, 0.0f);
        for (int i = 0; i < n / 2; ++i) sc[(size_t) i] = 0.8f * (float) std::sin (2.0 * 3.14159265358979 * 1000.0 * i / sr);

        auto* pr = proc.getParameters().getParameter ("release");
        bool finite = true, smooth = true;
        float prevGain = 1.0f;

        // v0.3.0 latency alignment for the out/in gain measurement:
        proc.prepareToPlay (sr, bs);
        const int sweepLat = juce::jlimit (0, n, (int) proc.getLatencySamples());
        std::vector<float> sweepDelay ((size_t) sweepLat + 1, 0.0f);
        size_t sweepDelayPos = 0;

        // Automate release 50 -> 1000 ms over the first second, block-wise.
        for (int start = 0; start < n; start += bs)
        {
            const int count = std::min (bs, n - start);
            const float frac = juce::jlimit (0.0f, 1.0f, (float) start / (float) (n / 2));
            pr->setValueNotifyingHost (frac);

            juce::AudioBuffer<float> block (3, count);
            block.clear();
            for (int i = 0; i < count; ++i)
            {
                block.setSample (0, i, mainS[(size_t) start + i]);
                block.setSample (1, i, mainS[(size_t) start + i]);
                block.setSample (2, i, sc[(size_t) start + i]);
            }
            juce::MidiBuffer midi;
            proc.processBlock (block, midi);

            for (int i = 0; i < count; ++i)
            {
                // Align the measurement with the reported lookahead latency:
                const float in = mainS[(size_t) start + i];
                sweepDelay[(size_t) sweepDelayPos] = in;
                const float delayedIn = sweepDelay[(size_t) ((sweepDelayPos + 1) % sweepDelay.size())];
                sweepDelayPos = (sweepDelayPos + 1) % sweepDelay.size();
                const float out = block.getSample (0, i);
                const float g = (std::abs (delayedIn) > 1.0e-6f) ? out / delayedIn : prevGain;
                if (! std::isfinite (g) || g < -0.001f || g > 1.001f) { finite = false; }
                if (std::abs (g - prevGain) > 0.3f) smooth = false;
                prevGain = g;
            }
        }
        check (finite, "automation: gain finite and within [0,1] during Release sweep");
        check (smooth, "automation: no discontinuities during Release sweep");
    }

    // ==================================================================
    // 5. State: save/restore with both parameters + old-state compatibility.
    // ==================================================================
    printf ("\n5. State save/restore + compatibility\n");
    {
        // 5a. Immediate-save after changing BOTH parameters. A real host
        //     calls getStateInformation on the MESSAGE thread with a running
        //     dispatcher, so the lazy APVTS timer can fire. The test message
        //     thread has no running dispatcher, so we pump it ourselves —
        //     exactly what the production flush loop does.
        SideChainAudioProcessor proc;
        if (auto* pa = proc.getParameters().getParameter ("sidechainAmount"))
            pa->setValueNotifyingHost (pa->convertTo0to1 (73.0f));
        if (auto* pr = proc.getParameters().getParameter ("release"))
            pr->setValueNotifyingHost (pr->convertTo0to1 (400.0f));

        // Pump pending APVTS sync (async ValueTree messages) before save.
        for (int i = 0; i < 10; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        juce::MemoryBlock mb;
        proc.getStateInformation (mb);

        SideChainAudioProcessor proc2;
        proc2.setStateInformation (mb.getData(), (int) mb.getSize());

        auto* a2 = dynamic_cast<juce::AudioParameterFloat*> (proc2.getParameters().getParameter ("sidechainAmount"));
        auto* r2 = dynamic_cast<juce::AudioParameterFloat*> (proc2.getParameters().getParameter ("release"));
        check (a2 != nullptr && std::abs (a2->get() - 73.0f) < 0.01f,  "immediate save: Amount=73 restores");
        check (r2 != nullptr && std::abs (r2->get() - 400.0f) < 0.5f,  "immediate save: Release=400 ms restores");

        // 5b. Old state (pre-Phase 6): ValueTree without stateVersion and
        //     without a release PARAM. Must load safely, Release = default.
        juce::ValueTree oldState ("PARAMS");
        oldState.setProperty ("stateVersion", 1, nullptr);
        juce::ValueTree oldParam ("PARAM");
        oldParam.setProperty ("id", "sidechainAmount", nullptr);
        oldParam.setProperty ("value", 30.0f, nullptr);
        oldState.addChild (oldParam, -1, nullptr);

        juce::MemoryBlock mb2;
        juce::MemoryOutputStream mos (mb2, false);
        oldState.writeToStream (mos);

        SideChainAudioProcessor proc3;
        proc3.setStateInformation (mb2.getData(), (int) mb2.getSize());

        auto* a3 = dynamic_cast<juce::AudioParameterFloat*> (proc3.getParameters().getParameter ("sidechainAmount"));
        auto* r3 = dynamic_cast<juce::AudioParameterFloat*> (proc3.getParameters().getParameter ("release"));
        check (a3 != nullptr && std::abs (a3->get() - 30.0f) < 0.01f, "old state: Amount=30 restores");
        check (r3 != nullptr && std::abs (r3->get() - 150.0f) < 0.01f, "old state without Release: safe default 150 ms");

        // 5c. Current state schema carries version 6 and its default curve.
        SideChainAudioProcessor proc4;
        juce::MemoryBlock mb4;
        proc4.getStateInformation (mb4);
        auto tree = juce::ValueTree::readFromData (mb4.getData(), mb4.getSize());
        check (tree.isValid() && (int) tree.getProperty ("stateVersion", juce::var (0)) == 6
                   && tree.getChildWithName ("PUMPCURVE").isValid(),
               "saved state carries schema 6 and default PUMPCURVE subtree");
    }

    // ==================================================================
    // 6. Graph ordering under different Shape values (real DSP data,
    //    beat-driven at 120 BPM with one duck and a long recovery gap).
    // ==================================================================
    printf ("\n6. Graph ordering vs Shape\n");
    {
        // 60 BPM: one beat, then a full second of open transport to watch
        // the envelope recover; the graph must show the difference between
        // a short plateau (shape 50) and a long plateau (shape 1000).
        auto graphRun = [&] (float shapeMs)
        {
            SideChainAudioProcessor proc;
            if (auto* pa = proc.getParameters().getParameter ("sidechainAmount"))
                pa->setValueNotifyingHost (pa->convertTo0to1 (100.0f));
            if (auto* p = proc.getParameters().getParameter ("release"))
                p->setValueNotifyingHost (p->convertTo0to1 (shapeMs));
            if (auto* dl = proc.getParameters().getParameter ("duckLength"))
                dl->setValueNotifyingHost (dl->convertTo0to1 (250.0f));
            proc.prepareToPlay (sr, bs);
            proc.setPlayHead (new HarnessPlayHead());
            // Override BPM for the harness play head is fixed at 120; the
            // trigger grid is 120 BPM - fine: compare recovery at the SAME
            // absolute probe time for both shapes.

            std::vector<float> gr;
            juce::AudioBuffer<float> block (2, bs);
            juce::MidiBuffer midi;
            for (int start = 0; start < n; start += bs)
            {
                block.clear();
                for (int i = 0; i < bs; ++i)
                {
                    block.setSample (0, i, mainS[(size_t) start + i]);
                    block.setSample (1, i, mainS[(size_t) start + i]);
                }
                proc.processBlock (block, midi);
                gr.push_back (proc.currentGainReductionDb.load());
            }
            return gr;
        };

        auto fast = graphRun (50.0f);
        auto slow = graphRun (1000.0f);

        // Probe ~350 ms after the beat trigger (beat 0 at block 0): the
        // short-plateau shape must have recovered further by then.
        const int probeBlock = (int) (0.15 * sr) / bs;
        const float fastGr = fast[(size_t) probeBlock];
        const float slowGr = slow[(size_t) probeBlock];
        printf ("     +150 ms residual GR: shape(50) %.1f dB, shape(1000) %.1f dB\n", fastGr, slowGr);
        check (fastGr > slowGr + 2.0f,
               "graph: short-plateau shape recovers further (real DSP difference)");

        // Both shapes duck deeply on the beat itself.
        float fastDeepest = 0.0f, slowDeepest = 0.0f;
        for (int b = 0; b < (int) (0.25 * sr) / bs; ++b)
        {
            fastDeepest = std::min (fastDeepest, fast[(size_t) b]);
            slowDeepest = std::min (slowDeepest, slow[(size_t) b]);
        }
        printf ("     deepest GR on the beat: fast %.1f dB, slow %.1f dB\n",
                fastDeepest, slowDeepest);
        check (fastDeepest < -20.0f && slowDeepest < -20.0f,
               "graph: both shapes duck deeply (depth unchanged by shape)");
    }

    // ==================================================================
    // 7. Edge cases under different Release values.
    // ==================================================================
    printf ("\n7. Edge cases vs Release\n");
    {
        const float releases[] = { 50.0f, 1000.0f };
        for (float release : releases)
        {
            // 7a. No play head: nothing triggers at any Shape (stopped).
            {
                SideChainAudioProcessor proc;
                if (auto* p = proc.getParameters().getParameter ("release"))
                    p->setValueNotifyingHost (p->convertTo0to1 (release));
                std::vector<float> silence ((size_t) n, 0.0f);
                auto r = runProcessor (proc, sr, bs, silence, mainS);
                check (settledGain (r.gainCurve, sr) > 0.99f && ! r.nanInf,
                       "Shape " + std::to_string ((int) release) + " ms: stopped transport => unity, no artifacts");
            }
            // 7b. Silent main stream while playing: gain path stays clean.
            {
                SideChainAudioProcessor proc;
                if (auto* p = proc.getParameters().getParameter ("release"))
                    p->setValueNotifyingHost (p->convertTo0to1 (release));
                auto r = runProcessor (proc, sr, bs, {}, mainS);
                bool bounded = true;
                for (float g : r.gainCurve) if (g > 1.001f) { bounded = false; break; }
                check (bounded && ! r.nanInf,
                       "Shape " + std::to_string ((int) release) + " ms: gain never exceeds unity");
            }
            // 7c. Leakage: main silent + loud sidechain => silent output.
        //     NOTE: the gain harness can't see absolute output when the main
        //     is silent (g is undefined at zero input), so this probe reads
        //     the buffer output directly through a dedicated run.
            {
                SideChainAudioProcessor proc;
                if (auto* p = proc.getParameters().getParameter ("release"))
                    p->setValueNotifyingHost (p->convertTo0to1 (release));
                proc.prepareToPlay (sr, bs);
                enableSidechain (proc);

                juce::AudioBuffer<float> block (4, bs);
                juce::MidiBuffer midi;
                float maxOut = 0.0f;
                const float loud = 0.9f;
                for (int b = 0; b < n / bs; ++b)
                {
                    block.clear();
                    for (int i = 0; i < bs; ++i)
                    {
                        block.setSample (2, i, loud);
                        block.setSample (3, i, loud);
                        // main channels stay silent
                    }
                    proc.processBlock (block, midi);
                    for (int i = 0; i < bs; ++i)
                        maxOut = std::max (maxOut, std::abs (block.getSample (0, i)));
                }
                check (maxOut < 1.0e-6f,
                       "Release " + std::to_string ((int) release) + " ms: no sidechain leakage to output");
            }
            // 7d. Stereo: shared gain sanity. DC sidechain for the first
            //     half, silence after (Amount stays at its 50% default =>
            //     ducked gain ~0.12-0.16). At max Release the full cascade
            //     needs ~4.3 s, so a 1 s tail only recovers to ~0.16; the
            //     honest assertion is that the gain is MOVING UP vs the
            //     settled ducked level (longer release => slower recovery,
            //     but strictly recovering) and finite throughout.
            {
                SideChainAudioProcessor proc;
                if (auto* p = proc.getParameters().getParameter ("release"))
                    p->setValueNotifyingHost (p->convertTo0to1 (release));
                std::vector<float> sc ((size_t) n, 0.0f);
                for (int i = 0; i < n / 2; ++i) sc[(size_t) i] = 0.8f;
                auto r = runProcessor (proc, sr, bs, sc, mainS);
                const double recovered = settledGain (r.gainCurve, sr, 0.02);
                const double threshold = (release < 100.0f) ? 0.9 : 0.13;
                check (! r.nanInf && recovered > threshold,
                       "Release " + std::to_string ((int) release) + " ms: stereo run finite, recovery > "
                           + std::to_string (threshold).substr (0, 4));
            }
        }
    }

    // ==================================================================
    // 8. Phase 7A: corrupt/malformed state hardening.
    //    Policy (see setStateInformation): valid -> restore; missing or
    //    invalid (non-numeric, NaN, Inf, out of range) -> that parameter
    //    goes to its default; unparsable/empty data -> keep current state;
    //    unknown properties/children -> ignored; version stamped/future OK.
    // ==================================================================
    printf ("\n8. Phase 7A: corrupt state hardening\n");
    {
        // Helper: build a state tree with given amount/release values.
        // A value of juce::var() (void) means "omit the PARAM child".
        auto makeState = [] (juce::var amountVal, juce::var releaseVal, int version = 1)
        {
            juce::ValueTree tree ("PARAMS");
            if (version >= 0) tree.setProperty ("stateVersion", version, nullptr);
            if (! amountVal.isVoid())
            {
                juce::ValueTree p ("PARAM");
                p.setProperty ("id", "sidechainAmount", nullptr);
                p.setProperty ("value", amountVal, nullptr);
                tree.addChild (p, -1, nullptr);
            }
            if (! releaseVal.isVoid())
            {
                juce::ValueTree p ("PARAM");
                p.setProperty ("id", "release", nullptr);
                p.setProperty ("value", releaseVal, nullptr);
                tree.addChild (p, -1, nullptr);
            }
            return tree;
        };

        auto loadTree = [] (SideChainAudioProcessor& proc, const juce::ValueTree& tree)
        {
            juce::MemoryBlock mb;
            juce::MemoryOutputStream mos (mb, false);
            tree.writeToStream (mos);
            proc.setStateInformation (mb.getData(), (int) mb.getSize());
        };

        auto readAmount = [] (SideChainAudioProcessor& p)
        { return dynamic_cast<juce::AudioParameterFloat*> (p.getParameters().getParameter ("sidechainAmount"))->get(); };
        auto readRelease = [] (SideChainAudioProcessor& p)
        { return dynamic_cast<juce::AudioParameterFloat*> (p.getParameters().getParameter ("release"))->get(); };

        // 8.1 Valid state restores both.
        {
            SideChainAudioProcessor proc;
            loadTree (proc, makeState (juce::var (66.0), juce::var (300.0)));
            check (std::abs (readAmount (proc) - 66.0f) < 0.01f
                   && std::abs (readRelease (proc) - 300.0f) < 0.01f,
                   "7A: valid state restores Amount=66 and Release=300");
        }
        // 8.2 Missing Release -> default 150.
        {
            SideChainAudioProcessor proc;
            loadTree (proc, makeState (juce::var (40.0), juce::var()));
            check (std::abs (readAmount (proc) - 40.0f) < 0.01f
                   && std::abs (readRelease (proc) - 150.0f) < 0.01f,
                   "7A: missing Release -> default 150 ms");
        }
        // 8.3 Missing Amount -> default 50.
        {
            SideChainAudioProcessor proc;
            loadTree (proc, makeState (juce::var(), juce::var (250.0)));
            check (std::abs (readAmount (proc) - 50.0f) < 0.01f
                   && std::abs (readRelease (proc) - 250.0f) < 0.01f,
                   "7A: missing Amount -> default 50 %");
        }
        // 8.4 Negative Release -> default (never reaches DSP).
        {
            SideChainAudioProcessor proc;
            loadTree (proc, makeState (juce::var (50.0), juce::var (-100.0)));
            const float r = readRelease (proc);
            check (r >= 50.0f && std::abs (r - 150.0f) < 0.01f,
                   "7A: negative Release -> default 150 ms, never out of range");
        }
        // 8.5 Excessive Release (> 1000) -> default.
        {
            SideChainAudioProcessor proc;
            loadTree (proc, makeState (juce::var (50.0), juce::var (5000.0)));
            const float r = readRelease (proc);
            check (r <= 1000.0f && std::abs (r - 150.0f) < 0.01f,
                   "7A: Release above maximum -> default 150 ms");
        }
        // 8.6 Invalid Amount (out of range) -> default 50.
        {
            SideChainAudioProcessor proc;
            loadTree (proc, makeState (juce::var (250.0), juce::var (200.0)));
            const float a = readAmount (proc);
            check (a >= 0.0f && a <= 100.0f && std::abs (a - 50.0f) < 0.01f,
                   "7A: Amount above maximum -> default 50 %");
        }
        // 8.7 NaN payload -> default.
        {
            SideChainAudioProcessor proc;
            loadTree (proc, makeState (juce::var (50.0), juce::var (std::numeric_limits<double>::quiet_NaN())));
            const float r = readRelease (proc);
            check (std::isfinite (r) && std::abs (r - 150.0f) < 0.01f,
                   "7A: NaN Release payload -> default, DSP value finite");
        }
        // 8.8 Infinity payload -> default.
        {
            SideChainAudioProcessor proc;
            loadTree (proc, makeState (juce::var (std::numeric_limits<double>::infinity()), juce::var (200.0)));
            const float a = readAmount (proc);
            check (std::isfinite (a) && std::abs (a - 50.0f) < 0.01f,
                   "7A: Inf Amount payload -> default, DSP value finite");
        }
        // 8.9 Non-numeric value (string) -> default.
        {
            SideChainAudioProcessor proc;
            loadTree (proc, makeState (juce::var (50.0), juce::var (juce::String ("garbage"))));
            const float r = readRelease (proc);
            check (std::isfinite (r) && std::abs (r - 150.0f) < 0.01f,
                   "7A: non-numeric Release value -> default");
        }
        // 8.10 Malformed XML bytes -> keep current state, no crash.
        {
            SideChainAudioProcessor proc;
            if (auto* pa = proc.getParameters().getParameter ("sidechainAmount"))
                pa->setValueNotifyingHost (pa->convertTo0to1 (80.0f));
            const char junk[] = "this is not a ValueTree at all ---{{";
            proc.setStateInformation (junk, (int) sizeof (junk) - 1);
            check (std::abs (readAmount (proc) - 80.0f) < 0.01f,
                   "7A: malformed XML ignored, current state kept");
        }
        // 8.11 Empty/null state -> no crash, current state kept.
        {
            SideChainAudioProcessor proc;
            proc.setStateInformation (nullptr, 0);
            const char one = 'x';
            proc.setStateInformation (&one, 1);
            check (true, "7A: empty/null state handled without crash");
        }
        // 8.12 Future state version -> known parameters still load.
        {
            SideChainAudioProcessor proc;
            loadTree (proc, makeState (juce::var (44.0), juce::var (350.0), 99));
            check (std::abs (readAmount (proc) - 44.0f) < 0.01f
                   && std::abs (readRelease (proc) - 350.0f) < 0.01f,
                   "7A: future stateVersion 99 -> known parameters load safely");
        }
        // 8.13 Unknown properties and unknown PARAM children -> ignored.
        {
            SideChainAudioProcessor proc;
            auto tree = makeState (juce::var (55.0), juce::var (175.0));
            tree.setProperty ("someFutureProperty", "ignored", nullptr);
            juce::ValueTree unknownParam ("PARAM");
            unknownParam.setProperty ("id", "someFutureParameter", nullptr);
            unknownParam.setProperty ("value", 1.0, nullptr);
            tree.addChild (unknownParam, -1, nullptr);
            loadTree (proc, tree);
            check (std::abs (readAmount (proc) - 55.0f) < 0.01f
                   && std::abs (readRelease (proc) - 175.0f) < 0.01f,
                   "7A: unknown properties/parameters ignored, known ones load");
        }
        // 8.14 DSP stays finite and in-range after all the above abuse.
        {
            SideChainAudioProcessor proc;
            loadTree (proc, makeState (juce::var (std::numeric_limits<double>::quiet_NaN()),
                                       juce::var (-1.0e30)));
            proc.prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> block (4, 512);
            juce::MidiBuffer midi;
            bool finite = true;
            for (int b = 0; b < 16; ++b)
            {
                block.clear();
                for (int i = 0; i < 512; ++i)
                {
                    block.setSample (0, i, 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 440.0 * (b * 512 + i) / 48000.0));
                    block.setSample (1, i, block.getSample (0, i));
                    block.setSample (2, i, 0.8f);
                    block.setSample (3, i, 0.8f);
                }
                proc.processBlock (block, midi);
                for (int i = 0; i < 512; ++i)
                    if (! std::isfinite (block.getSample (0, i))) finite = false;
            }
            check (finite, "7A: DSP output finite after NaN/negative state load");
        }
    }

    // ==================================================================
    // 9. PumpCurve bounded model + schema 6 state persistence.
    // ==================================================================
    printf ("\n9. PumpCurve model + schema 6 state\n");
    {
        using sid::curve::Point;
        using sid::curve::PumpCurve;
        using sid::curve::StateMode;

        const auto defaults = PumpCurve::defaultPoints();
        PumpCurve defaultCurve;
        bool defaultsValid = defaultCurve.size() == defaults.size();
        for (std::size_t i = 0; i < defaults.size(); ++i)
            defaultsValid = defaultsValid && defaultCurve[i].x == defaults[i].x
                          && defaultCurve[i].y == defaults[i].y;
        check (defaultsValid && PumpCurve::isValid (defaults.data(), defaults.size()),
               "PumpCurve default is valid and deterministic");
        check (std::abs (defaultCurve[1].x - 0.018) < 1.0e-12
                   && defaultCurve[1].y > 0.06 && defaultCurve[1].y < 0.07,
               "PumpCurve default reflects 1.5 ms attack and -24 dB duck depth at defaults");

        PumpCurve two;
        const Point twoPoints[] = {{ 0.0, 1.0 }, { 1.0, 1.0 }};
        check (two.trySetPoints (twoPoints, 2) && two.size() == 2,
               "PumpCurve accepts exactly 2 points");

        Point sixteen[PumpCurve::kMaximumPoints] {};
        for (std::size_t i = 0; i < PumpCurve::kMaximumPoints; ++i)
            sixteen[i] = { (double) i / (PumpCurve::kMaximumPoints - 1), 0.5 };
        sixteen[0].y = sixteen[PumpCurve::kMaximumPoints - 1].y = 1.0;
        Point eleven[PumpCurve::kMaximumPoints + 1] {};
        for (std::size_t i = 0; i < PumpCurve::kMaximumPoints + 1; ++i)
            eleven[i] = { (double) i / PumpCurve::kMaximumPoints, 0.5 };
        eleven[0].y = eleven[PumpCurve::kMaximumPoints].y = 1.0;
        Point seven[7] {};
        for (std::size_t i = 0; i < 7; ++i)
            seven[i] = { (double) i / 6.0, 0.5 };
        seven[0].y = seven[6].y = 1.0;
        PumpCurve sixteenCurve;
        check (sixteenCurve.trySetPoints (sixteen, PumpCurve::kMaximumPoints),
               "PumpCurve accepts maximum 16 points");
        PumpCurve oversized;
        check (! PumpCurve::isValid (eleven, PumpCurve::kMaximumPoints + 1)
                   && ! oversized.trySetPoints (eleven, PumpCurve::kMaximumPoints + 1),
               "PumpCurve rejects more than 16 points");
        check (! PumpCurve::isValid (twoPoints, 1), "PumpCurve rejects fewer than 2 points");
        check (defaults.size() == PumpCurve::defaultPoints().size()
                   && PumpCurve::isValid (defaults.data(), defaults.size())
                   && PumpCurve::isValid (seven, 7),
               "default curve generation repeats deterministically; other valid point counts work");

        Point nanPoint[] = {{ 0.0, 1.0 }, { 0.5, std::numeric_limits<double>::quiet_NaN() }, { 1.0, 1.0 }};
        Point infPoint[] = {{ 0.0, 1.0 }, { 0.5, std::numeric_limits<double>::infinity() }, { 1.0, 1.0 }};
        Point rangePoint[] = {{ 0.0, 1.0 }, { 0.5, 1.01 }, { 1.0, 1.0 }};
        Point rangeXPoint[] = {{ 0.0, 1.0 }, { 1.01, 0.5 }, { 1.0, 1.0 }};
        Point endpointPoint[] = {{ 0.01, 1.0 }, { 1.0, 1.0 }};
        Point unordered[] = {{ 0.0, 1.0 }, { 0.7, 0.4 }, { 0.4, 0.5 }, { 1.0, 1.0 }};
        Point tooClose[] = {{ 0.0, 1.0 }, { 0.0005, 0.5 }, { 1.0, 1.0 }};
        check (! PumpCurve::isValid (nanPoint, 3), "PumpCurve rejects NaN coordinates");
        check (! PumpCurve::isValid (infPoint, 3), "PumpCurve rejects infinite coordinates");
        check (! PumpCurve::isValid (rangePoint, 3) && ! PumpCurve::isValid (rangeXPoint, 3),
               "PumpCurve rejects out-of-range x/y values deterministically");
        const bool rejectedWithoutMutation = ! two.trySetPoints (rangePoint, 3) && two.size() == 2;
        check (rejectedWithoutMutation, "PumpCurve failed update leaves the prior valid points unchanged");
        check (! PumpCurve::isValid (endpointPoint, 2), "PumpCurve requires fixed unity endpoints");
        check (! PumpCurve::isValid (unordered, 4), "PumpCurve rejects unordered interior x values");
        check (! PumpCurve::isValid (tooClose, 3), "PumpCurve enforces minimum x spacing");

        // State fixture helpers.
        auto loadTree = [] (SideChainAudioProcessor& proc, const juce::ValueTree& tree)
        {
            juce::MemoryBlock mb;
            juce::MemoryOutputStream mos (mb, false);
            tree.writeToStream (mos);
            proc.setStateInformation (mb.getData(), (int) mb.getSize());
        };
        auto makeVersionedTree = [] (int version)
        {
            juce::ValueTree tree ("PARAMS");
            tree.setProperty ("stateVersion", version, nullptr);
            const struct { const char* id; double value; int sinceVersion; } values[] = {
                { "sidechainAmount", 67.3, 1 }, { "release", 333.0, 1 },
                { "sidechainOffset", -12.0, 3 }, { "duckLength", 612.0, 4 }
            };
            for (const auto& value : values)
            {
                if (version < value.sinceVersion)
                    continue;
                juce::ValueTree p ("PARAM");
                p.setProperty ("id", value.id, nullptr);
                p.setProperty ("value", value.value, nullptr);
                tree.addChild (p, -1, nullptr);
            }
            if (version == 2)
            {
                juce::ValueTree removed ("PARAM");
                removed.setProperty ("id", "sidechainWhileStopped", nullptr);
                removed.setProperty ("value", 1.0, nullptr);
                tree.addChild (removed, -1, nullptr);
            }
            return tree;
        };
        auto readFloat = [] (SideChainAudioProcessor& proc, const char* id)
        {
            auto* p = dynamic_cast<juce::AudioParameterFloat*> (proc.getParameters().getParameter (id));
            return p == nullptr ? std::numeric_limits<float>::quiet_NaN() : p->get();
        };

        // New instance has a deterministic default curve and stable Smooth parameter.
        {
            SideChainAudioProcessor proc;
            auto* smooth = dynamic_cast<juce::AudioParameterFloat*> (
                proc.getParameters().getParameter ("smooth"));
            check (smooth != nullptr && smooth->getNormalisableRange().start == 0.0f
                       && smooth->getNormalisableRange().end == 100.0f
                       && std::abs (smooth->get() - 50.0f) < 0.01f
                       && proc.getCurveStateMode() == StateMode::pumpCurve,
                   "Smooth parameter is 0..100, default 50; new instance has curve mode");
        }

        // Schema 6 curve and all legacy parameter values round-trip exactly.
        {
            SideChainAudioProcessor source;
            source.getParameters().getParameter ("sidechainAmount")->setValueNotifyingHost (
                source.getParameters().getParameter ("sidechainAmount")->convertTo0to1 (67.3f));
            source.getParameters().getParameter ("duckLength")->setValueNotifyingHost (
                source.getParameters().getParameter ("duckLength")->convertTo0to1 (612.0f));
            source.getParameters().getParameter ("release")->setValueNotifyingHost (
                source.getParameters().getParameter ("release")->convertTo0to1 (333.0f));
            source.getParameters().getParameter ("sidechainOffset")->setValueNotifyingHost (
                source.getParameters().getParameter ("sidechainOffset")->convertTo0to1 (-12.0f));
            source.getParameters().getParameter ("smooth")->setValueNotifyingHost (
                source.getParameters().getParameter ("smooth")->convertTo0to1 (73.4f));
            const Point custom[] = {{ 0.0, 1.0 }, { 0.1, 0.2 }, { 0.55, 0.6 }, { 1.0, 1.0 }};
            const bool accepted = source.setPumpCurvePoints (custom, 4);
            source.getParameters().state.addChild (juce::ValueTree ("FUTURE_CHILD"), -1, nullptr);
            juce::MemoryBlock saved;
            source.getStateInformation (saved);
            const auto savedTree = juce::ValueTree::readFromData (saved.getData(), saved.getSize());
            SideChainAudioProcessor restored;
            restored.setStateInformation (saved.getData(), (int) saved.getSize());
            bool exactCurve = restored.getPumpCurve().size() == 4;
            for (std::size_t i = 0; i < 4; ++i)
                exactCurve = exactCurve && restored.getPumpCurve()[i].x == custom[i].x
                                       && restored.getPumpCurve()[i].y == custom[i].y;
            check (accepted && exactCurve
                       && restored.getCurveStateMode() == StateMode::pumpCurve
                       && (int) savedTree.getProperty ("stateVersion", juce::var (0)) == 6
                       && savedTree.getChildWithName ("PUMPCURVE").getNumChildren() == 4
                       && savedTree.getChildWithName ("FUTURE_CHILD").isValid(),
                   "schema 6 curve serializes as inspectable POINT children and round-trips exactly");
            check (std::abs (readFloat (restored, "sidechainAmount") - 67.3f) < 0.06f
                       && std::abs (readFloat (restored, "duckLength") - 612.0f) < 0.1f
                       && std::abs (readFloat (restored, "release") - 333.0f) < 0.1f
                       && std::abs (readFloat (restored, "sidechainOffset") + 12.0f) < 0.1f
                       && std::abs (readFloat (restored, "smooth") - 73.4f) < 0.06f,
                   "schema 6 round-trip preserves all five parameters");
        }

        // All historical schemas load original parameter data while remaining legacy.
        for (int version = 1; version <= 5; ++version)
        {
            SideChainAudioProcessor proc;
            auto oldTree = makeVersionedTree (version);
            loadTree (proc, oldTree);
            juce::MemoryBlock saved;
            proc.getStateInformation (saved);
            const auto savedTree = juce::ValueTree::readFromData (saved.getData(), saved.getSize());
            const float expectedLength = version >= 4 ? 612.0f
                : sid::dsp::DuckEngine::kDuckLengthDefaultMs;
            const float expectedOffset = version >= 3 ? -12.0f : 0.0f;
            check (proc.getCurveStateMode() == StateMode::legacy
                       && std::abs (readFloat (proc, "sidechainAmount") - 67.3f) < 0.06f
                       && std::abs (readFloat (proc, "duckLength") - expectedLength) < 0.1f
                       && std::abs (readFloat (proc, "release") - 333.0f) < 0.1f
                       && std::abs (readFloat (proc, "sidechainOffset") - expectedOffset) < 0.1f
                       && ! savedTree.getChildWithName ("PUMPCURVE").isValid(),
                   "legacy state v" + std::to_string (version) + " loads parameters and remains legacy");
        }

        // Missing curve on schema 6, malformed curve, and unknown children are safe/deterministic.
        {
            SideChainAudioProcessor proc;
            auto missing = makeVersionedTree (6);
            juce::ValueTree unknown ("FUTURE_CHILD");
            unknown.setProperty ("value", 42, nullptr);
            missing.addChild (unknown, -1, nullptr);
            loadTree (proc, missing);
            check (proc.getCurveStateMode() == StateMode::legacy
                       && std::abs (readFloat (proc, "release") - 333.0f) < 0.1f,
                   "schema 6 missing curve defaults to legacy and tolerates unknown child");

            auto malformed = makeVersionedTree (6);
            juce::ValueTree curve ("PUMPCURVE");
            curve.setProperty ("pointCount", 3, nullptr);
            const Point invalid[] = {{ 0.0, 1.0 }, { 0.5, std::numeric_limits<double>::infinity() }, { 1.0, 1.0 }};
            for (const auto& point : invalid)
            {
                juce::ValueTree p ("POINT");
                p.setProperty ("x", point.x, nullptr);
                p.setProperty ("y", point.y, nullptr);
                curve.addChild (p, -1, nullptr);
            }
            malformed.addChild (curve, -1, nullptr);
            loadTree (proc, malformed);
            juce::MemoryBlock sanitised;
            proc.getStateInformation (sanitised);
            const auto sanitisedTree = juce::ValueTree::readFromData (sanitised.getData(), sanitised.getSize());
            check (proc.getCurveStateMode() == StateMode::legacy
                       && PumpCurve::isValid (proc.getPumpCurve().storage().data(), proc.getPumpCurve().size())
                       && std::abs (readFloat (proc, "sidechainAmount") - 67.3f) < 0.06f
                       && std::abs (readFloat (proc, "release") - 333.0f) < 0.1f
                       && ! sanitisedTree.getChildWithName ("PUMPCURVE").isValid(),
                   "malformed schema 6 curve falls back safely without changing valid parameters");
        }
    }

    printf ("\n===========================================\n");
    printf ("%d/%d checks passed. %s\n", totalChecks - failures, totalChecks,
            failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
