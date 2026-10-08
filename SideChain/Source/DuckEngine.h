/*
    SideChain - DuckEngine v3 (0.4.0 internal tempo-synced trigger).

    PRODUCT MODEL (0.4.0 - the authoritative behaviour)
    ---------------------------------------------------
    SideChainer is a TEMPO-SYNCHRONISED rhythmic ducking effect. It needs
    NO external sidechain audio. The DAW transport (PPQ position + BPM)
    drives an internal beat scheduler that fires ONE duck envelope on
    every quarter-note beat:

        DAW beat -> internal trigger -> duck envelope -> main signal ducks
                 -> recovery -> next beat -> next duck

    WHY THE EXTERNAL DETECTOR WAS REMOVED
    -------------------------------------
    Versions up to 0.3.x analysed an external sidechain signal with a
    transient/onset detector (fast/slow followers, threshold, rise, slew,
    hysteresis). That architecture required the user to route a kick track
    into an auxiliary input - NOT the intended product. The detector code
    was genuinely unused once triggers come from the host timeline and has
    been removed (the full 0.3.0 detector remains in version control
    history). The ENVELOPE machinery below is the validated 0.3.0 engine,
    unchanged: attack -> hold -> exponential release, with the DUCK
    LENGTH / Release-as-shape mapping, lookahead + offset delay line and
    the musical depth curve.

    Signal chain (all per-sample, audio-thread safe, no allocation):

        BEAT SCHEDULER (processor; host PPQ/BPM, sample-accurate)
          -> fireTrigger() at the exact sample of each quarter note
          -> ENVELOPE GENERATOR (attack -> hold -> exponential release)
          -> ENVELOPE DELAY LINE (lookahead + user offset, always >= 0)
          -> one shared gain applied identically to MAIN L and R (the main
             path is delayed by the same amount via the processor's
             lookahead ring buffer, so envelope and audio stay in sync)

    Envelope timing (DUCK LENGTH model, unchanged from validated 0.3.0):
      - DUCK LENGTH (user parameter, ms) is the TOTAL audible duration of
        one duck: attack + hold + 3 release taus ~= duckLengthMs (a 3-tau
        exponential recovery is ~95 % complete - the ear hears it done).
      - RELEASE (user parameter, ms) is the envelope SHAPE / character: it
        sets how much of that total length is the held plateau versus the
        exponential tail. hold = holdFraction(release) * duckLength,
        tau = (duckLength - hold) / 3.
      With no trigger the gain is exactly 1.0 - no reduction at all.

    Depth: musical pumping range, 100 % = -24 dB. See amountToDepthDb().

    Offset: OFFSET -30..+30 ms (1 ms UI steps). The envelope output is
    delayed by (lookahead + offset) samples - always >= 0 since offset >=
    -lookaheadMs. The processor delays the MAIN path by exactly the same
    amount and reports that latency to the host, so a negative offset
    genuinely ducks EARLIER than the beat.
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>

namespace sid::dsp
{

class DuckEngine
{
public:
    //======================================================================
    // ---- envelope tuning ----
    static constexpr float kEnvAttackMs   = 1.5f;

    // ---- DUCK LENGTH parameter range: total duck duration ----
    static constexpr float kDuckLengthMinMs    = 50.0f;
    static constexpr float kDuckLengthMaxMs    = 1000.0f;
    static constexpr float kDuckLengthDefaultMs = 250.0f;   // Classic Kick feel

    // Release-as-shape: hold fraction of the total length. Linear in the
    // Release parameter: 50 ms -> 0.05 (tight plateau, long tail),
    // 1000 ms -> 0.50 (long plateau, shorter tail).
    static constexpr float kHoldFracMin = 0.05f;
    static constexpr float kHoldFracMax = 0.50f;

    // ---- depth (musical pumping range) ----
    static constexpr float kMaxDuckingDb   = -24.0f;
    static constexpr float kAmountCurveExp = 1.6f;

    // ---- Release (shape) parameter range ----
    static constexpr float kReleaseMinMs    = 50.0f;
    static constexpr float kReleaseMaxMs    = 1000.0f;
    static constexpr float kReleaseDefaultMs = 150.0f;

    // ---- offset ----
    static constexpr int kOffsetStepMs = 1;
    static constexpr int kOffsetMinMs  = -30;
    static constexpr int kOffsetMaxMs  =  30;

    static int clampOffsetMs (int offsetMs) noexcept
    {
        return juce::jlimit (kOffsetMinMs, kOffsetMaxMs, offsetMs);
    }

    // Lookahead the processor must delay the MAIN path by to support the
    // most negative offset. Also the plugin's reported latency.
    static int lookaheadMs() noexcept  { return juce::jmax (0, -kOffsetMinMs); }
    static int lookaheadSamples (double sampleRate) noexcept
    {
        return (int) std::lround (lookaheadMs() * sampleRate / 1000.0);
    }

    //======================================================================
    void prepare (double sampleRate)
    {
        jassert (sampleRate > 0.0);
        sampleRate_ = sampleRate;
        updateCoefficients();
        reset();
    }

    void reset()
    {
        envelopePhase_  = Phase::idle;
        envInstantGain_ = 1.0f;
        holdCounter_    = 0;

        absoluteSample_      = 0;
        lastTriggerSample_   = -1;
        triggerCount_        = 0;

        envDelay_.assign ((size_t) totalEnvDelaySamples() + 1, 1.0f);
        envDelayPos_ = 0;
        envDelayedGain_ = 1.0f;
    }

    //======================================================================
    static float amountToDepthDb (float amount01)
    {
        amount01 = juce::jlimit (0.0f, 1.0f, amount01);
        if (amount01 <= 0.0f)
            return 0.0f;                       // Amount = 0 -> strictly unity

        // Musical pumping curve across 0..-24 dB: 25 % -> ~-4.4 dB,
        // 50 % -> ~-8.7 dB, 75 % -> ~-14.9 dB, 100 % -> -24 dB.
        return kMaxDuckingDb * std::pow (amount01, kAmountCurveExp);
    }

    //======================================================================
    void setOffsetMs (int offsetMs) noexcept
    {
        const int clamped = clampOffsetMs (offsetMs);
        if (clamped != offsetMs_)
        {
            offsetMs_ = clamped;
            rebuildDelay();
        }
    }
    int getOffsetMs() const noexcept { return offsetMs_; }

    // User-facing Release (ms): envelope SHAPE (plateau vs tail split).
    // The total duration is DUCK LENGTH (setDuckLengthMs below), NOT this.
    void setReleaseTimes (float /*legacyDetector*/, float envelopeReleaseMs) noexcept
    {
        releaseMs_ = juce::jlimit (kReleaseMinMs, kReleaseMaxMs, envelopeReleaseMs);
        updateCoefficients();
    }
    float getReleaseMs() const noexcept { return releaseMs_; }

    // User-facing DUCK LENGTH (ms): TOTAL audible duck duration
    // (attack + hold + 3 release taus).
    void setDuckLengthMs (float duckLengthMs) noexcept
    {
        duckLengthMs_ = juce::jlimit (kDuckLengthMinMs, kDuckLengthMaxMs, duckLengthMs);
        updateCoefficients();
    }
    float getDuckLengthMs() const noexcept { return duckLengthMs_; }

    // Hold fraction of the total length for a given Release (shape) value.
    // Public so the graph preview and tests share the exact engine mapping.
    static float holdFractionForRelease (float releaseMs) noexcept
    {
        const float r = juce::jlimit (kReleaseMinMs, kReleaseMaxMs, releaseMs);
        return kHoldFracMin
             + (r - kReleaseMinMs) * (kHoldFracMax - kHoldFracMin)
                                      / (kReleaseMaxMs - kReleaseMinMs);
    }

    // Offset changes allocate-free swap of the ring length.
    void setLatencyCompensationWindowSize (int maxLookaheadSamples)
    {
        if (maxLookaheadSamples > maxDelayCapacity_)
        {
            maxDelayCapacity_ = maxLookaheadSamples;
            rebuildDelay();
        }
    }

    //======================================================================
    // INTERNAL TRIGGER: start ONE duck envelope NOW (this sample). Called
    // by the processor's beat scheduler at the exact sample of each
    // quarter-note beat. Re-triggering while an envelope is running simply
    // restarts the attack (musical: every beat pumps).
    void fireTrigger() noexcept
    {
        envelopePhase_     = Phase::attack;
        lastTriggerSample_ = absoluteSample_;
        ++triggerCount_;
    }

    bool  isEnvelopeActive() const noexcept { return envDelayedGain_ < 0.9999f; }
    int   getTriggerCount() const noexcept  { return triggerCount_; }
    long long getLastTriggerSample() const noexcept { return lastTriggerSample_; }

    //======================================================================
    // Process one sample of the ENVELOPE timeline (no audio input - the
    // trigger comes from the host timeline via fireTrigger()). Returns the
    // gain for BOTH main channels at THIS output sample.
    inline float processSample (float depthDb)
    {
        ++absoluteSample_;

        advanceEnvelope (depthDb);

        // Envelope delay line: delay the envelope by (lookahead + offset)
        // samples - the same delay the processor applies to the main audio.
        // Negative offset therefore pulls the duck EARLIER than the beat on
        // the delayed (host-compensated) timeline.
        envDelay_[(size_t) envDelayPos_] = envInstantGain_;
        envDelayedGain_ = envDelay_[(size_t) ((envDelayPos_ + 1) % envDelay_.size())];
        envDelayPos_ = (envDelayPos_ + 1) % (int) envDelay_.size();

        return envDelayedGain_;
    }

    float getCurrentGain() const noexcept        { return envDelayedGain_; }
    float getCurrentReductionDb() const noexcept
    {
        return juce::Decibels::gainToDecibels (envDelayedGain_, -120.0f);
    }
    float getDerivedHoldMs() const noexcept
    {
        return (float) holdSamples_ * 1000.0f / (float) sampleRate_;
    }
    float getDerivedReleaseTauMs() const noexcept { return releaseTauMs_; }

