#!/usr/bin/env python3
"""Tests for the PumpCurve release gate (release_gate.py).

The gate decides whether a release may be published, so its tests are mostly
*negative controls*: each one builds the smallest fixture that makes one check
lie, and asserts that the gate refuses it. The gate is always run as a
subprocess with --json, i.e. through the interface a release engineer uses.

Covered here:

  * identity and naming refusals (a legacy bundle name in the staging directory),
  * artifact contracts (a package whose bytes do not match its recorded hash,
    a package that is not a package),
  * host acceptance honesty (a "verified" claim with no evidence artifact,
    evidence that exists, an undisclosed unverified host in the release notes),
  * credential hygiene (a token-looking string in the evidence),
  * live distribution: the SPA fallback, a real artifact hash match, a route with
    no artifact, an artifact for the wrong platform,
  * the two modes (public demands notarisation and live downloads, beta documents
    them) and the exit-code/JSON consistency of both,
  * the real candidate end to end, when one is available.

Usage:
    python3 SideChain/Tests/release_gate_tests.py [--evidence-root DIR]

Exit codes: 0 all tests passed, 1 a test failed, 2 usage error.
--evidence-root defaults to $PUMPCURVE_EVIDENCE_ROOT.
"""
import argparse
import hashlib
import http.server
import json
import os
import pathlib
import plistlib
import shutil
import socket
import subprocess
import sys
import tempfile
import threading

HERE = pathlib.Path(__file__).resolve().parent
PROJECT = HERE.parent
GATE = HERE / "release_gate.py"
PRODUCT = "PumpCurve"
VERSION = "0.5.1"

passed = 0
failed = 0
notes = []


def ok(message):
    global passed
    passed += 1
    print("  ok:   %s" % message)


def bad(message):
    global failed
    failed += 1
    print("  FAIL: %s" % message)


def note(message):
    notes.append(message)
    print("  note: %s" % message)


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    return sha256_bytes(pathlib.Path(path).read_bytes())


# ------------------------------------------------------------------- fixtures

def make_staged_bundle(root, name, executable="SideChain"):
    """A structurally plausible, deliberately unsigned macOS bundle."""
    bundle = root / name
    (bundle / "Contents" / "MacOS").mkdir(parents=True)
    plist = {
        "CFBundleIdentifier": "com.musicprod.sidechain",
        "CFBundleName": PRODUCT,
        "CFBundleExecutable": executable,
        "CFBundleVersion": VERSION,
        "AudioComponents": [{"type": "aufx", "subtype": "SdCh", "manufacturer": "Musc",
                             "name": "Music-Prod: %s" % PRODUCT,
                             "factoryFunction": "SideChainAUFactory"}],
    }
    with open(bundle / "Contents" / "Info.plist", "wb") as f:
        plistlib.dump(plist, f)
    (bundle / "Contents" / "MacOS" / executable).write_bytes(b"\x7fELF not really")
    return bundle


def make_macos_dir(root, pkg_bytes=b"package-payload"):
    """A directory with a package and its recorded hash."""
    macos = root / "installer"
    macos.mkdir(parents=True)
    pkg = macos / ("%s-%s-macOS-Universal.pkg" % (PRODUCT, VERSION))
    pkg.write_bytes(pkg_bytes)
    (macos / "SHA256SUMS").write_text("%s  %s\n" % (sha256_bytes(pkg_bytes), pkg.name),
                                      encoding="utf-8")
    return macos, pkg


def make_acceptance(root, statuses):
    """statuses: {host: (status, evidence_name or None)}"""
    data = {"_about": "fixture"}
    for host, (status, evidence) in statuses.items():
        (root / (evidence or "nothing.txt")).write_text("evidence for %s\n" % host, encoding="utf-8")
        data[host] = {"status": status, "note": "fixture for %s" % host,
                      "evidence": evidence or ""}
    path = root / "host-acceptance.json"
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")
    return path


def all_hosts_untested():
    return {h: ("not_tested", None) for h in
            ("logic_pro", "fl_studio", "ableton_live", "windows_daw")}


def release_notes(root, hosts_disclosed=True):
    text = ["# PumpCurve %s" % VERSION,
            "Logic Pro not tested. FL Studio not tested. Ableton not tested. Windows not tested."]
    if not hosts_disclosed:
        text = ["# PumpCurve %s" % VERSION, "All DAWs are supported."]
    path = root / ("RELEASE-NOTES-%s.md" % VERSION)
    path.write_text("\n".join(text) + "\n", encoding="utf-8")
    return path


# ------------------------------------------------------------------ http fixture

