#!/usr/bin/env python3
"""Provision and prove the patched JUCE 6.1.3 tree a Windows build needs.

Two modes:

  --provision --dest DIR [--stock-tar FILE]
        download (or reuse) the official JUCE 6.1.3 release tarball, verify its
        SHA-256, extract it into DIR, apply the overlay that lives next to this
        script, then verify the result against the manifest.

  --verify --root DIR
        verify an already provisioned tree (DIR/modules) against the manifest.

  --extract-stock --dest DIR [--stock-tar FILE]
        extract the unmodified JUCE 6.1.3 tarball without applying the overlay.
        Projucer is built from this tree (it is a tool, not the product), while
        the plug-in itself is compiled against the patched tree.

The manifest is the contract: it lists the SHA-256 of every file of the patched
``modules/`` tree that the validated builds were compiled against. Verification
fails on a missing file, an extra file or a single changed byte, so a Windows
build cannot silently drift onto a different JUCE.

Exit codes: 0 verified, 1 verification failure, 2 usage error.
"""
import argparse
import hashlib
import pathlib
import shutil
import subprocess
import sys
import tarfile
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
OVERLAY = HERE / "JUCE-6.1.3-patched-overlay.tar.gz"
MANIFEST = HERE / "JUCE-6.1.3-patched-modules.sha256"
JUCE_VERSION = "6.1.3"
JUCE_URL = "https://codeload.github.com/juce-framework/JUCE/tar.gz/refs/tags/%s" % JUCE_VERSION
JUCE_SHA256 = "c3ab91886cbbaf0981932134a461bb3ef35dd379cb5521a0a44697767c4e30d6"
IGNORE_PREFIXES = ["juce_audio_devices/native/oboe/"]


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fail(message):
    print("FAIL: " + message)
    return 1


def read_manifest():
    entries = {}
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        digest, rel = line.split(None, 1)
        entries[rel.strip()] = digest
    return entries


def ignored(rel):
    return any(rel.startswith(p) for p in IGNORE_PREFIXES)


