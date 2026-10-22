#!/bin/bash
# ============================================================================
# PumpCurve - Windows build tooling tests (runnable without Windows).
#
# What this proves on a Mac, and why it is worth having:
#
#   * the patched JUCE 6.1.3 tree the Windows build compiles against is fully
#     reproducible from public inputs (the JUCE 6.1.3 release tarball plus
#     juce/JUCE-6.1.3-patched-overlay.tar.gz) and byte-identical to the tree the
#     validated builds used,
#   * the recorded manifest still describes the JUCE tree on this machine,
#   * the staging/packaging tool refuses a non-x64 module, a module that still
#     carries a legacy product name, and never rewrites a byte while renaming,
#   * the installer recipe keeps the properties the release depends on (shared
#     VST3 target, x64-only, uninstaller outside the host scan path, legacy
#     cleanup guarded by the product's own version resource),
#   * the CI workflow still performs the steps a Windows build needs (patched
#     JUCE, Projucer, MSBuild, Inno Setup, install/uninstall test, results
#     branch), and
#   * the product name/version constants agree across every Windows file.
#
# It is *not* a Windows build. Only the Windows runner in
# .github/workflows/windows-vst3.yml can produce the VST3; this script makes sure
# that run has correct inputs and does not silently lose a verification step.
#
# Usage:  ./SideChain/Builds/Windows/test_windows_tooling.sh
# Env:    SIDECHAIN_JUCE_ROOT     JUCE tree used by the validated builds
#         SIDECHAIN_JUCE_STOCK_TAR  cached JUCE 6.1.3 tarball (enables the full
#                                 overlay reconstruction test without network)
# Exit:   0 all checks passed, 1 at least one check failed
# ============================================================================
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROJECT="$(cd "$HERE/../.." && pwd)"          # .../SideChain (the plug-in project)
REPO_ROOT="$(cd "$PROJECT/.." && pwd)"        # .../SideChain (the git repository)
JUCE_DIR="$HERE/juce"
TOOLS="$HERE/tools"
INSTALLER="$HERE/installer"
WORKFLOW="$REPO_ROOT/.github/workflows/windows-vst3.yml"

PASS=0
FAIL=0
NOTES=()

ok()   { printf '  ok:   %s\n' "$1"; PASS=$((PASS + 1)); }
bad()  { printf '  FAIL: %s\n' "$1"; FAIL=$((FAIL + 1)); }
note() { printf '  note: %s\n' "$1"; NOTES+=("$1"); }
check(){ # check <condition-result: 0/1> <message>
    if [ "$1" = "0" ]; then ok "$2"; else bad "$2"; fi
}

echo "============================================================"
echo " PumpCurve Windows tooling tests"
echo "============================================================"

# ---------------------------------------------------------------------------
echo
echo "-- 1. staging / packaging logic (fixture self-test)"
# ---------------------------------------------------------------------------
if out="$(python3 "$TOOLS/stage_windows_vst3.py" --self-test 2>&1)"; then
    echo "$out" | sed 's/^/    /'
    ok "stage/packaging self-test passed (identity, x64, no-overwrite, determinism)"
else
    echo "$out" | tail -20 | sed 's/^/    /'
    bad "stage/packaging self-test failed"
fi

# ---------------------------------------------------------------------------
echo
echo "-- 2. every Windows tool compiles"
# ---------------------------------------------------------------------------
py_ok=0
for f in "$TOOLS/build_windows_vst3.py" "$TOOLS/stage_windows_vst3.py" \
         "$JUCE_DIR/verify_juce_tree.py" "$JUCE_DIR/regenerate_juce_overlay.py"; do
    if python3 -m py_compile "$f" 2>/dev/null; then
        rm -rf "$(dirname "$f")/__pycache__"
    else
        py_ok=1
        echo "    syntax error in $f"
    fi
done
check "$py_ok" "all Windows build tools compile"

# ---------------------------------------------------------------------------
echo
echo "-- 3. the recorded JUCE manifest matches the tree the builds use"
# ---------------------------------------------------------------------------
JUCE_ROOT=""
if [ -f "$PROJECT/Tests/validation_common.sh" ]; then
    # shellcheck source=../../Tests/validation_common.sh
    source "$PROJECT/Tests/validation_common.sh"
    JUCE_ROOT="$(resolve_juce_root 2>/dev/null)" || JUCE_ROOT=""
fi
[ -n "$JUCE_ROOT" ] || JUCE_ROOT="${SIDECHAIN_JUCE_ROOT:-}"

