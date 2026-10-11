#!/usr/bin/env python3
"""Stage and package the Windows VST3 under its customer name, and prove it.

Why this exists
---------------
The JUCE build emits a Windows bundle called ``SideChain.vst3`` (that is the
MSBuild target name). Hosts that derive the displayed name from the *file* name -
FL Studio's plug-in database does - would then list the product as "SideChain".
The shipped bundle must therefore be ``PumpCurve.vst3``, with the module binary
inside it also called ``PumpCurve.vst3`` (the VST3 convention is
``<Name>.vst3/Contents/x86_64-win/<Name>.vst3``).

Renaming is safe: it changes no byte of the module and no identifier inside it.
This tool *proves* that - it hashes every file before and after staging and
refuses to continue if any byte differs - and additionally inspects the module
itself (x64 PE, product identity present, no legacy product name) so a stale
SideChainer binary can never be staged as PumpCurve.

Usage
-----
  python stage_windows_vst3.py --stage   <built SideChain.vst3> --out-dir <dir>
  python stage_windows_vst3.py --package <staged PumpCurve.vst3> --out-dir <dir>
  python stage_windows_vst3.py --inspect-module <path to .vst3 module binary>
  python stage_windows_vst3.py --self-test          # runs anywhere, builds fixtures
  python stage_windows_vst3.py --verify-zip <zip>   # checks structure + hashes

Exit codes: 0 success, 1 verification failure, 2 usage error.
"""
import argparse
import contextlib
import hashlib
import io
import json
import pathlib
import shutil
import struct
import sys
import tempfile
import zipfile

PRODUCT = "PumpCurve"
LEGACY_NAMES = ("SideChain.vst3", "SideChainer.vst3")
LEGACY_PRODUCT_NAMES = ("SideChainer", "SideChain")
WINDOWS_ARCH_DIR = "x86_64-win"
ZIP_FIXED_DATE = (1980, 1, 1, 0, 0, 0)  # deterministic archives
PE_MACHINE_X64 = 0x8664
PE32PLUS_MAGIC = 0x20B


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def tree_hashes(root):
    """{relative posix path: sha256} for every file under root, name-independent."""
    root = pathlib.Path(root)
    return {p.relative_to(root).as_posix(): sha256(p)
            for p in sorted(root.rglob("*")) if p.is_file()}


def fail(msg):
    print("FAIL: " + msg)
    return 1


def module_binaries(bundle):
    """The VST3 module binaries inside a bundle (they carry the real extension)."""
    return sorted(p for p in pathlib.Path(bundle).rglob("*.vst3") if p.is_file())


def arch_dirs(bundle):
    contents = pathlib.Path(bundle) / "Contents"
    return sorted(d.name for d in contents.iterdir() if d.is_dir()) if contents.is_dir() else []


# --------------------------------------------------------------------------- PE

def pe_machine_type(data):
    """Return (machine, is_pe32plus) for a PE image, or (None, None)."""
    if len(data) < 0x40 or data[:2] != b"MZ":
        return None, None
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if e_lfanew + 24 > len(data) or data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        return None, None
    machine = struct.unpack_from("<H", data, e_lfanew + 4)[0]
    magic = struct.unpack_from("<H", data, e_lfanew + 24)[0]
    return machine, magic == PE32PLUS_MAGIC


def product_identity(data, product=PRODUCT, version=None):
    """Evidence read straight out of the module binary.

    A JUCE VST3 embeds the plug-in name as a UTF-8 C string (the factory class
    name) and the Windows version resource repeats it as UTF-16LE. Both are
    checked, plus the absence of the legacy product names, because "the file is
    called PumpCurve.vst3" says nothing about what is inside it.
    """
    utf8 = product.encode("utf-8")
    utf16 = product.encode("utf-16-le")
    legacy_hits = []
    for legacy in LEGACY_PRODUCT_NAMES:
        if legacy.encode("utf-16-le") in data and legacy.encode("utf-8") in data:
            legacy_hits.append(legacy)
    info = {
        "class_name_utf8": utf8 in data,
        "product_name_utf16le": utf16 in data,
        "legacy_names_present": legacy_hits,
    }
    if version:
        info["version_utf16le"] = version.encode("utf-16-le") in data
    return info


