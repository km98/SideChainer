#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <limits>
#include <vector>

namespace
{
    int checks = 0;
    int failures = 0;

    void check (bool ok, const char* label)
    {
        ++checks;
        std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (! ok) ++failures;
    }

    class TestPlayHead final : public juce::AudioPlayHead
    {
    public:
        bool getCurrentPosition (CurrentPositionInfo& result) override
        {
            result = CurrentPositionInfo{};
            result.isPlaying = true;
            result.bpm = bpm;
            result.timeInSamples = sample;
            result.ppqPosition = sample / (60.0 / bpm * sampleRate);
            return true;
        }

        double bpm = 60.0;
        double sampleRate = 48000.0;
        long long sample = 0;
    };

    struct Render
    {
        std::vector<float> gain;
        int triggerCount = 0;
    };

    Render render (SideChainAudioProcessor& processor, double bpm, int samples)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 256;
        auto* playHead = new TestPlayHead();
        playHead->bpm = bpm;
        playHead->sampleRate = sampleRate;
        processor.setPlayHead (playHead);
        processor.prepareToPlay (sampleRate, blockSize);

        Render result;
        result.gain.reserve ((std::size_t) samples);
        const int latency = processor.getLatencySamples();
        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;
        for (int start = 0; start < samples; start += blockSize)
        {
            const int count = juce::jmin (blockSize, samples - start);
            block.setSize (2, count, false, false, true);
            for (int i = 0; i < count; ++i)
            {
                block.setSample (0, i, 0.25f);
                block.setSample (1, i, 0.25f);
            }
            playHead->sample = start;
            processor.processBlock (block, midi);
            for (int i = 0; i < count; ++i)
            {
                const int absolute = start + i;
                const float expectedInput = absolute >= latency ? 0.25f : 0.0f;
                const float gain = expectedInput > 0.0f
                                 ? block.getSample (0, i) / expectedInput : 1.0f;
                result.gain.push_back (gain);
            }
        }
        result.triggerCount = processor.duckEngineForTest().getTriggerCount();
        return result;
    }

    void setFloatParam (SideChainAudioProcessor& processor, const char* id, float value)
    {
        if (auto* parameter = processor.getParameters().getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    double minGain (const Render& render, int first, int last)
    {
        double result = 1.0;
        for (int i = first; i < last && i < (int) render.gain.size(); ++i)
            result = std::min (result, (double) render.gain[(std::size_t) i]);
        return result;
    }

    bool finiteBounded (const Render& render)
    {
        for (const auto gain : render.gain)
            if (! std::isfinite (gain) || gain < 0.0f || gain > 1.0f)
                return false;
        return true;
    }

    const sid::curve::Point kEarlyCurve[] = {
        { 0.0, 1.0 }, { 0.025, 0.06309573444801933 },
        { 0.12, 0.06309573444801933 }, { 0.65, 0.80 }, { 1.0, 1.0 }
    };
    const sid::curve::Point kLateCurve[] = {
        { 0.0, 1.0 }, { 0.18, 0.92 }, { 0.42, 0.06309573444801933 },
        { 0.70, 0.25 }, { 1.0, 1.0 }
    };

    bool installCurveThroughState (SideChainAudioProcessor& target,
                                  const sid::curve::Point* points, std::size_t count)
    {
        SideChainAudioProcessor source;
        if (! source.setPumpCurvePoints (points, count))
            return false;
        juce::MemoryBlock state;
        source.getStateInformation (state);
        target.setStateInformation (state.getData(), (int) state.getSize());
        return target.getCurveStateMode() == sid::curve::StateMode::pumpCurve;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::puts ("PumpCurve Phase D DSP integration tests");

    // The model owns deterministic bounded interpolation and SMOOTH behavior.
    {
        sid::curve::PumpCurve curve;
        const auto low = curve.evaluate (0.055, 0.0);
        const auto high = curve.evaluate (0.055, 1.0);
        check (std::isfinite (low) && low >= 0.0 && low <= 1.0
                   && std::isfinite (high) && high >= 0.0 && high <= 1.0,
               "model evaluation remains finite and bounded");
        check (std::abs (low - high) > 1.0e-4,
               "SMOOTH changes the model interpolation deterministically");
        check (curve.evaluate (-1.0, 0.5) == 1.0
                   && curve.evaluate (2.0, 0.5) == 1.0
                   && curve.evaluate (std::numeric_limits<double>::quiet_NaN(), 0.5) == 1.0,
               "out-of-domain and invalid evaluation inputs fail safely");
    }

    // Persisted schema 6 point data drives the actual processor envelope.
    {
        SideChainAudioProcessor early, late;
        check (installCurveThroughState (early, kEarlyCurve, 5)
                   && installCurveThroughState (late, kLateCurve, 5),
               "schema 6 serialized PumpCurve states load in authoritative curve mode");
        auto a = render (early, 60.0, 40000);
        auto b = render (late, 60.0, 40000);
        const int sample = 4800; // 100 ms into the first sample-accurate cycle
        check (a.triggerCount == 1 && b.triggerCount == 1
                   && std::abs (a.gain[(std::size_t) sample] - b.gain[(std::size_t) sample]) > 0.08f,
               "distinct PumpCurve shapes produce distinct DSP envelope output");
        check (finiteBounded (a) && finiteBounded (b),
               "curve-driven DSP gains are finite and remain in [0, 1]");

        SideChainAudioProcessor schema6WithoutCurve;
        juce::ValueTree missingCurve ("PARAMS");
        missingCurve.setProperty ("stateVersion", 6, nullptr);
        auto amount = juce::ValueTree ("PARAM");
        amount.setProperty ("id", "sidechainAmount", nullptr);
        amount.setProperty ("value", 75.0, nullptr);
        missingCurve.addChild (amount, -1, nullptr);
        juce::MemoryBlock missingBytes;
        juce::MemoryOutputStream missingStream (missingBytes, false);
        missingCurve.writeToStream (missingStream);
        schema6WithoutCurve.setStateInformation (missingBytes.getData(), (int) missingBytes.getSize());
        check (schema6WithoutCurve.getCurveStateMode() == sid::curve::StateMode::legacy
                   && finiteBounded (render (schema6WithoutCurve, 60.0, 40000)),
               "schema 6 without curve retains deterministic legacy envelope fallback");
    }

    // Amount remains depth control; TIME scales the authored cycle duration.
    {
        SideChainAudioProcessor shallow, deep;
        installCurveThroughState (shallow, kEarlyCurve, 5);
        installCurveThroughState (deep, kEarlyCurve, 5);
        setFloatParam (shallow, "sidechainAmount", 50.0f);
        setFloatParam (deep, "sidechainAmount", 100.0f);
        const auto a = render (shallow, 60.0, 40000);
        const auto b = render (deep, 60.0, 40000);
        check (minGain (b, 1440, 4000) + 0.08 < minGain (a, 1440, 4000),
               "Amount changes PumpCurve ducking depth while preserving curve shape");

        SideChainAudioProcessor shortTime, longTime;
        installCurveThroughState (shortTime, kEarlyCurve, 5);
        installCurveThroughState (longTime, kEarlyCurve, 5);
        setFloatParam (shortTime, "duckLength", 200.0f);
        setFloatParam (longTime, "duckLength", 600.0f);
        const auto shortRender = render (shortTime, 60.0, 44000);
        const auto longRender = render (longTime, 60.0, 44000);
        check (shortRender.gain[16000] > longRender.gain[16000] + 0.10f,
               "TIME/duckLength scales normalized curve-cycle duration");
    }

    // SMOOTH parameter affects the audible evaluated shape, not trigger time.
    {
        SideChainAudioProcessor linear, smoothed;
        installCurveThroughState (linear, kLateCurve, 5);
        installCurveThroughState (smoothed, kLateCurve, 5);
        setFloatParam (linear, "smooth", 0.0f);
        setFloatParam (smoothed, "smooth", 100.0f);
        const auto a = render (linear, 60.0, 40000);
        const auto b = render (smoothed, 60.0, 40000);
        double greatestDifference = 0.0;
        for (std::size_t i = 1500; i < 18000; ++i)
            greatestDifference = std::max (greatestDifference,
                std::abs ((double) a.gain[i] - (double) b.gain[i]));
        check (greatestDifference > 0.01 && a.triggerCount == b.triggerCount,
               "SMOOTH alters envelope interpolation without changing BeatScheduler timing");
    }

    // BeatScheduler remains the source of repeated sample-accurate triggers.
    {
        SideChainAudioProcessor repeated;
        installCurveThroughState (repeated, kEarlyCurve, 5);
        const auto result = render (repeated, 120.0, 104000);
        check (result.triggerCount == 5,
               "repeated PumpCurve cycles retain the scheduler's exact five-beat count");
        check (finiteBounded (result),
               "repeated curve cycles remain finite and bounded through retriggers");
    }

    // Legacy schemas and malformed schema 6 curves stay on the legacy engine.
    {
        const auto loadLegacy = [] (SideChainAudioProcessor& processor, int version, double releaseMs)
        {
            juce::ValueTree state ("PARAMS");
            state.setProperty ("stateVersion", version, nullptr);
            const struct { const char* id; double value; } values[] = {
                { "sidechainAmount", 100.0 }, { "release", releaseMs },
                { "duckLength", 300.0 }, { "sidechainOffset", 0.0 }
            };
            for (const auto& value : values)
            {
                juce::ValueTree parameter ("PARAM");
                parameter.setProperty ("id", value.id, nullptr);
                parameter.setProperty ("value", value.value, nullptr);
                state.addChild (parameter, -1, nullptr);
            }
            juce::MemoryBlock bytes;
            juce::MemoryOutputStream stream (bytes, false);
            state.writeToStream (stream);
            processor.setStateInformation (bytes.getData(), (int) bytes.getSize());
        };

        SideChainAudioProcessor legacyShort, legacyLong;
        loadLegacy (legacyShort, 5, 50.0);
        loadLegacy (legacyLong, 5, 1000.0);
        const auto a = render (legacyShort, 60.0, 40000);
        const auto b = render (legacyLong, 60.0, 40000);
        const int earlyTailSample = 1440 + (int) (0.08 * 48000.0);
        check (legacyShort.getCurveStateMode() == sid::curve::StateMode::legacy
                   && std::abs (minGain (a, 1440, 8000) - minGain (b, 1440, 8000)) < 1.0e-4
                   && a.gain[(std::size_t) earlyTailSample] > b.gain[(std::size_t) earlyTailSample] + 0.05f,
               "legacy schema 5 retains Amount depth and Release tail-shape behavior");

        SideChainAudioProcessor malformed;
        juce::ValueTree state ("PARAMS");
        state.setProperty ("stateVersion", 6, nullptr);
        juce::ValueTree curve ("PUMPCURVE");
        curve.setProperty ("pointCount", 3, nullptr);
        const sid::curve::Point invalid[] = {
            { 0.0, 1.0 }, { 0.5, std::numeric_limits<double>::infinity() }, { 1.0, 1.0 }
        };
        for (const auto& item : invalid)
        {
            juce::ValueTree point ("POINT");
            point.setProperty ("x", item.x, nullptr);
            point.setProperty ("y", item.y, nullptr);
            curve.addChild (point, -1, nullptr);
        }
        state.addChild (curve, -1, nullptr);
        juce::MemoryBlock bytes;
        juce::MemoryOutputStream stream (bytes, false);
        state.writeToStream (stream);
        malformed.setStateInformation (bytes.getData(), (int) bytes.getSize());
        const auto fallback = render (malformed, 60.0, 40000);
        check (malformed.getCurveStateMode() == sid::curve::StateMode::legacy
                   && fallback.triggerCount == 1 && finiteBounded (fallback),
               "malformed schema 6 PumpCurve safely falls back to bounded legacy DSP");
    }

    std::printf ("%d/%d checks passed. %s\n", checks - failures, checks,
                 failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
