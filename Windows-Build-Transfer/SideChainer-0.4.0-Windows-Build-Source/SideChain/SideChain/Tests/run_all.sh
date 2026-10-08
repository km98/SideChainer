#!/bin/bash
# ============================================================================
# SideChain - combined regression test runner (project-local, no globals).
#
# Compiles (if needed) and runs all five suites:
#   1. DSPRegressionTests   - Phase 3 DSP regression (32 checks)
#   2. Phase4Tests          - Phase 4 parameter/state/graph (13 checks)
#   3. Phase5TimingTests    - Phase 5 true time constants (24 checks)
#   4. Phase6Tests          - Phase 6 Release parameter + Phase 7A state hardening (49 checks)
#   5. PresetTests          - Phase 7 factory preset system (28 checks)
#   6. Phase8Tests          - Phase 8 transport gate + auth state machine
#   7. TriggerDSPTests      - 0.3.0 transient-trigger DSP (detector/envelope/offset/presets)
#   8. DuckLengthTests      - 0.3.0 DUCK LENGTH + graph view modes
#   9. DetectorTests        - 0.3.0 detector matrix (Logic-level regression)
#
# Usage:  ./Tests/run_all.sh [--rebuild]
#         --rebuild forces recompilation even if binaries exist
#
# Exit code: 0 only if every suite passes.
# ============================================================================

cd "$(dirname "$0")" || exit 1

JUCE_MODULES="/Users/martin/Documents/HISE/JUCE/modules"
COMMON_FLAGS="-std=c++17 -O2 -DNDEBUG=1 -Wno-deprecated-declarations -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1"
GUI_FLAGS="-std=c++17 -O1 -DNDEBUG=1 -Wno-deprecated-declarations -Wno-deprecated -mmacosx-version-min=11.0 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1"

TUS_AUDIO="../JuceLibraryCode/include_juce_core.cpp ../JuceLibraryCode/include_juce_audio_basics.cpp ../JuceLibraryCode/include_juce_audio_formats.cpp ../JuceLibraryCode/include_juce_events.cpp ../JuceLibraryCode/include_juce_data_structures.cpp ../JuceLibraryCode/include_juce_graphics.cpp ../JuceLibraryCode/include_juce_dsp.cpp"
TUS_GUI="../JuceLibraryCode/include_juce_gui_basics.cpp ../JuceLibraryCode/include_juce_gui_extra.cpp ../JuceLibraryCode/include_juce_audio_processors.cpp"
FRAMEWORKS="-framework Cocoa -framework Accelerate -framework CoreMIDI -framework CoreFoundation -framework IOKit -framework QuartzCore -framework AudioToolbox -framework CoreAudio"
GUI_FRAMEWORKS="$FRAMEWORKS -framework AVFoundation -framework CoreVideo -framework CoreMedia -framework Metal -framework MetalKit -framework Carbon"

# Phase 6: compiles the REAL processor headlessly (no editor) into the test.
# Note: -DJucePlugin_Name=\"SideChain\" keeps the quote chars in the argv string
# (clang parses the quoted string literal itself), matching the verified
# manual build recipe.
PH6_FLAGS="$GUI_FLAGS -DSIDECHAIN_HEADLESS_TEST=1 -DJucePlugin_Build_VST3=1 -DJucePlugin_Build_AU=1 -DJucePlugin_Name=\"SideChain\""
PH6_TUS="$TUS_AUDIO $TUS_GUI ../Source/PluginProcessor.cpp ../Source/MusicProdAuth.cpp"

REBUILD=0
[ "$1" == "--rebuild" ] && REBUILD=1

overall=0

