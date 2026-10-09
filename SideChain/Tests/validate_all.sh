#!/bin/bash
# ============================================================================
# SideChain - reusable validation entry point.
#
# Runs the whole automated validation workflow against the current checkout and
# reports every stage separately. Safe to call from any working directory.
#
#   SideChain/Tests/validate_all.sh [--out-dir DIR] [--help]
#
# External JUCE location (same variable the Xcode project uses):
#   SIDECHAIN_JUCE_ROOT=/path/to/HISE/JUCE SideChain/Tests/validate_all.sh
#   When unset, the standard sibling layout (<repo>/../HISE/JUCE) is used.
#
# Stages
#   1  prechecks (JUCE resolver, tools, project, safety, infrastructure tests)
#   2  focused PumpCurve preset/state + DSP integration suites
#   3  full regression suite, forced rebuild, per-suite totals
#   4  UI verification harness
#   5  universal Release build of AU + VST3 (arm64 + x86_64)
#   6  architecture verification of both products
#   7  VST3 factory/bus harness against the freshly built plugin
#   8  AU structural verification (no installation)
#   9  optional official Steinberg validator (never fatal)
#   10 summary
#
# Exit code: 0 only if every required stage passed. Optional-stage limitations
# are reported separately from failures.
# ============================================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PROJECT_DIR="$REPO_ROOT/SideChain"
XCODEPROJ="$PROJECT_DIR/Builds/MacOSX/SideChain.xcodeproj"
PBXPROJ="$XCODEPROJ/project.pbxproj"
RELEASE_ASSETS="$PROJECT_DIR/Builds/MacOSX/build/release-assets"
HOST_CHECK="$PROJECT_DIR/HostCheck/VST3BusCheck.cpp"
SCHEME="SideChain - All"

# shellcheck source=validation_common.sh
source "$SCRIPT_DIR/validation_common.sh"

OUT_DIR=""
while [ $# -gt 0 ]; do
    case "$1" in
        --out-dir) shift; [ $# -gt 0 ] || { echo "ERROR: --out-dir needs a directory" >&2; exit 2; }; OUT_DIR="$1" ;;
        -h|--help) sed -n '2,32p' "$0"; exit 0 ;;
        *) echo "ERROR: unknown argument '$1' (see --help)" >&2; exit 2 ;;
    esac
    shift
done

# ---------------------------------------------------------------------------
# Stage bookkeeping: every stage is recorded, so a later success can never
# hide an earlier failure.
# ---------------------------------------------------------------------------
STAGE_NAMES=()
STAGE_STATUS=()
STAGE_NOTES=()
record()
{
    STAGE_NAMES+=("$1")
    STAGE_STATUS+=("$2")
    STAGE_NOTES+=("$3")
    printf '  -> STAGE %s: %s\n' "$2" "$1"
    [ -n "$3" ] && printf '     %s\n' "$3"
    return 0
}
banner()
{
    printf '\n============================================================\n'
    printf ' STAGE %s\n' "$1"
    printf '============================================================\n'
}

FAILED_REQUIRED=0
BLOCKED=""

abort_to_summary()
{
    BLOCKED="$1"
    return 0
}

stage_allowed()
{
    # Usage: stage_allowed "stage name"
    if [ -n "$BLOCKED" ]; then
        record "$1" "SKIP" "blocked by earlier failure: $BLOCKED"
        return 1
    fi
    return 0
}

START_TS="$(date '+%Y%m%d%H%M%S')"
BASELINE_STATUS="$(git -C "$REPO_ROOT" status --porcelain=v1 2>/dev/null)"

# ---------------------------------------------------------------------------
banner "1 - PRECHECKS"
# ---------------------------------------------------------------------------
PRECHECK_OK=1

# Output directory first: everything this run produces stays outside the
# repository, so validation can never dirty the checkout.
if [ -z "$OUT_DIR" ]; then
    OUT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/sidechain-validate.XXXXXX")" || PRECHECK_OK=0
    OUT_DIR_CREATED=1
else
    OUT_DIR_CREATED=0
    if [ -e "$OUT_DIR" ]; then
        if [ -d "$OUT_DIR" ] && [ -z "$(ls -A "$OUT_DIR" 2>/dev/null)" ]; then
            echo "  using existing empty output dir: $OUT_DIR"
        else
            printf 'ERROR: --out-dir "%s" already exists and is not empty; refusing to overwrite.\n' "$OUT_DIR" >&2
            PRECHECK_OK=0
        fi
    fi
