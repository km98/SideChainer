/*
    PumpCurve - internal tempo-synced beat trigger scheduler (0.4.0;
    free-running mode added in 0.4.2).

    The product behaviour: the DAW transport's musical timeline (PPQ
    position + BPM from AudioPlayHead::CurrentPositionInfo) fires ONE duck
    envelope on EVERY QUARTER-NOTE BEAT, sample-accurately:

        DAW beat -> internal trigger -> duck envelope -> recovery -> ...

    Scheduling model (all in ABSOLUTE MUSICAL TIME = quarter-note PPQ):
      - `nextBeatPpq_` is the absolute PPQ of the next quarter-note beat.
      - `nextBeatHostSample_` is the host SAMPLE FRAME (monotonic, from
        CurrentPositionInfo.timeInSamples) at which that beat occurs.
      - For every beat inside the current block the trigger fires at the
        EXACT sample offset (multiple beats per block all fire, in order).

    FREE-RUNNING MODE (0.4.2, see advanceFreeRunning):
      When the user switches the transport gate OFF ("ACTIVE ONLY DURING
      PLAY/REC" = off) the effect must operate whether or not the transport
      rolls. A stopped DAW timeline cannot advance a beat grid, so this mode
      keeps its own sample clock and spaces beats by one quarter note at the
      current tempo (falling back to kFreeRunFallbackBpm when the host reports
      no usable tempo). It is a SEPARATE clock from the host-anchored
      schedule: entering it clears the host schedule, and the host-anchored
      path re-anchors from the playhead when it resumes, so neither mode can
      emit the other's stale beats.

    Robustness (required behaviours):
      - PLAY/RECORD: scheduling active. STOP: reset() is called; nothing
        is scheduled and no stale triggers survive a stop.
      - Transport jump / loop restart / reposition: the host reports a new
        timeInSamples; any discontinuity from the previous block end
        re-anchors the schedule from the CURRENT PPQ - no stale triggers.
      - Tempo change: each beat step converts PPQ -> samples using the
        CURRENT block BPM, so tempo changes resynchronize continuously.
      - BPM unavailable / invalid (<= 0): the block produces no triggers
        (honest fail-safe, no fake timing). Free-running mode uses the
        documented fallback tempo instead of inventing a grid from nothing.
      - PPQ unavailable but BPM valid: beats continue from the previous
        schedule when contiguous (tempo grid stays musical).

    Kept host-independent (plain values in/out) so it is fully
    deterministic-testable without a host.
*/

#pragma once

#include <JuceHeader.h>
#include <optional>
#include <cmath>

namespace sid::transport
{

class BeatScheduler
{
public:
    // Tempo used by the free-running clock when the host reports no usable
    // BPM. Documented product value (the factory presets are all quoted at
    // 120 BPM), never presented as host timing.
    static constexpr double kFreeRunFallbackBpm = 120.0;

    // The scheduler converts BPM -> host samples; the processor sets the
    // rate once in prepareToPlay.
    void setSampleRate (double sampleRate) noexcept { sampleRate_ = sampleRate; }

    // Advance the scheduler across one audio block.
    //
    //   blockStartHostSample : host sample frame of the first sample of
    //                          this block (CurrentPositionInfo.timeInSamples).
    //   numSamples           : block length in samples (> 0).
    //   bpm                  : current host tempo (must be > 0).
    //   ppqAtBlockStart      : musical position of the block start in
    //                          quarter-note PPQ (>= 0; < 0 = unavailable).
    //
    // For every quarter-note beat inside [blockStart, blockStart+numSamples)
    // onBeat is invoked with the EXACT sample offset from the block start.
    template <typename TriggerFn>
    void advance (long long blockStartHostSample, int numSamples,
                  double bpm, double ppqAtBlockStart, TriggerFn&& onBeat)
    {
        if (numSamples <= 0 || bpm <= 0.0 || sampleRate_ <= 0.0)
            return; // BPM unavailable / invalid -> fail safe: no triggers

        const bool discontiguous = lastBlockEndSample_.has_value()
            && blockStartHostSample != *lastBlockEndSample_;

        if (discontiguous || ! nextBeatHostSample_.has_value())
        {
            // First block, stop->play, or a transport jump/loop restart:
            // re-anchor the schedule from the host's CURRENT position. A
            // negative/unavailable PPQ cannot be anchored musically; skip
            // scheduling this block (fail-safe).
            reset();
            if (ppqAtBlockStart < 0.0)
            {
                lastBlockEndSample_ = blockStartHostSample + numSamples;
                return;
            }
            anchorAt (blockStartHostSample, bpm, ppqAtBlockStart);
        }
        // Contiguous block: schedule survives; tempo changes are absorbed
        // per beat inside emitBeats (each step uses the current bpm).

        lastBlockEndSample_ = blockStartHostSample + numSamples;

        // Emit every beat falling inside this block.
        while (nextBeatHostSample_ < blockStartHostSample + (long long) numSamples)
        {
            long long delta = *nextBeatHostSample_ - blockStartHostSample;
            delta = juce::jlimit ((long long) 0, (long long) numSamples - 1, delta);
            onBeat ((int) delta);

            // One quarter note in host samples at the CURRENT tempo.
            *nextBeatHostSample_ += (long long) std::llround (60.0 / bpm * sampleRate_);
            *nextBeatPpq_        += 1.0;
        }
    }

