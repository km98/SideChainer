/*
    PumpCurve state model.

    This is deliberately independent of the audio processor and allocates
    nothing. Phase B stores normalized breakpoints only; it does not evaluate
    the curve or connect it to the DSP.
*/
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sid::curve
{

struct Point
{
    double x = 0.0;
    double y = 1.0;
};

enum class StateMode : std::uint8_t
{
    legacy,
    pumpCurve
};

class PumpCurve
{
public:
    static constexpr std::size_t kMinimumPoints = 2;
    static constexpr std::size_t kMaximumPoints = 16;
    static constexpr double kMinimumSpacing = 1.0 / 1024.0;

    // Uses the validated attack (1.5 ms), hold fraction at default Release
    // (150 ms), and exponential recovery with tau derived from the default
    // Duck Length (250 ms): hold + 3*tau equals the normalized duration.
    // These fixed coordinates are tempo and Amount independent.
    static constexpr std::array<Point, 6> defaultPoints() noexcept
    {
        constexpr double duckLengthMs = 250.0;
        constexpr double attackMs = 1.5;
        constexpr double holdFractionMin = 0.05;
        constexpr double holdFractionMax = 0.50;
        constexpr double releaseMsMin = 50.0;
        constexpr double releaseMsMax = 1000.0;
        constexpr double releaseDefaultMs = 150.0;
        constexpr double holdMs = (holdFractionMin
            + (releaseDefaultMs - releaseMsMin) * (holdFractionMax - holdFractionMin)
                                             / (releaseMsMax - releaseMsMin)) * duckLengthMs;
        constexpr double tauMs = (duckLengthMs - holdMs) / 3.0;
        // DuckEngine's validated maximum Amount depth is -24 dB.
        constexpr double minimumGain = 0.06309573444801933;
        constexpr double attackSettlingTimeMs = 3.0 * attackMs;
        return {{{ 0.0, 1.0 },
                 { attackSettlingTimeMs / duckLengthMs, minimumGain },
                 { (attackSettlingTimeMs + holdMs) / duckLengthMs, minimumGain },
                 { (attackSettlingTimeMs + holdMs + tauMs) / duckLengthMs,
                   1.0 - (1.0 - minimumGain) * 0.36787944117144233 },
                 { (attackSettlingTimeMs + holdMs + 2.0 * tauMs) / duckLengthMs,
                   1.0 - (1.0 - minimumGain) * 0.1353352832366127 },
                 { 1.0, 1.0 }}};
    }

    PumpCurve() noexcept
    {
        const auto defaults = defaultPoints();
        for (std::size_t i = 0; i < defaults.size(); ++i)
            points_[i] = defaults[i];
        count_ = defaults.size();
    }

    std::size_t size() const noexcept { return count_; }
    const Point& operator[] (std::size_t index) const noexcept { return points_[index]; }
    const std::array<Point, kMaximumPoints>& storage() const noexcept { return points_; }

    // Strict validation makes malformed input deterministic: no clamping or
    // reordering changes the authored curve. On failure, this object is left
    // untouched so callers can explicitly choose a fallback.
    static bool isValid (const Point* points, std::size_t count) noexcept
    {
        if (points == nullptr || count < kMinimumPoints || count > kMaximumPoints)
            return false;

        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& p = points[i];
            if (! std::isfinite (p.x) || ! std::isfinite (p.y)
                || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0)
                return false;
        }

        if (points[0].x != 0.0 || points[0].y != 1.0
            || points[count - 1].x != 1.0 || points[count - 1].y != 1.0)
            return false;

        for (std::size_t i = 1; i < count; ++i)
            if (points[i].x - points[i - 1].x < kMinimumSpacing)
                return false;

        return true;
    }

    bool trySetPoints (const Point* points, std::size_t count) noexcept
    {
        if (! isValid (points, count))
            return false;

        points_ = {};
        for (std::size_t i = 0; i < count; ++i)
            points_[i] = points[i];
        count_ = count;
        return true;
    }

private:
    std::array<Point, kMaximumPoints> points_ {};
    std::size_t count_ = kMinimumPoints;
};

} // namespace sid::curve
