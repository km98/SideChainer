#!/bin/bash
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/validation_common.sh"
JUCE="$(resolve_juce_modules)" || exit 1
[ $# -eq 1 ] || { echo 'Usage: user_preset_format_probe.sh OUT_DIR'; exit 2; }
OUT="$1"
mkdir -p "$OUT" || exit 1
cd "$HERE" || exit 1
clang++ -std=c++17 -O1 -DNDEBUG=1 -Wno-deprecated-declarations -Wno-unavailable-declarations \
    -mmacosx-version-min=11.0 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
    -DJUCE_PLUGINHOST_AU=1 -DJUCE_PLUGINHOST_VST3=1 -DJUCE_PLUGINHOST_AU_DISABLE_AUV3=1 \
    -I../JuceLibraryCode -I"$JUCE" -I"$JUCE/juce_audio_processors/format_types/VST3_SDK" -x objective-c++ \
    ../JuceLibraryCode/include_juce_core.cpp \
    ../JuceLibraryCode/include_juce_audio_basics.cpp ../JuceLibraryCode/include_juce_audio_formats.cpp \
    ../JuceLibraryCode/include_juce_events.cpp ../JuceLibraryCode/include_juce_data_structures.cpp \
    ../JuceLibraryCode/include_juce_graphics.cpp ../JuceLibraryCode/include_juce_dsp.cpp \
    ../JuceLibraryCode/include_juce_gui_basics.cpp ../JuceLibraryCode/include_juce_gui_extra.cpp \
    ../JuceLibraryCode/include_juce_audio_processors.cpp UserPresetFormatProbe.cpp \
    -framework Cocoa -framework Accelerate -framework CoreMIDI -framework CoreFoundation \
    -framework IOKit -framework QuartzCore -framework AudioToolbox -framework CoreAudio \
    -framework AVFoundation -framework CoreVideo -framework CoreMedia -framework Metal \
    -framework MetalKit -framework Carbon -framework AudioUnit -weak_framework CoreAudioKit \
    -o "$OUT/UserPresetFormatProbe" > "$OUT/format-probe-compile.log" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then cat "$OUT/format-probe-compile.log"; fi
exit "$rc"
