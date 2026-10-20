/*
    PumpCurve - live waveform scope transport (0.4.2).

    Audio thread -> GUI communication for the LIVE waveform layer drawn
    behind the PumpCurve curve:

        audio thread (single producer)
            per block: split the block into up to kFramesPerBlock equal
            chunks and push ONE min/max frame per chunk (input + processed
            output) into a lock-free single-producer/single-consumer FIFO
                ->
        GUI timer (~30 Hz, single consumer)
            drains the FIFO into a rolling history and repaints the graph

    Guarantees (identical shape to GraphData.h's graph transport):
      - no locks, no allocations, no GUI calls on the audio thread
        (fixed-capacity ring, constructed once)
      - min/max envelopes only, never raw sample arrays
      - every value that enters a frame is finite and clamped to +/- full
        scale, so a NaN, an Inf or an over-hot (clipping) block can never
        corrupt the drawing maths or draw outside the plot
      - the consumer can tell a stalled producer from silence, so a stale
        trace is never presented as if it were current audio

    Deliberately independent of the ducking DSP and of the transport gate:
    the scope draws the REAL main input and the REAL processed output in
    every transport state, and never feeds anything back into the audio
    path.
*/

#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sid::scope
{

// How many scope frames one audio block is split into. Four keeps the trace
// responsive (~1.3 s window at 30 Hz GUI refresh, 512-sample blocks at 48 kHz)
// for a cost of a few comparisons per sample.
constexpr int kFramesPerBlock = 4;

// Display full scale. Samples outside [-1, 1] are clipped to the plot edge
// (deterministic and documented) rather than resizing the trace, which is what
// "prevent clipping from corrupting the visualization" means here.
constexpr float kFullScale = 1.0f;

// Below this magnitude a chunk counts as silence (-80 dBFS).
constexpr float kSilenceFloor = 1.0e-4f;

// A chunk seed that no real sample can produce, so an untouched accumulator
// can never be mistaken for measured audio.
constexpr float kAccumulatorSeed = 2.0f;

// NaN/Inf -> 0, then clamp to +/- full scale. Applied on the audio thread to
// every value that enters a frame.
inline float sanitiseSample (float value) noexcept
{
    if (value != value)                    // NaN
        return 0.0f;
    if (value > kFullScale)
        return kFullScale;
    if (value < -kFullScale)
        return -kFullScale;
    return value;
}

// One decimated slice: the min/max envelope of the signal over the chunk.
//
// The two channels are unioned (min of both minimums, max of both maximums)
// instead of being summed to mono: an anti-phase stereo pair would cancel
// itself into a misleading flat line, and the envelope of what is actually
// playing is what the user needs to compare before/after modulation.
struct WaveformFrame
{
    float inputMin  = 0.0f;   // main input (the signal the gain is applied to)
    float inputMax  = 0.0f;
    float outputMin = 0.0f;   // processed output = what is heard
    float outputMax = 0.0f;
};

// Single-producer/single-consumer ring, pre-allocated at construction time.
// Capacity 2048 frames: at 4 frames per 512-sample block and 48 kHz that is
// ~5.4 s of audio, far above one 30 Hz GUI drain interval. When full, the
// oldest frame is dropped so the consumer can always drain to empty and the
// audio thread never blocks.
class WaveformFifo
{
public:
    static constexpr int kCapacity = 2048; // power of two

    void push (const WaveformFrame& frame) noexcept
    {
        const auto writeIndex = writeIndex_.load (std::memory_order_relaxed);
        const auto readIndex  = readIndex_.load (std::memory_order_acquire);

        if (writeIndex - readIndex == (std::uint64_t) kCapacity)
            readIndex_.store (readIndex + 1, std::memory_order_release);

        slots_[writeIndex & (std::uint64_t) (kCapacity - 1)] = frame;
        writeIndex_.store (writeIndex + 1, std::memory_order_release);
    }

    bool pop (WaveformFrame& out) noexcept
    {
        const auto readIndex  = readIndex_.load (std::memory_order_relaxed);
        const auto writeIndex = writeIndex_.load (std::memory_order_acquire);

        if (readIndex == writeIndex)
            return false;

        out = slots_[readIndex & (std::uint64_t) (kCapacity - 1)];
        readIndex_.store (readIndex + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<WaveformFrame, kCapacity> slots_ {};
    std::atomic<std::uint64_t> writeIndex_ { 0 };
    std::atomic<std::uint64_t> readIndex_  { 0 };
};

// GUI-side rolling history. Plain data and arithmetic only (no drawing, no
// JUCE dependency), so the decimation, the silence detection and the
// stalled-producer detection are all directly testable headlessly.
class WaveformHistory
{
public:
    static constexpr int kColumns = 512;              // plot resolution
    static constexpr std::int64_t kStaleAfterMs = 250; // producer stall window

    void clear() noexcept
    {
        for (int i = 0; i < kColumns; ++i)
        {
            inputMin_[i] = inputMax_[i] = 0.0f;
            outputMin_[i] = outputMax_[i] = 0.0f;
        }
        writePos_ = 0;
        count_ = 0;
        lastFrameMs_ = 0;
    }

    // Append one frame (newest column).
    void push (const WaveformFrame& frame) noexcept
    {
        inputMin_[(std::size_t) writePos_]  = sanitiseSample (frame.inputMin);
        inputMax_[(std::size_t) writePos_]  = sanitiseSample (frame.inputMax);
        outputMin_[(std::size_t) writePos_] = sanitiseSample (frame.outputMin);
        outputMax_[(std::size_t) writePos_] = sanitiseSample (frame.outputMax);
        writePos_ = (writePos_ + 1) % kColumns;
        if (count_ < kColumns)
            ++count_;
    }

    // Drain every available frame. nowMs is the consumer's clock, used only to
    // detect a stalled producer. Returns how many frames were appended.
    int consume (WaveformFifo& fifo, std::int64_t nowMs) noexcept
    {
        WaveformFrame frame;
        int appended = 0;
        while (fifo.pop (frame))
        {
            push (frame);
            ++appended;
        }
        if (appended > 0)
            lastFrameMs_ = nowMs;
        return appended;
    }

    int count() const noexcept { return count_; }

    // True while the producer is still delivering frames: false after a stall
    // of more than kStaleAfterMs, so a frozen trace degrades to the no-signal
    // fallback instead of pretending to be current audio.
    bool isLive (std::int64_t nowMs) const noexcept
    {
        return count_ > 0 && lastFrameMs_ != 0
            && nowMs >= lastFrameMs_ && nowMs - lastFrameMs_ <= kStaleAfterMs;
    }

    // True when any column in the valid window carries signal above the
    // silence floor.
    bool hasSignal() const noexcept
    {
        for (int i = 0; i < count_; ++i)
            if (std::abs (inputMin (i)) > kSilenceFloor
                || std::abs (inputMax (i)) > kSilenceFloor
                || std::abs (outputMin (i)) > kSilenceFloor
                || std::abs (outputMax (i)) > kSilenceFloor)
                return true;
        return false;
    }

    // What the painter should draw: live measurement with audible content.
    bool hasDisplayableSignal (std::int64_t nowMs) const noexcept
    {
        return isLive (nowMs) && hasSignal();
    }

    // Column 0 is the oldest retained column, count()-1 the newest.
    float inputMin (int column) const noexcept  { return inputMin_[(std::size_t) indexOf (column)]; }
    float inputMax (int column) const noexcept  { return inputMax_[(std::size_t) indexOf (column)]; }
    float outputMin (int column) const noexcept { return outputMin_[(std::size_t) indexOf (column)]; }
    float outputMax (int column) const noexcept { return outputMax_[(std::size_t) indexOf (column)]; }

private:
    int indexOf (int column) const noexcept
    {
        if (column < 0) column = 0;
        if (column >= count_) column = count_ > 0 ? count_ - 1 : 0;
        return (writePos_ - count_ + column + kColumns) % kColumns;
    }

    std::array<float, kColumns> inputMin_ {};
    std::array<float, kColumns> inputMax_ {};
    std::array<float, kColumns> outputMin_ {};
    std::array<float, kColumns> outputMax_ {};
    int writePos_ = 0;
    int count_ = 0;
    std::int64_t lastFrameMs_ = 0;
};

} // namespace sid::scope
