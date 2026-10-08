# PHASE 7A REPORT

Scope: pre-preset stabilization — (1) manual host verification facilitation, (2) Release scale honesty review, (3) corrupt-state hardening + tests. No presets, no Windows, no packaging, no signing, no new parameters, no DSP changes, no unrelated projects touched.

## 1. FILES INSPECTED

All read before editing (re-read from disk after the environment restart, per instructions):

- `Source/PluginProcessor.h`
- `Source/PluginProcessor.cpp` (full, incl. getStateInformation flush loop, state recovery policy block, sanitisation helpers, setStateInformation gate)
- `Source/DuckEngine.h` (validated in Phase 5/6 — intentionally NOT rewritten)
- `Source/PluginEditor.h`
- `Source/PluginEditor.cpp`
- `Source/GraphData.h`
- `Source/GraphComponent.h`
- `Source/GraphComponent.cpp`
- `Tests/DSPRegressionTests.cpp`
- `Tests/Phase5TimingTests.cpp`
- `Tests/Phase6Tests.cpp` (incl. the 14 Phase 7A state-hardening checks in section 8)
- `Tests/run_all.sh`
- `Tests/MANUAL_HOST_TEST.md`
- `PHASE6_REPORT.md`

## 2. FILES CREATED / MODIFIED

All paths project-relative (`SideChain/SideChain/`):

Modified:
- `Tests/run_all.sh` — Phase 6/4 compile invocation fixed: the test source was being passed TWICE (once inside the TU list, once by `build_and_run`), producing `duplicate symbol '_main'`; the earlier flag-quoting workaround (6 positional args) also misaligned `build_and_run`'s arguments. Now: `-DJucePlugin_Name=\"SideChain\"` lives inside `PH6_FLAGS`, the duplicated source was removed from both TU strings, and compile failure detection uses the compiler's exit status (`PIPESTATUS`) with full diagnostics echoed (linker errors contain no `error:` text, which is why previous failures showed no diagnostic).
- `Source/PluginProcessor.cpp` — hardened `setStateInformation` (was already on disk from the pre-restart work; verified in full this phase; Release/Debug rebuilt with it):
  - null/empty data → ignored, current state kept
  - `readFromData` result gated on `tree.getType() == juce::Identifier("PARAMS")` — garbage input otherwise yields a "valid" tree typed as the junk string and would be treated as loadable state
  - per-parameter sanitisation via anonymous-namespace helpers `sanitisedParamValue` (rejects void/non-numeric/non-finite) and `paramValueInRange` (finite + inside `getNormalisableRange()` ± 1e-6)
  - invalid/missing parameter → reset to default (50 / 150) via `setValueNotifyingHost(convertTo0to1(...))` after `replaceState`, then `applyReleaseToEngine()`
  - documented policy comment block added
- `Source/PluginEditor.cpp` — Release tooltip honesty change (verified on disk): `"Release timing (internal). Full musical recovery \~4x longer"`; label "RELEASE" and ms display unchanged
- `Tests/MANUAL_HOST_TEST.md` — one-line accuracy update: build reference now says "Phase 7A Release builds (Phase 6 UI + Phase 7A state hardening)" (same artifact paths)

Created:
- `Tests/CPUBench.cpp` — Phase 7A CPU sanity benchmark (10 s stereo main + stereo sidechain, block 512, 48 kHz, Release -O2; signal pre-generated outside the timed region; raw-pointer per-sample loop; same conditions as the Phase 5/6 baseline measurement)
- `Tests/cpu_bench.sh` — build+run script for the above (same compile recipe as the audio-only suites)
- `PHASE7A_REPORT.md` — this file

## 3. LOGIC PRO

