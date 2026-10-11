# PumpCurve 0.4.1 — Windows x64 VST3 build, installer and test pipeline

This directory is the Windows half of the release: the reproducible build inputs,
the build driver, the Inno Setup installer recipe, the install/uninstall test, and
the GitHub Actions workflow that runs all of it on a real Windows machine.

**There is no Windows machine in this project.** The pipeline runs on
GitHub-hosted `windows-2022` runners, which is the only way a Windows binary gets
produced here. Everything in this directory is checked locally by
`test_windows_tooling.sh`, which runs on macOS; only the workflow produces the
binary, the installer and the install-test evidence.

| | |
|---|---|
| Product | `PumpCurve` 0.4.1 (`Music-Prod`) |
| Format / architecture | VST3, x64 (PE32+) |
| Kept technical identity | bundle id `com.musicprod.sidechain`, `pluginCode=SdCh`, manufacturer code `Musc` |
| Customer-visible bundle | `PumpCurve.vst3` (module `Contents/x86_64-win/PumpCurve.vst3`) |
| Toolchain | MSVC v143 via Visual Studio 2022, MSBuild, Projucer 6.1.3, Python 3, Inno Setup 6 |
| Built by | [`.github/workflows/windows-vst3.yml`](../../../.github/workflows/windows-vst3.yml) |
| Code signing | **none** — no Authenticode certificate exists, so the installer is unsigned |

## 0. What a real MSVC build actually produces

Two facts only a real Windows build reveals. Both are handled by the tooling, both
are covered by tests (`stage_windows_vst3.py --self-test`,
`build_windows_vst3.py --self-test`), and both were found by running the pipeline
rather than by reading documentation.

* **The exporter links a flat module, not a bundle.** JUCE 6.1.3's Visual Studio
exporter emits a single PE DLL called `SideChain.vst3` in
`Builds/VisualStudio2022/x64/Release/VST3/`; unlike the Xcode exporter it assembles
no `Contents/` hierarchy. `stage_windows_vst3.py` accepts either shape and always
writes the canonical bundle `PumpCurve.vst3/Contents/x86_64-win/PumpCurve.vst3`,
copying the module verbatim and re-hashing it, so the shipped bytes are provably the
compiled bytes.
* **The version resource names the project, not the product.** JUCE fills
`ProductName` and `FileDescription` from the *project* name
(`juce_ResourceRc.cpp`), and this project is `SideChain` - Windows file properties
and host plug-in information would show "SideChain" even though the VST3 class name
is `PumpCurve`. The driver rewrites those two values in the generated
`resources.rc` after the resave and before MSBuild, preserves the original as
`resources.rc.pre-pumpcurve`, and records every before/after hash in
`RUN-MANIFEST.json`. `stage_windows_vst3.py --inspect-module` fails when a legacy
name survives in the version resource, and only reports it when it appears as a
plain string (compiled-in source paths legitimately carry the project name).

## 1. Why the JUCE tree is pinned twice

The validated macOS build compiles against a *locally patched* JUCE 6.1.3 tree
(`SIDECHAIN_JUCE_ROOT`). A Windows build on stock JUCE would not be the same
product, so the delta is recorded and re-applied automatically:

* `juce/JUCE-6.1.3-patched-overlay.tar.gz` — every file that differs from the
  official JUCE 6.1.3 release tarball (351 files, byte-exact, deterministic
  archive).
* `juce/JUCE-6.1.3-patched-modules.sha256` — SHA-256 of all 1798 files of the
  patched `modules/` tree, i.e. the contract the verification enforces.
* `juce/verify_juce_tree.py` — downloads the official tarball (pinned SHA-256
  `c3ab9188…e30d6`), applies the overlay, and fails on a single changed byte.
  `--extract-stock` gives the unmodified tree that Projucer is built from.
* `juce/regenerate_juce_overlay.py` — regenerates both artifacts from a stock and
  a patched tree, so the overlay can be rebuilt and reviewed instead of trusted.

The overlays have a stated exclusion: `juce_audio_devices/native/oboe/**` (Android
only, never compiled here, and it contains a `README.md`/`readme.md` pair that a
case-insensitive file system cannot represent faithfully).

## 2. The pipeline

`.github/workflows/windows-vst3.yml` runs on `windows-2022` and:

