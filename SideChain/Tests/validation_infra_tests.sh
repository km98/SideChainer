#!/bin/bash
# ============================================================================
# SideChain - focused tests for the validation infrastructure itself.
#
# Covers requirements that the C++ suites cannot observe:
#   * JUCE resolver: standard-layout default, explicit override, invalid
#     override diagnostics and non-fallback behaviour
#   * architecture enforcement (arm64 + x86_64) used by validate_all.sh
#   * run_all.sh rebuild guarantee: fingerprint reuse, forced recompilation
#     and explicit failure when a --suite name does not exist
#
# Usage: ./Tests/validation_infra_tests.sh [--juce-root DIR]
#   SIDECHAIN_JUCE_ROOT=/path/to/HISE/JUCE ./Tests/validation_infra_tests.sh
#
# Exit code: 0 only if every check passes.
# ============================================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

# shellcheck source=validation_common.sh
source "$SCRIPT_DIR/validation_common.sh"

checks=0
failures=0
check()
{
    checks=$((checks + 1))
    if [ "$1" = "1" ]; then
        printf '  [PASS] %s\n' "$2"
    else
        failures=$((failures + 1))
        printf '  [FAIL] %s\n' "$2"
    fi
}

TMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/sidechain-infra-tests.XXXXXX")" || exit 1
trap 'rm -rf "$TMP_ROOT"' EXIT

printf 'SideChain validation infrastructure tests\n'
printf '  repository: %s\n' "$(sidechain_repo_root)"
printf '  scratch:    %s\n\n' "$TMP_ROOT"

# ---------------------------------------------------------------------------
printf 'JUCE resolver\n'
# ---------------------------------------------------------------------------
expected_default="$(default_juce_root)"

# 1/2. Default discovery in the standard development layout.
if [ -d "$expected_default/modules/juce_core" ]; then
    resolved="$(unset SIDECHAIN_JUCE_ROOT; JUCE_MODULES_FILE="$SCRIPT_DIR/validation_common.sh"; resolve_juce_root 2>"$TMP_ROOT/default.err")"
    check "$([ "$resolved" = "$expected_default" ] && echo 1 || echo 0)" \
        "unset override resolves the standard sibling JUCE root ($expected_default)"
    check "$([ "$(unset SIDECHAIN_JUCE_ROOT; juce_override_state)" = "default" ] && echo 1 || echo 0)" \
        "unset override reports state 'default'"
else
    # A clone kept elsewhere: default discovery must fail with guidance.
    if (unset SIDECHAIN_JUCE_ROOT; resolve_juce_root >"$TMP_ROOT/default.out" 2>"$TMP_ROOT/default.err"); then
        check 0 "unset override resolves a sibling JUCE root (expected: none present here)"
    else
        check "$(grep -q "SIDECHAIN_JUCE_ROOT" "$TMP_ROOT/default.err" && echo 1 || echo 0)" \
            "unset override without a sibling JUCE fails and points at SIDECHAIN_JUCE_ROOT"
    fi
fi

# 3. Explicit override is honoured verbatim.
if [ -n "${SIDECHAIN_JUCE_ROOT:-}" ]; then
    explicit_root="$(resolve_juce_root)"
    check "$([ "$explicit_root" = "$SIDECHAIN_JUCE_ROOT" ] && echo 1 || echo 0)" \
        "explicit override is used verbatim ($explicit_root)"
    check "$([ "$(juce_override_state)" = "explicit" ] && echo 1 || echo 0)" \
        "explicit override reports state 'explicit'"
else
    explicit_root="$(SIDECHAIN_JUCE_ROOT="$expected_default" resolve_juce_root)"
    check "$([ "$explicit_root" = "$expected_default" ] && echo 1 || echo 0)" \
        "explicit override is used verbatim ($explicit_root)"
fi

# 4. Invalid explicit override: clear diagnostics, no silent fallback.
bad_root="$TMP_ROOT/not-a-juce-root"
mkdir -p "$bad_root"
if SIDECHAIN_JUCE_ROOT="$bad_root" resolve_juce_root >"$TMP_ROOT/bad.out" 2>"$TMP_ROOT/bad.err"; then
    check 0 "invalid explicit override is rejected"
else
    bad_diag=1
    grep -q 'SIDECHAIN_JUCE_ROOT' "$TMP_ROOT/bad.err" || bad_diag=0
    grep -q "$bad_root" "$TMP_ROOT/bad.err" || bad_diag=0
    [ ! -s "$TMP_ROOT/bad.out" ] || bad_diag=0
    check "$bad_diag" "invalid explicit override fails with actionable diagnostics and no fallback"
fi

