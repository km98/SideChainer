/*
    SideChainer - DUCK LENGTH + view-mode tests (0.4.0 internal trigger).

    DUCK LENGTH (total audible duck duration) remains the user's
    independent duration control; presets now restore their factory base
    length on selection (template semantics). Envelope runs are driven by
    fireTrigger() on the beat grid - exactly what the processor's beat
    scheduler does in production.
*/

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/GraphComponent.h"

static int totalChecks = 0, failures = 0;
static void check (bool ok, const std::string& what)
{
    ++totalChecks;
    printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

struct Run
{
    std::vector<float> gain;
    int triggers = 0;
};

static Run runBeats (double sr, int beatCount, double bpm,
                     float amountPercent, float releaseMs, float duckLengthMs)
{
    sid::dsp::DuckEngine e;
    e.prepare (sr);
    e.setReleaseTimes (150.0f, releaseMs);
    e.setDuckLengthMs (duckLengthMs);

    Run r;
    const double samplesPerBeat = 60.0 / bpm * sr;
    const int n = (int) ((double) beatCount * samplesPerBeat + 2.5 * sr); // room to recover
    r.gain.reserve ((size_t) n);
    const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (amountPercent / 100.0f);

    double nextBeat = 0.0;
    int fired = 0;
    for (int i = 0; i < n; ++i)
    {
        // Fire exactly `beatCount` triggers on the beat grid, then let the
        // last envelope recover without further beats.
        if (fired < beatCount && (double) i >= nextBeat)
        {
            e.fireTrigger();
            ++fired;
            nextBeat += samplesPerBeat;
        }
        r.gain.push_back (e.processSample (depthDb));
    }
    r.triggers = e.getTriggerCount();
    return r;
}

// Audible duck length (ms) of the FIRST envelope: from the first ducked
// sample after the trigger to the last sample below 95% of unity WITHIN
// the same cycle (a sustained gap above 90 ms of recovered gain ends the
// envelope - later beats start new cycles).
static double audibleDuckLengthMs (const std::vector<float>& gain, double sr)
{
    const auto firstDuck = std::find_if (gain.begin(), gain.end(),
                                         [] (float g) { return g < 0.995f; });
    if (firstDuck == gain.end()) return -1.0;
    const int start = (int) (firstDuck - gain.begin());
    int lastDucked = start;
    int recoveredRun = 0;
    const int maxRecoveredRun = (int) (0.010 * sr); // 10 ms above 0.95 ends it
    for (size_t i = (size_t) start; i < gain.size(); ++i)
    {
        if (gain[i] < 0.95f)
        {
            lastDucked = (int) i;
            recoveredRun = 0;
        }
        else if (++recoveredRun > maxRecoveredRun)
            break;
    }
    return (double) (lastDucked - start) / sr * 1000.0;
}

static double minGain (const std::vector<float>& g)
{
    float m = 1.0f;
    for (float x : g) m = std::min (m, x);
    return m;
}

// Real-processor harness (headless): a scripted play head at `bpm`
// PLAYING from sample 0 - the same sequence as in real Logic.
struct ProcRun
{
    std::vector<sid::graph::GraphFrame> frames;
    int triggers = 0;
};

class HarnessPlayHead : public juce::AudioPlayHead
{
public:
    bool getCurrentPosition (CurrentPositionInfo& result) override
    {
        result = CurrentPositionInfo{};
        result.isPlaying = true;
        result.bpm = 120.0;
        result.timeInSamples = (long long) blockCounter_ * 512;
        result.ppqPosition = (double) (blockCounter_ * 512) / 24000.0;
        ++blockCounter_;
        return true;
    }
    int blockCounter_ = 0;
};

static ProcRun runProcessor (SideChainAudioProcessor& proc, double sr,
                             const std::vector<float>& main, int block = 512)
{
    proc.setPlayHead (new HarnessPlayHead());
    proc.prepareToPlay (sr, block);

    ProcRun r;
    const int n = (int) main.size();
    juce::AudioBuffer<float> buffer (2, n);
    buffer.clear();
    for (int i = 0; i < n; ++i)
    {
        buffer.setSample (0, i, main[(size_t) i]);
        buffer.setSample (1, i, main[(size_t) i]);
    }

    for (int start = 0; start < n; start += block)
    {
        const int count = std::min (block, n - start);
        juce::AudioBuffer<float> blk (2, count);
        for (int ch = 0; ch < 2; ++ch)
            blk.copyFrom (ch, 0, buffer.getReadPointer (ch) + start, count);
        juce::MidiBuffer midi;
        proc.processBlock (blk, midi);

        sid::graph::GraphFrame f;
        while (proc.graphFifo.pop (f))
            r.frames.push_back (f);
    }
    r.triggers = proc.duckEngineForTest().getTriggerCount();
    return r;
}

int main()
{
    printf ("SideChainer DUCK LENGTH + View-Mode Tests (0.4.0)\n=================================================\n");
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double sr = 48000.0;

    // ==================================================================
    printf ("\n1. ENGINE: DUCK LENGTH basics\n");
    {
        // 1.1 clamping + defaults
        {
            sid::dsp::DuckEngine e; e.prepare (sr);
            check (e.getDuckLengthMs() == sid::dsp::DuckEngine::kDuckLengthDefaultMs,
                   "default duck length is kDuckLengthDefaultMs (250 ms)");
            e.setDuckLengthMs (1.0f);
            check (e.getDuckLengthMs() == sid::dsp::DuckEngine::kDuckLengthMinMs,
                   "duck length clamps to minimum (50 ms)");
            e.setDuckLengthMs (99999.0f);
            check (e.getDuckLengthMs() == sid::dsp::DuckEngine::kDuckLengthMaxMs,
                   "duck length clamps to maximum (1000 ms)");
        }

        // 1.2 short vs long: same beats, clearly different audible duration
        {
            auto s = runBeats (sr, 1, 120.0, 75.0f, 150.0f, 60.0f);
            auto l = runBeats (sr, 1, 120.0, 75.0f, 150.0f, 900.0f);
            const double sMs = audibleDuckLengthMs (s.gain, sr);
            const double lMs = audibleDuckLengthMs (l.gain, sr);
            printf ("     audible duck: short %.1f ms, long %.1f ms\n", sMs, lMs);
            check (s.triggers == 1, "one trigger fired per beat");
            check (sMs > 0 && sMs < 120.0, "short length (60 ms) -> audible duck < 120 ms");
            check (lMs > 600.0, "long length (900 ms) -> audible duck > 600 ms");
            check (lMs > sMs * 3.0, "long duck is clearly (>3x) longer than short");
        }

        // 1.3 monotonic duration scaling across the range
        {
            const float lens[] = { 60.0f, 150.0f, 300.0f, 600.0f, 1000.0f };
            double prev = -1.0;
            bool increasing = true;
            std::string detail;
            for (float len : lens)
            {
                auto r = runBeats (sr, 1, 120.0, 75.0f, 150.0f, len);
                const double ms = audibleDuckLengthMs (r.gain, sr);
                detail += " " + std::to_string ((int) len) + "->" + std::to_string ((int) ms);
                if (ms <= prev) increasing = false;
                prev = ms;
            }
            printf ("     scaling:%s\n", detail.c_str());
            check (increasing, "audible duck duration grows monotonically with DUCK LENGTH");
        }

        // 1.4 depth is NOT changed by duck length (Amount owns depth)
        {
            auto a = runBeats (sr, 1, 120.0, 75.0f, 150.0f, 60.0f);
            auto b = runBeats (sr, 1, 120.0, 75.0f, 150.0f, 1000.0f);
            const double da = -20.0 * std::log10 (std::max ((double) minGain (a.gain), 1e-9));
            const double db = -20.0 * std::log10 (std::max ((double) minGain (b.gain), 1e-9));
            check (std::abs (da - db) < 1.0,
                   "duck length does not change duck depth (75% stays ~-14.9 dB)");
        }

        // 1.5 Release is SHAPE, not total duration
        {
            auto r1 = runBeats (sr, 1, 120.0, 75.0f, 50.0f, 300.0f);
            auto r2 = runBeats (sr, 1, 120.0, 75.0f, 1000.0f, 300.0f);
            const double m1 = audibleDuckLengthMs (r1.gain, sr);
            const double m2 = audibleDuckLengthMs (r2.gain, sr);
            check (std::abs (m1 - m2) < 0.45 * std::max (m1, m2),
                   "Release (shape) moves the envelope far less than Duck Length does");
            check (m1 > 150.0 && m1 < 500.0, "Release 50 / Length 300 stays ~300 ms total");
            check (m2 > 150.0 && m2 < 500.0, "Release 1000 / Length 300 stays ~300 ms total");
        }

        // 1.6 default timing (250 ms total, hold ~9.7% = 24 ms, tau ~75 ms)
        {
            sid::dsp::DuckEngine e; e.prepare (sr);
            e.setReleaseTimes (150.0f, 150.0f);
            e.setDuckLengthMs (250.0f);
            const float hold = e.getDerivedHoldMs();
            const float tau  = e.getDerivedReleaseTauMs();
            printf ("     derived hold %.1f ms, tau %.1f ms\n", hold, tau);
            check (std::abs (hold - 24.3f) < 2.0f,
                   "default hold ~= 0.097 x 250 ms");
            check (std::abs ((hold + 3.0 * tau) - 250.0f) < 2.0f,
                   "hold + 3 tau ~= the full 250 ms duck length");
        }
    }

    // ==================================================================
    printf ("\n2. PROCESSOR: parameter, presets, state\n");
    {
        // 2.1 parameter exists with the right range/default
        {
            SideChainAudioProcessor proc;
            auto* p = dynamic_cast<juce::AudioParameterFloat*> (
                proc.getParameters().getParameter ("duckLength"));
            check (p != nullptr, "duckLength parameter exists");
            if (p != nullptr)
            {
                check (std::abs (p->get() - sid::dsp::DuckEngine::kDuckLengthDefaultMs) < 0.01f,
                       "duckLength default is 250 ms");
                check (p->getNormalisableRange().start == 50.0f
                       && p->getNormalisableRange().end == 1000.0f,
                       "duckLength range 50..1000 ms");
            }
        }

        // 2.2 preset apply RESTORES its factory base length (template
        //     semantics), and the user's length then rules until the next
        //     preset selection.
        {
            SideChainAudioProcessor proc;
            auto* dl = dynamic_cast<juce::AudioParameterFloat*> (
                proc.getParameters().getParameter ("duckLength"));

            proc.applyFactoryPreset ("Micro Kick");
            check (std::abs (dl->get() - 60.0f) < 0.01f,
                   "applying Micro Kick restores its 60 ms base length");

            // User sets an explicit length; changing presets restores each
            // preset's own base (conventional template behaviour).
            dl->setValueNotifyingHost (dl->convertTo0to1 (120.0f));
            proc.applyFactoryPreset ("EDM Pump");
            check (std::abs (dl->get() - 600.0f) < 0.05f,
                   "EDM Pump selection restores its 600 ms base length");
            dl->setValueNotifyingHost (dl->convertTo0to1 (120.0f));
            proc.applyDefaultPreset();
            check (std::abs (dl->get() - 250.0f) < 0.05f,
                   "Default preset restores the 250 ms default length");
        }

        // 2.3 engine follows the parameter (automation path)
        {
            SideChainAudioProcessor proc;
            proc.prepareToPlay (sr, 512);
            auto* dl = dynamic_cast<juce::AudioParameterFloat*> (
                proc.getParameters().getParameter ("duckLength"));
            dl->setValueNotifyingHost (dl->convertTo0to1 (800.0f));
            check (std::abs (proc.duckEngineForTest().getDuckLengthMs() - 800.0f) < 0.5f,
                   "parameterChanged -> engine duck length sync (automation path)");
        }

        // 2.4 state v5 save/load round-trip
        {
            SideChainAudioProcessor proc;
            proc.prepareToPlay (sr, 512);
            auto* dl = dynamic_cast<juce::AudioParameterFloat*> (
                proc.getParameters().getParameter ("duckLength"));
            dl->setValueNotifyingHost (dl->convertTo0to1 (333.0f));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

            juce::MemoryBlock mb;
            proc.getStateInformation (mb);

            SideChainAudioProcessor proc2;
            proc2.setStateInformation (mb.getData(), (int) mb.getSize());
            auto* dl2 = dynamic_cast<juce::AudioParameterFloat*> (
                proc2.getParameters().getParameter ("duckLength"));
            check (std::abs (dl2->get() - 333.0f) < 0.05f,
                   "state round-trip restores duckLength (333 ms)");
            check (std::abs (proc2.duckEngineForTest().getDuckLengthMs() - 333.0f) < 0.5f,
                   "restored state re-syncs the engine's duck length");
        }

        // 2.5 legacy state (v3, no duckLength) loads with default length
        {
            juce::ValueTree tree ("PARAMS");
            tree.setProperty ("stateVersion", 3, nullptr);
            auto mkParam = [&] (const char* id, double v)
            {
                juce::ValueTree p ("PARAM");
                p.setProperty ("id", juce::String (id), nullptr);
                p.setProperty ("value", v, nullptr);
                tree.appendChild (p, nullptr);
            };
            mkParam ("sidechainAmount", 75.0);
            mkParam ("release", 120.0);
            mkParam ("sidechainOffset", 5.0);
            mkParam ("sidechainWhileStopped", 1.0); // removed in 0.4.0: ignored

            juce::MemoryOutputStream mos;
            tree.writeToStream (mos);

            SideChainAudioProcessor proc;
            proc.setStateInformation (mos.getData(), (int) mos.getDataSize());
            auto* dl = dynamic_cast<juce::AudioParameterFloat*> (
                proc.getParameters().getParameter ("duckLength"));
            check (dl != nullptr
                   && std::abs (dl->get() - sid::dsp::DuckEngine::kDuckLengthDefaultMs) < 0.05f,
                   "legacy v3 state loads with duckLength at default");
        }

        // 2.6 out-of-range duckLength in state -> sanitised to default
        {
            juce::ValueTree tree ("PARAMS");
            tree.setProperty ("stateVersion", 4, nullptr);
            auto mkParam = [&] (const char* id, double v)
            {
                juce::ValueTree p ("PARAM");
                p.setProperty ("id", juce::String (id), nullptr);
                p.setProperty ("value", v, nullptr);
                tree.appendChild (p, nullptr);
            };
            mkParam ("sidechainAmount", 50.0);
            mkParam ("release", 150.0);
            mkParam ("duckLength", 4000.0); // outside 50..1000

            juce::MemoryOutputStream mos;
            tree.writeToStream (mos);

            SideChainAudioProcessor proc;
            proc.setStateInformation (mos.getData(), (int) mos.getDataSize());
            auto* dl = dynamic_cast<juce::AudioParameterFloat*> (
                proc.getParameters().getParameter ("duckLength"));
            check (dl != nullptr
                   && std::abs (dl->get() - sid::dsp::DuckEngine::kDuckLengthDefaultMs) < 0.05f,
                   "out-of-range duckLength (4000 ms) sanitised to default");
        }

        // 2.7 gesture-free restore ("undo") path
        {
            SideChainAudioProcessor proc;
            proc.prepareToPlay (sr, 512);
            auto* dl = dynamic_cast<juce::AudioParameterFloat*> (
                proc.getParameters().getParameter ("duckLength"));
            dl->setValueNotifyingHost (dl->convertTo0to1 (700.0f));
            check (std::abs (proc.duckEngineForTest().getDuckLengthMs() - 700.0f) < 0.5f,
                   "length change reaches the engine");
            dl->setValueNotifyingHost (dl->convertTo0to1 (150.0f)); // "undo"
            check (std::abs (proc.duckEngineForTest().getDuckLengthMs() - 150.0f) < 0.5f,
                   "gesture-free restore (undo path) returns the engine to 150 ms");
        }
    }

    // ==================================================================
    printf ("\n3. GRAPH: view modes + preview + live data\n");
    {
        // 3.1 default view mode
        {
            GraphComponent g;
            check (g.getViewMode() == GraphComponent::ViewMode::sidechain,
                   "graph default view is SIDECHAIN");
        }

        // 3.2 view switch is display-only: history/FIFO/DSP untouched
        {
            SideChainAudioProcessor proc;
            GraphComponent g;

            std::vector<float> main ((size_t) (2.2 * sr), 0.5f);
            auto run = runProcessor (proc, sr, main);
            check (run.frames.size() > 10, "processor produced graph frames");
            check (run.triggers >= 4, "beat triggers fired through the processor");

            for (auto& f : run.frames) g.pushFrameForTest (f);
            const int w1 = g.writePosForTest();
            const float r0 = g.reductionAtForTest (0);

            g.setViewMode (GraphComponent::ViewMode::analyzer);
            g.setViewMode (GraphComponent::ViewMode::sidechain);
            check (g.writePosForTest() == w1,
                   "view switching does not advance/corrupt graph history");
            check (std::abs (g.reductionAtForTest (0) - r0) < 1.0e-6f,
                   "view switching preserves reduction history bit-exactly");
            check (std::abs (proc.duckEngineForTest().getDuckLengthMs()
                                 - sid::dsp::DuckEngine::kDuckLengthDefaultMs) < 0.5f,
                   "view switching does not alter DSP state");
        }

        // 3.3 live duck envelope + beat markers appear in real frames
        {
            SideChainAudioProcessor proc;
            GraphComponent g;

            std::vector<float> main ((size_t) (2.2 * sr), 0.5f);
            auto run = runProcessor (proc, sr, main);

            bool sawDuck = false, sawTrigger = false, sawBpm = false;
            for (const auto& f : run.frames)
            {
                if (f.gainReductionDb < -5.0f) sawDuck = true;
                if (f.triggerFired) sawTrigger = true;
                if (f.bpm > 100.0f) sawBpm = true;
            }
            g.setViewMode (GraphComponent::ViewMode::sidechain);
            for (auto& f : run.frames) g.pushFrameForTest (f);
            {
                juce::Image img (juce::Image::ARGB, 100, 60, true);
                juce::Graphics gfx (img);
                g.paint (gfx);
            }

            check (sawDuck, "live frames carry real gain reduction (duck envelope)");
            check (sawTrigger, "live frames carry beat triggerFired markers");
            check (sawBpm, "live frames carry the host BPM (beat-aware display)");
            check (! g.historyIsSilentForTest(),
                   "history with live data is NOT silent (live beats preview)");
        }

        // 3.4 stopped/silence: no fake live data, preview visible
        {
            SideChainAudioProcessor proc;   // never run
            GraphComponent g;
            check (g.historyIsSilentForTest(), "fresh graph history is silent");
            g.setViewMode (GraphComponent::ViewMode::sidechain);
            {
                juce::Image img (juce::Image::ARGB, 100, 60, true);
                juce::Graphics gfx (img);
                g.paint (gfx);
            }
            check (g.historyIsSilentForTest(), "painting preview does not fabricate history");
        }

        // 3.5 preview responds to Amount, Duck Length and Release
        {
            check (GraphComponent::settledDepthDb (100.0f)
                   > GraphComponent::settledDepthDb (50.0f),
                   "preview depth grows with Amount");

            GraphComponent g;
            g.setPreviewParams (50.0f, 150.0f, 120.0f);
            check (g.historyIsSilentForTest(), "setPreviewParams never fabricates history");
            g.setPreviewParams (50.0f, 150.0f, 900.0f);
            g.setPreviewParams (80.0f, 400.0f, 300.0f);
            check (g.historyIsSilentForTest(), "preview param changes keep history clean");
        }
    }

    printf ("\n==============================\n");
    if (failures == 0)
        printf ("%d/%d checks passed. ALL TESTS PASSED\n", totalChecks, totalChecks);
    else
        printf ("%d/%d checks passed. %d FAILURES\n", totalChecks - failures, totalChecks, failures);
    printf ("==============================\n");
    return failures == 0 ? 0 : 1;
}
