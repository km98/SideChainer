/*
    SideChain - Phase 3 DSP regression tests (0.4.0 internal trigger).

    Exercises DuckEngine through a harness that mirrors the production
    path: the BEAT SCHEDULER fires the trigger (fireTrigger) on the
    quarter-note grid, the envelope generator + delay line produce the
    gain applied to both main channels. Same invariant families as the
    original Phase 3 suite: unity bounds, finite output, shared-gain
    stereo integrity, depth curve, block-size and sample-rate stability.
*/

#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

#include "../Source/DuckEngine.h"

static int failures = 0;
static int totalChecks = 0;

static void check (bool ok, const std::string& what)
{
    ++totalChecks;
    printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

namespace sid::test
{

struct Result
{
    std::vector<float> outL, outR;
    std::vector<float> gainCurve;        // per-sample applied gain
    bool nanInfFound = false;
};

// Runs the exact per-sample production path for n frames with internal
// beat triggers at `bpm` (bpm <= 0 => no triggers at all).
static Result run (double sampleRate, int blockSize,
                   float amountPercent, double bpm,
                   const std::vector<float>& mainL, const std::vector<float>& mainR)
{
    sid::dsp::DuckEngine engine;
    engine.prepare (sampleRate);

    Result r;
    const int n = (int) mainL.size();
    r.outL.resize ((size_t) n); r.outR.resize ((size_t) n);
    r.gainCurve.reserve ((size_t) n);

    const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (amountPercent / 100.0f);
    const double samplesPerBeat = (bpm > 0.0) ? 60.0 / bpm * sampleRate : 0.0;
    double nextBeat = 0.0;

    for (int start = 0; start < n; start += blockSize)
    {
        const int count = std::min (blockSize, n - start);

        while (bpm > 0.0 && nextBeat < start + count)
        {
            if (nextBeat >= start)
                engine.fireTrigger();
            nextBeat += samplesPerBeat;
        }

        for (int i = start; i < start + count; ++i)
        {
            const float gain = engine.processSample (depthDb);

            r.outL[(size_t) i] = mainL[(size_t) i] * gain;
            r.outR[(size_t) i] = mainR[(size_t) i] * gain;
            r.gainCurve.push_back (gain);

            if (! std::isfinite (r.outL[(size_t) i]) || ! std::isfinite (r.outR[(size_t) i])
                || ! std::isfinite (gain))
                r.nanInfFound = true;
        }
    }
    return r;
}

static double rms (const std::vector<float>& v)
{
    if (v.empty()) return 0.0;
    double s = 0;
    for (float x : v) s += (double) x * x;
    return std::sqrt (s / (double) v.size());
}

static double peak (const std::vector<float>& v)
{
    float p = 0;
    for (float x : v) p = std::max (p, std::abs (x));
    return p;
}

static std::vector<float> sine (int n, double freq, double sr, float amp)
{
    std::vector<float> v ((size_t) n);
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        v[(size_t) i] = amp * (float) std::sin (ph);
        ph += 2.0 * 3.14159265358979 * freq / sr;
    }
    return v;
}

} // namespace sid::test

using namespace sid::test;

