#!/bin/bash
# Build + run the installed-AU host probe (real Logic-like hosting path).
cd "$(dirname "$0")" || exit 1
JUCE_MODULES="/Users/martin/Documents/HISE/JUCE/modules"
FLAGS="-std=c++17 -O1 -DNDEBUG=1 -Wno-deprecated-declarations -Wno-unavailable-declarations -mmacosx-version-min=11.0 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_MODAL_LOOPS_PERMITTED=1 -DJUCE_PLUGINHOST_AU=1"
TUS_AUDIO="../JuceLibraryCode/include_juce_core.cpp ../JuceLibraryCode/include_juce_audio_basics.cpp ../JuceLibraryCode/include_juce_audio_formats.cpp ../JuceLibraryCode/include_juce_events.cpp ../JuceLibraryCode/include_juce_data_structures.cpp ../JuceLibraryCode/include_juce_graphics.cpp ../JuceLibraryCode/include_juce_dsp.cpp"
TUS_GUI="../JuceLibraryCode/include_juce_gui_basics.cpp ../JuceLibraryCode/include_juce_gui_extra.cpp ../JuceLibraryCode/include_juce_audio_processors.cpp"
FRAMEWORKS="-framework Cocoa -framework Accelerate -framework CoreMIDI -framework CoreFoundation -framework IOKit -framework QuartzCore -framework AudioToolbox -framework CoreAudio"
GUI_FRAMEWORKS="$FRAMEWORKS -framework AVFoundation -framework CoreVideo -framework CoreMedia -framework Metal -framework MetalKit -framework Carbon -framework AudioUnit -weak_framework CoreAudioKit -DJUCE_PLUGINHOST_AU_DISABLE_AUV3=1"

# shellcheck disable=SC2086
clang++ $FLAGS -x objective-c++ \
    -I../JuceLibraryCode -I"$JUCE_MODULES" \
    $TUS_AUDIO $TUS_GUI \
    HostProbeAU.cpp $GUI_FRAMEWORKS -o HostProbeAU 2>&1 | grep -E "error" | head -20
if [ ! -f HostProbeAU ]; then
    echo "[FAIL] compile HostProbeAU"
    exit 1
fi
./HostProbeAU "$@"
