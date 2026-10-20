#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include <cstdio>
#include <limits>

static int checks = 0, failures = 0;
static void check (bool ok, const char* text)
{ ++checks; std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", text); if (! ok) ++failures; }
static void set (SideChainAudioProcessor& p, const char* id, float v)
{ auto* parameter = p.getParameters().getParameter (id); parameter->setValueNotifyingHost (parameter->convertTo0to1 (v)); }
static juce::ValueTree stateOf (SideChainAudioProcessor& p)
{ juce::MemoryBlock data; p.getStateInformation (data); return juce::ValueTree::readFromData (data.getData(), data.getSize()); }
struct Listener : juce::AudioProcessorParameter::Listener
{
    int changes = 0, starts = 0, ends = 0;
    void parameterValueChanged (int, float) override { ++changes; }
    void parameterGestureChanged (int, bool starting) override { starting ? ++starts : ++ends; }
};
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    // Independent child invocation proves persistence survives process exit.
    if (argc == 3 && juce::String (argv[1]) == "--reopen")
    {
        sid::presets::UserPresetLibrary library { juce::File (argv[2]) };
        auto entries = library.list();
        SideChainAudioProcessor p;
        return entries.size() == 1 && p.loadUserPreset (library, entries[0].file).wasOk()
            && std::abs (p.getParameters().getRawParameterValue ("sidechainAmount")->load() - 82.7f) < 0.0001f
            && p.getParameters().getRawParameterValue ("transportGate")->load() == 0.0f ? 0 : 1;
    }
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("pumpcurve-user-presets-tests", "", false);
    struct Cleanup { juce::File root; ~Cleanup() { root.deleteRecursively(); } } cleanup { root };
    sid::presets::UserPresetLibrary library (root);
    check (library.list().isEmpty(), "missing directory is an empty library");
    const auto defaultPath = sid::presets::UserPresetLibrary::defaultDirectory().getFullPathName();
#if JUCE_MAC
    check (defaultPath.endsWith ("/Library/Application Support/Music-Prod/PumpCurve/User Presets"), "macOS shared per-user location");
#else
    check (defaultPath.endsWith ("Music-Prod/PumpCurve/User Presets"), "shared per-user location");
#endif
    SideChainAudioProcessor source;
    set (source, "sidechainAmount", 82.7f); set (source, "duckLength", 612.345f);
    set (source, "release", 333.123f); set (source, "sidechainOffset", -17.0f);
    set (source, "smooth", 73.4f); set (source, "transportGate", 0.0f);
    const sid::curve::Point points[] = { {0.0, 1.0}, {0.08, 0.12}, {0.43, 0.21}, {0.73, 0.84}, {1.0, 1.0} };
    check (source.setPumpCurvePoints (points, 5), "custom curve accepted");
    const auto original = stateOf (source);
    juce::File file;
    const auto unicodeName = juce::String::fromUTF8 ("My Pump Ü");
    check (source.saveUserPreset (library, unicodeName, file).wasOk(), "save complete sound immediately after edits");
    check (file.existsAsFile() && file.getParentDirectory() == root, "persistent file in chosen per-user directory");
    check (library.list().size() == 1 && library.list()[0].name == unicodeName, "saved preset immediately discoverable");
    check (source.getCurrentUserPresetName() == unicodeName, "saved preset display follows exact sound");
    juce::String name; juce::ValueTree stored;
    check (library.read (file, name, stored).wasOk() && stored.isEquivalentTo (original), "all sound fields and curve positions round-trip exactly");
    check (! stored.hasProperty ("waveform") && stored.getNumChildren() == 7, "only six persistent parameters and curve; no waveform/meter/auth state");
    juce::MemoryBlock before; file.loadFileAsData (before);
    juce::File duplicate;
    check (source.saveUserPreset (library, unicodeName, duplicate).failed(), "duplicate refused");
    check (source.saveUserPreset (library, juce::String::fromUTF8 ("my pump ü"), duplicate).failed(), "case-insensitive duplicate refused");
    juce::MemoryBlock after; file.loadFileAsData (after);
    check (before == after, "duplicate attempts leave original bytes unchanged");
    for (const auto* invalid : { "", "../escape", "a/b", "a\\b", "name:", " trailing", "trailing ", "trailing.", ".." })
        check (source.saveUserPreset (library, invalid, duplicate).failed(), "unsafe filename/name refused");
    check (source.saveUserPreset (library, juce::String::repeatedString ("a", 81), duplicate).failed(), "overlong name refused");
    juce::ChildProcess child;
    juce::StringArray args { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(), "--reopen", root.getFullPathName() };
    check (child.start (args) && child.waitForProcessToFinish (15000) && child.getExitCode() == 0, "new process reopens and loads persisted sound");
    SideChainAudioProcessor au, vst3;
    check (au.loadUserPreset (library, file).wasOk(), "AU processor loads shared library preset");
    check (vst3.loadUserPreset (library, file).wasOk(), "VST3 processor loads same shared library preset");
    for (const auto* id : { "sidechainAmount", "duckLength", "release", "sidechainOffset", "smooth", "transportGate" })
        check (au.getParameters().getRawParameterValue (id)->load() == source.getParameters().getRawParameterValue (id)->load(), "exact stored parameter restored");
    check (stateOf (au).isEquivalentTo (original) && stateOf (vst3).isEquivalentTo (original), "both processor instances preserve exact complete state");
    Listener listener;
    auto* amount = au.getParameters().getParameter ("sidechainAmount"); amount->addListener (&listener);
    check (au.loadUserPreset (library, file).wasOk() && listener.changes > 0 && listener.starts == 1 && listener.ends == 1, "preset load notifies host and brackets automation gesture");
    amount->removeListener (&listener);
    set (au, "smooth", 44.0f);
    check (au.getCurrentUserPresetName().isEmpty(), "edited sound no longer mislabeled as saved preset");
    const auto corrupt = root.getChildFile ("corrupt.pumpcurvepreset"); corrupt.replaceWithText ("not a preset");
    check (library.list().size() == 1, "corrupt file excluded from selector");
    const auto preCorrupt = stateOf (au);
    check (au.loadUserPreset (library, corrupt).failed() && stateOf (au).isEquivalentTo (preCorrupt), "corrupt load fails without changing sound");
    auto badState = original.createCopy();
    badState.getChild (0).setProperty ("value", std::numeric_limits<double>::quiet_NaN(), nullptr);
    juce::File badFile;
    check (library.save ("Bad parameter", badState, badFile).wasOk(), "invalid parameter fixture written");
    check (au.loadUserPreset (library, badFile).failed() && stateOf (au).isEquivalentTo (preCorrupt), "NaN rejected transactionally");
    auto badCurve = original.createCopy(); badCurve.getChildWithName ("PUMPCURVE").getChild (1).setProperty ("x", -4.0, nullptr);
    check (library.save ("Bad curve", badCurve, badFile).wasOk() && au.loadUserPreset (library, badFile).failed(), "invalid curve rejected");
    auto legacy = original.createCopy(); legacy.setProperty ("stateVersion", 6, nullptr);
    for (int i = legacy.getNumChildren(); --i >= 0;)
        if (legacy.getChild (i).getProperty ("id").toString() == "transportGate") legacy.removeChild (i, nullptr);
    juce::File legacyFile;
    check (library.save ("Legacy", legacy, legacyFile).wasOk(), "legacy sound stored with versioned wrapper");
    check (au.loadUserPreset (library, legacyFile).wasOk() && au.getParameters().getRawParameterValue ("transportGate")->load() == 1.0f, "missing transport gate migrates to ON even after OFF");
    for (int v = 1; v <= 5; ++v)
    {
        auto old = legacy.createCopy(); old.setProperty ("stateVersion", v, nullptr); old.removeChild (old.getChildWithName ("PUMPCURVE"), nullptr);
        juce::File oldFile;
        check (library.save ("Legacy " + juce::String (v), old, oldFile).wasOk()
            && au.loadUserPreset (library, oldFile).wasOk() && au.getCurveStateMode() == sid::curve::StateMode::legacy,
            "older state schemas preserve legacy envelope mode");
    }
    const auto blocker = root.getChildFile ("not-a-directory"); blocker.replaceWithText ("occupied");
    sid::presets::UserPresetLibrary unwritable (blocker.getChildFile ("child"));
    check (source.saveUserPreset (unwritable, "Cannot save", duplicate).failed(), "unwritable directory returns error, no crash");
    check (! file.getSiblingFile ("escape").exists(), "no path traversal created");
    check (source.applyFactoryPreset ("Classic Kick") && source.getCurrentPresetDisplayName() == "Classic Kick", "factory presets remain independent and unchanged");
    au.loadUserPreset (library, file); vst3.loadUserPreset (library, file);
    au.prepareToPlay (48000, 256); vst3.prepareToPlay (48000, 256);
    bool sameAudio = true, ducks = false, wave = false; juce::MidiBuffer midi;
    for (int block = 0; block < 150; ++block)
    {
        juce::AudioBuffer<float> a (2, 256), b (2, 256);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) { a.setSample (c, i, 0.5f); b.setSample (c, i, 0.5f); }
        au.processBlock (a, midi); vst3.processBlock (b, midi);
        for (int i = 0; i < 256; ++i) { sameAudio &= a.getSample (0, i) == b.getSample (0, i); ducks |= a.getSample (0, i) > 0 && a.getSample (0, i) < 0.45f; }
    }
    sid::scope::WaveformFrame frame;
    while (au.waveformFifo.pop (frame)) wave |= frame.inputMax > 0 && frame.outputMax > 0;
    check (sameAudio && ducks, "shared preset produces identical real DSP audio and gate-OFF free-run ducking");
    check (wave, "live input/output waveform still produced after user preset load");
    std::printf ("\n%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