- Installed: Logic Pro 12.0.1
- Agent GUI tested: **No** — reliable interactive GUI automation is not available in this environment (matching the Phase 5/6 finding). The plugin was NOT driven through Logic's UI by the agent.
- Result: **NOT AUTOMATICALLY VERIFIED**
- Exact sidechain UI wording: **not observed by the agent.** The guide (`Tests/MANUAL_HOST_TEST.md`) describes the expected location as the "Sidechain" dropdown in the top-right of the plugin window header, listing source tracks, but this wording must be confirmed by the human tester — it has not been read from a live Logic session.
- Manual user verification required: **YES** — the 12-step Logic procedure in `Tests/MANUAL_HOST_TEST.md` (Part 1), including the 12 acceptance items from the Phase 7A brief (load, effect recognition, sidechain menu, routing, passthrough, detector activity, no sidechain leakage, Amount depth, Release recovery, sidechain removal, graph behaviour, stability).
- Notes: The strongest automated evidence available is system-level: `auval -v aufx SdCh Musc` **PASS** (full AU validation incl. bus/connection semantics, parameter setting and scheduled ramps) on the exact Release build installed at `~/Library/Audio/Plug-Ins/Components/SideChain.component`. No host result is claimed beyond that.

## 4. ABLETON LIVE

- Installed: Ableton Live 11 Suite
- Agent GUI tested: **No** — same GUI-automation limitation.
- Result: **NOT AUTOMATICALLY VERIFIED**
- Exact sidechain UI wording: **not observed by the agent.** Ableton presents VST3 aux inputs in the audio-routing chooser of the plugin's track (typical wording "Sidechain"/aux input selection), but the exact label on this system must be recorded by the human tester per the guide.
- Manual user verification required: **YES** — the 10-step Ableton procedure in `Tests/MANUAL_HOST_TEST.md` (Part 2), covering load, VST3 sidechain availability, routing, separation of main audio, Amount/Release behaviour, graph response, sidechain removal, leakage and stability.
- Notes: Automated evidence: `HostCheck/VST3BusCheck` **PASSED (0 failures)** on the exact Release build — 2 audio input buses (bus 0 `kMain` stereo "Input", bus 1 `kAux` stereo "Sidechain", not active by default), 1 `kMain` stereo output bus. No host result is claimed beyond that.

## 5. RELEASE SEMANTICS

- Current user-facing label: **RELEASE** (secondary gold knob, right of the large SIDECHAIN AMOUNT knob)
- Current displayed range: 50–1000 ms, default 150 ms, skewed scale (0.3) centred on 200 ms, ms display rounded to 10 ms
- Actual DSP relationship: the user value scales BOTH internal release constants by `releaseMs / 150` (`DuckEngine::releaseToFactor`): detectorRelease = 150·factor, gainRelease = 120·factor, preserving the validated 150:120 ratio. Because the detector envelope must fall across the 36 dB trigger window while the gain tail recovers, the measured audible recovery is a cascade: t63 ≈ 4.3× the displayed value (50 → ~221 ms, 150 → ~543–640 ms, 1000 → > 4 s; Phase 6 + Phase 5 measurements agree).
- Evaluation: A user seeing "RELEASE 150 ms" could reasonably expect ~150 ms of total audible recovery — that expectation would be wrong by ~4×. However, the underlying parameter is a legitimate, host-automatable internal timing control, and the DSP architecture is validated. Option C (rescaling the display to musical recovery) would misrepresent the value hosts automate and would distort the parameter's direct relationship to the internal constants. Option B (renaming) would add confusion, not honesty. **Option A was chosen**: keep RELEASE + ms display, define it explicitly as the internal timing control.
- Change made: tooltip now reads "Release timing (internal). Full musical recovery ~4x longer" (`Source/PluginEditor.cpp`, Release knob). No label, range, default, or display-format change.
- Reason: smallest change that removes the misleading reading, with zero DSP impact and zero automation-surface impact (host-visible parameter unchanged; Amount/Release automation stay valid).

## 6. STATE HARDENING

- Current state version: **1** (stamped in `getStateInformation` before `writeToStream`; no schema change in 7A, so version stays 1)
- Missing Amount: runtime default restored → **50 %** (`setValueNotifyingHost` through the parameter object; clamped, host-notified, ValueTree-synced)
- Missing Release: runtime default restored → **150 ms** (`kReleaseDefaultMs`), then `applyReleaseToEngine()` re-syncs the engine
- Invalid Amount: out-of-range/negative/non-numeric/non-finite → default 50 % (never enters DSP)
- Invalid Release: negative / above maximum / non-numeric / NaN / Inf → default 150 ms (never enters DSP)
- Malformed state: unparsable data OR data whose decoded tree is not typed `PARAMS` → ignored ENTIRELY; current state kept; no partial load; no crash. Empty/null/size-0 data → same.
- Future version: `stateVersion` > 1 (test uses 99) → known parameters still load safely (forward-compatible subset); version kept as saved; unknown props/children ignored
- Unknown properties: ignored safely (APVTS semantics + explicit tests)
- NaN/Inf: cannot enter the DSP — `sanitisedParamValue` rejects non-finite values at the state layer before anything reaches a parameter or the engine; test verifies DSP output stays finite after a NaN/negative payload load
- Final recovery policy (as implemented and documented in `PluginProcessor.cpp`):
  - valid Amount/Release in range → restore
  - missing PARAM / missing id / non-numeric / non-finite / out-of-range value → that parameter resets to its default (50 / 150)
  - unparsable data / wrong tree type / empty data → ignore entirely, keep current state
  - missing `stateVersion` → stamped 1; future version → load known parameters, keep saved version
  - unknown properties and unknown PARAM children → ignored

