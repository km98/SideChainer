#!/usr/bin/env python3
"""PumpCurve Windows x64 VST3 build driver (JUCE 6.1.3 / Projucer + MSBuild).

Runs on the Windows build host (or a GitHub-hosted Windows runner); every mode
except the build itself also runs on macOS/Linux so the pipeline can be checked
without Windows.

Usage
-----
  python build_windows_vst3.py --check                      # tree self-check (any OS)
  python build_windows_vst3.py --build-projucer --juce-root <stock JUCE tree>
  python build_windows_vst3.py --build --juce-root <patched JUCE tree> \
                               --projucer <Projucer.exe> [--out-dir <dir>]

What --build does, in order
---------------------------
 1. validates the .jucer product identity and the JUCE tree against the recorded
    manifest (never builds against drifted JUCE),
 2. exports the Visual Studio 2022 solution with Projucer 6.1.3,
 3. builds the VST3 target x64 Release with MSBuild,
 4. locates the built module (the Visual Studio exporter links a flat
    SideChain.vst3 DLL; a bundled layout is accepted as well if one appears),
 5. stages it as PumpCurve.vst3 (byte-identical module, canonical bundle layout)
    and packages a deterministic ZIP,
 6. writes RUN-MANIFEST.json with every artifact hash.

Between 2 and 3 the generated Windows version resource is corrected: JUCE fills
its ProductName/FileDescription from the *project* name, which is SideChain, and
the customer must not see that as the product name.

Prerequisites on Windows: Visual Studio 2022 with "Desktop development with C++"
(v143) plus MSBuild on PATH from a Developer Command Prompt. CMake is not used.

Exit codes: 0 success, 1 build/verification failure, 2 usage error.
"""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

EXPECT_PLUGIN_NAME = "PumpCurve"
EXPECT_PLUGIN_CODE = "SdCh"
EXPECT_COMPANY = "Music-Prod"
EXPECT_VERSION = "0.5.1"
EXPECT_BUNDLE_ID = "com.musicprod.sidechain"

HERE = pathlib.Path(__file__).resolve().parent
WINDEV = HERE.parent                     # <repo>/SideChain/Builds/Windows
PROJECT = WINDEV.parents[1]              # <repo>/SideChain  (the plug-in project)
REPO = PROJECT.parent                    # <repo>            (the git repository)
JUCER = PROJECT / "SideChain.jucer"
SOURCE = PROJECT / "Source"
WIN_ASSETS = WINDEV / "juce"
VERIFY_JUCE = WIN_ASSETS / "verify_juce_tree.py"
STAGE_TOOL = HERE / "stage_windows_vst3.py"
INSTALLER = WINDEV / "installer" / "PumpCurve.iss"
EXPECT_JUCE_MODULES = [
    "juce_audio_basics", "juce_audio_devices", "juce_audio_formats",
    "juce_audio_processors", "juce_audio_utils", "juce_core",
    "juce_data_structures", "juce_dsp", "juce_events", "juce_graphics",
    "juce_gui_basics", "juce_gui_extra",
]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fail(msg):
    print("FAIL: " + msg)
    return 1


def git_revision():
    try:
        out = subprocess.run(["git", "rev-parse", "HEAD"], cwd=str(REPO),
                             capture_output=True, text=True)
        if out.returncode == 0:
            return out.stdout.strip()
    except OSError:
        pass
    return None


def read_jucer_attrs():
    """Attributes of the <JUCERPROJECT> element ONLY.

    The XML declaration on line 1 also carries version="1.0"; reading it would
    report the wrong project version, so the element tag is isolated first.
    """
    if not JUCER.is_file():
        return None
    text = JUCER.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"<JUCERPROJECT\b[^>]*>", text, re.S)
    if not m:
        return None
    tag = m.group(0)
    attrs = {}
    for key in ("name", "version", "pluginName", "pluginCode", "companyName",
                "bundleIdentifier", "pluginManufacturerCode", "projectType",
                "pluginManufacturer", "buildVST3", "buildAU"):
        a = re.search(r'\b%s="([^"]*)"' % key, tag)
        attrs[key] = a.group(1) if a else None
    return attrs