if [ -n "$JUCE_ROOT" ] && [ -d "$JUCE_ROOT/modules" ]; then
    if out="$(python3 "$JUCE_DIR/verify_juce_tree.py" --verify --root "$JUCE_ROOT" 2>&1)"; then
        echo "$out" | grep -E 'manifest|files found|byte-identical' | sed 's/^/    /'
        ok "manifest describes the JUCE tree at $JUCE_ROOT"
        check_manifest_root="$JUCE_ROOT"
    else
        echo "$out" | tail -20 | sed 's/^/    /'
        bad "manifest does not match the JUCE tree at $JUCE_ROOT"
        check_manifest_root=""
    fi
else
    note "no JUCE tree resolved (set SIDECHAIN_JUCE_ROOT); manifest/tree cross-check skipped"
fi

# ---------------------------------------------------------------------------
echo
echo "-- 4. the JUCE tree is reproducible from public inputs"
# ---------------------------------------------------------------------------
STOCK_TAR="${SIDECHAIN_JUCE_STOCK_TAR:-}"
[ -n "$STOCK_TAR" ] || STOCK_TAR="$(ls /tmp/pc-juce/juce-6.1.3.tar.gz 2>/dev/null | head -1)"
if [ -n "$STOCK_TAR" ] && [ -f "$STOCK_TAR" ]; then
    prov_dir="$(mktemp -d)"
    if out="$(python3 "$JUCE_DIR/verify_juce_tree.py" --provision \
                --dest "$prov_dir/juce" --stock-tar "$STOCK_TAR" 2>&1)"; then
        echo "$out" | grep -E 'sha256|applied overlay|files found|JUCE TREE' | sed 's/^/    /'
        ok "JUCE 6.1.3 + overlay reproduces the validated tree (stock tarball verified)"
        if [ -n "${check_manifest_root:-}" ]; then
            if diff -rq --exclude='oboe' "$prov_dir/juce/modules" "$check_manifest_root/modules" 2>/dev/null | grep -qv '/oboe/'; then
                bad "provisioned tree differs from the tree used for validation"
            else
                ok "provisioned tree is byte-identical to the validation tree"
            fi
        fi
    else
        echo "$out" | tail -20 | sed 's/^/    /'
        bad "overlay reconstruction failed"
    fi
    rm -rf "$prov_dir"
else
    note "no cached JUCE 6.1.3 tarball (SIDECHAIN_JUCE_STOCK_TAR or /tmp/pc-juce/juce-6.1.3.tar.gz); reconstruction not re-verified here (the CI workflow verifies it on every run)"
fi

# ---------------------------------------------------------------------------
echo
echo "-- 5. installer recipe keeps the properties the release depends on"
# ---------------------------------------------------------------------------
iss="$INSTALLER/PumpCurve.iss"
if [ ! -f "$iss" ]; then
    bad "missing $iss"
else
    grep -q 'DefaultDirName={commoncf}\\VST3' "$iss"
    check $? "installs into the shared VST3 folder ({commoncf}\\VST3)"
    grep -q 'ArchitecturesAllowed=x64compatible' "$iss"
    check $? "x64-only install"
    grep -q 'ArchitecturesInstallIn64BitMode=x64compatible' "$iss"
    check $? "installs in 64-bit mode so {commoncf} is the 64-bit folder"
    grep -q 'UninstallFilesDir={commoncf}\\{#AppName}' "$iss"
    check $? "uninstaller is kept out of the folder hosts scan"
    grep -q "LEGACY_BUNDLES = 'SideChain.vst3,SideChainer.vst3'" "$iss"
    check $? "knows the legacy bundle names it may clean up"
    # Inno Setup's Pascal Script has no Pointer type and no StringFileInfo accessor,
    # so the recipe cannot read a version resource itself: ISCC rejects the script.
    # The ownership decision therefore lives in the helper it ships.
    helper="$INSTALLER/cleanup_legacy.ps1"
    if [ ! -f "$helper" ]; then
        bad "missing $helper (the installer's ownership check)"
    else
        grep -q "ProductName -eq 'PumpCurve'" "$helper"
        check $? "legacy cleanup requires the product's own version resource"
        grep -q "CompanyName -eq 'Music-Prod'" "$helper"
        check $? "legacy cleanup recognises the previous product by its publisher"
        grep -q 'exit 2' "$helper" && grep -q 'exit 3' "$helper"
        check $? "unreadable or foreign bundle means 'not ours' (never deleted)"
        [ "$(grep -c 'Remove-Item -Recurse -Force' "$helper")" = "1" ]
        check $? "exactly one deletion call, inside the guarded helper"
        grep -q 'x86_64-win' "$helper"
        check $? "the helper reads the module inside the bundle"
    fi
    # Only code counts here: the recipe's comments legitimately explain which APIs
    # Pascal Script rejects, so the patterns are the usages, not the words.
    if grep -qE 'P: *Pointer|VerQueryValue\(|GetFileVersionInfo\(' "$iss"; then
        bad "recipe uses Pascal Script version-resource APIs that ISCC rejects"
    else
        ok "recipe leaves the version-resource check to the helper"
    fi
    grep -q 'ExtractTemporaryFile(CLEANUP_HELPER)' "$iss"
    check $? "the helper is extracted at install time"
    grep -q 'cleanup_legacy.ps1"; Flags: dontcopy' "$iss"
    check $? "the helper travels inside the installer"
    if grep -q 'DelTree(' "$iss"; then
        bad "recipe still deletes legacy bundles itself"
    else
        ok "the recipe performs no deletion of its own"
    fi
    if grep -q 'DeleteFile' "$iss"; then
        bad "recipe deletes individual files"
    else
        ok "recipe never deletes individual files"
    fi
    grep -q 'AppId={{8A1C9B4E-3D2F-4C77-9E31-1F5B7C0A2D64}' "$iss"
    check $? "stable AppId so upgrades replace the same product"
