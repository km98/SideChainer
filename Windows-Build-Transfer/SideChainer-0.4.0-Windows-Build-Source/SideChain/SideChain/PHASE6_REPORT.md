# PHASE 6 REPORT — SideChain (Music-Prod)

Phase 6 goal: production Release control, versioned state, manual host verification guide.

Verdict: **GO**

---

## 1. FILES INSPECTED

All twelve required files were read in full before editing:

- `Source/PluginProcessor.h` / `.cpp` — parameter layout, processBlock path, lazy-state flush workaround
- `Source/DuckEngine.h` — smoothing constants (fixed at compile time pre-Phase 6), direction rule, graph accessors
- `Source/PluginEditor.h` / `.cpp` — single-knob layout, custom AmountKnob painting, 30 Hz timer
- `Source/GraphData.h` — GraphFrame fields + SPSC FIFO (drop-oldest)
- `Source/GraphComponent.h` / `.cpp` — 512-column history, real-data paint
- `Tests/DSPRegressionTests.cpp` — Phase 3 suite
- `Tests/Phase5TimingTests.cpp` — Phase 5 timing suite
- `Tests/run_all.sh` — combined runner (3 suites)
- `PHASE5_REPORT.md` — measured recovery data (50→288 ms, 150→640 ms, 400→1616 ms at default mapping)

Inspection conclusions (no guessing):

- Amount enters the engine as `depthDb` per block; timing constants were compile-time constants.
- Detector release (150 ms) and gain release (120 ms) are separate internal stages with separate coefficients and direction rules.
- The editor had exactly one custom knob; a second knob fits the existing `ProdKnob` pattern.
- APVTS state: `parameters.state.writeToStream` in `getStateInformation`, no versioning.
- No state versioning existed.
- The graph receives `gainReductionDb` from `duckEngine.getCurrentReductionDb()` per block — a variable Release flows through automatically with no graph changes.

## 2. FILES CREATED / MODIFIED

Created:
- `Tests/Phase6Tests.cpp` — Phase 6 suite (35 checks, compiles the real processor headlessly)
- `Tests/MANUAL_HOST_TEST.md` — Logic + Ableton manual verification guide (user verification required)

Modified:
- `Source/DuckEngine.h` — release constants renamed to `kDetectorReleaseMsDefault` / `kGainReleaseMsDefault`; new runtime API `setReleaseTimes(detMs, gainMs)`, `getDetectorReleaseMs()`, `getGainReleaseMs()`, `releaseToFactor(ms)`, private `updateCoefficients()`; Release-mapping block documenting factor semantics (150 ms → factor 1.0 = Phase 5 default)
- `Source/PluginProcessor.h` — class now also derives from `APVTS::Listener`; added `releaseRawParameter`, `applyReleaseToEngine()`, `parameterChanged()`, applied-release members; header docs updated
- `Source/PluginProcessor.cpp` — `release` parameter added (50–1000 ms, skew 0.3 centred 200 ms, default 150 ms); constructor wires listener + `applyReleaseToEngine()`; `prepareToPlay` re-applies release after engine prepare; `getStateInformation` stamps `stateVersion = 1`, matches each PARAM child by id (bug fix), and additionally calls `Timer::callPendingTimersSynchronously()`; `setStateInformation` stamps missing version and re-syncs the engine; `createEditor` gained a `SIDECHAIN_HEADLESS_TEST` guard so tests can link the processor without the GUI
- `Source/PluginEditor.h` / `.cpp` — `AmountKnob` generalised to `ProdKnob`; Release knob added (smaller, gold accent, tooltip "Recovery time - how quickly the ducked signal returns", double-click resets to 150 ms, 180-step drag sensitivity); two-knob layout (Amount large left, Release smaller right) with value/caption labels; ms display rounded to 10 ms
- `Tests/run_all.sh` — added Phase 6 suite (compiles `../Source/PluginProcessor.cpp` headlessly with the test)
- `SideChain.jucer` — unchanged (no new source files; test-only additions live under `Tests/`)

## 3. RELEASE PARAMETER

- **ID:** `release` (stable, lowercase, APVTS-managed)
- **Name:** "Release" (host-facing); UI caption "RELEASE"
- **Range:** 50–1000 ms, `NormalisableRange` skew 0.3 with `setSkewForCentre(200)` — the musically dense 50–400 ms region gets most of the knob travel; 0.1-step-free continuous range
- **Default:** 150 ms (exact Phase 5 DSP default)
- **Display unit:** milliseconds ("150 ms"), UI rounds to the nearest 10 ms
- **Mapping:** `factor = releaseMs / 150`; detector release = `150·factor` (= releaseMs), gain release = `120·factor` (= releaseMs·0.8). Preserves the validated 150:120 ratio exactly; factor 1.0 reproduces Phase 5 bit-for-bit
- **Host automation:** native APVTS `AudioParameterFloat` — normalised 0–1 automation, smooth, no custom code (verified by sweep test)
- **State persistence:** saved as a normal APVTS PARAM child inside the versioned ValueTree; restored via `replaceState` + `applyReleaseToEngine()`