    // Call on prepareToPlay / transport stop: forget every schedule so the
    // next block re-anchors cleanly (no stale triggers).
    void reset()
    {
        resetHostSchedule();
        nextFreeRunBeat_.reset();
    }

    // Forget only the host-anchored schedule (used when the free-running clock
    // takes over, so a later resumption cannot emit a stale host beat).
    void resetHostSchedule()
    {
        nextBeatHostSample_.reset();
        nextBeatPpq_.reset();
        lastBlockEndSample_.reset();
    }

    // ------------------------------------------------------------------
    // Free-running mode: the transport gate is OFF, so the effect operates
    // regardless of the host transport. Beats are spaced one quarter note
    // apart at `bpm` (or kFreeRunFallbackBpm when that is unusable) on this
    // scheduler's own sample clock.
    //
    // The FIRST call arms the clock one whole beat ahead instead of firing
    // immediately: switching the gate off must not punch a trigger into the
    // middle of a bar (no abrupt discontinuity), it just resumes the grid from
    // the next beat.
    // ------------------------------------------------------------------
    template <typename TriggerFn>
    void advanceFreeRunning (int numSamples, double bpm, TriggerFn&& onBeat)
    {
        if (numSamples <= 0 || sampleRate_ <= 0.0)
            return;

        const double tempo = (std::isfinite (bpm) && bpm > 0.0)
                           ? bpm : kFreeRunFallbackBpm;
        const long long samplesPerQuarter = juce::jmax (
            (long long) 1,
            (long long) std::llround (60.0 / tempo * sampleRate_));

        if (! nextFreeRunBeat_.has_value())
            nextFreeRunBeat_ = samplesPerQuarter; // one beat of grace

        while (*nextFreeRunBeat_ < (long long) numSamples)
        {
            const long long offset = juce::jlimit ((long long) 0,
                                                   (long long) numSamples - 1,
                                                   *nextFreeRunBeat_);
            onBeat ((int) offset);
            *nextFreeRunBeat_ += samplesPerQuarter;
        }

        *nextFreeRunBeat_ -= numSamples;
    }

    // Test/diagnostic accessors.
    bool      hasSchedule() const noexcept      { return nextBeatHostSample_.has_value(); }
    long long nextBeatHostSample() const        { return *nextBeatHostSample_; }
    double    nextBeatPpq() const               { return *nextBeatPpq_; }
    bool      hasFreeRunSchedule() const noexcept { return nextFreeRunBeat_.has_value(); }
    long long nextFreeRunBeatSamples() const    { return *nextFreeRunBeat_; }

private:
    void anchorAt (long long blockStart, double bpm, double ppqAtBlockStart)
    {
        const double samplesPerQuarter = 60.0 / bpm * sampleRate_;

        // Fractional position within the current quarter note (0..1).
        const double beatFraction = ppqAtBlockStart - std::floor (ppqAtBlockStart);
        const double currentBeatPpq = std::floor (ppqAtBlockStart);

        // If this block begins exactly on a beat, fire it now. Otherwise,
        // schedule the NEXT integer-PPQ boundary: the remaining fraction,
        // not the already elapsed fraction. Keep the PPQ anchor in sync with
        // the host-sample anchor so subsequent one-beat increments agree.
        const bool startsOnBeat = beatFraction < 1.0e-9
                               || (1.0 - beatFraction) < 1.0e-9;
        const double samplesToNextBeat = startsOnBeat
            ? 0.0
            : (1.0 - beatFraction) * samplesPerQuarter;

        nextBeatHostSample_ = blockStart + (long long) std::llround (samplesToNextBeat);
        nextBeatPpq_        = startsOnBeat ? currentBeatPpq : currentBeatPpq + 1.0;
    }

    std::optional<long long> nextBeatHostSample_;
    std::optional<double>    nextBeatPpq_;
    std::optional<long long> lastBlockEndSample_;

    // Free-running clock (transport gate OFF): samples until the next beat,
    // relative to the current block start. Audio-thread owned plain integer,
    // no allocation, no host dependency.
    std::optional<long long> nextFreeRunBeat_;

    double sampleRate_ = 0.0;
};

} // namespace sid::transport