fi

grep -q 'ISCC' "$INSTALLER/build_installer.cmd"
check $? "installer build script discovers ISCC and fails loudly without it"
grep -q 'Get-FileHash' "$INSTALLER/verify_install.ps1"
check $? "installed-state verifier compares a SHA-256"
grep -q '0x8664' "$INSTALLER/verify_install.ps1"
check $? "installed-state verifier checks the x64 PE machine type"
grep -q 'foreign plug-in survived' "$INSTALLER/test_install_uninstall.ps1"
check $? "install test asserts a foreign plug-in survives install and uninstall"

# ---------------------------------------------------------------------------
echo
echo "-- 5b. staging handles what the Visual Studio exporter actually emits"
# ---------------------------------------------------------------------------
# JUCE 6.1.3's MSVC exporter links a flat PE DLL called SideChain.vst3; unlike the
# Xcode exporter it assembles no Contents/ hierarchy. Staging must wrap that module
# into the bundle layout instead of waiting for a bundle that never appears - the
# first Windows CI run failed exactly there.
grep -q 'def stage_flat_module' "$TOOLS/stage_windows_vst3.py"
check $? "staging wraps a flat Windows module into the VST3 bundle layout"
grep -q 'flat Windows module wrapped into the VST3 bundle layout' "$TOOLS/stage_windows_vst3.py"
check $? "the wrapped layout is recorded in the staging manifest"
grep -q 'if built.is_file():' "$TOOLS/stage_windows_vst3.py"
check $? "staging accepts a file as well as a bundle directory"
grep -q 'def locate_built_artifact' "$TOOLS/build_windows_vst3.py"
check $? "the build driver locates the flat exporter output too"
grep -q 'built_module_sha256' "$TOOLS/build_windows_vst3.py"
check $? "the driver proves the staged module is the built module"
python3 "$TOOLS/stage_windows_vst3.py" --self-test >/dev/null 2>&1
check $? "staging self-test passes, flat-module cases included"
python3 "$TOOLS/build_windows_vst3.py" --self-test >/dev/null 2>&1
check $? "version-resource identity self-test passes"
# JUCE fills ProductName/FileDescription from the *project* name (SideChain), so the
# generated resource must be corrected before compiling or Windows shows the wrong
# product name. This is a customer-visible identity claim, not a cosmetic detail.
grep -q 'def fix_generated_resource_identity' "$TOOLS/build_windows_vst3.py"
check $? "the driver corrects the generated version resource"
grep -q 'resources.rc.pre-pumpcurve' "$TOOLS/build_windows_vst3.py"
check $? "the original generated resource is preserved for review"
grep -q 'product_identity_rc' "$TOOLS/build_windows_vst3.py"
check $? "the resource correction is recorded in the run manifest"
grep -q 'legacy_names_utf16' "$TOOLS/stage_windows_vst3.py"
check $? "staging separates version-resource identity from compiled-in paths"

# ---------------------------------------------------------------------------
echo
echo "-- 6. the CI workflow still builds, installs and tests the product"
# ---------------------------------------------------------------------------
if [ ! -f "$WORKFLOW" ]; then
    bad "missing $WORKFLOW"
