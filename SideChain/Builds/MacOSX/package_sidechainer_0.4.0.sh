#!/bin/bash
#
# package_sidechainer_0.4.0.sh
#
# Builds an unsigned SideChainer macOS installer package from already
# verified signed production plugin bundles.
#
# PACKAGING FIX (2026-10-04)
# ---------------------------
# The previous ad-hoc packaging run created the staging directories
#
#     <pkgroot>/Library/Audio/Plug-Ins/Components
#     <pkgroot>/Library/Audio/Plug-Ins/VST3
#
# with `mkdir -m 700 -p ...`, which applied mode 0700 to those two leaf
# directories.  pkgbuild was invoked with `--ownership recommended`, which
# correctly archieves UID/GID as root:wheel but never changes modes - modes
# are always taken from the on-disk staging tree.  The resulting package
# therefore archived
#
#     ./Library/Audio/Plug-Ins/VST3        40700  0  wheel
#     ./Library/Audio/Plug-Ins/Components  40700  0  wheel
#
# and the macOS Installer installed /Library/Audio/Plug-Ins/VST3 as
# drwx------ root:wheel, which normal user-level DAW processes cannot
# traverse, so the VST3 was invisible to hosts.
#
# This script creates every staging directory with mode 0755 (umask 022 +
# explicit chmod), never touches anything inside the signed plugin bundles,
# and refuses to emit a package unless the archived BOM records every staging
# parent and both plugin directories as 40755 / uid 0 / gid wheel.
#
# Usage:
#   ./package_sidechainer_0.4.0.sh --version 0.4.1 \\
#       --input <signed-plugin-source.pkg> \\
#       --expected-input-sha <sha256> --output <new-unsigned.pkg>
#
# Input defaults to the verified production package whose bundles are the
# canonical signed plugin payloads.  Override with:
#   --input  <verified-source.pkg>
#   --expected-input-sha <sha256>   (env EXPECTED_INPUT_SHA also works)

set -euo pipefail
umask 022

INPUT_PKG="/Users/martin/Documents/SideChain/SideChain/Builds/MacOSX/build/release-assets/SideChainer-0.4.0-macOS-PRODUCTION-20261004.pkg"
EXPECTED_INPUT_SHA="449d3df87dde51b8f55f7107734c81e8ae3976f2985e54f2d3b319a044306abf"
OUTPUT_PKG=""
IDENT="com.musicprod.sidechain.pkg"
VERSION="0.4.1"
INSTALL_LOCATION="/"

die() { echo "ERROR: $*" >&2; exit 1; }
sha256of() { shasum -a 256 "$1" | awk '{print $1}'; }

while [ $# -gt 0 ]; do
    case "$1" in
        --output)               OUTPUT_PKG="${2:?--output needs a path}"; shift 2 ;;
        --input)                INPUT_PKG="${2:?--input needs a path}"; shift 2 ;;
        --expected-input-sha)   EXPECTED_INPUT_SHA="${2:?needs a sha256}"; shift 2 ;;
        --version)              VERSION="${2:?--version needs a version}"; shift 2 ;;
        -h|--help)              sed -n '2,40p' "$0"; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
done

[ -n "$OUTPUT_PKG" ] || die "--output <new-unsigned.pkg> is required"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "invalid --version: $VERSION"
[ -f "$INPUT_PKG" ] || die "input package not found: $INPUT_PKG"
[ -e "$OUTPUT_PKG" ] && die "refusing to overwrite existing output: $OUTPUT_PKG"

echo "== Preflight =="
echo "input : $INPUT_PKG"
actual="$(sha256of "$INPUT_PKG")"
echo "input sha256: $actual"
[ "$actual" = "$EXPECTED_INPUT_SHA" ] || die "input package hash mismatch (expected $EXPECTED_INPUT_SHA)"

TMP="$(mktemp -d /tmp/sidechainer-pkgbuild.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

echo
echo "== Extract verified plugin bundles from input payload =="
pkgutil --expand-full "$INPUT_PKG" "$TMP/src" >/dev/null
SRC_PAYLOAD="$TMP/src/Payload"
[ -d "$SRC_PAYLOAD/Library/Audio/Plug-Ins/Components/SideChain.component" ] || die "source AU bundle missing"
[ -d "$SRC_PAYLOAD/Library/Audio/Plug-Ins/VST3/SideChain.vst3" ] || die "source VST3 bundle missing"

echo
echo "== Build staging root with 0755 directories (FIX) =="
PKGROOT="$TMP/pkgroot"
mkdir -p "$PKGROOT/Library/Audio/Plug-Ins/Components"
mkdir -p "$PKGROOT/Library/Audio/Plug-Ins/VST3"
chmod 0755 "$PKGROOT" \
           "$PKGROOT/Library" \
           "$PKGROOT/Library/Audio" \
           "$PKGROOT/Library/Audio/Plug-Ins" \
           "$PKGROOT/Library/Audio/Plug-Ins/Components" \
           "$PKGROOT/Library/Audio/Plug-Ins/VST3"

# Copy the signed bundles verbatim; ditto preserves modes/symlinks/xattrs.
# Nothing inside these bundles is chmod'ed.
ditto "$SRC_PAYLOAD/Library/Audio/Plug-Ins/Components/SideChain.component" \
      "$PKGROOT/Library/Audio/Plug-Ins/Components/SideChain.component"
ditto "$SRC_PAYLOAD/Library/Audio/Plug-Ins/VST3/SideChain.vst3" \
      "$PKGROOT/Library/Audio/Plug-Ins/VST3/SideChain.vst3"

