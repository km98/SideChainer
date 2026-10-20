#!/bin/bash
#
# publish_pumpcurve_beta.sh — explicit PumpCurve beta publish command.
#
# The PumpCurve counterpart of publish_sidechainer_beta.sh. Same private beta
# channel and the same upload token; only the product segment differs, so a
# PumpCurve artifact can never land on (or overwrite) a SideChainer object.
#
# Usage:
#   ./publish_pumpcurve_beta.sh macos   <artifact-path> <expected-sha256> [expected-size-bytes]
#   ./publish_pumpcurve_beta.sh windows <artifact-path> <expected-sha256> [expected-size-bytes]
#   ./publish_pumpcurve_beta.sh <platform> <artifact> <sha256> --preflight-only
#
# Platforms and the fixed objects they write (bucket sidechainer-test):
#   macos   -> pumpcurve/macos/latest    served at https://music-prod.com/pumpcurve/latestbeta
#   windows -> pumpcurve/windows/latest  served at https://music-prod.com/pumpcurve/windows/latestbeta
#
# What it does (all-or-nothing, fail closed):
#   1. verify the local artifact (exists, size if given, SHA-256 must match)
#   2. assert the product-scoped test object is NOT publicly readable
#   3. retrieve the upload token from the macOS Keychain at runtime
#      (never printed, never written to disk, never placed in a command-line
#       argument — it is fed to curl via an stdin config so it only exists in
#       memory)
#   4. upload the raw artifact to sidechainer-test-upload/pumpcurve/<platform>
#   5. verify the server-reported metadata (success/platform/filename/size/sha)
#   6. download the stored object back through the canonical beta route
#      (alias -> function -> 300 s signed URL), verify the served filename and
#      the bytes' SHA-256, and therefore prove the public route works
#
# This script NEVER touches the production release system: no release_products,
# product_releases, release_artifacts rows, no `releases` bucket, no
# music-prod-studio-api routes, no function deployments, no website deploys.
#
# Prerequisites the owner supplies (none of them exist in a bare checkout):
#   * the upload token in the macOS Keychain under the channel service name
#     (default "Music-Prod SideChainer Beta Upload"; override with
#      PUMPCURVE_UPLOAD_KC_SERVICE)
#   * the deployable functions and the website alias, i.e.
#     `supabase functions deploy sidechainer-test-upload|download` and a live
#     /pumpcurve/latestbeta rewrite in public/.htaccess
#
# NOTE on macOS artifacts: prefer the STAPLED .pkg. A .dmg that was notarised
# while containing an un-stapled inner .pkg still installs online, but the
# stapled .pkg verifies without network access.
#
# The publish step is intentionally a deliberate manual command: it is NOT
# wired into the build pipeline.

set -euo pipefail
umask 077

PROJECT_REF='wfpeajmdojcjqyrsnxbk'
BUCKET='sidechainer-test'
PRODUCT_SEGMENT='pumpcurve'
FN_BASE="https://${PROJECT_REF}.supabase.co/functions/v1"
UPLOAD_FN_BASE="${FN_BASE}/sidechainer-test-upload"
DOWNLOAD_FN_BASE="${FN_BASE}/sidechainer-test-download"
ALIAS_BASE='https://music-prod.com/pumpcurve'
DEFAULT_KC_SERVICE='Music-Prod SideChainer Beta Upload'
KC_SERVICE="${PUMPCURVE_UPLOAD_KC_SERVICE:-$DEFAULT_KC_SERVICE}"

die() { echo "ERROR: $*" >&2; exit 1; }

usage() {
    sed -n '3,20p' "$0" >&2
    exit 2
}

# ---------------------------------------------------------------- arguments
PREFLIGHT_ONLY=0
ARGS=()
for a in "$@"; do
    case "$a" in
        --preflight-only) PREFLIGHT_ONLY=1 ;;
        -h|--help) usage ;;
        *) ARGS+=("$a") ;;
    esac
done
set -- ${ARGS[@]+"${ARGS[@]}"}

[ $# -ge 3 ] || usage
PLATFORM="$1"; ART="$2"; EXP_SHA="$3"; EXP_SIZE="${4:-}"

case "$PLATFORM" in
    macos|windows) ;;
    *) die "unknown platform '$PLATFORM' (expected macos or windows)" ;;
esac

# Per-platform endpoints. macOS is the bare product alias; Windows is nested,
# matching the .htaccess rewrites.
if [ "$PLATFORM" = 'macos' ]; then
    UPLOAD_ENDPOINT="${UPLOAD_FN_BASE}/${PRODUCT_SEGMENT}/macos"
    DOWNLOAD_FN="${DOWNLOAD_FN_BASE}/${PRODUCT_SEGMENT}/latestbeta"
    ALIAS="${ALIAS_BASE}/latestbeta"
