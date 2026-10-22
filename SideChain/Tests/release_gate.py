#!/usr/bin/env python3
"""PumpCurve release gate - decides whether this product may be published.

It reads *evidence artifacts*, not status prose: the staged bundles, the signed
installer, the recorded hashes, the actual download endpoints and the host test
evidence. Anything it cannot verify is reported as BLOCKED (an external
prerequisite is missing), NOT_TESTED (work that has not happened) or FAIL (an
artifact contradicts its own claim). Nothing is inferred from a report saying a
step was done.

Every result carries one of four classifications:

    A  verified and complete
    B  built, but not signed / notarised / published (a documented limitation)
    C  not built or not tested
    D  blocked by an external prerequisite

Modes
-----
    --mode public   (default) the customer-facing release: notarisation,
                    Gatekeeper acceptance and live downloads must all pass.
    --mode beta     preview/beta: the macOS candidate must still be validated,
                    correctly named and hash-verified, but a missing notarisation
                    or an unpublished download is a documented limitation rather
                    than a failure. The verdict is reported as BETA-ONLY.

Exit codes: 0 the requested mode is satisfied, 1 it is not, 2 usage/environment
error (nothing could be judged).
"""
import argparse
import hashlib
import json
import os
import pathlib
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request

PRODUCT = "PumpCurve"
VERSION = "0.5.2"
BUNDLE_ID = "com.musicprod.sidechain"
AU_TUPLE = ("aufx", "SdCh", "Musc")
TEAM_ID = "3A4R5EKM7V"
SPA_MARKER = "text/html"
LEGACY_NAMES = ("SideChain.component", "SideChainer.component", "SideChain.vst3", "SideChainer.vst3")
DEFAULT_ROUTES = {"/pumpcurve/latestbeta": "macos",
                  "/pumpcurve/windows/latestbeta": "windows"}
HOSTS = ("logic_pro", "fl_studio", "ableton_live", "windows_daw")

PASS, FAIL, LIMITATION, NOT_TESTED, BLOCKED, INFO = (
    "PASS", "FAIL", "LIMITATION", "NOT_TESTED", "BLOCKED", "INFO")
CLASS_OF = {PASS: "A", FAIL: "-", LIMITATION: "B", NOT_TESTED: "C", BLOCKED: "D", INFO: "-"}
SECRET_PATTERNS = (r"BEGIN [A-Z ]*PRIVATE KEY", r"eyJ[A-Za-z0-9_-]{20,}",
                   r"service_role", r"xox[baprs]-", r"AKIA[0-9A-Z]{16}")

HERE = pathlib.Path(__file__).resolve().parent
PROJECT = HERE.parent                       # <repo>/SideChain
REPO = PROJECT.parent


# --------------------------------------------------------------------------- util

def sha256_file(path, chunk=1 << 20):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(chunk), b""):
            h.update(block)
    return h.hexdigest()


def sh(cmd, cwd=None):
    try:
        p = subprocess.run([str(c) for c in cmd], cwd=cwd, capture_output=True, text=True)
        return p.returncode, (p.stdout or "") + (p.stderr or "")
    except OSError as exc:
        return 127, str(exc)


def newest(paths):
    paths = [p for p in paths if p]
    return max(paths, key=lambda p: p.stat().st_mtime) if paths else None


def tree_hash(root):
    """Order-independent content fingerprint of a bundle (names included)."""
    root = pathlib.Path(root)
    h = hashlib.sha256()
    for p in sorted(root.rglob("*")):
        if p.is_file():
            h.update(p.relative_to(root).as_posix().encode())
            h.update(sha256_file(p).encode())
    return h.hexdigest()


class Gate:
    def __init__(self, mode):
        self.mode = mode
        self.results = []

    def add(self, area, status, detail, claim=""):
        self.results.append({"area": area, "status": status,
                             "classification": CLASS_OF[status],
                             "detail": detail, "claim": claim})
        icon = {"PASS": "ok  ", "FAIL": "FAIL", "LIMITATION": "lim ",
                "NOT_TESTED": "n/t ", "BLOCKED": "BLK ", "INFO": "info"}[status]
        print("  [%s] %s: %s" % (icon, area, detail))
        return status

    def count(self, status):
        return sum(1 for r in self.results if r["status"] == status)

    def blocking(self):
        return [r for r in self.results if r["status"] in (FAIL, BLOCKED)]


# ----------------------------------------------------------------- evidence discovery

