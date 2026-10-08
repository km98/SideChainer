# SideChainer 0.4.0 — Release Status

**Re-audited:** 2026-10-02  
**Mac candidate decision:** **A. NOTARIZED + GATEKEEPER VERIFIED**  
**Publication decision:** **BLOCKED — NOT READY FOR PUBLIC PUBLISH**  
**Scope:** 0.4.0 macOS distribution signing/notarization plus existing release gates. No DSP/UI/auth source, website, Studio, or production service was changed.

A separate universal AU/VST3 candidate was Developer ID signed, accepted by Apple notarization, stapled, and passed Gatekeeper's install/distribution assessment after extracting a quarantine-marked copy of the final ZIP. The original unsigned ZIP and build inputs were preserved. This is the required **A** artifact gate, not a claim that installation, Logic/Ableton acceptance, upload, or publication is complete. Windows remains **NOT AVAILABLE / NOT TESTED**.

## Release matrix

| Gate | Status | Evidence / outstanding work |
|---|---|---|
| macOS production AU | **SIGNED + NOTARIZED; INSTALLED** | 0.4.0; bundle ID `com.musicprod.sidechain`; universal `x86_64 arm64`; strict Developer ID signature, ticket stapled, Gatekeeper PASS; installed AU enumeration + `auval` PASS; headless host probe 4/4. |
| macOS production VST3 | **SIGNED + NOTARIZED; INSTALLED** | 0.4.0; bundle ID `com.musicprod.sidechain`; universal `x86_64 arm64`; strict Developer ID signature, ticket stapled, Gatekeeper PASS; installed candidate factory/bus check passes. |
| Product/version | **PASS** | JUCE 6.1.3 Projucer project, version 0.4.0. Existing macOS exporter/build unchanged. Project directory is not a Git worktree; branch/status are N/A. |
| Developer ID certificate | **PASS** | `Developer ID Application: Martin Kadziolka (3A4R5EKM7V)`; Team ID `3A4R5EKM7V`; valid 2026-09-13 through 2031-09-14; SHA-256 fingerprint `0E0E33392E7F8034C94707814ED182EDC49014DC76CD971DB3897639F44715B8`. Apple Development identity was not used. |
| Notary credentials | **PASS** | `SideChainerNotary` Keychain profile validated successfully with `notarytool history`; no secret was exposed or passed on the command line. |
| AU/VST3 signatures | **PASS** | Signed candidate bundles pass `codesign --verify --strict --deep`; Team ID and Developer ID chain confirmed; Hardened Runtime and secure timestamp present; no entitlements added. |
| Notarization / staple | **PASS** | Final submission `c3b77e78-9dd2-4a11-8777-b77ba83c4d20` **Accepted**; both AU and VST3 stapled and `stapler validate` passed. The final ZIP digest equals the submitted ZIP digest. |
| Gatekeeper | **PASS for plugin payloads** | On extracted bundles marked with `com.apple.quarantine`, `spctl --assess --type install` returns `accepted / source=Notarized Developer ID`; `syspolicy_check distribution` passes for AU and VST3. `spctl --type execute` rejects plugin bundles as “does not seem to be an app”; `spctl --type install` is not applicable to the ZIP container itself (`source=no usable signature`); Gatekeeper accepts the extracted, stapled Developer ID plugin bundles. The plugin bundles—not the ZIP itself—are the assessed code objects. |
| Local release ZIP | **PASS; signed/notarized candidate created separately** | Final candidate ZIP contains AU, VST3, release notes, and normal signature/staple metadata only; no source/tests/intermediates. Original unsigned ZIP remains unchanged. Exact identity below. |
| Private release storage | **PENDING** | No artifact uploaded; object existence, size/hash/key and access policy have not been verified. |
| Release/catalog migrations | **PREPARED; UNAPPLIED** | SideChainer version and DRAFT catalog migrations exist in the dirty website checkout. Live schema, release bucket, conflicting rows and object were not verified. Do not apply copied/unrelated migrations. |
| Live Studio update endpoint | **PASS: no published release** | Live `/updates?product=sidechain&current=0.4.0&platform=macos` returns `latestVersion: null`, no artifacts, `status: noPublishedRelease`. This is honest but does not establish a live SideChainer catalog entry. |
| Studio product/install integration | **IMPLEMENTED; local tests pass** | Technical key `sidechain`, display name SideChainer, AU/VST3 identity detector. Live catalog item is absent; plugin installer remains disabled. |
| Music-Prod.com source page | **IMPLEMENTED locally; READY FOR REVIEW** | SideChainer page/assets/download/migrations are present in the website checkout; this task made no changes there. |
| Music-Prod.com live page/download | **BLOCKED / NOT LIVE** | `/sidechainer` renders “Link Not Found”; the intended `.zip` URL returns website HTML/404 instead of a ZIP. Responsive live-page checks cannot pass until deployed. |
| Mac installation | **PASS — user-level Music-Prod convention** | Installed the exact signed candidate at `~/Library/Audio/Plug-Ins/Components/SideChain.component` and `~/Library/Audio/Plug-Ins/VST3/SideChain.vst3`; installed executable hashes match the release candidate. AU registration and `auval` pass; system-level `/Library/.../SideChainer.*` paths are not the established identity/path and remain unused. Original unsigned bundles plus `.pre-recovery` copies were hash-verified and preserved outside plugin scan directories. |
| Logic Pro | **NOT TESTED — real editor/audio acceptance requires user-run GUI verification** | An earlier test attempt created a fresh unsaved `Untitled 3` project without opening an existing project. The test flow created an External Instrument track; whether it was removed is unverified. A background AXPress in that context opened a generic `Inst 1` window, which does not establish that SideChainer was inserted or loaded. No SideChainer editor rendering or Logic audio result is claimed. Under the current no-autonomous-GUI rule, no further DAW actions will be taken. The latest read-only process check found Logic Pro still running (PID 78481); its current save/close and track state are unverified. |
| Auth/product isolation | **IMPLEMENTED; focused tests pass** | SideChainer ID `sidechain`; VYRE ID `vyre`. No auth changes made. |
| Windows VST3 | **NOT AVAILABLE / NOT TESTED** | No Windows machine/VM/toolchain/host or artifact. Detailed handoff: [WINDOWS_BUILD_HANDOFF_0.4.0.md](WINDOWS_BUILD_HANDOFF_0.4.0.md). |
| Ableton Live | **NOT TESTED** | The signed VST3 candidate is installed at the established user-level `~/Library/Audio/Plug-Ins/VST3/SideChain.vst3` path, with its executable hash verified. Ableton Live was not launched and the plugin was not loaded or tested in the host. |
| Upload / publish | **NO / NO** | No storage upload, public deployment, catalog application, release publication, commit, or push occurred. |

