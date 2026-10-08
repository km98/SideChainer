#!/bin/bash
#
# publish_sidechainer_beta.sh — explicit SideChainer macOS beta publish command.
#
# Usage:
#   ./publish_sidechainer_beta.sh <artifact-path> <expected-sha256> [expected-size-bytes]
#
# What it does (all-or-nothing, fail closed):
#   1. verify the local artifact (exists, size if given, SHA-256 must match)
#   2. assert the test bucket is NOT publicly readable
#   3. retrieve the upload token from the macOS Keychain at runtime
#      (service: "Music-Prod SideChainer Beta Upload"; never printed, never
#       written to disk, never placed in a command-line argument — it is fed
#       to curl via an stdin config so it only exists in memory)
#   4. upload the raw artifact to the fixed macOS latest endpoint
#      sidechainer-test-upload/macos  (bucket sidechainer-test,
#      object sidechainer/macos/latest — the ONLY object this script writes)
#   5. verify the server-reported metadata (success/platform/filename/size/sha)
#   6. download the stored object back through the canonical beta download
#      route (alias -> function -> 300 s signed URL) and verify bytes
#   7. verify the stable alias and no-store cache headers
#
# This script NEVER touches the production release system: no release_products,
# product_releases, release_artifacts rows, no `releases` bucket, no
# music-prod-studio-api routes, no Lovable, no function deployments.
#
# The publish step is intentionally a deliberate manual command: it is NOT
# wired into the build pipeline.

set -euo pipefail
umask 077

ENDPOINT='https://wfpeajmdojcjqyrsnxbk.supabase.co/functions/v1/sidechainer-test-upload/macos'
DOWNLOAD_FN='https://wfpeajmdojcjqyrsnxbk.supabase.co/functions/v1/sidechainer-test-download/latestbeta'
ALIAS='https://music-prod.com/sidechainer/latestbeta'
PUBLIC_OBJ='https://wfpeajmdojcjqyrsnxbk.supabase.co/storage/v1/object/public/sidechainer-test/sidechainer/macos/latest'
KC_SERVICE='Music-Prod SideChainer Beta Upload'

die() { echo "ERROR: $*" >&2; exit 1; }

usage() {
    echo "Usage: $0 <artifact-path> <expected-sha256> [expected-size-bytes]" >&2
    exit 2
}

[ $# -ge 2 ] || usage
ART="$1"
EXP_SHA="$2"
EXP_SIZE="${3:-}"

[ -f "$ART" ] || die "artifact not found: $ART"
FN="$(basename "$ART")"
SIZE="$(stat -f '%z' "$ART")"

echo "== 1. local artifact preflight =="
echo "   path: $ART"
echo "   size: $SIZE"
if [ -n "$EXP_SIZE" ] && [ "$SIZE" != "$EXP_SIZE" ]; then
    die "size mismatch: got $SIZE, expected $EXP_SIZE"
fi
SHA="$(shasum -a 256 "$ART" | awk '{print $1}')"
[ "$SHA" = "$EXP_SHA" ] || die "sha256 mismatch: got $SHA, expected $EXP_SHA"
echo "   sha256: $SHA  (matches expected)"

echo "== 2. test bucket must not be public =="
PUB="$(curl -s -o /dev/null -w '%{http_code}' --max-time 30 "$PUBLIC_OBJ" || echo 000)"
[ "$PUB" != "200" ] || die "sidechainer-test object is publicly readable — aborting"
echo "   public path status: $PUB (not 200 => private OK)"

echo "== 3. retrieve upload token from macOS Keychain (never printed/stored) =="
T="$(security find-generic-password -s "$KC_SERVICE" -a "$(id -un)" -w 2>/dev/null)" || T=""
if [ -z "$T" ]; then
    T="$(security find-generic-password -s "$KC_SERVICE" -w 2>/dev/null)" || T=""
fi
[ -n "$T" ] || die "upload token not found in Keychain (service: $KC_SERVICE)"
echo "   token retrieved: yes (value suppressed)"

echo "== 4. upload to fixed latest endpoint =="
# Token is provided to curl through an stdin config: it never appears in
# argv, in any file, or in any log.
if ! RESP="$(printf 'header = "Authorization: Bearer %s"\nheader = "Content-Type: application/octet-stream"\nheader = "X-Filename: %s"\n' "$T" "$FN" \
        | curl -sS -K - -X POST --data-binary "@$ART" -w '\n%{http_code}' --max-time 900 "$ENDPOINT" 2>&1)"; then
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
# NOTE: python code goes in -c (argv), the response body arrives on stdin
# via the pipe — never combine a pipe with a heredoc on the same command.
if ! printf '%s' "$BODY" | python3 -c '
import sys, json
fn, size, sha = sys.argv[1], int(sys.argv[2]), sys.argv[3]
try:
    d = json.load(sys.stdin)
except Exception:
    print("   server response is not JSON -> FAIL"); sys.exit(1)
print("   success  =", d.get("success"))
print("   platform =", d.get("platform"))
print("   filename =", d.get("filename"))
print("   fileSize =", d.get("fileSize"))
print("   sha256   =", d.get("sha256"))
ok = (d.get("success") is True and d.get("platform") == "macos"
      and d.get("filename") == fn and d.get("fileSize") == size
      and d.get("sha256") == sha)
if not ok:
    print("   server metadata mismatch -> FAIL"); sys.exit(1)
print("   server metadata: OK")
' "$FN" "$SIZE" "$SHA"; then
    die "server metadata verification failed"
fi

echo "== 6+7. stable route + stored-bytes verification =="
if ! python3 - "$DOWNLOAD_FN" "$ALIAS" "$SHA" "$SIZE" <<'PY'
import http.client, hashlib, json, base64, time, sys, urllib.parse, os

fn_url, alias, exp_sha, exp_size = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
TMP = "/tmp/.sb-publish-verify.dmg"

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
if "no-store" not in hh.get("cache-control", ""):
    fail("alias response missing no-store")
print("   alias: 302 -> download function, no-store OK")

# function hop (mints the 300 s signed URL)
t0 = time.time()
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
print(f"   function: 302, no-store OK, served filename: {dl_name or '(none)'}")

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

# download stored object and hash it
h, hh = get(u.netloc, u.path + ("?" + u.query if u.query else ""))
if h != 200:
    fail(f"signed download failed (HTTP {h})")
sha = hashlib.sha256()
size = 0
c = http.client.HTTPSConnection(u.netloc, timeout=300)
c.request("GET", u.path + ("?" + u.query if u.query else ""), headers={"User-Agent": "curl/8.0"})
r = c.getresponse()
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
print("   stored object: size", size, ", sha256", got, " (matches expected)")
PY
then
    die "stable route / stored-bytes verification failed"
fi

echo
echo "PUBLISH OK"
echo "  object:      sidechainer-test/sidechainer/macos/latest"
echo "  endpoint:    $ENDPOINT"
echo "  stable url:  $ALIAS"
echo "  sha256:      $SHA"
echo "  size:        $SIZE bytes"