def _prefer(candidates, preferred_names, marker):
    """Pick the best candidate directory: preferred name first, then completeness.

    Several directories in a candidate tree look plausible (a validation build and
    the shipped staging, a site-staging copy of the package, ...). Preferring the
    conventional names keeps the gate pointed at the artifacts the release actually
    ships, and the choice is always printed so it can be audited.
    """
    if not candidates:
        return None
    named = [c for c in candidates if c.name in preferred_names]
    pool = named or candidates
    complete = [c for c in pool if all((c / m).exists() for m in marker)]
    return newest(complete or pool)


def discover_evidence(root):
    """Find the newest candidate layout under an evidence root.

    Expected: a directory holding PumpCurve-<version>-macOS-Universal.pkg (the
    packed, signed candidate) and a staging directory holding both renamed bundles.
    """
    found = {"macos_dir": None, "staged_dir": None}
    if not root or not pathlib.Path(root).is_dir():
        return found
    root = pathlib.Path(root)
    pkgs = root.glob("**/PumpCurve-%s-macOS-Universal.pkg" % VERSION)
    found["macos_dir"] = _prefer([p.parent for p in pkgs], {"installer"},
                                 ["PumpCurve-%s-macOS-Universal.pkg" % VERSION])
    staged = root.glob("**/PumpCurve.component")
    found["staged_dir"] = _prefer([p.parent for p in staged], {"staged-products"},
                                  ["PumpCurve.component", "PumpCurve.vst3"])
    return found


def discover_windows(root, explicit):
    """A Windows bundle is the one with a Contents/x86_64-win module inside.

    Both platforms ship a PumpCurve.vst3 directory, so matching on the directory
    name alone would happily pick the macOS bundle.
    """
    if explicit:
        return pathlib.Path(explicit)
    if root and pathlib.Path(root).is_dir():
        modules = list(pathlib.Path(root).glob("**/Contents/x86_64-win/*.vst3"))
        if modules:
            # module -> x86_64-win -> Contents -> <bundle> -> its directory
            return newest([m.parents[3] for m in modules])
    default = PROJECT / "Builds" / "Windows" / "artifacts"
    return default if default.is_dir() and list(
        default.glob("Contents/x86_64-win/*.vst3")) else None


# ------------------------------------------------------------------------- checks

def check_identity(gate, staged_dir):
    jucer = PROJECT / "SideChain.jucer"
    if not jucer.is_file():
        return gate.add("product identity", FAIL, "no .jucer at %s" % jucer)
    text = jucer.read_text(encoding="utf-8", errors="replace")
    tag = re.search(r"<JUCERPROJECT\b[^>]*>", text, re.S)
    attrs = dict(re.findall(r'(\w+)="([^"]*)"', tag.group(0))) if tag else {}
    expectations = {"pluginName": PRODUCT, "version": VERSION,
                    "bundleIdentifier": BUNDLE_ID, "pluginCode": "SdCh",
                    "pluginManufacturerCode": "Musc", "companyName": "Music-Prod"}
    bad = {k: (attrs.get(k), v) for k, v in expectations.items() if attrs.get(k) != v}
    if bad:
        return gate.add("product identity", FAIL,
                        "the project definition does not match the release: %s" % bad)
    gate.add("product identity", PASS,
             "project defines %s %s, bundle id %s, AU %s/%s, code %s"
             % (PRODUCT, VERSION, BUNDLE_ID, AU_TUPLE[0], AU_TUPLE[1], attrs.get("pluginCode")),
             claim="SideChain.jucer")

    if not staged_dir:
        return gate.add("bundle names", NOT_TESTED,
                        "no staged products supplied or found; cannot prove the shipped names")
    for name in ("%s.component" % PRODUCT, "%s.vst3" % PRODUCT):
        bundle = pathlib.Path(staged_dir) / name
        if not bundle.is_dir():
            return gate.add("bundle names", FAIL, "missing staged bundle: %s" % bundle)
    leftovers = [n for n in LEGACY_NAMES if (pathlib.Path(staged_dir) / n).exists()]
    if leftovers:
        return gate.add("bundle names", FAIL,
                        "legacy bundle names still staged: %s" % ", ".join(leftovers))
    return gate.add("bundle names", PASS,
                    "%s.component and %s.vst3 are staged, no legacy name present"
                    % (PRODUCT, PRODUCT), claim=str(staged_dir))