## 7. TESTS

- Phase 3: **32/32 PASS** (`DSPRegressionTests`)
- Phase 4: **13/13 PASS** (`Phase4Tests`)
- Phase 5: **24/24 PASS** (`Phase5TimingTests`)
- Phase 6: **35/35 PASS** (Phase 6 sections of `Phase6Tests`)
- New Phase 7A: **14/14 PASS** (section 8 of `Phase6Tests`: valid restore; missing Amount→default; missing Release→default; negative Release; Release above max; Amount above max; NaN Release payload (DSP finite); Inf Amount payload (DSP finite); string value; malformed XML keeps current state; empty/null no-crash; future stateVersion 99; unknown props/params ignored; DSP finite after NaN/negative load)
- Combined: **104/104 PASS — ALL SUITES PASSED**
- Exit code: **0** — verified from a truly clean state: all four test binaries deleted, `./Tests/run_all.sh --rebuild` → exit 0; plain `./Tests/run_all.sh` → exit 0 (uses freshly built binaries).

## 8. AU

- Debug: **BUILD SUCCEEDED** (`SideChain - All`, universal, includes Phase 7A hardening)
- Release: **BUILD SUCCEEDED** (`SideChain - All`, universal, includes Phase 7A hardening), installed to `~/Library/Audio/Plug-Ins/Components/`
- auval: `auval -v aufx SdCh Musc` → **`* * PASS` / AU VALIDATION SUCCEEDED** (after `killall -9 AudioComponentRegistrar`; bus/connection semantics, parameter setting, ramped scheduling, MIDI all PASS)
- Bus topology: AU sidechain (aux) input bus intact as validated by auval's connection-semantics checks (2-in/1-out stereo arrangement unchanged since Phase 1)

## 9. VST3

- Debug: **BUILD SUCCEEDED** (`SideChain - All`, universal)
- Release: **BUILD SUCCEEDED** (`SideChain - All`, universal), installed to `~/Library/Audio/Plug-Ins/VST3/`
- Main bus: input bus 0 — `kMain`, stereo, name `Input`, defaultActive=yes — **PASS**
- Sidechain bus: input bus 1 — **`kAux`**, stereo, name `Sidechain`, defaultActive=no — **PASS** (intact)
- Output bus: output bus 0 — `kMain`, stereo, name `Output`, defaultActive=yes — **PASS**
- (`HostCheck/VST3BusCheck`: "VST3 BUS CHECK PASSED (0 failure(s))")

## 10. UNIVERSAL

- AU: `lipo -archs` → **`x86_64 arm64`**
- VST3: `lipo -archs` → **`x86_64 arm64`**

## 11. GRAPH

- Changed: **No** (zero changes this phase to GraphData/GraphComponent/graph plumbing)
- Real DSP data preserved: **Yes** — graph still renders actual sidechain/duck/output data from the audio thread via the existing 30 Hz FIFO; Phase 5 graph-ordering checks (10/10) and Phase 4 FIFO checks (4/4) remain green
- Visual behavior: unchanged by 7A; the manual guide describes expected graph behaviour for the human host tests (amber SIDECHAIN spikes, red DUCK fill drops fast / recovers slowly, OUTPUT dips)

## 12. PERFORMANCE