fi
case "$OUT_DIR" in
    "$REPO_ROOT"/*)
        printf 'ERROR: output dir "%s" is inside the repository; choose a disposable path.\n' "$OUT_DIR" >&2
        PRECHECK_OK=0
        ;;
esac
mkdir -p "$OUT_DIR" || PRECHECK_OK=0

JUCE_ERR="$OUT_DIR/juce-resolve.err"
JUCE_ROOT="$(resolve_juce_root 2>"$JUCE_ERR")" || JUCE_ROOT=""
if [ -z "$JUCE_ROOT" ]; then
    echo "  JUCE resolver failure:"
    [ -s "$JUCE_ERR" ] && sed 's/^/    /' "$JUCE_ERR"
    PRECHECK_OK=0
else
    JUCE_MODULES="$JUCE_ROOT/modules"
    echo "  JUCE root:     $JUCE_ROOT ($(juce_override_state))"
    echo "  JUCE modules:  $JUCE_MODULES"
fi

for f in "$SCRIPT_DIR/run_all.sh" "$SCRIPT_DIR/ui_verify.sh" "$SCRIPT_DIR/validation_common.sh" \
         "$SCRIPT_DIR/validation_infra_tests.sh" "$HOST_CHECK" "$PBXPROJ" \
         "$PROJECT_DIR/SideChain.jucer"; do
    if [ -f "$f" ]; then
        echo "  present:       ${f#"$REPO_ROOT"/}"
    else
        echo "  MISSING:       $f"
        PRECHECK_OK=0
    fi
done

for tool in clang++ xcodebuild lipo plutil nm shasum mktemp; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "  MISSING TOOL:  $tool"
        PRECHECK_OK=0
    fi
done
echo "  tools:         clang++ $(clang++ --version 2>/dev/null | head -1 | sed 's/.*version //;s/ .*//'), $(xcodebuild -version 2>/dev/null | tr '\n' ' ')"

if command -v xcodebuild >/dev/null 2>&1; then
    if xcodebuild -list -project "$XCODEPROJ" >/dev/null 2>&1; then
        echo "  xcode project: schemes available"
    else
        echo "  ERROR:         xcodebuild cannot read $XCODEPROJ"
        PRECHECK_OK=0
    fi
fi

echo "  repository:    $REPO_ROOT"
echo "  output dir:    $OUT_DIR (created by this run: $OUT_DIR_CREATED)"

# Snapshot any installed plugin bundles so a later stage can prove nothing was installed.
INSTALLED_SNAPSHOT="$OUT_DIR/installed-plugins.before"
: > "$INSTALLED_SNAPSHOT"
for p in "$HOME/Library/Audio/Plug-Ins/Components/SideChain.component" \
         "$HOME/Library/Audio/Plug-Ins/VST3/SideChain.vst3"; do
    if [ -e "$p" ]; then
        printf '%s %s %s\n' "$p" "$(stat -f '%m' "$p")" "$(stat -f '%z' "$p/Contents/MacOS/SideChain" 2>/dev/null)" >> "$INSTALLED_SNAPSHOT"
    fi
done
echo "  installed plugin bundles recorded: $(grep -c . "$INSTALLED_SNAPSHOT" || true)"

if [ "$PRECHECK_OK" != "1" ]; then
    record "prechecks" "FAIL" "required prerequisites missing (see above)"
    abort_to_summary "prechecks"
else
    record "prechecks" "PASS" "JUCE=$JUCE_ROOT, out=$OUT_DIR"
fi

# Infrastructure self-tests (resolver, architecture enforcement, rebuild guarantee).
if stage_allowed "validation infrastructure tests"; then
    INFRA_LOG="$OUT_DIR/stage1-infra-tests.log"
    if SIDECHAIN_JUCE_ROOT="$JUCE_ROOT" "$SCRIPT_DIR/validation_infra_tests.sh" >"$INFRA_LOG" 2>&1; then
        infra_totals="$(grep -oE '[0-9]+/[0-9]+ infrastructure checks passed' "$INFRA_LOG" | tail -1)"
        record "infrastructure self-tests" "PASS" "${infra_totals:-see $INFRA_LOG}"
    else
        record "infrastructure self-tests" "FAIL" "see $INFRA_LOG"
        FAILED_REQUIRED=1
        abort_to_summary "infrastructure self-tests"
    fi
fi