## Exact local Mac artifact fingerprints

These hashes identify the currently available **unsigned** bytes only; they must not be reused as the digest of a later signed/notarized package.

| Artifact | Version / architecture | Size | SHA-256 |
|---|---|---:|---|
| `SideChainer-0.4.0-macOS.zip` | AU + VST3, universal | 3,757,207 bytes | `8fc28fe46bb1f99f7c696ac5bab4ad0dbe177e263a58c6e6a04ccfee1817fd3b` |
| `SideChain.component` executable | AU, universal | 4,888,552 bytes | `6c4e8ad40043d3cc5cf28bebd02fe702e4114bbc11e883488ef96e576c0c4570` |
| `SideChain.vst3` executable | VST3, universal | 5,010,472 bytes | `3487813062eaf3960060edc3bdf109242109dd9f00249ea10cc36e647c16dd22` |

Both bundles have `CFBundleName`/executable `SideChain`, identifier `com.musicprod.sidechain`, and version 0.4.0. The editor/product brand is **SideChainer**; VST3 technical/plugin naming remains SideChain to preserve the validated identity and compatibility. The release archive contains only `SideChain.component`, `SideChain.vst3`, and `RELEASE_NOTES.txt` under `SideChainer-0.4.0-macOS/`.

The website checkout's local ZIP copy is byte-identical to the **unsigned** project ZIP; the production URL does **not** serve those bytes. No website/Studio changes were made.

## Signing, notarization, and final candidate evidence (2026-10-02)

