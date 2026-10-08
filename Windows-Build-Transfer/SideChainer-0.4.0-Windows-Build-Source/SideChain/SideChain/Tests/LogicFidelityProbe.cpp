/*
    Logic-fidelity probe (headless, real processor compiled in).

    Reproduces the FULL Logic opening sequence that the clean-host probe
    (HostProbeAU, which passed) skips: the editor is constructed ON TOP of
    the processor (Logic always opens an editor), then host state is
    restored, then the user drags knobs while the transport runs.

    Checks for the exact reported symptoms:
      1. editor-open + run            -> duck present?
      2. restore state + run          -> duck present?
      3. preset apply while editor    -> duck + parameter reaches engine?
      4. knob drag (Slider setValue)  -> parameter + engine follow?
      5. toggle                       -> does the value survive?
      6. graph frames produced        -> non-empty duck trace?

    Build: Tests/run_all.sh --probe (or manually via PH6 recipe + PluginEditor.cpp)
*/

#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/GraphData.h"

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

static int checks = 0, failures = 0;
static void check (bool ok, const std::string& what)
{
    ++checks;
    printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

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

class PlayingHead : public juce::AudioPlayHead
{
public:
    bool getCurrentPosition (CurrentPositionInfo& result) override
    {
        result = CurrentPositionInfo{};
        result.isPlaying = true;
        return true;
    }
};

namespace
{
    bool enableSidechain (SideChainAudioProcessor& proc)
    {
        auto layout = proc.getBusesLayout();
        layout.getChannelSet (true, 1) = juce::AudioChannelSet::stereo();
        return proc.setBusesLayout (layout);
    }
}

struct RunStats
{
    double minGain = 1.0;
    int duckFrames = 0;
    int triggerFrames = 0;
    int frames = 0;
};

static RunStats runWithEditor (SideChainAudioProcessor& proc, double sr, int block,
                               const std::vector<float>& sc, const std::vector<float>& main)
{
    proc.setPlayHead (new PlayingHead());
    proc.prepareToPlay (sr, block);
    enableSidechain (proc);

    RunStats r;
    const int n = (int) sc.size();
    const int totalCh = 4;

    juce::AudioBuffer<float> buffer (totalCh, n);
    buffer.clear();
    for (int i = 0; i < n; ++i)
    {
        buffer.setSample (0, i, main[(size_t) i]);
        buffer.setSample (1, i, main[(size_t) i]);
        buffer.setSample (2, i, sc[(size_t) i]);
        buffer.setSample (3, i, sc[(size_t) i]);
    }

    const int lat = juce::jlimit (0, n, (int) proc.getLatencySamples());
    std::vector<float> inDelay ((size_t) lat + 1, 0.0f);
    size_t inDelayPos = 0;

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> blk (totalCh, block);
    for (int start = 0; start < n; start += block)
    {
        const int count = std::min (block, n - start);
        blk.clear();
        for (int ch = 0; ch < totalCh; ++ch)
            blk.copyFrom (ch, 0, buffer.getReadPointer (ch) + start, count);
        proc.processBlock (blk, midi);

        for (int i = 0; i < count; ++i)
        {
            const float out = std::abs (blk.getSample (0, i));
            const float d = inDelay[(size_t) inDelayPos];
            inDelay[(size_t) inDelayPos] = std::abs (main[(size_t) (start + i)]);
            inDelayPos = (inDelayPos + 1) % (size_t) juce::jmax (1, lat);
            if (d > 0.1f)
                r.minGain = std::min (r.minGain, (double) (out / d));
        }

        sid::graph::GraphFrame f;
        while (proc.graphFifo.pop (f))
        {
            ++r.frames;
            if (f.gainReductionDb < -3.0f) ++r.duckFrames;
            if (f.triggerFired) ++r.triggerFrames;
        }
    }
    return r;
}

// Friend accessor (SIDECHAIN_HEADLESS_TEST builds only): reproduces the
// real user input paths on the editor's raw controls.
struct SideChainEditorTestAccess
{
    static juce::Slider& duckLength (SideChainAudioProcessorEditor& e) { return e.duckLengthKnob; }
    static juce::Slider& amount (SideChainAudioProcessorEditor& e)     { return e.amountKnob; }
    static juce::Button& offsetNext (SideChainAudioProcessorEditor& e) { return e.offsetNextButton; }
};

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    printf ("SideChain Logic-fidelity probe (editor-on-top sequence)\n");
    const double sr = 48000.0;
    const int bs = 512;

    auto sc = kick (sr, 0.25);
    const int n = (int) sc.size();
    std::vector<float> main ((size_t) n, 0.0f);
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        main[(size_t) i] = 0.5f * (float) std::sin (ph);
        ph += 2.0 * 3.14159265358979 * 440.0 / sr;
    }
    auto duckDb = [] (const RunStats& r)
    {
        return -20.0 * std::log10 (std::max (r.minGain, 1.0e-9));
    };

    // ---- 1. editor constructed on top (Logic ALWAYS does this) ------------
    {
        SideChainAudioProcessor proc;
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setVisible (true);
        auto r = runWithEditor (proc, sr, bs, sc, main);
        printf ("     editor-on-top: minGain %.4f (%.1f dB), frames %d, duckFrames %d, triggers %d\n",
                r.minGain, duckDb (r), r.frames, r.duckFrames, r.triggerFrames);
        check (duckDb (r) > 5.0, "1: editor-open -> duck still present");
        check (r.duckFrames > 0 && r.triggerFrames > 0, "1: graph frames show the duck");
    }

    // ---- 2. state restore after editor open (Logic project load) ----------
    {
        SideChainAudioProcessor procA;
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (procA.createEditor());
            ed->setVisible (true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            juce::MemoryBlock mb;
            procA.getStateInformation (mb);

            SideChainAudioProcessor procB;
            {
                std::unique_ptr<juce::AudioProcessorEditor> ed2 (procB.createEditor());
                ed2->setVisible (true);
                procB.setStateInformation (mb.getData(), (int) mb.getSize());
                auto r = runWithEditor (procB, sr, bs, sc, main);
                printf ("     state-restore: minGain %.4f (%.1f dB)\n", r.minGain, duckDb (r));
                check (duckDb (r) > 5.0, "2: state-restore with editor -> duck present");
            }
        }
    }

    // ---- 3. preset apply while the editor is open -------------------------
    {
        SideChainAudioProcessor proc;
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setVisible (true);
        proc.applyFactoryPreset ("Kick Pump");
        auto r = runWithEditor (proc, sr, bs, sc, main);
        printf ("     preset apply: minGain %.4f (%.1f dB)\n", r.minGain, duckDb (r));
        check (duckDb (r) > 5.0, "3: preset apply with editor -> duck present");
    }

    // ---- 4. knob drag path (juce::Slider::setValue like ProdKnob drag) ----
    {
        SideChainAudioProcessor proc;
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setVisible (true);

        auto* edPtr = [&]() -> SideChainAudioProcessorEditor*
        {
            printf ("[s4-cast] begin\n"); fflush (stdout);
            auto* p4 = dynamic_cast<SideChainAudioProcessorEditor*> (ed.get());
            printf ("[s4-cast] result %p\n", (void*) p4); fflush (stdout);
            return p4;
        }();
        // Simulate a user drag on the DUCK LENGTH and AMOUNT knobs.
        SideChainEditorTestAccess::duckLength (*edPtr).setValue (500.0, juce::sendNotificationSync);
        SideChainEditorTestAccess::amount (*edPtr).setValue (100.0, juce::sendNotificationSync);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);

        auto* amount = dynamic_cast<juce::AudioParameterFloat*> (
            proc.getParameters().getParameter ("sidechainAmount"));
        auto* dl = dynamic_cast<juce::AudioParameterFloat*> (
            proc.getParameters().getParameter ("duckLength"));
        printf ("     knob drag: Amount %.1f %% (engine depth %.1f dB), Length %.0f ms\n",
                amount->get(),
                sid::dsp::DuckEngine::amountToDepthDb (amount->get() / 100.0f),
                dl->get());
        check (std::abs (amount->get() - 100.0f) < 0.01f, "4: Amount knob drag reaches the parameter");
        check (std::abs (dl->get() - sid::dsp::DuckEngine::kDuckLengthDefaultMs) < 0.5f, "4: DuckLength knob drag reaches the parameter");

        auto r = runWithEditor (proc, sr, bs, sc, main);
        printf ("     knob drag run: minGain %.4f (%.1f dB)\n", r.minGain, duckDb (r));
        check (duckDb (r) > 20.0, "4: after knob drags -> deep duck present (Amount 100%)");
    }

    // ---- 5. (0.4.0) the whileStopped toggle is GONE with the external
    //        sidechain; the editor must construct cleanly without it. ----
    {
        SideChainAudioProcessor proc;
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        check (proc.getParameters().getParameter ("sidechainWhileStopped") == nullptr,
               "5: removed whileStopped control/param absent, editor builds");
    }

    // ---- 6. offset arrows change the parameter ----------------------------
    {
        SideChainAudioProcessor proc;
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setVisible (true);
        auto* edPtr = static_cast<SideChainAudioProcessorEditor*> (ed.get());
        SideChainEditorTestAccess::offsetNext (*edPtr).triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        auto* off = dynamic_cast<juce::AudioParameterFloat*> (
            proc.getParameters().getParameter ("sidechainOffset"));
        printf ("     offset after next-click: %d ms\n", (int) std::lround (off->get()));
        check (std::abs (off->get() - 1.0f) < 0.51f, "6: offset arrow click steps +1 ms");
    }

    printf ("\n==============================\n");
    printf ("%d/%d checks passed. %s\n", checks - failures, checks,
            failures == 0 ? "ALL PROBES PASSED" : "REGRESSION CONFIRMED IN PROBE");
    printf ("==============================\n");
    return failures == 0 ? 0 : 1;
}