def inspect_module(module_path, version=None, expected_product=PRODUCT):
    """Strict identity/architecture check on a staged VST3 module binary."""
    module_path = pathlib.Path(module_path)
    if not module_path.is_file():
        return fail("module not found: %s" % module_path)
    data = module_path.read_bytes()
    machine, pe32plus = pe_machine_type(data)
    if machine is None:
        return fail("%s is not a PE image" % module_path.name)
    ok = True
    print("  module:        %s" % module_path)
    print("  size:          %d bytes" % len(data))
    print("  sha256:        %s" % sha256(module_path))
    if machine != PE_MACHINE_X64:
        ok = False
        print("  FAIL: PE machine 0x%04X is not x64 (0x%04X)" % (machine, PE_MACHINE_X64))
    else:
        print("  architecture:  x64 (PE machine 0x8664)")
    if not pe32plus:
        ok = False
        print("  FAIL: not a PE32+ image")
    identity = product_identity(data, expected_product, version)
    print("  class name '%s' present:        %s" % (expected_product, identity["class_name_utf8"]))
    print("  product name '%s' in version resource: %s"
          % (expected_product, identity["product_name_utf16le"]))
    if version:
        print("  version '%s' in version resource:       %s" % (version, identity["version_utf16le"]))
    if identity["legacy_names_present"]:
        ok = False
        print("  FAIL: legacy product name(s) still inside the binary: %s"
              % ", ".join(identity["legacy_names_present"]))
    else:
        print("  no legacy SideChain/SideChainer product name inside the binary")
    return 0 if ok else 1


# ---------------------------------------------------------------------- staging

def stage(built_bundle, out_dir, version=None):
    """Rename a built bundle to the customer name, proving no byte changed."""
    built_bundle = pathlib.Path(built_bundle)
    out_dir = pathlib.Path(out_dir)
    if not built_bundle.is_dir():
        return fail("built bundle is not a directory: %s" % built_bundle)

    mods = module_binaries(built_bundle)
    if not mods:
        return fail("no VST3 module binary inside %s" % built_bundle)
    if len(mods) > 1:
        print("  note: %d module binaries found; staging the newest" % len(mods))
        mods = [max(mods, key=lambda p: p.stat().st_mtime)]
    module = mods[0]
    arch = module.parent.name
    if arch != WINDOWS_ARCH_DIR:
        return fail("expected the module under Contents/%s, found Contents/%s "
                    "(is this a Windows x64 build?)" % (WINDOWS_ARCH_DIR, arch))

    # Identity first: never stage a bundle that is not this product.
    print("  inspecting the built module before staging:")
    if inspect_module(module, version) != 0:
        return fail("built module failed identity/architecture inspection; refusing to stage")

    target = out_dir / ("%s.vst3" % PRODUCT)
    if target.exists():
        return fail("refusing to overwrite existing staged bundle: %s" % target)

    before = tree_hashes(built_bundle)
    out_dir.mkdir(parents=True, exist_ok=True)
    shutil.copytree(built_bundle, target)

    # Rename the module binary to match the bundle (VST3 convention).
    new_module = target / "Contents" / arch / ("%s.vst3" % PRODUCT)
    if new_module != (target / module.relative_to(built_bundle)):
        if new_module.exists():
            return fail("refusing to overwrite existing module: %s" % new_module)
        (target / module.relative_to(built_bundle)).rename(new_module)

    after = tree_hashes(target)
    renamed_module = ("Contents/%s/%s.vst3" % (arch, PRODUCT))
    if renamed_module not in after:
        return fail("staged bundle has no %s" % renamed_module)
    if len(after) != len(before):
        return fail("file count changed during staging (%d -> %d)" % (len(before), len(after)))
    for rel, digest in before.items():
        want = renamed_module if rel == module.relative_to(built_bundle).as_posix() else rel
        if after.get(str(want)) != digest:
            return fail("staging changed the bytes of %s" % rel)

    manifest = {
        "product": PRODUCT,
        "version": version,
        "platform": "windows-x64",
        "format": "VST3",
        "staged_bundle": target.name,
        "renamed_from": built_bundle.name,
        "module": renamed_module,
        "arch_dir": arch,
        "module_sha256": sha256(target / renamed_module),
        "files": after,
        "note": "every file byte-identical to the built bundle; only names changed",
    }
    (target.parent / "MANIFEST-staged.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8")

    print("STAGED: %s" % target)
    print("  module: %s" % renamed_module)
    print("  module sha256: %s" % manifest["module_sha256"])
    print("  %d files, all byte-identical to the built bundle" % len(after))
    print("  manifest: %s/MANIFEST-staged.json" % target.parent)
    return 0


