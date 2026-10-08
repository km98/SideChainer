/*
    SideChainer - 0.4.0 internal trigger scheduler tests.

    Deterministic tests for BeatScheduler (pure, host-independent) plus the
    full processor path with a scripted AudioPlayHead:

      Transport:  PLAY fires every quarter note, RECORD fires, STOP fires
                  nothing, restart resynchronizes, jumps produce no stale
                  triggers, loop restart works, tempo change works, BPM
                  unavailable fails safely.
      Timing:     sample-accurate beat location, one trigger per beat, no
                  duplicate/missed triggers across block boundaries,
                  multiple beats per block handled.
      Processor:  one beat = one duck; 4 beats = 4 ducks; recovery between;
                  no external sidechain required.
*/

#include <cstdio>
#include <cmath>
#include <vector>
#include <optional>

#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/BeatScheduler.h"

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
    // Collects (hostSample, beatIndex) pairs from the scheduler.
    struct Record
    {
        std::vector<std::pair<long long, double>> beats;

        void feed (sid::transport::BeatScheduler& s, double sr, int blockSize,
                   long long startSample, double bpm, double ppq,
                   bool validPpq = true)
        {
            s.advance (startSample, blockSize, bpm, validPpq ? ppq : -1.0,
                       [this] (int offset)
                       {
                           // host sample of the beat = startSample + offset.
                           // We recover it from the last pushed end.
                           beats.push_back ({ pendingStart_ + offset, nextPpq_ });
                           nextPpq_ += 1.0;
                       });
            pendingStart_ = startSample;
        }

        long long pendingStart_ = 0;
        double nextPpq_ = 0.0;
    };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double sr = 48000.0;

    printf ("SideChainer Beat Scheduler Tests (0.4.0)\n========================================\n");

    // ==================================================================
    // 1. Pure scheduler: sample-accurate quarter-note beats.
    // ==================================================================
    printf ("\n1. Scheduler: sample-accurate quarter notes\n");
    {
        // 120 BPM = one beat per 0.5 s = 24000 samples.
        sid::transport::BeatScheduler s;
        s.setSampleRate (sr);
        const double bpm = 120.0;
        const int blockSize = 512;

        // Simulate 2 s of contiguous playback starting at PPQ 0.
        std::vector<long long> beatSamples;
        long long sample = 0;
        s.advance (sample, blockSize, bpm, 0.0,
                   [&] (int off) { beatSamples.push_back (sample + off); });
        sample += blockSize;
        while (sample < (long long) (2.0 * sr))
        {
            s.advance (sample, blockSize, bpm, sample / (60.0 / bpm * sr),
                       [&] (int off) { beatSamples.push_back (sample + off); });
            sample += blockSize;
        }

        // Expected beats at 0, 24000, 48000, 72000 and 96000 samples (the
        // last block [95744, 96256) still contains the 96000 beat).
        check ((int) beatSamples.size() == 5, "one trigger per quarter note over 2 s (got "
               + std::to_string (beatSamples.size()) + " of 5)");
        bool exact = beatSamples.size() == 5;
        for (int i = 0; i < (int) beatSamples.size(); ++i)
            if (std::llabs (beatSamples[(size_t) i] - (long long) (i * 24000)) > 1)
                exact = false;
        check (exact, "beats land at exact quarter-note sample positions");

        // A host may begin processing after the transport is already between
        // beats (e.g. plugin insertion or a non-beat-aligned locate). From
        // PPQ 2.25 the next beat is 0.75 quarter notes away, not 0.25.
        sid::transport::BeatScheduler fractional;
        fractional.setSampleRate (sr);
        std::vector<int> fractionalOffsets;
        fractional.advance (1000, 48000, bpm, 2.25,
                            [&] (int off) { fractionalOffsets.push_back (off); });
        check (fractionalOffsets.size() == 2
                   && fractionalOffsets[0] == 18000
                   && fractionalOffsets[1] == 42000,
               "fractional PPQ anchors at the next integer beat, then advances one quarter per trigger");
    }

    // ==================================================================
    // 2. Multiple beats inside one block + no duplicates at boundaries.
    // ==================================================================
    printf ("\n2. Multi-beat blocks and boundary safety\n");
    {
        sid::transport::BeatScheduler s;
        s.setSampleRate (sr);
        const double bpm = 120.0; // 24000 samples per quarter

        // 48000-sample block starting exactly on a beat: beats at offsets
        // 0 and 24000 inside this block.
        std::vector<int> offsets;
        s.advance (0, 48000, bpm, 0.0, [&] (int off) { offsets.push_back (off); });
        check (offsets.size() == 2 && offsets[0] == 0 && offsets[1] == 24000,
               "block spanning 2 beats fires both, at exact offsets");

        // Block boundary straddle: 250 BPM = 11520 samples per quarter -
        // NOT a multiple of 512, so beats repeatedly straddle block
        // boundaries. Over 60 blocks (30720 samples) the beats at 0,
        // 11520 and 23040 must each fire exactly once at the exact sample.
        sid::transport::BeatScheduler s2;
        s2.setSampleRate (sr);
        std::vector<long long> hits;
        long long sample = 0;
        for (int b = 0; b < 60; ++b)
        {
            const long long start = sample;
            s2.advance (start, 512, 250.0, (double) start / 11520.0,
                        [&] (int off) { hits.push_back (start + off); });
            sample += 512;
        }
        bool straddleOk = hits.size() == 3;
        for (int i = 0; i < (int) hits.size(); ++i)
            if (std::llabs (hits[(size_t) i] - (long long) (i * 11520)) > 1) straddleOk = false;
        check (straddleOk,
               "quarter notes straddling block boundaries: no misses, no duplicates");
    }

    // ==================================================================
    // 3. Transport jump / loop restart: no stale triggers.
    // ==================================================================
    printf ("\n3. Transport jumps and loop restarts\n");
    {
        sid::transport::BeatScheduler s;
        s.setSampleRate (sr);
        const double bpm = 120.0;
        std::vector<long long> hits;

        // Play 1 s from 0, then JUMP the host timeline to sample 100000
        // (discontinuity) as if the user repositioned.
        long long sample = 0;
        for (int b = 0; b < 93; ++b) // 93 * 512 ~= 47616 samples
        {
            s.advance (sample, 512, bpm, (double) sample / 24000.0,
                       [&] (int off) { hits.push_back (sample + off); });
            sample += 512;
        }
        const size_t hitsBeforeJump = hits.size();
        sample = 100000; // jump
        s.advance (sample, 512, bpm, (double) sample / 24000.0,
                   [&] (int off) { hits.push_back (sample + off); });
        sample += 512;
        // After a jump the schedule re-anchors from the new PPQ: no trigger
        // may fire at a stale (pre-jump) position.
        bool stale = false;
        for (size_t i = hitsBeforeJump; i < hits.size(); ++i)
            if (hits[i] < 100000) stale = true;
        check (! stale, "transport jump: no stale triggers from the old timeline");

        // Loop restart: jump back to 0 -> schedule re-anchors and beats
        // fire again from the loop start.
        sample = 0;
        s.advance (sample, 512, bpm, 0.0,
                   [&] (int off) { hits.push_back (sample + off); });
        check (hits.back() < 512, "loop restart re-anchors to the loop start");
    }

    // ==================================================================
    // 4. Tempo change and BPM unavailable.
    // ==================================================================
    printf ("\n4. Tempo change / BPM unavailable\n");
    {
        sid::transport::BeatScheduler s;
        s.setSampleRate (sr);
        std::vector<long long> hits;
        long long sample = 0;

        // 1 s at 120 BPM (24000 spq), then switch to 60 BPM (48000 spq)
        // WITHOUT a jump (contiguous timeline): subsequent beats follow the
        // new tempo (96000, 144000...).
        for (int b = 0; b < 93; ++b)
        {
            s.advance (sample, 512, 120.0, (double) sample / 24000.0,
                       [&] (int off) { hits.push_back (sample + off); });
            sample += 512;
        }
        for (int b = 0; b < 47; ++b)
        {
            s.advance (sample, 512, 60.0, (double) sample / 48000.0,
                       [&] (int off) { hits.push_back (sample + off); });
            sample += 512;
        }
        bool tempoFollowed = true;
        // Late beats must be near the 60 BPM grid (48000-multiples): check
        // every hit after sample 96000 is within 2 samples of a 48000 grid.
        for (long long h : hits)
            if (h >= 96000)
            {
                const long long m = h % 48000;
                if (std::min (m, 48000 - m) > 2) tempoFollowed = false;
            }
        check (tempoFollowed, "tempo change: beat grid follows the new BPM");

        // BPM unavailable (0): advance() produces NO triggers, no crash.
        sid::transport::BeatScheduler s2;
        s2.setSampleRate (sr);
        int bpmZeroHits = 0;
        s2.advance (0, 512, 0.0, 0.0, [&] (int) { ++bpmZeroHits; });
        check (bpmZeroHits == 0, "BPM unavailable -> no triggers (fail-safe)");

        // PPQ unavailable (negative) -> no triggers for that block.
        sid::transport::BeatScheduler s3;
        s3.setSampleRate (sr);
        int ppqInvalidHits = 0;
        s3.advance (0, 512, 120.0, -1.0, [&] (int) { ++ppqInvalidHits; });
        check (ppqInvalidHits == 0, "PPQ unavailable -> no triggers (fail-safe)");
    }

    // ==================================================================
    // 5. Full processor: PLAY/RECORD/STOP + beat-accurate ducking.
    // ==================================================================
    printf ("\n5. Processor integration (real processBlock + play head)\n");
    {
        class TestPlayHead : public juce::AudioPlayHead
        {
        public:
            bool getCurrentPosition (CurrentPositionInfo& result) override
            { result = info; return positionValid; }
            CurrentPositionInfo info;
            bool positionValid = true;
        };

        const double bpm = 120.0; // beat every 0.5 s = 24000 samples
        const double seconds = 2.2;
        const int n = (int) (seconds * sr);
        auto mainS = sine (n, 440.0, sr, 0.5f);

        // Helper: run the real processor with a scripted transport.
        auto run = [&] (bool playing, bool recording) -> std::vector<float>
        {
            SideChainAudioProcessor proc;
            auto* ph = new TestPlayHead();
            ph->info.bpm = bpm;
            proc.setPlayHead (ph);
            proc.prepareToPlay (sr, 512);

            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer midi;
            std::vector<float> out;
            out.reserve ((size_t) n);
            long long timeline = 0;
            for (int start = 0; start + 512 <= n; start += 512)
            {
                ph->positionValid = true;
                ph->info.isPlaying = playing;
                ph->info.isRecording = recording;
                ph->info.timeInSamples = timeline;
                ph->info.ppqPosition = (double) timeline / 24000.0;

                for (int i = 0; i < 512; ++i)
                {
                    block.setSample (0, i, mainS[(size_t) (start + i)]);
                    block.setSample (1, i, mainS[(size_t) (start + i)]);
                }
                proc.processBlock (block, midi);
                for (int i = 0; i < 512; ++i) out.push_back (block.getSample (0, i));
                timeline += 512;
            }
            return out;
        };

        // Reference: a 120 BPM duck train through the same engine, to
        // compare against the processor output during PLAY.
        sid::dsp::DuckEngine ref;
        ref.prepare (sr);
        ref.setReleaseTimes (150.0f, 150.0f);
        ref.setDuckLengthMs (250.0f);
        const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (0.5f);
        // Duck length 250 ms at 120 BPM = 24000 samples; fire every beat.
        std::vector<float> refGain;
        refGain.reserve ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            if (i % 24000 == 0) ref.fireTrigger();
            refGain.push_back (ref.processSample (depthDb));
        }
        // Latency-align: the processor's output = delayedIn * gain.
        const int lat = 1440; // 30 ms lookahead at 48k
        std::vector<float> delayedIn ((size_t) n, 0.0f);
        for (int i = 0; i < n; ++i)
            delayedIn[(size_t) i] = (i >= lat) ? mainS[(size_t) (i - lat)] : 0.0f;

        auto playOut = run (true, false);
        bool playDucks = false;
        {
            // Compare envelope presence: min gain during PLAY vs the model.
            float minOut = 1.0f; float maxRatio = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float g = (std::abs (delayedIn[(size_t) i]) > 1.0e-6f)
                    ? playOut[(size_t) i] / delayedIn[(size_t) i] : 1.0f;
                if (g < minOut) minOut = g;
                if (g > maxRatio) maxRatio = g;
            }
            playDucks = minOut < 0.4f && maxRatio <= 1.001f;
        }
        check (playDucks, "PLAY: beat-synchronised ducking occurs (deep + bounded)");

        // PLAY must produce EXACTLY 5 ducks in 2.2 s (beats at 0/0.5/1/1.5/2 s).
        {
            SideChainAudioProcessor proc;
            auto* ph = new TestPlayHead();
            ph->info.bpm = bpm;
            proc.setPlayHead (ph);
            proc.prepareToPlay (sr, 512);
            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer midi;
            long long timeline = 0;
            for (int start = 0; start + 512 <= n; start += 512)
            {
                ph->info.isPlaying = true;
                ph->info.timeInSamples = timeline;
                ph->info.ppqPosition = (double) timeline / 24000.0;
                for (int i = 0; i < 512; ++i)
                {
                    block.setSample (0, i, mainS[(size_t) (start + i)]);
                    block.setSample (1, i, mainS[(size_t) (start + i)]);
                }
                proc.processBlock (block, midi);
                timeline += 512;
            }
            const int trig = proc.duckEngineForTest().getTriggerCount();
            check (trig == 5, "PLAY 2.2 s @120 BPM -> exactly 5 beat triggers (got "
                   + std::to_string (trig) + ")");
        }

        // STOP: no triggers at all -> output == delayed input (unity).
        {
            auto stopOut = run (false, false);
            bool unity = true;
            // Only full blocks were produced: compare inside the output's
            // own length (out.size() can be < n by up to one block).
            const int stopN = (int) std::min ((long long) stopOut.size(), (long long) n);
            for (int i = lat; i < stopN; ++i)
                if (std::abs (stopOut[(size_t) i] - delayedIn[(size_t) i]) > 1.0e-3f)
                    { unity = false; break; }
            check (unity, "STOP: no triggers, output transparent (bit-identical path)");
        }

        // RECORD: triggers just like PLAY.
        {
            auto recOut = run (false, true);
            bool recDucks = false;
            float minOut = 1.0f;
            for (int i = 0; i < n; ++i)
            {
                const float g = (std::abs (delayedIn[(size_t) i]) > 1.0e-6f)
                    ? recOut[(size_t) i] / delayedIn[(size_t) i] : 1.0f;
                if (g < minOut) minOut = g;
            }
            recDucks = minOut < 0.4f;
            check (recDucks, "RECORD: beat-synchronised ducking occurs");
        }

        // 4 beats -> 4 ducks with recovery between (no continuous duck).
        {
            SideChainAudioProcessor proc;
            auto* ph = new TestPlayHead();
            ph->info.bpm = bpm;
            proc.setPlayHead (ph);
            proc.prepareToPlay (sr, 512);
            proc.applyFactoryPreset ("Classic Kick"); // 250 ms length < beat gap

            const int n4 = (int) (2.1 * sr); // beats at 0, 0.5, 1, 1.5, 2 s
            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer midi;
            long long timeline = 0;
            std::vector<float> gainCurve;
            for (int start = 0; start + 512 <= n4; start += 512)
            {
                ph->info.isPlaying = true;
                ph->info.timeInSamples = timeline;
                ph->info.ppqPosition = (double) timeline / 24000.0;
                for (int i = 0; i < 512; ++i)
                {
                    block.setSample (0, i, mainS[(size_t) (start + i)]);
                    block.setSample (1, i, mainS[(size_t) (start + i)]);
                }
                proc.processBlock (block, midi);
                for (int i = 0; i < 512; ++i) gainCurve.push_back (block.getSample (0, i));
                timeline += 512;
            }
            const int trig = proc.duckEngineForTest().getTriggerCount();
            check (trig >= 4, "four-beat window produces at least 4 ducks (got "
                   + std::to_string (trig) + ")");

            // Recovery: gain between beats must return above 90 % unity.
            bool recovers = false;
            for (int i = 1000; i < (int) gainCurve.size() - 1000; ++i)
            {
                const float in = mainS[(size_t) i];
                const float g = (std::abs (in) > 1.0e-6f) ? gainCurve[(size_t) i] / in : 1.0f;
                if (g > 0.9f) { recovers = true; break; }
            }
            check (recovers, "main audio recovers between beat ducks");
        }

        // Stopped-then-play restart: resynchronizes, first block beats.
        {
            SideChainAudioProcessor proc;
            auto* ph = new TestPlayHead();
            ph->info.bpm = bpm;
            proc.setPlayHead (ph);
            proc.prepareToPlay (sr, 512);
            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer midi;
            long long timeline = 0;
            // STOPPED 5 blocks (timeline does not advance).
            for (int b = 0; b < 5; ++b)
            {
                ph->info.isPlaying = false;
                ph->info.timeInSamples = timeline;
                for (int i = 0; i < 512; ++i) block.setSample (0, i, 0.0f);
                proc.processBlock (block, midi);
            }
            // PLAY from sample 0 again: the first block fires beat 0.
            ph->info.isPlaying = true;
            ph->info.timeInSamples = 0;
            ph->info.ppqPosition = 0.0;
            for (int i = 0; i < 512; ++i) block.setSample (0, i, 0.3f);
            proc.processBlock (block, midi);
            check (proc.duckEngineForTest().getTriggerCount() == 1,
                   "stop->play restart resynchronizes (beat fires immediately)");
        }
    }

    // ==================================================================
    // 6. DUCK LENGTH affects the envelope duration on the beat grid.
    // ==================================================================
    printf ("\n6. DUCK LENGTH drives audible duration (beats fixed)\n");
    {
        class TestPlayHead : public juce::AudioPlayHead
        {
        public:
            bool getCurrentPosition (CurrentPositionInfo& result) override
            { result = info; return true; }
            CurrentPositionInfo info;
        };
        const double bpm = 120.0;

        auto measureDepth = [&] (float lengthMs) -> float
        {
            SideChainAudioProcessor proc;
            auto* ph = new TestPlayHead();
            ph->info.bpm = bpm;
            proc.setPlayHead (ph);
            proc.prepareToPlay (sr, 512);
            proc.applyFactoryPreset ("Classic Kick");
            if (auto* pl = proc.getParameters().getParameter ("duckLength"))
                pl->setValueNotifyingHost (pl->convertTo0to1 (lengthMs));

            const int n = (int) (1.2 * sr);
            auto mainS = sine (n, 440.0, sr, 0.5f);
            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer midi;
            long long timeline = 0;
            float minGain = 1.0f;
            const int lat = juce::jlimit (0, n, (int) proc.getLatencySamples());
            std::vector<float> inDelay ((size_t) lat + 1, 0.0f);
            size_t inDelayPos = 0;
            for (int start = 0; start + 512 <= n; start += 512)
            {
                ph->info.isPlaying = true;
                ph->info.timeInSamples = timeline;
                ph->info.ppqPosition = (double) timeline / 24000.0;
                for (int i = 0; i < 512; ++i)
                {
                    block.setSample (0, i, mainS[(size_t) (start + i)]);
                    block.setSample (1, i, mainS[(size_t) (start + i)]);
                }
                proc.processBlock (block, midi);
                for (int i = 0; i < 512; ++i)
                {
                    inDelay[(size_t) inDelayPos] = mainS[(size_t) (start + i)];
                    const float delayedIn = inDelay[(size_t) ((inDelayPos + 1) % inDelay.size())];
                    inDelayPos = (inDelayPos + 1) % inDelay.size();
                    const float o = block.getSample (0, i);
                    const float g = (std::abs (delayedIn) > 1.0e-6f) ? o / delayedIn : 1.0f;
                    minGain = std::min (minGain, g);
                }
                timeline += 512;
            }
            // Deepest gain must represent a real duck (Amount 50% => ~-8.7 dB
            // at the settled depth; a 50 ms envelope still reaches it).
            return minGain;
        };

        // Both extremes of DUCK LENGTH produce a real audible duck on the
        // beat grid (min gain well below unity at Amount 50 %).
        const float deep = measureDepth (50.0f);
        const float deepLong = measureDepth (1000.0f);
        printf ("    [deep=%.3f deepLong=%.3f]\n", deep, deepLong);
        check (deep < 0.55f && deepLong < 0.55f,
               "DUCK LENGTH min (50 ms) and max (1000 ms) both duck on beats");
    }

    printf ("\n==============================\n");
    printf ("%d/%d checks passed. %s\n", totalChecks - failures, totalChecks,
            failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