def verify(root):
    root = pathlib.Path(root)
    modules = root / "modules"
    if not modules.is_dir():
        return fail("no modules/ directory under %s" % root)
    entries = read_manifest()

    actual = {}
    for path in sorted(modules.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        if ignored(rel.replace("modules/", "", 1)):
            continue
        actual[rel] = sha256_file(path)

    missing = sorted(set(entries) - set(actual))
    extra = sorted(set(actual) - set(entries))
    changed = sorted(rel for rel in set(entries) & set(actual)
                     if entries[rel] != actual[rel])

    print("== JUCE %s tree verification ==" % JUCE_VERSION)
    print("  root:            %s" % root)
    print("  manifest:        %s (%d files)" % (MANIFEST.name, len(entries)))
    print("  files found:     %d" % len(actual))
    print("  ignored:         %s" % ", ".join(IGNORE_PREFIXES))
    ok = True
    if missing:
        ok = False
        print("  MISSING (%d):" % len(missing))
        for rel in missing[:20]:
            print("    %s" % rel)
    if extra:
        ok = False
        print("  UNEXPECTED (%d):" % len(extra))
        for rel in extra[:20]:
            print("    %s" % rel)
    if changed:
        ok = False
        print("  MODIFIED (%d):" % len(changed))
        for rel in changed[:20]:
            print("    %s" % rel)
    if ok:
        print("  every file byte-identical to the patched tree the builds were validated against")
    print("\nJUCE TREE: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def download(url, dest):
    print("  downloading %s" % url)
    with urllib.request.urlopen(url) as response, open(dest, "wb") as out:
        shutil.copyfileobj(response, out)


def provision(dest, stock_tar):
    dest = pathlib.Path(dest)
    for required in (OVERLAY, MANIFEST):
        if not required.is_file():
            return fail("missing %s next to this script" % required.name)

    work = dest.parent / (dest.name + "-dl")
    work.mkdir(parents=True, exist_ok=True)
    tar_path = pathlib.Path(stock_tar) if stock_tar else work / ("juce-%s.tar.gz" % JUCE_VERSION)
    if not tar_path.is_file():
        download(JUCE_URL, tar_path)
    got = sha256_file(tar_path)
    print("  JUCE %s archive sha256: %s" % (JUCE_VERSION, got))
    if got != JUCE_SHA256:
        return fail("JUCE tarball sha256 mismatch\n  expected %s\n  got      %s"
                    % (JUCE_SHA256, got))

    if dest.exists():
        shutil.rmtree(dest)
    extract_root = work / "extract"
    if extract_root.exists():
        shutil.rmtree(extract_root)
    extract_root.mkdir(parents=True)
    with tarfile.open(tar_path, "r:gz") as tar:
        tar.extractall(extract_root)
    inner = list(extract_root.glob("JUCE-*"))
    if len(inner) != 1:
        return fail("unexpected JUCE archive layout: %s" % [p.name for p in extract_root.iterdir()])
    shutil.move(str(inner[0]), str(dest))
    print("  extracted stock JUCE %s -> %s" % (JUCE_VERSION, dest))

    with tarfile.open(OVERLAY, "r:gz") as tar:
        members = [m for m in tar.getmembers() if m.isfile()]
        tar.extractall(dest)
    print("  applied overlay: %s (%d files)" % (OVERLAY.name, len(members)))

    return verify(dest)


def extract_stock(dest, stock_tar):
    """Extract the unmodified JUCE 6.1.3 tree (no overlay, no manifest)."""
    dest = pathlib.Path(dest)
    work = dest.parent / (dest.name + "-dl")
    work.mkdir(parents=True, exist_ok=True)
    tar_path = pathlib.Path(stock_tar) if stock_tar else work / ("juce-%s.tar.gz" % JUCE_VERSION)
    if not tar_path.is_file():
        download(JUCE_URL, tar_path)
    got = sha256_file(tar_path)
    print("  JUCE %s archive sha256: %s" % (JUCE_VERSION, got))
    if got != JUCE_SHA256:
        return fail("JUCE tarball sha256 mismatch\n  expected %s\n  got      %s"
                    % (JUCE_SHA256, got))
    if dest.exists():
        shutil.rmtree(dest)
    extract_root = work / "extract-stock"
    if extract_root.exists():
        shutil.rmtree(extract_root)
    extract_root.mkdir(parents=True)
    with tarfile.open(tar_path, "r:gz") as tar:
        tar.extractall(extract_root)
    inner = list(extract_root.glob("JUCE-*"))
    if len(inner) != 1:
        return fail("unexpected JUCE archive layout: %s" % [p.name for p in extract_root.iterdir()])
    shutil.move(str(inner[0]), str(dest))
    print("  extracted unmodified JUCE %s -> %s (overlay NOT applied)" % (JUCE_VERSION, dest))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--provision", action="store_true", help="extract + patch + verify")
    ap.add_argument("--extract-stock", action="store_true",
                    help="extract the unmodified JUCE 6.1.3 tarball (no overlay, no verify)")
    ap.add_argument("--verify", action="store_true", help="verify an existing tree")
    ap.add_argument("--dest", help="destination directory for --provision")
    ap.add_argument("--root", help="JUCE root to verify with --verify")
    ap.add_argument("--stock-tar", help="reuse an already downloaded JUCE 6.1.3 tarball")
    args = ap.parse_args()

    if args.provision:
        if not args.dest:
            ap.error("--provision needs --dest")
        return provision(args.dest, args.stock_tar)
    if args.extract_stock:
        if not args.dest:
            ap.error("--extract-stock needs --dest")
        return extract_stock(args.dest, args.stock_tar)
    if args.verify:
        if not args.root:
            ap.error("--verify needs --root")
        return verify(args.root)
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
