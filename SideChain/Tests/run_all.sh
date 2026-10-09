#!/bin/bash
# ============================================================================
# SideChain - combined regression test runner (project-local, no globals).
#
# Compiles and runs the current suites:
#   1. DSPRegressionTests            - Phase 3 DSP regression (25 checks)
#   2. Phase4Tests                   - Phase 4 parameter/state/graph (13 checks)
#   3. Phase6Tests                   - Phase 6 Release parameter + state hardening
#   4. PresetTests                   - Phase 7 factory preset system
#   5. Phase8Tests                   - Phase 8 transport gate + auth state machine
#   6. DuckLengthTests               - 0.4.0 DUCK LENGTH + graph view modes
#   7. BeatSchedulerTests            - 0.4.0 internal beat scheduler
#   8. PumpCurveDspTests             - Phase D PumpCurve DSP integration
#   9. PumpCurvePresetStateTests     - Phase E preset curve ownership + schema migration
#
# Usage:
#   ./Tests/run_all.sh [--rebuild] [--suite NAME[,NAME]] [--out-dir DIR]
#                      [--dry-run] [--list]
#
#   --rebuild          force recompilation of every selected suite
#   --suite NAME       run only the named suite(s); may be repeated
#   --out-dir DIR      where binaries, stamps and compile logs are written
#                      (default: Tests/.build)
#   --dry-run          print the compile/reuse plan and exit without building
#   --list             list the known suite names and exit
#
# External JUCE location:
#   SIDECHAIN_JUCE_ROOT=/path/to/HISE/JUCE ./Tests/run_all.sh --rebuild
#   When unset, the standard sibling layout (<repo>/../HISE/JUCE) is used.
#
# Rebuild guarantee: a suite is recompiled whenever --rebuild is given, when
# its binary is missing, or when the recorded fingerprint of its exact compile
# command and input file contents no longer matches. Timestamps alone are not
# trusted, so a newer-but-stale binary cannot produce a false pass.
#
# Exit code: 0 only if every selected suite passes.
# ============================================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

# shellcheck source=validation_common.sh
source "$SCRIPT_DIR/validation_common.sh"

JUCE_MODULES="$(resolve_juce_modules)" || exit 1
JUCE_ROOT="$(resolve_juce_root)" || exit 1

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
DRY_RUN=0
OUT_DIR="$SCRIPT_DIR/.build"
SELECTED_SUITES=""

while [ $# -gt 0 ]; do
    case "$1" in
        --rebuild) REBUILD=1 ;;
        --dry-run) DRY_RUN=1 ;;
        --list)
            printf '%s\n' DSPRegressionTests Phase4Tests Phase6Tests PresetTests \
                Phase8Tests DuckLengthTests BeatSchedulerTests PumpCurveDspTests \
                PumpCurvePresetStateTests
            exit 0
            ;;
        --suite)
            shift
            [ $# -gt 0 ] || { echo "ERROR: --suite needs a suite name" >&2; exit 2; }
            SELECTED_SUITES="${SELECTED_SUITES}${SELECTED_SUITES:+,}$1"
            ;;
        --out-dir)
            shift
            [ $# -gt 0 ] || { echo "ERROR: --out-dir needs a directory" >&2; exit 2; }
            OUT_DIR="$1"
            ;;
        -h|--help)
            sed -n '2,40p' "$0"
            exit 0
            ;;
        *)
            echo "ERROR: unknown argument '$1' (see --help)" >&2
            exit 2
            ;;
    esac
    shift
done

suite_selected()
{
    [ -z "$SELECTED_SUITES" ] && return 0
    case ",$SELECTED_SUITES," in
        *",$1,"*) return 0 ;;
        *) return 1 ;;
    esac
}

# Fingerprint of the exact compile recipe plus the contents of every input.
# Changing a source file, the flags or the JUCE location invalidates it.
suite_fingerprint()
{
    local src="$1" flags="$2" tus="$3" f
    {
        printf 'runner=2\nflags=%s\njuce=%s\nsrc=%s\n' "$flags" "$JUCE_MODULES" "$src"
        for f in $tus "$src"; do
            if [ -f "$f" ]; then
                printf '%s %s\n' "$f" "$(shasum -a 256 "$f" 2>/dev/null | awk '{print $1}')"
            else
                printf '%s MISSING\n' "$f"
            fi
        done
    } | shasum -a 256 | awk '{print $1}'
}

planned_action()
{
    local name="$1" src="$2" flags="$3" tus="$4"
    local binary="$OUT_DIR/$name" stamp="$OUT_DIR/$name.stamp"
    if [ "$REBUILD" = "1" ]; then
        echo "compile"
        return
    fi
    if [ ! -f "$binary" ] || [ ! -f "$stamp" ]; then
        echo "compile"
        return
    fi
    if [ "$(cat "$stamp" 2>/dev/null)" != "$(suite_fingerprint "$src" "$flags" "$tus")" ]; then
        echo "compile"
        return
    fi
    echo "reuse"
}