- Build input: existing Release outputs; JUCE 6.1.3 / Xcode 26.3, `SideChain.jucer` 0.4.0, Release `optimisation=3`, universal `x86_64 arm64`, deployment target macOS 11.0. No rebuild or DSP/UI/auth source edits.
- Identity: `Developer ID Application: Martin Kadziolka (3A4R5EKM7V)`; Team `3A4R5EKM7V`; identity fingerprint `FE494D252A0D9723DE0778B6FAED678DF5BFAA1C`; certificate SHA-256 fingerprint `0E0E33392E7F8034C94707814ED182EDC49014DC76CD971DB3897639F44715B8`; Developer ID Certification Authority → Apple Root CA; valid to 2031-09-14.
- Signature options: `--options runtime --timestamp`; both candidate bundles pass `codesign --verify --strict --deep`; identity, Team ID, timestamp, and runtime flag verified. No entitlements were added; bundle resources sealed. No nested Mach-O code beyond each signed universal executable.
- Bundle metadata: both remain `com.musicprod.sidechain`, version `0.4.0`, technical name/executable `SideChain`; architectures `x86_64 arm64`.
- Container convention: ZIP, matching the existing Music-Prod plugin distribution. The AU/VST3 bundles were stapled **before** final ZIP creation; ZIP itself is not a staplable executable. Accepted Apple tickets are online and stapled to each bundle.
- Final notary submission: `c3b77e78-9dd2-4a11-8777-b77ba83c4d20`, status **Accepted** (queried with `notarytool info`). This is the exact final archive; the submitted ZIP and final distribution ZIP are byte-identical at 3,806,593 bytes and SHA-256 `10a4b89ecb8c0db66af77aa2851cf0affdf9ab764f4adf2334c0b38364904317`.

| Required release identity | Final result |
|---|---|
| Signing | **PASS** — Developer ID Application, Hardened Runtime, secure timestamp; no added entitlements |
| Team | `3A4R5EKM7V` |
| AU signature / staple | **PASS / PASS** — strict signature and stapled ticket validate |
| VST3 signature / staple | **PASS / PASS** — strict signature and stapled ticket validate |
| Notarization | **PASS** — submission `c3b77e78-9dd2-4a11-8777-b77ba83c4d20`, **Accepted** |
| Gatekeeper | **PASS for AU/VST3 payloads extracted from the quarantined final ZIP**; ZIP itself is not a code object |
| Final artifact | [SideChainer-0.4.0-macOS-SIGNED-NOTARIZED.zip](../Builds/MacOSX/build/release-assets/SideChainer-0.4.0-macOS-SIGNED-NOTARIZED.zip) — 3,806,593 bytes; SHA-256 `10a4b89ecb8c0db66af77aa2851cf0affdf9ab764f4adf2334c0b38364904317` |
| AU executable SHA-256 | `cb5ea146299342ee9d7dab617f7e2b2e20bd004909e9f0c4137f885c96468135` |
| VST3 executable SHA-256 | `d54c3077d95eb2e75251fc40ed6d509299fb56910e31435589f079586e3c90a0` |
| Logic / Ableton | **NOT TESTED / NOT TESTED** |
| Upload / publication | **NOT YET / NOT YET** |
- `stapler validate`: PASS for AU and VST3. `spctl --assess --type install`: PASS, `source=Notarized Developer ID`; `syspolicy_check distribution`: PASS for both. Repeated after setting quarantine on the final ZIP and extracting it; the extracted AU/VST3 bundles preserved quarantine and were accepted by Gatekeeper. Test copies live only in the candidate scratch folder and are not package payload.
- Final AU executable: 4,925,920 bytes; SHA-256 `cb5ea146299342ee9d7dab617f7e2b2e20bd004909e9f0c4137f885c96468135`.
- Final VST3 executable: 5,031,104 bytes; SHA-256 `d54c3077d95eb2e75251fc40ed6d509299fb56910e31435589f079586e3c90a0`.
- Candidate folder: `Builds/MacOSX/build/release-assets/SideChainer-0.4.0-macOS-SIGNED-CANDIDATE/`. Final archive: `Builds/MacOSX/build/release-assets/SideChainer-0.4.0-macOS-SIGNED-NOTARIZED.zip`. Exact notarized submission copy retained as `SideChainer-0.4.0-macOS-SIGNED-NOTARIZED-SUBMITTED.zip` and `SideChainer-0.4.0-macOS-SIGNED-NOTARY-SUBMISSION.zip` (same size and digest). The original unsigned ZIP still hashes to `8fc28fe46bb1f99f7c696ac5bab4ad0dbe177e263a58c6e6a04ccfee1817fd3b`.

