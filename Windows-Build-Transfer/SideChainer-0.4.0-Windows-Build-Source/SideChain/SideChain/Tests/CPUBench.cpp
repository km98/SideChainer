// SideChain Phase 7A CPU sanity benchmark.
// Conditions identical to the Phase 5/6 baseline measurement:
//   10 s stereo main + stereo sidechain, block 512, 48 kHz, Release -O2.
// Reports wall-clock time, realtime multiple and % of one core.
//
// Build/run:  cd Tests && ./cpu_bench.sh

#include <JuceHeader.h>
#include "../Source/DuckEngine.h"

#include <chrono>
#include <cstdio>
#include <cmath>

static void fillSignal (juce::AudioBuffer<float>& mainBuf,
                        juce::AudioBuffer<float>& scBuf,
                        int startSample, int numSamples, double sampleRate)
{
    const float twoPi = juce::MathConstants<float>::twoPi;

    for (int i = 0; i < numSamples; ++i)
    {
        const double t = (double) (startSample + i) / sampleRate;

        // Music-like main signal.
        const float main = 0.25f * std::sin ((float) (twoPi * 220.0 * t))
                         + 0.20f * std::sin ((float) (twoPi * 554.37 * t))
                         + 0.15f * std::sin ((float) (twoPi * 1108.0 * t));
        mainBuf.setSample (0, i, main);
        mainBuf.setSample (1, i, main * 0.9f);

        // Kick-ish sidechain: 2 Hz gate, 120 ms bursts.
        const double phase = std::fmod (t, 0.5);
        const float burst = phase < 0.12 ? 1.0f : 0.0f;
        const float sc = burst * std::sin ((float) (twoPi * 60.0 * t));
        scBuf.setSample (0, i, sc);
        scBuf.setSample (1, i, sc);
    }
}

static int beatCountdown_ = 0;
static double engineSampleRate_ = 48000.0;

static void runBlockRange (sid::dsp::DuckEngine& engine,
                           juce::AudioBuffer<float>& mainBuf,
                           juce::AudioBuffer<float>& scBuf,
                           int startSample, int numSamples)
{
    const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (0.5f);

    // Raw pointers (as a real processBlock would use) — no bounds-checked
    // accessors inside the loop, so we measure engine cost only.
    const float* scL = scBuf.getReadPointer (0)  + startSample;
    const float* scR = scBuf.getReadPointer (1)  + startSample;
    float* outL      = mainBuf.getWritePointer (0) + startSample;
    float* outR      = mainBuf.getWritePointer (1) + startSample;

    for (int i = 0; i < numSamples; ++i)
    {
        // 0.4.0: the engine is trigger-driven (no sidechain input); beat
        // triggers fire on a 120 BPM grid via fireTrigger().
        if (beatCountdown_ <= 0)
        {
            engine.fireTrigger();
            beatCountdown_ = (int) std::lround (0.5 * engineSampleRate_);
        }
        --beatCountdown_;
        const float gain = engine.processSample (depthDb);
        outL[i] *= gain;
        outR[i] *= gain;
    }
}

int main()
{
    constexpr double  sampleRate = 48000.0;
    constexpr int     blockSize  = 512;
    constexpr int     seconds    = 10;
    const     int     totalSamples = (int) (sampleRate * seconds);
    const     int     numBlocks   = totalSamples / blockSize;

    sid::dsp::DuckEngine engine;
    engine.prepare (sampleRate);
    engineSampleRate_ = sampleRate;
    beatCountdown_ = 0; // fire on the first sample
    engine.setReleaseTimes (150.0f, sid::dsp::DuckEngine::kReleaseDefaultMs);

    // Test signal: main = music-like sum of sines; sidechain = kick-ish
    // amplitude-modulated burst train (2 Hz gate, 120 ms bursts).
    // Pre-generated OUTSIDE the timed region so the benchmark measures
    // only the engine's per-sample cost (like the Phase 5/6 baseline).
    juce::AudioBuffer<float> mainBuf (2, totalSamples);
    juce::AudioBuffer<float> scBuf   (2, totalSamples);
    fillSignal (mainBuf, scBuf, 0, totalSamples, sampleRate);

    // Warm-up (JIT/page faults, cache) - 1 s not timed.
    {
        for (int i = 0; i < sampleRate / blockSize; ++i)
            runBlockRange (engine, mainBuf, scBuf, i * blockSize, blockSize);
        engine.reset();
    }

    const auto t0 = std::chrono::steady_clock::now();

    for (int b = 0; b < numBlocks; ++b)
        runBlockRange (engine, mainBuf, scBuf, b * blockSize, blockSize);

    const auto t1 = std::chrono::steady_clock::now();
    const double wallMs
        = std::chrono::duration<double, std::milli> (t1 - t0).count();

    const double audioMs = seconds * 1000.0;
    const double realtime = audioMs / wallMs;
    const double pctOfCore = 100.0 * wallMs / audioMs;

    std::printf ("CPU bench: %.1f s audio (stereo main + stereo sidechain), "
                 "block %d, %.0f kHz, Release -O2\n",
                 (double) seconds, blockSize, sampleRate / 1000.0);
    std::printf ("  wall time      : %.2f ms\n", wallMs);
    std::printf ("  realtime       : %.0fx\n", realtime);
    std::printf ("  %% of one core  : %.3f %%\n", pctOfCore);
    std::printf ("  final gain     : %.4f (finite: %s)\n",
                 engine.getCurrentGain(),
                 std::isfinite (engine.getCurrentGain()) ? "yes" : "NO");

    return 0;
}
