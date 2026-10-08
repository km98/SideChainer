# SideChainer — Migration Manifest (Mac → new Mac)

**Generated:** 2026-10-08 (inspection date) — migration preparation only; nothing was modified, committed, pushed, or published.
**Inspected host:** Martins-MacBook-Pro.local, user `martin`, macOS (Darwin).
**Scope of this document:** authoritative checklist for moving the SideChainer development project to a new development Mac so development, validation, Windows build coordination, and release work continue without losing local work.

---

## 1. Project identity

| Item | Value (as inspected) |
|---|---|
| Project path | `/Users/martin/Documents/SideChain/SideChain` (≈ 1.2 GB total) |
| Git repository | **NONE.** There is no `.git` at the project path or any ancestor (`git rev-parse --show-toplevel` → *not a git repository*), and no `.git`/`.gitmodules` anywhere inside the project (searched to depth 6). |
| Branch / status / submodules | Not applicable — not a Git worktree. No remotes, no history, no submodules. |
| Consequence | **Every file in this project is local-only and uncommitted.** There is no history, no diff baseline, and no remote backup anywhere for SideChainer. Loss risk = total if the disk copy is lost. |
| Project type | JUCE 6.1.3 Projucer project (`SideChain.jucer`, version **0.4.1**, bundle ID `com.musicprod.sidechain`), Xcode/macOS exporter only. No CMake. |
| Related dirs (outside project) | `/Users/martin/Documents/SideChain/Phase1-SidechainBusTest` (87 MB), `/Users/martin/Documents/SideChain/Windows-Build-Transfer` (≈ 11 MB zip + extracted), `/Users/martin/Documents/HISE` (167 MB — JUCE module dependency) |

## 2. Migration date

Inspection & manifest creation: **2026-10-08**. Actual transfer date: TBD (archive deliberately NOT created yet).

## 3. MUST transfer

### 3.1 The project itself (entire directory)

`/Users/martin/Documents/SideChain/SideChain` — copy whole. Critical contents:

- `SideChain.jucer` — project definition (v0.4.1; Xcode exporter; JUCE modules path `../../HISE/JUCE/modules` → resolves to `/Users/martin/Documents/HISE/JUCE/modules`).
- `Source/` — all production C++ (PluginProcessor/Editor, GraphComponent, DuckEngine, BeatScheduler, MusicProdAuth, InfoPage, PresetManager, TransportGate, Branding, …).
- `Resources/music-prod-logo.png`
- `JuceLibraryCode/` — generated JUCE wrapper sources + `AppConfig.h`, `JucePluginDefines.h`, `BinaryData.*` — **required** (test scripts and Xcode build compile these directly).
- `Builds/MacOSX/SideChain.xcodeproj/` — Xcode project (references JUCE via `$(SRCROOT)/../../../../HISE/JUCE/modules`).
- `Builds/MacOSX/Info-AU.plist`, `Info-VST3.plist`, `RecentFilesMenuTemplate.nib`
- `Builds/MacOSX/package_sidechainer_0.4.0.sh` — packaging script (0755-staging fix; input default = verified 0.4.0 PRODUCTION pkg with expected SHA-256).
- `Builds/MacOSX/publish_sidechainer_beta.sh` — beta publish script (Supabase upload; reads Keychain — see §6).
- `HostCheck/VST3BusCheck.cpp` (+ compiled `VST3BusCheck` binary — rebuildable)
- `Tests/` — **sources, scripts and documentation are required:** all `*.cpp` test suites, `run_all.sh`, `ui_verify.sh`, `run_hostprobe.sh`, `cpu_bench.sh`, `read_diag.sh`, and the 19 `*.md` release/deployment/verification documents (incl. `RELEASE_REPORT_0.4.0_DISTRIBUTION.md`, `RELEASE_WORKFLOW_0.4.0.md`, `WINDOWS_BUILD_HANDOFF_0.4.0.md`, `MANUAL_VERIFICATION_RESULTS.md`, `HOSTINGER_*`, `DEPLOYMENT_ORIGIN_TRACE_0.4.0.md`, …).
- `PHASE4_REPORT.md` … `PHASE9_REPORT.md` (root) — development history.
- `docs/SIDECHAINER-MIGRATION-MANIFEST.md` — this file.

### 3.2 Release evidence & artifacts

`Builds/MacOSX/build/release-assets/` — transfer the release evidence, excluding disposable scratch (see §4):