def check_tree(juce_root=None):
    """Validate the build inputs. Runnable on macOS/Linux/Windows."""
    print("== PumpCurve Windows build self-check ==")
    ok = True

    if not JUCER.is_file():
        return fail("jucer project missing: %s" % JUCER)
    print("  jucer:            %s" % JUCER)

    n_src = len(list(SOURCE.glob("*.cpp"))) + len(list(SOURCE.glob("*.h")))
    print("  Source files:     %d" % n_src)
    if n_src == 0:
        ok = False
        print("  FAIL: no sources found under %s" % SOURCE)

    attrs = read_jucer_attrs()
    checks = (("pluginName", EXPECT_PLUGIN_NAME), ("pluginCode", EXPECT_PLUGIN_CODE),
              ("companyName", EXPECT_COMPANY), ("version", EXPECT_VERSION),
              ("bundleIdentifier", EXPECT_BUNDLE_ID), ("projectType", "audioplug"),
              ("buildVST3", "1"))
    for key, want in checks:
        got = attrs.get(key)
        mark = "ok" if got == want else "MISMATCH"
        print("  %-20s %-24s (%s)" % (key + ":", got, mark))
        if got != want:
            ok = False

    for rel in ("tools/stage_windows_vst3.py", "juce/verify_juce_tree.py",
                "juce/JUCE-6.1.3-patched-overlay.tar.gz",
                "juce/JUCE-6.1.3-patched-modules.sha256",
                "installer/PumpCurve.iss", "installer/verify_install.ps1",
                "installer/build_installer.cmd"):
        if (WINDEV / rel).is_file():
            print("  present:          %s" % rel)
        else:
            ok = False
            print("  FAIL: missing %s" % rel)

    if juce_root:
        print("  JUCE tree:        %s" % juce_root)
        for mod in EXPECT_JUCE_MODULES:
            if not (pathlib.Path(juce_root) / "modules" / mod).is_dir():
                ok = False
                print("  FAIL: JUCE module missing: %s" % mod)
        rc = subprocess.run([sys.executable, str(VERIFY_JUCE), "--verify",
                             "--root", str(juce_root)])
        if rc.returncode != 0:
            ok = False
            print("  FAIL: JUCE tree does not match the recorded manifest")
    else:
        print("  JUCE tree:        not supplied (pass --juce-root to verify it)")

    print("\nSELF-CHECK: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def run(cmd, cwd=None):
    print("  $ %s" % " ".join(str(c) for c in cmd))
    return subprocess.run([str(c) for c in cmd], cwd=cwd)


def run_captured(cmd, cwd=None):
    """Run a tool whose own diagnostics matter, and print them.

    Projucer explains a failed resave on stdout; without this the CI log only
    shows that it exited non-zero.
    """
    print("  $ %s" % " ".join(str(c) for c in cmd))
    proc = subprocess.run([str(c) for c in cmd], cwd=cwd, capture_output=True, text=True)
    for stream, text in (("out", proc.stdout), ("err", proc.stderr)):
        for line in (text or "").splitlines():
            print("    [%s] %s" % (stream, line))
    return proc


# Files that exist only in the patched tree, never in stock JUCE 6.1.3. Finding one
# of them through the project's own module resolution is unambiguous evidence that
# the build does not silently use a different JUCE.
PATCHED_MARKERS = ("juce_core/logging/juce_AudioThreadGuard.h",
                   "juce_core/containers/VariantBuffer.h",
                   "juce_core/logging/juce_AudioThreadGuard.cpp")


def juce_modules_root(path):
    """Return the JUCE modules directory a path points at, or None.

    Include paths appear both as ".../modules" and as ".../modules/juce_core", so
    the caller's path and its parent are both considered.
    """
    path = pathlib.Path(path)
    for candidate in (path, path.parent):
        if (candidate / "juce_core" / "juce_core.h").is_file():
            return candidate
    return None


def module_dir_has_patched_marker(module_dir):
    module_dir = pathlib.Path(module_dir)
    for marker in PATCHED_MARKERS:
        if (module_dir / marker).is_file():
            return marker
    return None


def jucer_module_dir():
    """The module directory the .jucer itself resolves to (relative paths)."""
    text = JUCER.read_text(encoding="utf-8", errors="replace")
    for path in re.findall(r'<MODULEPATH id="juce_core" path="([^"]+)"', text):
        candidate = (JUCER.parent / path.replace("\\", "/")).resolve()
        if candidate.is_dir():
            return candidate
    return None


def verify_generated_project_uses_patched_juce():
    """Prove the exported Visual Studio project builds against the patched tree.

    Two independent signals:
      1. the .jucer's own module paths resolve to a tree carrying a patched-only
         file (this is what Projucer used to write the project), and
      2. the generated project's include paths either also resolve to such a tree,
         or are consistent with it.
    A generated project that points at a JUCE tree *without* the marker is treated
    as drift and stops the build.
    """
    build_dir = PROJECT / "Builds" / "VisualStudio2022"
    project_files = sorted(build_dir.glob("*.vcxproj"))
    if not project_files:
        return fail("no .vcxproj generated under %s" % build_dir)

    jucer_modules = jucer_module_dir()
    if not jucer_modules:
        return fail("the .jucer has no resolvable juce_core module path; the module layout "
                    "(<repo>/../HISE/JUCE/modules) is not provisioned on this machine")
    jucer_marker = module_dir_has_patched_marker(jucer_modules)
    print("  .jucer module path:  %s" % jucer_modules)
    if not jucer_marker:
        return fail("the .jucer resolves juce_core to %s, which has none of the patched-only "
                    "files %s - refusing to build against a different JUCE"
                    % (jucer_modules, list(PATCHED_MARKERS)))
    print("  patched marker:      %s" % jucer_marker)

    # Confirm the generated project references that tree rather than something else.
    drifted = []
    matched = []
    for path in project_files + sorted(build_dir.glob("*.props")):
        text = path.read_text(encoding="utf-8", errors="replace")
        tokens = re.findall(r'(?:\.\.[\\/])+(?:[^;"<>]*?juce_core)', text)
        tokens += re.findall(r'[A-Za-z]:[^;"<>]*?\\juce_core', text)
        for token in tokens:
            candidate = (path.parent / token).resolve()
            root = juce_modules_root(candidate)
            if root is None:
                # Not a JUCE modules directory (a source file path, a SDK path, ...).
                continue
            if module_dir_has_patched_marker(root):
                matched.append(root)
            else:
                drifted.append(root)
    if drifted:
        return fail("the generated project also references JUCE modules without the patched "
                    "files: %s" % sorted({str(d) for d in drifted})[:3])
    if matched:
        print("  generated project:   %s" % sorted({str(m) for m in matched})[0])
    else:
        print("  generated project:   no juce_core include path could be resolved from the "
              ".vcxproj (Projucer may emit it in a property sheet); the .jucer check above "
              "is authoritative")
    return 0


def require_windows(mode):
    if os.name != "nt":
        return fail("%s requires Windows (use --check on other OSes)" % mode)
    return 0


def find_msbuild(explicit=None):
    """MSBuild to use: an explicit path wins, then PATH.

    A GitHub runner does not necessarily have MSBuild on PATH in a bash step, so
    the workflow locates it with vswhere and passes the path in.
    """
    if explicit:
        path = pathlib.Path(explicit)
        if path.is_file():
            return str(path)
        print("  warning: --msbuild %s is not a file; falling back to PATH" % explicit)
    return shutil.which("msbuild") or shutil.which("MSBuild.exe")


def build_projucer(juce_root, msbuild_arg=None):
    """Compile Projucer 6.1.3 from the *stock* tree (it is a tool, not the product).

    The plug-in itself is compiled against the patched tree; Projucer only has to
    be able to read the .jucer format of 6.1.3, so building it from unmodified
    sources keeps the two concerns separate.
    """
    rc = require_windows("--build-projucer")
    if rc:
        return rc
    juce_root = pathlib.Path(juce_root)
    sln = juce_root / "extras" / "Projucer" / "Builds" / "VisualStudio2022" / "Projucer.sln"
    if not sln.is_file():
        return fail("Projucer solution not found: %s" % sln)
    msbuild = find_msbuild(msbuild_arg)
    if not msbuild:
        return fail("msbuild not on PATH (run from a Developer Command Prompt, "
                    "or pass --msbuild <path to MSBuild.exe>)")
    print("  msbuild: %s" % msbuild)
    rc = run([msbuild, sln, "/m", "/p:Configuration=Release", "/p:Platform=x64"])
    if rc.returncode != 0:
        return fail("msbuild failed for Projucer")
    cands = sorted(juce_root.rglob("Projucer.exe"))
    if not cands:
        return fail("no Projucer.exe produced under %s" % juce_root)
    # Prefer a Release x64 build.
    exe = max(cands, key=lambda p: ("Release" in str(p), "x64" in str(p), p.stat().st_mtime))
    print("PROJUCER OK: %s" % exe)
    print("  sha256: %s" % sha256(exe))
    return 0


def locate_built_artifact(builds):
    """Find what MSBuild produced under the project's Builds directory.

    JUCE 6.1.3's Visual Studio exporter links a flat ``SideChain.vst3`` (a PE DLL
    named with the .vst3 extension) into .../Builds/VisualStudio2022/build/x64/
    Release/VST3/. A bundle directory is preferred when one exists, then the
    exporter output, then anything named .vst3 that is not inside a bundle.
    """
    builds = pathlib.Path(builds)

    def rank(path):
        if path.is_dir():
            kind = 0                      # a real bundle
        elif path.parent.name == "VST3":
            kind = 1                      # the exporter's flat output directory
        elif "Contents" in path.parts:
            kind = 3                      # module inside a bundle: least specific
        else:
            kind = 2
        return (kind, -path.stat().st_mtime)

    for pattern in ("SideChain.vst3", "*.vst3"):
        cands = [p for p in builds.rglob(pattern) if p.is_dir() or p.is_file()]
        cands = [p for p in cands if not any(part.endswith(".build") for part in p.parts)]
        if cands:
            return sorted(cands, key=rank)[0]
    return None


def fix_generated_resource_identity(builds_dir):
    """Rewrite the product identity JUCE writes into the generated Windows .rc.

    JUCE 6.1.3 fills the VERSIONINFO resource from *project* names - see
    extras/Build/juce_build_tools/utils/juce_ResourceRc.cpp, where
    ``ProductName`` and ``FileDescription`` both come from the project name and
    only ``CompanyName`` comes from the company - and this project is called
    SideChain. The compiled module would therefore report "SideChain" as its
    product name in Windows file properties and in host plug-in information even
    though its VST3 class name is PumpCurve.

    Projucer regenerates resources.rc on every export, so the correction has to run
    after the resave and before MSBuild. Each generated file is rewritten in place,
    the original is preserved once as resources.rc.pre-pumpcurve for review, and
    the before/after hashes are returned for the run manifest.
    """
    builds_dir = pathlib.Path(builds_dir)
    candidates = sorted(p for p in builds_dir.rglob("resources.rc") if p.is_file())
    if not candidates:
        return None, "no generated resources.rc under %s (the exporter layout changed?)" % builds_dir

    pattern = re.compile(r'(VALUE\s+"(?:ProductName|FileDescription)",\s+")[^"]*(")')
    info = {}
    for rc in candidates:
        original = rc.read_text(encoding="utf-8", errors="replace")
        rewritten, matched = pattern.subn(r"\g<1>%s\g<2>" % EXPECT_PLUGIN_NAME, original)
        # "changes" counts real edits: re-running on an already corrected file must
        # report zero, otherwise a rerun would look like it modified the build.
        changes = matched if rewritten != original else 0
        entry = {"path": str(rc), "changes": changes, "values_matched": matched,
                 "before_sha256": hashlib.sha256(original.encode("utf-8")).hexdigest(),
                 "after_sha256": hashlib.sha256(rewritten.encode("utf-8")).hexdigest()}
        if changes:
            backup = rc.with_suffix(rc.suffix + ".pre-pumpcurve")
            if not backup.exists():
                backup.write_text(original, encoding="utf-8")
            rc.write_text(rewritten, encoding="utf-8")
            entry["backup"] = str(backup)
            print("  version resource: %s -> ProductName/FileDescription = %s (%d value(s))"
                  % (rc.relative_to(builds_dir), EXPECT_PLUGIN_NAME, changes))
        elif matched:
            print("  version resource: %s already identifies %s"
                  % (rc.relative_to(builds_dir), EXPECT_PLUGIN_NAME))
        else:
            print("  version resource: %s carries no ProductName/FileDescription value"
                  % rc.relative_to(builds_dir))
        info[str(rc.relative_to(builds_dir))] = entry

    # Verify by reading the values back, tolerating the padding the exporter emits
    # (``VALUE "ProductName",    "SideChain"``): an exact-string comparison here
    # would report a false failure and block a correct build.
    leftover, missing = [], []
    for rel in info:
        text = (builds_dir / rel).read_text(encoding="utf-8", errors="replace")
        for field in ("ProductName", "FileDescription"):
            match = re.search(r'VALUE\s+"%s",\s*"([^"]*)"' % field, text)
            if match is None:
                missing.append("%s (%s)" % (field, rel))
            elif match.group(1) != EXPECT_PLUGIN_NAME:
                leftover.append("%s=%s (%s)" % (field, match.group(1), rel))
    if missing:
        return None, ("the generated resource no longer carries %s - the exporter format "
                      "changed and the product identity cannot be guaranteed"
                      % ", ".join(missing))
    if leftover:
        return None, "generated resource still names another product: %s" % ", ".join(leftover)
    return info, None


def self_test_rc_identity():
    """Fixture test for the version-resource rewrite (runs on any OS)."""
    print("== version-resource identity self-test (fixture) ==")
    with tempfile.TemporaryDirectory() as tmp:
        builds = pathlib.Path(tmp) / "Builds" / "VisualStudio2022"
        (builds / "SideChain_VST3").mkdir(parents=True)
        rc = builds / "SideChain_VST3" / "resources.rc"
        # The padding is copied from a real Projucer-generated resources.rc: the
        # exporter aligns the values, so any comparison must tolerate whitespace.
        rc.write_text('BLOCK "040904b0"\nBEGIN\n'
                      '    VALUE "CompanyName",     "Music-Prod"\n'
                      '    VALUE "FileDescription", "SideChain"\n'
                      '    VALUE "ProductName",     "SideChain"\n'
                      'END\n', encoding="utf-8")
        info, error = fix_generated_resource_identity(builds)
        if error:
            return fail("self-test: %s" % error)
        text = rc.read_text(encoding="utf-8")
        if not re.search(r'VALUE\s+"ProductName",\s*"PumpCurve"', text):
            return fail("self-test: ProductName was not rewritten")
        if not re.search(r'VALUE\s+"FileDescription",\s*"PumpCurve"', text):
            return fail("self-test: FileDescription was not rewritten")
        if not re.search(r'VALUE\s+"CompanyName",\s*"Music-Prod"', text):
            return fail("self-test: CompanyName must not be touched")
        backup = builds / "SideChain_VST3" / "resources.rc.pre-pumpcurve"
        if not backup.is_file():
            return fail("self-test: the original resource was not preserved for review")
        kept = backup.read_text(encoding="utf-8")
        if not re.search(r'VALUE\s+"ProductName",\s*"SideChain"', kept):
            return fail("self-test: the preserved copy must be the original, not the rewrite")
        print("  ok: ProductName/FileDescription rewritten, CompanyName untouched, original kept")
        first = info[str(rc.relative_to(builds))]
        if not first["changes"] or first["before_sha256"] == first["after_sha256"]:
            return fail("self-test: the rewrite was not recorded as a change")
        info2, error2 = fix_generated_resource_identity(builds)
        if error2:
            return error2
        if info2[str(rc.relative_to(builds))]["changes"] != 0:
            return fail("self-test: the rewrite is not idempotent")
        print("  ok: running it again changes nothing")
        empty = pathlib.Path(tmp) / "empty"
        empty.mkdir()
        _, error3 = fix_generated_resource_identity(empty)
        if not error3:
            return fail("self-test: a missing resource file must be reported, not ignored")
        print("  ok: a missing generated resource is reported instead of silently skipped")

        # A resource whose value cannot be found must be reported: that means the
        # exporter format changed and the identity claim would be unverified.
        other = pathlib.Path(tmp) / "other" / "SideChain_VST3"
        other.mkdir(parents=True)
        (other / "resources.rc").write_text('# no version block here\n', encoding="utf-8")
        _, error4 = fix_generated_resource_identity(pathlib.Path(tmp) / "other")
        if not error4 or "no longer carries" not in error4:
            return fail("self-test: an unreadable resource format was not reported")
        print("  ok: a resource without the expected fields is reported")
    print("SELF-TEST: PASS")
    return 0


def build(juce_root, projucer, out_dir, msbuild_arg=None):
    rc = require_windows("--build")
    if rc:
        return rc
    juce_root = pathlib.Path(juce_root).resolve()
    pj = pathlib.Path(projucer).resolve()
    if not pj.is_file():
        return fail("Projucer not found: %s" % pj)
    if not juce_root.is_dir():
        return fail("JUCE tree not found: %s" % juce_root)

    # 0. Never build against drifted JUCE.
    rc = run([sys.executable, str(VERIFY_JUCE), "--verify", "--root", juce_root])
    if rc.returncode != 0:
        return fail("JUCE tree does not match the recorded manifest")

    out_dir = pathlib.Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    # 1. Export the Visual Studio 2022 solution from the .jucer.
    #
    # Projucer 6.1.3 has no --setglobalpath option: --resave exports every
    # exporter the project defines, and the project's module paths are relative to
    # the .jucer file, i.e. <repo>/../HISE/JUCE/modules. The caller therefore has
    # to provision the patched tree at that sibling location (the workflow does).
    rc = run_captured([pj, "--resave", JUCER])
    if rc.returncode != 0:
        return fail("Projucer --resave failed with exit code %d" % rc.returncode)

    sln = PROJECT / "Builds" / "VisualStudio2022" / "SideChain.sln"
    if not sln.is_file():
        return fail("expected solution not generated: %s (is the VS2022 exporter present "
                    "in the .jucer?)" % sln)
    rc = verify_generated_project_uses_patched_juce()
    if rc != 0:
        return rc

    # 1b. The exporter regenerated the Windows version resource: make it name the
    #     product rather than the project before anything is compiled.
    rc_info, rc_error = fix_generated_resource_identity(PROJECT / "Builds" / "VisualStudio2022")
    if rc_error:
        return fail(rc_error)

    # 2. Build the VST3 target, x64 Release.
    msbuild = find_msbuild(msbuild_arg)
    if not msbuild:
        return fail("msbuild not on PATH (run from a Developer Command Prompt, "
                    "or pass --msbuild <path to MSBuild.exe>)")
    print("  msbuild: %s" % msbuild)
    rc = run([msbuild, sln, "/m", "/p:Configuration=Release", "/p:Platform=x64"])
    if rc.returncode != 0:
        return fail("msbuild failed")

    # 3. Locate the built module and prove it really is this product.
    #    The Visual Studio exporter emits a flat SideChain.vst3 DLL; a bundle (as
    #    produced by JUCE's CMake or the Xcode exporter) is handled as well.
    built = locate_built_artifact(PROJECT / "Builds")
    if built is None:
        return fail("no VST3 build output produced under %s" % (PROJECT / "Builds"))
    print("  built artifact: %s (%s)"
          % (built, "bundle" if built.is_dir() else "flat module"))
    built_module = built if built.is_file() else None
    if built_module is None:
        inner = sorted(p for p in built.rglob("*.vst3") if p.is_file())
        built_module = max(inner, key=lambda p: p.stat().st_mtime) if inner else None
    if built_module is None:
        return fail("the built bundle contains no VST3 module binary")
    built_module_sha256 = sha256(built_module)
    print("  built module sha256: %s" % built_module_sha256)

    if (out_dir / ("%s.vst3" % EXPECT_PLUGIN_NAME)).exists():
        return fail("refusing to overwrite an existing staged artifact in %s" % out_dir)

    # 4. Stage under the customer name (byte-identical, renamed) and package.
    rc = run([sys.executable, str(STAGE_TOOL), "--stage", built, "--out-dir", out_dir,
              "--version", EXPECT_VERSION])
    if rc.returncode != 0:
        return fail("staging failed")
    staged = out_dir / ("%s.vst3" % EXPECT_PLUGIN_NAME)
    rc = run([sys.executable, str(STAGE_TOOL), "--package", staged,
              "--out-dir", out_dir, "--version", EXPECT_VERSION])
    if rc.returncode != 0:
        return fail("packaging failed")

    # 5. Keep the project file Projucer rewrote, so the exporter definition can be
    #    reviewed (and committed in its normalised form) instead of guessed at.
    shutil.copy2(JUCER, out_dir / "SideChain.jucer.resaved")

    # 6. Record every artifact hash next to the artifacts.
    zip_path = out_dir / ("%s-%s-Windows-x64.zip" % (EXPECT_PLUGIN_NAME, EXPECT_VERSION))
    staged_manifest = json.loads((out_dir / "MANIFEST-staged.json").read_text(encoding="utf-8"))
    # The shipped module must be the module the compiler produced, or nothing here
    # means anything.
    if staged_manifest.get("module_sha256") != built_module_sha256:
        return fail("staged module sha256 %s differs from the built module %s"
                    % (staged_manifest.get("module_sha256"), built_module_sha256))
    print("  identity proven: staged module == built module (%s)" % built_module_sha256)
    artifacts = {}
    for p in sorted(out_dir.glob("*")):
        if p.is_file() and p.name != "RUN-MANIFEST.json":
            artifacts[p.name] = {"bytes": p.stat().st_size, "sha256": sha256(p)}
    run_manifest = {
        "product": EXPECT_PLUGIN_NAME,
        "version": EXPECT_VERSION,
        "platform": "windows-x64",
        "format": "VST3",
        "source_revision": git_revision(),
        "juce": "6.1.3 + juce/JUCE-6.1.3-patched-overlay.tar.gz "
                "(verified against juce/JUCE-6.1.3-patched-modules.sha256)",
        "projucer": {"path": str(pj), "sha256": sha256(pj)},
        "built_bundle": str(built),
        "product_identity_rc": rc_info,
        "built_module_sha256": built_module_sha256,
        "staged_module_sha256": staged_manifest.get("module_sha256"),
        "artifacts": artifacts,
        "signed": False,
        "signing_note": "no Authenticode certificate is used in this pipeline; "
                        "the installer is unsigned until a certificate is supplied",
    }
    (out_dir / "RUN-MANIFEST.json").write_text(
        json.dumps(run_manifest, indent=2, sort_keys=True), encoding="utf-8")

    print("\nBUILD OK -> %s" % zip_path)
    for name, info in sorted(artifacts.items()):
        print("  %-44s %10d  %s" % (name, info["bytes"], info["sha256"]))
    print("Next: compile the installer (installer/build_installer.cmd), run the")
    print("install/uninstall test (installer/test_install_uninstall.ps1), then test the")
    print("plug-in in FL Studio / Ableton Live and record the results.")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="validate the build inputs only")
    ap.add_argument("--self-test", action="store_true",
                    help="run the version-resource identity self-test (any OS)")
    ap.add_argument("--build", action="store_true", help="build the VST3 (Windows)")
    ap.add_argument("--build-projucer", action="store_true",
                    help="compile Projucer 6.1.3 from a stock tree (Windows)")
    ap.add_argument("--juce-root", help="JUCE tree to build against")
    ap.add_argument("--projucer", help="path to Projucer.exe")
    ap.add_argument("--msbuild", help="path to MSBuild.exe (default: from PATH)")
    ap.add_argument("--out-dir", default=str(WINDEV / "artifacts"),
                    help="artifact directory (default: Builds/Windows/artifacts)")
    args = ap.parse_args()

    if args.check:
        return check_tree(args.juce_root or os.environ.get("SIDECHAIN_JUCE_ROOT"))
    if args.self_test:
        return self_test_rc_identity()
    if args.build_projucer:
        if not args.juce_root:
            ap.error("--build-projucer needs --juce-root")
        return build_projucer(args.juce_root, args.msbuild)
    if args.build:
        root = args.juce_root or os.environ.get("SIDECHAIN_JUCE_ROOT")
        if not root:
            ap.error("--build needs --juce-root (or SIDECHAIN_JUCE_ROOT)")
        if not args.projucer:
            ap.error("--build needs --projucer")
        return build(root, args.projucer, args.out_dir, args.msbuild)
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
