# PHASE 7 REPORT — SideChain (Music-Prod)

Phase 7 goal: production preset system — 10 compiled-in factory presets over the two existing parameters, compact styled selector UI, derived Custom/Default identity, audio-safe recall, full regression.

Verdict: **GO**

---

## 1. FILES INSPECTED

All required files were read completely before editing:

- `Source/PluginProcessor.h` / `.cpp` — APVTS architecture (2 params, no undo manager), processBlock path, state flush loop, Phase 7A hardening
- `Source/DuckEngine.h` — validated DSP (untouched this phase)
- `Source/PluginEditor.h` / `.cpp` — ProdKnob architecture, 800×500 layout, header/graph/controls structure, 30 Hz timer
- `Source/GraphData.h` / `GraphComponent.h` / `GraphComponent.cpp` — real-data graph transport (untouched this phase)
- `Tests/DSPRegressionTests.cpp` (Phase 3), `Tests/Phase5TimingTests.cpp` (Phase 5), `Tests/Phase6Tests.cpp` (Phase 6 + 7A), `Tests/Phase4Tests.cpp` (harness context)
- `Tests/run_all.sh` — 5-suite runner mechanics
- `Tests/CPUBench.cpp` / `Tests/cpu_bench.sh` — benchmark conditions
- `Tests/MANUAL_HOST_TEST.md` — host guide status
- `PHASE7A_REPORT.md`, `PHASE6_REPORT.md` — prior-phase contracts

Inspection conclusions (no guessing):

- Parameters are `AudioParameterFloat` in APVTS; `setValueNotifyingHost` + `convertTo0to1` is the established host-visible change path; an APVTS::Listener (`parameterChanged`) already re-syncs the engine on Release changes.
- `parameters` is constructed with `nullptr` undo manager — **no undo system exists**, so none was built (per brief).
- Amount: 0–100, step 0.1, default 50. Release: 50–1000 ms, skew 0.3 centre 200, default 150.
- State: versioned ValueTree (`stateVersion` = 1), PARAM children matched by `id`; Phase 7A sanitisation gates load.
- UI: header 34 px → graph → controls row; `ProdKnob` shared painting; palette constants in an anonymous namespace.
- Measured depth anchors for preset design: Amount 25/50/75/100 % → ≈ −4.9/−17/−35/−59 dB settled (Phase 3/5/6 measurements); audible recovery ≈ 4.3× the Release value (Phase 6).
- No factory preset equals the default (50 %/150 ms) → a derived "Default" display is cleanly representable.

## 2. FILES CREATED / MODIFIED

All paths project-relative (`SideChain/SideChain/`):