- **0.4.1 FINAL artifacts (canonical, keep byte-exact):**
  - `SideChainer-0.4.1-macOS-PRODUCTION-CANDIDATE-20261005/SideChainer-0.4.1-macOS-PRODUCTION-FINAL.pkg` — 3,817,687 B, SHA-256 `ee50a367d6d1d8113768abb0fb647c7dedf20402542e5b1c9ebd4db8400a2052`, Developer ID Installer signed, notarization Accepted, stapled.
  - `…/SideChainer-0.4.1-macOS-PRODUCTION-FINAL.dmg` — 4,482,690 B, SHA-256 `8545ea2e5302afc92807466e4b4bc33d9338dfa62d24894b80ab65361eca46f8`, `hdiutil verify` VALID, stapled.
- **Notarization receipts (JSON):** `final-pkg-notarization.json`, `final-dmg-notarization.json`, `pkg-notarization.json`, `dmg-notarization.json`, `auverified-pkg-notarization.json`, `dmg-notary-log.json`, `pkg-notary-log.json`.
- **Validation logs:** `final-regression-rerun.log`, `final-ui-verify-rerun.log`, `final-package-au-probe-rerun.log`, `final-package-vst3-bus-rerun.log`, `auval-0.4.1.log`, `xcodebuild*.log`, `final-package-bom.txt`, hostprobe/vst3-bus logs.
- `RELEASE_NOTES.txt`, `Music-Prod-Loop.mid` (test MIDI used in verification).
- 0.4.0 signed/notarized artifacts and candidate trees (historical release record): `SideChainer-0.4.0-macOS-PRODUCTION.pkg/.dmg`, `SideChainer-0.4.0-macOS-SIGNED-NOTARIZED*.zip`, `SideChainer-0.4.0-macOS-*CANDIDATE*/` trees, `SideChainer-0.4.0-PRE-SIGNED-INSTALL-BACKUP-20261002/`.

### 3.3 External directories that are part of this project's build

- **`/Users/martin/Documents/HISE` (167 MB)** — **hard build dependency.** `HISE/JUCE/modules` (36 MB) is the JUCE **6.1.3** module tree referenced by the `.jucer`, by `project.pbxproj` header search paths, and hard-coded as `/Users/martin/Documents/HISE/JUCE/modules` in `Tests/run_all.sh`, `Tests/ui_verify.sh`, `Tests/run_hostprobe.sh`. Includes local patch artifacts (`working608_patch.diff`, `JuceHeadlessPlugin.patch`, `custom_diffs/`). **Same absolute path must exist on the new Mac.**
- **`/Users/martin/Documents/SideChain/Windows-Build-Transfer/`** — Windows coordination package: `SideChainer-0.4.0-Windows-Build-Source.zip` (10.7 MB) + extracted tree (contains its own `HISE/JUCE` copy + frozen source snapshot). Working handoff contract lives in `Tests/WINDOWS_BUILD_HANDOFF_0.4.0.md`.

### 3.4 Recommended (low cost, historical evidence)

- `/Users/martin/Documents/SideChain/Phase1-SidechainBusTest` (87 MB) — phase-1 test project referenced by the PHASE reports.
- `/Users/martin/Downloads/SideChainer-0.4.1-macOS-Beta-Installer.dmg` — delivered tester copy (outside project; optional).

## 4. MUST NOT transfer (disposable caches / generated intermediates)

Safe to exclude from the migration archive (regenerable; nothing here is the only copy of anything):

