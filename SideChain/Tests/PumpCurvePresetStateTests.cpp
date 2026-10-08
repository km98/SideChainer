#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PresetManager.h"
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
int checks = 0, failures = 0;
void check (bool ok, const char* label)
{
    ++checks;
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (! ok) ++failures;
}
void pumpLoop()
{
    for (int i = 0; i < 10; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
}
void load (SideChainAudioProcessor& p, const juce::ValueTree& tree)
{
    juce::MemoryBlock bytes;
    juce::MemoryOutputStream stream (bytes, false);
    tree.writeToStream (stream);
    p.setStateInformation (bytes.getData(), (int) bytes.getSize());
}
juce::ValueTree save (SideChainAudioProcessor& p)
{
    pumpLoop();
    juce::MemoryBlock bytes;
    p.getStateInformation (bytes);
    return juce::ValueTree::readFromData (bytes.getData(), bytes.getSize());
}
bool same (const sid::curve::PumpCurve& a, const sid::curve::PumpCurve& b)
{
    return sid::presets::samePumpCurve (a, b);
}
bool extract (const juce::ValueTree& tree, sid::curve::PumpCurve& result)
{
    const auto node = tree.getChildWithName ("PUMPCURVE");
    if (! node.isValid() || ! node.getProperty ("pointCount").isInt()) return false;
    const int count = (int) node.getProperty ("pointCount");
    if (count < 0 || count > (int) sid::curve::PumpCurve::kMaximumPoints
        || node.getNumChildren() != count) return false;
    sid::curve::Point points[sid::curve::PumpCurve::kMaximumPoints] {};
    for (int i = 0; i < count; ++i)
    {
        const auto point = node.getChild (i);
        if (! point.hasType ("POINT") || ! point.hasProperty ("x") || ! point.hasProperty ("y")) return false;
        points[i] = { (double) point.getProperty ("x"), (double) point.getProperty ("y") };
    }
    return result.trySetPoints (points, (std::size_t) count);
}
float value (SideChainAudioProcessor& p, const char* id)
{
    auto* param = dynamic_cast<juce::AudioParameterFloat*> (p.getParameters().getParameter (id));
    return param != nullptr ? param->get() : std::numeric_limits<float>::quiet_NaN();
}
juce::ValueTree legacy (int version)
{
    juce::ValueTree tree ("PARAMS"); tree.setProperty ("stateVersion", version, nullptr);
    const struct { const char* id; double v; int since; } params[] = {
        { "sidechainAmount", 67.3, 1 }, { "release", 333.0, 1 },
        { "sidechainOffset", -12.0, 3 }, { "duckLength", 612.0, 4 }
    };
    for (const auto& item : params)
    {
        if (version < item.since) continue;
        juce::ValueTree p ("PARAM"); p.setProperty ("id", item.id, nullptr);
        p.setProperty ("value", item.v, nullptr); tree.addChild (p, -1, nullptr);
    }
    return tree;
}
juce::ValueTree schema6 (const sid::curve::PumpCurve& curve)
{
    juce::ValueTree tree ("PARAMS"); tree.setProperty ("stateVersion", 6, nullptr);
    const struct { const char* id; double v; } params[] = {
        { "sidechainAmount", 67.3 }, { "release", 333.0 }, { "sidechainOffset", -12.0 },
        { "duckLength", 612.0 }, { "smooth", 73.4 }
    };
    for (const auto& item : params)
    {
        juce::ValueTree p ("PARAM"); p.setProperty ("id", item.id, nullptr);
        p.setProperty ("value", item.v, nullptr); tree.addChild (p, -1, nullptr);
    }
    juce::ValueTree node ("PUMPCURVE"); node.setProperty ("pointCount", (int) curve.size(), nullptr);
    for (std::size_t i = 0; i < curve.size(); ++i)
    {
        juce::ValueTree p ("POINT"); p.setProperty ("x", curve[i].x, nullptr);
        p.setProperty ("y", curve[i].y, nullptr); node.addChild (p, -1, nullptr);
    }
    tree.addChild (node, -1, nullptr); return tree;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::puts ("PumpCurve Phase E preset/state migration tests");

    bool curvesValid = sid::presets::factoryPresets().size() == 10;
    bool allPresetsApply = true;
    for (const auto& preset : sid::presets::factoryPresets())
    {
        curvesValid = curvesValid
            && preset.pumpCurve.size() >= 2 && preset.pumpCurve.size() <= 16
            && sid::curve::PumpCurve::isValid (preset.pumpCurve.storage().data(), preset.pumpCurve.size())
            && same (preset.pumpCurve, sid::presets::makePresetCurve (preset.releaseMs, preset.duckLengthMs));
        SideChainAudioProcessor p;
        allPresetsApply = allPresetsApply && p.applyFactoryPreset (preset.name)
            && p.getCurveStateMode() == sid::curve::StateMode::pumpCurve
            && same (p.getPumpCurve(), preset.pumpCurve)
            && std::abs (value (p, "sidechainAmount") - preset.amountPercent) < 0.01f
            && std::abs (value (p, "release") - preset.releaseMs) < 0.01f
            && std::abs (value (p, "duckLength") - preset.duckLengthMs) < 0.01f;
    }
    check (curvesValid, "all ten factory presets own deterministic valid 6-point PumpCurves");
    check (allPresetsApply, "all presets apply curves and preserve established parameter values");

    {
        const auto* first = sid::presets::findByName ("Micro Kick");
        const auto* next = sid::presets::findByName ("Wide Pump");
        SideChainAudioProcessor p;
        const bool switched = first && next && p.applyFactoryPreset (first->name)
            && same (p.getPumpCurve(), first->pumpCurve) && p.applyFactoryPreset (next->name)
            && same (p.getPumpCurve(), next->pumpCurve);
        const auto state = save (p);
        sid::curve::PumpCurve stored;
        SideChainAudioProcessor restored; load (restored, state);
        check (switched && extract (state, stored) && same (stored, next->pumpCurve)
            && restored.getCurveStateMode() == sid::curve::StateMode::pumpCurve
            && same (restored.getPumpCurve(), next->pumpCurve),
            "switching preset updates its curve and schema-6 state round-trips exactly");
    }

    {
        const auto* preset = sid::presets::findByName ("EDM Pump");
        SideChainAudioProcessor a, b;
        const bool applied = preset && a.applyFactoryPreset (preset->name) && b.applyFactoryPreset (preset->name);
        const auto sa = save (a), sb = save (b);
        sid::curve::PumpCurve ca, cb;
        check (applied && extract (sa, ca) && extract (sb, cb) && same (ca, cb)
            && same (ca, preset->pumpCurve), "same preset application yields deterministic serialized state");
    }

    {
        bool okay = true;
        for (int version = 1; version <= 5; ++version)
        {
            SideChainAudioProcessor p; load (p, legacy (version)); const auto migrated = save (p);
            okay = okay && p.getCurveStateMode() == sid::curve::StateMode::legacy
                && std::abs (value (p, "sidechainAmount") - 67.3f) < 0.06f
                && std::abs (value (p, "release") - 333.0f) < 0.1f
                && std::abs (value (p, "sidechainOffset") - (version >= 3 ? -12.0f : 0.0f)) < 0.1f
                && std::abs (value (p, "duckLength") - (version >= 4 ? 612.0f : 250.0f)) < 0.1f
                && std::abs (value (p, "smooth") - 50.0f) < 0.01f
                && (int) migrated.getProperty ("stateVersion", juce::var (0)) == 5
                && ! migrated.getChildWithName ("PUMPCURVE").isValid();
        }
        check (okay, "schemas 1-5 retain supported values and continue serializing as legacy schema 5");

        // Preserve a future schema's version marker when retaining its
        // recognized parameters, while still refusing unknown curve formats.
        SideChainAudioProcessor future;
        load (future, legacy (99));
        const auto futureSaved = save (future);
        check ((int) futureSaved.getProperty ("stateVersion", juce::var (0)) == 99
                   && future.getCurveStateMode() == sid::curve::StateMode::legacy,
               "future schema version is retained with legacy fallback");
    }

    {
        const auto* preset = sid::presets::findByName ("Classic Kick");
        SideChainAudioProcessor source, restored;
        const bool applied = preset && source.applyFactoryPreset (preset->name);
        const auto state = save (source); load (restored, state);
        check (applied && std::abs (value (restored, "sidechainAmount") - 75.0f) < 0.01f
            && std::abs (value (restored, "release") - 150.0f) < 0.1f
            && std::abs (value (restored, "duckLength") - 250.0f) < 0.1f
            && restored.getCurveStateMode() == sid::curve::StateMode::pumpCurve
            && same (restored.getPumpCurve(), preset->pumpCurve),
            "schema-6 preset save/restore preserves curve and all established parameter values");
    }

    {
        auto missing = schema6 (sid::curve::PumpCurve());
        for (int i = missing.getNumChildren(); --i >= 0;)
            if (missing.getChild (i).hasType ("PUMPCURVE")) missing.removeChild (i, nullptr);
        SideChainAudioProcessor noCurve; load (noCurve, missing); const auto noState = save (noCurve);
        auto malformed = schema6 (sid::curve::PumpCurve());
        auto node = malformed.getChildWithName ("PUMPCURVE").getChild (1);
        node.setProperty ("y", std::numeric_limits<double>::infinity(), nullptr);
        SideChainAudioProcessor bad; load (bad, malformed); const auto badState = save (bad);
        check (noCurve.getCurveStateMode() == sid::curve::StateMode::legacy
            && bad.getCurveStateMode() == sid::curve::StateMode::legacy
            && std::abs (value (noCurve, "sidechainAmount") - 67.3f) < 0.06f
            && std::abs (value (noCurve, "release") - 333.0f) < 0.1f
            && std::abs (value (noCurve, "duckLength") - 612.0f) < 0.1f
            && std::abs (value (bad, "sidechainOffset") + 12.0f) < 0.1f
            && (int) noState.getProperty ("stateVersion", juce::var (0)) == 5
            && ! noState.getChildWithName ("PUMPCURVE").isValid()
            && ! badState.getChildWithName ("PUMPCURVE").isValid(),
            "missing/malformed schema-6 curve falls back without parameter loss or stale curve data");
    }

    {
        SideChainAudioProcessor p;
        const sid::curve::PumpCurve defaults;
        const sid::curve::Point custom[] = {{0.0,1.0},{0.2,0.2},{0.7,0.8},{1.0,1.0}};
        const bool set = p.setPumpCurvePoints (custom, 4);
        p.applyFactoryPreset ("Full Beat"); p.applyDefaultPreset(); const auto state = save (p);
        sid::curve::PumpCurve stored;
        check (set && p.getCurveStateMode() == sid::curve::StateMode::pumpCurve
            && same (p.getPumpCurve(), defaults) && extract (state, stored) && same (stored, defaults)
            && (int) state.getProperty ("stateVersion", juce::var (0)) == 6
            && p.getCurrentPresetDisplayName() == "Default",
            "explicit default/reset restores canonical valid curve and deterministic schema 6 state");
    }

    std::printf ("%d/%d checks passed. %s\n", checks - failures, checks,
        failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
