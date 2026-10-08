# SideChainer 0.4.0 — Windows x64 VST3 Build Handoff

**Status:** build not performed; no usable Windows machine, VM, or Windows toolchain is available in the audited environment (2026-10-02). This is a build-and-test handoff, not evidence of Windows support.

## Source of truth and frozen product contract

Use the current SideChainer project source at this revision, without importing older detector code or changing DSP/UI:

- Project: `SideChain.jucer` (Projucer-managed; there is no authoritative CMake project)
- JUCE modules: JUCE **6.1.3**, currently referenced via `../../HISE/JUCE/modules`
- Project version: **0.4.0**
- Plugin/project name: **SideChain** (the stable technical bundle/executable identity)
- User-facing editor/product brand: **SideChainer**
- Manufacturer: **Music-Prod**; VST3 manufacturer code `Musc`
- VST3 plugin code: `SdCh`
- Bundle/product identifier: `com.musicprod.sidechain`
- Auth technical product ID: `sidechain`
- Current source set is the complete `Source/` directory, Projucer-listed support files, `Resources/music-prod-logo.png`, `JuceLibraryCode/`, and the same JUCE 6.1.3 tree used for the validated macOS project. Preserve the current one stereo main input / one stereo output and internally scheduled quarter-note ducks; **no auxiliary sidechain input**.

The current SideChain `.jucer` has only an Xcode/macOS exporter. Windows was not added to its configuration; it was not demonstrated as intentionally disabled. Do not save a Windows exporter into the shared `.jucer` as part of this handoff. Generate it from an isolated working copy instead.

## Required Windows x64 environment

- Windows 10/11 x64 machine or VM.
- **Visual Studio 2022** with the *Desktop development with C++* workload and a current MSVC v143 x64/x86 build tools component; Windows 10/11 SDK; MSBuild.
- **Projucer 6.1.4** can open/generate the project format used here. The linked JUCE source/modules must remain exactly **JUCE 6.1.3** (the shipped Projucer is 6.1.4, so do not re-save the source project with a newer JUCE modules checkout or let the generated project silently use mismatched modules).
- JUCE's **bundled VST3 SDK** is already at `JUCE/modules/juce_audio_processors/format_types/VST3_SDK`; use it. No separate Steinberg SDK/runtime is expected for this JUCE project. Do not add external libraries or another plugin framework.
- The source requires C++17 and the JUCE modules already declared in the project. Enable Windows audio backends only if needed for the host/build; no new DSP dependencies are expected. Windows signing/notarization is outside this Mac release scope.

## Isolated project setup (do not edit the original)

1. Copy the SideChainer project into a disposable Windows working directory, including `SideChain.jucer`, `Source/`, `Resources/`, and `JuceLibraryCode/`. Also provide the exact JUCE 6.1.3 modules tree at a stable path. Keep the clean source revision separately so generated-file edits can be discarded safely.
2. Open the copy in Projucer 6.1.4. Add a **Visual Studio 2022** exporter targeting `Builds/VisualStudio2022`.
3. In the **copy only**, configure the format list to VST3 only (`buildVST3=1`; disable AU, AUv3, legacy VST, Standalone, RTAS, AAX, Unity and all other formats). Keep MIDI input/output and MIDI-effect options off. Keep the existing `pluginChannelConfigs` / processor source behavior (stereo main in/out only). Do not change plugin identity, source, resource content, or macOS exporter settings.
4. Set JUCE module search paths to the supplied JUCE 6.1.3 `modules` directory. The current relative path `../../HISE/JUCE/modules` is machine-layout-specific; resolve it in the working copy to the actual JUCE 6.1.3 directory.
5. Select **x64** and **Release** in the generated solution; do not build Win32, Debug, or other plugin formats.
6. In Projucer, set the VST3 binary copy/install target to a temporary staging directory for first validation (or leave binary copy off). Do not build/install into a user's system plugin folder until the bundle passes the pre-install checks.

## Build commands and expected output

From a Visual Studio 2022 Developer Command Prompt (x64), in the isolated project copy:

```bat
msbuild Builds\\VisualStudio2022\\SideChain.sln /m /p:Configuration=Release /p:Platform=x64
```

Use the actual generated solution filename if Projucer gives it a different project-name suffix. Expected intermediate production bundle is normally:

```text
Builds\\VisualStudio2022\\x64\\Release\\VST3\\SideChain.vst3
```

Projucer's MSVC exporter defaults its VST3 copy folder to `$(OutDir)\\VST3`; it can be configured. Find the one just-built `SideChain.vst3` under the generated VS2022 Release/x64 output and record the full path. Never take a MacOSX `Release` bundle, Debug bundle, diagnostic, old version, or test harness as the artifact.

For the release zip, stage exactly one top-level `SideChain.vst3` bundle into:

```text
SideChainer-0.4.0-Windows-x64-VST3/SideChain.vst3/...
```

