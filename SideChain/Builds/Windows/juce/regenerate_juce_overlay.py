#!/usr/bin/env python3
"""Capture the delta between stock JUCE 6.1.3 and the locally patched tree.

Why this exists
---------------
The validated macOS build is compiled against a *locally patched* JUCE 6.1.3
tree (``SIDECHAIN_JUCE_ROOT``); the Windows build must use exactly the same
modules or it is not the same product. Shipping the whole ~36 MB tree inside the
product repository would be unappealing, so this script records only what
differs from stock 6.1.3:

    juce/JUCE-6.1.3-patched-overlay.tar.gz     every file that is not byte-equal
                                              to the JUCE 6.1.3 release tarball
    juce/JUCE-6.1.3-patched-modules.sha256     SHA-256 of every file of the
                                              patched ``modules/`` tree

``verify_juce_tree.py`` re-applies the overlay to a fresh JUCE 6.1.3 download and
proves the result matches that manifest byte for byte, so a Windows build can be
reproduced from public inputs plus this repository.

Usage
-----
    python3 regenerate_juce_overlay.py \
        --stock-modules  /path/to/JUCE-6.1.3/modules \
        --patched-modules /path/to/patched/JUCE/modules \
        --ignore juce_audio_devices/native/oboe/ \
        --out-dir .

Run it from this directory so the artifacts land next to this script. It refuses
to overwrite existing artifacts unless --force is given; a changed overlay must be
a deliberate, reviewed act.
"""
import argparse
import gzip
import hashlib
import io
import os
import pathlib
import sys
import tarfile

OVERLAY_NAME = "JUCE-6.1.3-patched-overlay.tar.gz"
MANIFEST_NAME = "JUCE-6.1.3-patched-modules.sha256"

# Not compiled on any desktop platform; the vendor directory also contains a
# README.md/readme.md pair that a case-insensitive file system cannot represent
# faithfully, which would make byte-exact verification impossible on Windows.
DEFAULT_IGNORE = ["juce_audio_devices/native/oboe/"]
FIXED_MTIME = 315532800  # 1980-01-01, the earliest timestamp a ZIP/tar may carry


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def walk(root, ignore):
    root = pathlib.Path(root)
    found = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(filenames):
            full = pathlib.Path(dirpath) / name
            rel = full.relative_to(root).as_posix()
            if any(rel.startswith(prefix) for prefix in ignore):
                continue
            if name in (".DS_Store",):
                continue
            found[rel] = full
    return found


def fail(message):
    print("FAIL: " + message)
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stock-modules", required=True,
                    help="modules/ directory of an unmodified JUCE 6.1.3 tree")
    ap.add_argument("--patched-modules", required=True,
                    help="modules/ directory of the patched tree that was validated")
    ap.add_argument("--ignore", action="append", default=[],
                    help="path prefix (relative to modules/) to exclude; repeatable")
    ap.add_argument("--out-dir", default=".", help="where to write the artifacts")
    ap.add_argument("--force", action="store_true", help="overwrite existing artifacts")
    args = ap.parse_args()

    ignore = DEFAULT_IGNORE + args.ignore
    stock_root = pathlib.Path(args.stock_modules)
    patched_root = pathlib.Path(args.patched_modules)
    out_dir = pathlib.Path(args.out_dir)
    if not stock_root.is_dir() or not patched_root.is_dir():
        return fail("both --stock-modules and --patched-modules must be directories")

    stock = walk(stock_root, ignore)
    patched = walk(patched_root, ignore)

    overlay = []
    added, modified = [], []
    for rel, path in sorted(patched.items()):
        other = stock.get(rel)
        if other is None:
            added.append(rel)
        elif path.read_bytes() != other.read_bytes():
            modified.append(rel)
    overlay = sorted(added + modified)
    removed = sorted(set(stock) - set(patched))

    print("== JUCE 6.1.3 overlay ===")
    print("  patched tree files:  %d" % len(patched))
    print("  identical to stock:  %d" % (len(patched) - len(overlay)))
    print("  modified:            %d" % len(modified))
    print("  added:               %d" % len(added))
    print("  absent from patched: %d%s" % (len(removed),
                                           " " + ", ".join(removed) if removed else ""))

    out_dir.mkdir(parents=True, exist_ok=True)
    overlay_path = out_dir / OVERLAY_NAME
    manifest_path = out_dir / MANIFEST_NAME
    for path in (overlay_path, manifest_path):
        if path.exists() and not args.force:
            return fail("%s exists; pass --force to replace it deliberately" % path)

    # Deterministic tar.gz: fixed metadata, sorted members, no gzip timestamp.
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w") as tar:
        for rel in overlay:
            data = patched[rel].read_bytes()
            info = tarfile.TarInfo(name="modules/" + rel)
            info.size = len(data)
            info.mtime = FIXED_MTIME
            info.mode = 0o644
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            info.type = tarfile.REGTYPE
            tar.addfile(info, io.BytesIO(data))
    with open(overlay_path, "wb") as f:
        with gzip.GzipFile(fileobj=f, mode="wb", mtime=0) as gz:
            gz.write(raw.getvalue())

    lines = ["%s  modules/%s" % (sha256_file(patched[rel]), rel) for rel in sorted(patched)]
    manifest_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    print("  overlay:  %s (%d files, %d bytes, sha256 %s)"
          % (overlay_path.name, len(overlay), overlay_path.stat().st_size,
             sha256_file(overlay_path)))
    print("  manifest: %s (%d entries)" % (manifest_path.name, len(lines)))
    print("Verify with: python3 verify_juce_tree.py --provision --dest <dir>")
    return 0


if __name__ == "__main__":
    sys.exit(main())