| Path (under `SideChain/SideChain/`) | Size | Why disposable |
|---|---|---|
| `Builds/MacOSX/build/Debug/` | 224 MB | Debug build output |
| `Builds/MacOSX/build/Release/` | 31 MB | Rebuildable Release output (final artifacts live in release-assets) |
| `Builds/MacOSX/build/SideChain.build/` | 10 MB | Xcode intermediates |
| `Builds/MacOSX/build/XCBuildData/`, `EagerLinkingTBDs/`, `ExplicitPrecompiledModules/`, `SwiftExplicitPrecompiledModules/` | ~1 MB | Xcode caches |
| `Builds/MacOSX/build/recovery-original-installed/` | 9.5 MB | Install-recovery copy of bundles (0.4.0) |
| `Builds/MacOSX/build-validation/` entire dir | 155 MB | `DerivedData/` (71 MB), `lifecycle-wake-test-20261005/` (74 MB incl. DerivedData), `projucer-worktree/` (10 MB) — one-off validation scratch |
| `Builds/MacOSX/build/release-assets/SideChainer-0.4.1-macOS-PRODUCTION-CANDIDATE-20261005/{DerivedData,DerivedData-AU,DerivedData-AUVersion,DerivedData-Final,DerivedData-Final-Absolute,BuiltProducts}` | ~386 MB | Xcode DerivedData/build products scratch inside the candidate dir — **keep the sibling pkg/dmg/receipts/logs listed in §3.2** |
| Same dir: `package-root/`, `package-root-final/`, `pkgroot-rebuild/`, `dmg-root/`, `dmg-final-staging/`, `package-build-final-tmp/`, `package-input/`, `dmg-payload/`, `SideChainer-0.4.1-macOS/` staging copies | ~45 MB | Packaging staging trees (rebuildable from FINAL pkg) |
| `Builds/MacOSX/Builds/` (nested duplicate output tree) | 31 MB | Stray nested build output incl. duplicate candidate BuiltProducts |
| `Tests/*.o`, compiled test binaries (`DSPRegressionTests`, `Phase4Tests`, `Phase6Tests`, `Phase8Tests`, `PresetTests`, `DuckLengthTests`, `BeatSchedulerTests`, `UIVerifyHarness`, `HostProbeAU`, `LogicFidelityProbe`, `CPUBench`, `BeatSchedulerTests`…), `LogicFidelityProbe.dSYM/`, `HostCheck/VST3BusCheck` binary, `Tests/*.log` | ~90 MB | Rebuilt by `Tests/*.sh` on the new Mac |
| `.DS_Store` files | — | macOS noise |

**Not part of this migration (do not touch):** unrelated checkouts (`Music-ProdPROJECT-GitHub`, `Music-ProdStudio-Backend*`, ChordEngine, VYRE, `surge`, etc.) and `/Users/martin/Documents/HISE-source` (1.1 GB git checkout — provenance copy only; the build uses `Documents/HISE`, not this).

*No separate machine-readable inventory file was created: with no Git repo the manifest itself is the single source of truth, and an extra file would only duplicate it and risk drifting out of sync.*

## 5. Local-only files (exist outside any VCS)

Everything in §3 is local-only (no Git exists). Highest-risk unique local state:

1. Entire source tree + all release evidence (nothing has ever been committed anywhere).
2. `/Users/martin/Documents/HISE` (un-versioned working copy with local JUCE patches; a separate `HISE-source` git repo exists but was not verified to match).
3. Keychain items in §6 (cannot be copied as files; must be re-created).
4. Notarization receipts + validation logs (only copies of pass evidence for 0.4.1).
5. `Windows-Build-Transfer` zip (only packaged Windows handoff snapshot).

## 6. Credential / Keychain requirements — NAMES ONLY, no values

Nothing below was read or printed. All values must be re-created/re-obtained on the new Mac manually.

| # | Credential (name/type) | Where expected | Used by |
|---|---|---|---|
| 1 | macOS Keychain **generic password**, service **`Music-Prod SideChainer Beta Upload`**, account `martin` (login keychain) — Supabase beta **upload token** | Keychain (presence re-confirmed at inspection; value not read) | `Builds/MacOSX/publish_sidechainer_beta.sh` via `security find-generic-password -s … -a $(id -un)` |
| 2 | `notarytool` **Keychain profile `SideChainerNotary`** (encapsulates Apple notary API credentials) | Keychain (notarytool profile store) | Notarization workflow (validated per `Tests/RELEASE_REPORT_0.4.0_DISTRIBUTION.md`) |
| 3 | Code-signing identity **`Developer ID Application: Martin Kadziolka (3A4R5EKM7V)`**, Team **`3A4R5EKM7V`** (cert SHA-256 fingerprint `0E0E33…715B8`, valid to 2031-09-14) | Login Keychain (private key inside); no `.p12` export found anywhere in project | `codesign` for AU/VST3 signing |
| 4 | **Developer ID Installer** identity (same team; used to sign FINAL pkg) | Login Keychain | `productbuild`/`pkgbuild`-related signing + notarization |
| 5 | Adjacent but NOT referenced by this project: Keychain generic password service **`com.music-prod.studio.auth`**, account `session` (Studio) | Login Keychain | Mentioned for completeness; migrate only if Studio work continues on the new Mac |

**Not present / nothing to migrate:** no `.env` files, no `.p12`/`.cer`/provisioning profiles, and no embedded tokens/JWTs/private keys found in project source or docs (pattern scan outside build dirs: 0 matches). **Hostinger:** no API token/credentials exist on this Mac (CLI installed but OAuth never completed) — nothing to transfer. **Supabase:** no keys in source; only endpoint URLs + the Keychain upload token (row 1). **GitHub:** no remote/credentials used by this project.

