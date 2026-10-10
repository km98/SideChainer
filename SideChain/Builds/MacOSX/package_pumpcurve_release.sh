#!/bin/bash
# ============================================================================
# PumpCurve - macOS release packaging (customer installer) driver.
#
# Turns an already-built universal Release into the customer artifacts:
#
#   1. stages the bundles under their customer-visible names
#      (PumpCurve.component / PumpCurve.vst3) and PROVES the rename changed no
#      byte - see stage_customer_product_names in Tests/validation_common.sh
#   2. builds a /Library installer PKG that carries the cleanup scripts from
#      Builds/MacOSX/installer-scripts/ (they remove only OUR legacy SideChain.* /
#      SideChainer.* bundles, so the same aufx/SdCh/Musc identity is never
#      registered twice after an upgrade)
#   3. signs with the Developer ID Installer identity when one is available
#   4. optionally wraps the PKG in a signed DMG with READ-ME-FIRST + SHA256SUMS
#
# Everything is written OUTSIDE the repository (a temp directory by default), so
# packaging can never dirty the checkout or the tracked release-assets tree.
# Existing files are never overwritten.
#
# Usage
#   Builds/MacOSX/package_pumpcurve_release.sh --products DIR [options]
#
#   --products DIR     directory containing the built SideChain.component and
#                      SideChain.vst3 (e.g. SideChain/Tests/validate_all.sh's
#                      out-dir/products)
#   --out-dir DIR      where the artifacts are written (default: a fresh temp dir)
#   --version X.Y.Z    version for the file names and the package (default: 0.4.1)
#   --dmg              also build a signed DMG around the PKG
#   --readme FILE      customer read-me copied into the DMG
# Exit code: 0 only when every gate passed.
# ============================================================================

set -u
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# Builds/MacOSX -> the product directory -> the repository root.
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
# shellcheck source=../../../SideChain/Tests/validation_common.sh
source "$REPO_ROOT/SideChain/Tests/validation_common.sh" || {
    echo "ERROR: cannot load $REPO_ROOT/SideChain/Tests/validation_common.sh" >&2
    exit 1
}
for helper in bundle_content_hash stage_customer_product_names; do
    command -v "$helper" >/dev/null 2>&1 || { echo "ERROR: helper '$helper' unavailable" >&2; exit 1; }
done

PRODUCTS=""
OUT_DIR=""
VERSION="0.4.1"
MAKE_DMG=0
README=""
INSTALLER_IDENT="Developer ID Installer: Martin Kadziolka (3A4R5EKM7V)"
PKG_IDENT="com.musicprod.sidechain.pkg"
INSTALLER_SCRIPTS="$SCRIPT_DIR/installer-scripts"

usage() { sed -n '2,31p' "$0"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --products) shift; [ $# -gt 0 ] || { echo "ERROR: --products needs a directory" >&2; exit 2; }; PRODUCTS="$1" ;;
        --out-dir)  shift; [ $# -gt 0 ] || { echo "ERROR: --out-dir needs a directory" >&2; exit 2; }; OUT_DIR="$1" ;;
        --version)  shift; [ $# -gt 0 ] || { echo "ERROR: --version needs a value" >&2; exit 2; }; VERSION="$1" ;;
        --dmg)      MAKE_DMG=1 ;;
        --readme)   shift; [ $# -gt 0 ] || { echo "ERROR: --readme needs a file" >&2; exit 2; }; README="$1" ;;
        -h|--help)  usage; exit 0 ;;
        *) echo "ERROR: unknown argument '$1'" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

[ -n "$PRODUCTS" ] || { echo "ERROR: --products is required (see --help)" >&2; exit 2; }
[ -d "$PRODUCTS" ] || { echo "ERROR: no such products directory: $PRODUCTS" >&2; exit 2; }
[ -x "$INSTALLER_SCRIPTS/preinstall" ]  || { echo "ERROR: missing $INSTALLER_SCRIPTS/preinstall" >&2; exit 2; }
[ -x "$INSTALLER_SCRIPTS/postinstall" ] || { echo "ERROR: missing $INSTALLER_SCRIPTS/postinstall" >&2; exit 2; }

if [ -z "$OUT_DIR" ]; then
    OUT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/pumpcurve-release.XXXXXX")" || exit 1