def check_macos_bundles(gate, staged_dir):
    if not staged_dir:
        return gate.add("macOS bundles", NOT_TESTED, "no staged products supplied or found")
    staged = pathlib.Path(staged_dir)
    au = staged / ("%s.component" % PRODUCT)
    vst3 = staged / ("%s.vst3" % PRODUCT)
    problems, facts = [], []

    for bundle in (au, vst3):
        plist_path = bundle / "Contents" / "Info.plist"
        if not plist_path.is_file():
            problems.append("no Info.plist in %s" % bundle.name)
            continue
        with open(plist_path, "rb") as f:
            plist = plistlib.load(f)
        if plist.get("CFBundleIdentifier") != BUNDLE_ID:
            problems.append("%s: CFBundleIdentifier is %s" % (bundle.name, plist.get("CFBundleIdentifier")))
        if plist.get("CFBundleName") != PRODUCT and plist.get("CFBundleDisplayName") != PRODUCT:
            problems.append("%s: does not present itself as %s" % (bundle.name, PRODUCT))
        exe = bundle / "Contents" / "MacOS" / (plist.get("CFBundleExecutable") or "")
        if not exe.is_file():
            problems.append("%s: no executable at %s" % (bundle.name, exe))
            continue
        rc, archs = sh(["lipo", "-archs", exe])
        if rc != 0 or "x86_64" not in archs or "arm64" not in archs:
            problems.append("%s: not universal (%s)" % (bundle.name, archs.strip() or "lipo failed"))
        rc, out = sh(["codesign", "--verify", "--deep", "--strict", "--verbose=2", bundle])
        if rc != 0:
            problems.append("%s: codesign --verify failed: %s" % (bundle.name, out.strip().splitlines()[-1:]))
        rc, out = sh(["codesign", "-dv", "--verbose=4", bundle])
        if TEAM_ID not in out:
            problems.append("%s: signature is not from team %s" % (bundle.name, TEAM_ID))
        facts.append("%s %s" % (bundle.name, tree_hash(bundle)[:16]))

    # The AU registration tuple is a compatibility contract, not a label.
    au_plist = au / "Contents" / "Info.plist"
    if au_plist.is_file():
        with open(au_plist, "rb") as f:
            plist = plistlib.load(f)
        comps = plist.get("AudioComponents") or []
        if not comps:
            problems.append("AU Info.plist declares no AudioComponents")
        else:
            c = comps[0]
            got = (c.get("type"), c.get("subtype"), c.get("manufacturer"))
            if got != AU_TUPLE:
                problems.append("AU tuple changed: %s (expected %s)" % (got, AU_TUPLE))
            else:
                facts.append("AU %s/%s/%s '%s'" % (got + (c.get("name"),)))

    if problems:
        return gate.add("macOS bundles", FAIL, "; ".join(problems))
    return gate.add("macOS bundles", PASS,
                    "both bundles signed (team %s), universal, identity unchanged; %s"
                    % (TEAM_ID, ", ".join(facts)), claim=str(staged))


def check_macos_installer(gate, macos_dir):
    if not macos_dir:
        return gate.add("macOS installer", NOT_TESTED, "no installer directory supplied or found")
    macos_dir = pathlib.Path(macos_dir)
    pkg = macos_dir / ("%s-%s-macOS-Universal.pkg" % (PRODUCT, VERSION))
    dmg = macos_dir / ("%s-%s-macOS-Universal.dmg" % (PRODUCT, VERSION))
    sums = macos_dir / "SHA256SUMS"
    if not pkg.is_file():
        return gate.add("macOS installer", FAIL, "no package at %s" % pkg)
    if not sums.is_file():
        return gate.add("macOS installer", FAIL, "no SHA256SUMS next to the package")

    recorded = {}
    for line in sums.read_text(encoding="utf-8").splitlines():
        m = re.match(r"^([0-9a-f]{64})\s+(\S+)$", line.strip())
        if m:
            recorded[m.group(2)] = m.group(1)
    problems = []
    for artifact in (pkg, dmg):
        if not artifact.is_file():
            problems.append("missing %s" % artifact.name)
            continue
        want = recorded.get(artifact.name)
        if not want:
            problems.append("%s has no recorded hash" % artifact.name)
        elif sha256_file(artifact) != want:
            problems.append("%s does not match its recorded hash" % artifact.name)

    rc, out = sh(["pkgutil", "--check-signature", pkg])
    if rc != 0 or "signed by a developer certificate issued by Apple" not in out:
        problems.append("package is not signed for distribution: %s" % out.strip().splitlines()[:1])

    # Confirm the payload really carries the product bundles and no legacy name.
    # The payload is a cpio archive, so the member list comes from pkgutil itself.
    rc, listing = sh(["pkgutil", "--payload-files", pkg])
    if rc != 0:
        problems.append("could not list the package payload: %s" % listing.strip()[:200])
    else:
        members = [line.strip().lstrip("./") for line in listing.splitlines() if line.strip()]
        if not any(m.startswith("Library/Audio/Plug-Ins/Components/%s.component/" % PRODUCT)
                   for m in members):
            problems.append("package payload lacks %s.component under the Components folder" % PRODUCT)
        if not any(m.startswith("Library/Audio/Plug-Ins/VST3/%s.vst3/" % PRODUCT)
                   for m in members):
            problems.append("package payload lacks %s.vst3 under the VST3 folder" % PRODUCT)
        stale = sorted({n for n in LEGACY_NAMES
                        if any(m.startswith("Library/Audio/Plug-Ins/") and "/%s/" % n in m
                               for m in members)})
        if stale:
            problems.append("package payload still contains %s" % ", ".join(stale))

    with tempfile.TemporaryDirectory() as tmp:
        rc, out = sh(["pkgutil", "--expand", pkg, tmp + "/pkg"])
        if rc == 0:
            scripts = [p.name for p in pathlib.Path(tmp).rglob("*")]
            if not any(n in scripts for n in ("preinstall", "postinstall")):
                problems.append("package carries no install scripts (upgrade cleanup)")
        else:
            problems.append("could not expand the package: %s" % out.strip()[:200])

    if problems:
        return gate.add("macOS installer", FAIL, "; ".join(problems))
    return gate.add("macOS installer", PASS,
                    "PKG %s and DMG %s match their recorded SHA-256, package is signed, "
                    "payload carries the product bundles under the standard install paths"
                    % (sha256_file(pkg)[:16], dmg.is_file() and sha256_file(dmg)[:16]),
                    claim=str(sums))