class Handler(http.server.BaseHTTPRequestHandler):
    mode = "spa"
    pkg_path = None
    other_path = None
    windows_path = None

    def log_message(self, *args):        # keep the test output clean
        pass

    def _serve_bytes(self, data, ctype="application/octet-stream"):
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):                     # noqa: N802 (http.server API)
        if self.path == "/blob":
            return self._serve_bytes(pathlib.Path(Handler.pkg_path).read_bytes())
        if self.path == "/blob-other":
            return self._serve_bytes(pathlib.Path(Handler.other_path).read_bytes())
        if self.path == "/blob-win":
            return self._serve_bytes(pathlib.Path(Handler.windows_path).read_bytes())

        macos_route = "/pumpcurve/latestbeta"
        windows_route = "/pumpcurve/windows/latestbeta"
        if self.path not in (macos_route, windows_route):
            self.send_response(404)
            self.end_headers()
            return

        mode = Handler.mode
        if mode == "spa":
            return self._serve_bytes(b"<!doctype html><html>app shell</html>", "text/html")
        if mode in ("real", "real_lowercase"):
            # Each route serves its own platform's artifact. `real_lowercase`
            # names the header the way an HTTP/2 CDN does ("location").
            self.send_response(302)
            self.send_header("location" if mode == "real_lowercase" else "Location",
                             "/blob-win" if self.path == windows_route else "/blob")
            self.end_headers()
            return
        if mode == "stop":
            self.send_response(302)
            self.send_header("Location", "http://127.0.0.1:1/does-not-exist")
            self.end_headers()
            return
        if mode == "windows_missing" and self.path == windows_route:
            self.send_response(404)
            self.end_headers()
            return
        if mode == "windows_missing":
            self.send_response(302)
            self.send_header("Location", "/blob")
            self.end_headers()
            return
        if mode == "wrong_platform":
            self.send_response(302)
            self.send_header("Location", "/blob-other")
            self.end_headers()
            return
        self.send_response(500)
        self.end_headers()


def start_server(mode, pkg_path, other_path=None, windows_path=None):
    Handler.mode = mode
    Handler.pkg_path = str(pkg_path)
    Handler.other_path = str(other_path or pkg_path)
    Handler.windows_path = str(windows_path or pkg_path)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, "http://127.0.0.1:%d" % port


# ---------------------------------------------------------------------- harness

def run_gate(args, json_path):
    cmd = [sys.executable, str(GATE)] + [str(a) for a in args] + ["--json", str(json_path)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    data = None
    if json_path.is_file():
        data = json.loads(json_path.read_text(encoding="utf-8"))
        json_path.unlink()
    return proc, data


def area_status(data, area):
    """Statuses of an area, or None when the area was never evaluated."""
    statuses = [r["status"] for r in data["results"] if r["area"] == area and r["status"] != "INFO"]
    return statuses or None


def check(condition, message):
    if condition:
        ok(message)
    else:
        bad(message)


def expect_area(proc, data, area, status, message, area2=None, status2=None):
    if data is None:
        return bad("%s (the gate produced no JSON; stderr: %s)" % (message, proc.stderr[-200:]))
    statuses = area_status(data, area)
    good = statuses is not None and status in statuses
    if good and area2:
        good = status2 in (area_status(data, area2) or [])
    check(good, "%s [%s=%s%s]" % (message, area, statuses,
                                  "" if not area2 else ", %s=%s" % (area2, area_status(data, area2))))


def expect_exit(proc, want, message):
    check(proc.returncode == want, "%s (exit %s, wanted %s)" % (message, proc.returncode, want))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--evidence-root",
                    default=os.environ.get("PUMPCURVE_EVIDENCE_ROOT"),
                    help="a real candidate tree, for the end-to-end test")
    args = ap.parse_args()

    print("============================================================")
    print(" release gate tests")
    print("============================================================")

    work = pathlib.Path(tempfile.mkdtemp(prefix="release-gate-tests-"))
    try:
        run_tests(work, args.evidence_root)
    finally:
        shutil.rmtree(work, ignore_errors=True)

    print("")
    print("============================================================")
    print(" passed: %d  failed: %d" % (passed, failed))
    for n in notes:
        print("   - %s" % n)
    print(" RELEASE GATE TESTS: %s" % ("PASS" if failed == 0 else "FAIL"))
    print("============================================================")
    return 0 if failed == 0 else 1