build_and_run()
{
    local name="$1"; shift
    local src="$1"; shift
    local flags="$1"; shift
    local tus="$1"; shift
    local fws="$1"; shift

    echo ""
    echo "============================================================"
    echo " $name"
    echo "============================================================"

    local action
    action="$(planned_action "$name" "$src" "$flags" "$tus")"

    if [ "$DRY_RUN" = "1" ]; then
        echo "  PLAN $name: $action"
        return 0
    fi

    local binary="$OUT_DIR/$name"
    local log="$OUT_DIR/$name.compile.log"

    if [ "$action" = "compile" ]; then
        echo "  compiling $src ..."
        # shellcheck disable=SC2086
        clang++ $flags -x objective-c++ \
            -I../JuceLibraryCode -I"$JUCE_MODULES" \
            $tus "$src" $fws -o "$binary" 2>&1 | tee "$log"
        # Check the COMPILER's exit status (tee hides it in the pipeline),
        # and dump diagnostics on failure (linker errors lack "error:").
        if [ "${PIPESTATUS[0]}" -ne 0 ]; then
            echo "  [FAIL] compile $name (see $log)"
            return 1
        fi
        suite_fingerprint "$src" "$flags" "$tus" > "$OUT_DIR/$name.stamp"
    else
        echo "  binary up to date (fingerprint match)"
    fi

    "$binary"
    local rc=$?
    if [ $rc -ne 0 ]; then
        echo "  *** $name FAILED (exit $rc) ***"
        return 1
    fi
    return 0
}

if [ "$DRY_RUN" != "1" ]; then
    mkdir -p "$OUT_DIR" || exit 1
fi
echo "repository:   $(sidechain_repo_root)"
echo "JUCE root:    $JUCE_ROOT ($(juce_override_state))"
echo "output dir:   $OUT_DIR"
[ -n "$SELECTED_SUITES" ] && echo "suites:       $SELECTED_SUITES"

overall=0
ran=0

run_suite()
{
    local name="$1"; shift
    suite_selected "$name" || return 0
    ran=$((ran + 1))
    build_and_run "$name" "$@" || overall=1
}

# 1. Phase 3 DSP regression
run_suite DSPRegressionTests DSPRegressionTests.cpp \
    "$COMMON_FLAGS" \
    "$TUS_AUDIO" "$FRAMEWORKS"

# 2. Phase 4 parameter/state/graph (needs APVTS -> audio_processors + gui)
run_suite Phase4Tests Phase4Tests.cpp \
    "$GUI_FLAGS" \
    "../JuceLibraryCode/include_juce_core.cpp ../JuceLibraryCode/include_juce_audio_basics.cpp ../JuceLibraryCode/include_juce_audio_formats.cpp ../JuceLibraryCode/include_juce_events.cpp ../JuceLibraryCode/include_juce_data_structures.cpp ../JuceLibraryCode/include_juce_graphics.cpp ../JuceLibraryCode/include_juce_dsp.cpp $TUS_GUI" \
    "$GUI_FRAMEWORKS"

# (0.4.0: Phase 5 timing suite retired with the external detector; see note above.)

# 3. Phase 6 Release parameter tests (real processor, headless)
run_suite Phase6Tests Phase6Tests.cpp \
    "$PH6_FLAGS" \
    "$PH6_TUS" "$GUI_FRAMEWORKS"

# 4. Phase 7 preset tests (real processor, headless — same recipe as Phase 6)
run_suite PresetTests PresetTests.cpp \
    "$PH6_FLAGS" \
    "$PH6_TUS" "$GUI_FRAMEWORKS"

# 5. Phase 8 transport + auth tests (real processor + mock auth transport)
run_suite Phase8Tests Phase8Tests.cpp \
    "$PH6_FLAGS" \
    "$PH6_TUS" "$GUI_FRAMEWORKS"

# 6. 0.4.0 DUCK LENGTH + graph view-mode tests (real processor, headless)
run_suite DuckLengthTests DuckLengthTests.cpp \
    "$PH6_FLAGS" \
    "$PH6_TUS ../Source/GraphComponent.cpp" "$GUI_FRAMEWORKS"

# 7. 0.4.0 internal beat-scheduler tests (pure scheduler + processor integration)
run_suite BeatSchedulerTests BeatSchedulerTests.cpp \
    "$PH6_FLAGS" \
    "$PH6_TUS" "$GUI_FRAMEWORKS"

# 8. Phase D PumpCurve DSP integration (real processor + schema 6 states)
run_suite PumpCurveDspTests PumpCurveDspTests.cpp \
    "$PH6_FLAGS" \
    "$PH6_TUS" "$GUI_FRAMEWORKS"

# 9. Phase E preset curve ownership and schema migration compatibility.
run_suite PumpCurvePresetStateTests PumpCurvePresetStateTests.cpp \
    "$PH6_FLAGS" \
    "$PH6_TUS" "$GUI_FRAMEWORKS"

# NOTE (0.4.0 architecture change): TriggerDSPTests and DetectorTests
# exercised the REMOVED external sidechain transient detector and are
# retired. Their protections (one trigger per event, no sustained ducking)
# are carried by BeatSchedulerTests (one trigger per beat, STOP = no
# triggers) and the beat-grid suites above. Phase5TimingTests was also
# retired: its timing windows asserted the old detector's follower tau;
# envelope timing is now covered by DSPRegressionTests + DuckLengthTests.

echo ""
echo "============================================================"
if [ -n "$SELECTED_SUITES" ] && [ "$ran" -eq 0 ]; then
    echo " ERROR: no known suite matched --suite '$SELECTED_SUITES'"
    echo "============================================================"
    exit 2
fi
if [ "$overall" -eq 0 ]; then
    [ "$DRY_RUN" = "1" ] && echo " DRY RUN COMPLETE ($ran suite(s) planned)" || echo " ALL SUITES PASSED"
else
    echo " ONE OR MORE SUITES FAILED"
fi
echo "============================================================"
exit $overall
