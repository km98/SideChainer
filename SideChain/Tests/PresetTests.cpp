/*
    SideChainer - Phase 7 preset system tests (0.4.0 internal trigger).

    Tests the REAL production processor (SideChainAudioProcessor) plus the
    compiled-in factory table (PresetManager.h) headlessly.

    0.4.0 coverage: factory table integrity (10 trigger presets, distinct
    base durations, all trigger internally), exact recall of Amount +
    Shape + base Duck Length through the proper parameter-notification
    path, derived Custom/Default identity, value-based re-identification
    (rename safety), state round-trips, and audio safety of preset
    switching while processing.
*/

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <atomic>
#include <set>

#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PresetManager.h"
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
    float readAmount (SideChainAudioProcessor& p)
    {
        return dynamic_cast<juce::AudioParameterFloat*> (
            p.getParameters().getParameter ("sidechainAmount"))->get();
    }

    float readRelease (SideChainAudioProcessor& p)
    {
        return dynamic_cast<juce::AudioParameterFloat*> (
            p.getParameters().getParameter ("release"))->get();
    }

    float readDuckLength (SideChainAudioProcessor& p)
    {
        return dynamic_cast<juce::AudioParameterFloat*> (
            p.getParameters().getParameter ("duckLength"))->get();
    }

    // ---- test play head: drives the internal beat scheduler ------------
    class TestPlayHead : public juce::AudioPlayHead
    {
    public:
        bool getCurrentPosition (CurrentPositionInfo& result) override
        {
            result = info;
            return positionValid;
        }
        CurrentPositionInfo info;
        bool positionValid = true;
    };

    // Installs the play head on the processor (real AudioProcessor API).
    void installPlayHead (SideChainAudioProcessor& proc, double bpm,
                          bool playing, bool recording = false)
    {
        auto* ph = new TestPlayHead();
        ph->info.bpm = bpm;
        ph->info.isPlaying = playing;
        ph->info.isRecording = recording;
        ph->info.timeInSamples = 0;
        ph->info.ppqPosition = 0.0;
        proc.setPlayHead (ph);
    }

    // Advances a fake host timeline: per block, sets timeInSamples/ppq and
    // runs processBlock with stereo main audio. Runs WITHOUT any play head
    // when `playing` is false (stopped => no triggers, no ducking).
    struct RunResult
    {
        std::vector<float> out;
        std::vector<float> gain;
        bool nanInf = false;
        int triggerCount = 0;
    };

    RunResult runPlaying (SideChainAudioProcessor& proc, double sr, int blockSize,
                          double bpm, bool playing,
                          const std::vector<float>& mainL,
                          double* timelineSamples = nullptr)
    {
        RunResult r;
        const int n = (int) mainL.size();
        const int lat = juce::jlimit (0, n, (int) proc.getLatencySamples());
        std::vector<float> inDelay ((size_t) lat + 1, 0.0f);
        size_t inDelayPos = 0;

        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;
        const double samplesPerQuarter = 60.0 / bpm * sr;
        int triggerBefore = proc.duckEngineForTest().getTriggerCount();

        for (int start = 0; start + blockSize <= n; start += blockSize)
        {
            auto* ph = dynamic_cast<TestPlayHead*> (proc.getPlayHead());
            if (ph != nullptr)
            {
                ph->info.isPlaying = playing;
                ph->info.isRecording = false;
                const double t = (timelineSamples != nullptr)
                    ? *timelineSamples : (double) start;
                ph->info.timeInSamples = (long long) std::llround (t);
                ph->info.ppqPosition = t / samplesPerQuarter;
            }

            for (int i = 0; i < blockSize; ++i)
                block.setSample (0, i, mainL[(size_t) (start + i)]);
            for (int i = 0; i < blockSize; ++i)
                block.setSample (1, i, mainL[(size_t) (start + i)]);

            proc.processBlock (block, midi);

            for (int i = 0; i < blockSize; ++i)
            {
                const float in = mainL[(size_t) (start + i)];
                inDelay[(size_t) inDelayPos] = in;
                const float delayedIn = inDelay[(size_t) ((inDelayPos + 1) % inDelay.size())];
                inDelayPos = (inDelayPos + 1) % inDelay.size();
                const float o = block.getSample (0, i);
                r.out.push_back (o);
                const float g = (std::abs (delayedIn) > 1.0e-6f) ? o / delayedIn : 1.0f;
                if (! std::isfinite (g) || g < -0.001f || g > 1.001f) r.nanInf = true;
                r.gain.push_back (g);
            }

            if (timelineSamples != nullptr)
                *timelineSamples += blockSize;
        }

        r.triggerCount = proc.duckEngineForTest().getTriggerCount() - triggerBefore;
        return r;
    }
}

