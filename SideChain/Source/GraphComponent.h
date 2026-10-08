/*
    SideChain - real-time graph component (Phase 4; 0.3.0 view modes).

    TWO VIEW MODES:

      SIDECHAIN (default) - the primary visualization: ONLY the actual
          ducking envelope. While playing, recent real duck cycles scroll
          with trigger tick marks; while stopped / silent, a clean
          parameter-driven preview of the current duck shape is shown
          (labelled PREVIEW). No analyzer traces are drawn.

      ANALYZER - the original Phase 4 multi-trace view: input, sidechain,
          gain reduction (duck fill + ticks) and output over a rolling
          time window. Preserved unchanged.

    All data comes from the audio thread via the GraphFrameFifo; nothing
    is synthesised in the GUI (the preview is an explicit, labelled
    parameter visualization, not fake measurement data).
*/

#pragma once

#include <JuceHeader.h>
#include "GraphData.h"
#include "PumpCurve.h"

//==============================================================================
class GraphComponent : public juce::Component
{
public:
    GraphComponent();

    enum class ViewMode { sidechain, analyzer };

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    using CurveChanged = std::function<bool (const sid::curve::PumpCurve&)>;
    void setCurve (const sid::curve::PumpCurve& curve,
                   sid::curve::StateMode mode = sid::curve::StateMode::pumpCurve);
    void setSmoothness (float smoothnessPercent) noexcept;
    const sid::curve::PumpCurve& getCurve() const noexcept { return curve_; }
    void setCurveChangedCallback (CurveChanged callback) { curveChanged_ = std::move (callback); }
    void resetCurve();
    juce::TextButton& resetButtonForTest() noexcept { return resetButton_; }
    juce::Rectangle<float> plotBoundsForTest() const noexcept { return curvePlotBounds(); }

    // Deterministic edit primitives shared by mouse handling and headless UI tests.
    int hitTestPoint (juce::Point<float> position) const noexcept;
    bool selectPointAt (juce::Point<float> position) noexcept;
    bool moveSelectedPointTo (double x, double y) noexcept;
    bool insertPointAt (double x, double y) noexcept;
    bool deleteSelectedPoint() noexcept;
    int getSelectedPointIndex() const noexcept { return selectedPoint_; }
    sid::curve::StateMode getCurveStateMode() const noexcept { return curveMode_; }

    // Called by the editor timer: drains new frames into the history.
    void consumeFrames (sid::graph::GraphFrameFifo& fifo);

    // Clears history to silence (used on start / reset).
    void resetHistory();

    // SIDECHAIN (default) / ANALYZER. Pure display switch: it does not
    // touch the history, the FIFO or the DSP (verified by tests).
    void setViewMode (ViewMode m)        { if (m != viewMode_) { viewMode_ = m; repaint(); } }
    ViewMode getViewMode() const noexcept { return viewMode_; }
    void setHistoryDisplayEnabled (bool enabled) noexcept { historyDisplayEnabled_ = enabled; repaint(); }
    bool isHistoryDisplayEnabled() const noexcept { return historyDisplayEnabled_; }

    // Static preview of the CURRENT parameter ducking characteristic,
    // shown (dimmed) whenever the live history carries no signal yet.
    // Driven by the live parameter values - never hardcoded per preset.
    void setPreviewParams (float amountPercent, float releaseMs, float duckLengthMs);

public:
    // settled ducking depth (dB, positive) for a given Amount. Public so the
    // UI harness can assert the preview's parameter response. Uses the DSP's
    // musical v0.3.0 mapping (100 % = -24 dB).
    static float settledDepthDb (float amountPercent);

#if defined (SIDECHAIN_HEADLESS_TEST)
    // Test-only accessors (headless UI tests assert history purity).
    void pushFrameForTest (const sid::graph::GraphFrame& f) { pushFrame (f); }
    int  writePosForTest() const noexcept { return writePos_; }
    float reductionAtForTest (int i) const
    {
        return reductionHist_[(size_t) ((writePos_ - 1 - i + 2 * kHistorySize) % kHistorySize)];
    }
    bool historyIsSilentForTest() const { return historyIsSilent(); }
#endif

private:
    void pushFrame (const sid::graph::GraphFrame& f);
    juce::Rectangle<float> curvePlotBounds() const noexcept;
    juce::Point<float> pointToPosition (const sid::curve::Point&) const noexcept;
    juce::Point<double> positionToNormalised (juce::Point<float>) const noexcept;
    bool commitCurve (const sid::curve::PumpCurve&) noexcept;
    juce::Path makeSmoothCurvePath (juce::Rectangle<float>) const;
    void paintPumpCurve (juce::Graphics&);

    static constexpr float kPointHitRadius = 10.0f;
    static constexpr int kHistorySeconds = 6;
    static constexpr int kHistorySize    = 512; // graph resolution (columns)

    // Rolling history (GUI-side copy, decimated from the FIFO stream).
    std::array<float, kHistorySize> inputHist_ {};
    std::array<float, kHistorySize> sidechainHist_ {};
    std::array<float, kHistorySize> reductionHist_ {}; // stored as positive depth dB
    std::array<float, kHistorySize> outputHist_ {};
    std::array<bool,  kHistorySize> triggerHist_ {};   // 0.3.0: kick fired this frame

    int writePos_ = 0;
    bool filled_ = false;

    // ---- view mode (default SIDECHAIN) -----------------------------------
    ViewMode viewMode_ = ViewMode::sidechain;
    bool historyDisplayEnabled_ = true;

    // ---- no-signal preview (see setPreviewParams) ------------------------
    float previewAmountPercent_ = 50.0f;
    float previewReleaseMs_     = 150.0f;
    float previewDuckLengthMs_  = 250.0f;
    float liveBpm_ = 0.0f;   // last host tempo seen in live frames (display)

    sid::curve::PumpCurve curve_;
    sid::curve::StateMode curveMode_ = sid::curve::StateMode::pumpCurve;
    double curveSmoothness_ = 0.5;
    CurveChanged curveChanged_;
    juce::TextButton resetButton_ { "RESET" };
    int selectedPoint_ = -1;
    bool draggingPoint_ = false;

    // True while the history contains nothing but silence: no frames have
    // carried input, sidechain or output level above the silence floor.
    bool historyIsSilent() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphComponent)
};
