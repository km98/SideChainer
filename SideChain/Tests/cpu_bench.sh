#!/bin/bash
# Phase 7A CPU sanity benchmark (same recipe as the audio-only suites).
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
FLAGS="-std=c++17 -O2 -DNDEBUG=1 -Wno-deprecated-declarations -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1"
TUS_AUDIO="../JuceLibraryCode/include_juce_core.cpp ../JuceLibraryCode/include_juce_audio_basics.cpp ../JuceLibraryCode/include_juce_audio_formats.cpp ../JuceLibraryCode/include_juce_events.cpp ../JuceLibraryCode/include_juce_data_structures.cpp ../JuceLibraryCode/include_juce_graphics.cpp ../JuceLibraryCode/include_juce_dsp.cpp"
FRAMEWORKS="-framework Cocoa -framework Accelerate -framework CoreMIDI -framework CoreFoundation -framework IOKit -framework QuartzCore -framework AudioToolbox -framework CoreAudio"

BENCH="$OUT_DIR/CPUBench"
rm -f "$BENCH"

# shellcheck disable=SC2086
clang++ $FLAGS -x objective-c++ \
    -I../JuceLibraryCode -I"$JUCE_MODULES" \
    $TUS_AUDIO CPUBench.cpp $FRAMEWORKS -o "$BENCH" 2>&1 | tee "$OUT_DIR/cpubench_compile.log" | grep -E "error:" | head -20
compile_status="${PIPESTATUS[0]}"
if [ "$compile_status" -ne 0 ] || [ ! -f "$BENCH" ]; then
    echo "[FAIL] compile CPUBench (see $OUT_DIR/cpubench_compile.log)"
    exit 1
fi

"$BENCH"