- CPU: **0.058–0.076 % of one core** (median ~0.064 %) measured with the new committed benchmark `Tests/CPUBench.cpp` — 10 s stereo main + stereo sidechain, block 512, 48 kHz, Release -O2, engine loop only (signal pre-generated outside the timed region, raw-pointer access). 1558× realtime. This is the same measurement condition as the Phase 5/6 baseline (0.036 %) which is no longer reproducible from a committed source; the new benchmark is committed so the number is now reproducible. DSP was unchanged in 7A, so the small delta vs. the historical figure is benchmark-harness difference (signal content and loop shape), not a plugin regression. Well within "reasonable" either way.
- Allocations: none in processBlock (engine is fixed-state, per-sample, no dynamic containers; state hardening touches only the message thread)
- Locks: none on the audio thread (Release re-sync via existing lock-free mechanism)
- Issues: none

## 13. PROBLEMS FOUND

1. **`Tests/run_all.sh` compiled test sources twice** — `Phase4Tests.cpp`/`Phase6Tests.cpp` appeared both in the TU string and as the `build_and_run` source argument → `duplicate symbol '_main'` at link. This was the real cause of the recurring "phantom link failure with no diagnostic": the old failure detection (`2>&1 | grep "error:"`) both swallowed the compiler's exit status and filtered out linker diagnostics (`ld: 1 duplicate symbols` contains no `error:` until the clang driver line). Fixed: source listed once, and failure detection now uses the compiler exit status with full output echoed to console and `Tests/compile.log`.
2. **Garbage-typed state tree accepted** (found during pre-restart inspection, confirmed and fixed this phase): `ValueTree::readFromData` can return a "valid" tree whose TYPE is the first bytes of arbitrary garbage, which the old code treated as loadable state. Fixed with the `PARAMS` type gate; covered by the "malformed XML ignored, current state kept" test.
3. **CPU baseline not reproducible**: the 0.036 % figure from Phases 5/6 had no committed benchmark source. Fixed by committing `Tests/CPUBench.cpp` + `Tests/cpu_bench.sh` with the same measurement conditions; measured 0.058–0.076 % (median ~0.064 %).

## 14. PHASE 7A CONCLUSION

**GO**

All three 7A goals are met with no scope creep: (1) host verification was facilitated honestly — Logic and Ableton are clearly marked NOT AUTOMATICALLY VERIFIED / MANUAL USER VERIFICATION REQUIRED with the up-to-date guide, backed by the strongest available automated evidence (auval PASS, VST3BusCheck 0 failures); no host result was guessed. (2) Release semantics were reviewed against the measured DSP cascade and resolved with the minimal honest change (Option A tooltip clarification); the validated DSP and the host-visible parameter surface are untouched. (3) State loading is hardened against all 14 malformed/corrupt/unexpected input classes with a documented policy, NaN/Inf cannot reach the DSP, and 14 new tests cover every case. The combined runner now exits 0 from a clean rebuild (the duplicate-`main` runner bug is fixed and diagnosed), Release and Debug are rebuilt universal with all 7A changes, auval passes, the VST3 kAux bus is intact, CPU is ~0.064 % of a core, and the audio thread remains allocation- and lock-free. No unrelated project was modified.

## 15. PHASE 7 RECOMMENDATION

Phase 7 should be the **preset system** — nothing else. Concretely:

1. Factory preset files (bundled read-only resources, e.g. Subtle / Drum Bounce / Classic Sidechain / Heavy Duck) built ONLY from the two existing parameters (`sidechainAmount`, `release`) — no new parameters to support them.
2. A minimal preset browser UI (compact prev/next + name display on the existing 800×500 editor, styled with the existing ProdKnob/label language) — no redesign.
3. User preset save/load into a standard per-user folder, with the save format reusing the existing versioned `PARAMS` state (stateVersion 1) so the Phase 7A hardening protects preset loading for free — corrupt/foreign preset files must follow exactly the same recovery policy (ignore or default, never crash).
4. Preset switching must remain click-free and automation-safe (dirty-state handling per host conventions).

Explicitly NOT in Phase 7: Windows, packaging, signing, notarization, new DSP modes, new user parameters (Depth/Attack/Threshold/Ratio/Knee/Mix/Sidechain filter/Lookahead/MIDI).

The remaining human task regardless of Phase 7: run `Tests/MANUAL_HOST_TEST.md` in Logic Pro 12.0.1 (AU) and Ableton Live 11 Suite (VST3) and record the exact sidechain UI wording.