# ---------------------------------------------------------------------------
banner "2 - FOCUSED PUMPCURVE SUITES"
# ---------------------------------------------------------------------------
FOCUSED_SUITES="PumpCurvePresetStateTests PumpCurveDspTests"
FOCUSED_TOTALS=""
for suite in $FOCUSED_SUITES; do
    if ! stage_allowed "focused suite $suite"; then
        break
    fi
    log="$OUT_DIR/stage2-$suite.log"
    if SIDECHAIN_JUCE_ROOT="$JUCE_ROOT" "$SCRIPT_DIR/run_all.sh" --suite "$suite" --rebuild \
            --out-dir "$OUT_DIR/test-build" >"$log" 2>&1; then
        total="$(grep -oE '[0-9]+/[0-9]+ checks passed' "$log" | tail -1)"
        printf '  %s: %s\n' "$suite" "${total:-no total parsed}"
        FOCUSED_TOTALS="$FOCUSED_TOTALS ${suite}=${total:-?}"
        record "focused suite $suite" "PASS" "${total:-see $log}"
    else
        printf '  %s: FAILED (see %s)\n' "$suite" "$log"
        tail -20 "$log" | sed 's/^/    /'
        record "focused suite $suite" "FAIL" "see $log"
        FAILED_REQUIRED=1
        abort_to_summary "focused suite $suite"
    fi
done

# ---------------------------------------------------------------------------
banner "3 - FULL REGRESSION (FORCED REBUILD)"
# ---------------------------------------------------------------------------
if stage_allowed "full regression"; then
    log="$OUT_DIR/stage3-run-all.log"
    if SIDECHAIN_JUCE_ROOT="$JUCE_ROOT" "$SCRIPT_DIR/run_all.sh" --rebuild \
            --out-dir "$OUT_DIR/test-build" >"$log" 2>&1; then
        per_suite="$(awk '/^ [A-Za-z0-9]+$/ {name=$1; next} /checks passed/ {if (name != "") {printf "%s %s\n", name, $0; name=""}}' "$log")"
        printf '%s\n' "$per_suite" | sed 's/^/  /'
        suite_count="$(printf '%s\n' "$per_suite" | grep -c . || true)"
        totals="$(grep -oE '[0-9]+/[0-9]+ checks passed' "$log" | awk -F'[/ ]' '{ok+=$1; all+=$2} END {print ok"/"all}')"
        fail_lines="$(grep -c '\[FAIL\]' "$log" || true)"
        if [ "$suite_count" -ge 9 ] && [ "$fail_lines" = "0" ] && grep -q 'ALL SUITES PASSED' "$log"; then
            record "full regression" "PASS" "$suite_count suites, $totals checks, 0 FAIL lines"
        else
            record "full regression" "FAIL" "$suite_count suites, $totals checks, $fail_lines FAIL lines (see $log)"
            FAILED_REQUIRED=1
            abort_to_summary "full regression"
        fi
    else
        tail -20 "$log" | sed 's/^/    /'
        record "full regression" "FAIL" "see $log"
        FAILED_REQUIRED=1
        abort_to_summary "full regression"
    fi
fi

# ---------------------------------------------------------------------------
banner "4 - UI VERIFICATION"
# ---------------------------------------------------------------------------
if stage_allowed "UI verification"; then
    log="$OUT_DIR/stage4-ui-verify.log"
    if SIDECHAIN_JUCE_ROOT="$JUCE_ROOT" "$SCRIPT_DIR/ui_verify.sh" \
            --out-dir "$OUT_DIR/test-build" >"$log" 2>&1; then
        total="$(grep -oE '[0-9]+/[0-9]+ checks passed' "$log" | tail -1)"
        if grep -q 'UI VERIFICATION PASSED' "$log"; then
            record "UI verification" "PASS" "${total:-see $log}"
        else
            record "UI verification" "FAIL" "no UI VERIFICATION PASSED line (see $log)"
            FAILED_REQUIRED=1
            abort_to_summary "UI verification"
        fi
    else
        tail -20 "$log" | sed 's/^/    /'
        record "UI verification" "FAIL" "see $log"
        FAILED_REQUIRED=1
        abort_to_summary "UI verification"
    fi
fi