else
    UPLOAD_ENDPOINT="${UPLOAD_FN_BASE}/${PRODUCT_SEGMENT}/windows"
    DOWNLOAD_FN="${DOWNLOAD_FN_BASE}/${PRODUCT_SEGMENT}/windows/latestbeta"
    ALIAS="${ALIAS_BASE}/windows/latestbeta"
fi
OBJECT_KEY="${PRODUCT_SEGMENT}/${PLATFORM}/latest"
PUBLIC_OBJ="https://${PROJECT_REF}.supabase.co/storage/v1/object/public/${BUCKET}/${OBJECT_KEY}"

[ -f "$ART" ] || die "artifact not found: $ART"
FN="$(basename "$ART")"
SIZE="$(stat -f '%z' "$ART")"

echo "== 1. local artifact preflight =="
echo "   platform: $PLATFORM"
echo "   object:   $BUCKET/$OBJECT_KEY"
echo "   path:     $ART"
echo "   size:     $SIZE"
if [ -n "$EXP_SIZE" ] && [ "$SIZE" != "$EXP_SIZE" ]; then
    die "size mismatch: got $SIZE, expected $EXP_SIZE"
fi
SHA="$(shasum -a 256 "$ART" | awk '{print $1}')"
[ "$SHA" = "$EXP_SHA" ] || die "sha256 mismatch: got $SHA, expected $EXP_SHA"
echo "   sha256:   $SHA  (matches expected)"

# The stored filename is what the customer's browser will save. The download
# function refuses to serve PumpCurve objects without one and rejects names
# outside [A-Za-z0-9][A-Za-z0-9._-]{0,199}, so fail here instead of after upload.
case "$FN" in
    [A-Za-z0-9]*) ;;
    *) die "artifact file name is not servable: $FN" ;;
esac
[ "${#FN}" -le 200 ] || die "artifact file name is too long: $FN"
case "$FN" in *[!A-Za-z0-9._-]*) die "artifact file name has characters the channel rejects: $FN" ;; esac
echo "   filename: $FN  (servable)"

echo "== 2. test object must not be public =="
PUB="$(curl -s -o /dev/null -w '%{http_code}' --max-time 30 "$PUBLIC_OBJ" || echo 000)"
[ "$PUB" != "200" ] || die "object is publicly readable — aborting"
echo "   public path status: $PUB (not 200 => private OK)"

echo "== 3. retrieve upload token from macOS Keychain (never printed/stored) =="
T="$(security find-generic-password -s "$KC_SERVICE" -a "$(id -un)" -w 2>/dev/null)" || T=""
if [ -z "$T" ]; then
    T="$(security find-generic-password -s "$KC_SERVICE" -w 2>/dev/null)" || T=""
fi
if [ -z "$T" ]; then
    if [ "$PREFLIGHT_ONLY" = "1" ]; then
        echo "   token: NOT PRESENT (service: $KC_SERVICE) — preflight only, stopping here"
        echo
        echo "PREFLIGHT OK (artifact and privacy checks passed; nothing was uploaded)"
        exit 0
    fi
    die "upload token not found in Keychain (service: $KC_SERVICE). Store it with:
       security add-generic-password -s \"$KC_SERVICE\" -a \"$(id -un)\" -w   (prompts, value never echoed)
     or re-run with --preflight-only to validate everything except the upload."
fi
echo "   token retrieved: yes (value suppressed)"

if [ "$PREFLIGHT_ONLY" = "1" ]; then
    unset T
    echo
    echo "PREFLIGHT OK (artifact, privacy and credential checks passed; nothing was uploaded)"
    exit 0
fi

echo "== 4. upload to the fixed $PLATFORM endpoint =="
# Token is provided to curl through an stdin config: it never appears in
# argv, in any file, or in any log.
if ! RESP="$(printf 'header = "Authorization: Bearer %s"\nheader = "Content-Type: application/octet-stream"\nheader = "X-Filename: %s"\n' "$T" "$FN" \
        | curl -sS -K - -X POST --data-binary "@$ART" -w '\n%{http_code}' --max-time 900 "$UPLOAD_ENDPOINT" 2>&1)"; then
    unset T
    die "upload transport error: $RESP"
fi
# Defensive: refuse to emit anything if the response ever echoed the token.
if [[ "$RESP" == *"$T"* ]]; then
    unset T
    die "server response contained the token — suppressed all output"
fi
unset T
CODE="$(printf '%s' "$RESP" | tail -n1)"
BODY="$(printf '%s' "$RESP" | sed '$d')"
[ "$CODE" = "200" ] || [ "$CODE" = "201" ] || die "upload rejected with HTTP $CODE: $BODY"
echo "   HTTP $CODE"

