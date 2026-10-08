/*
    SideChain - real-time graph data transport (Phase 4).

    Audio thread -> GUI communication:

        audio thread (single producer)
            pushes one pre-aggregated GraphFrame per audio BLOCK
            into a lock-free single-producer/single-consumer FIFO
                ->
        GUI timer (~30 Hz, single consumer)
            drains the FIFO into a rolling history
            and repaints the graph

    Properties:
      - no locks, no allocations on the audio thread (fixed-capacity FIFO,
        constructed once)
      - no GUI calls from the audio thread
      - pre-aggregated block levels only (never raw samples)
      - all fields are plain floats; benign value tearing is impossible
        because a whole frame is written before being published

    The FIFO is `juce::AbstractFifo`-free on purpose: a simple power-of-two
    SPSC ring with atomic read/write indices is smaller and fully adequate.
    When full, the oldest frame is silently dropped (GUI never blocks the
    audio thread); pop() therefore always terminates at the write index.
*/

#pragma once

#include <JuceHeader.h>

namespace sid::graph
{

struct GraphFrame
{
    float inputLevelDb     = -100.0f; // main input block peak, dBFS
    float sidechainLevelDb = -100.0f; // analyzer trace (== input since 0.4.0)
    float gainReductionDb  = 0.0f;    // <= 0 (0 dB = no ducking)
    float outputLevelDb    = -100.0f; // main output block peak, dBFS
    bool  triggerFired     = false;   // 0.4.0: an internal BEAT triggered this block
    float bpm              = 0.0f;    // 0.4.0: host tempo for the beat-aware display
};

class GraphFrameFifo
{
public:
    static constexpr int kCapacity = 1024; // power of two; ~21 s of 512-frames

    void push (const GraphFrame& f) noexcept
    {
        const auto writeIndex = writeIndex_.load (std::memory_order_relaxed);
        const auto readIndex  = readIndex_.load (std::memory_order_acquire);

        // Full? Drop the OLDEST frame so the consumer can always drain to
        // empty; this keeps (writeIndex - readIndex) <= kCapacity and makes
        // pop() well-defined even after the ring has wrapped.
        if (writeIndex - readIndex == (uint64_t) kCapacity)
            readIndex_.store (readIndex + 1, std::memory_order_release);

        slots_[writeIndex & (kCapacity - 1)] = f;
        writeIndex_.store (writeIndex + 1, std::memory_order_release);
    }

    bool pop (GraphFrame& out) noexcept
    {
        const auto readIndex = readIndex_.load (std::memory_order_relaxed);
        const auto writeIndex = writeIndex_.load (std::memory_order_acquire);

        if (readIndex == writeIndex)
            return false;

        out = slots_[readIndex & (kCapacity - 1)];
        readIndex_.store (readIndex + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<GraphFrame, kCapacity> slots_ {};
    std::atomic<uint64_t> writeIndex_ { 0 };
    std::atomic<uint64_t> readIndex_  { 0 };
};

} // namespace sid::graph