# ---------------------------------------------------------------------------
banner "5 - UNIVERSAL RELEASE BUILD"
# ---------------------------------------------------------------------------
PRODUCTS="$OUT_DIR/products"
DERIVED="$OUT_DIR/derived"
VST3_BINARY="$PRODUCTS/SideChain.vst3/Contents/MacOS/SideChain"
AU_BINARY="$PRODUCTS/SideChain.component/Contents/MacOS/SideChain"
BUILD_CMD=(xcodebuild -project "$XCODEPROJ" -scheme "$SCHEME" -configuration Release
    ARCHS="arm64 x86_64" ONLY_ACTIVE_ARCH=NO
    SIDECHAIN_JUCE_ROOT="$JUCE_ROOT"
    CONFIGURATION_BUILD_DIR="$PRODUCTS"
    -derivedDataPath "$DERIVED" build)

if stage_allowed "universal release build"; then
    log="$OUT_DIR/stage5-xcodebuild.log"
    printf '  command: %s\n' "${BUILD_CMD[*]}"
    if ! "${BUILD_CMD[@]}" >"$log" 2>&1; then
        tail -20 "$log" | sed 's/^/    /'
        record "universal release build" "FAIL" "xcodebuild exited non-zero (see $log)"
        FAILED_REQUIRED=1
        abort_to_summary "universal release build"
    else
        build_ok=1
        grep -q 'BUILD SUCCEEDED' "$log" || build_ok=0
        [ -f "$VST3_BINARY" ] || build_ok=0
        [ -f "$AU_BINARY" ] || build_ok=0
        case "$PRODUCTS" in
            "$RELEASE_ASSETS"*) build_ok=0 ;;
        esac
        resolved_archs="$(xcodebuild -project "$XCODEPROJ" -scheme "$SCHEME" -configuration Release \
            ARCHS="arm64 x86_64" ONLY_ACTIVE_ARCH=NO SIDECHAIN_JUCE_ROOT="$JUCE_ROOT" \
            CONFIGURATION_BUILD_DIR="$PRODUCTS" -showBuildSettings 2>/dev/null \
            | awk -F' = ' '/^ +ARCHS = /{print $2; exit}')"
        printf '  ARCHS resolved: %s\n' "$resolved_archs"
        case "$resolved_archs" in
            *arm64*) ;;
            *) build_ok=0 ;;
        esac
        case "$resolved_archs" in
            *x86_64*) ;;
            *) build_ok=0 ;;
        esac
        # The tracked release-asset tree must be untouched and must receive no new files.
        if [ -n "$(git -C "$REPO_ROOT" status --porcelain=v1 -- "$RELEASE_ASSETS" 2>/dev/null)" ]; then
            build_ok=0
            echo "  ERROR: tracked release assets changed"
        fi
        new_assets="$(find "$RELEASE_ASSETS" -newermt "$START_TS" 2>/dev/null | head -5)"
        if [ -n "$new_assets" ]; then
            build_ok=0
            echo "  ERROR: new/modified files under release-assets:"
            printf '%s\n' "$new_assets" | sed 's/^/    /'
        fi
        if [ "$(git -C "$REPO_ROOT" status --porcelain=v1 2>/dev/null)" != "$BASELINE_STATUS" ]; then
            build_ok=0
            echo "  ERROR: repository status changed during the build:"
            git -C "$REPO_ROOT" status --porcelain=v1 | sed 's/^/    /'
        fi
        if [ "$build_ok" = "1" ]; then
            record "universal release build" "PASS" "products in $PRODUCTS, ARCHS='$resolved_archs'"
        else
            record "universal release build" "FAIL" "see $log"
            FAILED_REQUIRED=1
            abort_to_summary "universal release build"
        fi
    fi
fi

# ---------------------------------------------------------------------------
banner "6 - ARCHITECTURE VERIFICATION"
# ---------------------------------------------------------------------------
if stage_allowed "architecture verification"; then
    arch_ok=1
    require_universal_binary "$VST3_BINARY" "SideChain.vst3" || arch_ok=0
    require_universal_binary "$AU_BINARY" "SideChain.component" || arch_ok=0
    if [ "$arch_ok" = "1" ]; then
        record "architecture verification" "PASS" "VST3 and AU both arm64 + x86_64"
    else
        record "architecture verification" "FAIL" "a product is missing a slice"
        FAILED_REQUIRED=1
        abort_to_summary "architecture verification"
    fi
fi

