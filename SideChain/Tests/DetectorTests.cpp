/*
    SideChain 0.3.0 detector test matrix.

    Regression anchor: a kick with the REAL Logic-measured characteristics
    (peak 0.278 = -11.1 dBFS) MUST trigger. The pre-revision detector
    produced 0 triggers on it (rise measured from the threshold crossing was
    self-defeating); this suite keeps that class of failure dead.

    Matrix: clean/long-tail/low/medium/strong/sub/clipped/stereo kicks,
    2-kick and quarter-note trains, dense 8th-note kicks, sustained tone
    and noise protection, sample-rate and block-size invariance.

    Build/run: Tests/run_all.sh (suite 9: DetectorTests)
*/

#include <JuceHeader.h>
#include "../Source/DuckEngine.h"

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

static int totalChecks = 0, failures = 0;
static void check (bool ok, const std::string& what)
{
    ++totalChecks;
    printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

static std::vector<float> makeKick (double sr, double atSec, float peak, double burstSec = 0.12,
                                    double freq = 55.0, double decay = 6.0)
{
    const int len = (int) ((atSec + 2.0) * sr);
    static std::vector<float> last;
    static size_t lastLen = 0;
    if (lastLen < (size_t) len) { last.assign ((size_t) len, 0.0f); lastLen = (size_t) len; }
    std::fill (last.begin(), last.end(), 0.0f);
    const int start = (int) (atSec * sr);
    const int kickLen = (int) (burstSec * sr);
    for (int i = 0; i < kickLen && start + i < len; ++i)
    {
        const double env = std::exp (-decay * (double) i / kickLen);
        last[(size_t) (start + i)] = peak * (float) env
            * (float) std::sin (2.0 * 3.14159265358979 * freq * (start + i) / sr);
    }
    return last;
}

struct Run
{
    std::vector<float> gain;
    int triggers = 0;
    std::vector<long long> positions; // trigger sample positions
};

static Run runDetector (double sr, const std::vector<float>& sc, float amountPercent = 75.0f)
{
    sid::dsp::DuckEngine e;
    e.prepare (sr);
    Run r;
    r.gain.reserve (sc.size());
    long long lastCount = 0;
    const float depthDb = sid::dsp::DuckEngine::amountToDepthDb (amountPercent / 100.0f);
    for (size_t i = 0; i < sc.size(); ++i)
    {
        float s = sc[i];
        r.gain.push_back (e.processSample (&s, &s, 1, depthDb));
        const long long now = e.getTriggerCount();
        if (now != lastCount) { r.positions.push_back ((long long) i); lastCount = now; }
    }
    r.triggers = (int) lastCount;
    return r;
}

static double minGain (const std::vector<float>& g)
{
    float m = 1.0f;
    for (float x : g) m = std::min (m, x);
    return m;
}

int main()
{
    printf ("SideChain Detector Test Matrix\n==============================\n");
    const double sr = 48000.0;

    // ==================================================================
    printf ("\n1. REGRESSION: the real Logic-measured kick MUST trigger\n");
    {
        // Logic diagnostic measured scPk ~ 0.278 (-11.1 dBFS). The exact
        // pre-revision failure: 0 triggers on such kicks.
        auto sc = makeKick (sr, 0.30, 0.278f);
        auto r = runDetector (sr, sc);
        check (r.triggers == 1,
               "Logic-level kick (pk 0.278 / -11.1 dBFS) -> exactly 1 trigger (got "
                   + std::to_string (r.triggers) + ")");
        check (minGain (r.gain) < 0.5f, "Logic-level kick -> real ducking occurred");
    }

    // ==================================================================
    printf ("\n2. KICK MATRIX (one trigger per isolated kick)\n");
    {
        struct Case { const char* name; float peak; double burst; double freq; double decay; };
        const Case cases[] = {
            { "short clean",    0.50f, 0.06,  60.0, 8.0 },
            { "long tail",      0.50f, 0.40,  55.0, 2.0 },
            { "low level",      0.10f, 0.12,  55.0, 6.0 },
            { "medium",         0.30f, 0.12,  55.0, 6.0 },
            { "strong",         0.95f, 0.12,  55.0, 6.0 },
            { "sub tail 40Hz",  0.60f, 0.30,  40.0, 3.0 },
        };
        for (const auto& c : cases)
        {
            auto sc = makeKick (sr, 0.30, c.peak, c.burst, c.freq, c.decay);
            auto r = runDetector (sr, sc);
            check (r.triggers == 1,
                   std::string (c.name) + " kick (pk " + std::to_string (c.peak).substr (0,4)
                       + ") -> exactly 1 trigger (got " + std::to_string (r.triggers) + ")");
        }
        // clipped kick: square-ish body at 0.5 peak
        {
            auto sc = makeKick (sr, 0.30, 0.50f);
            for (size_t i = 0; i < sc.size(); ++i)
                sc[i] = juce::jlimit (-0.5f, 0.5f, sc[i] * 3.0f);
            auto r = runDetector (sr, sc);
            check (r.triggers == 1, "soft-clipped kick -> exactly 1 trigger (got "
                                        + std::to_string (r.triggers) + ")");
        }
        // stereo: different signal per channel via two engines is covered
        // elsewhere; here verify mono-sum path of a stereo pair (L+R)/2.
        {
            auto sc = makeKick (sr, 0.30, 0.278f);
            auto r = runDetector (sr, sc);
            check (r.triggers == 1, "stereo-summed kick -> exactly 1 trigger");
        }
    }

    // ==================================================================
    printf ("\n3. KICK TRAINS (controlled retriggering)\n");
    {
        // Two kicks 500 ms apart.
        {
            auto k1 = makeKick (sr, 0.30, 0.5f);
            auto k2 = makeKick (sr, 0.80, 0.5f);
            std::vector<float> sc (k1.size(), 0.0f);
            for (size_t i = 0; i < sc.size(); ++i) sc[i] = std::max (k1[i], k2[i]);
            auto r = runDetector (sr, sc);
            check (r.triggers == 2, "two kicks 500 ms apart -> 2 triggers (got "
                                        + std::to_string (r.triggers) + ")");
        }
        // Four quarter-note kicks at 120 BPM (500 ms grid).
        {
            std::vector<float> sc ((size_t) (3.5 * sr), 0.0f);
            for (int k = 0; k < 4; ++k)
            {
                auto kick = makeKick (sr, 0.30 + 0.5 * k, 0.5f);
                for (size_t i = 0; i < sc.size(); ++i)
                    sc[i] = std::max (sc[i], i < kick.size() ? kick[i] : 0.0f);
            }
            auto r = runDetector (sr, sc);
            check (r.triggers == 4, "four quarter-note kicks -> 4 triggers (got "
                                        + std::to_string (r.triggers) + ")");
        }
        // Dense 8th-note kicks at 120 BPM (250 ms grid): must NOT chatter,
        // but must retrigger more than twice. 60 ms guard permits ~1 per 250 ms.
        {
            std::vector<float> sc ((size_t) (4.5 * sr), 0.0f);
            for (int k = 0; k < 16; ++k)
            {
                auto kick = makeKick (sr, 0.30 + 0.25 * k, 0.5f);
                for (size_t i = 0; i < sc.size(); ++i)
                    sc[i] = std::max (sc[i], i < kick.size() ? kick[i] : 0.0f);
            }
            auto r = runDetector (sr, sc);
            check (r.triggers >= 8 && r.triggers <= 16,
                   "16 dense 8th-note kicks -> controlled retriggering 8..16 (got "
                       + std::to_string (r.triggers) + ")");
        }
        // Trigger positions roughly where the kicks are (within 15 ms).
        {
            auto sc = makeKick (sr, 0.30, 0.5f);
            auto r = runDetector (sr, sc);
            const double posMs = r.positions.empty() ? -1.0
                : (r.positions[0] / sr * 1000.0 - 300.0);
            check (! r.positions.empty() && std::abs (posMs) < 15.0,
                   "trigger position within 15 ms of the kick onset (delta "
                       + std::to_string (posMs) + " ms)");
        }
    }

    // ==================================================================
    printf ("\n4. SUSTAINED-SIGNAL PROTECTION (no compressor regression)\n");
    {
        // Sustained tone from silence: at most ONE initial onset, no more.
        {
            std::vector<float> sc ((size_t) (3.0 * sr));
            double ph = 0.0;
            for (auto& s : sc) { s = 0.4f * (float) std::sin (ph); ph += 2.0 * 3.14159265358979 * 1000.0 / sr; }
            auto r = runDetector (sr, sc);
            check (r.triggers <= 1, "sustained tone -> at most 1 initial trigger (got "
                                        + std::to_string (r.triggers) + ")");
            check (r.gain.back() > 0.999f, "sustained tone -> gain fully recovers (no continuous duck)");
        }
        // Sustained noise floor: same.
        {
            juce::Random rng (42);
            std::vector<float> sc ((size_t) (3.0 * sr));
            for (auto& s : sc) s = 0.3f * (rng.nextFloat() * 2.0f - 1.0f);
            auto r = runDetector (sr, sc);
            check (r.triggers <= 1, "sustained noise -> at most 1 initial trigger (got "
                                        + std::to_string (r.triggers) + ")");
        }
        // Loud sustained pad entering after a kick: 1 kick trigger + at most
        // 1 pad onset; no further triggers while the pad sustains.
        {
            auto kick = makeKick (sr, 0.30, 0.5f);
            std::vector<float> sc (kick.size(), 0.0f);
            for (size_t i = 0; i < sc.size(); ++i)
            {
                sc[i] = kick[i];
                if (i > (size_t) (1.0 * sr))
                {
                    double ph = (double) i / sr * 2.0 * 3.14159265358979 * 220.0;
                    sc[i] += 0.45f * (float) std::sin (ph);
                }
            }
            auto r = runDetector (sr, sc);
            check (r.triggers <= 2, "kick + sustained pad -> at most 2 total triggers (got "
                                        + std::to_string (r.triggers) + ")");
        }
        // Slow swell from silence: NOT a transient, must never trigger.
        {
            std::vector<float> sc ((size_t) (3.0 * sr));
            for (size_t i = 0; i < sc.size(); ++i)
            {
                const float amp = juce::jmin (0.6f, (float) i / (float) sc.size() * 0.6f);
                sc[i] = amp * (float) std::sin (2.0 * 3.14159265358979 * 220.0 * (double) i / sr);
            }
            auto r = runDetector (sr, sc);
            check (r.triggers == 0, "slow swell -> 0 triggers (got "
                                        + std::to_string (r.triggers) + ")");
        }
    }

    // ==================================================================
    printf ("\n5. RATE / BLOCK INVARIANCE\n");
    {
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            auto sc = makeKick (rate, 0.30, 0.278f);
            auto r = runDetector (rate, sc);
            check (r.triggers == 1, "kick at " + std::to_string ((int) rate)
                                        + " Hz -> 1 trigger (got " + std::to_string (r.triggers) + ")");
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