Then create `SideChainer-0.4.0-Windows-x64-VST3.zip` from that folder only after Windows host verification; exclude source, `.pdb`, generated solution/intermediates, tests, and all macOS artifacts. Record size and SHA-256 from the final ZIP bytes.

## Before installation — inspect actual output

- Verify the bundle is a VST3 directory (contains `Contents/Resources/moduleinfo.json` and the Windows VST3 module under `Contents/x86_64-win/` in the exact SDK/bundle layout generated by this JUCE 6.1.3 exporter).
- Inspect the module's PE headers with Visual Studio `dumpbin /headers` or `llvm-readobj --file-headers`; require **machine x64 / AMD64**, not ARM64 or x86.
- Inspect `moduleinfo.json` and/or host Plug-in Manager metadata: category Fx, vendor Music-Prod, plugin name/description SideChainer (if the host exposes the editor name) and stable technical component/plugin identity corresponding to the JUCE codes `Musc`/`SdCh`. Check the actual generated VST3 identity using the VST3 validator/host; do not infer correctness only from the filename.
- Confirm the only produced installable format is VST3. Confirm user-facing metadata/editor says **SideChainer**, not an unintended SideChain/SideChainer mismatch. Confirm reported version 0.4.0. Release builds should be optimized with `NDEBUG`; no diagnostic/test macro or Phase1 target.
- Load the component using the official validator included with Steinberg's VST3 SDK if available for the selected environment, or a VST3-capable host's plug-in scan/manager. A binary inspection alone is not a host pass.

## Install and host test (required before claiming Windows support)

Once the bundle is accepted, install only `SideChain.vst3` to:

```text
C:\\Program Files\\Common Files\\VST3\\SideChain.vst3
```

If elevated installation is not available, install to the host-supported per-user VST3 folder and record the actual path. Preserve/rename nothing already there: if this exact SideChain bundle already exists, inspect its version and get approval before replacing it. Rescan/restart the host.

In a current REAPER or Ableton Live VST3 host, create a disposable project with **one audio track**, a sustained pad/tone/audio clip, and SideChainer inserted directly on that track. Do not create a kick track or external sidechain routing. Save only the disposable test project. Check:

- Host discovers and loads the 64-bit VST3 without a crash; UI renders at normal Windows scaling, including SideChainer and Music-Prod marks, controls, arrows, graph and text without clipping/overflow or unexpected background/font rendering.
- Default graph view is SIDECHAIN; ANALYZER is switchable. The beat preview is visible while stopped. STOP does not schedule new ducks; PLAY/RECORD resumes scheduling, and transport restart re-anchors.
- With only the sustained audio track (no trigger track and no external routing), playback audibly pumps **once per quarter-note beat**.
- Amount changes duck depth; DUCK LENGTH changes total audible duration; Release/Shape changes the envelope plateau/tail; Offset shifts duck timing; preset selection and previous/next arrows work.
- Observe SIDECHAIN graph for at least Micro Kick (shortest), one short, one medium, one long and Full Beat (longest); show visibly distinct envelopes and hear their duration differences.
- Analyzer, preset selector, Amount, Duck Length, Release, offset arrows, state save/reopen and auth UI behavior are checked; test auth safely without using real user credentials. Record DAW and exact version, sample rate, buffer/block size, OS, observed UI differences, crashes/dropouts, and CPU meter. CPU should be measured in the actual host with the test session playing; do not substitute a macOS microbenchmark.

## Existing local automated coverage (Mac-only harness today)

- Existing source regression baseline: `Tests/run_all.sh` — seven current suites, **227/227** was the latest recorded forced rebuild in the manual ledger; a fresh run in the audited workspace passes **227/227**. The shell runner itself hard-codes macOS frameworks/absolute JUCE path, so it is not a Windows test command as-is.
- Offscreen UI harness: `Tests/UIVerifyHarness` — **43/43** on macOS only; it checks logos/knobs/arrow/graph/layout and mock auth.
- `auval` is AU-only; VST3 bus check binary in `HostCheck/` is a macOS `dlopen`/Mach-O check. Neither validates a Windows VST3.
- Port the pure C++ DSP/scheduler tests to a Visual Studio test project only if needed to validate the same code; do not weaken/remove the existing suites or introduce a parallel production build architecture solely for testing. The decisive acceptance still includes a real Windows VST3 host run.

## Evidence template

Record with the resulting artifact:

```text
Source revision / archive hash:
Windows version + architecture:
JUCE modules version/path: 6.1.3 / ...
Projucer version: 6.1.4
Visual Studio/MSVC version:
Solution / Configuration / Platform: Release / x64
Built bundle path:
PE machine:
VST3 component identity + version:
Host + version:
Installed path:
UI/DSP/preset/transport checklist:
Host CPU, sample rate, block size:
ZIP name / byte size / SHA-256:
```

Until every build, install, metadata, and real-host check is recorded against the final ZIP, Windows must remain **NOT AVAILABLE / NOT TESTED** in the product catalog and website.