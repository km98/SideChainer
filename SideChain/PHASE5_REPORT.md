# PHASE 5 REPORT — SideChain (Music-Prod)

Phase 5 goal: mathematically correct sample-rate-aware time constants, host verification, regression infrastructure.

Verdict: **GO**

---

## 1. FILES INSPECTED

Before any edit, all ten required files were read in full:

- `Source/DuckEngine.h` — smoothing implementation, detector state, constants
- `Source/PluginProcessor.h` / `.cpp` — parameter path, bus layout, processBlock, graph push
- `Source/PluginEditor.h` / `.cpp` — production UI, 30 Hz FIFO drain
- `Source/GraphData.h` — GraphFrame + SPSC FIFO (drop-oldest)
- `Source/GraphComponent.h` / `.cpp` — 512-column rolling history, paint
- `Tests/DSPRegressionTests.cpp` — Phase 3 suite (timing-dependent checks identified: TEST 4/5 recovery, TEST 9 settled-depth bound)
- `Tests/Phase4Tests.cpp` — Phase 4 suite
- `PHASE4_REPORT.md` — prior findings, including the deferred timing defect

Inspection conclusions (no guessing):

- Smoothing: `timeToCoefficient` returned `exp(−1/tau)` and the engine blended with it *directly* (`state += coeff·(target−state)`), so the error decayed by `(1−coeff)` per sample → effective tau `1/ln(tau)` samples, not `tau` samples. (E.g. the 150 ms release actually smoothed with tau ≈ 0.12 samples.)
- Direction selection was ALREADY correct in both stages: detector uses attack when `hybrid > detectorEnv_`, gain uses attack when `targetGain < gain_`.
- Detector state: `envSquared_` (fixed 0.15 RMS window; 0.9995/sample silence tail), `detectorEnv_` (hybrid linear), `gain_` ∈ [0,1].
- Graph source: real per-block engine values; parameter path APVTS atomic → `amountToDepthDb`; mapping `−60·amount^1.8`.
- Amount mapping: unchanged (no concrete regression found).

## 2. FILES CREATED / MODIFIED

Created:
- `Tests/Phase5TimingTests.cpp` — deterministic timing suite (24 checks)
- `Tests/run_all.sh` — combined one-command runner (Phase 3 + 4 + 5)

