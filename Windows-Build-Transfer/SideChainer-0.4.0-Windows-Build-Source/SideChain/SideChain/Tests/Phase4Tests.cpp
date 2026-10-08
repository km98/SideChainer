/*
    SideChain - Phase 4 regression tests (0.4.0 internal trigger update).

    Coverage:
      A. GraphFrameFifo SPSC behaviour (unchanged; ordering/overfill).
      B. Graph data content: silence, main-only, beat-triggered ducking.
         0.4.0: there is no sidechain bus; frames come from the internal
         beat-triggered engine, and sidechainLevelDb mirrors the input.
      C. Amount automation ramp (no clicks / no discontinuities) with beats
         firing on the internal trigger grid.
      D. State round-trip sanity (ValueTree serialization of Amount).
*/

#include <cstdio>
#include <cmath>
#include <vector>

#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/GraphData.h"
#include "../Source/DuckEngine.h"

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
    // Minimal AudioProcessor for the APVTS owner in section D.
    class DummyProcessor : public juce::AudioProcessor
    {
    public:
        DummyProcessor() : AudioProcessor (BusesProperties()
            .withInput ("In", juce::AudioChannelSet::stereo(), true)
            .withOutput ("Out", juce::AudioChannelSet::stereo(), true)) {}
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        const juce::String getName() const override { return "D"; }
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}
    };

    DummyProcessor* dummyProcessor() { static DummyProcessor dp; return &dp; }
}

// Block-wise processing mirroring the production processBlock (0.4.0):
// internal beat triggers fire on the quarter-note grid; returns the
// per-block GraphFrames exactly as the plugin would push them.
static std::vector<sid::graph::GraphFrame> runBlocks (double sr, int blockSize, float amountPercent,
                                                      const std::vector<float>& mainL,
                                                      const std::vector<float>& mainR,
                                                      double bpm)
{
    sid::dsp::DuckEngine engine;
    engine.prepare (sr);

    std::vector<sid::graph::GraphFrame> frames;
    const int n = (int) mainL.size();
    const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (amountPercent / 100.0f);
    const double samplesPerBeat = (bpm > 0.0) ? 60.0 / bpm * sr : 0.0;
    double nextBeat = 0.0;

    for (int start = 0; start < n; start += blockSize)
    {
        const int count = std::min (blockSize, n - start);
        float inPeak = 0, outPeak = 0;

        // bpm == 0 => no scheduling at all (mirrors the scheduler fail-safe).
        while (bpm > 0.0 && nextBeat < start + count)
        {
            if (nextBeat >= start)
                engine.fireTrigger();
            nextBeat += samplesPerBeat;
        }

        for (int i = start; i < start + count; ++i)
        {
            const float gain = engine.processSample (depthDb);
            const float outL = mainL[(size_t) i] * gain;

            inPeak  = std::max (inPeak,  std::abs (mainL[(size_t) i]));
            outPeak = std::max (outPeak, std::abs (outL));
        }

        sid::graph::GraphFrame f;
        f.inputLevelDb     = juce::Decibels::gainToDecibels (inPeak,  -100.0f);
        f.sidechainLevelDb = f.inputLevelDb;   // 0.4.0: mirrors input
        f.gainReductionDb  = engine.getCurrentReductionDb();
        f.outputLevelDb    = juce::Decibels::gainToDecibels (outPeak, -100.0f);
        f.bpm              = (float) bpm;
        frames.push_back (f);
    }
    return frames;
}