private:
    enum class Phase { idle, attack, hold, release };

    int totalEnvDelaySamples() const
    {
        // lookahead (for negative offset) + positive offset, whole ms.
        const int frac = (int) std::lround (sampleRate_ / 1000.0); // samples per ms
        return juce::jmax (0, lookaheadSamples (sampleRate_) + offsetMs_ * frac);
    }

    void rebuildDelay()
    {
        const size_t newSize = (size_t) totalEnvDelaySamples() + 1;
        const size_t oldSize = envDelay_.size();
        envDelay_.resize (newSize, 1.0f);
        if (newSize > oldSize)
            std::fill (envDelay_.begin() + (long) oldSize, envDelay_.end(), 1.0f);
        if (envDelayPos_ >= (int) envDelay_.size())
            envDelayPos_ = 0;
    }

    void updateCoefficients()
    {
        envAttackCoeff_  = timeToCoefficient (kEnvAttackMs);

        // DUCK LENGTH model: hold = f(release) * duckLength, and the
        // release tau takes the remainder so that hold + 3*tau ~= the
        // total audible duck length.
        const float holdMs = holdFractionForRelease (releaseMs_) * duckLengthMs_;
        const float tauMs  = juce::jmax (5.0f, (duckLengthMs_ - holdMs) / 3.0f);
        envReleaseCoeff_ = timeToCoefficient (tauMs);
        releaseTauMs_    = tauMs;
        holdSamples_     = (long long) std::lround (holdMs * sampleRate_ / 1000.0);
    }

    void advanceEnvelope (float depthDb)
    {
        switch (envelopePhase_)
        {
            case Phase::idle:
                envInstantGain_ = 1.0f;
                break;

            case Phase::attack:
            {
                // depthDb is already <= 0 dB (0 at Amount 0, -24 at max).
                const float target = juce::Decibels::decibelsToGain (
                    juce::jmin (0.0f, depthDb));
                envInstantGain_ += envAttackCoeff_ * (target - envInstantGain_);
                if (envInstantGain_ - target < 1.0e-4f)
                {
                    envInstantGain_ = target;
                    envelopePhase_  = Phase::hold;
                    holdCounter_    = 0;
                }
                break;
            }

            case Phase::hold:
                envInstantGain_ = juce::Decibels::decibelsToGain (
                    juce::jmin (0.0f, depthDb));
                if (++holdCounter_ >= holdSamples_)
                    envelopePhase_ = Phase::release;
                break;

            case Phase::release:
                envInstantGain_ += envReleaseCoeff_ * (1.0f - envInstantGain_);
                if (1.0f - envInstantGain_ < 1.0e-4f)
                {
                    envInstantGain_ = 1.0f;
                    envelopePhase_  = Phase::idle;
                }
                break;
        }
    }

    float timeToCoefficient (float ms) const
    {
        const double tauSamples = juce::jmax (1.0, (double) ms * sampleRate_ / 1000.0);
        return (float) (1.0 - std::exp (-1.0 / tauSamples));
    }

    //======================================================================
    double sampleRate_ = 48000.0;
    float  releaseMs_     = kReleaseDefaultMs;
    float  duckLengthMs_  = kDuckLengthDefaultMs;
    float  releaseTauMs_  = kReleaseDefaultMs;   // diagnostic: derived tau
    int    offsetMs_   = 0;

    // trigger state
    long long absoluteSample_ = 0;
    long long lastTriggerSample_ = -1;
    int    triggerCount_ = 0;

    // envelope state (real-time timeline)
    Phase  envelopePhase_  = Phase::idle;
    float  envInstantGain_ = 1.0f;
    long long holdCounter_ = 0;
    long long holdSamples_ = 0;

    // envelope delay (shared output timeline)
    std::vector<float> envDelay_ { 1.0f, 1.0f };
    int    envDelayPos_    = 0;
    float  envDelayedGain_ = 1.0f;
    int    maxDelayCapacity_ = 0;

    // coefficients
    float envAttackCoeff_  = 0.0f;
    float envReleaseCoeff_ = 0.0f;
};

} // namespace sid::dsp