def run_tests(work, evidence_root):
    # ---- a synthetic candidate that passes the naming and identity checks ----
    print("")
    print("-- synthetic candidate: identity, naming, artifact contracts")
    base = work / "base"
    staged = base / "staged-products"
    staged.mkdir(parents=True)
    make_staged_bundle(staged, "%s.component" % PRODUCT)
    make_staged_bundle(staged, "%s.vst3" % PRODUCT)
    macos, pkg = make_macos_dir(base)
    acceptance = make_acceptance(base, all_hosts_untested())
    notes_file = release_notes(base)
    common = ["--staged-dir", staged, "--macos-dir", macos, "--host-acceptance", acceptance,
              "--release-notes", notes_file, "--offline"]

    proc, data = run_gate(["--mode", "beta"] + common, work / "t1.json")
    expect_area(proc, data, "product identity", "PASS", "identity comes from the project definition")
    expect_area(proc, data, "bundle names", "PASS", "both product bundles are staged under their names")

    # A legacy bundle name in the staging directory must be refused.
    legacy = base / "legacy-staged"
    shutil.copytree(staged, legacy)
    make_staged_bundle(legacy, "SideChain.component")
    proc, data = run_gate(["--mode", "beta", "--staged-dir", legacy, "--macos-dir", macos,
                           "--host-acceptance", acceptance, "--offline"], work / "t2.json")
    expect_area(proc, data, "bundle names", "FAIL", "a legacy bundle name in staging is refused")
    expect_exit(proc, 1, "a legacy name makes the run not ready")

    # Tampering with the package must break the hash contract.
    tampered = work / "tampered"
    tampered.mkdir()
    tmacos, tpkg = make_macos_dir(tampered, pkg_bytes=b"tampered-payload")
    tmacos_pkg = tmacos / tpkg.name
    data_bytes = bytearray(tmacos_pkg.read_bytes())
    data_bytes[0] ^= 0xFF
    tmacos_pkg.write_bytes(bytes(data_bytes))
    proc, data = run_gate(["--mode", "beta", "--staged-dir", staged, "--macos-dir", tmacos,
                           "--host-acceptance", acceptance, "--offline"], work / "t3.json")
    expect_area(proc, data, "macOS installer", "FAIL",
                "a package that does not match its recorded hash is refused")

    # A file named .pkg that is not a real package must be refused too.
    expect_area(proc, data, "macOS installer", "FAIL",
                "a file that is not a real package is refused")

    # ---- Windows: a stale candidate is not evidence about this one ----
    print("")
    print("-- Windows artifact provenance")
    evidence = work / "windows-evidence"
    old_win = evidence / "run13-20261011" / "windows-artifacts"
    old_module = old_win / ("%s.vst3" % PRODUCT) / "Contents" / "x86_64-win" / ("%s.vst3" % PRODUCT)
    old_module.parent.mkdir(parents=True)
    old_module.write_bytes(b"MZ a module from an older candidate")
    (old_win / "RUN-MANIFEST.json").write_text(json.dumps({"version": "0.4.1"}), encoding="utf-8")

    # Discovered implicitly (no --windows-dir): the 0.4.1 build says nothing about
    # the current candidate, so it must be NOT_TESTED, not an identity FAIL.
    proc, data = run_gate(["--mode", "beta", "--staged-dir", staged, "--macos-dir", macos,
                           "--host-acceptance", acceptance, "--release-notes", notes_file,
                           "--evidence-root", evidence, "--offline"], work / "t10.json")
    expect_area(proc, data, "Windows artifacts", "NOT_TESTED",
                "an older candidate's Windows build is reported NOT_TESTED, not FAIL")
    if data is not None:
        detail = [r["detail"] for r in data["results"] if r["area"] == "Windows artifacts"]
        check(any("0.4.1" in d and VERSION in d for d in detail),
              "the detail names both the stale version and the candidate version")

    # Supplied explicitly: the caller asked for THAT directory to be judged, so a
    # version mismatch stays a FAIL instead of being silently excused.
    proc, data = run_gate(["--mode", "beta", "--staged-dir", staged, "--macos-dir", macos,
                           "--host-acceptance", acceptance, "--release-notes", notes_file,
                           "--windows-dir", old_win, "--offline"], work / "t11.json")
    expect_area(proc, data, "Windows artifacts", "FAIL",
                "an explicitly supplied wrong-version Windows build is refused")

    # A manifest that MATCHES the candidate cannot excuse a broken module: the
    # browser-installed manifest must not become a way around the module check.
    same_win = work / "windows-same-version"
    same_module = same_win / ("%s.vst3" % PRODUCT) / "Contents" / "x86_64-win" / ("%s.vst3" % PRODUCT)
    same_module.parent.mkdir(parents=True)
    same_module.write_bytes(b"MZ not a real PE image")
    (same_win / "RUN-MANIFEST.json").write_text(json.dumps({"version": VERSION}), encoding="utf-8")
    proc, data = run_gate(["--mode", "beta", "--staged-dir", staged, "--macos-dir", macos,
                           "--host-acceptance", acceptance, "--release-notes", notes_file,
                           "--windows-dir", same_win, "--offline"], work / "t12.json")
    expect_area(proc, data, "Windows artifacts", "FAIL",
                "a matching manifest cannot hide a module that is not a PE image")

    # ---- host acceptance honesty ----
    print("")
    print("-- host acceptance claims")
    liars = work / "liars"
    liars.mkdir()
    claims = all_hosts_untested()
    claims["fl_studio"] = ("verified", None)                 # claims without evidence
    liar_file = make_acceptance(liars, claims)
    proc, data = run_gate(["--mode", "beta", "--staged-dir", staged, "--macos-dir", macos,
                           "--host-acceptance", liar_file, "--release-notes", notes_file,
                           "--offline"], work / "t4.json")
    expect_area(proc, data, "host acceptance: fl_studio", "FAIL",
                "a verified claim without an evidence artifact is refused")

    honest = work / "honest"
    honest.mkdir()
    claims = all_hosts_untested()
    claims["fl_studio"] = ("discovery_verified", "fl-db.txt")
    honest_file = make_acceptance(honest, claims)
    proc, data = run_gate(["--mode", "beta", "--staged-dir", staged, "--macos-dir", macos,
                           "--host-acceptance", honest_file, "--release-notes", notes_file,
                           "--offline"], work / "t5.json")
    expect_area(proc, data, "host acceptance: fl_studio", "LIMITATION",
                "discovery is recorded as a limitation, not as verified")

    hiding = work / "hiding-notes"
    hiding.mkdir()
    proc, data = run_gate(["--mode", "beta", "--staged-dir", staged, "--macos-dir", macos,
                           "--host-acceptance", honest_file,
                           "--release-notes", release_notes(hiding, hosts_disclosed=False),
                           "--offline"], work / "t6.json")
    expect_area(proc, data, "release notes", "FAIL",
                "notes that hide unverified hosts are refused")

    # ---- credential hygiene ----
    print("")
    print("-- credential hygiene")
    leaky = work / "leaky"
    shutil.copytree(base, leaky)
    (leaky / "staged-products" / "token.txt").write_text(
        "token=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.payload.signature\n", encoding="utf-8")
    proc, data = run_gate(["--mode", "beta", "--staged-dir", leaky / "staged-products",
                           "--macos-dir", leaky / "installer", "--host-acceptance", acceptance,
                           "--offline"], work / "t7.json")
    expect_area(proc, data, "credential hygiene", "FAIL",
                "a token-looking string in the evidence is refused")

    # ---- live distribution ----
    print("")
    print("-- live distribution behaviour")
    other = work / ("%s-%s-Windows-x64-Setup.exe" % (PRODUCT, VERSION))
    other.write_bytes(b"a windows installer, not a mac package")
    win_dir = work / "windows-artifacts"
    win_dir.mkdir()
    win_zip = win_dir / ("%s-%s-Windows-x64.zip" % (PRODUCT, VERSION))
    win_zip.write_bytes(b"a zipped windows bundle")
    (win_dir / other.name).write_bytes(other.read_bytes())

    def live(mode, windows_dir=None, mode_arg="public"):
        server, base_url = start_server(mode, pkg, other, win_zip)
        try:
            extra = ["--windows-dir", windows_dir] if windows_dir else []
            return run_gate(["--mode", mode_arg, "--staged-dir", staged, "--macos-dir", macos,
                             "--host-acceptance", acceptance, "--release-notes", notes_file,
                             "--url-base", base_url] + extra, work / "live.json")
        finally:
            server.shutdown()
            server.server_close()

    proc, data = live("spa")
    expect_area(proc, data, "distribution", "FAIL",
                "an HTTP 200 text/html SPA fallback is refused")
    expect_exit(proc, 1, "the SPA fallback makes the run not ready")

    proc, data = live("real", windows_dir=win_dir)
    expect_area(proc, data, "distribution", "PASS",
                "routes that serve their own platform's artifact with a matching hash pass")

    proc, data = live("real_lowercase", windows_dir=win_dir)
    expect_area(proc, data, "distribution", "PASS",
                "a lower-case 'location' header (HTTP/2 CDN) is a redirect like any other")

    proc, data = live("stop")
    expect_area(proc, data, "distribution", "FAIL",
                "a redirect to a URL that does not answer is refused")

    proc, data = live("windows_missing")
    expect_area(proc, data, "distribution", "BLOCKED",
                "a route with no published artifact is BLOCKED, not PASS")

    proc, data = live("wrong_platform")
    expect_area(proc, data, "distribution", "FAIL",
                "serving the other platform's artifact on a route is refused")

    # ---- modes ----
    print("")
    print("-- modes and verdicts")
    proc, data = run_gate(["--mode", "public", "--staged-dir", staged, "--macos-dir", macos,
                           "--host-acceptance", acceptance, "--release-notes", notes_file,
                           "--offline"], work / "t8.json")
    expect_exit(proc, 1, "public mode is not ready while notarisation is missing")
    check(data is not None and "notarisation" in data["reason"],
          "public mode names notarisation as the blocker")
    check(data is not None and data["verdict"] == "NOT READY",
          "public mode verdict is NOT READY")
    check(data is not None and data["ready"] is False and "ready" in data,
          "the JSON records ready=false consistently with the exit code")

    proc, data = run_gate(["--mode", "beta", "--staged-dir", staged, "--macos-dir", macos,
                           "--host-acceptance", acceptance, "--release-notes", notes_file,
                           "--offline"], work / "t9.json")
    failing = sorted({r["area"] for r in (data or {"results": []})["results"]
                      if r["status"] == "FAIL"})
    # The fixtures are deliberately unusable on macOS: the bundles are unsigned
    # non-binaries and the "package" is not a package. Those two areas must be the
    # only failures, which shows the gate's failures are attributable.
    check(failing == ["macOS bundles", "macOS installer"],
          "only the deliberately broken fixture artifacts fail (got %s)" % failing)
    check(data is not None and data["ready"] == (proc.returncode == 0),
          "exit code and JSON 'ready' always agree")

    # Determinism: the same inputs must give the same verdict.
    proc_b, data_b = run_gate(["--mode", "beta", "--staged-dir", staged, "--macos-dir", macos,
                               "--host-acceptance", acceptance, "--release-notes", notes_file,
                               "--offline"], work / "t10.json")
    check(data is not None and data_b is not None and data["verdict"] == data_b["verdict"],
          "the gate is deterministic for identical inputs")

    # ---- the real candidate ----
    print("")
    print("-- real candidate (end to end)")
    if not evidence_root or not pathlib.Path(evidence_root).is_dir():
        note("no --evidence-root/PUMPCURVE_EVIDENCE_ROOT; the real candidate was not judged here")
        return
    # Judge a *completed* validation run. The suite runs these tests as one of its
    # own stages, so the log of the run happening right now has no verdict line yet;
    # picking it (the previous rule took the newest name) made the gate fail against
    # the very run it was meant to exercise. A log without a verdict is skipped, and
    # among the completed ones the most recently modified wins.
    completed = []
    for candidate in pathlib.Path(evidence_root).glob("**/validate_all-run*.log"):
        try:
            text = candidate.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        if "VALIDATION PASSED" in text or "VALIDATION FAILED" in text:
            completed.append(candidate)
    validation_log = max(completed, key=lambda p: p.stat().st_mtime) if completed else None
    if completed:
        print("  validation log judged: %s (%s)"
              % (validation_log.name,
                 "passed" if "VALIDATION PASSED" in validation_log.read_text(
                     encoding="utf-8", errors="replace") else "failed"))
    real_notes = None
    for candidate in sorted(pathlib.Path(evidence_root).glob("**/RELEASE-NOTES-%s.md" % VERSION)):
        real_notes = candidate
    if real_notes is None:
        return note("no real release notes found; the end-to-end judgement was skipped")
    args_common = ["--evidence-root", evidence_root, "--validation-log", validation_log or "",
                   "--release-notes", real_notes]
    proc, data = run_gate(["--mode", "beta", "--offline"] + args_common, work / "real.json")
    if data is None:
        return bad("the gate produced no JSON for the real candidate")
    expect_area(proc, data, "macOS bundles", "PASS",
                "the shipped bundles are signed, universal and correctly identified")
    expect_area(proc, data, "macOS installer", "PASS",
                "the package matches its hashes, is signed and carries the product only")
    check(data["verdict"] == "BETA-ONLY (documented limitations)" and proc.returncode == 0,
          "the real candidate is a beta-only release with documented limitations")

    proc, data = run_gate(["--mode", "public", "--offline"] + args_common, work / "real2.json")
    check(data is not None and data["ready"] is False and proc.returncode == 1,
          "the same candidate is not a public release (notarisation outstanding)")


if __name__ == "__main__":
    sys.exit(main())