def check_notarization(gate, macos_dir):
    if not macos_dir:
        return gate.add("notarisation", NOT_TESTED, "no installer directory supplied or found")
    pkg = pathlib.Path(macos_dir) / ("%s-%s-macOS-Universal.pkg" % (PRODUCT, VERSION))
    dmg = pathlib.Path(macos_dir) / ("%s-%s-macOS-Universal.dmg" % (PRODUCT, VERSION))
    if not pkg.is_file():
        return gate.add("notarisation", NOT_TESTED, "no package to notarise")

    rc_staple, out_staple = sh(["stapler", "validate", pkg])
    rc_spctl, out_spctl = sh(["spctl", "-a", "-t", "install", "-vv", pkg])
    accepted = "accepted" in out_spctl and "source=Notarized Developer ID" in out_spctl
    if rc_staple == 0 and rc_spctl == 0 and accepted:
        detail = "stapled and accepted by Gatekeeper"
        if dmg.is_file():
            rc_d, _ = sh(["stapler", "validate", dmg])
            if rc_d != 0:
                return gate.add("notarisation", FAIL,
                                "package is notarised but the disk image is not stapled")
        return gate.add("notarisation", PASS, detail, claim=str(macos_dir))

    have, missing = notarization_credentials()
    reason = ("Gatekeeper reports: %s" % out_spctl.strip().splitlines()[0]
              if out_spctl.strip() else "no notarisation ticket")
    gate.add("notarisation", BLOCKED,
             "%s; credentials present: %s; still missing: %s"
             % (reason, ", ".join(have) or "none", "; ".join(missing) or "nothing"),
             claim=str(macos_dir))
    gate.add("notarisation", INFO, notarization_next_step(pkg, missing))
    return BLOCKED


def notarization_credentials():
    """Which notarisation credentials exist on this machine, and what is missing.

    Presence only: no credential value is read, printed or put on a command line.
    notarytool accepts either a stored keychain profile or an App Store Connect API
    key, and the API-key route needs BOTH the .p8 file and the issuer id - the key
    id sits in the file name, but the issuer id can only come from App Store
    Connect, so its absence is the blocker worth naming precisely.
    """
    have, missing = [], []
    for profile in ("AC_PASSWORD", "SideChainerNotary", "notarytool-profile"):
        rc, _ = sh(["security", "find-generic-password", "-s", profile])
        if rc == 0:
            have.append("keychain profile '%s'" % profile)
    keys = sorted(pathlib.Path.home().joinpath(".private_keys").glob("AuthKey_*.p8"))
    if keys:
        have.append("App Store Connect API key file %s" % ", ".join(k.name for k in keys))
        if os.environ.get("ASC_ISSUER_ID"):
            have.append("issuer id (ASC_ISSUER_ID is set)")
        else:
            missing.append("the App Store Connect issuer id for %s (App Store Connect > "
                           "Users and Access > Integrations > App Store Connect API) - "
                           "export ASC_ISSUER_ID to supply it" % keys[0].name)
    if not have:
        missing.append("every notarisation credential (stored keychain profile or API key)")
    return have, missing


