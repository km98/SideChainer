/*
    SideChain - graph component implementation (Phase 4; 0.3.0 view modes).

    SIDECHAIN view (default): the actual ducking envelope only.
      - live: the gain-reduction history IS the envelope - one kick, one
        dip; recent cycles scroll naturally as new frames arrive; small
        tick marks mark the triggers. Offset shifts the envelope relative
        to the ticks because the DSP itself is shifted.
      - stopped/silent: a labelled PREVIEW of the current shape, derived
        from Amount (depth), Duck Length (width of one cycle) and Release
        (plateau/tail proportion). Subtle, elegant, clearly marked.

    ANALYZER view: the original Phase 4 multi-trace view (input,
    sidechain, duck fill + ticks, output) - preserved unchanged.

    Visual concept (dark, minimal, Music-Prod direction):
      - background: near-black panel with a subtle grid
      - scale  : dB, top = 0 dBFS, bottom = -60 dB (fixed, honest scale)
*/

#include "GraphComponent.h"
#include "DuckEngine.h"

namespace
{
    constexpr float kTopDb    =   0.0f;
    constexpr float kBottomDb = -60.0f;

    float dbToY (float db, float height)
    {
        // Map dBFS range to y (0 dB at top, -60 at bottom).
        const float norm = juce::jlimit (0.0f, 1.0f,
                                         (db - kBottomDb) / (kTopDb - kBottomDb));
        return (1.0f - norm) * height;
    }

    // -100 dB frames (silence) map to the very bottom of the level area.
    float levelDbToY (float db, float height)
    {
        return dbToY (juce::jlimit (kBottomDb, kTopDb, db), height);
    }
}

//==============================================================================
GraphComponent::GraphComponent()
{
    setOpaque (true);
    resetHistory();
}

void GraphComponent::resetHistory()
{
    inputHist_.fill (-100.0f);
    sidechainHist_.fill (-100.0f);
    reductionHist_.fill (0.0f);
    outputHist_.fill (-100.0f);
    writePos_ = 0;
    filled_ = false;
    repaint();
}

void GraphComponent::consumeFrames (sid::graph::GraphFrameFifo& fifo)
{
    sid::graph::GraphFrame f;
    bool gotAny = false;

    while (fifo.pop (f))
    {
        pushFrame (f);
        gotAny = true;
    }

    if (gotAny)
        repaint();
}

void GraphComponent::pushFrame (const sid::graph::GraphFrame& f)
{
    inputHist_[(size_t) writePos_]     = f.inputLevelDb;
    sidechainHist_[(size_t) writePos_] = f.sidechainLevelDb;
    reductionHist_[(size_t) writePos_] = -f.gainReductionDb; // positive depth
    outputHist_[(size_t) writePos_]    = f.outputLevelDb;
    triggerHist_[(size_t) writePos_]   = f.triggerFired;
    if (f.bpm > 0.0f)
        liveBpm_ = f.bpm;

    writePos_ = (writePos_ + 1) % kHistorySize;
    if (writePos_ == 0)
        filled_ = true;
}

//==============================================================================
// No-signal preview
//==============================================================================

void GraphComponent::setPreviewParams (float amountPercent, float releaseMs,
                                       float duckLengthMs)
{
    amountPercent = juce::jlimit (0.0f, 100.0f, amountPercent);
    releaseMs     = juce::jlimit (1.0f, 5000.0f, releaseMs);
    duckLengthMs  = juce::jlimit (1.0f, 5000.0f, duckLengthMs);

    if (std::abs (amountPercent - previewAmountPercent_) > 0.05f
        || std::abs (releaseMs - previewReleaseMs_) > 0.05f
        || std::abs (duckLengthMs - previewDuckLengthMs_) > 0.05f)
    {
        previewAmountPercent_ = amountPercent;
        previewReleaseMs_     = releaseMs;
        previewDuckLengthMs_  = duckLengthMs;
        if (historyIsSilent())          // only matters while no live data
            repaint();
    }
}