# 5. Missing (nonexistent) explicit override path behaves the same way.
if SIDECHAIN_JUCE_ROOT="$TMP_ROOT/does-not-exist" resolve_juce_root >/dev/null 2>"$TMP_ROOT/missing.err"; then
    check 0 "nonexistent explicit override is rejected"
else
    check "$(grep -q 'does-not-exist' "$TMP_ROOT/missing.err" && echo 1 || echo 0)" \
        "nonexistent explicit override names the bad path in the diagnostics"
fi

# ---------------------------------------------------------------------------
printf '\nArchitecture enforcement\n'
# ---------------------------------------------------------------------------
cat > "$TMP_ROOT/thin.c" <<'EOF'
int main (void) { return 0; }
EOF
clang -arch arm64 "$TMP_ROOT/thin.c" -o "$TMP_ROOT/thin_arm64" 2>/dev/null
clang -arch arm64 -arch x86_64 "$TMP_ROOT/thin.c" -o "$TMP_ROOT/universal" 2>/dev/null
if [ -f "$TMP_ROOT/thin_arm64" ] && [ -f "$TMP_ROOT/universal" ]; then
    if require_universal_binary "$TMP_ROOT/thin_arm64" "fixture" >"$TMP_ROOT/thin.out" 2>&1; then
        check 0 "arm64-only fixture is rejected by the universal check"
    else
        check "$(grep -q 'arm64-only\|found "arm64"' "$TMP_ROOT/thin.out" && echo 1 || echo 0)" \
            "arm64-only fixture is rejected by the universal check"
    fi
    if require_universal_binary "$TMP_ROOT/universal" "fixture" >"$TMP_ROOT/uni.out" 2>&1; then
        check "$(grep -q 'arm64' "$TMP_ROOT/uni.out" && grep -q 'x86_64' "$TMP_ROOT/uni.out" && echo 1 || echo 0)" \
            "universal fixture is accepted and reports both slices"
    else
        check 0 "universal fixture is accepted and reports both slices"
    fi
    if require_universal_binary "$TMP_ROOT/missing-binary" "fixture" >/dev/null 2>&1; then
        check 0 "missing binary is reported as a failure"
    else
        check 1 "missing binary is reported as a failure"
    fi
else
    check 0 "architecture fixtures compiled"
fi

# ---------------------------------------------------------------------------
printf '\nrun_all.sh rebuild guarantee and suite selection\n'
# ---------------------------------------------------------------------------
runner="$SCRIPT_DIR/run_all.sh"
plan_out="$TMP_ROOT/fresh"
if "$runner" --dry-run --out-dir "$plan_out" >"$TMP_ROOT/dry-fresh.log" 2>&1; then
    check "$(grep -q ': compile' "$TMP_ROOT/dry-fresh.log" && echo 1 || echo 0)" \
        "dry run with an empty output directory plans a compile for every suite"
else
    check 0 "dry run with an empty output directory plans a compile for every suite"
fi

# Unknown suite name must fail loudly instead of reporting success.
if "$runner" --suite NotASuite --dry-run --out-dir "$plan_out" >"$TMP_ROOT/dry-unknown.log" 2>&1; then
    check 0 "unknown --suite name is rejected"
else
    check "$(grep -qi 'no known suite' "$TMP_ROOT/dry-unknown.log" && echo 1 || echo 0)" \
        "unknown --suite name is rejected with a clear message"
fi

# Real rebuild behaviour on the cheapest suite (audio-only, no GUI modules).
real_out="$TMP_ROOT/real"
if "$runner" --suite DSPRegressionTests --rebuild --out-dir "$real_out" >"$TMP_ROOT/rebuild-1.log" 2>&1; then
    check "$(grep -q 'compiling DSPRegressionTests.cpp' "$TMP_ROOT/rebuild-1.log" && echo 1 || echo 0)" \
        "explicit --rebuild compiles the suite from current source"
    mtime_first="$(stat -f '%m' "$real_out/DSPRegressionTests" 2>/dev/null)"
else
    check 0 "explicit --rebuild compiles the suite from current source"
    mtime_first=""
fi

if "$runner" --suite DSPRegressionTests --out-dir "$real_out" >"$TMP_ROOT/reuse.log" 2>&1; then
    check "$(grep -q 'binary up to date (fingerprint match)' "$TMP_ROOT/reuse.log" && echo 1 || echo 0)" \
        "unchanged sources reuse the fingerprinted binary instead of recompiling"
    mtime_reuse="$(stat -f '%m' "$real_out/DSPRegressionTests" 2>/dev/null)"
    check "$([ -n "$mtime_first" ] && [ "$mtime_first" = "$mtime_reuse" ] && echo 1 || echo 0)" \
        "reuse did not touch the existing binary"