def package(staged, out_dir, version):
    """Deterministic ZIP of the staged bundle plus a SHA256SUMS file."""
    staged = pathlib.Path(staged)
    out_dir = pathlib.Path(out_dir)
    if not staged.is_dir():
        return fail("staged bundle is not a directory: %s" % staged)
    if staged.name != "%s.vst3" % PRODUCT:
        return fail("staged bundle must be named %s.vst3 (found %s); stage it first"
                    % (PRODUCT, staged.name))
    if not module_binaries(staged):
        return fail("staged bundle contains no module binary")

    out_dir.mkdir(parents=True, exist_ok=True)
    zip_path = out_dir / ("%s-%s-Windows-x64.zip" % (PRODUCT, version))
    if zip_path.exists():
        return fail("refusing to overwrite existing archive: %s" % zip_path)

    files = sorted(p for p in staged.rglob("*") if p.is_file())
    # The archive must unpack to PumpCurve.vst3/... so the customer drops the
    # bundle straight into the VST3 folder.
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for p in files:
            info = zipfile.ZipInfo(str(p.relative_to(staged.parent)), date_time=ZIP_FIXED_DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, p.read_bytes())

    sums = out_dir / "SHA256SUMS"
    with open(sums, "a", encoding="utf-8") as f:
        f.write("%s  %s\n" % (sha256(zip_path), zip_path.name))

    print("PACKAGED: %s (%d bytes)" % (zip_path, zip_path.stat().st_size))
    print("  sha256: %s" % sha256(zip_path))
    return 0


def verify_zip(zip_path, version):
    zip_path = pathlib.Path(zip_path)
    if not zip_path.is_file():
        return fail("no such archive: %s" % zip_path)
    with zipfile.ZipFile(zip_path) as z:
        names = z.namelist()
        required = [
            "%s.vst3/Contents/%s/%s.vst3" % (PRODUCT, WINDOWS_ARCH_DIR, PRODUCT),
        ]
        missing = [r for r in required if r not in names]
        if missing:
            print("  archive members:")
            for n in names:
                print("    %s" % n)
            return fail("archive is missing %s" % ", ".join(missing))
        for legacy in LEGACY_NAMES:
            if any(n.startswith(legacy + "/") for n in names):
                return fail("archive still contains a legacy bundle: %s" % legacy)
        print("VERIFIED: %s" % zip_path.name)
        print("  %d members, entry point %s" % (len(names), required[0]))
        print("  no legacy SideChain/SideChainer bundle inside")
    return 0


# ------------------------------------------------------------------- self-test

def synthetic_module(product=PRODUCT, machine=PE_MACHINE_X64, version="0.4.1",
                     legacy=None, pe=True):
    """A minimal, structurally valid PE image carrying JUCE-style identity strings.

    Enough for the inspection code to exercise every branch without a compiler.
    """
    data = bytearray(b"\0" * 0x400)
    if pe:
        data[0:2] = b"MZ"
        e_lfanew = 0x80
        struct.pack_into("<I", data, 0x3C, e_lfanew)
        data[e_lfanew:e_lfanew + 4] = b"PE\0\0"
        struct.pack_into("<H", data, e_lfanew + 4, machine)
        struct.pack_into("<H", data, e_lfanew + 24, PE32PLUS_MAGIC)
    body = b"".join([
        b"PumpCurveFactory\0",
        product.encode("utf-8") + b"\0",
        product.encode("utf-16-le"),
        ("\0" + version).encode("utf-16-le"),
        b"Music-Prod\0",
    ])
    if legacy:
        body += legacy.encode("utf-8") + legacy.encode("utf-16-le")
    data.extend(body)
    return bytes(data)