echo
echo "== Staging assertions (pre-build gate) =="
for d in "$PKGROOT" "$PKGROOT/Library" "$PKGROOT/Library/Audio" \
         "$PKGROOT/Library/Audio/Plug-Ins" \
         "$PKGROOT/Library/Audio/Plug-Ins/Components" \
         "$PKGROOT/Library/Audio/Plug-Ins/VST3"; do
    mode="$(stat -f '%Lp' "$d")"
    echo "  $mode  $d"
    [ "$mode" = "755" ] || die "staging directory is not 0755: $d"
done

# Bundle interiors must be byte-identical to the verified source payload.
# Hash files relative to <...>/Library/Audio/Plug-Ins so src/dst/pkg trees
# produce directly diffable output.
list_hashes() {
    ( cd "$1" && find Components/SideChain.component VST3/SideChain.vst3 \( -type f -o -type l \) | LC_ALL=C sort | while read -r f; do
        printf '%s  %s\n' "$(shasum -a 256 "$f" | awk '{print $1}')" "$f"
    done )
}
list_hashes "$SRC_PAYLOAD/Library/Audio/Plug-Ins" > "$TMP/src-hashes.txt"
list_hashes "$PKGROOT/Library/Audio/Plug-Ins"      > "$TMP/dst-hashes.txt"
diff -u "$TMP/src-hashes.txt" "$TMP/dst-hashes.txt" \
    || die "staged bundles differ from the verified source payload"
echo "  bundle contents byte-identical to source payload"

codesign --verify --deep --strict "$PKGROOT/Library/Audio/Plug-Ins/Components/SideChain.component" \
    || die "staged AU signature invalid"
codesign --verify --deep --strict "$PKGROOT/Library/Audio/Plug-Ins/VST3/SideChain.vst3" \
    || die "staged VST3 signature invalid"
echo "  staged bundle code signatures valid"

echo
echo "== pkgbuild --ownership recommended =="
pkgbuild --root "$PKGROOT" \
         --identifier "$IDENT" \
         --version "$VERSION" \
         --install-location "$INSTALL_LOCATION" \
         --ownership recommended \
         "$OUTPUT_PKG"

echo
echo "== Post-build BOM gate =="
CHECK="$TMP/check"
pkgutil --expand-full "$OUTPUT_PKG" "$CHECK" >/dev/null
BOM_LINES="$(lsbom -p 'fmuG' "$CHECK/Bom")"

expect_line() {
    local want="$1"
    printf '%s\n' "$BOM_LINES" | grep -F -x -- "$want" \
        || die "BOM assertion failed: expected exactly [$want]"
}
BOM_TAB=$'	'
expect_line ".${BOM_TAB}40755${BOM_TAB}0${BOM_TAB}wheel"
expect_line "./Library${BOM_TAB}40755${BOM_TAB}0${BOM_TAB}wheel"
expect_line "./Library/Audio${BOM_TAB}40755${BOM_TAB}0${BOM_TAB}wheel"
expect_line "./Library/Audio/Plug-Ins${BOM_TAB}40755${BOM_TAB}0${BOM_TAB}wheel"
expect_line "./Library/Audio/Plug-Ins/Components${BOM_TAB}40755${BOM_TAB}0${BOM_TAB}wheel"
expect_line "./Library/Audio/Plug-Ins/VST3${BOM_TAB}40755${BOM_TAB}0${BOM_TAB}wheel"
expect_line "./Library/Audio/Plug-Ins/Components/SideChain.component${BOM_TAB}40755${BOM_TAB}0${BOM_TAB}wheel"
expect_line "./Library/Audio/Plug-Ins/VST3/SideChain.vst3${BOM_TAB}40755${BOM_TAB}0${BOM_TAB}wheel"

if printf '%s\n' "$BOM_LINES" | grep -qE '(^|[[:space:]])[0-9]*700($|[[:space:]])'; then
    printf '%s\n' "$BOM_LINES" | grep -E '700' >&2 || true
    die "BOM still contains mode 0700 entries"
fi
echo "  every staging parent and plugin directory archived as 40755 / uid 0 / gid wheel"

# Payload file bytes must still match the verified source payload exactly.
list_hashes "$CHECK/Payload/Library/Audio/Plug-Ins" > "$TMP/pkg-hashes.txt"
diff -u "$TMP/src-hashes.txt" "$TMP/pkg-hashes.txt" \
    || die "packaged bundle bytes differ from verified source payload"
echo "  packaged bundle bytes identical to verified source payload"

# Extract identity from the <pkg-info> element only (the XML declaration
# also contains a version="1.0" attribute and must not be matched).
PKGINFO_LINE="$(grep -m1 '<pkg-info ' "$CHECK/PackageInfo")" || die "PackageInfo has no <pkg-info> element"
PKGINFO_ID="$(printf '%s\n' "$PKGINFO_LINE" | sed -n 's/.* identifier="\([^"]*\)".*/\1/p')"
PKGINFO_VER="$(printf '%s\n' "$PKGINFO_LINE" | sed -n 's/.* version="\([^"]*\)".*/\1/p')"
[ "$PKGINFO_ID" = "$IDENT" ] || die "PackageInfo identifier mismatch: $PKGINFO_ID"
[ "$PKGINFO_VER" = "$VERSION" ] || die "PackageInfo version mismatch: $PKGINFO_VER"
echo "  PackageInfo identifier=$PKGINFO_ID version=$PKGINFO_VER"

echo
echo "UNSIGNED PACKAGE OK: $OUTPUT_PKG"