else
    check 0 "unchanged sources reuse the fingerprinted binary instead of recompiling"
    check 0 "reuse did not touch the existing binary"
fi

if "$runner" --suite DSPRegressionTests --rebuild --out-dir "$real_out" >"$TMP_ROOT/rebuild-2.log" 2>&1; then
    mtime_second="$(stat -f '%m' "$real_out/DSPRegressionTests" 2>/dev/null)"
    check "$([ -n "$mtime_second" ] && [ "$mtime_second" != "$mtime_first" ] && echo 1 || echo 0)" \
        "a second explicit --rebuild really recompiles the binary"
else
    check 0 "a second explicit --rebuild really recompiles the binary"
fi

# The fingerprint must follow source contents, not timestamps.
printf '  note: fingerprint covers compile flags, JUCE location and input hashes\n'
stamp="$real_out/DSPRegressionTests.stamp"
if [ -f "$stamp" ]; then
    if ( cd "$real_out" && touch DSPRegressionTests ); then :; fi
    if "$runner" --suite DSPRegressionTests --dry-run --out-dir "$real_out" >"$TMP_ROOT/dry-after-touch.log" 2>&1; then
        check "$(grep -q 'PLAN DSPRegressionTests: reuse' "$TMP_ROOT/dry-after-touch.log" && echo 1 || echo 0)" \
            "touching the binary does not force a rebuild (content fingerprint, not timestamps)"
    else
        check 0 "touching the binary does not force a rebuild (content fingerprint, not timestamps)"
    fi
else
    check 0 "build stamp written next to the binary"
fi

# ---------------------------------------------------------------------------
printf '\nCustomer-visible product-name staging\n'
# ---------------------------------------------------------------------------
# The shipped bundles are PumpCurve.* even though the Xcode target is SideChain,
# so the rename has to be staged and proven. These fixtures use real universal
# Mach-O files, so the architecture and byte-identity checks are exercised for
# real rather than mocked.
mkstub_bundle() { # <bundle-dir> <kind: au|vst3> <bundle-id> <display-name>
    local dir="$1" kind="$2" id="$3" display="$4"
    mkdir -p "$dir/Contents/MacOS"
    cp "$TMP_ROOT/universal" "$dir/Contents/MacOS/SideChain"
    if [ "$kind" = "au" ]; then
        cat > "$dir/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>SideChain</string>
  <key>CFBundleIdentifier</key><string>$id</string>
  <key>CFBundleName</key><string>$display</string>
  <key>CFBundleDisplayName</key><string>$display</string>
  <key>AudioComponents</key><array><dict>
    <key>name</key><string>Music-Prod: $display</string>
    <key>factoryFunction</key><string>SideChainAUFactory</string>
    <key>manufacturer</key><string>Musc</string>
    <key>type</key><string>aufx</string>
    <key>subtype</key><string>SdCh</string>
    <key>version</key><integer>1025</integer>
  </dict></array>
</dict></plist>
EOF
    else
        cat > "$dir/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>SideChain</string>
  <key>CFBundleIdentifier</key><string>$id</string>
  <key>CFBundleName</key><string>$display</string>
  <key>CFBundleDisplayName</key><string>$display</string>
</dict></plist>
EOF
    fi
}

