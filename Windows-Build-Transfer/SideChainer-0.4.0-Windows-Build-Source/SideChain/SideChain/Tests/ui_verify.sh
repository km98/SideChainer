#!/bin/bash
# UI verification harness: real editor + InfoPage, offscreen.
cd "$(dirname "$0")" || exit 1
JUCE_MODULES="/Users/martin/Documents/HISE/JUCE/modules"
FLAGS="-std=c++17 -O1 -DNDEBUG=1 -Wno-deprecated-declarations -Wno-deprecated -Wno-unavailable-declarations -mmacosx-version-min=11.0 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DSIDECHAIN_HEADLESS_TEST=1 -DJucePlugin_Build_VST3=1 -DJucePlugin_Build_AU=1 -DJucePlugin_Name=\"SideChain\" -DJucePlugin_VersionString=\"0.4.0\" -DJucePlugin_VersionCode=0x400 -DJucePlugin_Version=0.4.0"
TUS_AUDIO="../JuceLibraryCode/include_juce_core.cpp ../JuceLibraryCode/include_juce_audio_basics.cpp ../JuceLibraryCode/include_juce_audio_formats.cpp ../JuceLibraryCode/include_juce_events.cpp ../JuceLibraryCode/include_juce_data_structures.cpp ../JuceLibraryCode/include_juce_graphics.cpp ../JuceLibraryCode/include_juce_dsp.cpp"
TUS_GUI="../JuceLibraryCode/include_juce_gui_basics.cpp ../JuceLibraryCode/include_juce_gui_extra.cpp ../JuceLibraryCode/include_juce_audio_processors.cpp"
FRAMEWORKS="-framework Cocoa -framework Accelerate -framework CoreMIDI -framework CoreFoundation -framework IOKit -framework QuartzCore -framework AudioToolbox -framework CoreAudio"
GUI_FRAMEWORKS="$FRAMEWORKS -framework AVFoundation -framework CoreVideo -framework CoreMedia -framework Metal -framework MetalKit -framework Carbon"

# shellcheck disable=SC2086
clang++ $FLAGS -x objective-c++ \
    -I../JuceLibraryCode -I"$JUCE_MODULES" \
    $TUS_AUDIO $TUS_GUI \
    ../Source/PluginProcessor.cpp ../Source/MusicProdAuth.cpp \
    ../Source/GraphComponent.cpp ../Source/InfoPage.cpp \
    ../Source/PluginEditor.cpp ../JuceLibraryCode/BinaryData.cpp \
    UIVerifyHarness.cpp $GUI_FRAMEWORKS -o UIVerifyHarness 2>&1 | tee ui_compile.log
if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "[FAIL] compile UIVerifyHarness"
    exit 1
fi
./UIVerifyHarness