build_and_run () {
    local name="$1"; shift
    local src="$1"; shift
    local flags="$1"; shift
    local tus="$1"; shift
    local fws="$1"; shift

    echo ""
    echo "============================================================"
    echo " $name"
    echo "============================================================"

    if [ ! -f "$name" ] || [ "$src" -nt "$name" ] || [ "$REBUILD" == "1" ]; then
        echo "  compiling $src ..."
        # shellcheck disable=SC2086
        clang++ $flags -x objective-c++ \
            -I../JuceLibraryCode -I"$JUCE_MODULES" \
            $tus "$src" $fws -o "$name" 2>&1 | tee compile.log
        # Check the COMPILER's exit status (grep hides it in the pipeline),
        # and dump diagnostics on failure (linker errors lack "error:").
        if [ "${PIPESTATUS[0]}" -ne 0 ]; then
            echo "  [FAIL] compile $name (see output above / compile.log)"
            return 1
        fi
    else
        echo "  binary up to date"
    fi

    "./$name"
    local rc=$?
    if [ $rc -ne 0 ]; then
        echo "  *** $name FAILED (exit $rc) ***"
        return 1
    fi
    return 0
}

# 1. Phase 3 DSP regression
build_and_run DSPRegressionTests DSPRegressionTests.cpp \
    "$COMMON_FLAGS" \
    "$TUS_AUDIO" "$FRAMEWORKS" || overall=1

# 2. Phase 4 parameter/state/graph (needs APVTS -> audio_processors + gui)
build_and_run Phase4Tests Phase4Tests.cpp \
    "$GUI_FLAGS" \
    "../JuceLibraryCode/include_juce_core.cpp ../JuceLibraryCode/include_juce_audio_basics.cpp ../JuceLibraryCode/include_juce_audio_formats.cpp ../JuceLibraryCode/include_juce_events.cpp ../JuceLibraryCode/include_juce_data_structures.cpp ../JuceLibraryCode/include_juce_graphics.cpp ../JuceLibraryCode/include_juce_dsp.cpp $TUS_GUI" \
    "$GUI_FRAMEWORKS" || overall=1

# (0.4.0: Phase 5 timing suite retired with the external detector; see note above.)

# 4. Phase 6 Release parameter tests (real processor, headless)
build_and_run Phase6Tests Phase6Tests.cpp \
    "$PH6_FLAGS" \
    "$PH6_TUS" "$GUI_FRAMEWORKS" || overall=1

# 5. Phase 7 preset tests (real processor, headless — same recipe as Phase 6)
build_and_run PresetTests PresetTests.cpp \
    "$PH6_FLAGS" \
    "$PH6_TUS" "$GUI_FRAMEWORKS" || overall=1

# 6. Phase 8 transport + auth tests (real processor + mock auth transport)
build_and_run Phase8Tests Phase8Tests.cpp \
    "$PH6_FLAGS" \
    "$PH6_TUS" "$GUI_FRAMEWORKS" || overall=1

# 7. 0.4.0 DUCK LENGTH + graph view-mode tests (real processor, headless)
build_and_run DuckLengthTests DuckLengthTests.cpp \
    "$PH6_FLAGS" \
    "$PH6_TUS ../Source/GraphComponent.cpp" "$GUI_FRAMEWORKS" || overall=1

# 8. 0.4.0 internal beat-scheduler tests (pure scheduler + processor integration)
build_and_run BeatSchedulerTests BeatSchedulerTests.cpp \
    "$PH6_FLAGS" \
    "$PH6_TUS" "$GUI_FRAMEWORKS" || overall=1

# NOTE (0.4.0 architecture change): TriggerDSPTests and DetectorTests
# exercised the REMOVED external sidechain transient detector and are
# retired. Their protections (one trigger per event, no sustained ducking)
# are carried by BeatSchedulerTests (one trigger per beat, STOP = no
# triggers) and the beat-grid suites above. Phase5TimingTests was also
# retired: its timing windows asserted the old detector's follower tau;
# envelope timing is now covered by DSPRegressionTests + DuckLengthTests.

echo ""
echo "============================================================"
if [ $overall -eq 0 ]; then
    echo " ALL SUITES PASSED"
else
    echo " ONE OR MORE SUITES FAILED"
fi
echo "============================================================"
exit $overall