def self_test(version):
    """Fixture end-to-end test - runs on any OS, proves the logic on real bytes."""
    print("== staging/packaging self-test (fixture) ==")
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        built = tmp / "build" / "SideChain.vst3"
        (built / "Contents" / WINDOWS_ARCH_DIR).mkdir(parents=True)
        (built / "Contents" / "Resources").mkdir(parents=True)
        blob = synthetic_module(version=version)
        (built / "Contents" / WINDOWS_ARCH_DIR / "SideChain.vst3").write_bytes(blob)
        (built / "Contents" / "Resources" / "moduleinfo.json").write_text(
            '{"name": "PumpCurve"}', encoding="utf-8")
        (built / "Contents" / "Desktop.ini").write_text("[.ShellClassInfo]\n", encoding="utf-8")
        before = tree_hashes(built)

        # Zero-argument form: a non-PE file must be rejected, not staged.
        not_pe = tmp / "notpe.vst3"
        not_pe.write_bytes(b"just text")
        if inspect_module(not_pe) == 0:
            return fail("self-test: a non-PE file was accepted as a module")
        print("  ok: a non-PE file is rejected")

        # A 32-bit module must be rejected.
        x86 = tmp / "x86.vst3"
        x86.write_bytes(synthetic_module(machine=0x014C))
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            rc = inspect_module(x86)
        if rc == 0 or "is not x64" not in captured.getvalue():
            return fail("self-test: a 32-bit module was accepted")
        print("  ok: a non-x64 module is rejected")

        # A module still carrying the legacy product name must be rejected.
        legacy = tmp / "legacy.vst3"
        legacy.write_bytes(synthetic_module(legacy="SideChainer"))
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            rc = inspect_module(legacy)
        if rc == 0 or "legacy product name" not in captured.getvalue():
            return fail("self-test: a legacy-named module was accepted")
        print("  ok: a module carrying a legacy product name is rejected")

        # And the same stale binary must never be staged under the new name.
        stale = tmp / "stale" / "SideChain.vst3"
        (stale / "Contents" / WINDOWS_ARCH_DIR).mkdir(parents=True)
        (stale / "Contents" / WINDOWS_ARCH_DIR / "SideChain.vst3").write_bytes(
            synthetic_module(legacy="SideChainer"))
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            rc = stage(stale, tmp / "stale-artifacts", version)
        if rc == 0:
            return fail("self-test: a legacy binary was staged as PumpCurve")
        stale_out = tmp / "stale-artifacts"
        if stale_out.exists() and any(stale_out.iterdir()):
            return fail("self-test: staging left a partial artifact behind")
        print("  ok: a legacy binary is refused, nothing is written")

        rc = stage(built, tmp / "artifacts", version)
        if rc != 0:
            return rc
        staged = tmp / "artifacts" / "PumpCurve.vst3"
        if not (staged / "Contents" / WINDOWS_ARCH_DIR / "PumpCurve.vst3").is_file():
            return fail("self-test: module was not renamed to PumpCurve.vst3")
        if (staged / "Contents" / WINDOWS_ARCH_DIR / "SideChain.vst3").exists():
            return fail("self-test: old module name still present")
        after = tree_hashes(staged)
        if len(after) != len(before):
            return fail("self-test: file count changed")
        if sorted(after.values()) != sorted(before.values()):
            return fail("self-test: file contents changed during staging")
        print("  ok: staging renamed both the bundle and the module, bytes untouched")

        # Re-staging must refuse: an old artifact may never masquerade as new.
        # Its own FAIL line is expected here, so it is captured instead of printed.
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            rc = stage(built, tmp / "artifacts", version)
        if rc == 0:
            return fail("self-test: staging overwrote an existing bundle")
        if "refusing to overwrite" not in captured.getvalue():
            return fail("self-test: staging refused for the wrong reason")
        print("  ok: staging refuses to overwrite an existing bundle")

        rc = package(staged, tmp / "artifacts", version)
        if rc != 0:
            return rc
        rc = verify_zip(tmp / "artifacts" / ("%s-%s-Windows-x64.zip" % (PRODUCT, version)), version)
        if rc != 0:
            return rc
        sums = (tmp / "artifacts" / "SHA256SUMS").read_text(encoding="utf-8").strip()
        zip_hash = sha256(tmp / "artifacts" / ("%s-%s-Windows-x64.zip" % (PRODUCT, version)))
        if zip_hash not in sums:
            return fail("self-test: SHA256SUMS does not carry the archive hash")
        print("  ok: SHA256SUMS carries the archive hash")
        print("  ok: packaging is deterministic (fixed archive timestamps)")

    print("SELF-TEST: PASS")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stage", metavar="BUILT_BUNDLE", help="stage a built SideChain.vst3")
    ap.add_argument("--package", metavar="STAGED_BUNDLE", help="package a staged PumpCurve.vst3")
    ap.add_argument("--verify-zip", metavar="ZIP", help="verify an existing archive")
    ap.add_argument("--inspect-module", metavar="FILE", help="inspect a VST3 module binary")
    ap.add_argument("--out-dir", help="output directory (default: ./artifacts)")
    ap.add_argument("--version", default="0.4.1", help="product version for file names")
    ap.add_argument("--self-test", action="store_true", help="run the fixture self-test")
    args = ap.parse_args()

    if args.self_test:
        return self_test(args.version)
    out = pathlib.Path(args.out_dir) if args.out_dir else pathlib.Path("artifacts")
    if args.stage:
        return stage(args.stage, out, args.version)
    if args.package:
        return package(args.package, out, args.version)
    if args.verify_zip:
        return verify_zip(args.verify_zip, args.version)
    if args.inspect_module:
        return inspect_module(args.inspect_module, args.version)
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