1. runs the local self-tests (`stage_windows_vst3.py --self-test`,
   `build_windows_vst3.py --self-test`, `build_windows_vst3.py --check`),
2. provisions the patched JUCE tree (verified against the manifest) and the stock
   tree that Projucer is built from,
3. builds Projucer 6.1.3 with MSBuild,
4. exports the Visual Studio 2022 solution from `SideChain.jucer` (toolset v143)
   and builds the VST3 x64 Release,
5. inspects the built module (PE x64, product name present, no legacy product
   name) and stages it as `PumpCurve.vst3`, byte-identical, then packages
   `PumpCurve-0.4.1-Windows-x64.zip` and `RUN-MANIFEST.json` with every hash,
6. installs Inno Setup 6 and compiles
   `PumpCurve-0.4.1-Windows-x64-Setup.exe`,
7. **tests the installer**: silent install → installed module hash equals the
   built artifact → our own legacy `SideChain.vst3` is cleaned up → another
   vendor's plug-in in the same folder survives → silent uninstall → the foreign
   plug-in is still there,
8. runs the installed-state verifier as a positive *and* negative control,
9. records the signature state, writes `RESULTS.md`, and publishes the artifacts,
   logs and hashes to a per-run branch
   `windows-build-results/<sha7>-r<run number>` (fetchable with plain `git
   fetch`, no extra token) plus a workflow artifact.

Nothing in the pipeline publishes a customer download. Distribution happens
through music-prod.com once the upload credential and the DAW acceptance tests
exist.

## 3. Building by hand on a Windows machine

```cmd
python tools\build_windows_vst3.py --check
python juce\verify_juce_tree.py --provision --dest C:\juce-patched
python juce\verify_juce_tree.py --extract-stock --dest C:\juce-stock
python tools\build_windows_vst3.py --build-projucer --juce-root C:\juce-stock
python tools\build_windows_vst3.py --build --juce-root C:\juce-patched ^
       --projucer C:\juce-stock\extras\Projucer\Builds\VisualStudio2022\x64\Release\Projucer.exe
installer\build_installer.cmd 0.4.1 artifacts\PumpCurve.vst3
```

Run the build from a **Developer Command Prompt for VS 2022** so `msbuild` is on
`PATH`.

## 4. Verifying an installation

```powershell
powershell -ExecutionPolicy Bypass -File installer\verify_install.ps1 `
    -Sha256 <module sha256 from RUN-MANIFEST.json>
powershell -ExecutionPolicy Bypass -File installer\test_install_uninstall.ps1 `
    -Setup <PumpCurve-0.4.1-Windows-x64-Setup.exe> `
    -ExpectedModuleSha256 <sha256> `
    -StagedBundle <staged PumpCurve.vst3>
```

The installer never deletes anything it cannot identify as this product: a legacy
`SideChain.vst3` / `SideChainer.vst3` is removed only when its version resource
says `ProductName = PumpCurve` or `CompanyName = Music-Prod`; a 32-bit legacy
bundle is reported and left alone. Uninstalling removes only what the installer
wrote (plus the uninstaller in `…\Common Files\PumpCurve`, deliberately outside
the folder hosts scan).

## 5. Host acceptance (not done by the pipeline)

The runner has no DAW, so discovery/insertion is **NOT TESTED** there. On a real
Windows machine, with FL Studio and Ableton Live:

1. FL Studio: Options → Manage plug-ins → *Find installed plug-ins*, confirm the
   entry is **PumpCurve**; insert into a Mixer effect slot; editor opens; audio
   passes; tempo-synced ducking follows host tempo; Amount / TIME / SMOOTH /
   Release respond; graph edit; presets; transport start/stop/loop; save/reopen
   the project and confirm curve, parameters and preset persist.
2. Ableton Live: repeat on a track's device chain.
3. Record PASS / FAIL / NOT TESTED per step — appearing in a list is discovery,
   not insertion.

## 6. Honest status

* No Windows binary existed before this pipeline; the old `SideChainer-0.4.0`
  Windows binary is a different product and is never shipped as PumpCurve.
* The installer is **unsigned**: expect a SmartScreen warning until a certificate
  is supplied (`signtool` is not used anywhere in this pipeline).
* `test_windows_tooling.sh` (macOS) checks the inputs, the recipe and the
  workflow; it cannot replace a Windows run. Read the newest
  `windows-build-results/*` branch or the workflow run for the real evidence.
