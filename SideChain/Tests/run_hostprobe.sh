#!/bin/bash
# Build + run the installed-AU host probe (real Logic-like hosting path).
# Usage: ./Tests/run_hostprobe.sh [probe arguments...]
# JUCE location: SIDECHAIN_JUCE_ROOT=/path/to/HISE/JUCE (defaults to the
# standard sibling layout, see validation_common.sh).
set -u
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

# shellcheck source=validation_common.sh
source "$SCRIPT_DIR/validation_common.sh"

OUT_DIR="${SIDECHAIN_TEST_OUT_DIR:-$SCRIPT_DIR/.build}"
mkdir -p "$OUT_DIR" || exit 1

JUCE_MODULES="$(resolve_juce_modules)" || exit 1
FLAGS="-std=c++17 -O1 -DNDEBUG=1 -Wno-deprecated-declarations -Wno-unavailable-declarations -mmacosx-version-min=11.0 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_MODAL_LOOPS_PERMITTED=1 -DJUCE_PLUGINHOST_AU=1"
TUS_AUDIO="../JuceLibraryCode/include_juce_core.cpp ../JuceLibraryCode/include_juce_audio_basics.cpp ../JuceLibraryCode/include_juce_audio_formats.cpp ../JuceLibraryCode/include_juce_events.cpp ../JuceLibraryCode/include_juce_data_structures.cpp ../JuceLibraryCode/include_juce_graphics.cpp ../JuceLibraryCode/include_juce_dsp.cpp"
TUS_GUI="../JuceLibraryCode/include_juce_gui_basics.cpp ../JuceLibraryCode/include_juce_gui_extra.cpp ../JuceLibraryCode/include_juce_audio_processors.cpp"
FRAMEWORKS="-framework Cocoa -framework Accelerate -framework CoreMIDI -framework CoreFoundation -framework IOKit -framework QuartzCore -framework AudioToolbox -framework CoreAudio"
GUI_FRAMEWORKS="$FRAMEWORKS -framework AVFoundation -framework CoreVideo -framework CoreMedia -framework Metal -framework MetalKit -framework Carbon -framework AudioUnit -weak_framework CoreAudioKit -DJUCE_PLUGINHOST_AU_DISABLE_AUV3=1"

PROBE="$OUT_DIR/HostProbeAU"
rm -f "$PROBE"

# shellcheck disable=SC2086
clang++ $FLAGS -x objective-c++ \
    -I../JuceLibraryCode -I"$JUCE_MODULES" \
    $TUS_AUDIO $TUS_GUI \
    HostProbeAU.cpp $GUI_FRAMEWORKS -o "$PROBE" 2>&1 | tee "$OUT_DIR/hostprobe_compile.log" | grep -E "error" | head -20
compile_status="${PIPESTATUS[0]}"
if [ "$compile_status" -ne 0 ] || [ ! -f "$PROBE" ]; then
    echo "[FAIL] compile HostProbeAU (see $OUT_DIR/hostprobe_compile.log)"
    exit 1
fi
"$PROBE" "$@"
