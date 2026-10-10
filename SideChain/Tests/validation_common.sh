#!/bin/bash
# ============================================================================
# SideChain - shared helpers for the test and validation scripts.
#
# Sourced, never executed directly:
#
#   source "$(dirname "$0")/validation_common.sh"
#   JUCE_MODULES="$(resolve_juce_modules)" || exit 1
#
# Provides:
#   resolve_juce_root      - validated JUCE root on stdout (diagnostics on stderr)
#   resolve_juce_modules   - "$root/modules" on stdout
#   juce_override_state    - "explicit" | "default" | "missing" (for messages/tests)
#   require_universal_binary <binary> <label>   - enforces arm64 + x86_64
#   bundle_content_hash <bundle>                - name-independent bundle fingerprint
#   stage_customer_product_names <products> <stage> [name]
#                          - stage the customer-visible PumpCurve.* bundle names and
#                            prove the rename preserved every byte and identifier
#
# JUCE discovery rules
#   * SIDECHAIN_JUCE_ROOT set: used verbatim. It must exist and contain
#     modules/juce_core and modules/juce_audio_plugin_client, otherwise the
#     helper fails - an invalid explicit override is never silently ignored.
#   * SIDECHAIN_JUCE_ROOT unset: the standard development layout is assumed,
#     i.e. the JUCE checkout sits next to the repository (<repo>/../HISE/JUCE).
#     This mirrors the default of SIDECHAIN_JUCE_ROOT in the Xcode project and
#     the .jucer module paths, so no absolute machine path is baked in.
# ============================================================================

# Directory of the sourcing script (the test scripts live in <repo>/SideChain/Tests).
sidechain_tests_dir()
{
    local src="${BASH_SOURCE[1]:-${BASH_SOURCE[0]}}"
    (cd "$(dirname "$src")" && pwd)
}

# Repository root derived from the sourcing script location.
sidechain_repo_root()
{
    local tests_dir
    tests_dir="$(sidechain_tests_dir)" || return 1
    (cd "$tests_dir/../.." && pwd)
}

# Reported by the tests to distinguish the two discovery paths.
juce_override_state()
{
    if [ -n "${SIDECHAIN_JUCE_ROOT:-}" ]; then
        echo "explicit"
    else
        echo "default"
    fi
}

# The path searched when SIDECHAIN_JUCE_ROOT is not supplied.
default_juce_root()
{
    local repo_root parent
    repo_root="$(sidechain_repo_root)" || return 1
    parent="$(cd "$repo_root/.." && pwd)" || return 1
    printf '%s/HISE/JUCE\n' "$parent"
}

juce_root_is_valid()
{
    local root="$1"
    [ -n "$root" ] && [ -d "$root" ] \
        && [ -d "$root/modules/juce_core" ] \
        && [ -d "$root/modules/juce_audio_plugin_client" ]
}

juce_usage_hint()
{
    printf '%s\n' \
        "  Supply the JUCE checkout explicitly, for example:" \
        "    SIDECHAIN_JUCE_ROOT=/path/to/HISE/JUCE $0" \
        "  The directory must contain modules/juce_core and" \
        "  modules/juce_audio_plugin_client."
}

# Echoes the validated JUCE root; returns non-zero with an actionable message.
resolve_juce_root()
{
    if [ -n "${SIDECHAIN_JUCE_ROOT:-}" ]; then
        local explicit="$SIDECHAIN_JUCE_ROOT"
        if ! juce_root_is_valid "$explicit"; then
            printf 'ERROR: SIDECHAIN_JUCE_ROOT="%s" is not a usable JUCE root.\n' "$explicit" >&2
            printf '       Expected "%s/modules/juce_core" and "%s/modules/juce_audio_plugin_client".\n' \
                "$explicit" "$explicit" >&2
            juce_usage_hint >&2
            return 1
        fi
        printf '%s\n' "$explicit"
        return 0
    fi

    local fallback
    fallback="$(default_juce_root)" || return 1
    if ! juce_root_is_valid "$fallback"; then
        printf 'ERROR: no JUCE installation found at the standard location "%s".\n' "$fallback" >&2
        printf '       The default layout expects the JUCE checkout next to the repository\n' >&2
        printf '       (a clone kept elsewhere has no sibling HISE/JUCE directory).\n' >&2
        juce_usage_hint >&2
        return 1
    fi
    printf '%s\n' "$fallback"
}

resolve_juce_modules()
{
    local root
    root="$(resolve_juce_root)" || return 1
    printf '%s/modules\n' "$root"
}

# Fails unless the given Mach-O binary contains both arm64 and x86_64.
require_universal_binary()
{
    local binary="$1"
    local label="${2:-$1}"
    if [ ! -f "$binary" ]; then
        printf '[FAIL] %s: missing binary (%s)\n' "$label" "$binary"
        return 1
    fi
    local archs
    archs="$(lipo -archs "$binary" 2>/dev/null)"
    if [ -z "$archs" ]; then
        printf '[FAIL] %s: lipo could not read architectures (%s)\n' "$label" "$binary"
        return 1
    fi
    local ok=1 arch
    for arch in arm64 x86_64; do
        case " $archs " in
            *" $arch "*) ;;
            *) ok=0 ;;
        esac
    done
    if [ "$ok" != "1" ]; then
        printf '[FAIL] %s: expected arm64 + x86_64, found "%s" (%s)\n' "$label" "$archs" "$binary"
        return 1
    fi
    printf '[PASS] %s: universal slices "%s" (%s)\n' "$label" "$archs" "$binary"
    return 0
}