def notarization_next_step(pkg, missing):
    if not missing:
        return ("credentials are available: xcrun notarytool submit \"%s\" --wait, then "
                "xcrun stapler staple \"%s\", then re-run this gate" % (pkg, pkg))
    return ("blocked on %s; once supplied either store a profile with xcrun notarytool "
            "store-credentials --keychain-profile AC_PASSWORD or submit directly with "
            "--key/--key-id/--issuer, then xcrun stapler staple \"%s\" and re-run this "
            "gate" % ("; ".join(missing), pkg))


def check_windows(gate, windows_dir, explicit=False):
    """Judge the Windows artifact, keeping "stale" apart from "broken".

    A discovered artifact from an older candidate (the evidence root still holds
    run13's 0.4.1 build) is NOT evidence about this version: reporting it as an
    identity FAIL made a candidate look broken when in truth no Windows build
    existed for it yet. The build's own RUN-MANIFEST.json names its version, so an
    implicit discovery of a different version is reported NOT_TESTED. An
    explicitly supplied --windows-dir is different: the caller asked for that
    directory to be judged, so a version mismatch there stays a FAIL.
    """
    if not windows_dir:
        return gate.add("Windows artifacts", BLOCKED,
                        "no Windows build fetched yet; the pipeline is "
                        ".github/workflows/windows-vst3.yml and its output lands on a "
                        "windows-build-results/<sha>-r<n> branch or Builds/Windows/artifacts")
    windows_dir = pathlib.Path(windows_dir)
    bundle = windows_dir / ("%s.vst3" % PRODUCT)
    module = bundle / "Contents" / "x86_64-win" / ("%s.vst3" % PRODUCT)
    setup = windows_dir / ("%s-%s-Windows-x64-Setup.exe" % (PRODUCT, VERSION))
    run_manifest = windows_dir / "RUN-MANIFEST.json"

    if not module.is_file():
        return gate.add("Windows artifacts", BLOCKED,
                        "no staged %s.vst3 module under %s" % (PRODUCT, windows_dir))
    if run_manifest.is_file() and not explicit:
        try:
            recorded = json.loads(run_manifest.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            recorded = {}
        recorded_version = recorded.get("version")
        if recorded_version and str(recorded_version) != VERSION:
            return gate.add(
                "Windows artifacts", NOT_TESTED,
                "the staged Windows build found is %s (recorded by its RUN-MANIFEST), "
                "not this candidate's %s; no Windows build exists for %s yet"
                % (recorded_version, VERSION, VERSION),
                claim=str(run_manifest))
    tool = PROJECT / "Builds" / "Windows" / "tools" / "stage_windows_vst3.py"
    rc, out = sh([sys.executable, tool, "--inspect-module", module, "--version", VERSION])
    if rc != 0:
        return gate.add("Windows artifacts", FAIL,
                        "the staged Windows module failed identity verification: %s"
                        % out.strip().splitlines()[-1])
    problems = []
    if run_manifest.is_file():
        run_manifest = json.loads(run_manifest.read_text(encoding="utf-8"))
        claimed = run_manifest.get("staged_module_sha256")
        if claimed and claimed != sha256_file(module):
            problems.append("staged module does not match the hash recorded by the build")
        if run_manifest.get("signed"):
            gate.add("Windows signing", PASS, "artifact reports a verified Authenticode signature")
        else:
            gate.add("Windows signing", LIMITATION,
                     "installer is UNSIGNED (no Authenticode certificate); SmartScreen will "
                     "warn and the artifact must be labelled as such")
    else:
        gate.add("Windows provenance", NOT_TESTED,
                 "no RUN-MANIFEST.json: the artifact cannot be traced to a workflow run")

    if not setup.is_file():
        problems.append("no installer (Setup.exe) next to the bundle")
    if problems:
        return gate.add("Windows artifacts", FAIL, "; ".join(problems))
    return gate.add("Windows artifacts", LIMITATION,
                    "staged VST3 verified (x64 PE, product identity, hash matches the build) "
                    "and installer present; not host-tested and not published",
                    claim=str(windows_dir))


def http_probe(url, timeout=30):
    """Return (status, headers, body_prefix) for the first hop, without following."""
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, req, fp, code, msg, headers, newurl):
            return None
    opener = urllib.request.build_opener(NoRedirect)
    req = urllib.request.Request(url, headers={"User-Agent": "pumpcurve-release-gate"})
    try:
        # The headers stay an HTTPMessage: header names are case-insensitive, and
        # an HTTP/2 server (Hostinger's CDN) sends them in lower case, which a
        # plain dict would turn into a "redirect without a Location header".
        with opener.open(req, timeout=timeout) as resp:
            return resp.status, resp.headers, resp.read(2048)
    except urllib.error.HTTPError as exc:
        return exc.code, (exc.headers if exc.headers is not None else {}), b""
    except Exception as exc:                                  # noqa: BLE001
        return None, {"error": str(exc)}, b""