# ---------------------------------------------------------------------------
banner "7 - VST3 FACTORY/BUS CHECK"
# ---------------------------------------------------------------------------
if stage_allowed "VST3 factory/bus check"; then
    harness="$OUT_DIR/VST3BusCheck"
    log="$OUT_DIR/stage7-vst3buscheck.log"
    if clang++ -std=c++17 -O1 -DNDEBUG=1 \
            -I"$JUCE_MODULES/juce_audio_processors/format_types/VST3_SDK" \
            "$HOST_CHECK" -o "$harness" >"$OUT_DIR/stage7-harness-compile.log" 2>&1 \
       && "$harness" "$VST3_BINARY" >"$log" 2>&1; then
        sed 's/^/  /' "$log" | tail -12
        if grep -q 'BUS CHECK PASSED' "$log"; then
            record "VST3 factory/bus check" "PASS" "$(grep -oE 'BUS CHECK PASSED \(0 failure\(s\)\)' "$log" | tail -1)"
        else
            record "VST3 factory/bus check" "FAIL" "harness reported failures (see $log)"
            FAILED_REQUIRED=1
            abort_to_summary "VST3 factory/bus check"
        fi
    else
        printf '  harness compile/run failed (logs: %s, %s)\n' "$OUT_DIR/stage7-harness-compile.log" "$log"
        tail -20 "$log" 2>/dev/null | sed 's/^/    /'
        record "VST3 factory/bus check" "FAIL" "see $OUT_DIR/stage7-harness-compile.log and $log"
        FAILED_REQUIRED=1
        abort_to_summary "VST3 factory/bus check"
    fi
fi

