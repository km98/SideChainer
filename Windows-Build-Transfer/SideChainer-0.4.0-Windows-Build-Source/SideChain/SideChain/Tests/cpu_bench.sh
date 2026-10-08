#!/bin/bash
# Phase 7A CPU sanity benchmark (same recipe as the audio-only suites).
cd "$(dirname "$0")" || exit 1
JUCE_MODULES="/Users/martin/Documents/HISE/JUCE/modules"
FLAGS="-std=c++17 -O2 -DNDEBUG=1 -Wno-deprecated-declarations -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1"
TUS_AUDIO="../JuceLibraryCode/include_juce_core.cpp ../JuceLibraryCode/include_juce_audio_basics.cpp ../JuceLibraryCode/include_juce_audio_formats.cpp ../JuceLibraryCode/include_juce_events.cpp ../JuceLibraryCode/include_juce_data_structures.cpp ../JuceLibraryCode/include_juce_graphics.cpp ../JuceLibraryCode/include_juce_dsp.cpp"
FRAMEWORKS="-framework Cocoa -framework Accelerate -framework CoreMIDI -framework CoreFoundation -framework IOKit -framework QuartzCore -framework AudioToolbox -framework CoreAudio"

clang++ $FLAGS -x objective-c++ \
    -I../JuceLibraryCode -I"$JUCE_MODULES" \
    $TUS_AUDIO CPUBench.cpp $FRAMEWORKS -o CPUBench 2>&1 | grep -E "error:" && { echo "[FAIL] compile"; exit 1; }

./CPUBench