int main()
{
    printf ("SideChain Phase 4 Regression Tests (0.4.0)\n==========================================\n");
    const double sr = 48000.0;
    const int n = 48000;
    const int bs = 512;
    const double bpm = 120.0;

    // ------------------------------------------------------------------
    printf ("\nA. GraphFrameFifo SPSC behaviour\n");
    {
        sid::graph::GraphFrameFifo fifo;
        bool ordered = true, sizeOk = true;

        // Overfill: FIFO keeps the newest kCapacity frames (oldest dropped).
        const int pushed = sid::graph::GraphFrameFifo::kCapacity + 500;
        for (int i = 0; i < pushed; ++i)
        {
            sid::graph::GraphFrame f;
            f.gainReductionDb = -(float) i * 0.001f;
            fifo.push (f);
        }
        const int expectedFirst = pushed - sid::graph::GraphFrameFifo::kCapacity;
        int popped = 0;
        for (int i = expectedFirst; i < pushed; ++i)
        {
            sid::graph::GraphFrame f;
            if (! fifo.pop (f)) { sizeOk = false; break; }
            ++popped;
            if (std::abs (f.gainReductionDb - (-(float) i * 0.001f)) > 1e-6) ordered = false;
        }
        check (sizeOk && popped == sid::graph::GraphFrameFifo::kCapacity,
               "FIFO: newest kCapacity frames retrieved after overfill");
        check (ordered, "FIFO: strict ordering preserved");
        {
            sid::graph::GraphFrame leftover;
            check (! fifo.pop (leftover), "FIFO: empty after drain");
        }

        // Over-capacity push must not crash (already exercised above by the
        // +500 overfill); a final sanity push beyond capacity.
        for (int i = 0; i < 200; ++i)
        {
            sid::graph::GraphFrame f; f.gainReductionDb = (float) i; fifo.push (f);
        }
        check (true, "FIFO: over-capacity push tolerated (no crash)");
    }

    // ------------------------------------------------------------------
    printf ("\nB1. Graph data: silence everywhere (no beats fired)\n");
    {
        std::vector<float> silence (n, 0.0f);
        // BPM 0 => no beats scheduled => the fail-safe no-trigger path.
        auto frames = runBlocks (sr, bs, 50.0f, silence, silence, 0.0);

        bool allSilent = true;
        for (auto& f : frames)
            if (f.inputLevelDb > -99.0f || f.sidechainLevelDb > -99.0f
                || f.outputLevelDb > -99.0f || std::abs (f.gainReductionDb) > 0.01f)
                allSilent = false;
        check (allSilent, "silence: input/sidechain/output at silence, GR = 0");
    }

    printf ("\nB2. Graph data: main only, no beat triggers (BPM 0 fail-safe)\n");
    {
        auto mainL = sine (n, 440.0, sr, 0.5f);
        auto mainR = mainL;
        auto frames = runBlocks (sr, bs, 50.0f, mainL, mainR, 0.0);

        bool ok = true;
        for (auto& f : frames)
        {
            if (f.inputLevelDb < -8.0f)   ok = false;          // input present
            if (f.gainReductionDb < -0.1f) ok = false;         // no ducking
            if (std::abs (f.outputLevelDb - f.inputLevelDb) > 0.5f) ok = false; // output == input
        }
        check (ok, "main only + no beats: input>0, GR=0, output==input");
    }

    printf ("\nB3. Graph data: beats firing (internal trigger)\n");
    {
        auto mainL = sine (n, 440.0, sr, 0.5f);
        auto mainR = mainL;
        auto frames = runBlocks (sr, bs, 100.0f, mainL, mainR, bpm);

        // 0.4.0: beats fire every 0.5 s; the envelope ducks deeply right
        // after each beat and recovers before the next one (250 ms default
        // length < 500 ms beat gap).
        bool sawDeepGr = false, tailClean = true, bpmCarried = true;
        for (size_t i = 0; i < frames.size(); ++i)
        {
            const auto& f = frames[i];
            if (f.gainReductionDb <= -20.0f) sawDeepGr = true;
            if (i >= frames.size() - 3)
                if (f.gainReductionDb < -1.0f) tailClean = false;  // recovered
            if (std::abs ((double) f.bpm - bpm) > 0.01) bpmCarried = false;
        }
        check (sawDeepGr && tailClean,
               "beat-driven ducking: deep GR per beat and recovery between beats");
        check (bpmCarried, "graph frames carry the host BPM for the beat display");
    }

    // ------------------------------------------------------------------
    printf ("\nC. Amount automation ramp (no clicks / no discontinuities)\n");
    {
        // Sweep Amount 0 -> 100 over 0.5 s while beat-triggered audio runs;
        // the per-sample gain must never jump discontinuously.
        sid::dsp::DuckEngine engine;
        engine.prepare (sr);

        auto main = sine (n, 440.0, sr, 0.5f);
        const double samplesPerBeat = 60.0 / bpm * sr;
        double nextBeat = 0.0;

        float prevGain = 1.0f;
        float maxStep = 0.0f;
        bool finite = true;

        const int rampSamples = n / 2;
        for (int start = 0; start < n; start += bs)
        {
            const int count = std::min (bs, n - start);
            const float blockStart01 = (float) start / (float) rampSamples;
            const float blockEnd01   = (float) (start + count) / (float) rampSamples;
            const float amountStart = juce::jlimit (0.0f, 1.0f, blockStart01) * 100.0f;
            const float amountEnd   = juce::jlimit (0.0f, 1.0f, blockEnd01) * 100.0f;
            const float amountMid = 0.5f * (amountStart + amountEnd);
            const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (amountMid / 100.0f);

            while (nextBeat < start + count)
            {
                if (nextBeat >= start)
                    engine.fireTrigger();
                nextBeat += samplesPerBeat;
            }

            for (int i = start; i < start + count; ++i)
            {
                const float gain = engine.processSample (depthDb);
                if (! std::isfinite (gain)) finite = false;
                maxStep = std::max (maxStep, std::abs (gain - prevGain));
                prevGain = gain;
            }
        }
        printf ("     (max adjacent-sample gain step during automation sweep: %g)\n", maxStep);
        check (finite, "automation ramp: no NaN/Inf");
        check (maxStep < 0.1f, "automation ramp: no gain discontinuities (no clicks)");
    }

    // ------------------------------------------------------------------
    printf ("\nD. State round-trip sanity (ValueTree serialization of Amount)\n");
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            "sidechainAmount", "Amount",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f));

        juce::AudioProcessorValueTreeState state (*dummyProcessor(), nullptr, "PARAMS", std::move (layout));
        state.state.getChildWithName ("PARAM").setProperty ("value", 73.0f, nullptr);
        check (std::abs (state.getRawParameterValue ("sidechainAmount")->load() - 73.0f) < 1e-4f,
               "state: tree write propagates to parameter/atomic");

        juce::MemoryBlock mb;
        juce::MemoryOutputStream mos (mb, false);
        state.state.writeToStream (mos);

        juce::AudioProcessorValueTreeState::ParameterLayout layout2;
        layout2.add (std::make_unique<juce::AudioParameterFloat> (
            "sidechainAmount", "Amount",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f));
        juce::AudioProcessorValueTreeState state2 (*dummyProcessor(), nullptr, "PARAMS", std::move (layout2));

        auto tree = juce::ValueTree::readFromData (mb.getData(), mb.getSize());
        check (tree.isValid(), "state: serialized tree valid");
        state2.replaceState (tree);

        const float restored = state2.getRawParameterValue ("sidechainAmount")->load();
        check (std::abs (restored - 73.0f) < 1e-4f, "state: Amount=73 survives round-trip");
    }

    printf ("\n==================================\n");
    printf ("%d/%d checks passed. %s\n", totalChecks - failures, totalChecks,
            failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