if [ -f "$TMP_ROOT/universal" ]; then
    # 1. A faithful fixture: staging must succeed and keep every byte.
    fx="$TMP_ROOT/naming/products"
    mkdir -p "$fx"
    mkstub_bundle "$fx/SideChain.component" au   "$PUMPCURVE_BUNDLE_ID" "$PUMPCURVE_DISPLAY_NAME"
    mkstub_bundle "$fx/SideChain.vst3"      vst3 "$PUMPCURVE_BUNDLE_ID" "$PUMPCURVE_DISPLAY_NAME"
    if stage_customer_product_names "$fx" "$TMP_ROOT/naming/staged" >"$TMP_ROOT/naming/ok.log" 2>&1; then
        check 1 "valid built bundles are staged under the PumpCurve names"
    else
        check 0 "valid built bundles are staged under the PumpCurve names"
        sed 's/^/        /' "$TMP_ROOT/naming/ok.log"
    fi
    check "$([ -d "$TMP_ROOT/naming/staged/PumpCurve.component" ] && [ -d "$TMP_ROOT/naming/staged/PumpCurve.vst3" ] && echo 1 || echo 0)" \
        "both staged bundles exist as PumpCurve.component and PumpCurve.vst3"
    check "$([ "$(bundle_content_hash "$fx/SideChain.component")" = "$(bundle_content_hash "$TMP_ROOT/naming/staged/PumpCurve.component")" ] && echo 1 || echo 0)" \
        "staged AU content hash equals the built AU (rename changed no byte)"
    check "$([ -s "$TMP_ROOT/naming/staged/SHA256SUMS" ] && echo 1 || echo 0)" \
        "a staged-file manifest is written next to the renamed bundles"

    # 2. An existing staging directory must never be reused silently.
    if stage_customer_product_names "$fx" "$TMP_ROOT/naming/staged" >"$TMP_ROOT/naming/again.log" 2>&1; then
        check 0 "staging refuses to overwrite an existing PumpCurve bundle"
    else
        check "$(grep -q 'refusing to overwrite' "$TMP_ROOT/naming/again.log" && echo 1 || echo 0)" \
            "staging refuses to overwrite an existing PumpCurve bundle"
    fi

    # 3. A foreign bundle identifier must be rejected, not silently renamed.
    fx2="$TMP_ROOT/naming-foreign/products"
    mkdir -p "$fx2"
    mkstub_bundle "$fx2/SideChain.component" au   "com.example.someone-else" "$PUMPCURVE_DISPLAY_NAME"
    mkstub_bundle "$fx2/SideChain.vst3"      vst3 "com.example.someone-else" "$PUMPCURVE_DISPLAY_NAME"
    if stage_customer_product_names "$fx2" "$TMP_ROOT/naming-foreign/staged" >"$TMP_ROOT/naming-foreign/bad-id.log" 2>&1; then
        check 0 "a bundle with a foreign identifier is rejected"
    else
        check "$(grep -q 'bundle identifier not preserved' "$TMP_ROOT/naming-foreign/bad-id.log" && echo 1 || echo 0)" \
            "a bundle with a foreign identifier is rejected"
    fi

    # 4. A wrong displayed name must be rejected: the whole point of the rename.
    fx3="$TMP_ROOT/naming-wrongname/products"
    mkdir -p "$fx3"
    mkstub_bundle "$fx3/SideChain.component" au   "$PUMPCURVE_BUNDLE_ID" "SideChain"
    mkstub_bundle "$fx3/SideChain.vst3"      vst3 "$PUMPCURVE_BUNDLE_ID" "SideChain"
    if stage_customer_product_names "$fx3" "$TMP_ROOT/naming-wrongname/staged" >"$TMP_ROOT/naming-wrongname/bad-name.log" 2>&1; then
        check 0 "a bundle that does not display as PumpCurve is rejected"
    else
        check "$(grep -q 'displayed name' "$TMP_ROOT/naming-wrongname/bad-name.log" && echo 1 || echo 0)" \
            "a bundle that does not display as PumpCurve is rejected"
    fi

    # 5. A missing built bundle must be reported rather than half-staged.
    fx4="$TMP_ROOT/naming-missing/products"
    mkdir -p "$fx4"
    mkstub_bundle "$fx4/SideChain.component" au "$PUMPCURVE_BUNDLE_ID" "$PUMPCURVE_DISPLAY_NAME"
    if stage_customer_product_names "$fx4" "$TMP_ROOT/naming-missing/staged" >"$TMP_ROOT/naming-missing/missing.log" 2>&1; then
        check 0 "a missing built VST3 bundle fails the staging stage"
    else
        check "$(grep -q 'missing built bundle' "$TMP_ROOT/naming-missing/missing.log" && echo 1 || echo 0)" \
            "a missing built VST3 bundle fails the staging stage"
    fi
else
    check 0 "product-name staging fixtures available (universal fixture missing)"
fi

# The stage must be wired into validate_all.sh, and the shipped names must not
# reintroduce the target name anywhere the customer can see it.
printf '\nWiring of the staging stage into validate_all.sh\n'
if grep -q 'stage_customer_product_names' "$SCRIPT_DIR/validate_all.sh"; then
    check 1 "validate_all.sh calls the product-name staging helper"
else
    check 0 "validate_all.sh calls the product-name staging helper"
fi
check "$(grep -q 'CUSTOMER-VISIBLE PRODUCT NAMES' "$SCRIPT_DIR/validate_all.sh" && echo 1 || echo 0)" \
    "validate_all.sh reports the staging stage in its output"
check "$(grep -q 'products-pumpcurve' "$SCRIPT_DIR/validate_all.sh" && echo 1 || echo 0)" \
    "validate_all.sh keeps the staged bundles outside the tracked release assets"

# ---------------------------------------------------------------------------
printf '\n'
echo "============================================================"
printf ' %s/%s infrastructure checks passed\n' "$((checks - failures))" "$checks"
if [ "$failures" -eq 0 ]; then
    echo " VALIDATION INFRASTRUCTURE TESTS PASSED"
else
    echo " VALIDATION INFRASTRUCTURE TESTS FAILED ($failures failure(s))"
fi
echo "============================================================"
[ "$failures" -eq 0 ] || exit 1
exit 0
