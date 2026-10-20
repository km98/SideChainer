/*
    PumpCurve 0.4.2 - TRANSPORT SWITCH + LIVE WAVEFORM regression suite.

    Suite 1 - TRANSPORT SWITCH ("ACTIVE ONLY DURING PLAY/REC"):
      the user-facing gate restored in 0.4.2, evaluated on the REAL processor
      with its own play head installed through AudioProcessor::setPlayHead (no
      host, no faked transport booleans - the same AudioPlayHead API the hosts
      use). Cases: default ON, legacy state migration (no gate child -> ON),
      save/restore of both states, gating while stopped / playing / recording,
      the free-running path with the gate OFF, and the guarantee that the
      default ON keeps the established DSP behaviour byte for byte.

    Suite 2 - LIVE WAVEFORM SCOPE:
      the audio->GUI scope transport (sid::scope) drawn behind the curve.
      Cases: the output trace really is the PROCESSED signal (per-frame
      envelope ratio), silence, invalid samples, block sizes from 1 to 4096,
      ring overrun order, the stalled-producer fallback, and an allocation
      census proving processBlock allocates nothing (audio-thread safety).

    Suite 3 - NON-REGRESSION: presets, reset, curve edit and legacy schema
      migration still behave, with the gate present in the state.
*/

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <new>
#include <limits>
#include <utility>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>

#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/GraphComponent.h"
#include "../Source/TransportGate.h"
#include "../Source/BeatScheduler.h"
#include "../Source/WaveformData.h"

// ---------------------------------------------------------------------------
// Allocation census: the audio thread must not allocate. Every global
// allocation is counted while the counter is armed, so the claim in the source
// comment ("no allocations on the audio thread") is measured, not asserted.
// ---------------------------------------------------------------------------
namespace
{
    bool countAllocations = false;
    long allocationCount = 0;
}

void* operator new (std::size_t size)
{
    if (countAllocations) ++allocationCount;
    if (size == 0) size = 1;
    void* p = std::malloc (size);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}

void* operator new[] (std::size_t size) { return operator new (size); }

void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    if (countAllocations) ++allocationCount;
    return std::malloc (size == 0 ? 1 : size);
}
void operator delete (void* p, const std::nothrow_t&) noexcept { std::free (p); }

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
    for (int i = 0; i < n; ++i)
    {
        v[(size_t) i] = amp * (float) std::sin (ph);
        ph += 2.0 * 3.141592653589793 * freq / sr;
    }
    return v;
}

namespace
{
    // ---- test play head (installed through the REAL AudioProcessor API) ----
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

    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlockSize  = 512;

    // One full cycle of the scope history, used to bound the tests.
    struct ScopeSummary
    {
        int frames = 0;
        float inputPeak = 0.0f;
        float outputPeak = 0.0f;
        float minGainRatio = 1.0f;   // deepest output/input envelope ratio
        bool allFinite = true;
        bool allWithinFullScale = true;
        bool outputNeverExceedsInput = true;
    };

    // Runs the real processor and drains BOTH streams, exactly as the editor
    // does (scope FIFO drained once per "GUI tick", i.e. per block here so a
    // test can inspect every frame).
    ScopeSummary runAndDrain (SideChainAudioProcessor& proc,
                              const std::vector<float>& audio,
                              int blockSize,
                              bool playing,
                              bool recording = false)
    {
        ScopeSummary s;
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        const int n = (int) audio.size();
        auto* ph = dynamic_cast<TestPlayHead*> (proc.getPlayHead());

        sid::scope::WaveformFrame frame;

        // prepareToPlay publishes a run of silence frames so the GUI starts on
        // a clean baseline; drop them so the counts below describe the audio.
        while (proc.waveformFifo.pop (frame)) {}

        for (int start = 0; start + blockSize <= n; start += blockSize)
        {
            if (ph != nullptr)
            {
                ph->positionValid = true;
                ph->info.isPlaying = playing;
                ph->info.isRecording = recording;
                ph->info.bpm = 120.0;
                ph->info.timeInSamples = start;
                ph->info.ppqPosition = (double) start / 24000.0;
            }

            for (int ch = 0; ch < 2; ++ch)
            {
                auto* out = buffer.getWritePointer (ch);
                for (int i = 0; i < blockSize; ++i)
                    out[i] = audio[(size_t) (start + i)];
            }

            proc.processBlock (buffer, midi);

            while (proc.waveformFifo.pop (frame))
            {
                ++s.frames;
                if (! std::isfinite (frame.inputMin) || ! std::isfinite (frame.inputMax)
                    || ! std::isfinite (frame.outputMin) || ! std::isfinite (frame.outputMax))
                    s.allFinite = false;
                if (std::abs (frame.inputMin) > 1.0f + 1.0e-6f || std::abs (frame.inputMax) > 1.0f + 1.0e-6f
                    || std::abs (frame.outputMin) > 1.0f + 1.0e-6f || std::abs (frame.outputMax) > 1.0f + 1.0e-6f)
                    s.allWithinFullScale = false;

                const float inPeak = std::max (std::abs (frame.inputMin), std::abs (frame.inputMax));
                const float outPeak = std::max (std::abs (frame.outputMin), std::abs (frame.outputMax));
                s.inputPeak = std::max (s.inputPeak, inPeak);
                s.outputPeak = std::max (s.outputPeak, outPeak);

                if (outPeak > inPeak + 1.0e-6f)
                    s.outputNeverExceedsInput = false;

                // Only compare envelopes on frames that carry real input, so a
                // near-zero crossing cannot make the ratio meaningless.
                if (inPeak > 0.2f)
                    s.minGainRatio = std::min (s.minGainRatio, outPeak / inPeak);
            }
        }
        return s;
    }