## 4. RELEASE DSP

- **Detector release behavior:** scales the hybrid envelope's fall τ. Dominates the *start* of the recovery cascade (linear domain across the 36 dB trigger window)
- **Gain release behavior:** scales the final gain's rise τ. Dominates the *tail* of the recovery
- **How Release maps to each:** both scale by the same factor (`releaseMs/150`), preserving their ratio (option C in the design menu, "proportionally controls both"). Evidence for the choice:
  - Option A (detector only): gain tail stays 120 ms regardless — Release would stop having audible effect once the detector recovered, poor control resolution at long settings
  - Option B (gain only): detector still bridges rapid triggers at 150 ms regardless, contradicting "short = tighter pumping"
  - Option C: both stages scale; measured recovery is monotonic and musically proportional across the whole range (see below)
- **Minimum recovery:** Release 50 ms → recovery t63 ≈ 221 ms measured through the real processor (≈ 4.3× the nominal value, the documented cascade amplification)
- **Maximum recovery:** Release 1000 ms → still recovering after 1 s (t63 beyond the test window; measured ≈ 4.3 s by extrapolation, consistent with Phase 5's 640 ms @ 150 ms)
- **Reason for chosen mapping:** measured linearity (50 → 221 ms, 150 → 543–640 ms, 400 → 1616 ms, 1000 → >4000 ms ≈ linear ×4.3), settled depth proven invariant across all settings, and the shortest setting (50 ms) still smoothing the detector enough to avoid chatter. 50–1000 ms covers tight EDM pumping to long ambient sidechaining; the skew makes the musical centre comfortable to dial

## 5. AMOUNT

- **ID:** `sidechainAmount` (unchanged)
- **Range:** 0–100 % (unchanged)
- **Default:** 50 % (unchanged)
- **Existing mapping preserved:** yes — `depthDb = −60·amount^1.8`; verified unchanged by Phase 3 suite (32/32) and by the Phase 6 matrix (settled depth at Amount 100 %: −59.0/−59.1/−59.2 dB across Release 50/200/1000 ms — depth independent of Release)

## 6. STATE

- **State version:** 1 (`stateVersion` attribute on the root ValueTree, stamped on every save; stamped to 1 on load if missing)
- **Amount saved:** yes (APVTS PARAM child, unchanged mechanism)
- **Release saved:** yes (same mechanism)
- **Old-state compatibility:** tested — a pre-Phase 6 tree without `stateVersion` and without a `release` PARAM loads safely; Release falls back to its 150 ms default; unknown future properties are ignored by APVTS (no crash path)
- **Immediate-save behavior:** both parameters are captured on an immediate save. Two production fixes were needed (see §19): the flush loop now matches each PARAM child *by id* (previously it only inspected the first child, which happened to be Release), and it additionally calls `Timer::callPendingTimersSynchronously()` because APVTS's flush timer delivers its callback via a message that `runDispatchLoopUntil` alone does not reliably pump in host processes. Verified by the Amount=73/Release=400 immediate-save regression test

## 7. AUDIO TESTS

All in `Tests/Phase6Tests.cpp` (35/35 PASS), driven through the real `SideChainAudioProcessor`:

- **Release minimum (50 ms):** recovery t63 = 221 ms; full recovery within the window
- **Release mid (150 ms):** recovery t63 = 543 ms (engine-only reference: 640 ms; the processor-path value differs slightly because the harness infers gain from output/input — within tolerance)
- **Release maximum (1000 ms):** still recovering after 1 s of silence (clearly slower, monotonic ordering 50 < 150 < 1000)
- **Amount + Release matrix:** 4 amounts × 3 releases = 12 runs; settled ducked depth per Amount: 25 % → −4.9 dB, 50 % → −16.9/−17.0/−17.0 dB, 75 % → −35.2/−35.2/−35.3 dB, 100 % → −59.0/−59.1/−59.2 dB across Release 50/200/1000 ms
- **Recovery behavior:** monotonic in Release at every Amount; short = fast, long = slow
- **Depth stability:** depth identical across Release values (±0.3 dB, within tolerance); Release never changes depth
- **Sidechain disabled:** pure passthrough at both Release extremes (gain exactly 1.0)
- **Sidechain silent:** unity gain, no artifacts, at both extremes
- **Leakage:** main silent + loud sidechain → output silent (< 1e-6) at both extremes
- **Stereo:** shared-gain runs finite and recovering at both extremes (1000 ms recovery threshold adjusted for the 4.3× cascade)
- All runs: no NaN/Inf, no zipper steps > 0.25, output never above unity

## 8. AUTOMATION

- **Amount:** unchanged APVTS automation; regression covered by Phase 4 sweep test (still passing)
- **Release:** full 50→1000 ms block-rate sweep through `setValueNotifyingHost` while audio runs: gain finite and within [0,1] at every sample, no discontinuities (max adjacent-sample step well under 0.3)
- **Smoothness:** coefficient updates happen once per parameter change on the message thread; the engine's per-sample smoothing makes even block-rate automation click-free. No custom automation code was added

## 9. GRAPH

- **Real DSP data:** unchanged — audio thread pushes one real frame per block; GUI drains at 30 Hz
- **Release reflected:** automatically, because the engine's gain reduction now follows the Release setting. Verified: +300 ms after a 50 ms trigger, residual GR is −1.1 dB at Release 50 ms vs −17.1 dB at Release 1000 ms through the real processor
- **Temporal behavior:** trigger→GR→output ordering preserved at every Release (Phase 5's ordering suite still passes unmodified; the Phase 6 graph test asserts the release-dependent residual difference)
- **Any issues:** none. No fake animation, no second graph system, no GraphData changes

## 10. UI

- **Amount control:** large custom `ProdKnob` (~120 px), mint value arc, caption "SIDECHAIN AMOUNT", value label "50 %", tooltip "Ducking intensity - how much the main signal ducks", double-click resets to 50 %
- **Release control:** smaller custom `ProdKnob` (~84 px), soft-gold value arc, caption "RELEASE", value label "150 ms" (10 ms rounding), tooltip "Recovery time - how quickly the ducked signal returns", double-click resets to 150 ms
- **Visual hierarchy:** Amount clearly primary (larger, centred-left, standard accent); Release clearly secondary (≈ 70 % size, offset right, distinct warm accent). Both share identical painting language
- **Final editor size:** 800 × 500 (unchanged)
- **Overall design changes:** single-knob layout became a two-knob control row; interaction conventions are standard JUCE (vertical drag, fine drag via 180-step sensitivity, double-click reset). No help panels, one short tooltip per knob

## 11. MANUAL HOST GUIDE

- **Logic guide:** `Tests/MANUAL_HOST_TEST.md`, Part 1 — 15 numbered checks with expected observations, including recording the exact wording of Logic's sidechain selector
- **Ableton guide:** same file, Part 2 — 10 numbered checks, including recording the exact wording of Live's aux routing entry
- **User verification status:** MANUAL USER VERIFICATION REQUIRED — not yet performed. The guide is written for a non-programmer and asks for checklist marks plus the two "exact wording" fields

## 12. LOGIC PRO

- **Installed:** Yes (12.0.1)
- **Agent GUI tested:** No — interactive GUI automation of Logic is not reliably available in this environment
- **Result:** Not automatically verified. Automated evidence available: `auval` (system-level AU host) enumerates the AU with Input/Sidechain/Output buses all present and stereo, and validates the unit (§15). Logic presents AU sidechain inputs via the standard plugin-header sidechain selector; the exact wording must be recorded by the user
- **Manual user test required:** Yes — Part 1 of `Tests/MANUAL_HOST_TEST.md`

## 13. ABLETON LIVE

- **Installed:** Yes (11 Suite)
- **Agent GUI tested:** No — Live offers no scriptable way to load a VST3 and configure aux routing headlessly
- **Result:** Not automatically verified. Automated evidence: `HostCheck/VST3BusCheck` confirms the kAux stereo sidechain bus exists, is named "Sidechain", and is inactive by default (§16)
- **Manual user test required:** Yes — Part 2 of `Tests/MANUAL_HOST_TEST.md`

(REAPER: not installed, not tested, not installed per instructions.)

## 14. TEST SUITES

- **Phase 3:** 32/32 PASS (unmodified expectations)
- **Phase 4:** 13/13 PASS (unmodified)
- **Phase 5:** 24/24 PASS (unmodified — default Release reproduces the validated DSP exactly, so the fixed-constant timing tests remain valid as-is)
- **Phase 6:** 35/35 PASS (new)
- **Combined:** `./Tests/run_all.sh` → `ALL SUITES PASSED` (104 checks)
- **Exit code:** 0

## 15. AU

- **Debug:** BUILD SUCCEEDED
- **Release:** BUILD SUCCEEDED
- **auval:** `auval -v aufx SdCh Musc` → **AU VALIDATION SUCCEEDED**
- **Bus topology:** Input (stereo) + Sidechain (stereo) inputs, Output (stereo) — unchanged

## 16. VST3

- **Debug:** BUILD SUCCEEDED
- **Release:** BUILD SUCCEEDED
- **Main bus:** Input bus 0, kMain, stereo, default-active
- **Sidechain bus:** Input bus 1, kAux, stereo, "Sidechain", NOT active by default
- **Output bus:** Output bus 0, kMain, stereo
- (`HostCheck/VST3BusCheck`: PASSED, 0 failures)

## 17. ARCHITECTURES

- **AU Universal:** `lipo -archs` → `x86_64 arm64`
- **VST3 Universal:** `lipo -archs` → `x86_64 arm64`

## 18. PERFORMANCE

- **CPU:** 10 s stereo main + stereo sidechain, block 512, 48 kHz, Release -O2: 3.60 ms → 2779× realtime, **0.036 %** of one core — identical to the Phase 5 baseline. Release adds no per-sample cost (coefficients recompute only on parameter change)
- **Allocations:** none in processBlock (verified by design review; the only new per-block work is an atomic load of the Release value)
- **Locks:** none
- **Issues:** none

## 19. PROBLEMS FOUND

All found and fixed during this phase:

1. **`prepareToPlay` silently reset Release (fixed).** `duckEngine.prepare()` recomputes all coefficients from defaults, so a host calling `prepareToPlay` after the user set Release would revert to 150 ms while the UI showed the user's value. Fix: `prepareToPlay` now calls `applyReleaseToEngine()` after `prepare()`. Caught by the graph-ordering test failing at Amount 100 %.
2. **State flush only inspected the first PARAM child (fixed).** The Phase 4 immediate-save workaround read `getChildWithName("PARAM")`, which returns the *first* child. With two parameters the first child is now `release`, so Amount was never actually checked — and a fresh APVTS could serialize stale values. Fix: the loop matches each parameter's own PARAM child by its `id` property.
3. **Lazy APVTS flush timer needed an extra pump (fixed).** APVTS's flush timer callback is delivered as a message; `runDispatchLoopUntil` alone did not surface it in all cases. `getStateInformation` now also calls `Timer::callPendingTimersSynchronously()`. Verified by the Amount=73/Release=400 immediate-save test.
4. **`stateVersion` was stamped on load but never on save (fixed).** Freshly saved states carried no version. Fix: `getStateInformation` sets `stateVersion = 1` before serializing.
5. **Test-harness calibration (test-only):** several initial Phase 6 expectations were wrong, not the code — gain inferred as out/in ripples at sine zero-crossings (averaged), block counts vs sample indexes confused in one probe, the Release-parameter test initially forgot that Amount defaults to 50 % (capping GR at −18 dB), and the 1000 ms recovery threshold ignored the documented 4.3× cascade. All adjusted with explanatory comments; no coverage weakened.

## 20. PHASE 6 CONCLUSION

**GO.**

Release is a production parameter with a stable ID, a musically chosen 50–1000 ms range with a skewed scale, a documented proportional mapping into both internal release constants that exactly reproduces the validated Phase 5 default, and measured monotonic recovery behavior. Amount remains untouched as the sole depth control and its mapping is proven independent of Release. Both parameters automate smoothly, state is versioned and round-trips including old-state compatibility and immediate-save, the graph reflects the real DSP with no changes to the transport, the UI adds Release as a clearly secondary knob without clutter, the manual host guide exists for both hosts with honest "not automatically verified" status, all 104 regression checks pass in one command with exit 0, AU and VST3 validate with unchanged bus topologies, both remain universal, CPU is unchanged at 0.036 %, and the audio thread remains allocation- and lock-free. No presets, no Windows, no packaging, no unrelated projects touched.

## 21. PHASE 7 RECOMMENDATION

Phase 7 should consolidate quality and real-world confidence:

1. **Execute the manual host verification** (Parts 1–2 of `Tests/MANUAL_HOST_TEST.md`) and fold the recorded observations — especially the exact sidechain-selector wording in Logic and Live — into the project docs. This is the single largest outstanding evidence gap.
2. **Fix the residual Release-range feel question with listening data:** the 4.3× cascade amplification means "50 ms" sounds like ~220 ms recovery. Consider whether the user-facing scale should be re-labelled (e.g. relabel the knob's endpoints to the *measured* recovery, or re-map so the knob reads perceived recovery). Decide with the host-listening session, not speculatively.
3. **Add a graceful load test with corrupted state** (truncated/garbage `setStateInformation` input) to harden the versioned-state path before presets arrive.
4. **Pre-preset groundwork:** parameter grouping (`AudioProcessorParameterGroup`) and a stable parameter ordering contract, so the preset system in a later phase has a fixed layout to target.
5. **Optional polish:** remember-and-restore editor size, and keyboard fine-adjust on the knobs (arrow keys), both standard host expectations that cost little.