## Installation verification (2026-10-02)

- Before install, captured and preserved all installed user AU/VST3 bundles, including rollback copies, under `Builds/MacOSX/build/release-assets/SideChainer-0.4.0-PRE-SIGNED-INSTALL-BACKUP-20261002/`. Copies match their originals byte-for-byte by executable SHA-256: AU `6c4e8ad40043d3cc5cf28bebd02fe702e4114bbc11e883488ef96e576c0c4570`; AU `.pre-recovery` `7d9a582c0af396dc922da81c29887b984e1788adcb56f3d40f56a63c8b55fccf`; VST3 `3487813062eaf3960060edc3bdf109242109dd9f00249ea10cc36e647c16dd22`; VST3 `.pre-recovery` `d74d6ba4028dfcd36f2e0817bb2a4244047a664c40449e81a731071e9970cb41`.
- Moved those exact originals/rollback copies out of plugin scan directories (not deleted), then installed only the signed candidate to the established per-user `SideChain.component` and `SideChain.vst3` paths. The distinct `IPlugSideChain` and `Phase1` test plugins were not changed.
- Installed AU SHA-256 `cb5ea146299342ee9d7dab617f7e2b2e20bd004909e9f0c4137f885c96468135`; installed VST3 SHA-256 `d54c3077d95eb2e75251fc40ed6d509299fb56910e31435589f079586e3c90a0`. Both retain bundle ID `com.musicprod.sidechain`, version 0.4.0, universal `x86_64 arm64`, Developer ID Team `3A4R5EKM7V`, valid strict signature and stapled ticket; installed Gatekeeper install-type assessment says `accepted / source=Notarized Developer ID`.
- Post-install AU enumeration reports one `aufx SdCh Musc` at the installed path; `auval -v aufx SdCh Musc` passes (0.4.0, Cocoa view available, latency property passes). VST3 factory/bus check passes on the installed executable (one stereo input/output). The installed signed AU loads in the headless host probe and passes 4/4 internal ducking, amount, sample/block, and state save/reload scenarios. These are installation/registration/headless checks, not real DAW UI evidence.
- Earlier Logic work used only the fresh unsaved `Untitled 3` test project; no existing or production project was opened. A track-creation attempt created an External Instrument track, and an AXPress in that context opened a generic `Inst 1` window. This is not evidence that SideChainer was inserted. The track's removal/current existence and the project's current state are unverified; no claim of zero tracks is made. The latest subsequent read-only process check found Logic Pro running (PID 78481). Under the current no-autonomous-GUI rule, no further Logic interaction will be performed. No LetItReign or other user project was touched.

## Remaining gates / next safe action

The artifact classification is **A. NOTARIZED + GATEKEEPER VERIFIED**. Installation passes at the established user-level `SideChain.component` / `SideChain.vst3` paths; installed hashes match and old/rollback copies remain preserved outside plugin scan locations. Earlier Logic work used only a fresh unsaved `Untitled 3`; no LetItReign or production project was opened. A generic `Inst 1` window in an External Instrument context was not a proven SideChainer instance. The test project's current track/save state is unverified, and Logic was still running at the latest read-only process check. Real Logic editor/audio acceptance remains NOT TESTED.

Before publication: real Logic and Ableton host acceptance still needs user-performed testing in disposable sessions, because the current policy forbids autonomous GUI interaction. Do not use synthetic keyboard/mouse events as a workaround. Only after those gates should the exact ZIP be uploaded and the separate, still-unpublished release/catalog/site workflow be considered. Windows remains out of scope and unchanged.

## Validation run in this audit