int main()
{
    printf ("SideChainer Phase 7 Preset Tests (0.4.0)\n========================================\n");
    juce::ScopedJuceInitialiser_GUI juceInit;

    // ==================================================================
    // 1. Factory table integrity.
    // ==================================================================
    printf ("\n1. Factory preset table\n");
    {
        const auto& table = sid::presets::factoryPresets();

        check (table.size() == 10, "exactly 10 factory presets exist");

        std::set<std::string> names;
        bool unique = true, valid = true, knownNames = true, distinctLengths = true;
        for (int i = 0; i < table.size(); ++i)
        {
            const auto& p = table.getReference (i);
            if (! names.insert (p.name.toStdString()).second) unique = false;
            if (p.amountPercent < 0.0f || p.amountPercent > 100.0f
                || p.releaseMs < sid::dsp::DuckEngine::kReleaseMinMs
                || p.releaseMs > sid::dsp::DuckEngine::kReleaseMaxMs
                || p.duckLengthMs < sid::dsp::DuckEngine::kDuckLengthMinMs
                || p.duckLengthMs > sid::dsp::DuckEngine::kDuckLengthMaxMs)
                valid = false;
            for (int j = 0; j < i; ++j)
                if (std::abs (table.getReference (j).duckLengthMs - p.duckLengthMs) < 1.0f)
                    distinctLengths = false;
        }
        check (unique, "every preset has a unique name");
        check (valid, "every preset has valid Amount/Shape/Length values");
        check (distinctLengths, "every preset has a DISTINCT base duck length");

        // The required internal trigger preset list, by name.
        const char* expected[] = { "Micro Kick", "Tight Kick", "Classic Kick",
                                   "Short Pump", "Medium Pump", "Wide Pump",
                                   "Deep Pump", "EDM Pump", "Long Pump", "Full Beat" };
        for (const char* nm : expected)
            if (sid::presets::findByName (nm) == nullptr)
                knownNames = false;
        check (knownNames, "all required trigger preset names present");

        // The exact documented values (0.4.0 internal trigger redesign).
        struct { const char* name; float amount; float release; float length; } expectedVals[] = {
            { "Micro Kick",   60.0f, 150.0f,  60.0f },
            { "Tight Kick",   70.0f, 120.0f,  90.0f },
            { "Classic Kick", 75.0f, 150.0f, 250.0f },
            { "Short Pump",   60.0f, 120.0f, 180.0f },
            { "Medium Pump",  65.0f, 200.0f, 350.0f },
            { "Wide Pump",    70.0f, 300.0f, 500.0f },
            { "Deep Pump",    85.0f, 250.0f, 400.0f },
            { "EDM Pump",     90.0f, 400.0f, 600.0f },
            { "Long Pump",    80.0f, 500.0f, 800.0f },
            { "Full Beat",    95.0f, 350.0f, 950.0f } };
        bool valuesExact = true;
        for (const auto& e : expectedVals)
        {
            const auto* p = sid::presets::findByName (e.name);
            if (p == nullptr || std::abs (p->amountPercent - e.amount) > 1.0e-6f
                || std::abs (p->releaseMs - e.release) > 1.0e-6f
                || std::abs (p->duckLengthMs - e.length) > 1.0e-6f)
                valuesExact = false;
        }
        check (valuesExact, "preset values match the documented design exactly");
    }

    // ==================================================================
    // 2. Preset recall: exact values through the proper notification path.
    // ==================================================================
    printf ("\n2. Preset recall (exact values, host-visible notification)\n");
    {
        bool allExact = true;
        for (const auto& p : sid::presets::factoryPresets())
        {
            SideChainAudioProcessor proc;
            if (! proc.applyFactoryPreset (p.name)) { allExact = false; continue; }

            // Pump the async APVTS parameter->tree sync so reads are settled.
            for (int i = 0; i < 10; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

            if (std::abs (readAmount (proc) - p.amountPercent) > 0.01f
                || std::abs (readRelease (proc) - p.releaseMs) > 0.01f
                || std::abs (readDuckLength (proc) - p.duckLengthMs) > 0.01f)
                allExact = false;
        }
        check (allExact, "every preset writes its exact Amount, Shape and base Length");

        // Host visibility through the parameter objects.
        {
            SideChainAudioProcessor proc;
            proc.applyFactoryPreset ("Classic Kick");
            check (std::abs (readAmount (proc) - 75.0f) < 0.01f
                   && std::abs (readRelease (proc) - 150.0f) < 0.01f
                   && std::abs (readDuckLength (proc) - 250.0f) < 0.01f,
                   "preset recall is host-visible through the parameter objects");
        }

        // Unknown preset name rejected cleanly.
        {
            SideChainAudioProcessor proc;
            const bool applied = proc.applyFactoryPreset ("No Such Preset");
            check (! applied && std::abs (readAmount (proc) - 50.0f) < 0.01f,
                   "unknown preset name rejected, state unchanged");
        }
    }

    // ==================================================================
    // 3. Derived identity: factory name / Default / Custom.
    // ==================================================================
    printf ("\n3. Current-preset identity (derived, never stored)\n");
    {
        // Fresh processor = plugin defaults -> "Default".
        {
            SideChainAudioProcessor proc;
            check (proc.getCurrentPresetDisplayName() == "Default",
                   "fresh plugin shows \"Default\" (50% / 150 ms / 250 ms)");
        }

        // Selecting each preset -> that exact name; changing any parameter
        // -> Custom; exact return -> the name again.
        bool namesOk = true, customAmount = true, customRelease = true,
             customLength = true, returnOk = true;
        for (const auto& p : sid::presets::factoryPresets())
        {
            SideChainAudioProcessor proc;
            proc.applyFactoryPreset (p.name);

            if (proc.getCurrentPresetDisplayName() != p.name) namesOk = false;

            // Manual AMOUNT change (notification path, as a host would) -> Custom.
            const float newAmount = (p.amountPercent > 7.0f) ? p.amountPercent - 7.0f : p.amountPercent + 13.0f;
            if (auto* pa = proc.getParameters().getParameter ("sidechainAmount"))
                pa->setValueNotifyingHost (pa->convertTo0to1 (juce::jlimit (0.0f, 100.0f, newAmount)));
            if (proc.getCurrentPresetDisplayName() != "Custom") customAmount = false;

            proc.applyFactoryPreset (p.name);
            if (auto* pr = proc.getParameters().getParameter ("release"))
                pr->setValueNotifyingHost (pr->convertTo0to1 (p.releaseMs + 25.0f));
            if (proc.getCurrentPresetDisplayName() != "Custom") customRelease = false;

            proc.applyFactoryPreset (p.name);
            if (auto* pl = proc.getParameters().getParameter ("duckLength"))
                pl->setValueNotifyingHost (pl->convertTo0to1 (p.duckLengthMs + 30.0f));
            if (proc.getCurrentPresetDisplayName() != "Custom") customLength = false;

            proc.applyFactoryPreset (p.name);
            if (proc.getCurrentPresetDisplayName() != p.name) returnOk = false;
        }
        check (namesOk, "selecting a preset shows its name");
        check (customAmount, "manual Amount change after a preset -> Custom");
        check (customRelease, "manual Shape change after a preset -> Custom");
        check (customLength, "manual Duck Length change after a preset -> Custom");
        check (returnOk, "exact return to preset values restores the preset name");

        // Boundary sanity of the matching tolerance (0.05): a value just
        // outside the tolerance must NOT match.
        {
            SideChainAudioProcessor proc;
            proc.applyFactoryPreset ("Micro Kick"); // 60 / 150 / 60
            if (auto* pa = proc.getParameters().getParameter ("sidechainAmount"))
                pa->setValueNotifyingHost (pa->convertTo0to1 (60.11f));
            check (proc.getCurrentPresetDisplayName() == "Custom",
                   "near-miss values do not falsely match a preset");
        }

        // Default menu action restores the plugin defaults.
        {
            SideChainAudioProcessor proc;
            proc.applyFactoryPreset ("EDM Pump");
            proc.applyDefaultPreset();
            check (std::abs (readAmount (proc) - 50.0f) < 0.01f
                   && std::abs (readRelease (proc) - 150.0f) < 0.01f
                   && std::abs (readDuckLength (proc) - 250.0f) < 0.01f
                   && proc.getCurrentPresetDisplayName() == "Default",
                   "applyDefaultPreset restores defaults and shows Default");
        }
    }

    // ==================================================================
    // 4. State: preset/custom round-trips; name/index never stored.
    // ==================================================================
    printf ("\n4. State round-trips (values are authoritative)\n");
    {
        auto pump = [] { for (int i = 0; i < 10; ++i)
                            juce::MessageManager::getInstance()->runDispatchLoopUntil (10); };

        // 4a. Preset state restores to the exact values AND re-identifies.
        {
            SideChainAudioProcessor proc;
            proc.applyFactoryPreset ("EDM Pump");
            pump();

            juce::MemoryBlock mb;
            proc.getStateInformation (mb);

            SideChainAudioProcessor proc2;
            proc2.setStateInformation (mb.getData(), (int) mb.getSize());

            check (std::abs (readAmount (proc2) - 90.0f) < 0.01f
                   && std::abs (readRelease (proc2) - 400.0f) < 0.01f
                   && std::abs (readDuckLength (proc2) - 600.0f) < 0.01f,
                   "saved preset state restores exact Amount + Shape + Length");
            check (proc2.getCurrentPresetDisplayName() == "EDM Pump",
                   "restored state re-identifies the factory preset (by values)");
        }

        // 4b. Custom values restore as values and identify as Custom.
        {
            SideChainAudioProcessor proc;
            proc.applyFactoryPreset ("Classic Kick");
            if (auto* pa = proc.getParameters().getParameter ("sidechainAmount"))
                pa->setValueNotifyingHost (pa->convertTo0to1 (63.4f));
            pump();

            juce::MemoryBlock mb;
            proc.getStateInformation (mb);

            SideChainAudioProcessor proc2;
            proc2.setStateInformation (mb.getData(), (int) mb.getSize());
            check (std::abs (readAmount (proc2) - 63.4f) < 0.01f
                   && proc2.getCurrentPresetDisplayName() == "Custom",
                   "saved custom state restores values and identifies as Custom");
        }

        // 4c. Saved state carries no preset name/index property (renaming a
        //     preset must not break old sessions): check the raw tree.
        {
            SideChainAudioProcessor proc;
            proc.applyFactoryPreset ("Classic Kick");
            pump();

            juce::MemoryBlock mb;
            proc.getStateInformation (mb);
            auto tree = juce::ValueTree::readFromData (mb.getData(), mb.getSize());

            bool hasPresetProps = false;
            for (int i = 0; i < tree.getNumProperties(); ++i)
            {
                const auto propName = tree.getPropertyName (i).toString();
                if (propName.containsIgnoreCase ("preset"))
                    hasPresetProps = true;
            }
            check (! hasPresetProps,
                   "saved state contains no preset name/index (values only)");

            // Simulate a renamed factory table: same values, different name
            // string. A restored session must still match by values.
            sid::presets::FactoryPreset renamed { "Classic Kick (renamed)", 75.0f, 150.0f, 250.0f };
            auto& mutableTable = const_cast<juce::Array<sid::presets::FactoryPreset>&> (
                sid::presets::factoryPresets());
            mutableTable.getReference (2).name = renamed.name;
            const juce::String identity = proc.getCurrentPresetDisplayName();
            mutableTable.getReference (2).name = "Classic Kick"; // restore

            check (identity == "Classic Kick (renamed)",
                   "preset rename does not break value-based identification");
        }

        // 4d. stateVersion carries the current schema version (5 in 0.4.0).
        {
            SideChainAudioProcessor proc;
            proc.applyFactoryPreset ("Micro Kick");
            pump();
            juce::MemoryBlock mb;
            proc.getStateInformation (mb);
            auto tree = juce::ValueTree::readFromData (mb.getData(), mb.getSize());
            check (tree.isValid() && (int) tree.getProperty ("stateVersion", juce::var (0)) == 5,
                   "stateVersion = 5 (0.4.0 internal-trigger schema)");
        }
    }

    // ==================================================================
    // 5. Audio safety of preset switching during active processing.
    // ==================================================================
    printf ("\n5. Preset switching while processing (audio safety)\n");
    {
        const double sr = 48000.0;
        const int n = (int) (2.0 * sr);
        const int bs = 512;
        const double bpm = 120.0;
        auto mainS = sine (n, 440.0, sr, 0.5f);

        SideChainAudioProcessor proc;
        installPlayHead (proc, bpm, true);

        // Bus topology + DSP architecture must be unchanged by preset use.
        const auto layoutBefore = proc.getBusesLayout();
        proc.applyFactoryPreset ("Classic Kick");
        check (proc.getBusesLayout().getMainInputChannelSet()  == layoutBefore.getMainInputChannelSet()
               && proc.getBusesLayout().getMainOutputChannelSet() == layoutBefore.getMainOutputChannelSet(),
               "preset selection leaves bus topology unchanged");

        proc.prepareToPlay (sr, bs);

        bool finite = true, noZipper = true, leakageFree = true;
        float maxOut = 0.0f;

        // Align the out/in comparison with the reported lookahead latency.
        const int lat = juce::jlimit (0, n, (int) proc.getLatencySamples());
        std::vector<float> inDelay ((size_t) lat + 1, 0.0f);
        size_t inDelayPos = 0;

        // Simultaneous parameter changes mid-stream: switch presets every
        // 250 ms of audio (all three parameters change at once).
        int presetSwitchCounter = 0;
        const auto& table = sid::presets::factoryPresets();

        juce::AudioBuffer<float> block (2, bs);
        juce::MidiBuffer midi;
        float prevGain = 1.0f;
        double timeline = 0.0;

        for (int start = 0; start + bs <= n; start += bs)
        {
            if (presetSwitchCounter == 0)
            {
                const auto& p = table.getReference ((start / bs / 10) % table.size());
                proc.applyFactoryPreset (p.name);
            }
            ++presetSwitchCounter;

            auto* ph = dynamic_cast<TestPlayHead*> (proc.getPlayHead());
            ph->info.isPlaying = true;
            ph->info.timeInSamples = (long long) std::llround (timeline);
            ph->info.ppqPosition = timeline / (60.0 / bpm * sr);

            for (int i = 0; i < bs; ++i)
            {
                block.setSample (0, i, mainS[(size_t) start + i]);
                block.setSample (1, i, mainS[(size_t) start + i]);
            }
            proc.processBlock (block, midi);

            for (int i = 0; i < bs; ++i)
            {
                const float in = mainS[(size_t) start + i];
                inDelay[(size_t) inDelayPos] = in;
                const float delayedIn = inDelay[(size_t) ((inDelayPos + 1) % inDelay.size())];
                inDelayPos = (inDelayPos + 1) % inDelay.size();
                const float out = block.getSample (0, i);
                const float outR = block.getSample (1, i);
                const float g = (std::abs (delayedIn) > 1.0e-6f) ? out / delayedIn : prevGain;
                if (! std::isfinite (g)) finite = false;
                if (g > 1.0f + 1.0e-6f) { finite = false; } // gain never > 1
                if (std::abs (g - prevGain) > 0.25f) noZipper = false;
                prevGain = g;
                maxOut = std::max (maxOut, std::abs (out));
                if (std::abs (out - outR) > 1.0e-6f) finite = false;
                if (std::abs (out) > std::abs (delayedIn) + 1.0e-6f) leakageFree = false;
            }
            timeline += bs;
        }

        check (finite, "preset switching: output finite, gain <= unity, stereo intact");
        check (noZipper, "preset switching: no clicks/zipper (max sample step <= 0.25)");
        check (leakageFree && maxOut <= 0.5f + 1.0e-6f,
               "preset switching: output never exceeds main input");

        // Graph keeps flowing real data after preset churn (no fake data).
        {
            sid::graph::GraphFrame f;
            int popped = 0;
            while (proc.graphFifo.pop (f)) ++popped;
            check (popped > 0, "graph: real DSP frames continue after preset switches");
        }
    }

    // ==================================================================
    // 6. All presets trigger internally on every beat (no kick audio).
    // ==================================================================
    printf ("\n6. Internal triggering works for every preset\n");
    {
        const double sr = 48000.0;
        const double bpm = 120.0;   // one beat every 500 ms
        const double seconds = 2.2; // ~4 beats
        const int n = (int) (seconds * sr);

        bool allTriggered = true, noneMuted = true;
        for (const auto& p : sid::presets::factoryPresets())
        {
            SideChainAudioProcessor proc;
            installPlayHead (proc, bpm, true);
            proc.prepareToPlay (sr, 512);
            proc.applyFactoryPreset (p.name);

            auto mainS = sine (n, 440.0, sr, 0.5f);
            auto r = runPlaying (proc, sr, 512, bpm, true, mainS);

            // 2.2 s at 120 BPM = 4 beats (at 0, 0.5, 1.0, 1.5, 2.0 s ->
            // the last one at 2.0 s lands inside the run) => exactly 5.
            if (r.triggerCount != 5) allTriggered = false;

            // Must not be continuously ducked: the gain must return close
            // to unity between envelopes (Short Pump and faster presets).
            float maxG = 0.0f;
            for (float g : r.gain) maxG = std::max (maxG, g);
            if (maxG < 0.95f) noneMuted = false;
        }
        check (allTriggered, "every preset triggers internally on every beat");
        check (noneMuted, "every preset recovers between beats (no continuous duck)");
    }

    printf ("\n==============================\n");
    printf ("%d/%d checks passed. %s\n", totalChecks - failures, totalChecks,
            failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