Created:
- `Source/PresetManager.h` — header-only factory preset system: `FactoryPreset` struct, the 10-entry compiled-in table, `findByName()`, `findByValues()` (0.05 tolerance, matching Amount's 0.1 step resolution). Documented per-preset design rationale. Zero DSP involvement.
- `Tests/PresetTests.cpp` — Phase 7 preset suite, 28 checks, compiles the real processor headlessly (same recipe as Phase6Tests).
- `PHASE7_REPORT.md` — this file.

Modified:
- `Source/PluginProcessor.h` — includes `PresetManager.h`; public API: `applyFactoryPreset (String) -> bool`, `applyDefaultPreset()`, `getCurrentPresetDisplayName() const`; private `applyPresetValues (float, float)` shared apply path; header doc updated.
- `Source/PluginProcessor.cpp` — preset apply implementation (below); no changes to processBlock, buses, DSP, or the Phase 7A state code.
- `Source/PluginEditor.h` — `presetBox` (ComboBox) + `presetCaption` (Label) members; `buildPresetMenu()` / `refreshPresetDisplay()` helpers; layout comment updated.
- `Source/PluginEditor.cpp` — preset selector styling + wiring; new 30 px preset strip in `resized()`; `timerCallback` refreshes the derived preset identity every tick; palette additions (`kControlBg`, `kControlEdge`, `kPresetText`); removed long-unused `kPanel` constant (fixed a pre-existing production warning); fixed the Phase 7A tooltip `\~` escape (warning) and used `addSeparator()` (correct JUCE 6.1.3 API).
- `Tests/run_all.sh` — added suite 5 (PresetTests, same headless recipe); header comment updated.

Unchanged: `Source/DuckEngine.h`, `Source/GraphData.h`, `Source/GraphComponent.*`, `Tests/MANUAL_HOST_TEST.md`, `SideChain.jucer` (the new header is picked up by the existing Source search path — confirmed by successful Xcode builds), all prior test suites, all unrelated projects.

## 3. PRESET ARCHITECTURE

- **Preset manager:** `sid::presets` namespace in `Source/PresetManager.h` — pure data + lookup helpers. It knows nothing about the processor, the engine, or the graph. Sits entirely ABOVE the DSP, exactly per the brief's architecture diagram (PluginProcessor → APVTS + DuckEngine + PresetManager as siblings; PresetManager does not touch DuckEngine).
- **Factory preset storage:** compiled into the plugin as a static `juce::Array<FactoryPreset>` (function-local static in `factoryPresets()`), one entry per named {amount, release} pair. No external files for factory presets; nothing to load, lose, or corrupt.
- **State relationship:** presets are NOT state. The authoritative saved state remains the two parameter values inside the versioned `PARAMS` tree. Preset identity is re-derived from values on load (see Custom detection), so preset naming/reordering can never break old sessions.
- **Custom detection:** `getCurrentPresetDisplayName()` compares the LIVE parameter values against the factory table via `findByValues` (tolerance 0.05, below the Amount step of 0.1). Exact match → that preset's name; plugin defaults (50/150) → "Default"; anything else → "Custom". "Custom" is never stored anywhere — it is a display concept only.
- **Parameter notification:** `applyPresetValues` uses the parameters' own mechanism exclusively: `beginChangeGesture()` → `setValueNotifyingHost (convertTo0to1(...))` on BOTH parameters → `endChangeGesture()`. No raw pointer/atomic mutation, no direct ValueTree writes. Consequences: host automation listeners are notified, the editor's SliderAttachments update automatically, the existing APVTS listener re-syncs the engine's release coefficients, and APVTS syncs the ValueTree for state.
- **Undo behavior:** the project has no UndoManager (APVTS constructed with `nullptr`); per the brief, no undo system was built. A preset apply is bracketed as one change gesture, which is the standard hook should undo ever be added.

## 4. FACTORY PRESETS

Compiled-in table (exact values, also documented in `PresetManager.h`):

| # | Name | Amount | Release | Design intent (vs measured DSP) |
|---|------|--------|---------|-------------------------------|
| 1 | Subtle | 15 % | 150 ms | Very light ducking (~ −2 dB settled with a typical trigger — depth curve `−60·amount^1.8` gives 15 % ≈ 3 % of max depth), default recovery |
| 2 | Gentle | 30 % | 200 ms | Light ducking (~ −6 dB), slightly longer tail |
| 3 | Vocal Duck | 50 % | 300 ms | Moderate depth (~ −17 dB), smooth recovery keeping speech intelligible in the gaps |
| 4 | Bass Duck | 60 % | 100 ms | Moderate/strong (~ −23 dB), fast recovery so bass returns between kicks (kick-to-bass use) |
| 5 | Kick Pump | 75 % | 100 ms | Strong rhythmic ducking (~ −35 dB), tight recovery |
| 6 | Deep Pump | 85 % | 250 ms | Deeper (~ −45 dB) with a longer musical tail |
| 7 | EDM Pump | 90 % | 400 ms | Stronger, longer pumping (~ −49 dB, ~1.7 s audible recovery) — the classic sidechain feel |
| 8 | Hard Pump | 95 % | 150 ms | Very strong (~ −53 dB) but snappy |
| 9 | Extreme | 100 % | 700 ms | Near-maximum aggression, very long tail (~3 s audible recovery) |
| 10 | 100% Duck | 100 % | 150 ms | Full depth with a default-speed recovery |

Names are exactly the brief's starting list (no renaming was warranted). No value was chosen for symmetry: Release differences encode the named use cases (fast inter-kick recovery for Bass/Kick at 100 ms; long tails for EDM/Extreme at 400/700 ms). Amounts map onto the measured depth curve; Releases onto the validated 50–1000 ms control whose audible recovery is ≈ 4.3× the displayed value (documented since Phase 6 and stated in the Release tooltip).

## 5. PRESET BEHAVIOR

- **Preset selection:** sets BOTH parameters through `setValueNotifyingHost` (gesture-bracketed). The DSP follows automatically (Amount → depth read per block from the atomic; Release → APVTS listener → `applyReleaseToEngine`). No detector reset, no engine restart, no audio interruption.
- **Amount manual change:** identity becomes "Custom" immediately (derived from live values; the editor timer refreshes it every tick, so it also tracks host automation).
- **Release manual change:** same — "Custom".
- **Return to exact preset:** manually returning to the exact parameter values re-identifies the preset by value (tested, including a 0.11 near-miss that correctly stays Custom, and a Hard Pump→100 % value that correctly identifies as "100% Duck" — matching a different preset's values names THAT preset, which is the honest behavior).
- **Audio safety:** per-sample smoothing (Phase 3/5 design) means even simultaneous Amount+Release changes are click-free. Tested by switching through all 10 presets every 250 ms during active kick-triggered audio: no NaN/Inf, no step > 0.25, never above unity, no leakage, stereo image intact.
- **Graph behavior:** unchanged and unmodified; it keeps consuming real DSP frames (tested that frames continue flowing after preset churn). No fake animation, no preset-specific graph data.

## 6. UI

- **Preset selector:** compact 240×24 `juce::ComboBox`, centred in a new 30 px strip between the header and the graph. Styled to the existing design language: dark panel body (`kControlBg`), subtle edge (`kControlEdge`), soft-mint text (`kPresetText`), centred text, no default blue popup chrome clashes. Keyboard-safe by default (standard JUCE ComboBox focus/menu navigation). It does not compete with AMOUNT: it is a thin text strip, not a knob.
- **Current preset display:** the closed box shows the derived identity — factory name when values match, else "Default"/"Custom".
- **Custom display:** shown as text with no item selected (for "Default" the popup also offers an explicit "Default (50% / 150 ms)" entry; "Custom" is display-only by design).
- **Amount:** unchanged — large mint knob, primary, caption "SIDECHAIN AMOUNT".
- **Release:** unchanged — smaller gold knob, secondary, caption "RELEASE".
- **Overall layout:** 800×500 unchanged; hierarchy now HEADER → PRESET (thin strip) → GRAPH → MAIN CONTROLS, matching the brief's conceptual sketch. Editor sources gained no new image assets; all drawing remains direct JUCE.

## 7. STATE

- **State version:** **1 — unchanged.** The serialized schema (PARAMS tree with two PARAM children + stateVersion) is unchanged by presets; incrementing would have been schema theatre. Explicitly tested ("stateVersion stays 1").
- **Amount:** saved/restored exactly as before (APVTS PARAM child).
- **Release:** same mechanism.
- **Preset name/index stored:** **NO** — tested that the saved tree contains no property whose name contains "preset". Values only.
- **Custom state:** not stored; a Custom session saves its values and re-identifies as "Custom" on restore (tested: 63.4 % custom value round-trips and shows Custom).
- **Corrupt-state handling:** all Phase 7A hardening intact and still passing (49/49 in Phase6Tests). Preset loading adds no new state path, so the hardening covers everything.

## 8. AUTOMATION

- **Amount:** unchanged APVTS automation; Phase 4 sweep test still green.
- **Release:** unchanged APVTS automation; Phase 6 sweep test still green.
- **Preset interaction:** preset apply goes through the same notification path as automation, so hosts see it as normal (gesture-bracketed) parameter changes. No "preset" parameter was created; presets are not automatable, by design.

## 9. TEST SUITES

- **Phase 3:** 32/32 PASS
- **Phase 4:** 13/13 PASS
- **Phase 5:** 24/24 PASS
- **Phase 6:** 35/35 PASS (+ 14 Phase 7A = 49/49 in Phase6Tests)
- **Phase 7A:** 14/14 PASS (inside Phase6Tests, unchanged)
- **Phase 7 presets:** **28/28 PASS** (`Tests/PresetTests.cpp`): 10 presets exist / unique names / valid ranges / required names / exact documented values · exact recall of every preset · host-visible parameter objects · engine re-sync after recall · unknown name rejected · fresh plugin shows "Default" · name shown on select · Amount-change→Custom · Release-change→Custom · exact return re-identifies · near-miss stays Custom · Default action restores 50/150 · preset state round-trip + re-identification · custom round-trip as Custom · no preset props in state · rename safety · stateVersion 1 · bus topology unchanged · finite during switching · never above unity / stereo intact · no zipper · no leakage · graph frames continue.
- **Combined:** 32+13+24+49+28 = **146/146 — ALL SUITES PASSED**, verified from a clean state (all binaries deleted, `--rebuild`).
- **Exit code:** **0**

## 10. CPU

- **Benchmark:** `Tests/CPUBench.cpp` via `Tests/cpu_bench.sh` (unchanged conditions: 10 s stereo main + stereo sidechain, block 512, 48 kHz, Release −O2).
- **Result:** **0.059 %** of one core (1695× realtime).
- **Comparison to Phase 7A:** 0.058–0.076 % range — **no regression** (the preset system added zero audio-path work; presets only move parameters on the message thread).
- **Any regression:** none. No test-only work can enter the plugin CPU path — the preset table is static data with no audio-thread touchpoints.

## 11. BUILD QUALITY

- **Debug AU:** BUILD SUCCEEDED (universal)
- **Release AU:** BUILD SUCCEEDED (universal), installed to `~/Library/Audio/Plug-Ins/Components/`
- **Debug VST3:** BUILD SUCCEEDED (universal)
- **Release VST3:** BUILD SUCCEEDED (universal), installed to `~/Library/Audio/Plug-Ins/VST3/`
- **Production-source warnings:** eliminated. Three were found and fixed this phase: (1) `kPanel` unused-const warning (pre-existing, removed), (2) `\~` unknown-escape warning from the Phase 7A tooltip string (fixed to a plain `~`), (3) an initial compile error caught by the Xcode build — `addSeparatorItem()` does not exist on JUCE 6.1.3 `ComboBox`; corrected to `addSeparator()` (the headless test harness never compiles the editor, which is why the test build did not catch it — the Xcode build did, exactly as the warning-audit step is designed to).
- **JUCE warnings:** the known benign set remains: Carbon deprecation from JUCE's macOS glue (unfixable without modifying JUCE — documented, not touched), one `std::move` unqualified-cast warning inside `juce_AudioProcessorValueTreeState.cpp` (JUCE internal), and deprecated-parameter-API warnings from the Phase 4 test's dummy processor (test-only, mirrors JUCE 6 legacy vtable).
- **Remaining warnings:** only the JUCE-internal ones listed above. Zero warnings originate from `Source/`.

## 12. AU

- **auval:** `auval -v aufx SdCh Musc` → **`* * PASS` / AU VALIDATION SUCCEEDED** (after `killall -9 AudioComponentRegistrar`, on the freshly installed Phase 7 Release build)
- **Input:** stereo (bus 0, "Input")
- **Sidechain:** stereo (bus 1, "Sidechain" aux input — validated via auval's connection-semantics checks)
- **Output:** stereo (bus 0, "Output")

## 13. VST3

- **Main bus:** input bus 0 — `kMain`, stereo, "Input", defaultActive=yes — PASS
- **Sidechain bus:** input bus 1 — **`kAux`**, stereo, "Sidechain", defaultActive=no — PASS (intact)
- **Output bus:** output bus 0 — `kMain`, stereo, "Output", defaultActive=yes — PASS
- (`HostCheck/VST3BusCheck`: "VST3 BUS CHECK PASSED (0 failure(s))")

## 14. UNIVERSAL

- **AU:** `lipo -archs` → `x86_64 arm64`
- **VST3:** `lipo -archs` → `x86_64 arm64`

## 15. HOST STATUS

Logic:
- Automatically verified: **No** (GUI automation unavailable; presets were NOT exercised inside Logic by the agent)
- Manual verification required: **YES** — `Tests/MANUAL_HOST_TEST.md` Part 1 (15 checks incl. exact sidechain-menu wording). AU-level automated evidence: auval PASS on the exact installed build.

Ableton:
- Automatically verified: **No** (same limitation)
- Manual verification required: **YES** — `Tests/MANUAL_HOST_TEST.md` Part 2 (10 checks incl. exact aux-routing wording). VST3-level automated evidence: VST3BusCheck 0 failures on the exact installed build.

No host result is claimed beyond the system-level validation above.

## 16. PROBLEMS FOUND

1. **`ComboBox::addSeparatorItem()` does not exist in JUCE 6.1.3** — first Xcode build failed in `PluginEditor.cpp`. Fixed with `addSeparator()`. Not caught earlier because the headless test harness does not compile the editor; the Xcode warning-audit step caught it, which is precisely why that step exists.
2. **Phase 7A tooltip contained `\~`** (over-escaped during 7A) — produced `-Wunknown-escape-sequence` from production source. Fixed to plain `~`.
3. **Pre-existing `kPanel` unused-const warning** in `PluginEditor.cpp` (unused since at least Phase 6). Removed.
4. **Test-only calibration:** the "Amount change → Custom" probe initially used `preset + 5`, which for the 95 % "Hard Pump" lands exactly on "100% Duck" (100/150) — correct value-identification, not a bug; the probe now steps −7 (always in range, never on another preset). Test adjusted, product code unchanged.

## 17. PHASE 7 CONCLUSION

**GO**

The preset system is exactly the brief: ten compiled-in factory presets over the two existing parameters (no new parameters, no hidden modes, no DSP changes — `DuckEngine.h` is byte-for-byte untouched), recalled through the proper host-visible notification path with gesture bracketing, with a compact styled selector that shows the derived identity (factory name / Default / Custom) and never stores preset identity in state (rename-safe, value-authoritative, stateVersion stays 1). All 28 new preset tests plus the full 104-check regression pass in one command from a clean rebuild with exit 0 (146/146). Preset switching during active audio is demonstrably click-free, finite, unity-bounded, leak-free and stereo-intact; the graph remains real-data driven. AU validates (auval PASS), the VST3 kAux sidechain is intact, both formats are universal, CPU is unchanged at 0.059 %, production-source warnings are eliminated (only documented JUCE-internal warnings remain), and no JUCE/HISE/ChordEngine/VYRE/Phase-1 files were modified. No Windows build, no packaging, no signing.

## 18. PHASE 8 RECOMMENDATION

Phase 8 should be **user preset save/load** — the natural completion of the preset system, still above the DSP:

1. A "Save User Preset…" flow that serializes the existing versioned `PARAMS` state to a standard per-user folder (e.g. `~/Audio/Presets/Music-Prod/SideChain/`), so corrupt/foreign files inherit the Phase 7A hardening for free.
2. Loading user presets through the same value-based path (factory matching first, then "Custom") so user presets never fork the identity logic.
3. Delete/rename for user presets, factory presets read-only.

Alternatively, if host confidence is preferred first: execute `Tests/MANUAL_HOST_TEST.md` in Logic 12.0.1 (AU) and Live 11 Suite (VST3) — the two exact-wording fields remain the largest outstanding evidence gap — before any further features.

Either way, explicitly NOT in Phase 8: Windows, packaging, signing, notarization, new DSP modes, new user parameters.