- **SideChainer core regressions:** `Tests/run_all.sh` — 7 suites, **227/227 pass**; includes state serialization/restoration and parameter/state hardening. `auval`'s latency property check passes; fixed lookahead sizes (1440 at 48 kHz, 1323 at 44.1 kHz) are recorded in the existing test/manual evidence. The retired `TriggerDSPTests` binary is not included in the current 0.4.0 runner.
- **SideChainer editor harness:** **43/43 pass** (offscreen rendering/layout/logo/controls/graph preview/auth mock); not real DAW UI evidence.
- **VST3 topology:** **0 failures on the signed candidate**, one stereo main input and one stereo output; no external sidechain bus.
- **AU validation:** `auval -v aufx SdCh Musc` — **AU VALIDATION SUCCEEDED**, version 0.4.0 and latency property PASS. The installed AU executable was separately verified to match the signed candidate SHA-256. The final candidate AU/VST3 bundles were rechecked in this audit: strict code-signature verification, stapled-ticket validation, Gatekeeper install assessment (`accepted / source=Notarized Developer ID`), and universal `x86_64 arm64` architecture all pass.
- **Signed AU host probe:** candidate loaded directly from the signed candidate path; **4/4** pass for beat-triggered ducking, Amount, 44.1/48 kHz block settings, and state save/reload. Legacy executable summary text says “REPRO FAILED (plugin works)”; checks themselves all pass. It is headless evidence, not Logic acceptance.
- **CPU probe:** **0.016%** of one core in synthetic 10 s, 48 kHz / block 512 optimized test. This is not a DAW CPU measurement.
- **Signed candidate architecture:** `x86_64 arm64` in both AU and VST3 after signing/stapling; exact executable hashes are recorded above.
- **Website:** focused SideChainer/link tests 12/12; full suite 647/647. Full lint remains red (585 errors/69 warnings across repo); targeted lint finds two existing `no-explicit-any` errors in `useSidechainVersions.ts`. TypeScript app check reports three unrelated VYRE errors. No files in this dirty website checkout were modified.
- **Studio:** SideChainer catalog tests 2/2; full suite 1,850 executed, 31 skipped, 0 failures.
- **Backend:** `npm run test:ts` — build succeeds; 90 tests pass, including release schema/service/API contract tests. These do not prove production schema or uploaded object state.
- **Install from final ZIP / AU/VST3 registration:** **PASS** at the established per-user paths; exact expected installed hashes match, AU `auval` passes, and VST3 topology passes. Development installs were preserved outside plugin scan directories.
- **Logic custom editor/audio and Ableton Live VST3:** **NOT TESTED**. Earlier Logic work was confined to a fresh unsaved `Untitled 3`; no user project was touched. The External Instrument context and generic `Inst 1` window do not prove SideChainer insertion, and the test project's current track/save state is unverified. A later read-only process check found Logic Pro still running (PID 78481). Existing [MANUAL_VERIFICATION_RESULTS.md](MANUAL_VERIFICATION_RESULTS.md) has no conclusive 0.4.0 Logic editor/pumping pass. The headless host probe is not DAW acceptance evidence. No further GUI interaction is authorized or will be performed autonomously.

## Windows handoff

The existing [SideChain.jucer](../SideChain.jucer) is Projucer-managed, JUCE 6.1.3, version 0.4.0, Xcode/macOS exporter only. No CMake project or Visual Studio solution exists. Do not edit that project for this handoff. The separate handoff describes the isolated VS2022 x64 VST3-only config, bundled VST3 SDK, build/output expectations, identity checks, install path and real-host/manual validation checklist.

Windows status remains **NOT AVAILABLE / NOT TESTED** until an actual Windows x64 Release bundle has been built, installed and tested in a Windows VST3 host with no external kick/sidechain routing.

## Final disposition

**MACOS CANDIDATE:** **A. NOTARIZED + GATEKEEPER VERIFIED** — Developer ID AU/VST3 signed, Apple notary submission Accepted, tickets stapled/validated, quarantine-aware Gatekeeper install assessment and distribution checks pass. Final package size/hash are recorded above.  
**INSTALL:** **PASS** — exact signed AU/VST3 installed to the established user-level paths after byte-verifying and preserving prior dev/rollback copies.  
**LOGIC / ABLETON:** real DAW editor/audio tests still **NOT TESTED**. Earlier Logic work used only a fresh unsaved `Untitled 3`; no user project was opened. No SideChainer instance was proven loaded. The latest read-only check found the Logic process running; its current save/close and track state are unverified. Further host testing must be performed manually by the user under the current GUI policy.  
**WINDOWS:** no build, install, host, UI or pumping test. **NOT AVAILABLE / NOT TESTED.**  
**PRODUCTION:** no upload; no publication. Overall release is **not yet publish-ready** until install/host gates and later upload/catalog/site workflow are completed.