echo "== 5. server metadata verification =="
if ! printf '%s' "$BODY" | python3 -c '
import sys, json
fn, size, sha, platform = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
try:
    d = json.load(sys.stdin)
except Exception:
    print("   server response is not JSON -> FAIL"); sys.exit(1)
print("   success  =", d.get("success"))
print("   platform =", d.get("platform"))
print("   filename =", d.get("filename"))
print("   fileSize =", d.get("fileSize"))
print("   sha256   =", d.get("sha256"))
ok = (d.get("success") is True and d.get("platform") == platform
      and d.get("filename") == fn and d.get("fileSize") == size
      and d.get("sha256") == sha)
if not ok:
    print("   server metadata mismatch -> FAIL"); sys.exit(1)
print("   server metadata: OK")
' "$FN" "$SIZE" "$SHA" "$PLATFORM"; then
    die "server metadata verification failed"
fi

echo "== 6. public route + stored-bytes verification =="
if ! python3 - "$DOWNLOAD_FN" "$ALIAS" "$SHA" "$SIZE" "$FN" <<'PY'
import http.client, hashlib, json, base64, time, sys, urllib.parse, os

fn_url, alias, exp_sha, exp_size, exp_name = (
    sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), sys.argv[5])
TMP = "/tmp/.pumpcurve-publish-verify.bin"

def split(url):
    u = urllib.parse.urlparse(url)
    return u.netloc, u.path + ("?" + u.query if u.query else "")

def get(host, path):
    c = http.client.HTTPSConnection(host, timeout=60)
    c.request("GET", path, headers={"User-Agent": "curl/8.0"})
    r = c.getresponse()
    if r.status != 302:
        r.read()
    h = {k.lower(): v for k, v in r.getheaders()}
    c.close()
    return r.status, h

def fail(msg):
    print(f"   {msg} -> FAIL"); sys.exit(1)

# alias hop
h, hh = get(*split(alias))
if h != 302 or "sidechainer-test-download" not in hh.get("location", ""):
    fail(f"alias did not 302 to the download function (HTTP {h})")
if "pumpcurve" not in hh.get("location", ""):
    fail("alias redirected to a URL that is not the PumpCurve product route")
if "no-store" not in hh.get("cache-control", ""):
    fail("alias response missing no-store")
print("   alias: 302 -> PumpCurve download function, no-store OK")

# function hop (mints the 300 s signed URL)
h, hh = get(*split(fn_url))
loc = hh.get("location", "")
if h != 302:
    fail(f"download function did not 302 (HTTP {h})")
if "no-store" not in hh.get("cache-control", ""):
    fail("function response missing no-store")
u = urllib.parse.urlparse(loc)
q = urllib.parse.parse_qs(u.query)
token = (q.get("token") or [""])[0]
dl_name = (q.get("download") or [""])[0]
if dl_name != exp_name:
    fail(f"served filename '{dl_name}' != expected '{exp_name}'")
print(f"   function: 302, no-store OK, served filename: {dl_name}")

# signed URL lifetime must be 300 s (decoded in memory; token never printed)
if token.count(".") == 2:
    pay = token.split(".")[1]
    pay += "=" * (-len(pay) % 4)
    p = json.loads(base64.urlsafe_b64decode(pay))
    lifetime = int(p["exp"]) - int(p.get("iat", p["exp"] - 300))
    if lifetime != 300:
        fail(f"signed URL lifetime {lifetime}s != 300s")
    print("   signed URL lifetime: 300s OK")
else:
    fail("signed URL token is not decodable; cannot prove 300s lifetime")

# download the stored object and hash it
sha = hashlib.sha256(); size = 0
c = http.client.HTTPSConnection(u.netloc, timeout=300)
c.request("GET", u.path + ("?" + u.query if u.query else ""), headers={"User-Agent": "curl/8.0"})
r = c.getresponse()
if r.status != 200:
    r.read(); c.close(); fail(f"signed download failed (HTTP {r.status})")
ctype = r.getheader("Content-Type") or ""
with open(TMP, "wb") as f:
    while True:
        chunk = r.read(1 << 20)
        if not chunk:
            break
        sha.update(chunk); f.write(chunk); size += len(chunk)
c.close()
got = sha.hexdigest()
os.remove(TMP)
if size != exp_size or got != exp_sha:
    fail(f"stored bytes mismatch (size {size}, sha {got})")
print(f"   stored object: size {size}, sha256 {got}  (matches expected)")
print(f"   content-type: {ctype or '(none)'}")
PY
then
    die "public route / stored-bytes verification failed"
fi

echo
echo "PUBLISH OK"
echo "  object:      $BUCKET/$OBJECT_KEY"
echo "  endpoint:    $UPLOAD_ENDPOINT"
echo "  public url:  $ALIAS"
echo "  sha256:      $SHA"
echo "  size:        $SIZE bytes"
