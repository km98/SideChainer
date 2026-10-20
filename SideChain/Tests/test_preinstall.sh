#!/bin/bash
# ============================================================================
# SideChain / PumpCurve - fixture test for the installer's legacy cleanup.
#
# Builds/MacOSX/installer-scripts/preinstall deletes the pre-0.4.2 beta bundles
# (SideChain.*, SideChainer.*) that carry OUR Audio Unit identity, because two
# bundles advertising the same aufx/SdCh/Musc tuple confuse host plug-in lists.
# It is the only release step that removes files from a customer's machine, so
# its policy is pinned here:
#
#   * a legacy-named bundle that IS ours (CFBundleIdentifier ==
#     com.musicprod.sidechain) is removed
#   * a legacy-named bundle that is NOT ours is left in place
#   * a bundle whose Info.plist cannot be read is left in place
#   * the current PumpCurve.* names are never touched
#   * a missing / unreadable root is not an error and the script always exits 0
#
# The script under test exposes PUMPCURVE_LEGACY_ROOTS for exactly this purpose,
# so the fixture never has to touch the real /Library.
#
# Usage: ./Tests/test_preinstall.sh
# Exit code: 0 only if every check passes.
# ============================================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PREINSTALL="$SCRIPT_DIR/../Builds/MacOSX/installer-scripts/preinstall"
OURS="com.musicprod.sidechain"

checks=0
failures=0
check() # <1|0> <description>
{
    checks=$((checks + 1))
    if [ "$1" = "1" ]; then
        printf '  [PASS] %s\n' "$2"
    else
        failures=$((failures + 1))
        printf '  [FAIL] %s\n' "$2"
    fi
}

present() { [ -e "$1" ] && echo 1 || echo 0; }

[ -f "$PREINSTALL" ] || { echo "test_preinstall: missing $PREINSTALL" >&2; exit 1; }

TMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/pumpcurve-preinstall-test.XXXXXX")" || exit 1
trap 'rm -rf "$TMP_ROOT"' EXIT

printf 'PumpCurve installer preinstall legacy-cleanup test\n'
printf '  script under test: %s\n' "$PREINSTALL"
printf '  scratch:           %s\n\n' "$TMP_ROOT"

mk_bundle() # <bundle-dir> <bundle-id|->    ('-' writes no Info.plist at all)
{
    local dir="$1" id="$2"
    mkdir -p "$dir/Contents/MacOS"
    : > "$dir/Contents/MacOS/SideChain"
    if [ "$id" != "-" ]; then
        mkdir -p "$dir/Contents"
        cat > "$dir/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
  <key>CFBundleIdentifier</key><string>$id</string>
</dict></plist>
EOF
    fi
}

OURS_ROOT="$TMP_ROOT/ours/Library"
OTHER_ROOT="$TMP_ROOT/other/Library"

# Fixture 1: legacy bundles that really are ours - must be removed.
mk_bundle "$OURS_ROOT/Audio/Plug-Ins/Components/SideChain.component"   "$OURS"
mk_bundle "$OURS_ROOT/Audio/Plug-Ins/Components/SideChainer.component" "$OURS"
mk_bundle "$OURS_ROOT/Audio/Plug-Ins/VST3/SideChain.vst3"             "$OURS"
mk_bundle "$OURS_ROOT/Audio/Plug-Ins/VST3/SideChainer.vst3"           "$OURS"
# Fixture 2: current names, and a legacy name belonging to somebody else.
mk_bundle "$OURS_ROOT/Audio/Plug-Ins/Components/PumpCurve.component"  "$OURS"
mk_bundle "$OURS_ROOT/Audio/Plug-Ins/VST3/PumpCurve.vst3"             "$OURS"
mk_bundle "$OTHER_ROOT/Audio/Plug-Ins/Components/SideChain.component" "com.example.other"
mk_bundle "$OTHER_ROOT/Audio/Plug-Ins/Components/Unrelated.component" "com.example.unrelated"
# Fixture 3: a legacy name with no readable identifier at all.
mk_bundle "$OTHER_ROOT/Audio/Plug-Ins/VST3/SideChainer.vst3"          "-"

LOG="$TMP_ROOT/preinstall.log"
PUMPCURVE_LEGACY_ROOTS="$OURS_ROOT:$OTHER_ROOT:$TMP_ROOT/does/not/exist" \
    /bin/sh "$PREINSTALL" >"$LOG" 2>&1
rc=$?

check "$([ "$rc" -eq 0 ] && echo 1 || echo 0)" \
    "preinstall exits 0 (rc=$rc) even with an unreadable root in the list"

for b in Components/SideChain.component Components/SideChainer.component \
         VST3/SideChain.vst3 VST3/SideChainer.vst3; do
    check "$([ ! -e "$OURS_ROOT/Audio/Plug-Ins/$b" ] && echo 1 || echo 0)" \
        "our legacy bundle was removed: $b"
done

for b in Components/PumpCurve.component VST3/PumpCurve.vst3; do
    check "$(present "$OURS_ROOT/Audio/Plug-Ins/$b")" \
        "the current bundle was left untouched: $b"
done

check "$(present "$OTHER_ROOT/Audio/Plug-Ins/Components/SideChain.component")" \
    "a legacy-named bundle owned by another vendor survives"
check "$(present "$OTHER_ROOT/Audio/Plug-Ins/Components/Unrelated.component")" \
    "an unrelated bundle survives"
check "$(present "$OTHER_ROOT/Audio/Plug-Ins/VST3/SideChainer.vst3")" \
    "a legacy-named bundle with no readable identifier survives"

if grep -q 'left in place (not ours)' "$LOG"; then
    check 1 "the run log reports the bundles it deliberately kept"
else
    check 0 "the run log reports the bundles it deliberately kept"
fi
check "$(grep -q 'TEST MODE' "$LOG" && echo 1 || echo 0)" \
    "the override root list was honoured (TEST MODE reported)"

# A second run over the cleaned fixture must be a harmless no-op.
PUMPCURVE_LEGACY_ROOTS="$OURS_ROOT" /bin/sh "$PREINSTALL" >"$TMP_ROOT/rerun.log" 2>&1
exit_rc=$?
check "$([ "$exit_rc" -eq 0 ] && echo 1 || echo 0)" \
    "re-running over an already clean root still exits 0 (idempotent)"
check "$(present "$OURS_ROOT/Audio/Plug-Ins/Components/PumpCurve.component")" \
    "the second run still did not touch the current bundle"

printf '\n'
echo "============================================================"
printf ' %s/%s preinstall fixture checks passed\n' "$((checks - failures))" "$checks"
if [ "$failures" -eq 0 ]; then
    echo " PREINSTALL LEGACY CLEANUP TESTS PASSED"
else
    echo " PREINSTALL LEGACY CLEANUP TESTS FAILED ($failures failure(s))"
fi
echo "============================================================"
[ "$failures" -eq 0 ] || exit 1
exit 0