float GraphComponent::settledDepthDb (float amountPercent)
{
    // Mirrors the v0.3.0 DuckEngine mapping: Amount 0..100 % -> 0..-24 dB
    // with a slightly exponential curve (kAmountCurveExp = 1.6).
    const auto norm = juce::jlimit (0.0f, 1.0f, amountPercent / 100.0f);
    return 24.0f * std::pow (norm, 1.6f);
}

bool GraphComponent::historyIsSilent() const
{
    // Any real frame leaves a trace above the silence floor (-100 dB):
    // input, sidechain, or output. A never-fed (or fully silent) history
    // keeps the preview visible.
    const int count = filled_ ? kHistorySize : writePos_;
    for (int i = 0; i < count; ++i)
    {
        const auto idx = (size_t) ((writePos_ - count + i + kHistorySize) % kHistorySize);
        if (inputHist_[idx] > -95.0f || sidechainHist_[idx] > -95.0f
            || outputHist_[idx] > -95.0f)
            return false;
    }
    return true;
}

//==============================================================================
void GraphComponent::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    // Panel background (same pure black field as the rest of the plugin;
    // the edge rect drawn by the editor delineates the plot area).
    g.fillAll (juce::Colour (0xff000000));

    const float labelStrip = 18.0f;
    auto plot = area.reduced (10.0f);
    plot.removeFromTop (labelStrip * 0.5f);

    const float w = plot.getWidth();
    const float h = plot.getHeight();
    const float x0 = plot.getX();
    const float y0 = plot.getY();

    // Subtle grid at 12 dB intervals
    g.setColour (juce::Colour (0xff232833));
    for (float db = kTopDb - 12.0f; db > kBottomDb; db -= 12.0f)
        g.drawHorizontalLine ((int) (y0 + dbToY (db, h)), x0, x0 + w);

    g.setColour (juce::Colour (0xff2c3342));
    g.drawHorizontalLine ((int) y0, x0, x0 + w); // 0 dB reference

    auto sampleAt = [this] (const std::array<float, kHistorySize>& hist, float frac) -> float
    {
        const int count = filled_ ? kHistorySize : writePos_;
        if (count <= 1) return -100.0f;
        const float pos = frac * (float) (count - 1);
        const int i0 = (int) pos;
        const int i1 = (i0 + 1) % kHistorySize;
        const float t = pos - (float) i0;

        // History is stored oldest -> newest ending at writePos_; read with
        // wraparound interpolation so the newest sample is at the right edge.
        const int idx0 = (writePos_ - count + i0 + kHistorySize) % kHistorySize;
        const int idx1 = (writePos_ - count + i1 + kHistorySize) % kHistorySize;
        return hist[(size_t) idx0] * (1.0f - t) + hist[(size_t) idx1] * t;
    };

    auto buildPath = [&] (const std::array<float, kHistorySize>& hist)
    {
        juce::Path p;
        const int count = filled_ ? kHistorySize : writePos_;
        if (count < 2) return p;

        for (int i = 0; i < count; ++i)
        {
            const float frac = (float) i / (float) (count - 1);
            const float v = sampleAt (hist, frac);
            const float y = y0 + levelDbToY (v, h);

            if (i == 0) p.startNewSubPath (x0 + frac * w, y);
            else        p.lineTo (x0 + frac * w, y);
        }
        return p;
    };

    auto drawTriggerTicks = [&] (int count)
    {
        g.setColour (juce::Colour (0x88e8ecf3));
        for (int i = 0; i < count; ++i)
        {
            if (! triggerHist_[(size_t) ((writePos_ - count + i + kHistorySize) % kHistorySize)])
                continue;
            const float frac = (float) i / (float) (count - 1);
            const float xTick = x0 + frac * w;
            g.drawVerticalLine ((int) xTick, y0 - 4.0f, y0 + 4.0f);
        }
    };

    // ---- NO-SIGNAL PREVIEW: current-parameter ducking shape --------------
    // Drawn first (lowest layer). When live data exists the live traces
    // paint over it, so it never competes with real measurements. It is a
    // visual preview of the current settings, clearly marked "PREVIEW".
    if (historyIsSilent())
    {
        const float depthDb = settledDepthDb (previewAmountPercent_);

        if (depthDb < 0.5f)
        {
            // 0 % Amount: essentially flat / no ducking - show the baseline.
            g.setColour (juce::Colour (0x28a8b3c7));
            g.drawHorizontalLine ((int) y0, x0, x0 + w);
        }
        else
        {
            // One full duck-and-recover cycle across the plot. The cycle's
            // horizontal span is proportional to Duck Length (300 ms maps to
            // the full plot width; clamped) so changing Duck Length visibly
            // widens/narrows the cycle. Shape mirrors the engine:
            // near-instant attack, a plateau of holdFraction(release) * the
            // cycle span, then a 3-tau exponential recovery (Release
            // changes the plateau/tail proportion).
            const float holdFrac = sid::dsp::DuckEngine::holdFractionForRelease (
                previewReleaseMs_);
            const float cycleSpan = juce::jlimit (0.12f, 0.92f,
                                      300.0f / juce::jmax (60.0f, previewDuckLengthMs_));
            const float attackFrac = 0.03f * cycleSpan;         // ~1.5 ms attack
            const float plateauSpan = cycleSpan * holdFrac;
            const float tailSpan = juce::jmax (0.02f, cycleSpan - attackFrac - plateauSpan);

            juce::Path duck;
            duck.startNewSubPath (x0, y0);
            const int steps = (int) w;
            for (int s = 1; s <= steps; ++s)
            {
                const float t = (float) s / (float) steps;   // 0..1 across plot

                float depth;
                if (t < attackFrac)
                    depth = depthDb * (t / attackFrac);
                else if (t < attackFrac + plateauSpan)
                    depth = depthDb;
                else if (t < attackFrac + plateauSpan + tailSpan)
                {
                    const float tr = (t - attackFrac - plateauSpan) / tailSpan;
                    depth = depthDb * std::exp (-tr * 3.0f);
                }
                else
                    depth = 0.0f;   // recovered: flat until the right edge

                duck.lineTo (x0 + t * w, y0 + dbToY (-depth, h));
            }
            duck.lineTo (x0 + w, y0);
            duck.closeSubPath();

            // Subtle/dim while inactive: low-alpha mint fill + faint outline.
            g.setColour (juce::Colour (0x227fd1c0));
            g.fillPath (duck);
            g.setColour (juce::Colour (0x667fd1c0));
            g.strokePath (duck,
                          juce::PathStrokeType (1.2f,
                                                juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
        }

        // Minimal state label (bottom-left, same quiet tone as axis labels).
        g.setFont (juce::Font (10.0f));
        g.setColour (juce::Colour (0x556b7687));
        g.drawText ("PREVIEW", plot.getX() + 4, (int) (y0 + h) - 16, 70, 14,
                    juce::Justification::bottomLeft);
    }

    if (viewMode_ == ViewMode::sidechain)
    {
        // ================================================================
        // SIDECHAIN view: ONLY the ducking envelope (+ trigger ticks).
        // ================================================================
        const int count = filled_ ? kHistorySize : writePos_;
        if (count >= 2)
        {
            bool anyEnvelope = false;
            juce::Path duck;
            duck.startNewSubPath (x0, y0);
            for (int i = 0; i < count; ++i)
            {
                const float frac = (float) i / (float) (count - 1);
                const float depth = sampleAt (reductionHist_, frac);
                if (depth > 0.2f) anyEnvelope = true;
                duck.lineTo (x0 + frac * w, y0 + dbToY (-depth, h));
            }
            duck.lineTo (x0 + w, y0);
            duck.closeSubPath();

            if (anyEnvelope)
            {
                g.setColour (juce::Colour (0x3d7fd1c0));
                g.fillPath (duck);
                g.setColour (juce::Colour (0xb37fd1c0));
                g.strokePath (duck,
                              juce::PathStrokeType (1.6f,
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
            }

            drawTriggerTicks (count);

            // Beat markers: with the internal tempo-synced trigger every
            // trigger tick IS a quarter-note beat. Full-height faint lines
            // under the tick marks make the beat grid readable; the label
            // shows the real host BPM the grid follows.
            {
                juce::Path beatLines;
                for (int i = 0; i < count; ++i)
                {
                    if (! triggerHist_[(size_t) ((writePos_ - count + i + kHistorySize) % kHistorySize)])
                        continue;
                    const float frac = (float) i / (float) (count - 1);
                    beatLines.startNewSubPath (x0 + frac * w, y0);
                    beatLines.lineTo (x0 + frac * w, y0 + h);
                }
                g.setColour (juce::Colour (0x18e8ecf3));
                g.strokePath (beatLines, juce::PathStrokeType (1.0f));
            }

            if (anyEnvelope)
            {
                g.setFont (juce::Font (10.0f));
                g.setColour (juce::Colour (0x887fd1c0));
                g.drawText ("DUCK ENVELOPE", plot.getRight() - 110, (int) y0 + 4,
                            106, 14, juce::Justification::topRight);

                if (liveBpm_ > 0.0f)
                {
                    g.setColour (juce::Colour (0x556b7687));
                    g.drawText (juce::String (liveBpm_, 1) + " BPM / BEAT",
                                plot.getRight() - 150, (int) (y0 + h) - 16,
                                146, 14, juce::Justification::bottomRight);
                }
            }
        }
    }
    else
    {
        // ================================================================
        // ANALYZER view: the original multi-trace signal analysis.
        // ================================================================
        const int count = filled_ ? kHistorySize : writePos_;

        // ---- DUCK ENVELOPE: filled reduction shadow + trigger ticks ----
        if (count >= 2)
        {
            bool anyEnvelope = false;
            juce::Path duck;
            duck.startNewSubPath (x0, y0);
            for (int i = 0; i < count; ++i)
            {
                const float frac = (float) i / (float) (count - 1);
                const float depth = sampleAt (reductionHist_, frac);
                if (depth > 0.2f) anyEnvelope = true;
                duck.lineTo (x0 + frac * w, y0 + dbToY (-depth, h));
            }
            duck.lineTo (x0 + w, y0);
            duck.closeSubPath();

            if (anyEnvelope)
            {
                g.setColour (juce::Colour (0x3d7fd1c0));
                g.fillPath (duck);
                g.setColour (juce::Colour (0xb37fd1c0));
                g.strokePath (duck,
                              juce::PathStrokeType (1.6f,
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
            }

            drawTriggerTicks (count);

            if (anyEnvelope)
            {
                g.setFont (juce::Font (10.0f));
                g.setColour (juce::Colour (0x887fd1c0));
                g.drawText ("DUCK", plot.getRight() - 54, (int) y0 - 13, 50, 14,
                            juce::Justification::topRight);
            }
        }

        // ---- SIDECHAIN ----
        {
            auto p = buildPath (sidechainHist_);
            g.setColour (juce::Colour (0xffffc857)); // warm amber: the trigger
            g.strokePath (p, juce::PathStrokeType (1.4f));
        }

        // ---- INPUT ----
        {
            auto p = buildPath (inputHist_);
            g.setColour (juce::Colour (0x88a8b3c7)); // subtle cool grey
            g.strokePath (p, juce::PathStrokeType (1.1f));
        }

        // ---- OUTPUT ----
        {
            auto p = buildPath (outputHist_);
            g.setColour (juce::Colour (0xff7fd1c0)); // clear mint: what you hear
            g.strokePath (p, juce::PathStrokeType (1.8f));
        }
    }

    // dB scale labels are shared by both views.
    g.setFont (juce::Font (11.0f));
    g.setColour (juce::Colour (0xff6b7687));
    g.drawText ("0 dB",  plot.getX() + 4, (int) y0 - 13, 60, 14, juce::Justification::topLeft);
    g.drawText ("-60",   plot.getX() + 4, (int) (y0 + h) - 14, 60, 14, juce::Justification::topLeft);
}
