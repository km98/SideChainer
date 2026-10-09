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