# ---------------------------------------------------------------------------
banner "8 - AU STRUCTURAL CHECK (NO INSTALLATION)"
# ---------------------------------------------------------------------------
if stage_allowed "AU structural check"; then
    au_ok=1
    require_universal_binary "$AU_BINARY" "AU executable" || au_ok=0

    # Expected identifiers come from the project definition, not from the build.
    jucer="$PROJECT_DIR/SideChain.jucer"
    exp_subtype="$(grep -oE 'pluginCode="[^"]+"' "$jucer" | head -1 | sed 's/.*="//;s/"//')"
    exp_manufacturer="$(grep -oE 'pluginManufacturerCode="[^"]+"' "$jucer" | head -1 | sed 's/.*="//;s/"//')"
    exp_name="$(grep -oE 'pluginName="[^"]+"' "$jucer" | head -1 | sed 's/.*="//;s/"//')"
    exp_company="$(grep -oE 'companyName="[^"]+"' "$jucer" | head -1 | sed 's/.*="//;s/"//')"
    exp_prefix="$(grep -oE 'pluginAUExportPrefix="[^"]+"' "$jucer" | head -1 | sed 's/.*="//;s/"//')"
    printf '  project expects: type=aufx subtype=%s manufacturer=%s name="%s: %s" factory=%sFactory\n' \
        "$exp_subtype" "$exp_manufacturer" "$exp_company" "$exp_name" "$exp_prefix"

    meta="$(plutil -extract AudioComponents json -o - "$PRODUCTS/SideChain.component/Contents/Info.plist" 2>/dev/null)"
    printf '  plist metadata: %s\n' "$meta"
    for expect in "\"type\":\"aufx\"" "\"subtype\":\"$exp_subtype\"" "\"manufacturer\":\"$exp_manufacturer\"" \
                  "\"factoryFunction\":\"${exp_prefix}Factory\"" "$exp_company: $exp_name"; do
        case "$meta" in
            *"$expect"*) echo "  match: $expect" ;;
            *) echo "  MISMATCH: expected $expect"; au_ok=0 ;;
        esac
    done

    if nm -gU "$AU_BINARY" 2>/dev/null | grep -q "_${exp_prefix}Factory"; then
        echo "  factory symbol present: _${exp_prefix}Factory"
    else
        echo "  MISSING factory symbol: _${exp_prefix}Factory"
        au_ok=0
    fi

    # Nothing may have been installed into the user's plugin directories.
    after="$OUT_DIR/installed-plugins.after"
    : > "$after"
    for p in "$HOME/Library/Audio/Plug-Ins/Components/SideChain.component" \
             "$HOME/Library/Audio/Plug-Ins/VST3/SideChain.vst3"; do
        if [ -e "$p" ]; then
            printf '%s %s %s\n' "$p" "$(stat -f '%m' "$p")" "$(stat -f '%z' "$p/Contents/MacOS/SideChain" 2>/dev/null)" >> "$after"
        fi
    done
    if diff -q "$INSTALLED_SNAPSHOT" "$after" >/dev/null 2>&1; then
        echo "  installed plugin bundles unchanged ($(grep -c . "$after" || true) present before/after)"
    else
        echo "  ERROR: installed plugin bundles changed during validation"
        diff "$INSTALLED_SNAPSHOT" "$after" | sed 's/^/    /'
        au_ok=0
    fi

    if [ "$au_ok" = "1" ]; then
        record "AU structural check" "PASS" "aufx/$exp_subtype/$exp_manufacturer, universal, factory symbol present, nothing installed"
    else
        record "AU structural check" "FAIL" "see the checks above"
        FAILED_REQUIRED=1
        abort_to_summary "AU structural check"
    fi
fi

# ---------------------------------------------------------------------------
banner "9 - OPTIONAL OFFICIAL STEINBERG VALIDATOR"
# ---------------------------------------------------------------------------
if stage_allowed "official VST3 validator"; then
    VALIDATOR_LOG="$OUT_DIR/stage9-validator.log"
    validator_bin=""
    for candidate in vst3validator validator VST3Validator; do
        if command -v "$candidate" >/dev/null 2>&1; then
            validator_bin="$(command -v "$candidate")"
            break
        fi
    done
    if [ -z "$validator_bin" ]; then
        sdk_root="$JUCE_MODULES/juce_audio_processors/format_types/VST3_SDK"
        found="$(find "$sdk_root" -maxdepth 6 -type f \( -name 'validator' -o -name 'vst3validator' -o -name 'VST3Validator' \) -perm -u+x 2>/dev/null | head -3)"
        printf '  prebuilt official validator binaries found: %s\n' "${found:-none}"
        printf '  SDK layout checked: %s\n' "${sdk_root#"$REPO_ROOT"/}"
        ls "$sdk_root" 2>/dev/null | sed 's/^/    /' | head -8
        if find "$sdk_root" -maxdepth 5 -type d -name validator 2>/dev/null | head -1 | grep -q .; then
            printf '  note: the SDK ships validator *source*; building it is out of scope for this phase\n'
        fi
        printf '  cmake available for building the SDK validator: %s\n' "$(command -v cmake >/dev/null 2>&1 && echo yes || echo no)"
        record "official VST3 validator" "LIMITATION" "no official validator binary available; not run"
    else
        printf '  using: %s\n' "$validator_bin"
        if "$validator_bin" "$PRODUCTS/SideChain.vst3" >"$VALIDATOR_LOG" 2>&1; then
            tail -15 "$VALIDATOR_LOG" | sed 's/^/  /'
            record "official VST3 validator" "PASS" "$validator_bin reported success (see $VALIDATOR_LOG)"
        else
            tail -20 "$VALIDATOR_LOG" | sed 's/^/  /'
            record "official VST3 validator" "FAIL" "$validator_bin reported problems (see $VALIDATOR_LOG)"
            FAILED_REQUIRED=1
        fi
    fi
fi

# ---------------------------------------------------------------------------
banner "10 - SUMMARY"
# ---------------------------------------------------------------------------
printf '\n  stage results:\n'
i=0
while [ $i -lt ${#STAGE_NAMES[@]} ]; do
    printf '    [%s] %s\n' "${STAGE_STATUS[$i]}" "${STAGE_NAMES[$i]}"
    [ -n "${STAGE_NOTES[$i]}" ] && printf '           %s\n' "${STAGE_NOTES[$i]}"
    i=$((i + 1))
done

fails=0
skips=0
limitations=0
for status in "${STAGE_STATUS[@]}"; do
    case "$status" in
        FAIL) fails=$((fails + 1)) ;;
        SKIP) skips=$((skips + 1)) ;;
        LIMITATION) limitations=$((limitations + 1)) ;;
    esac
done

printf '\n  JUCE root:      %s\n' "${JUCE_ROOT:-unresolved}"
printf '  output dir:     %s\n' "$OUT_DIR"
printf '  logs:           %s/stage*.log\n' "$OUT_DIR"

if [ "$fails" -eq 0 ] && [ "$skips" -eq 0 ] && [ "$FAILED_REQUIRED" -eq 0 ]; then
    printf '\n VALIDATION PASSED (all required stages passed)\n'
    [ "$limitations" -gt 0 ] && printf ' NOTE: %s optional stage(s) reported a limitation (not a failure).\n' "$limitations"
    printf '============================================================\n'
    exit 0
fi

printf '\n VALIDATION FAILED (%s failed stage(s), %s skipped)\n' "$fails" "$skips"
[ "$limitations" -gt 0 ] && printf ' Optional limitation(s) recorded: %s (not the cause of this failure).\n' "$limitations"
printf '============================================================\n'
exit 1
