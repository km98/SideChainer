# PumpCurve 0.4.1 — Windows build handoff (status and route)

**There is no Windows PumpCurve binary.** No Windows build host was reachable from
this machine, and the previously built Windows artifact is an older **SideChainer**
build that must not be presented as PumpCurve. This document records the
reproducible route to produce a correct one, and exactly which parts of it have
been exercised.

The full working bundle — source extract, the patched JUCE 6.1.3 tree, build
driver, staging tool, installer recipe and the integrity manifest — is the transfer
archive:

```
Windows-Build-Transfer/PumpCurve-0.4.1-Windows-Build-Source/
Windows-Build-Transfer/PumpCurve-0.4.1-Windows-Build-Source-RUN12.zip   (9.3 MB,
  sha256 58c5c3726e7f151a225243d9f0b981530d092b425c8eee21ae70caefdec84c0f)
```

It is deliberately **not** committed: it is a 2374-file, ~37 MB transfer tree that
carries a complete JUCE checkout. Its `MANIFEST.sha256` lists the SHA-256 of every
file (verified: 2373/2373 entries match).

## Source revision

`888e1805825faebeef8279998858ac8b27a157dc` (`main`), version `0.4.1`, product name
`PumpCurve`, plugin code `SdCh`, company `Music-Prod`, bundle identifier
`com.musicprod.sidechain`. Same revision whose macOS side passes the full
`Tests/validate_all.sh` workflow, including the product-name staging stage
(`docs/PUMPCURVE-RELEASE-NAMING.md`).

## The route

From a *Developer Command Prompt for VS 2022* (Visual Studio 2022, MSVC v143,
Windows 10/11 SDK, Python 3.8+):

```cmd
python tools\build_windows_vst3.py --check
python tools\build_windows_vst3.py --projucer "C:\path\to\Projucer.exe"   REM 6.1.3 only
python tools\stage_windows_vst3.py --stage artifacts\PumpCurve-0.4.1-Windows-x64.vst3 --out-dir artifacts
python tools\stage_windows_vst3.py --package artifacts\PumpCurve.vst3 --out-dir artifacts
python tools\stage_windows_vst3.py --verify-zip artifacts\PumpCurve-0.4.1-Windows-x64.zip
```

Optional installer:

```cmd
installer\build_installer.cmd 0.4.1 artifacts\PumpCurve.vst3
```

The MSBuild target emits `SideChain.vst3`; the shipped name must be
`PumpCurve.vst3` with its module binary also named `PumpCurve.vst3`
(`<Name>.vst3/Contents/x86_64-win/<Name>.vst3`), because hosts that show the file
name — FL Studio's plugin database — would otherwise list the product as
"SideChain". The staging tool renames both and proves the rename changed no byte
(per-file hashes before and after; it refuses to continue otherwise).

## What was verified on the Mac side

| Item | Result |
|---|---|
| Handoff self-check (`tools\build_windows_vst3.py --check`) | **PASS** — jucer identity `PumpCurve`/`SdCh`/`Music-Prod`/`0.4.1`, all 12 required JUCE modules present, patched-tree markers present, and the staging/installer tooling present |
| Staging + packaging logic (`tools\stage_windows_vst3.py --self-test`) | **PASS** — on a fixture: bundle *and* module renamed, every byte preserved, second staging refused, deterministic ZIP with `PumpCurve.vst3/Contents/x86_64-win/PumpCurve.vst3` as the entry point, no legacy bundle inside, `SHA256SUMS` written |
| Both scripts syntax-checked (`python -m py_compile`) | **PASS** |
| Transfer archive | **PASS** — extracts to a tree byte-identical to the source tree (2374 files) |
| Integration manifest | **PASS** — 2373/2373 entries match |

## What is NOT verified (do not claim otherwise)

* **No Windows compilation has been attempted.** `--projucer` build mode is
  Windows-only and was never executed.
* **`installer\PumpCurve.iss` has never been compiled.** Inno Setup is Windows-only.
  The recipe installs only `PumpCurve.vst3` into `%CommonProgramFiles%\VST3` and
  removes a previously installed `SideChain.vst3` / `SideChainer.vst3` **only when
  that file's version resource identifies it as this product**; the deletion logic
  must be proven on Windows before publishing.
* **`installer\verify_install.ps1` has never been executed.**
* **No host test has run**: FL Studio and Ableton Live acceptance for the Windows
  VST3 remain NOT TESTED.
* **No Authenticode signing.** Whether the artifact can be signed depends on a
  certificate on the Windows machine; if none exists, report it as unsigned.

## Requirements to close this out

1. An authorized Windows 10/11 x64 machine with Visual Studio 2022 (C++ workload)
   and Projucer 6.1.3, plus Python 3.8+.
2. Run the route above and record: module hash before/after staging, `ProductName`,
   version, architecture, signature state (or unsigned), and the ZIP/installer hash.
3. Run the host acceptance checklist in the handoff README §6 and the installed-state
   verification (`installer\verify_install.ps1`).
4. Only then may the Windows download alias be published; the macOS side has its own
   gates (notarisation and the distribution upload credential).