## 7. Required development tools (supported by project files/scripts)

- **Xcode** (project `Builds/MacOSX/SideChain.xcodeproj`; `xcodebuild` logs). Built with **Xcode 26.3 (17C529)** on this Mac. `objectVersion = 46` opens in any modern Xcode.
- **Xcode Command Line Tools**: `clang++` (C++17, `-mmacosx-version-min=11.0`, universal `x86_64+arm64`), `xcrun`.
- **macOS packaging/signing tools** (all ship with Xcode/CLT): `pkgbuild`, `pkgutil`, `lsbom`, `ditto`, `codesign`, `spctl`, `stapler`, `hdiutil`, `plutil`, `shasum`.
- **`notarytool`** (via `xcrun`) — notarization (profile `SideChainerNotary`).
- **`auval`** — AU validation (`auval -v aufx SdCh Musc`).
- **`python3`** (publish script + Tests scripts), **`curl`** (publish script), **`bash`**, **`security`** (Keychain lookups).
- **JUCE 6.1.3 module tree** at `/Users/martin/Documents/HISE/JUCE/modules` (path is hard-coded; see §3.3).
- Windows side (for coordination, on the Windows machine — not this migration): Visual Studio 2022 C++, MSBuild, **Projucer 6.1.4** against the **same JUCE 6.1.3** modules (per `Tests/WINDOWS_BUILD_HANDOFF_0.4.0.md`).
- Homebrew: present on this Mac (used to install utilities) but **not required by any SideChainer script**; installing it on the new Mac is optional convenience.

## 8. Required external services

- **Supabase project `wfpeajmdojcjqyrsnxbk`** — beta pipeline: edge functions `sidechainer-test-upload/macos`, `sidechainer-test-download/latestbeta`, storage bucket `sidechainer-test`, public alias `https://music-prod.com/sidechainer/latestbeta`. Only needed for beta publishing.
- **Auth endpoint** referenced by the plugin at runtime: Supabase function `vyre-plugin-auth` (`Source/PluginEditor.cpp`).
- **Apple Developer Program, Team `3A4R5EKM7V`** — Developer ID signing + notarization.
- **`music-prod.com` / Hostinger hosting** — website/alias hosting for the beta route; note: no working Hostinger credentials exist on this Mac (prior investigation: FTPS hostname mismatch, SFTP host key unverified, CLI OAuth incomplete). Blocked regardless of migration.
- **GitHub** — not used by this project (no remotes).

## 9. Known release requirements

1. Canonical inputs: signed plugin bundles inside the verified `SideChainer-0.4.0-macOS-PRODUCTION-20261004.pkg` (SHA-256 `449d3df8…06abf`, hard-coded default in `package_sidechainer_0.4.0.sh`) → rebuild pkg with 0755 staging fix.
2. Sign with Developer ID Application (Hardened Runtime + secure timestamp), sign/notarize pkg with Developer ID Installer, `notarytool` using profile `SideChainerNotary`, staple (`stapler`), verify (`spctl --assess --type install`, `codesign --verify --deep --strict`, `hdiutil verify`, `stapler validate`).
3. Regression gates before release: `Tests/run_all.sh --rebuild` (7 suites, baseline **227/227**), `Tests/ui_verify.sh` (**43/43**), `Tests/run_hostprobe.sh` (AU probe 4/4), `HostCheck/VST3BusCheck`, `auval -v aufx SdCh Musc`.
4. Beta publish: `publish_sidechainer_beta.sh <artifact> <sha256> [size]` — manual, fail-closed, needs Keychain row 1; never touches production release tables.
5. Windows artifact: still **NOT AVAILABLE / NOT TESTED**; follow `Tests/WINDOWS_BUILD_HANDOFF_0.4.0.md`.

## 10. Known Windows coordination requirements

- Transfer `Windows-Build-Transfer/` (zip + extracted) — frozen source snapshot incl. JUCE copy.
- Keep contract docs: `Tests/WINDOWS_BUILD_HANDOFF_0.4.0.md` (+ `RELEASE_REPORT_0.4.0_DISTRIBUTION.md`).
- Rules: do **not** save a Windows exporter into the shared `SideChain.jucer`; generate VS2022 project from an isolated copy; JUCE modules must stay exactly 6.1.3; identity codes `Musc`/`SdCh`, bundle `com.musicprod.sidechain` must not change.

