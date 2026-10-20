#!/bin/bash
# UI snapshot tool: renders the real editor + INFO page states to PNG (1x and 2x).
# Usage: ./Tests/ui_snapshot.sh [--out-dir DIR] [--png-dir DIR]
# JUCE location: SIDECHAIN_JUCE_ROOT=/path/to/HISE/JUCE (defaults to the
# standard sibling layout, see validation_common.sh).
set -u
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

# shellcheck source=validation_common.sh
source "$SCRIPT_DIR/validation_common.sh"

OUT_DIR="$SCRIPT_DIR/.build"
PNG_DIR="$SCRIPT_DIR/.build/ui-snapshots"
while [ $# -gt 0 ]; do
    case "$1" in
        --out-dir) shift; [ $# -gt 0 ] || { echo "ERROR: --out-dir needs a directory" >&2; exit 2; }; OUT_DIR="$1" ;;
        --png-dir) shift; [ $# -gt 0 ] || { echo "ERROR: --png-dir needs a directory" >&2; exit 2; }; PNG_DIR="$1" ;;
        *) echo "ERROR: unknown argument '$1'" >&2; exit 2 ;;
    esac
    shift
done
mkdir -p "$OUT_DIR" || exit 1

JUCE_MODULES="$(resolve_juce_modules)" || exit 1
FLAGS="-std=c++17 -O1 -DNDEBUG=1 -Wno-deprecated-declarations -Wno-deprecated -Wno-unavailable-declarations -mmacosx-version-min=11.0 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DSIDECHAIN_HEADLESS_TEST=1 -DJucePlugin_Build_VST3=1 -DJucePlugin_Build_AU=1 -DJucePlugin_Name=\"SideChain\""
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
    UISnapshot.cpp $GUI_FRAMEWORKS -o "$OUT_DIR/UISnapshot" 2>&1 | tee "$OUT_DIR/ui_snapshot_compile.log"
if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "[FAIL] compile UISnapshot (see $OUT_DIR/ui_snapshot_compile.log)"
    exit 1
fi
mkdir -p "$PNG_DIR" || exit 1
"$OUT_DIR/UISnapshot" --out-dir "$(cd "$PNG_DIR" && pwd)"