def download(url, timeout=120):
    req = urllib.request.Request(url, headers={"User-Agent": "pumpcurve-release-gate"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        data = resp.read()
    return data


def check_distribution(gate, url_base, artifacts_by_platform, offline):
    for route, platform in DEFAULT_ROUTES.items():
        url = url_base.rstrip("/") + route
        if offline:
            gate.add("distribution", LIMITATION,
                     "--offline: %s was NOT checked; the live download is unverified" % url)
            continue
        status, headers, prefix = http_probe(url)
        if status is None:
            gate.add("distribution", BLOCKED, "%s unreachable (%s)" % (url, headers.get("error")))
            continue
        ctype = (headers.get("Content-Type") or "").lower()
        if status == 200 and SPA_MARKER in ctype:
            gate.add("distribution", FAIL,
                     "%s returned HTTP 200 with %s - the SPA fallback, not an artifact"
                     % (url, ctype))
            continue
        if status in (404, 403, 410):
            gate.add("distribution", BLOCKED,
                     "%s returned HTTP %s: no artifact is published for this route "
                     "(upload credential/object still missing)" % (url, status))
            continue
        if status not in (301, 302, 303, 307, 308):
            gate.add("distribution", FAIL,
                     "%s returned HTTP %s (%s) instead of a download redirect"
                     % (url, status, ctype or "no content type"))
            continue
        location = headers.get("Location")
        if not location:
            gate.add("distribution", FAIL, "%s redirects without a Location header" % url)
            continue
        # A relative Location is legal HTTP and must be resolved, not treated as
        # an error; the signed storage URL the function returns is absolute.
        if not location.lower().startswith(("http://", "https://")):
            location = urllib.parse.urljoin(url, location)
        try:
            data = download(location)
        except Exception as exc:                              # noqa: BLE001
            gate.add("distribution", FAIL, "%s redirected to a URL that failed: %s" % (url, exc))
            continue
        digest = hashlib.sha256(data).hexdigest()
        matches = [name for name, h in artifacts_by_platform.items() if h.get(digest)]
        if not matches:
            gate.add("distribution", FAIL,
                     "%s downloaded %d bytes whose SHA-256 %s matches no staged artifact"
                     % (url, len(data), digest[:16]))
            continue
        if platform not in artifacts_by_platform:
            gate.add("distribution", FAIL,
                     "%s serves %s on the %s route" % (url, matches[0], platform))
            continue
        gate.add("distribution", PASS,
                 "%s -> real artifact %s (%d bytes, sha256 %s verified)"
                 % (url, matches[0], len(data), digest[:16]), claim=url)


def check_test_evidence(gate, validation_log, site_log):
    if validation_log and pathlib.Path(validation_log).is_file():
        text = pathlib.Path(validation_log).read_text(encoding="utf-8", errors="replace")
        if "VALIDATION PASSED" in text:
            stages = len(re.findall(r"-> STAGE PASS", text))
            gate.add("validation evidence", PASS,
                     "recorded validation run passed (%d stages)" % stages,
                     claim=str(validation_log))
        else:
            gate.add("validation evidence", FAIL,
                     "%s exists but does not contain VALIDATION PASSED" % validation_log)
    else:
        gate.add("validation evidence", NOT_TESTED,
                 "no validation log supplied; the recorded run is not part of this judgement")
    if site_log and pathlib.Path(site_log).is_file():
        text = pathlib.Path(site_log).read_text(encoding="utf-8", errors="replace")
        if re.search(r"\b\d+ passed\b", text):
            gate.add("distribution tests", PASS,
                     "recorded website test run passed (%s)"
                     % (re.search(r"\b\d+ passed\b", text).group(0)), claim=str(site_log))
        else:
            gate.add("distribution tests", FAIL,
                     "%s exists but reports no passing tests" % site_log)
    else:
        gate.add("distribution tests", NOT_TESTED,
                 "no website test log supplied; the routing tests are not part of this judgement")


def check_host_acceptance(gate, acceptance_file, release_notes, evidence_root=None):
    if not acceptance_file or not pathlib.Path(acceptance_file).is_file():
        return gate.add("host acceptance", NOT_TESTED,
                        "no host-acceptance.json supplied; DAW status is unproven")
    data = json.loads(pathlib.Path(acceptance_file).read_text(encoding="utf-8"))
    # Evidence may be referenced from the status file's own directory, from the
    # evidence root, or absolutely. Every resolved path is printed with its hash.
    bases = [pathlib.Path(acceptance_file).parent]
    if evidence_root:
        bases.append(pathlib.Path(evidence_root))
    undisclosed = []
    for host in HOSTS:
        entry = data.get(host)
        if not entry:
            gate.add("host acceptance: %s" % host, NOT_TESTED, "no entry in the status file")
            undisclosed.append(host)
            continue
        status = entry.get("status")
        evidence = entry.get("evidence")
        if status in ("verified", "discovery_verified"):
            if not evidence:
                gate.add("host acceptance: %s" % host, FAIL,
                         "claims '%s' without naming an evidence artifact" % status)
                continue
            candidates = [base / evidence for base in bases] + [pathlib.Path(evidence)]
            path = next((c for c in candidates if c.exists()), None)
            if path is None:
                gate.add("host acceptance: %s" % host, FAIL,
                         "claims '%s' but the evidence '%s' is missing (looked in %s)"
                         % (status, evidence, ", ".join(str(b) for b in bases)))
                continue
            digest = sha256_file(path)[:16] if path.is_file() else tree_hash(path)[:16]
            passed = status == "verified"
            gate.add("host acceptance: %s" % host, PASS if passed else LIMITATION,
                     "%s (%s, evidence sha %s)" % (status, entry.get("note", ""), digest),
                     claim=evidence)
            if not passed:
                undisclosed.append(host)
        elif status == "failed":
            gate.add("host acceptance: %s" % host, FAIL,
                     "host test failed: %s" % entry.get("note", ""))
        else:
            gate.add("host acceptance: %s" % host, NOT_TESTED,
                     "%s (%s)" % (status or "unstated", entry.get("note", "")))
            undisclosed.append(host)

    if release_notes and pathlib.Path(release_notes).is_file():
        notes = pathlib.Path(release_notes).read_text(encoding="utf-8", errors="replace").lower()
        for host in undisclosed:
            label = {"logic_pro": "logic", "fl_studio": "fl studio",
                     "ableton_live": "ableton", "windows_daw": "windows"}[host]
            if label not in notes or "not tested" not in notes:
                gate.add("release notes", FAIL,
                         "the notes do not disclose the unverified host '%s'" % host)
            else:
                gate.add("release notes", PASS,
                         "unverified host '%s' is disclosed in the release notes" % host)
    elif undisclosed:
        gate.add("release notes", BLOCKED,
                 "no release notes supplied to disclose the unverified host(s): %s"
                 % ", ".join(undisclosed))


def check_secrets(gate, dirs):
    hits = []
    for d in dirs:
        if not d or not pathlib.Path(d).is_dir():
            continue
        for path in pathlib.Path(d).rglob("*"):
            if not path.is_file() or path.stat().st_size > 4 << 20:
                continue
            try:
                text = path.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            for pattern in SECRET_PATTERNS:
                if re.search(pattern, text):
                    hits.append("%s (%s)" % (path.name, pattern.split("\\")[0][:20]))
    if hits:
        return gate.add("credential hygiene", FAIL,
                        "credential-looking material in the release evidence: %s" % "; ".join(hits[:5]))
    return gate.add("credential hygiene", PASS,
                    "no credential-looking material in the examined evidence")


# ---------------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mode", choices=("public", "beta"), default="public")
    ap.add_argument("--evidence-root", default=os.environ.get("PUMPCURVE_EVIDENCE_ROOT"),
                    help="directory to scan for the newest macOS candidate (run*/installer)")
    ap.add_argument("--macos-dir", default=os.environ.get("PUMPCURVE_MACOS_DIR"))
    ap.add_argument("--staged-dir", default=os.environ.get("PUMPCURVE_STAGED_DIR"))
    ap.add_argument("--windows-dir", default=os.environ.get("PUMPCURVE_WINDOWS_DIR"))
    ap.add_argument("--host-acceptance", default=str(HERE / "host-acceptance.json"))
    ap.add_argument("--release-notes", default=os.environ.get("PUMPCURVE_RELEASE_NOTES"))
    ap.add_argument("--validation-log", default=os.environ.get("PUMPCURVE_VALIDATION_LOG"))
    ap.add_argument("--site-test-log", default=os.environ.get("PUMPCURVE_SITE_TEST_LOG"))
    ap.add_argument("--url-base", default="https://music-prod.com")
    ap.add_argument("--offline", action="store_true",
                    help="do not touch the network; distribution is reported BLOCKED")
    ap.add_argument("--json", help="write the machine-readable result here")
    args = ap.parse_args()

    found = discover_evidence(args.evidence_root)
    macos_dir = args.macos_dir or found["macos_dir"]
    staged_dir = args.staged_dir or found["staged_dir"]
    windows_dir = discover_windows(args.evidence_root, args.windows_dir)

    print("== PumpCurve %s release gate (%s mode) ==" % (VERSION, args.mode))
    print("  macOS installer : %s" % (macos_dir or "<none found>"))
    print("  staged bundles  : %s" % (staged_dir or "<none found>"))
    print("  Windows         : %s" % (windows_dir or "<none found>"))
    print("")

    gate = Gate(args.mode)
    check_identity(gate, staged_dir)
    check_macos_bundles(gate, staged_dir)
    check_macos_installer(gate, macos_dir)
    check_notarization(gate, macos_dir)
    check_windows(gate, windows_dir, explicit=bool(args.windows_dir))
    check_test_evidence(gate, args.validation_log, args.site_test_log)
    check_host_acceptance(gate, args.host_acceptance, args.release_notes, args.evidence_root)

    artifacts = {}
    if macos_dir:
        for name in ("%s-%s-macOS-Universal.pkg" % (PRODUCT, VERSION),
                     "%s-%s-macOS-Universal.dmg" % (PRODUCT, VERSION)):
            p = pathlib.Path(macos_dir) / name
            if p.is_file():
                artifacts.setdefault("macos", {})[sha256_file(p)] = p
    if windows_dir:
        for name in ("%s-%s-Windows-x64.zip" % (PRODUCT, VERSION),
                     "%s-%s-Windows-x64-Setup.exe" % (PRODUCT, VERSION)):
            p = pathlib.Path(windows_dir) / name
            if p.is_file():
                artifacts.setdefault("windows", {})[sha256_file(p)] = p
    check_distribution(gate, args.url_base, artifacts, args.offline)
    check_secrets(gate, [macos_dir, staged_dir, windows_dir])

    print("")
    fails, blocked = gate.count(FAIL), gate.count(BLOCKED)
    limitations, untested = gate.count(LIMITATION), gate.count(NOT_TESTED)
    passes = gate.count(PASS)

    def group_statuses(area):
        return [r["status"] for r in gate.results
                if r["area"] == area and r["status"] != INFO]

    def not_passing(areas):
        out = []
        for area in areas:
            statuses = group_statuses(area)
            if not statuses:
                out.append("%s was not evaluated" % area)
            elif any(s != PASS for s in statuses):
                out.append("%s: %s" % (area, "/".join(sorted(set(statuses)))))
        return out

    # A candidate may not be shipped at all unless these are proven, in every mode.
    mandatory_always = ("product identity", "bundle names", "macOS bundles",
                        "macOS installer", "validation evidence", "credential hygiene")
    # Only a customer-facing release needs these; a beta may document them instead.
    mandatory_public = ("notarisation", "distribution", "Windows artifacts",
                        "distribution tests")

    problems = not_passing(mandatory_always)
    if args.mode == "public":
        problems += not_passing(mandatory_public)
        if args.offline:
            problems.append("the live download check was skipped (--offline)")
    if fails:
        problems.append("%d check(s) failed" % fails)

    ready = not problems
    if ready:
        verdict = "DISTRIBUTION READY" if args.mode == "public" else "BETA-ONLY (documented limitations)"
        reason = ("%d verified, %d limitation(s) recorded, %d item(s) untested"
                  % (passes, limitations, untested))
    else:
        verdict = "NOT READY"
        reason = "; ".join(problems)

    print("  %d PASS, %d LIMITATION, %d NOT_TESTED, %d BLOCKED, %d FAIL"
          % (passes, limitations, untested, blocked, fails))
    print("  %s: %s" % (verdict, reason))
    for r in gate.results:
        if r["status"] in (FAIL, BLOCKED):
            print("    -> %s [%s] %s" % (r["classification"], r["area"], r["detail"]))
    result = {"product": PRODUCT, "version": VERSION, "mode": args.mode,
              "verdict": verdict, "ready": ready, "reason": reason,
              "counts": {"pass": passes, "limitation": limitations,
                         "not_tested": untested, "blocked": blocked, "fail": fails},
              "results": gate.results}
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(result, indent=2), encoding="utf-8")
        print("  json: %s" % args.json)
    return 0 if ready else 1


if __name__ == "__main__":
    sys.exit(main())