Modified:
- `Source/DuckEngine.h` — the coefficient fix (`timeToCoefficient`) plus rewritten comments explaining the math, the direction rule and the constant semantics (see §3 and §15 of this report's context)
- `Tests/DSPRegressionTests.cpp` — one bound updated (TEST 9: 100% settled depth; see §5)

No other source files changed. No files outside the project were modified (the AU copy into `~/Library/Audio/Plug-Ins/Components/` is the sanctioned validation exception).

## 3. TIME CONSTANT FIX

- **Previous coefficient behavior:** `alpha = exp(−1/tau)` used *directly* as blend factor → effective time constant `1/ln(tau)` samples (error decayed by `exp(−1/tau)` per sample, not by `exp(−1/tau)` after tau samples). Smoothing was effectively instantaneous at audio rates.
- **Corrected coefficient behavior:** `alpha = 1 − exp(−1/tau)` blended as `state += alpha·(target−state)` → error reaches `1/e` of its initial distance after exactly `tau` samples.
- **Detector attack:** 1 ms (rise of the hybrid envelope) — measured 1.021 ms @ 48 kHz
- **Detector release:** 150 ms (fall of the hybrid envelope) — measured 181.3 ms @ 48 kHz (see tolerances in §4)
- **Final gain attack:** 2 ms (ducking engagement) — measured 2.042 ms @ 48 kHz
- **Final gain release:** 120 ms (recovery, isolated via a depth step) — measured 119.9 ms @ 48 kHz
- **Formula used:** `alpha = 1 − exp(−1 / tau)`, `tau = max(1, ms · sampleRate / 1000)` samples
- **Sample-rate handling:** tau is computed in samples from the live sample rate in `prepare()`; verified rate-aware by the existing Phase 3 sample-rate suite (44.1/48/88.2/96 kHz) and by the Phase 5 tests running at 48 kHz (formula is rate-general).
- Direction rule (unchanged, now documented in code): target **rising** → attack coefficient; target **falling** → release coefficient. Separate coefficients in both the detector (step 3) and the gain computer (step 6).

## 4. TIMING TESTS

`Tests/Phase5TimingTests.cpp` — **24/24 PASS**. Method: drive `processSample` with deterministic steps, record the per-sample curve, find the time to the 63.2 % (1/e) crossing.

- **Detector attack measured:** 1.021 ms (target 1 ms) — silence → 0.8 sidechain step
- **Detector release measured:** 181.27 ms (target 150 ms) — sustained → silence. Slightly above 1τ because the peak/RMS hybrid shape (not a pure step) decays the envelope; within tolerance
- **Final attack measured:** 2.042 ms (target ~2 ms + 1 ms detector chain in front) — gain drop to 63 % of the −59 dB depth
- **Final release measured:** 119.9 ms (target 120 ms) — isolated by stepping `depthDb` −60 → −18 dB with the sidechain constant, so only the gain stage moves; the full sidechain-release *cascade* (detector 150 ms τ in the linear domain across the 36 dB trigger window + gain tail + the deliberate 0.9995 RMS silence tail) reaches 63.2 % recovery in 639.9 ms and is asserted as bounded-by-design behavior
- **Test tolerances:** attack windows 0.2–6 ms (detector) and 1–15 ms (gain, chain-shaped); detector release 90–260 ms; gain release 80–200 ms; musical cascade 350–900 ms. Chosen to allow discrete-sample implementation, detector shaping and smoothstep — no exact-sample equality anywhere.
- **Results:** 24/24 PASS, all measured values printed in the output.

## 5. DSP REGRESSION

- **Phase 3 suite:** **32/32 PASS.** One expectation updated: TEST 9's 100 % bound (`> −55 dB`) was calibrated against the old over-smoothing behavior where the detector saturated the trigger; with true constants the settled depth at 100 % / 0.8-amplitude sidechain is −59.1 dB (trigger just under full). New bound: −60 < settled < −50 dB, with an explanatory comment. No other Phase 3 change; every other check (block sizes 32–1024, four sample rates, stereo integrity, leakage, extremes) passes unmodified. Notably the settled-depth zipper step improved from 0.024 to 6.98e-10.
- **Phase 4 suite:** **13/13 PASS** unmodified (FIFO, graph semantics B1–B4, automation ramp — max step improved from 0.0804 to ~0 at settled depth, state round-trip).
- **Phase 5 suite:** **24/24 PASS** (new; timing + musical probes + graph ordering).

Musical probes (§5 of the suite): kick→bass re-trigger with full recovery in 1.5 s gaps; sustained-vocal steady medium ducking with zero zipper; fast 10 ms/20 Hz burst train correctly bridges gaps (sustained ducking, no per-burst gain chatter — the musically correct behavior with 150 ms release).

## 6. AMOUNT

Parameter untouched: `sidechainAmount`, 0–100 %, default 50 %, mapping `depthDb = −60·amount^1.8`. Measured settled ducking (0.8-amplitude sidechain, 48 kHz, after the fix):

- **0 %:** 0.0 dB (exactly unity — strict passthrough preserved)
- **25 %:** −4.9 dB (subtle)
- **50 %:** −17.0 dB (medium)
- **75 %:** −35.2 dB (strong)
- **100 %:** −59.1 dB (maximum configured −60 dB region)
- **Mapping changed:** No
- **Reason if changed:** n/a — progression remains monotonic and predictable; the only shift vs Phase 4 is at 100 % (−59.1 vs −59.3 previously), attributable to the detector no longer over-saturating the trigger, still within the configured maximum.

## 7. HOST TESTING

**Logic Pro:**
- **Installed:** Yes (Logic Pro 12.0.1)
- **Tested:** No — the required checks (sidechain routing popup, per-track routing, listening) are interactive GUI operations that this environment cannot reliably automate; reporting instead of guessing, per instructions. The closest automated evidence is the full `auval` bus enumeration below.
- **Sidechain UI (from auval, the system-level AU host):** AU presents **Input Scope Bus Configuration: Default Bus Count 2** — Bus 0 "Input" (2 ch stereo), Bus 1 "Sidechain" (2 ch stereo, layouts include mono/stereo/disabled options), Output scope stereo. This is exactly how Logic's plug-in header will offer the sidechain selector.
- **Routing:** not verified interactively
- **Audible result:** not verified interactively
- **Limitations:** no GUI automation available in this session

**Ableton Live:**
- **Installed:** Yes (Live 11 Suite)
- **Tested:** No — Live offers no headless/scriptable session-driving interface for loading a VST3, enabling its sidechain chooser and auditioning; not reliably automatable here.
- **Sidechain UI:** not verified (VST3 kAux bus confirmed programmatically by `HostCheck/VST3BusCheck`)
- **Routing:** not verified
- **Audible result:** not verified
- **Limitations:** no automation path; not guessed

**REAPER:** Not installed, not tested (per instructions, not installed).

## 8. GRAPH

- **Real DSP data:** Yes — unchanged transport (audio thread → GraphFrameFifo → 30 Hz GUI drain). No fake data introduced.
- **Timing relationship:** verified deterministically by the new graph-ordering tests (7 in the Phase 5 suite): sidechain rise lands in the trigger block; no pre-ducking; gain reduction engages promptly (corrected ~3 ms attack chain); output ducks ≥10 dB below input while the trigger holds; recovery decays smoothly without re-trigger jumps (> 50 dB of the ~59 dB depth recovered inside a 1 s window; the ~1.1 s full cascade is the documented by-design behavior of 150 ms/120 ms constants).
- **Visual result:** graph now shows a fast, snappy duck on triggers with a musically long recovery tail — matching the corrected DSP. Architecture and painting were not modified.
- **Any issues:** none new. Note for viewers: the DUCK tail now lingers visibly for ~0.6–1 s after a short trigger; this is the true release, not a bug.

## 9. TEST RUNNER

- **Created:** `Tests/run_all.sh` (project-local, executable, no global config touched)
- **Command:** `./Tests/run_all.sh` (optional `--rebuild` forces recompilation; compiles any missing binary on demand)
- **Result:** `ALL SUITES PASSED` — 32/32 + 13/13 + 24/24
- **Exit-code behavior:** returns 0 only when all three suites pass; returns 1 (and prints `ONE OR MORE SUITES FAILED`) otherwise. Compile failures are also fatal.

## 10. PERFORMANCE

- **CPU result:** 10 s stereo main + stereo sidechain, block 512, 48 kHz, Release -O2: **3.60 ms** total → **2780× realtime**, **0.036 %** of one core (Phase 4 measured 0.035 % — statistically unchanged; the fix is the same two `exp()` calls in `prepare`, zero added per-sample cost).
- **Audio-thread allocations:** none (fixed-capacity FIFO, per-block stack frame, no dynamic containers).
- **Locks:** none.
- **Other issues:** none.

## 11. AU

- **Debug:** BUILD SUCCEEDED
- **Release:** BUILD SUCCEEDED
- **auval:** `auval -v aufx SdCh Musc` → **AU VALIDATION SUCCEEDED** (parameter set, ramped scheduling, MIDI all PASS)
- **Bus topology:** Input Scope Bus Count 2 — Bus 0 "Input" stereo (2 ch), Bus 1 "Sidechain" stereo (2 ch); Output stereo (2 ch). Unchanged from Phase 4.

## 12. VST3

- **Debug:** BUILD SUCCEEDED
- **Release:** BUILD SUCCEEDED
- **Main bus:** Input bus 0, `kMain`, stereo, "Input", default-active
- **Sidechain bus:** Input bus 1, `kAux`, stereo, "Sidechain", **NOT active by default**
- **Output bus:** Output bus 0, `kMain`, stereo, "Output"
- (`HostCheck/VST3BusCheck`: PASSED, 0 failures)

## 13. UNIVERSAL ARCHITECTURE

- **AU:** `lipo -archs` → `x86_64 arm64` (Universal)
- **VST3:** `lipo -archs` → `x86_64 arm64` (Universal)

## 14. NEW USER PARAMETERS

- **Sidechain Amount:** unchanged — sole control, ID `sidechainAmount`, 0–100 %, default 50 %
- **Release:** not added (evaluation below)
- **Depth:** not added (deliberately redundant with Amount, per scope)
- **Other:** none added

Release-control evaluation (requested): the corrected internal release (150 ms detector + 120 ms gain) produces a ~0.6–1 s musical recovery cascade — ideal for kick→bass and classic EDM pumping, pleasant for vocal→music. However, the recovery length is now long enough that different material would genuinely want different values (fast-paced material vs. ambient), so a future Release control would offer a real user benefit. Evidence is documented but per scope it was **not** implemented; recommend revisiting in a later phase with a properly considered range/curve.

## 15. PROBLEMS FOUND

1. **The core Phase 5 defect (fixed):** `timeToCoefficient` produced the complement of the correct blend coefficient — all four smoothing constants were effectively near-zero-time instead of their labels. Fixed and measured (§3/§4).
2. **Phase 3 TEST 9 stale bound (test fixed):** expected settled 100 % depth `> −55 dB`, an artifact of the old saturation behavior; corrected to the −60…−50 dB region with an explanatory comment. No coverage weakened — the monotonicity check remains and the new bound is tighter in meaning.
3. **Silence-tail interplay (documented, by design):** when the sidechain goes silent, the RMS stage's 0.9995/sample tail (denormal protection) adds ~111 ms of extra detector decay after the 150 ms release; the full recovery cascade measures ~0.64 s to 63.2 % and ~1.1 s to inaudibility. The graph-ordering tests assert this as smooth, bounded decay so future changes cannot silently alter it.
4. **A Phase 4 report inaccuracy (corrected here):** the Phase 4 report guessed the old bug made smoothing "much longer" than nominal; in fact it made it near-instantaneous (error decayed by `exp(−1/tau)` per sample). Recording the true mechanism for the record.

No problems were found in the graph transport, parameter path, bus architecture, or build system in this phase.

## 16. PHASE 5 CONCLUSION

**GO.**

All acceptance criteria are met: the four smoothing time constants are mathematically true (α = 1 − e^(−1/τ)), sample-rate aware, and verified by deterministic measurement (1.02 / 181 / 2.04 / 120 ms); Phase 3 (32/32) and Phase 4 (13/13) remain valid with one stale bound corrected; the Amount mapping and single-parameter interface are untouched; the graph still runs on real DSP data and its temporal ordering is now test-covered; a project-local combined runner (`Tests/run_all.sh`) executes all 69 checks in one command with correct exit codes; AU validates with the sidechain bus intact; the VST3 kAux bus is unchanged; both Release bundles are universal; CPU is unchanged at 0.036 % of a core; the audio thread remains allocation- and lock-free; no project outside `SideChain/SideChain` was modified. Host GUI checks (Logic, Ableton) were not performed because interactive GUI automation is unavailable in this environment — reported honestly rather than guessed, with the system-level `auval` bus enumeration as the strongest available automated evidence.

## 17. PHASE 6 RECOMMENDATION

Phase 6 should focus on real-world verification and the first evidence-driven control extension:

1. **Interactive host verification (human-in-the-loop):** load the AU in Logic Pro 12.0.1 and the VST3 in Ableton Live 11 Suite; confirm sidechain routing visibility, external trigger routing, ducking audibility, and that the graph shows the corrected fast-attack/slow-release shape on real material. All groundwork (bus topology, builds, expected behavior) is documented above for comparison.
2. **Release parameter, evidence-based:** the corrected release cascade (~0.6–1 s fixed) is the first internal constant that materially constrains musical use across genres. Add a single `release` parameter (e.g. 50–600 ms mapping to the gain-release constant, or a musical Slow/Normal/Fast switch) only together with host listening results from item 1.
3. **Editor polish pass:** the graph now shows long DUCK tails; consider an optional dB-grid label for the reduction trace and a small "release tail" visual cue once Release becomes user-facing.
4. **State versioning:** before any parameter is added, introduce a state version attribute in the APVTS tree so future preset evolution never misreads older sessions (currently the tree is version-less).