## 11. Exact restore order on the new Mac

1. Create the **same absolute layout** — user path must keep `/Users/martin/Documents/…` (scripts hard-code `/Users/martin/Documents/HISE/JUCE/modules` and default artifact paths; a different user name requires editing those scripts — deferred, not done now).
2. Copy `/Users/martin/Documents/HISE/` → new Mac (verify `HISE/JUCE/modules/juce_core` exists).
3. Copy `/Users/martin/Documents/SideChain/SideChain/` (full project, or archive minus §4 prune list).
4. Copy `/Users/martin/Documents/SideChain/Windows-Build-Transfer/`.
5. Optionally copy `Phase1-SidechainBusTest/` and the delivered beta DMG.
6. Install **Xcode** (26.x) + Command Line Tools; `xcodebuild -version`, `clang++ --version`.
7. Recreate credentials (names from §6): import Developer ID Application + Developer ID Installer certs/private keys → `notarytool storecredentials SideChainerNotary` → re-create Keychain generic password `Music-Prod SideChainer Beta Upload` (account = new username) with a fresh/re-obtained upload token. **No secret values are stored in this manifest.**
8. Run validation (§12).
9. Only after a passing verification: initialize a Git repo / remote backup for SideChainer (currently nonexistent — recommended follow-up, not performed in this task).

## 12. Post-migration verification checklist

- [ ] `ls /Users/martin/Documents/HISE/JUCE/modules` succeeds; matches 6.1.3 tree.
- [ ] `git` not required, but `SideChain.jucer`, `Source/`, `JuceLibraryCode/`, `Builds/MacOSX/SideChain.xcodeproj` all present.
- [ ] SHA-256 of restored `SideChainer-0.4.1-macOS-PRODUCTION-FINAL.pkg` = `ee50a367…a2052`; FINAL.dmg = `8545ea2e…46f8` (byte-exact transfer).
- [ ] Notarization receipt JSONs present and still status `Accepted`; `stapler validate` passes on FINAL pkg/dmg after restore.
- [ ] `xcodebuild` Release build of `SideChain.xcodeproj` succeeds (universal x86_64+arm64).
- [ ] `Tests/run_all.sh --rebuild` → ALL SUITES PASSED (baseline 227/227).
- [ ] `Tests/ui_verify.sh` → 43/43.
- [ ] `Tests/run_hostprobe.sh` → AU host probe 4/4 (requires installed AU).
- [ ] `HostCheck/VST3BusCheck` passes on built VST3.
- [ ] `auval -v aufx SdCh Musc` → validation succeeded (after installing AU).
- [ ] `codesign --verify --deep --strict` + `spctl --assess --type install` accepted (`source=Notarized Developer ID`) on restored bundles.
- [ ] `security find-generic-password -s "Music-Prod SideChainer Beta Upload" -a <user> ` → found (do not print value).
- [ ] `xcrun notarytool history --keychain-profile SideChainerNotary` → succeeds.
- [ ] `package_sidechainer_0.4.0.sh` preflight passes against preserved input pkg (expected SHA matches).
- [ ] Publish path sanity (no upload): script present, Keychain entry found, endpoints reachable — actual upload only when deliberately publishing.
- [ ] Windows handoff zip intact: `SideChainer-0.4.0-Windows-Build-Source.zip` SHA-256 recorded and verified.

## 13. Blockers / uncertainties (as of inspection)

1. **No Git repository exists** — no branch/status/history to report; nothing has ever been committed. Migration must be a file-level transfer (strategy C). Post-migration version control is strongly recommended but was intentionally not initialized in this task.
2. **Hard-coded absolute paths** (`/Users/martin/Documents/...`) in `Tests/run_all.sh`, `ui_verify.sh`, `run_hostprobe.sh`, package-script defaults, and the `.jucer`/pbxproj HISE layout — the new Mac must reproduce the same paths, or those files must be edited after transfer.
3. **Keychain values cannot be carried by this manifest** — items in §6 must be manually re-created; upload token must be re-obtained from its issuer.
4. **No `.p12` export of the signing certificates found** in the project or Documents — private keys exist only in this Mac's Keychain (plus a CSR at `/Users/martin/Documents/CertificateSigningRequest.certSigningRequest`); export/import via Keychain Access is required and was NOT performed here.
5. `Documents/HISE` is un-versioned; whether it exactly matches `HISE-source` (git) was not verified.
6. Hostinger access remains blocked (no credentials) — external to this migration.
