/*
    SideChain - graph component implementation (Phase 4; 0.3.0 view modes).

    LIVE WAVEFORM LAYER (0.4.2): paintPumpCurve() fills the plot, draws the
    grid, then paints the real input/output waveform envelopes (paintWaveform)
    and only afterwards the PumpCurve curve, its labels and its control points.
    The z-order is therefore exactly what the product requires: audio behind,
    editable curve on top.

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
#include "PumpCurveTheme.h"

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
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    resetButton_.setTooltip ("Restore the default PumpCurve (also: right-click the curve)");
    sid::theme::setButtonStyle (resetButton_, sid::theme::ButtonStyle::quiet);
    resetButton_.onClick = [this] { resetCurve(); };
    addAndMakeVisible (resetButton_);
    resetHistory();
}

// Panel geometry (0.5.0 redesign): a title strip on top (CURVE caption, the
// IN/OUT legend and RESET), a left gutter for the gain axis (UNITY / DUCK) and
// a bottom gutter for the time axis (HIT .. TIME). The plot itself - where the
// curve, its points and the live waveform live - takes everything else.
namespace
{
    constexpr float kPlotTop    = 32.0f;
    constexpr float kPlotBottom = 24.0f;
    constexpr float kPlotLeft   = 50.0f;
    constexpr float kPlotRight  = 18.0f;
}

juce::Rectangle<float> GraphComponent::curvePlotBounds() const noexcept
{
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromTop (kPlotTop);
    bounds.removeFromBottom (kPlotBottom);
    bounds.removeFromLeft (kPlotLeft);
    bounds.removeFromRight (kPlotRight);
    return bounds;
}

void GraphComponent::resized()
{
    resetButton_.setBounds (getWidth() - (int) kPlotRight - 56 + 6, 5, 56, 22);
    repaint();
}

juce::Point<float> GraphComponent::pointToPosition (const sid::curve::Point& point) const noexcept
{
    const auto plot = curvePlotBounds();
    return { plot.getX() + (float) point.x * plot.getWidth(),
             plot.getY() + (float) (1.0 - point.y) * plot.getHeight() };
}

juce::Point<double> GraphComponent::positionToNormalised (juce::Point<float> position) const noexcept
{
    const auto plot = curvePlotBounds();
    if (plot.getWidth() <= 0.0f || plot.getHeight() <= 0.0f)
        return {};
    return { juce::jlimit (0.0, 1.0, (double) (position.x - plot.getX()) / plot.getWidth()),
             juce::jlimit (0.0, 1.0, 1.0 - (double) (position.y - plot.getY()) / plot.getHeight()) };
}

void GraphComponent::setSmoothness (float smoothnessPercent) noexcept
{
    const double value = std::isfinite (smoothnessPercent)
                       ? juce::jlimit (0.0f, 100.0f, smoothnessPercent) / 100.0 : 0.5;
    if (value != curveSmoothness_)
    {
        curveSmoothness_ = value;
        repaint();
    }
}

void GraphComponent::setCurve (const sid::curve::PumpCurve& curve, sid::curve::StateMode mode)
{
    if (! sid::curve::PumpCurve::isValid (curve.storage().data(), curve.size()))
        return;
    bool unchanged = mode == curveMode_ && curve.size() == curve_.size();
    for (std::size_t i = 0; unchanged && i < curve.size(); ++i)
        unchanged = curve[i].x == curve_[i].x && curve[i].y == curve_[i].y;
    if (unchanged)
        return;

    curve_ = curve;
    curveMode_ = mode;
    if (selectedPoint_ >= (int) curve_.size())
        selectedPoint_ = -1;
    repaint();
}

bool GraphComponent::commitCurve (const sid::curve::PumpCurve& candidate) noexcept
{
    if (! sid::curve::PumpCurve::isValid (candidate.storage().data(), candidate.size()))
        return false;
    if (curveChanged_ && ! curveChanged_ (candidate))
        return false;
    curve_ = candidate;
    curveMode_ = sid::curve::StateMode::pumpCurve;
    repaint();
    return true;
}

void GraphComponent::resetCurve()
{
    sid::curve::PumpCurve defaults;
    if (commitCurve (defaults))
    {
        selectedPoint_ = -1;
        draggingPoint_ = false;
        repaint();
    }
}

int GraphComponent::hitTestPoint (juce::Point<float> position) const noexcept
{
    const float radiusSquared = kPointHitRadius * kPointHitRadius;
    int best = -1;
    float bestDistance = radiusSquared;
    for (std::size_t i = 0; i < curve_.size(); ++i)
    {
        const auto delta = pointToPosition (curve_[i]) - position;
        const float distance = delta.x * delta.x + delta.y * delta.y;
        if (distance <= bestDistance)
        {
            best = (int) i;
            bestDistance = distance;
        }
    }
    return best;
}

bool GraphComponent::selectPointAt (juce::Point<float> position) noexcept
{
    selectedPoint_ = hitTestPoint (position);
    repaint();
    return selectedPoint_ >= 0;
}

bool GraphComponent::moveSelectedPointTo (double x, double y) noexcept
{
    if (selectedPoint_ <= 0 || selectedPoint_ >= (int) curve_.size() - 1
        || ! std::isfinite (x) || ! std::isfinite (y))
        return false;

    const auto spacing = sid::curve::PumpCurve::kMinimumSpacing;
    const double minX = curve_[(std::size_t) selectedPoint_ - 1].x + spacing;
    const double maxX = curve_[(std::size_t) selectedPoint_ + 1].x - spacing;
    if (minX > maxX)
        return false;

    sid::curve::Point proposed[sid::curve::PumpCurve::kMaximumPoints] {};
    for (std::size_t i = 0; i < curve_.size(); ++i)
        proposed[i] = curve_[i];
    proposed[selectedPoint_] = { juce::jlimit (minX, maxX, x), juce::jlimit (0.0, 1.0, y) };

    sid::curve::PumpCurve candidate;
    return candidate.trySetPoints (proposed, curve_.size()) && commitCurve (candidate);
}

bool GraphComponent::insertPointAt (double x, double y) noexcept
{
    if (! std::isfinite (x) || ! std::isfinite (y)
        || curve_.size() >= sid::curve::PumpCurve::kMaximumPoints)
        return false;

    const auto spacing = sid::curve::PumpCurve::kMinimumSpacing;
    x = juce::jlimit (0.0, 1.0, x);
    y = juce::jlimit (0.0, 1.0, y);
    std::size_t insertion = 1;
    while (insertion < curve_.size() - 1 && curve_[insertion].x < x)
        ++insertion;
    if (x - curve_[insertion - 1].x < spacing || curve_[insertion].x - x < spacing)
        return false;

    sid::curve::Point proposed[sid::curve::PumpCurve::kMaximumPoints] {};
    for (std::size_t i = 0; i < insertion; ++i)
        proposed[i] = curve_[i];
    proposed[insertion] = { x, y };
    for (std::size_t i = insertion; i < curve_.size(); ++i)
        proposed[i + 1] = curve_[i];

    sid::curve::PumpCurve candidate;
    if (! candidate.trySetPoints (proposed, curve_.size() + 1) || ! commitCurve (candidate))
        return false;
    selectedPoint_ = (int) insertion;
    grabKeyboardFocus();
    return true;
}

bool GraphComponent::deleteSelectedPoint() noexcept
{
    if (selectedPoint_ <= 0 || selectedPoint_ >= (int) curve_.size() - 1)
        return false;

    const auto removed = (std::size_t) selectedPoint_;
    sid::curve::Point proposed[sid::curve::PumpCurve::kMaximumPoints] {};
    for (std::size_t src = 0, dst = 0; src < curve_.size(); ++src)
        if (src != removed)
            proposed[dst++] = curve_[src];

    sid::curve::PumpCurve candidate;
    if (! candidate.trySetPoints (proposed, curve_.size() - 1) || ! commitCurve (candidate))
        return false;
    selectedPoint_ = juce::jmin (selectedPoint_, (int) curve_.size() - 2);
    repaint();
    return true;
}

void GraphComponent::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    const int hit = hitTestPoint (event.position);
    if (event.mods.isRightButtonDown())
    {
        selectedPoint_ = hit;
        if (hit > 0 && hit < (int) curve_.size() - 1)
            deleteSelectedPoint();
        else
            repaint();
        return;
    }
    selectedPoint_ = hit;
    draggingPoint_ = hit > 0 && hit < (int) curve_.size() - 1;
    repaint();
}

void GraphComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (! draggingPoint_)
        return;
    const auto normalised = positionToNormalised (event.position);
    moveSelectedPointTo (normalised.x, normalised.y);
}

void GraphComponent::mouseUp (const juce::MouseEvent&)
{
    draggingPoint_ = false;
}

void GraphComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (hitTestPoint (event.position) < 0)
    {
        const auto normalised = positionToNormalised (event.position);
        insertPointAt (normalised.x, normalised.y);
    }
}

bool GraphComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        return deleteSelectedPoint();
    if (key == juce::KeyPress ('r', juce::ModifierKeys(), 0))
    {
        resetCurve();
        return true;
    }

    const double stepX = sid::curve::PumpCurve::kMinimumSpacing;
    constexpr double stepY = 0.02;
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey
        || key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        if (selectedPoint_ <= 0 || selectedPoint_ >= (int) curve_.size() - 1)
            return true;
        const auto& point = curve_[(std::size_t) selectedPoint_];
        double x = point.x, y = point.y;
        if (key == juce::KeyPress::leftKey) x -= stepX;
        if (key == juce::KeyPress::rightKey) x += stepX;
        if (key == juce::KeyPress::upKey) y += stepY;
        if (key == juce::KeyPress::downKey) y -= stepY;
        moveSelectedPointTo (x, y);
        return true;
    }
    return false;
}

juce::Path GraphComponent::makeSmoothCurvePath (juce::Rectangle<float> plot) const
{
    juce::Path path;
    if (curve_.size() < 2)
        return path;

    const auto toScreen = [&] (double x, double y)
    {
        return juce::Point<float> (plot.getX() + (float) x * plot.getWidth(),
                                   plot.getY() + (float) (1.0 - y) * plot.getHeight());
    };
    path.startNewSubPath (toScreen (curve_[0].x, curve_[0].y));
    for (std::size_t i = 0; i + 1 < curve_.size(); ++i)
    {
        const auto& p0 = curve_[i == 0 ? i : i - 1];
        const auto& p1 = curve_[i];
        const auto& p2 = curve_[i + 1];
        const auto& p3 = curve_[i + 2 < curve_.size() ? i + 2 : i + 1];
        const int steps = juce::jmax (8, (int) std::ceil ((p2.x - p1.x) * plot.getWidth() / 2.0f));
        for (int step = 1; step <= steps; ++step)
        {
            const double t = (double) step / steps;
            const double x = juce::jmap (t, p1.x, p2.x);
            const double y = curve_.evaluate (x, curveSmoothness_);
            path.lineTo (toScreen (x, y));
        }
    }
    return path;
}

void GraphComponent::resetHistory()
{
    inputHist_.fill (-100.0f);
    sidechainHist_.fill (-100.0f);
    reductionHist_.fill (0.0f);
    outputHist_.fill (-100.0f);
    writePos_ = 0;
    filled_ = false;
    waveformHistory_.clear();      // the scope is part of "history"
    waveformNowMs_ = 0;
    repaint();
}

//==============================================================================
// Live waveform layer
//==============================================================================

void GraphComponent::consumeWaveformFifo (sid::scope::WaveformFifo& fifo, juce::int64 nowMs)
{
    const bool wasDisplayable = hasDisplayableWaveform();
    waveformNowMs_ = nowMs;
    const int appended = waveformHistory_.consume (fifo, nowMs);

    // Repaint when there is new material, and also on the single tick where a
    // stalled producer or a drained window flips the layer to its no-signal
    // fallback (so the change is never left unpainted).
    if (appended > 0 || wasDisplayable != hasDisplayableWaveform())
        repaint();
}

void GraphComponent::paintWaveform (juce::Graphics& g, juce::Rectangle<float> plot)
{
    // Amplitude maps to the plot's own vertical extent with silence on the
    // centre line, so the waveform and the curve share one coordinate space:
    // the curve shows the gain being applied, the trace shows the audio it is
    // being applied to.
    const float centreY = plot.getCentreY();
    const float halfHeight = plot.getHeight() * 0.5f * 0.96f;
    const bool displayable = hasDisplayableWaveform();

    // Zero baseline (also the whole no-signal fallback).
    g.setColour (juce::Colour (displayable ? sid::theme::kGridMajor : sid::theme::kEdge));
    g.drawHorizontalLine ((int) centreY, plot.getX(), plot.getRight());

    if (! displayable)
    {
        g.setFont (sid::theme::captionFont (10.0f));
        g.setColour (juce::Colour (sid::theme::kTextFaint));
        g.drawText ("NO SIGNAL", (int) plot.getRight() - 92, (int) centreY + 6, 84, 12,
                    juce::Justification::topRight);
        return;
    }

    const int count = waveformHistory_.count();
    if (count < 2)
        return;

    // Filled min/max envelopes (top contour forward, bottom contour back):
    // the input as a quiet neutral body, the processed output as a mint body
    // inside it - the gap between the two IS the duck you hear. Both stay well
    // below the curve's own weight; the curve is painted after this layer.
    const float width = plot.getWidth();
    auto envelope = [&] (bool output)
    {
        juce::Path p;
        for (int i = 0; i < count; ++i)
        {
            const float x = plot.getX() + (float) i / (float) (count - 1) * width;
            const float top = centreY - (output ? waveformHistory_.outputMax (i)
                                                : waveformHistory_.inputMax (i)) * halfHeight;
            if (i == 0) p.startNewSubPath (x, top); else p.lineTo (x, top);
        }
        for (int i = count - 1; i >= 0; --i)
        {
            const float x = plot.getX() + (float) i / (float) (count - 1) * width;
            const float bottom = centreY - (output ? waveformHistory_.outputMin (i)
                                                   : waveformHistory_.inputMin (i)) * halfHeight;
            p.lineTo (x, juce::jmax (bottom, centreY - (output ? waveformHistory_.outputMax (i)
                                                               : waveformHistory_.inputMax (i)) * halfHeight + 0.6f));
        }
        p.closeSubPath();
        return p;
    };

    const auto inputBody = envelope (false);
    const auto outputBody = envelope (true);
    g.setColour (juce::Colour (sid::theme::kInput).withAlpha (0.12f));
    g.fillPath (inputBody);
    g.setColour (juce::Colour (sid::theme::kInput).withAlpha (0.22f));
    g.strokePath (inputBody, juce::PathStrokeType (0.7f));
    g.setColour (juce::Colour (sid::theme::kAccent).withAlpha (0.11f));
    g.fillPath (outputBody);
    g.setColour (juce::Colour (sid::theme::kAccent).withAlpha (0.30f));
    g.strokePath (outputBody, juce::PathStrokeType (0.8f));
}

void GraphComponent::paintLegend (juce::Graphics& g, juce::Rectangle<int> area)
{
    // IN / OUT legend in the title strip (always present, dimmed when nothing
    // is flowing, so the meaning of the traces never has to be guessed).
    const bool live = waveformDisplayEnabled_ && hasDisplayableWaveform();
    const float alpha = live ? 1.0f : 0.45f;
    g.setFont (sid::theme::captionFont (9.5f));
    auto item = [&] (const juce::String& text, juce::Colour c)
    {
        auto swatch = area.removeFromLeft (14).withSizeKeepingCentre (14, 2);
        g.setColour (c.withMultipliedAlpha (alpha));
        g.fillRect (swatch);
        area.removeFromLeft (5);
        g.setColour (juce::Colour (sid::theme::kTextMuted).withMultipliedAlpha (alpha));
        g.drawText (text, area.removeFromLeft (28), juce::Justification::centredLeft, false);
        area.removeFromLeft (8);
    };
    item ("IN", juce::Colour (sid::theme::kInput));
    item ("OUT", juce::Colour (sid::theme::kAccent));
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
void GraphComponent::paintPumpCurve (juce::Graphics& g)
{
    using namespace sid::theme;
    const auto curvePlot = curvePlotBounds();

    // The well: the curve editor is recessed INTO the instrument (darker than
    // the field, shadowed along its top edge) - depth, not a bordered box.
    const auto panel = getLocalBounds().toFloat();
    drawWell (g, panel, 6.0f);

    // Title strip: what this is, the trace legend, and (as a child) RESET.
    {
        auto strip = juce::Rectangle<int> ((int) curvePlot.getX(), 7,
                                           (int) curvePlot.getWidth() - 64, 20);
        g.setFont (captionFont (10.5f));
        g.setColour (juce::Colour (kTextMuted));
        g.drawText ("PUMP CURVE", strip.removeFromLeft (100), juce::Justification::centredLeft, false);
        if (waveformDisplayEnabled_)
            paintLegend (g, strip.removeFromRight (104));
    }

    // Grid: quarters of the duck in time and of the depth in gain, drawn as
    // faint dotted lines so they read as reference, never as structure.
    {
        const float dash[] = { 1.0f, 3.0f };
        g.setColour (juce::Colour (kGridMajor));
        for (int i = 1; i < 4; ++i)
        {
            const float gx = std::round (curvePlot.getX() + curvePlot.getWidth() * (float) i / 4.0f) + 0.5f;
            const float gy = std::round (curvePlot.getY() + curvePlot.getHeight() * (float) i / 4.0f) + 0.5f;
            g.drawDashedLine ({ gx, curvePlot.getY(), gx, curvePlot.getBottom() }, dash, 2, 1.0f);
            g.drawDashedLine ({ curvePlot.getX(), gy, curvePlot.getRight(), gy }, dash, 2, 1.0f);
        }
        // Unity and full-duck references are solid: they are the scale.
        g.setColour (juce::Colour (kGridMajor));
        g.fillRect (curvePlot.getX(), curvePlot.getY(), curvePlot.getWidth(), 1.0f);
        g.fillRect (curvePlot.getX(), curvePlot.getBottom() - 1.0f, curvePlot.getWidth(), 1.0f);
    }

    // Axis labels live OUTSIDE the plot, so a control point at either end can
    // never sit on top of them.
    {
        g.setFont (captionFont (9.5f));
        g.setColour (juce::Colour (kTextFaint));
        const int gutterX = getLocalBounds().getX() + 8;
        const int gutterW = (int) curvePlot.getX() - gutterX - 8;
        g.drawText ("UNITY", gutterX, (int) curvePlot.getY() - 6, gutterW, 12,
                    juce::Justification::centredRight, false);
        g.drawText ("DUCK", gutterX, (int) curvePlot.getBottom() - 6, gutterW, 12,
                    juce::Justification::centredRight, false);
        const int axisY = (int) curvePlot.getBottom() + 6;
        g.drawText ("HIT", (int) curvePlot.getX(), axisY, 60, 12,
                    juce::Justification::centredLeft, false);
        g.drawText ("TIME", (int) curvePlot.getRight() - 60, axisY, 60, 12,
                    juce::Justification::centredRight, false);
    }

    // Live audio BELOW the curve: the editable PumpCurve and its control
    // points are painted after this call, so they stay fully legible on top of
    // the waveform layer.
    if (waveformDisplayEnabled_)
        paintWaveform (g, curvePlot);

    const auto curvePath = makeSmoothCurvePath (curvePlot);
    juce::Path fill;
    fill.addPath (curvePath);
    fill.lineTo (curvePlot.getRight(), curvePlot.getY());
    fill.lineTo (curvePlot.getX(), curvePlot.getY());
    fill.closeSubPath();
    // The removed gain (unity down to the curve) is the duck: a mint wash that
    // deepens toward the curve keeps the shape readable without a block of
    // colour.
    g.setGradientFill (juce::ColourGradient (juce::Colour (kAccent).withAlpha (0.03f),
                                             0.0f, curvePlot.getY(),
                                             juce::Colour (kAccent).withAlpha (0.16f),
                                             0.0f, curvePlot.getBottom(), false));
    g.fillPath (fill);

    // Bloom, then the precise stroke on top.
    g.setColour (juce::Colour (kAccent).withAlpha (0.12f));
    g.strokePath (curvePath, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (kAccent));
    g.strokePath (curvePath, juce::PathStrokeType (2.5f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    for (std::size_t i = 0; i < curve_.size(); ++i)
    {
        const auto point = pointToPosition (curve_[i]);
        const bool selected = (int) i == selectedPoint_;
        const bool endpoint = (i == 0 || i + 1 == curve_.size());
        const float radius = selected ? 6.0f : 4.5f;
        if (selected)
        {
            g.setColour (juce::Colour (kAccent).withAlpha (0.18f));
            g.fillEllipse (point.x - 12.0f, point.y - 12.0f, 24.0f, 24.0f);
        }
        // A dark collar separates every handle from the curve and the traces.
        g.setColour (juce::Colour (kWell));
        g.fillEllipse (point.x - radius - 2.0f, point.y - radius - 2.0f,
                       (radius + 2.0f) * 2.0f, (radius + 2.0f) * 2.0f);
        g.setColour (endpoint ? juce::Colour (kText) : juce::Colour (kAccent));
        g.fillEllipse (point.x - radius, point.y - radius, radius * 2.0f, radius * 2.0f);
        if (selected)
        {
            g.setColour (juce::Colour (kText));
            g.drawEllipse (point.x - radius - 3.5f, point.y - radius - 3.5f,
                           (radius + 3.5f) * 2.0f, (radius + 3.5f) * 2.0f, 1.2f);
        }
    }

    // Keyboard focus (Delete removes the selected point): a quiet mint edge.
    if (hasKeyboardFocus (false))
    {
        g.setColour (juce::Colour (kAccent).withAlpha (0.35f));
        g.drawRoundedRectangle (panel.reduced (0.5f), 6.0f, 1.0f);
    }
}

void GraphComponent::paint (juce::Graphics& g)
{
    if (! historyDisplayEnabled_)
    {
        // The component is opaque: give the well's rounded corners the field
        // colour behind them before the well is drawn.
        g.setGradientFill (juce::ColourGradient (
            sid::theme::fieldColourAt ((float) getY() / (float) juce::jmax (1, getParentHeight())),
            0.0f, 0.0f,
            sid::theme::fieldColourAt ((float) getBottom() / (float) juce::jmax (1, getParentHeight())),
            0.0f, (float) getHeight(), false));
        g.fillAll();
        paintPumpCurve (g);
        return;
    }

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

    if (historyDisplayEnabled_ && viewMode_ == ViewMode::sidechain)
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
    else if (historyDisplayEnabled_)
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

    // Phase C foreground: the normalized editable PumpCurve is the primary
    // graph. Keep the historical analyzer/history engine alive underneath;
    // this opaque plotting surface is display-only and never feeds the DSP.
    paintPumpCurve (g);
}