    std::unique_ptr<SideChainAudioProcessor> makeProcessor (double amountPercent, bool gate)
    {
        auto proc = std::make_unique<SideChainAudioProcessor>();
        proc->prepareToPlay (kSampleRate, kBlockSize);

        if (auto* amount = dynamic_cast<juce::AudioParameterFloat*> (
                proc->getParameters().getParameter ("sidechainAmount")))
            amount->setValueNotifyingHost (amount->convertTo0to1 ((float) amountPercent));

        if (auto* gateParam = dynamic_cast<juce::AudioParameterBool*> (
                proc->getParameters().getParameter ("transportGate")))
            gateParam->setValueNotifyingHost (gate ? 1.0f : 0.0f);

        auto* playHead = new TestPlayHead();
        playHead->info.bpm = 120.0;
        playHead->info.timeInSamples = 0;
        playHead->info.ppqPosition = 0.0;
        proc->setPlayHead (playHead);
        return proc;
    }

    float currentGateValue (SideChainAudioProcessor& proc)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*> (
                proc.getParameters().getParameter ("transportGate")))
            return p->get();
        return -1.0f;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const int n = (int) (2.0 * kSampleRate);           // 2 s
    auto audio = sine (n, 220.0, kSampleRate, 0.8f);

    // ======================================================================
    // 1. TRANSPORT SWITCH: default, migration, round-trip.
    // ======================================================================
    printf ("\n1. Transport switch: default ON, migration, save/restore\n");
    {
        SideChainAudioProcessor proc;
        check (currentGateValue (proc) == 1.0f,
               "a new instance has the transport switch ON by default");

        auto* param = dynamic_cast<juce::AudioParameterBool*> (
            proc.getParameters().getParameter ("transportGate"));
        check (param != nullptr, "the switch is a host-automatable parameter (transportGate)");
        check (param != nullptr && param->getName (100) == "Active Only During Play/Rec",
               "the parameter is labelled 'Active Only During Play/Rec'");
        check (proc.getParameters().getParameter ("sidechainAmount") != nullptr
                   && proc.getParameters().getParameter ("release") != nullptr
                   && proc.getParameters().getParameter ("duckLength") != nullptr
                   && proc.getParameters().getParameter ("sidechainOffset") != nullptr
                   && proc.getParameters().getParameter ("smooth") != nullptr,
               "no existing parameter id was renamed or removed");
    }
    {
        // OFF -> saved -> new instance -> OFF
        SideChainAudioProcessor writer;
        auto* p = dynamic_cast<juce::AudioParameterBool*> (
            writer.getParameters().getParameter ("transportGate"));
        p->setValueNotifyingHost (0.0f);
        juce::MemoryBlock block;
        writer.getStateInformation (block);

        SideChainAudioProcessor reader;
        reader.setStateInformation (block.getData(), (int) block.getSize());
        check (currentGateValue (reader) == 0.0f,
               "save/restore preserves the switch OFF");

        p = dynamic_cast<juce::AudioParameterBool*> (
            writer.getParameters().getParameter ("transportGate"));
        p->setValueNotifyingHost (1.0f);
        juce::MemoryBlock block2;
        writer.getStateInformation (block2);
        SideChainAudioProcessor reader2;
        reader2.setStateInformation (block2.getData(), (int) block2.getSize());
        check (currentGateValue (reader2) == 1.0f,
               "save/restore preserves the switch ON");

        auto tree = juce::ValueTree::readFromData (block2.getData(), (size_t) block2.getSize());
        check ((int) tree.getProperty ("stateVersion", juce::var (0)) == 7,
               "saved state carries the current schema (7 = 0.4.2)");
    }
    {
        // Legacy migration: a state written before the switch existed (schema 6
        // with the parameter child REMOVED) must load with the default ON.
        SideChainAudioProcessor writer;
        juce::MemoryBlock block;
        writer.getStateInformation (block);
        auto tree = juce::ValueTree::readFromData (block.getData(), (size_t) block.getSize());
        tree.setProperty ("stateVersion", 6, nullptr);

        int removed = 0;
        for (int i = tree.getNumChildren(); --i >= 0;)
            if (tree.getChild (i).getProperty ("id").toString() == "transportGate")
            {
                tree.removeChild (i, nullptr);
                ++removed;
            }
        check (removed == 1, "fixture really removed the transportGate child from the legacy state");

        juce::MemoryBlock legacy;
        {
            juce::MemoryOutputStream stream (legacy, false);
            tree.writeToStream (stream);
        }
        SideChainAudioProcessor reader;
        auto* p = dynamic_cast<juce::AudioParameterBool*> (
            reader.getParameters().getParameter ("transportGate"));
        p->setValueNotifyingHost (0.0f);          // pre-set OFF: migration must override it
        reader.setStateInformation (legacy.getData(), (int) legacy.getSize());
        check (currentGateValue (reader) == 1.0f,
               "legacy state without the setting restores the switch to ON");

        // ...and a malformed / out-of-range value is treated the same way.
        auto broken = tree.createCopy();
        juce::ValueTree bad ("PARAM");
        bad.setProperty ("id", "transportGate", nullptr);
        bad.setProperty ("value", 17.5, nullptr);
        broken.addChild (bad, -1, nullptr);
        juce::MemoryBlock brokenBlock;
        {
            juce::MemoryOutputStream stream (brokenBlock, false);
            broken.writeToStream (stream);
        }
        SideChainAudioProcessor reader2;
        auto* p2 = dynamic_cast<juce::AudioParameterBool*> (
            reader2.getParameters().getParameter ("transportGate"));
        p2->setValueNotifyingHost (0.0f);
        reader2.setStateInformation (brokenBlock.getData(), (int) brokenBlock.getSize());
        check (currentGateValue (reader2) == 1.0f,
               "out-of-range gate value falls back to the ON default");
    }

    // ======================================================================
    // 2. GATING: stopped / playing / recording, gate ON and OFF.
    // ======================================================================
    printf ("\n2. Gating while stopped / playing / recording\n");
    {
        using sid::transport::triggerAllowed;
        check (triggerAllowed (true, true, false) && triggerAllowed (true, false, true),
               "gate ON: PLAY and RECORD both allow triggers");
        check (! triggerAllowed (true, false, false),
               "gate ON: a stopped transport does not allow triggers");
        check (triggerAllowed (false, false, false),
               "gate OFF: triggers are allowed regardless of the transport");
        check (triggerAllowed (false, true, false),
               "gate OFF: still allowed while playing");
    }
    {
        // Gate ON + PLAYING: the established host-anchored behaviour.
        auto proc = makeProcessor (75.0, true);
        const auto s = runAndDrain (*proc, audio, kBlockSize, true);
        const auto triggers = proc->duckEngineForTest().getTriggerCount();
        check (triggers >= 3 && triggers <= 5,
               "gate ON + PLAY at 120 BPM: one trigger per quarter note (got "
               + std::to_string (triggers) + " in 2 s)");
        check (s.minGainRatio < 0.5f,
               "gate ON + PLAY: the processed envelope really ducks (ratio "
               + std::to_string (s.minGainRatio) + ")");
        check (s.outputNeverExceedsInput,
               "gate ON + PLAY: the output trace never exceeds the input trace");
    }
    {
        // Gate ON + STOPPED: no triggers at all (0.4.0 semantics preserved).
        auto proc = makeProcessor (75.0, true);
        const auto s = runAndDrain (*proc, audio, kBlockSize, false);
        check (proc->duckEngineForTest().getTriggerCount() == 0,
               "gate ON + STOPPED: no beat triggers");
        check (std::abs (s.minGainRatio - 1.0f) < 0.02f,
               "gate ON + STOPPED: gain stays at unity (ratio "
               + std::to_string (s.minGainRatio) + ")");
    }
    {
        // Gate OFF + STOPPED: the effect must operate regardless of transport.
        auto proc = makeProcessor (75.0, false);
        const auto s = runAndDrain (*proc, audio, kBlockSize, false);
        const auto triggers = proc->duckEngineForTest().getTriggerCount();
        check (triggers >= 2,
               "gate OFF + STOPPED: the free-running clock keeps triggering (got "
               + std::to_string (triggers) + " in 2 s)");
        check (s.minGainRatio < 0.5f,
               "gate OFF + STOPPED: the processed signal is really ducked (ratio "
               + std::to_string (s.minGainRatio) + ")");
    }
    {
        // Gate OFF + PLAYING: identical to the host-anchored behaviour, because
        // the switch only relaxes the STOP condition.
        auto proc = makeProcessor (75.0, false);
        const auto s = runAndDrain (*proc, audio, kBlockSize, true);
        const auto triggers = proc->duckEngineForTest().getTriggerCount();
        check (triggers >= 3 && triggers <= 5,
               "gate OFF + PLAY: host-anchored beats still fire once per quarter note");
        check (s.minGainRatio < 0.5f, "gate OFF + PLAY: ducking unchanged while the transport rolls");
    }
    {
        // Gate OFF + RECORD alone (isRecording without isPlaying).
        auto proc = makeProcessor (75.0, false);
        const auto s = runAndDrain (*proc, audio, kBlockSize, false, true);
        check (proc->duckEngineForTest().getTriggerCount() >= 3,
               "gate OFF + RECORD alone: recording counts as an active transport");
        check (s.minGainRatio < 0.5f, "gate OFF + RECORD alone: ducking engages");
    }
    {
        // A stopped-then-playing run must not double-fire (no stale schedule).
        auto proc = makeProcessor (75.0, false);
        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::MidiBuffer midi;
        auto* ph = dynamic_cast<TestPlayHead*> (proc->getPlayHead());

        for (int start = 0; start + kBlockSize <= n; start += kBlockSize)
        {
            const bool playing = start >= kBlockSize * 20;   // stop, then play
            ph->info.isPlaying = playing;
            ph->info.isRecording = false;
            ph->info.bpm = 120.0;
            ph->info.timeInSamples = start;
            ph->info.ppqPosition = (double) start / 24000.0;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* out = buffer.getWritePointer (ch);
                for (int i = 0; i < kBlockSize; ++i)
                    out[i] = audio[(size_t) (start + i)];
            }
            proc->processBlock (buffer, midi);
        }
        // The STOP portion (0.21 s) is shorter than the free-run clock's
        // one-beat grace at 120 BPM, so it contributes no trigger; PLAY then
        // adds the host-anchored beats from the playhead position. Anything
        // beyond that count would be a stale or doubled beat.
        const auto triggers = proc->duckEngineForTest().getTriggerCount();
        check (triggers >= 3 && triggers <= 4,
               "free-run -> host handover produces no stale or doubled beats (got "
               + std::to_string (triggers) + ")");
    }
    {
        // The switch must not touch the audio path: with the transport rolling,
        // gate ON and gate OFF take the same host-anchored route and must
        // therefore produce bit-identical output.
        auto withGate = makeProcessor (75.0, true);
        auto withoutGate = makeProcessor (75.0, false);
        juce::AudioBuffer<float> a (2, kBlockSize), b (2, kBlockSize);
        juce::MidiBuffer midi;
        double peakDiff = 0.0;
        auto* phA = dynamic_cast<TestPlayHead*> (withGate->getPlayHead());
        auto* phB = dynamic_cast<TestPlayHead*> (withoutGate->getPlayHead());

        for (int start = 0; start + kBlockSize <= n; start += kBlockSize)
        {
            for (auto* ph : { phA, phB })
            {
                ph->info.isPlaying = true;
                ph->info.isRecording = false;
                ph->info.bpm = 120.0;
                ph->info.timeInSamples = start;
                ph->info.ppqPosition = (double) start / 24000.0;
            }
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlockSize; ++i)
                {
                    a.getWritePointer (ch)[i] = audio[(size_t) (start + i)];
                    b.getWritePointer (ch)[i] = audio[(size_t) (start + i)];
                }

            withGate->processBlock (a, midi);
            withoutGate->processBlock (b, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlockSize; ++i)
                    peakDiff = std::max (peakDiff, (double) std::abs (
                        a.getReadPointer (ch)[i] - b.getReadPointer (ch)[i]));
        }
        check (peakDiff == 0.0,
               "switch ON vs OFF while playing: bit-identical output (the switch does "
               "not alter the audio path)");

        auto legacy = makeProcessor (75.0, true);
        auto* legacyGate = dynamic_cast<juce::AudioParameterBool*> (
            legacy->getParameters().getParameter ("transportGate"));
        legacyGate->setValueNotifyingHost (1.0f);
        check (currentGateValue (*legacy) == 1.0f, "switch stays where it was set");
    }

    // ======================================================================
    // 3. FREE-RUNNING CLOCK (pure scheduler)
    // ======================================================================
    printf ("\n3. Free-running beat clock\n");
    {
        using sid::transport::BeatScheduler;
        BeatScheduler s;
        s.setSampleRate (kSampleRate);

        // 120 BPM at 48 kHz: a quarter note is 24000 samples. The first call
        // arms the clock one beat ahead (no trigger mid-bar).
        const int freeRunBlock = 4800;                 // 0.1 s per block
        std::vector<long long> beats;                  // absolute sample positions
        long long blockStart = 0;
        const auto collect = [&beats, &blockStart] (int offset)
        {
            beats.push_back (blockStart + offset);
        };

        for (int block = 0; block < 10; ++block)
        {
            blockStart = (long long) block * freeRunBlock;
            s.advanceFreeRunning (freeRunBlock, 120.0, collect);
        }
        check (beats.size() == 1 && beats[0] == 24000,
               "free-run: the first beat lands exactly one quarter note after the switch, "
               "so enabling it never triggers mid-bar (got "
               + std::to_string (beats.size()) + " beat(s))");
        check (s.hasFreeRunSchedule(), "free-run: the clock keeps its schedule across blocks");

        // Continue: one beat every 24000 samples, at exactly that spacing.
        beats.clear();
        for (int block = 10; block < 30; ++block)      // 2 s
        {
            blockStart = (long long) block * freeRunBlock;
            s.advanceFreeRunning (freeRunBlock, 120.0, collect);
        }
        check (beats.size() == 4, "free-run at 120 BPM: four beats in two seconds (got "
                                  + std::to_string (beats.size()) + ")");
        bool spacingExact = beats.size() == 4;
        for (std::size_t i = 1; i < beats.size(); ++i)
            if (beats[i] - beats[i - 1] != 24000)
                spacingExact = false;
        check (spacingExact,
               "free-run: consecutive beats are exactly one quarter note (24000 samples) apart");

        // Invalid tempo -> documented fallback, never a broken grid.
        BeatScheduler s2;
        s2.setSampleRate (kSampleRate);
        std::vector<int> beats2;
        s2.advanceFreeRunning (4800, 0.0, [&beats2] (int o) { beats2.push_back (o); });
        check (beats2.empty(), "free-run with invalid BPM: no trigger in the grace beat");
        for (int block = 0; block < 5; ++block)
            s2.advanceFreeRunning (4800, -1.0, [&beats2] (int o) { beats2.push_back (o); });
        check (beats2.size() == 1,
               "free-run with invalid BPM uses the documented 120 BPM fallback");

        // reset() clears the free-run clock as well as the host schedule.
        BeatScheduler s3;
        s3.setSampleRate (kSampleRate);
        s3.advanceFreeRunning (4800, 120.0, [] (int) {});
        check (s3.hasFreeRunSchedule(), "free-run clock armed");
        s3.reset();
        check (! s3.hasFreeRunSchedule() && ! s3.hasSchedule(),
               "reset() clears both the host and the free-run schedule");
    }

    // ======================================================================
    // 4. WAVEFORM SCOPE: the output trace is the PROCESSED signal.
    // ======================================================================
    printf ("\n4. Live waveform: real input vs real processed output\n");
    {
        auto unity = makeProcessor (0.0, true);      // Amount 0 = bit-transparent
        const auto unityStats = runAndDrain (*unity, audio, kBlockSize, true);
        check (unityStats.frames >= 4 * (n / kBlockSize) - 4,
               "one scope frame per chunk is published for every block ("
               + std::to_string (unityStats.frames) + " frames)");
        check (unityStats.allFinite, "every scope frame is finite");
        check (unityStats.allWithinFullScale,
               "every scope frame stays inside +/- full scale");
        check (unityStats.inputPeak > 0.75f && unityStats.inputPeak <= 0.8001f,
               "the input trace carries the REAL input amplitude (peak "
               + std::to_string (unityStats.inputPeak) + ")");
        check (std::abs (unityStats.minGainRatio - 1.0f) < 0.02f,
               "Amount 0: the output trace tracks the input trace (ratio "
               + std::to_string (unityStats.minGainRatio) + ")");

        auto ducking = makeProcessor (75.0, true);   // clearly audible ducking
        const auto duckStats = runAndDrain (*ducking, audio, kBlockSize, true);
        check (duckStats.minGainRatio < 0.5f,
               "Amount 75: the output trace shows the ducking (min ratio "
               + std::to_string (duckStats.minGainRatio) + ")");
        check (duckStats.minGainRatio < unityStats.minGainRatio,
               "the amount parameter changes what the scope draws");
        check (duckStats.outputNeverExceedsInput,
               "the processed trace never exceeds the input trace (no leakage)");
    }
    {
        // Silence: honest, no invented animation.
        auto proc = makeProcessor (75.0, true);
        const auto silent = runAndDrain (*proc, std::vector<float> ((size_t) n, 0.0f),
                                         kBlockSize, true);
        check (silent.allFinite && silent.inputPeak < sid::scope::kSilenceFloor,
               "silence: the scope reports silence rather than noise");

        sid::scope::WaveformHistory history;
        sid::scope::WaveformFifo fifo;
        sid::scope::WaveformFrame f;
        fifo.push (f);
        history.consume (fifo, 1000);
        check (! history.hasSignal(), "silence is detected (no signal in the window)");
        check (! history.hasDisplayableSignal (1000),
               "silence falls back to the no-signal presentation");
    }
    {
        // Invalid samples and clipping can never corrupt the visualization.
        sid::scope::WaveformHistory history;
        sid::scope::WaveformFifo fifo;
        sid::scope::WaveformFrame evil;
        evil.inputMin = std::numeric_limits<float>::quiet_NaN();
        evil.inputMax = std::numeric_limits<float>::infinity();
        evil.outputMin = -std::numeric_limits<float>::infinity();
        evil.outputMax = 1.0e30f;
        fifo.push (evil);
        history.consume (fifo, 1000);

        bool allSane = true;
        for (int i = 0; i < history.count(); ++i)
            for (float v : { history.inputMin (i), history.inputMax (i),
                             history.outputMin (i), history.outputMax (i) })
                if (! std::isfinite (v) || std::abs (v) > 1.0f)
                    allSane = false;
        check (allSane, "NaN / Inf / over-range samples are sanitised to a drawable range");

        // The same through the real processor with a poisoned buffer.
        auto proc = makeProcessor (75.0, true);
        auto poisoned = audio;
        poisoned[5000] = std::numeric_limits<float>::quiet_NaN();
        poisoned[5001] = std::numeric_limits<float>::infinity();
        poisoned[5002] = -1.0e30f;
        poisoned[5003] = 1.0e30f;
        const auto stats = runAndDrain (*proc, poisoned, kBlockSize, true);
        check (stats.allFinite && stats.allWithinFullScale,
               "the scope stays finite and in range even when the audio is not");
        check (proc->duckEngineForTest().getTriggerCount() > 0,
               "poisoned audio does not disturb the trigger schedule");
    }
    {
        // Changing buffer sizes, including the degenerate ones. Tiny blocks are
        // run on a shorter signal so the test stays quick.
        const int sizes[] = { 1, 2, 3, 7, 13, 64, 511, 512, 4096 };
        bool allSafe = true, framesBounded = true;
        for (int blockSize : sizes)
        {
            auto proc = makeProcessor (75.0, true);
            const int useSamples = juce::jmin (n, blockSize <= 64 ? 8192 : n);
            const std::vector<float> slice (audio.begin(), audio.begin() + useSamples);
            const auto stats = runAndDrain (*proc, slice, blockSize, true);
            const int blocks = useSamples / blockSize;
            if (! stats.allFinite || ! stats.allWithinFullScale)
                allSafe = false;
            // At least one frame per block, exactly kFramesPerBlock for blocks
            // that can hold that many chunks, and never more than one frame per
            // sample in the degenerate cases.
            if (stats.frames < blocks || stats.frames > blocks * blockSize)
                framesBounded = false;
            if (blockSize >= 16
                && stats.frames != sid::scope::kFramesPerBlock * blocks)
                framesBounded = false;
        }
        check (allSafe, "block sizes 1..4096: every scope frame stays finite and in range");
        check (framesBounded,
               "every block publishes 1..kFramesPerBlock scope frames "
               "(exactly kFramesPerBlock for normal blocks)");

        // A zero-length block must be a harmless no-op.
        auto proc = makeProcessor (75.0, true);
        sid::scope::WaveformFrame frame;
        while (proc->waveformFifo.pop (frame)) {}   // drop prepareToPlay's baseline
        juce::AudioBuffer<float> empty (2, 1);
        empty.setSize (2, 0);
        juce::MidiBuffer midi;
        proc->processBlock (empty, midi);
        check (! proc->waveformFifo.pop (frame),
               "a zero-length block publishes no scope frame and does not crash");
    }
    {
        // Ring behaviour: capacity is respected and the NEWEST data survives.
        sid::scope::WaveformFifo fifo;
        sid::scope::WaveformFrame frame;
        const int overrun = sid::scope::WaveformFifo::kCapacity + 10;
        for (int i = 0; i < overrun; ++i)
        {
            frame.inputMax = (float) i;
            fifo.push (frame);
        }
        int drained = 0;
        float lastValue = -1.0f;
        while (fifo.pop (frame))
        {
            ++drained;
            lastValue = frame.inputMax;
        }
        check (drained == sid::scope::WaveformFifo::kCapacity,
               "a full ring drops the OLDEST frames and never exceeds its capacity");
        check (std::abs (lastValue - (float) (overrun - 1)) < 0.5f,
               "the newest frame is always the one the GUI reads last");
    }
    {
        // Stalled producer: a frozen trace must not be presented as current.
        sid::scope::WaveformHistory history;
        sid::scope::WaveformFifo fifo;
        sid::scope::WaveformFrame frame;
        frame.inputMin = -0.5f; frame.inputMax = 0.5f;
        frame.outputMin = -0.4f; frame.outputMax = 0.4f;

        for (int i = 0; i < 64; ++i)
            fifo.push (frame);
        history.consume (fifo, 1000);
        check (history.hasDisplayableSignal (1000),
               "live audio is displayed while the producer keeps delivering");
        check (history.hasDisplayableSignal (1000 + sid::scope::WaveformHistory::kStaleAfterMs),
               "the trace is still considered live within the stall window");
        check (! history.hasDisplayableSignal (1000 + sid::scope::WaveformHistory::kStaleAfterMs + 1),
               "a stalled producer degrades to the no-signal fallback after the stall window");
        check (! history.isLive (500), "a clock that moved backwards is not treated as live");
    }

    {
        // Supported sample rates. The scope publishes FRAMES, so it is
        // rate-agnostic by construction - a host at 44.1 / 48 / 88.2 / 96 / 192
        // kHz must still get a real, in-range trace that shows the ducking, and
        // a re-prepare at a new rate must not splice the old trace onto the new
        // one. Each rate is driven for 1.5 s of audio so the trace has to be
        // right at every rate, not just at the 48 kHz the rest of the suite uses.
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
        bool baselineClean = true, framesPublished = true;
        bool finiteAndInRange = true, duckingVisible = true;

        for (double rate : rates)
        {
            auto proc = makeProcessor (75.0, true);
            proc->prepareToPlay (rate, kBlockSize);      // the host changing rate

            // Everything published by prepareToPlay is a silence baseline, so a
            // restart can never present an older trace as current audio.
            sid::scope::WaveformFrame frame;
            while (proc->waveformFifo.pop (frame))
            {
                if (! std::isfinite (frame.inputMin) || ! std::isfinite (frame.inputMax)
                    || ! std::isfinite (frame.outputMin) || ! std::isfinite (frame.outputMax)
                    || std::abs (frame.inputMax) > sid::scope::kSilenceFloor
                    || std::abs (frame.outputMax) > sid::scope::kSilenceFloor)
                    baselineClean = false;
            }

            const int samples = (int) (1.5 * rate);
            const auto stats = runAndDrain (*proc, sine (samples, 220.0, rate, 0.8f),
                                            kBlockSize, true);
            if (stats.frames < samples / kBlockSize)          framesPublished = false;
            if (! stats.allFinite || ! stats.allWithinFullScale) finiteAndInRange = false;
            if (! (stats.minGainRatio < 0.5f))                 duckingVisible = false;
        }

        check (baselineClean,
               "prepareToPlay publishes only a silent baseline at every rate (no stale trace)");
        check (framesPublished, "every rate publishes at least one scope frame per block");
        check (finiteAndInRange,
               "44100/48000/88200/96000/192000 Hz: every scope frame is finite and in range");
        check (duckingVisible,
               "44100/48000/88200/96000/192000 Hz: the processed trace shows the real ducking");
    }

    // ======================================================================
    // 5. AUDIO-THREAD SAFETY (measured, not asserted in a comment)
    // ======================================================================
    printf ("\n5. Audio-thread safety\n");
    {
        auto proc = makeProcessor (75.0, true);
        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::MidiBuffer midi;
        auto* ph = dynamic_cast<TestPlayHead*> (proc->getPlayHead());

        // Warm up (first blocks legitimately touch lazily-created host state).
        for (int start = 0; start < kBlockSize * 8; start += kBlockSize)
        {
            ph->info.isPlaying = true;
            ph->info.timeInSamples = start;
            ph->info.ppqPosition = (double) start / 24000.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlockSize; ++i)
                    buffer.getWritePointer (ch)[i] = audio[(size_t) (start + i)];
            proc->processBlock (buffer, midi);
        }

        allocationCount = 0;
        countAllocations = true;
        for (int start = kBlockSize * 8; start + kBlockSize <= n; start += kBlockSize)
        {
            ph->info.isPlaying = true;
            ph->info.timeInSamples = start;
            ph->info.ppqPosition = (double) start / 24000.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlockSize; ++i)
                    buffer.getWritePointer (ch)[i] = audio[(size_t) (start + i)];
            proc->processBlock (buffer, midi);
        }
        countAllocations = false;
        check (allocationCount == 0,
               "processBlock performs no heap allocation on the audio thread ("
               + std::to_string (allocationCount) + " allocations in "
               + std::to_string ((n - kBlockSize * 8) / kBlockSize) + " blocks)");
    }

    // ======================================================================
    // 6. NON-REGRESSION: presets, reset, curve edits, legacy schema
    // ======================================================================
    printf ("\n6. Existing behaviour is intact\n");
    {
        SideChainAudioProcessor proc;
        check (proc.applyFactoryPreset ("Wide Pump"), "factory presets still apply");
        auto* amount = dynamic_cast<juce::AudioParameterFloat*> (
            proc.getParameters().getParameter ("sidechainAmount"));
        check (std::abs (amount->get() - 70.0f) < 0.051f, "preset still sets its Amount");
        check (proc.getCurrentPresetDisplayName() == "Wide Pump",
               "the preset identity is still derived from the live values");

        proc.applyDefaultPreset();
        check (std::abs (amount->get() - 50.0f) < 0.051f, "Reset restores the defaults");
        check (currentGateValue (proc) == 1.0f,
               "preset/Reset actions leave the transport switch where the user put it");
    }
    {
        // Legacy schema 4 (no gate, legacy envelope) loads safely and keeps the
        // switch ON.
        juce::ValueTree tree ("PARAMS");
        tree.setProperty ("stateVersion", 4, nullptr);
        const std::pair<const char*, double> legacyParams[] = {
            { "sidechainAmount", 60.0 }, { "release", 200.0 },
            { "duckLength", 300.0 }, { "sidechainOffset", 5.0 } };
        for (const auto& p : legacyParams)
        {
            juce::ValueTree child ("PARAM");
            child.setProperty ("id", p.first, nullptr);
            child.setProperty ("value", p.second, nullptr);
            tree.addChild (child, -1, nullptr);
        }
        juce::MemoryBlock block;
        {
            juce::MemoryOutputStream stream (block, false);
            tree.writeToStream (stream);
        }
        SideChainAudioProcessor proc;
        proc.setStateInformation (block.getData(), (int) block.getSize());
        check (currentGateValue (proc) == 1.0f, "schema 4 state loads with the switch ON");
        check (proc.getCurveStateMode() == sid::curve::StateMode::legacy,
               "schema 4 state stays on the legacy envelope");
        auto* amount = dynamic_cast<juce::AudioParameterFloat*> (
            proc.getParameters().getParameter ("sidechainAmount"));
        check (std::abs (amount->get() - 60.0f) < 0.051f, "legacy parameters still restore");
    }
    {
        // Curve editing through the graph still commits and still serializes.
        GraphComponent graph;
        const auto initial = graph.getCurve();
        check (graph.insertPointAt (0.5, 0.8), "curve editing still inserts a point");
        check (graph.getCurve().size() == initial.size() + 1,
               "the curve model really changed");
        graph.resetCurve();
        check (graph.getCurve().size() == sid::curve::PumpCurve::defaultPoints().size(),
               "Reset still restores the default curve");
    }
    {
        // The graph's waveform layer is ON by default and paints real data.
        GraphComponent graph;
        check (graph.isWaveformDisplayEnabled(), "the live waveform layer is enabled by default");
        check (! graph.hasDisplayableWaveform(), "no waveform is shown before any audio arrives");

        sid::scope::WaveformFifo fifo;
        sid::scope::WaveformFrame frame;
        juce::int64 now = 5000;
        for (int i = 0; i < 256; ++i)
        {
            const float phase = (float) i / 32.0f;
            frame.inputMin = -0.6f; frame.inputMax = 0.6f;
            frame.outputMin = -0.6f * (float) std::abs (std::sin (phase));
            frame.outputMax = 0.6f * (float) std::abs (std::sin (phase));
            fifo.push (frame);
        }
        graph.consumeWaveformFifo (fifo, now);
        check (graph.hasDisplayableWaveform(), "draining the scope FIFO produces a displayable trace");
        check (graph.waveformHistoryForTest().count() == 256,
               "every scope frame reaches the GUI history");

        graph.setWaveformDisplayEnabled (false);
        check (! graph.hasDisplayableWaveform(), "the layer can be switched off (display-only)");
        graph.setWaveformDisplayEnabled (true);

        // Stale producer -> fallback.
        graph.consumeWaveformFifo (fifo, now + sid::scope::WaveformHistory::kStaleAfterMs + 1);
        check (! graph.hasDisplayableWaveform(), "a stalled producer falls back to NO SIGNAL");
    }

    // The summary line follows the convention every other suite uses
    // ("<ok>/<total> checks passed. ..."), because Tests/validate_all.sh parses
    // exactly that phrasing to count suites and checks in its stage-3 totals.
    // A different wording left this suite's 81 checks out of the recorded
    // validation totals even though the suite ran and passed.
    printf ("\n==============================\n");
    printf ("%d/%d checks passed. %s\n", totalChecks - failures, totalChecks,
            failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    printf ("==============================\n");
    return failures == 0 ? 0 : 1;
}