fi
case "$OUT_DIR" in
    "$REPO_ROOT"/*) echo "ERROR: --out-dir inside the repository is refused (keep the checkout clean)" >&2; exit 2 ;;
esac
mkdir -p "$OUT_DIR" || exit 1

STAGE="$OUT_DIR/staged-products"
PKG="$OUT_DIR/PumpCurve-${VERSION}-macOS-Universal.pkg"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/pumpcurve-pkg.XXXXXX")" || exit 1
trap 'rm -rf "$TMP"' EXIT

printf 'PumpCurve macOS release packaging\n'
printf '  products:   %s\n' "$PRODUCTS"
printf '  output:     %s\n' "$OUT_DIR"
printf '  version:    %s\n\n' "$VERSION"

# ---------------------------------------------------------------------------
printf '1. staging the customer-visible bundle names\n'
# ---------------------------------------------------------------------------
if [ -e "$STAGE" ]; then
    echo "ERROR: $STAGE already exists; refusing to reuse a staging directory" >&2
    exit 1
fi
# Capture the helper's own output and exit status separately: piping into sed
# would report sed's status and could hide a failed staging run.
if ! stage_customer_product_names "$PRODUCTS" "$STAGE" >"$TMP/staging.log" 2>&1; then
    sed 's/^/  /' "$TMP/staging.log"
    echo "ERROR: product-name staging failed" >&2
    exit 1
fi
sed 's/^/  /' "$TMP/staging.log"

# ---------------------------------------------------------------------------
printf '\n2. building the signed installer package\n'
# ---------------------------------------------------------------------------
[ -e "$PKG" ] && { echo "ERROR: refusing to overwrite $PKG" >&2; exit 1; }

ROOT="$TMP/pkgroot"
mkdir -p "$ROOT/Library/Audio/Plug-Ins/Components" "$ROOT/Library/Audio/Plug-Ins/VST3"
for d in "$ROOT" "$ROOT/Library" "$ROOT/Library/Audio" "$ROOT/Library/Audio/Plug-Ins" \
         "$ROOT/Library/Audio/Plug-Ins/Components" "$ROOT/Library/Audio/Plug-Ins/VST3"; do
    chmod 0755 "$d"
    [ "$(stat -f '%Lp' "$d")" = "755" ] || { echo "ERROR: staging dir not 0755: $d" >&2; exit 1; }
done
ditto "$STAGE/PumpCurve.component" "$ROOT/Library/Audio/Plug-Ins/Components/PumpCurve.component"
ditto "$STAGE/PumpCurve.vst3"      "$ROOT/Library/Audio/Plug-Ins/VST3/PumpCurve.vst3"

for b in PumpCurve.component PumpCurve.vst3; do
    case "$b" in *.component) sub=Components;; *) sub=VST3;; esac
    a="$(bundle_content_hash "$STAGE/$b")"
    c="$(bundle_content_hash "$ROOT/Library/Audio/Plug-Ins/$sub/$b")"
    [ -n "$a" ] && [ -n "$c" ] || { echo "ERROR: could not hash $b (empty fingerprint)" >&2; exit 1; }
    [ "$a" = "$c" ] || { echo "ERROR: $b staged payload differs from the staged source" >&2; exit 1; }
    echo "  $b: payload identical ($a)"
done

pkgbuild --root "$ROOT" --identifier "$PKG_IDENT" --version "$VERSION" \
         --scripts "$INSTALLER_SCRIPTS" --install-location / --ownership recommended "$PKG" \
    | sed 's/^/  /' || exit 1

pkgutil --expand-full "$PKG" "$TMP/chk" >/dev/null || exit 1
for want in Scripts/preinstall Scripts/postinstall; do
    [ -f "$TMP/chk/$want" ] || { echo "ERROR: package is missing $want" >&2; exit 1; }
done
echo "  cleanup scripts embedded: $(ls -1 "$TMP/chk/Scripts" | tr '\n' ' ')"

BOM="$(lsbom -p fmuG "$TMP/chk/Bom")"
if printf '%s\n' "$BOM" | grep -qE 'SideChain\.(component|vst3)|SideChainer\.(component|vst3)'; then
    echo "ERROR: payload still contains a legacy bundle name" >&2
    exit 1
fi
printf '%s\n' "$BOM" | grep -qE 'PumpCurve\.component' || { echo "ERROR: payload lacks PumpCurve.component" >&2; exit 1; }
printf '%s\n' "$BOM" | grep -qE 'PumpCurve\.vst3'      || { echo "ERROR: payload lacks PumpCurve.vst3" >&2; exit 1; }
printf '%s\n' "$BOM" | grep -qE '(^|[[:space:]])[0-9]*700($|[[:space:]])' && { echo "ERROR: BOM contains 0700 entries" >&2; exit 1; }
echo "  BOM: PumpCurve bundles present, no legacy names, no 0700 entries"

# NOTE: "Developer ID Installer" identities are NOT matched by
# `security find-identity -p codesigning` (that policy selects Application
# identities), which is why the plain listing is used here.
if security find-identity -v 2>/dev/null | grep -q "Developer ID Installer"; then
    echo "  signing with an available Developer ID Installer identity"
    productsign --sign "$INSTALLER_IDENT" "$PKG" "$PKG.signed" >/dev/null || exit 1
    mv "$PKG.signed" "$PKG"
    pkgutil --check-signature "$PKG" | sed 's/^/  /'
else
    echo "  WARNING: installer identity not in this keychain; the package is UNSIGNED"
fi

pkgutil --expand-full "$PKG" "$TMP/chk2" >/dev/null || exit 1
for b in Components/PumpCurve.component VST3/PumpCurve.vst3; do
    a="$(bundle_content_hash "$STAGE/$(basename "$b")")"
    c="$(bundle_content_hash "$TMP/chk2/Payload/Library/Audio/Plug-Ins/$b")"
    [ -n "$a" ] && [ -n "$c" ] || { echo "ERROR: could not hash the payload for $b" >&2; exit 1; }
    [ "$a" = "$c" ] || { echo "ERROR: signed payload differs for $b" >&2; exit 1; }
done
echo "  signed payload is byte-identical to the staged bundles"

printf '\n3. checksums\n'
sha() { shasum -a 256 "$1" | awk '{print $1}'; }
printf '%s  %s\n' "$(sha "$PKG")" "$(basename "$PKG")" > "$OUT_DIR/SHA256SUMS"

# ---------------------------------------------------------------------------
printf '\n4. disk image\n'
# ---------------------------------------------------------------------------
if [ "$MAKE_DMG" = "1" ]; then
    DMG="$OUT_DIR/PumpCurve-${VERSION}-macOS-Universal.dmg"
    DMG_STAGE="$TMP/dmg-stage"
    mkdir -p "$DMG_STAGE"
    cp -p "$PKG" "$DMG_STAGE/"
    [ -n "$README" ] && [ -f "$README" ] && cp -p "$README" "$DMG_STAGE/READ-ME-FIRST.txt"
    cp -p "$OUT_DIR/SHA256SUMS" "$DMG_STAGE/"
    hdiutil create -volname "PumpCurve ${VERSION}" -srcfolder "$DMG_STAGE" -ov -format UDZO "$DMG" >/dev/null || exit 1
    if security find-identity -v -p codesigning 2>/dev/null | grep -q "Developer ID Application"; then
        codesign --force --sign "Developer ID Application: Martin Kadziolka (3A4R5EKM7V)" --timestamp "$DMG" || exit 1
        codesign --verify --verbose=2 "$DMG" 2>&1 | sed 's/^/  /'
    fi
    printf '%s  %s\n' "$(sha "$DMG")" "$(basename "$DMG")" >> "$OUT_DIR/SHA256SUMS"
    echo "  DMG: $DMG"
else
    echo "  (skipped: pass --dmg)"
fi

printf '\n5. Gatekeeper / notarisation status\n'
spctl -a -vv -t install "$PKG" 2>&1 | sed 's/^/  /' || true
if xcrun notarytool history --keychain-profile AC_PASSWORD >/dev/null 2>&1; then
    echo "  a notarisation keychain profile exists: submit with"
    echo "    xcrun notarytool submit \"$PKG\" --keychain-profile AC_PASSWORD --wait"
else
    echo "  NOT notarised: no Apple credentials are available on this machine."
    echo "  Required to publish: an app-specific password or App Store Connect API key,"
    echo "  stored with 'xcrun notarytool store-credentials --keychain-profile AC_PASSWORD'."
    echo "  Until then Gatekeeper reports the package as 'Unnotarized Developer ID'."
fi

printf '\nartifacts in %s\n' "$OUT_DIR"
ls -la "$OUT_DIR" | sed 's/^/  /'
cat "$OUT_DIR/SHA256SUMS" | sed 's/^/  /'