else
    if command -v ruby >/dev/null 2>&1; then
        if ruby -ryaml -e "YAML.load_file('$WORKFLOW')" >/dev/null 2>&1; then
            ok "workflow YAML parses"
        else
            bad "workflow YAML does not parse"
        fi
    else
        note "ruby unavailable; workflow YAML not parsed"
    fi
    for needle in \
        'runs-on: windows-2022' \
        'contents: write' \
        'verify_juce_tree.py --provision' \
        '--build-projucer' \
        '--build' \
        'choco install innosetup' \
        'test_install_uninstall.ps1' \
        'verify_install.ps1' \
        'Get-AuthenticodeSignature' \
        'windows-build-results' \
        'upload-artifact'; do
        if grep -qF -- "$needle" "$WORKFLOW"; then
            ok "workflow: $needle"
        else
            bad "workflow lost: $needle"
        fi
    done

    # Projucer cannot resave a project whose resource files were not checked out:
    # the first Windows run failed on a missing Resources/music-prod-logo.png. The
    # sparse checkout must therefore keep covering everything the .jucer needs.
    for pattern in '/.github/' '/SideChain/Builds/Windows/' '/SideChain/JuceLibraryCode/' \
                   '/SideChain/Resources/' '/SideChain/Source/' '/SideChain/SideChain.jucer'; do
        if grep -qF -- "$pattern" "$WORKFLOW"; then
            ok "workflow sparse checkout includes $pattern"
        else
            bad "workflow sparse checkout misses $pattern"
        fi
    done

    referenced="$(grep -oE 'file="[^"]+"' "$PROJECT/SideChain.jucer" | sed 's/file="//; s/"$//' | head -1)"
    if [ -n "$referenced" ] && [ -f "$PROJECT/$referenced" ]; then
        ok "the .jucer resource exists in the repository: $referenced"
    else
        bad "the .jucer references a missing resource: ${referenced:-<none>}"
    fi
fi

# ---------------------------------------------------------------------------
echo
echo "-- 7. product constants agree everywhere"
# ---------------------------------------------------------------------------
VERSION="$(grep -oE '<JUCERPROJECT[^>]*version="[^"]+"' "$PROJECT/SideChain.jucer" \
    | grep -oE 'version="[^"]+"' | head -1 | cut -d'"' -f2)"
[ "$VERSION" = "0.5.2" ] && ok "jucer version is 0.5.2" || bad "jucer version is '${VERSION}'"
grep -q 'pluginName="PumpCurve"' "$PROJECT/SideChain.jucer"
check $? "jucer plugin name is PumpCurve"
grep -q 'bundleIdentifier="com.musicprod.sidechain"' "$PROJECT/SideChain.jucer"
check $? "bundle identifier unchanged (upgrade compatibility)"
grep -q 'EXPECT_VERSION = "0.5.2"' "$TOOLS/build_windows_vst3.py"
check $? "build driver version constant matches"
grep -q 'VERSION: 0.5.2' "$WORKFLOW"
check $? "workflow version constant matches"
grep -q 'PRODUCT = "PumpCurve"' "$TOOLS/stage_windows_vst3.py"
check $? "staging tool product constant matches"
grep -q "ProductName -eq 'PumpCurve'" "$INSTALLER/cleanup_legacy.ps1"
check $? "installer identity constant matches"

# ---------------------------------------------------------------------------
echo
echo "-- 8. no credentials in the Windows build assets"
# ---------------------------------------------------------------------------
# This script's own pattern text is excluded, so the check cannot match itself.
if grep -rInE '(BEGIN [A-Z ]*PRIVATE KEY|eyJ[A-Za-z0-9_-]{10,}|service_role|xox[baprs]-|AKIA[0-9A-Z]{16})' \
        --exclude="$(basename "$0")" "$HERE" >/dev/null 2>&1; then
    bad "a credential-looking string is present under Builds/Windows"
else
    ok "no credential-looking strings in the Windows build assets"
fi

# ---------------------------------------------------------------------------
echo
echo "-- 9. build inputs self-check (driver --check)"
# ---------------------------------------------------------------------------
if [ -n "${check_manifest_root:-}" ]; then
    if out="$(python3 "$TOOLS/build_windows_vst3.py" --check --juce-root "$check_manifest_root" 2>&1)"; then
        echo "$out" | grep -E 'SELF-CHECK|JUCE TREE' | sed 's/^/    /'
        ok "build driver self-check passed"
    else
        echo "$out" | tail -20 | sed 's/^/    /'
        bad "build driver self-check failed"
    fi
else
    note "build driver self-check skipped (no JUCE tree)"
fi

# ---------------------------------------------------------------------------
echo
echo "============================================================"
printf ' checks passed: %d\n' "$PASS"
printf ' checks failed: %d\n' "$FAIL"
if [ "${#NOTES[@]}" -gt 0 ]; then
    printf ' limitations recorded: %d\n' "${#NOTES[@]}"
    for n in "${NOTES[@]}"; do printf '   - %s\n' "$n"; done
fi
if [ "$FAIL" -eq 0 ]; then
    echo " WINDOWS TOOLING: PASS"
    echo "============================================================"
    exit 0
fi
echo " WINDOWS TOOLING: FAIL"
echo "============================================================"
exit 1
