/*
    SideChainer - factory preset system (0.4.0 internal trigger presets).

    With the 0.4.0 internal tempo-synced trigger there is no external
    kick: a preset is a named INTERNAL DUCKING TRIGGER SHAPE - a triple
    of existing user parameters:

        sidechainAmount (0..100 %)  -> duck depth (100 % = -24 dB)
        release         (50..1000)  -> envelope shape (plateau vs tail)
        duckLength      (50..1000)  -> base total duck duration (ms)

    All presets trigger internally on every quarter-note beat; no kick
    audio is involved anywhere.

    Preset independence (0.3.0 contract, preserved): selecting a preset
    sets its factory base length (conventional template behaviour); after
    that the user's DUCK LENGTH knob rules until the next preset
    selection. The saved state is purely parameter-valued - "Custom" is
    derived on demand by comparing live values against this table
    (findMatchingPreset), so renaming/reordering cannot break old sessions.

    The factory set runs from very short "kick-like" envelopes to very
    long pumping envelopes (all at 120 BPM for reference):

      Micro Kick    60 % / 150 /  60 ms : tight click, subtle depth
      Tight Kick    70 % / 120 /  90 ms : short but clearly audible
      Classic Kick  75 % / 150 / 250 ms : the default musical duck
      Short Pump    60 % / 120 / 180 ms : noticeable, snappy pumping
      Medium Pump   65 % / 200 / 350 ms : moderate duck, smooth recovery
      Wide Pump     70 % / 300 / 500 ms : longer recovery, laid back
      Deep Pump     85 % / 250 / 400 ms : deeper and longer
      EDM Pump      90 % / 400 / 600 ms : strong festival pump
      Long Pump     80 % / 500 / 800 ms : very long tail
      Full Beat     95 % / 350 / 950 ms : envelope ~ the whole beat
*/

#pragma once

#include <JuceHeader.h>
#include "PumpCurve.h"

namespace sid::presets
{

// Keep factory curve points tied to the established legacy preset envelope:
// same attack, Release-to-hold mapping and three release time constants,
// represented as normalized coordinates for the selected Duck Length.
inline sid::curve::PumpCurve makePresetCurve (float releaseMs, float duckLengthMs) noexcept
{
    constexpr double referenceMinimumGain = sid::curve::PumpCurve::kReferenceMinimumGain;
    const double release = juce::jlimit (50.0, 1000.0, (double) releaseMs);
    const double length = juce::jlimit (50.0, 1000.0, (double) duckLengthMs);
    const double holdFraction = 0.05 + (release - 50.0) * (0.50 - 0.05) / (1000.0 - 50.0);
    constexpr double attackSettlingMs = 4.5; // three 1.5 ms attack constants
    const double holdMs = holdFraction * length;
    const double releaseTauMs = (length - holdMs) / 3.0;
    const sid::curve::Point points[] = {
        { 0.0, 1.0 },
        { attackSettlingMs / length, referenceMinimumGain },
        { (attackSettlingMs + holdMs) / length, referenceMinimumGain },
        { (attackSettlingMs + holdMs + releaseTauMs) / length,
          1.0 - (1.0 - referenceMinimumGain) * 0.36787944117144233 },
        { (attackSettlingMs + holdMs + 2.0 * releaseTauMs) / length,
          1.0 - (1.0 - referenceMinimumGain) * 0.1353352832366127 },
        { 1.0, 1.0 }
    };

    sid::curve::PumpCurve curve;
    curve.trySetPoints (points, sizeof (points) / sizeof (points[0]));
    return curve;
}

struct FactoryPreset
{
    FactoryPreset (juce::String presetName, float amount, float release, float length)
        : name (std::move (presetName)), amountPercent (amount), releaseMs (release),
          duckLengthMs (length), pumpCurve (makePresetCurve (release, length)) {}

    juce::String name;
    float amountPercent; // sidechainAmount, 0..100
    float releaseMs;     // release (shape), 50..1000
    float duckLengthMs;  // base total duck duration, 50..1000
    sid::curve::PumpCurve pumpCurve;
};

inline bool samePumpCurve (const sid::curve::PumpCurve& a,
                           const sid::curve::PumpCurve& b) noexcept
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::abs (a[i].x - b[i].x) > 1.0e-6
            || std::abs (a[i].y - b[i].y) > 1.0e-6)
            return false;
    return true;
}

// ----------------------------------------------------------------------------
// Factory table (0.4.0 trigger shapes; see notes above).
// ----------------------------------------------------------------------------
inline const juce::Array<FactoryPreset>& factoryPresets()
{
    static const juce::Array<FactoryPreset> table ({
        FactoryPreset { "Micro Kick",   60.0f, 150.0f,  60.0f },
        FactoryPreset { "Tight Kick",   70.0f, 120.0f,  90.0f },
        FactoryPreset { "Classic Kick", 75.0f, 150.0f, 250.0f },
        FactoryPreset { "Short Pump",   60.0f, 120.0f, 180.0f },
        FactoryPreset { "Medium Pump",  65.0f, 200.0f, 350.0f },
        FactoryPreset { "Wide Pump",    70.0f, 300.0f, 500.0f },
        FactoryPreset { "Deep Pump",    85.0f, 250.0f, 400.0f },
        FactoryPreset { "EDM Pump",     90.0f, 400.0f, 600.0f },
        FactoryPreset { "Long Pump",    80.0f, 500.0f, 800.0f },
        FactoryPreset { "Full Beat",    95.0f, 350.0f, 950.0f }
    });
    return table;
}

inline const FactoryPreset* findByName (const juce::String& name)
{
    for (const auto& p : factoryPresets())
        if (p.name == name)
            return &p;
    return nullptr;
}

inline const FactoryPreset* findMatchingPreset (float amountPercent, float releaseMs,
                                                float duckLengthMs)
{
    for (const auto& p : factoryPresets())
        if (std::abs (p.amountPercent - amountPercent) < 0.051f
            && std::abs (p.releaseMs - releaseMs) < 0.051f
            && std::abs (p.duckLengthMs - duckLengthMs) < 0.051f)
            return &p;
    return nullptr;
}

inline const FactoryPreset* findMatchingPreset (float amountPercent, float releaseMs,
                                                float duckLengthMs,
                                                const sid::curve::PumpCurve& curve)
{
    for (const auto& p : factoryPresets())
        if (std::abs (p.amountPercent - amountPercent) < 0.051f
            && std::abs (p.releaseMs - releaseMs) < 0.051f
            && std::abs (p.duckLengthMs - duckLengthMs) < 0.051f
            && samePumpCurve (p.pumpCurve, curve))
            return &p;
    return nullptr;
}

} // namespace sid::presets