int main()
{
    printf ("SideChain Phase 3 DSP Regression Tests (0.4.0)\n==============================================\n");
    const double sr = 48000.0;
    const int n = 48000; // 1 second
    const double bpm = 120.0; // beat every 0.5 s

    // ----------------------------------------------------------------------
    printf ("\nTEST 1: Amount = 0%% => unity passthrough (even with beats firing)\n");
    {
        auto mainL = sine (n, 440.0, sr, 0.5f);
        auto mainR = sine (n, 660.0, sr, 0.5f);
        auto r = run (sr, 512, 0.0f, bpm, mainL, mainR);

        double diff = 0;
        for (int i = 0; i < n; ++i)
            diff += std::abs ((double) r.outL[(size_t) i] - (double) mainL[(size_t) i]);
        check (diff < 1e-4, "Amount 0%: output == input (L)");
        check (r.gainCurve.back() > 0.999f, "Amount 0%: final gain == 1.0");
    }

    // ----------------------------------------------------------------------
    printf ("\nTEST 2: No beats (stopped transport) => no ducking\n");
    {
        auto mainL = sine (n, 440.0, sr, 0.5f);
        auto mainR = sine (n, 660.0, sr, 0.5f);
        auto r = run (sr, 512, 50.0f, 0.0, mainL, mainR);

        check (r.gainCurve.back() > 0.999f, "no beats: gain stays at ~1.0");
        check (rms (r.outL) > 0.3 * rms (mainL), "no beats: main not muted");
    }

    // ----------------------------------------------------------------------
    printf ("\nTEST 3: One beat => one duck then smooth recovery\n");
    {
        auto mainL = sine (n, 440.0, sr, 0.5f);
        auto mainR = sine (n, 660.0, sr, 0.5f);
        // 1.2 s at 60 BPM => exactly one beat at t = 0.
        auto r = run (sr, 512, 100.0f, 60.0, mainL, mainR);

        float minGain = 1.0f; int minPos = 0;
        for (int i = 0; i < n; ++i)
            if (r.gainCurve[(size_t) i] < minGain) { minGain = r.gainCurve[(size_t) i]; minPos = i; }

        check (minGain < 0.2f, "beat: deep duck occurred (Amount 100 %)");
        check (minPos < (int) (0.3 * sr), "beat: reduction begins at the beat (no pre-ducking)");
        check (r.gainCurve.back() > 0.95f, "beat: smooth recovery to near-unity");
        // Recovery is gradual: 10 ms after min gain the envelope is still
        // ducked and has not jumped back.
        const int afterMin = minPos + (int) (0.010 * sr);
        if (afterMin < n)
            check (r.gainCurve[(size_t) afterMin] < 0.99f
                       && r.gainCurve[(size_t) afterMin] >= minGain,
                   "beat: recovery is smooth (no jump back)");
    }

    // ----------------------------------------------------------------------
    printf ("\nTEST 4: Repeated beats => repeated ducks, recovery between\n");
    {
        auto mainL = sine (n, 440.0, sr, 0.5f);
        auto mainR = sine (n, 660.0, sr, 0.5f);
        auto r = run (sr, 512, 100.0f, bpm, mainL, mainR);

        float minG = 1.0f;
        for (float g : r.gainCurve) minG = std::min (minG, g);
        check (minG < 0.2f, "repeated beats: deep ducks occurred");

        // Count distinct dips (runs of gain < 0.9) in 1 s at 120 BPM = 2.
        int dips = 0; bool inDip = false;
        for (float g : r.gainCurve)
        {
            if (g < 0.9f) { if (! inDip) ++dips; inDip = true; }
            else inDip = false;
        }
        check (dips == 2, "repeated beats: exactly one duck per beat (got "
               + std::to_string (dips) + " of 2)");

        // No settled ducking: gain returns near unity between beats.
        check (r.gainCurve.back() > 0.95f, "repeated beats: recovery between beats");
    }

    // ----------------------------------------------------------------------
    printf ("\nTEST 5: Stereo main integrity (different L/R signals)\n");
    {
        auto mainL = sine (n, 440.0, sr, 0.5f);
        auto mainR = sine (n, 440.0, sr, 0.3f);  // same tone, quieter R
        auto r = run (sr, 256, 100.0f, bpm, mainL, mainR);

        double maxRatioErr = 0;
        for (int i = 1; i < n; ++i)
        {
            if (std::abs (mainL[(size_t) i]) < 0.1f) continue;
            const double inRatio  = mainR[(size_t) i] / (mainL[(size_t) i] + 1e-12);
            const double outRatio = r.outR[(size_t) i] / (r.outL[(size_t) i] + 1e-12);
            maxRatioErr = std::max (maxRatioErr, std::abs (outRatio - inRatio));
        }
        check (maxRatioErr < 1e-6, "stereo main: L/R ratio preserved under ducking");
    }

    // ----------------------------------------------------------------------
    printf ("\nTEST 6: One shared gain on both main channels\n");
    {
        auto mainL = sine (n, 440.0, sr, 0.5f);
        auto mainR = mainL;
        auto r = run (sr, 256, 100.0f, bpm, mainL, mainR);

        double maxErr = 0;
        for (int i = 0; i < n; ++i)
            maxErr = std::max (maxErr, (double) std::abs (r.outR[(size_t) i] - r.outL[(size_t) i]));
        check (maxErr < 1e-9, "single shared gain applied to both main channels");
    }

    // ----------------------------------------------------------------------
    printf ("\nTEST 7: No leakage (silence in => silence out, even while ducking)\n");
    {
        std::vector<float> silence (n, 0.0f);
        auto r = run (sr, 512, 100.0f, bpm, silence, silence);

        check (peak (r.outL) == 0.0f && peak (r.outR) == 0.0f,
               "leakage: silent main stays exactly silent while beats duck");
        check (! r.nanInfFound, "leakage: no NaN/Inf");
    }

    // ----------------------------------------------------------------------
    printf ("\nTEST 8: Amount progression 0/25/50/75/100 %% monotonic\n");
    {
        auto mainL = sine (n, 440.0, sr, 0.5f);
        auto mainR = mainL;

        double prevSettledDb = 0.0;
        bool monotonic = true;
        std::string detail;

        for (float amount : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
        {
            auto r = run (sr, 512, amount, bpm, mainL, mainR);
            float minG = 1.0f;
            for (float g : r.gainCurve) minG = std::min (minG, g);
            const double settledDb = 20.0 * std::log10 (std::max ((double) minG, 1e-12));

            detail += "  " + std::to_string ((int) amount) + "%: " + std::to_string (settledDb).substr (0, 5) + " dB";
            if (settledDb > prevSettledDb + 0.01) monotonic = false;
            prevSettledDb = settledDb;
        }
        printf ("%s\n", detail.c_str());
        check (monotonic, "amount progression: ducking depth increases monotonically");
        check (prevSettledDb > -30.0 && prevSettledDb <= -20.0,
               "amount 100%: depth lands in the musical pump region (-24 dB)");
    }

    // ----------------------------------------------------------------------
    printf ("\nTEST 9: Extreme amounts / no NaN with beat triggers\n");
    {
        auto mainL = sine (n, 440.0, sr, 0.5f);
        auto mainR = mainL;
        auto r = run (sr, 512, 100.0f, 300.0, mainL, mainR); // very fast tempo

        check (! r.nanInfFound, "fast tempo: no NaN/Inf");
        check (r.gainCurve.back() >= 0.0f && r.gainCurve.back() <= 1.0f,
               "fast tempo: gain stays in [0,1]");
        float minG = 1.0f;
        for (float g : r.gainCurve) minG = std::min (minG, g);
        check (minG > 0.0f, "fast tempo: gain never collapses to exactly zero");
    }

    // ----------------------------------------------------------------------
    printf ("\nBLOCK SIZE STABILITY (32..1024, amount 100%%, 120 BPM)\n");
    {
        auto mainL = sine (n, 440.0, sr, 0.5f);
        auto mainR = mainL;

        double refSettled = -1.0;
        bool stable = true, finite = true;
        for (int bs : { 32, 64, 128, 256, 512, 1024 })
        {
            auto r = run (sr, bs, 100.0f, bpm, mainL, mainR);
            if (r.nanInfFound) finite = false;
            double sum = 0; int count = 0;
            for (int i = n - (int)(0.1 * sr); i < n; ++i) { sum += r.gainCurve[(size_t) i]; ++count; }
            const double settled = sum / count;
            if (refSettled < 0) refSettled = settled;
            else if (std::abs (settled - refSettled) > 0.01) stable = false;
        }
        check (finite, "block sizes: no NaN/Inf at any size");
        check (stable, "block sizes: response is block-size independent (32..1024)");
    }

    // ----------------------------------------------------------------------
    printf ("\nSAMPLE RATE STABILITY (44100/48000/88200/96000)\n");
    {
        bool allStable = true, allFinite = true;
        std::string detail;

        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            const int len = (int) rate; // 1 s
            auto mainL = sine (len, 440.0, rate, 0.5f);
            auto mainR = mainL;
            auto r = run (rate, 512, 100.0f, bpm, mainL, mainR);
            if (r.nanInfFound) allFinite = false;

            double sum = 0; int count = 0;
            for (int i = len - (int)(0.1 * rate); i < len; ++i) { sum += r.gainCurve[(size_t) i]; ++count; }
            const double settledDb = 20.0 * std::log10 (std::max (sum / count, 1e-12));
            detail += "  " + std::to_string ((int) rate) + "Hz: " + std::to_string (settledDb).substr (0, 5) + " dB";
            float minG = 1.0f;
            for (float g : r.gainCurve) minG = std::min (minG, g);
            if (minG > 0.9f || minG < 0.001f) allStable = false;
        }
        printf ("%s\n", detail.c_str());
        check (allFinite, "sample rates: no NaN/Inf");
        check (allStable, "sample rates: consistent beat-driven ducking (coefficients rate-aware)");
    }

    // ----------------------------------------------------------------------
    printf ("\nAMOUNT=0 TRANSPARENCY over all block sizes\n");
    {
        auto mainL = sine (n, 440.0, sr, 0.5f);
        auto mainR = sine (n, 660.0, sr, 0.5f);
        bool allUnity = true;
        for (int bs : { 32, 64, 128, 256, 512, 1024 })
        {
            auto r = run (sr, bs, 0.0f, bpm, mainL, mainR);
            for (float g : r.gainCurve) if (g != 1.0f) { allUnity = false; break; }
        }
        check (allUnity, "Amount=0: gain is exactly 1.0 at every sample, every block size");
    }

    // ----------------------------------------------------------------------
    printf ("\n======================================\n");
    printf ("%d/%d checks passed. %s\n", totalChecks - failures, totalChecks,
            failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