# ---------------------------------------------------------------------------
# Customer-visible product naming
# ---------------------------------------------------------------------------
# The Xcode target (and therefore the built bundle directories) is called
# SideChain, while the product is PumpCurve. Hosts that derive a displayed name
# from the bundle filename - FL Studio's VST3 database does - therefore show the
# wrong product unless the bundle is *staged* under the product name. Renaming the
# bundle directory is safe and sufficient: bundle identifiers, AU type/subtype/
# manufacturer, factory symbols, parameter IDs and the executable itself are all
# unchanged, so existing projects keep resolving and the AU identity is stable.
PUMPCURVE_DISPLAY_NAME="PumpCurve"
PUMPCURVE_BUNDLE_ID="com.musicprod.sidechain"

# Name-independent fingerprint of every file inside a bundle. Two bundles with
# identical fingerprints are byte-identical, whatever they are called.
bundle_content_hash()
{
    local bundle="$1"
    [ -d "$bundle" ] || return 1
    ( cd "$bundle" && find . -type f -exec shasum -a 256 {} + | LC_ALL=C sort -k2 | shasum -a 256 | awk '{print $1}' )
}

# plist_read <plist> <key> -> value on stdout (empty when absent)
plist_read()
{
    plutil -extract "$2" raw -o - "$1" 2>/dev/null || true
}

# Stage <products>/SideChain.component and <products>/SideChain.vst3 as
# <stage>/PumpCurve.component and <stage>/PumpCurve.vst3, then prove that the
# rename changed no byte and no identity. Fails (non-zero) instead of overwriting
# an existing staged bundle, so a stale staging directory can never masquerade as
# a fresh build.
#
# Usage: stage_customer_product_names <products_dir> <stage_dir> [display_name]
stage_customer_product_names()
{
    local products="$1" stage="$2" display="${3:-$PUMPCURVE_DISPLAY_NAME}"
    local ok=1 pair src dst name sub src_hash dst_hash

    for pair in "SideChain.component:PumpCurve.component:Components" \
                "SideChain.vst3:PumpCurve.vst3:VST3"; do
        src="${pair%%:*}"; rest="${pair#*:}"; dst="${rest%%:*}"; sub="${rest##*:}"
        name="$(basename "$src")"

        if [ ! -d "$products/$src" ]; then
            printf '[FAIL] staged product: missing built bundle %s\n' "$products/$src"
            ok=0
            continue
        fi
        if [ -e "$stage/$dst" ]; then
            printf '[FAIL] staged product: %s already exists; refusing to overwrite\n' "$stage/$dst"
            ok=0
            continue
        fi

        mkdir -p "$stage" || { ok=0; continue; }
        ditto "$products/$src" "$stage/$dst" || { printf '[FAIL] ditto failed for %s\n' "$src"; ok=0; continue; }

        src_hash="$(bundle_content_hash "$products/$src")"
        dst_hash="$(bundle_content_hash "$stage/$dst")"
        if [ "$src_hash" != "$dst_hash" ]; then
            printf '[FAIL] %s: staged content differs from the built bundle (%s vs %s)\n' "$dst" "$dst_hash" "$src_hash"
            ok=0
        else
            printf '[PASS] %s: content byte-identical to %s (%s)\n' "$dst" "$name" "$dst_hash"
        fi

        local src_id dst_id
        src_id="$(plist_read "$products/$src/Contents/Info.plist" CFBundleIdentifier)"
        dst_id="$(plist_read "$stage/$dst/Contents/Info.plist" CFBundleIdentifier)"
        if [ "$src_id" = "$PUMPCURVE_BUNDLE_ID" ] && [ "$dst_id" = "$src_id" ]; then
            printf '[PASS] %s: bundle identifier preserved (%s)\n' "$dst" "$dst_id"
        else
            printf '[FAIL] %s: bundle identifier not preserved (source "%s", staged "%s", expected "%s")\n' \
                "$dst" "$src_id" "$dst_id" "$PUMPCURVE_BUNDLE_ID"
            ok=0
        fi

        local src_meta dst_meta src_name
        src_meta="$(plutil -extract AudioComponents json -o - "$products/$src/Contents/Info.plist" 2>/dev/null)"
        dst_meta="$(plutil -extract AudioComponents json -o - "$stage/$dst/Contents/Info.plist" 2>/dev/null)"
        if [ -n "$src_meta" ] && [ "$src_meta" = "$dst_meta" ]; then
            printf '[PASS] %s: Audio Unit registration metadata unchanged by the rename\n' "$dst"
        elif [ -n "$src_meta" ]; then
            printf '[FAIL] %s: Audio Unit registration metadata changed by the rename\n' "$dst"
            ok=0
        fi

        src_name="$(plist_read "$stage/$dst/Contents/Info.plist" CFBundleName)"
        local src_display
        src_display="$(plist_read "$stage/$dst/Contents/Info.plist" CFBundleDisplayName)"
        if [ "$src_name" = "$display" ] && [ "$src_display" = "$display" ]; then
            printf '[PASS] %s: displayed name is "%s"\n' "$dst" "$display"
        else
            printf '[FAIL] %s: displayed name is "%s"/"%s", expected "%s"\n' "$dst" "$src_name" "$src_display" "$display"
            ok=0
        fi

        local binary="$stage/$dst/Contents/MacOS/SideChain"
        if [ -f "$binary" ]; then
            require_universal_binary "$binary" "$dst executable" || ok=0
        else
            printf '[FAIL] %s: expected executable missing (%s)\n' "$dst" "$binary"
            ok=0
        fi
    done

    if [ "$ok" = "1" ]; then
        ( cd "$stage" && find . -type f -exec shasum -a 256 {} + | LC_ALL=C sort -k2 > SHA256SUMS ) 2>/dev/null
        printf '[PASS] staged product names ready in %s (file manifest in SHA256SUMS)\n' "$stage"
        return 0
    fi
    return 1
}
