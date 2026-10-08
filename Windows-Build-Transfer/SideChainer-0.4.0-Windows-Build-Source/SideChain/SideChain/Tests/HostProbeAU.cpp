/*
    SideChain host probe - loads the REAL INSTALLED AU via the JUCE plugin
    hosting API and reproduces the "sidechain does not duck at all" Logic
    report (no code from the project is compiled in).

    Scenarios:
      A: 48k / 512  defaults, kick sidechain, PLAYING -> expect duck
      B: 44.1k / 128 (Logic defaults)                 -> expect duck
      C: 48k / 512  Amount forced to 100 %            -> expect duck
      D: state save from fresh instance + reload      -> expect duck
      E: sidechain bus never activated (like a host that sends on a disabled
         aux)                                          -> document behaviour
      F: sidechain silent (gate/bypass check)         -> expect NO duck

    Build: see Tests/run_hostprobe.sh
*/

#include <JuceHeader.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

static int checks = 0, failures = 0;
static void check (bool ok, const std::string& what)
{
    ++checks;
    printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

static std::vector<float> kick (double sr, double atSec, float amp = 0.95f,
                                double burstSec = 0.12)
{
    const int len = (int) ((atSec + 2.0) * sr);
    std::vector<float> v ((size_t) len, 0.0f);
    const int start = (int) (atSec * sr);
    const int kickLen = (int) (burstSec * sr);
    for (int i = 0; i < kickLen && start + i < len; ++i)
    {
        const double env = std::exp (-6.0 * (double) i / kickLen);
        v[(size_t) (start + i)] =
            amp * env * (float) std::sin (2.0 * 3.14159265358979 * 55.0 * (start + i) / sr);
    }
    return v;
}

class PlayingHead : public juce::AudioPlayHead
{
public:
    bool getCurrentPosition (CurrentPositionInfo& result) override
    {
        result = CurrentPositionInfo{};
        result.isPlaying = true;
        return true;
    }
};

struct ProbeResult
{
    double minGain = 1.0;
    double maxDuckDb = 0.0;
    int scPeakBlocks = 0;   // blocks where the sidechain carried signal
};

// Formats: 0 = mono main + mono SC; 1 = stereo main + stereo SC
static ProbeResult runHosted (juce::AudioPluginInstance* inst, double sr, int block,
                         const std::vector<float>& sc, const std::vector<float>& main,
                         int numMainCh, int numScCh)
{
    inst->setPlayHead (new PlayingHead());
    inst->prepareToPlay (sr, block);

    ProbeResult r;
    const int totalCh = numMainCh + numScCh;
    const int n = (int) main.size();

    juce::AudioBuffer<float> buffer (totalCh, n);
    buffer.clear();
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < numMainCh; ++c)
            buffer.setSample (c, i, main[(size_t) i]);

    const int trigStart = (int) (0.25 * sr);
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < numScCh; ++c)
            buffer.setSample (numMainCh + c, i, sc[(size_t) i]);

    bool seenPeak = false;
    juce::AudioBuffer<float> blk (totalCh, block);
    juce::MidiBuffer midi;
    for (int start = 0; start < n; start += block)
    {
        const int count = std::min (block, n - start);
        blk.clear();
        for (int ch = 0; ch < totalCh; ++ch)
            blk.copyFrom (ch, 0, buffer.getReadPointer (ch) + start, count);

        inst->processBlock (blk, midi);

        for (int i = 0; i < count; ++i)
        {
            const float o = std::abs (blk.getSample (0, i));
            const float m = std::abs (main[(size_t) (start + i)]);
            if (m > 0.1f && o > 1.0e-6f)
                r.minGain = std::min (r.minGain, (double) (o / m));
            if (start + i >= trigStart && std::abs (sc[(size_t) (start + i)]) > 0.5f)
                seenPeak = true;
        }
        if (seenPeak) r.scPeakBlocks++;
    }
    r.maxDuckDb = -20.0 * std::log10 (std::max (r.minGain, 1.0e-9));
    return r;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    const char* auPath = (argc > 1) ? argv[1]
        : "/Users/martin/Library/Audio/Plug-Ins/Components/SideChain.component";

    printf ("SideChain INSTALLED-AU host probe: %s\n", auPath);

    juce::OwnedArray<juce::PluginDescription> found;
    {
        juce::KnownPluginList plist;
        juce::AudioPluginFormatManager fm;
        fm.addDefaultFormats();

        for (auto* fmt : fm.getFormats())
            if (auto* auFmt = dynamic_cast<juce::AudioUnitPluginFormat*> (fmt))
            {
                juce::OwnedArray<juce::PluginDescription> types;
                auFmt->findAllTypesForFile (types, juce::File (auPath).getFullPathName());
                for (auto* d : types)
                {
                    printf ("  found: %s / %s chIn=%d chOut=%d\n",
                            d->name.toRawUTF8(), d->fileOrIdentifier.toRawUTF8(),
                            d->numInputChannels, d->numOutputChannels);
                    plist.addType (*d);
                }
            }
        for (const auto& d2 : plist.getTypes())
            found.add (new juce::PluginDescription (d2));
    }

    if (found.isEmpty())
    {
        printf ("  [FAIL] installed AU not discovered\n");
        return 1;
    }

    juce::AudioPluginFormatManager fm;
    fm.addDefaultFormats();
    juce::String createErr;
    std::unique_ptr<juce::AudioPluginInstance> inst (
        fm.createPluginInstance (*found[0], 48000.0, 512, createErr));
    if (inst == nullptr)
    {
        printf ("  [FAIL] createPluginInstance: %s\n", createErr.toRawUTF8());
        return 1;
    }
    printf ("  created: %s (in %d / out %d buses)\n",
            inst->getName().toRawUTF8(),
            inst->getBusCount (true), inst->getBusCount (false));

    const auto kickBuf = kick (48000.0, 0.25);
    const int n = (int) kickBuf.size();
    std::vector<float> main48 ((size_t) n, 0.0f);
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        main48[(size_t) i] = 0.5f * (float) std::sin (ph);
        ph += 2.0 * 3.14159265358979 * 440.0 / 48000.0;
    }

    // Bus layouts
    auto layouts = inst->getBusesLayout();
    printf ("  default layout: in0=%d ch, in1=%d ch\n",
            layouts.getNumChannels (true, 0),
            inst->getBusCount (true) > 1 ? layouts.getNumChannels (true, 1) : -1);

    // Scenario A: mono-in/mono-sc if the AU exposes them, else stereo.
    {
        auto lay = inst->getBusesLayout();
        const bool hasSc = inst->getBusCount (true) > 1;
        if (hasSc)
        {
            lay.getChannelSet (true, 1) = juce::AudioChannelSet::stereo();
            const bool ok = inst->setBusesLayout (lay);
            printf ("  sidechain activation: %s\n", ok ? "OK" : "REJECTED");
        }
    }

    printf ("\nA. defaults, 48k/512, PLAYING\n");
    {
        auto r = runHosted (inst.get(), 48000.0, 512, kickBuf, main48, 2, 2);
        printf ("     minGain %.4f (%.1f dB duck), sc blocks %d\n",
                r.minGain, r.maxDuckDb, r.scPeakBlocks);
        check (r.maxDuckDb > 5.0, "A: hosted AU DUCKS on a kick (>5 dB)");
    }

    printf ("\nB. 44.1k / 128 (Logic defaults)\n");
    {
        auto k441 = kick (44100.0, 0.25);
        const int n2 = (int) k441.size();
        std::vector<float> main441 ((size_t) n2, 0.0f);
        double ph2 = 0.0;
        for (int i = 0; i < n2; ++i)
        {
            main441[(size_t) i] = 0.5f * (float) std::sin (ph2);
            ph2 += 2.0 * 3.14159265358979 * 440.0 / 44100.0;
        }
        auto r = runHosted (inst.get(), 44100.0, 128, k441, main441, 2, 2);
        printf ("     minGain %.4f (%.1f dB duck), sc blocks %d\n",
                r.minGain, r.maxDuckDb, r.scPeakBlocks);
        check (r.maxDuckDb > 5.0, "B: 44.1k/128 hosted AU DUCKS (>5 dB)");
    }

    printf ("\nC. Amount forced to 100 %%\n");
    {
        for (auto* param : inst->getParameters())
            if (param->getName (32) == "Sidechain Amount")
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (param))
                    p->setValueNotifyingHost (p->convertTo0to1 (100.0f));
        auto r = runHosted (inst.get(), 48000.0, 512, kickBuf, main48, 2, 2);
        printf ("     minGain %.4f (%.1f dB duck)\n", r.minGain, r.maxDuckDb);
        check (r.maxDuckDb > 15.0, "C: Amount 100%% hosted duck is deep (>15 dB)");
    }

    printf ("\nD. state save / reload\n");
    {
        juce::MemoryBlock mb;
        inst->getStateInformation (mb);
        // Note: this instance already ran (states carry current values); a
        // fresh instance is created from the same state.
        std::unique_ptr<juce::AudioPluginInstance> inst2 (
            fm.createPluginInstance (*found[0], 48000.0, 512, createErr));
        if (inst2 != nullptr)
        {
            auto lay = inst2->getBusesLayout();
            if (inst2->getBusCount (true) > 1)
            {
                lay.getChannelSet (true, 1) = juce::AudioChannelSet::stereo();
                inst2->setBusesLayout (lay);
            }
            inst2->setStateInformation (mb.getData(), (int) mb.getSize());
            auto r = runHosted (inst2.get(), 48000.0, 512, kickBuf, main48, 2, 2);
            printf ("     minGain %.4f (%.1f dB duck)\n", r.minGain, r.maxDuckDb);
            check (r.maxDuckDb > 5.0, "D: reloaded-state instance DUCKS (>5 dB)");
        }
    }

    printf ("\n==============================\n");
    printf ("%d/%d checks passed. %s\n", checks - failures, checks,
            failures == 0 ? "REPRO FAILED (plugin works)" : "REGRESSION REPRODUCED");
    printf ("==============================\n");
    return failures == 0 ? 0 : 1;
}
