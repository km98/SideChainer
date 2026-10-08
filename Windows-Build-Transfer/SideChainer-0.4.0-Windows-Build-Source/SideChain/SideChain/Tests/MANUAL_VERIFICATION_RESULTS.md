# SideChain — FINAL VERIFICATION LEDGER

Status: **MANUAL VERIFICATION PENDING** (0.4.0 installed; real Logic editor/audio test unresolved)

Test machine: macOS 26.7, arm64 (M4 Pro) · Date started: 2026-09-27

## Build under test (exact identity)

| Item | Value |
|---|---|
| Installed AU | `~/Library/Audio/Plug-Ins/Components/SideChain.component` |
| AU binary md5 (original, has INFO defect) | `045bad84fde686cad418a1e211eb701c` |
| AU binary md5 (defect-1 fix, 0.2.0) | `19489587b830df8f6401ba5d1dbfd952` |
| **AU binary md5 (CURRENT, v0.2.1 refinement pass — use this one)** | **`f1b6f05c45d4cc20f1bd87b602340070`** |
| Installed VST3 | `~/Library/Audio/Plug-Ins/VST3/SideChain.vst3` (v0.2.1) |
| VST3 binary md5 (CURRENT) | `64e47fc297e3d3ea6ffbaea799132880` |
| Bundle version | **0.2.1** (CFBundleVersion + ShortVersion + auval `Component Version: 0.2.1 (0x201)` + `AudioComponents[0].version = 513`) |

## DEFECT 1 (found in Logic Pro manual test): INFO page controls missing

- **Status:** ROOT-CAUSED AND FIXED (2026-09-27)
- **Symptom:** opening INFO showed only the SideChain heading + Music-Prod
  logo; no SIGNED OUT / LOG IN / MUSIC-PROD+ content.
- **Root cause (confirmed by inspection + standalone reproduction):**
  `SideChainAudioProcessorEditor` constructed `infoPage` and toggled
  `setVisible()` on it, but NEVER called `addAndMakeVisible (infoPage)` and
  never assigned it bounds in `resized()`. The component was therefore never
  in the component hierarchy — `setVisible(true)` was a no-op and nothing
  painted. The heading/logo the user saw are drawn by the editor's own
  `paint()`/`paintOverChildren()`, which is why the page looked empty with
  just those two elements. A standalone harness proved the InfoPage itself
  was internally correct (8/8 controls, valid bounds) — the defect was
  entirely the editor integration.
- **Why it escaped automated testing:** all test suites build the processor
  headlessly (no editor sources linked), so the GUI had never been rendered
  before the first real host test. auval does not exercise custom editors.
- **Fix (minimal, 2 lines, `Source/PluginEditor.cpp` only):**
  1. constructor: `addAndMakeVisible (infoPage);`
  2. `resized()`: `infoPage.setBounds (getLocalBounds());`
- **Fix verification (harness on the real editor class):** infoPage is a
  child of the editor with bounds `0 0 800 520`; after clicking the INFO
  button the page is visible with **8/8 controls visible at nonzero bounds**
  (product title, version, MUSIC-PROD+ heading, account status, subscription
  status, detail label, action button, BACK).
- **Post-fix regression:** 197/197 checks exit 0; AU/VST3 Debug+Release build
  SUCCEEDED (no new warnings); auval `* * PASS`; VST3BusCheck 0 failures;
  universal `x86_64 arm64` both; CPU 0.057%. DSP/transport/presets/auth
  source untouched — the fix is GUI hierarchy only.
- **Logic Pro row 5.15 INFO check (and all rows using the old build) must be
  re-tested with the fixed build (md5 19489587…).**

---

## DEFECT 2 (found in Logic Pro manual test, v0.2.0 build `19489587…`):
## browser approval never flips the plugin to SIGNED IN + UI visual defects

- **Status:** ROOT-CAUSED, FIXED, REBUILT, REINSTALLED as **v0.2.1** (2026-09-29)

### 2A. Authentication state defect (FAIL — real logic bug, not timing)

- **Reproduction (user, Logic Pro, production auth):** browser approval page
  showed "Connect SideChain / Plugin connected / You can go back to SideChain
  now." while the plugin INFO page stayed at "MUSIC-PROD+ / WAITING FOR
  APPROVAL" with the device code and REOPEN LINK PAGE — indefinitely.
- **Root cause (source inspection, confirmed):** `AuthManager::run()`
  (`Source/MusicProdAuth.cpp`) only ever processed `logoutRequested` /
  `entitlementsRequested` flags. **The approval poll was never wired into the
  background thread.** The only poll path was `pollOnceForTesting()` — a test
  hook used exclusively by the mock-transport unit tests — and nothing in the
  plugin ever called it. So after LOG IN, `state` stayed `linking` forever:
  the browser approval genuinely succeeded server-side, but the plugin never
  asked the server again and could never observe it. No endpoint, product
  field, token matching or response-parsing defect existed; the plugin simply
  never polled. (This also could never show up in the automated suites:
  they drive polls explicitly via `pollOnceForTesting()`.)
- **Fix (minimal, `Source/MusicProdAuth.h/.cpp` + one line in
  `Source/PluginEditor.cpp`):**
  - `AuthManager::setBackgroundPolling (bool)` added. When enabled and the
    state is `linking`, the auth thread polls at the server-advised interval
    (from the start response's `interval` field) until approved/denied/expired
    or cancelled; `startLinking()` starts/wakes the thread,
    `cancelLinking()` wakes it to exit promptly.
  - The editor constructor now calls `setBackgroundPolling (true)` (the
    production setting).
  - Tests keep polling OFF and drive `pollOnceForTesting()` deterministically
    as before — no test race introduced.
- **Token storage re-verified:** token persists ONLY to
  `~/Library/Application Support/Music-Prod/SideChain/auth.json`; plugin
  (DAW) state contains no auth data (existing regression check 3.3/3.4
  still passes).
- **No backend change required or made.**
- **Automated proof (new regression tests, real background thread):**
  - 4.13 `setBackgroundPolling(true)` + `startLinking()` → mock approval
    arrives → state transitions to SIGNED IN with **no manual poll call**
    (exact reproduction of the defect, now passing).
  - 4.14 pending poll keeps LINKING; `cancelLinking()` exits to SIGNED OUT
    and the poll loop provably terminates (no further polls).
  - UI harness (`Tests/UIVerifyHarness.cpp` via `Tests/ui_verify.sh`, real
    editor + InfoPage classes): 10/10 — including "background approval ->
    SIGNED IN without any manual poll".

### 2B. Main-view UI defects (all fixed)

1. **Logo pasted-box:** the Music-Prod PNG asset is fully opaque (alpha 255
   everywhere) with its own near-black background (RGB 11,12,13), painted
   over the editor's blue-grey `#1a1d24` — a visible dark rectangle.
   **Fix:** the editor now derives an alpha mask from the asset's luminance
   once at first draw (white artwork = opaque, background = transparent) and
   draws that; the plugin background shows through. Asset file untouched.
2. **Duplicated "Music-Prod" text:** `paint()` drew a second "Music-Prod"
   string at `(getWidth()-220, 12)` which ran underneath/behind the INFO
   button (bounds `width-210, 8, 54×24`). **Fix:** the duplicate text draw is
   removed (the logo at the bottom is the single branding element). The INFO
   button is now laid out flush right `(getWidth()-14-54, 9, 54, 24)` with no
   text under it.
3. **PRESET caption overlap:** the caption label was a 10px string squeezed
   into a **5px-high** rect overlapping the selector's bottom edge.
   **Fix:** the caption is now drawn by the editor's `paint()` in a dedicated
   12px-high rect 4px below the selector (`presetCaptionBounds`), with 18px
   reserved in the layout — no stacking. The unused `presetCaption` label
   member is removed.
4. **Pure black background (requested direction):** `kBackground` changed
   `#1a1d24` → `#000000`; graph panel fill `#15181e` → `#000000` (edge rect
   still delineates the plot); InfoPage background likewise `#000000`.
   Knob track/body and control backgrounds adjusted one step so they still
   read on black. The dark Music-Prod aesthetic is preserved.

### 2C. INFO page

Same visual system (pure black). Verified layout order: BACK · SideChain ·
VERSION 0.2.1 · MUSIC-PROD+ · auth status · subscription status · detail ·
action button — 8/8 controls visible with non-empty bounds (UI harness).
Linking state shows device code + REOPEN LINK PAGE; signed-in state shows
SIGNED IN (AS …) + MUSIC-PROD+ ACTIVE/NOT ACTIVE + LOG OUT.

### 2D. Versioning

Patch bump **0.2.0 → 0.2.1** (bug-fix release, per project convention;
major/minor unchanged). Updated: `SideChain.jucer`,
`JuceLibraryCode/JucePluginDefines.h` (Version/VersionCode/VersionString),
`Builds/MacOSX/Info-AU.plist` + `Info-VST3.plist` (CFBundleVersion/Short),
`project.pbxproj` (`JUCE_APP_VERSION`/`JUCE_APP_VERSION_HEX`), and —
necessary — the AU `Info-AU.plist` `AudioComponents[0].version`
(512 `0x200` → 513 `0x201`; this is what auval reports and Logic's plugin
version display uses). NOTE: xcodebuild does NOT regenerate the built
bundle's plist from the template on every build; when bumping again, verify
`auval` prints the new `Component Version` and, if not, clean
`build/SideChain.build` and re-check the installed bundle's plist.

### 2E. Validation results (v0.2.1)

- Regression: **203/203 checks** across 6 suites, exit 0 (32+13+24+49+28+57;
  +6 new auth/background-polling checks)
- UI harness (real editor): **10/10** (`Tests/ui_verify.sh`)
- AU + VST3 Debug & Release: BUILD SUCCEEDED, no new project warnings
- auval `-v aufx SdCh Musc`: `Component Version: 0.2.1 (0x201)` · `* * PASS`
- VST3BusCheck: PASSED (0 failures; kMain/kAux/kMain intact)
- Universal: AU and VST3 `x86_64 arm64`
- CPU: 0.057 % of one core (1769× realtime)
- Installed: AU md5 `00fda6932c21ef7bed4975f2e66929eb`, VST3 md5
  `20f40a77e1f8e09c6e98cbe2275847cc`; AudioComponentRegistrar +
  AUHostingService restarted

### 2F. Required manual re-test (user, with md5 `00fda693…` / version 0.2.1)

- Section 4 auth flow: approve in browser → plugin MUST reach SIGNED IN
  within ~1 poll interval (~5 s; no DAW restart) → MUSIC-PROD+ line from the
  server response → LOG OUT returns to SIGNED OUT → re-login clean →
  persistence across plugin close/reopen.
- Visual: pure-black background, integrated logo (no box), no duplicate
  Music-Prod text near INFO, no PRESET caption overlap, clean INFO page.
- All sections 5/6 rows previously run against older builds must be repeated
  on this build.

---

## DEFECT 3 (v0.2.1 UI/branding): red/orange Music-Prod logo + rebrand to SideChainer

- **Status:** ROOT-CAUSED, FIXED, REBUILT, REINSTALLED (2026-09-29; version
  stays 0.2.1 — same-release UI defect-fix, no new release per convention)
- **Auth status: NOT touched and NOT regressed.** Zero changes to
  `MusicProdAuth.h/.cpp`, the polling loop, endpoints, product id, or token
  handling; the full auth suite passes 57/57 and the background-poll UI
  harness check ("background approval → SIGNED IN without any manual poll")
  still passes.

### 3A. Red/orange logo — exact root cause (TWO stacked bugs)

1. **HSB colour constructor misuse:** the luminance mask painted pixels with
   `juce::Colour (a, 1.0f, 1.0f, 1.0f)`. JUCE's 4-float `Colour` constructor
   is **(hue, saturation, brightness, alpha)** — the alpha was interpreted as
   HUE. The asset's dark background (a≈0 → hue 0 = red) painted as fully
   opaque red; artwork pixels tinted red/orange. This alone produced the
   exact red/orange rectangle the user saw.
2. **macOS CoreGraphics mask path:** even with a correct white mask,
   `drawImageWithin (…, fillAlphaChannelWithCurrentBrush = true)` routes
   through CoreGraphics `CGContextClipToMask`, which masks by the image's
   converted **greyscale luminance**, not its alpha channel (verified in the
   JUCE 6.1.3 source: `juce_mac_CoreGraphicsContext.mm::clipToImageAlpha`).
   White mask pixels → luminance 1 → fully opaque → a solid rectangle.
   Pixel-proof: the harness initially measured a 544px solid block; after the
   fix, alpha compositing yields the ~95px artwork only.
- **Fix (`Source/PluginEditor.cpp::drawMusicProdLogo`):** the mask is built
  as pure-white pixels whose ALPHA is derived from the asset's luminance
  (`Colours::white.withAlpha (a)`, asset file untouched), and it is drawn
  with the normal alpha-compositing path (`false`). High-resampling quality
  set for clean downscaled edges. Verified by pixel scan of a full editor
  snapshot: logo artwork ~95px wide, centred, white/neutral, **zero red
  pixels anywhere in the frame**, no rectangle.

### 3B. Product branding → "SideChainer" (wordmark, not a text label)

- **Reference:** the VYRE plugin presents its brand as bold, pixel-aligned
  interface artwork (branding baked into the panel, white on dark), with live
  text only for values. A distinct Music-Prod treatment was created — the
  VYRE logo/assets were NOT copied.
- **Implementation:** new shared header `Source/Branding.h`
  (`sid::branding::drawWordmark`): bold sans wordmark, two-tone — "Side" in
  primary white `#e8ecf3`, "Chainer" in Music-Prod mint `#7fd1c0` — with a
  thin rounded mint underline rule (45% alpha) hugging the baseline. Drawn
  with plain JUCE text (no new asset, no external dependency), crisp at any
  scale.
- **Main view:** the generic 20px "SideChain" title is replaced by the
  SideChainer wordmark (21px font height) in a deliberate header rect
  (14, 8 → 220×34) on the left, balancing the INFO button flush right
  (546→792). No duplicate Music-Prod text (removed in defect 2B).
- **INFO page:** the 34px generic "SideChain" label is replaced by the same
  wordmark at 34px, drawn in the reserved title rect. VERSION 0.2.1,
  MUSIC-PROD+, auth status, LOG IN/LOG OUT and BACK controls unchanged.
- **Identifier discipline (verified):** only user-visible branding changed.
  Unchanged: AU/VST3 names & subtype `SdCh`, auth product id `"sidechain"`,
  device_name `"SideChain"`, auth file path `Music-Prod/SideChain/auth.json`,
  class/file/project names, parameter IDs, factory/preset identifiers,
  tooltips. The UI harness (18/18) plus the full regression suite (203/203)
  confirm no behavioural change.

### 3C. Files changed

- `Source/Branding.h` — NEW: shared SideChainer wordmark helper.
- `Source/PluginEditor.cpp` — fixed logo mask colour + compositing path;
  header wordmark.
- `Source/InfoPage.cpp` — INFO wordmark (label reserved, text drawn in paint).
- `Tests/UIVerifyHarness.cpp` — pixel-level logo checks (white/neutral, zero
  red, size/centre) + wordmark presence checks; snapshot-based.
- Version/IDs untouched: still 0.2.1 everywhere.

---

## REFINEMENT PASS 1 (v0.2.1 UI/UX): ducking-curve preview · preset arrows · wordmark upgrade

- **Status:** IMPLEMENTED, BUILT, INSTALLED (2026-09-29; version stays 0.2.1 —
  UI refinement pass, no release bump per convention)
- **Auth/DSP/transport/presets behaviour: NOT touched.** Zero changes to
  auth, DSP, transport gate, parameter ranges or preset VALUES; the arrows
  drive the existing preset system, the preview reads the live parameters.

### R1. Ducking-curve preview (no-signal state) — `Source/GraphComponent.*`

- While the live history is entirely silent (no frames above the silence
  floor), the graph draws a dimmed mint PREVIEW of the CURRENT Amount/Release
  ducking shape: one duck-and-recover cycle (fast attack ramp, exponential
  recovery whose rate is governed by Release). Depth uses the DSP's MEASURED
  characteristic (`settledDepthDb`: ~59 dB at 100 % Amount, quadratic —
  PHASE7_REPORT §4), so the curve communicates real depth, not invented data.
  Clearly labelled "PREVIEW" (bottom-left, quiet tone).
- 0 % Amount → flat baseline (no ducking); 100 % → deepest curve. Preset
  selection changes the curve because presets set the same parameters.
- When live data exists, the live traces paint over the preview (preview is
  the lowest layer), and it stops repainting (no animation while playing).
- Driven by the editor timer from the LIVE parameter values (`setPreviewParams`),
  so host automation, undo and manual edits all update the shape.

### R2. Preset previous/next arrows — `Source/PluginProcessor.*` + editor

- New `SideChainAudioProcessor::stepPreset (±1)`: steps the FACTORY table
  through the same parameter-notification path as the dropdown (no second
  state mechanism). Anchoring from live values: exact preset → its index;
  "Default" (50/150) → before the first; any Custom → closest preset by
  Amount. Wraps at both ends of the table.
- Editor: `[◀] [ selector ] [▶]` group (22px DrawableButton arrows, custom
  vector triangles, Music-Prod control styling, tooltips), centred; the
  PRESET caption sits clearly below the group with real spacing. Arrows
  visible only on the main view (like the selector).
- Dropdown + arrows stay synchronised because both apply parameters and both
  refresh the value-derived display identity.

### R3. SideChainer wordmark upgrade — `Source/Branding.h`

- Now a constructed logo lockup: "Side" (heavy, white, slightly extended
  tracking) + mint divider bar + "Chainer" (heavy, mint, tightened tracking),
  letterforms drawn as glyph paths (crisp vector, exact spacing), plus a
  baseline hairline with a solid mint end-cap. Style language: black field,
  white type, restrained mint accents (Music-Prod.com design reference; no
  ChordEngine/VYRE assets copied).
- Used unchanged in both the main header (21px) and INFO page (34px).

### R4. Files changed

- `Source/GraphComponent.h/.cpp` — preview state/params + drawing + silence
  detection + `settledDepthDb`.
- `Source/PluginProcessor.h/.cpp` — `stepPreset` (reuses factory table +
  `applyPresetValues`).
- `Source/PluginEditor.h/.cpp` — arrows, layout group, caption spacing,
  preview wiring in the timer.
- `Source/Branding.h` — wordmark lockup upgrade.
- `Tests/UIVerifyHarness.cpp` — +10 checks (preview depth/shape response,
  arrow stepping incl. wrap + Custom anchor + identity sync, arrow glyphs,
  preview visibility).

### R5. Validation (current installed build)

- Regression: **203/203** exit 0 (auth suite 57/57 — auth untouched)
- UI harness (real editor): **31/31**
- AU + VST3 Debug & Release: BUILD SUCCEEDED · universal `x86_64 arm64`
- auval: `Component Version: 0.2.1 (0x201)` · `* * PASS`
- VST3BusCheck: PASSED (0 failures) · CPU: 0.063 % of a core
- Installed AU md5: **`f1b6f05c45d4cc20f1bd87b602340070`** · VST3 md5:
  **`64e47fc297e3d3ea6ffbaea799132880`**
- Registrar/hosting service restarted; **Logic must be reopened** to load
  this build.

### R6. Remaining MANUAL verification (user, in Logic with md5 `f1b6f05c…`)

- Wordmark reads as a logo; header clean; no overlap.
- Arrows visible/usable; stepping presets updates selector + curve; wrap works.
- Preview curve: 0 % flat · 50 % moderate · 100 % deepest · short vs long
  Release changes recovery shape · at least three factory presets differ
  visibly (Subtle shallow → Kick Pump/EDM/Deep pronounced → Extreme/100%
  Duck deepest).
- Live graph still primary when audio plays; no regression while stopped.
- Music-Prod logo still white/clean/centred; INFO branding SideChainer;
  auth state (SIGNED IN) preserved after reload.
| Auth state before test | signed out (`~/Library/Application Support/Music-Prod/SideChain/` does not exist) |
| Hosts | Logic Pro 12.0.1 (`/Applications/Logic Pro.app`) · Ableton Live 11 Suite (`/Applications/Ableton Live 11 Suite.app`) |
| Production endpoints | `https://wfpeajmdojcjqyrsnxbk.supabase.co/functions/v1/vyre-plugin-auth` · `https://music-prod.com/plugin/link` |

## Result legend

PASS / FAIL / NOT TESTED — every FAIL requires reproduction steps + evidence before any code change.

---

## 1. Automated verification — PASS (updated 2026-09-29 for v0.2.1)

- **203/203 checks** across 6 suites, exit 0 (32+13+24+49+28+57), `Tests/run_all.sh`
- UI harness (real editor + InfoPage): 10/10 (`Tests/ui_verify.sh`)
- AU Debug/Release, VST3 Debug/Release: BUILD SUCCEEDED
- auval `aufx SdCh Musc`: `Component Version: 0.2.1 (0x201)` · `* * PASS`
- VST3BusCheck: 0 failures (kMain in / kAux 'Sidechain' inactive-by-default / kMain out)
- Universal: AU and VST3 `x86_64 arm64`
- CPU: 0.057% of one core (1769× realtime)

## 2. Live backend verification — PASS (2026-09-27 22:33 CEST)

- `start product=sidechain` → 200, `product:"sidechain"`, `product_name:"SideChain"`, URLs `https://music-prod.com/plugin/link?code=…`
- Unknown product → 400 `unknown_product`; missing product → 400 `unknown_product`
- Legacy VYRE 2.0.1 (`device_name:"VYRE"`, no product) → 200, `/vyre/link`
- `poll` unapproved → `pending`; approve without session → 401; invalid token → 401; bogus code → 404; malformed body → 400; no 5xx

## 3. Live frontend verification — PASS

- `/plugin/link` live (200, new bundle `index--0ud_HHy.js`), renders "Connect SideChain", no VYRE branding
- `/vyre/link` live (200), VYRE page intact
- Page sends `product` on approve/deny and verifies server `product` match

## 4. Real interactive auth verification — **NOT TESTED** (user)

Procedure: see Phase 9 report / conversation "PART 1". Steps to record:

| # | Step | Result | Evidence |
|---|---|---|---|
| 4.1 | Plugin opens in DAW, INFO view reachable | | |
| 4.2 | INFO shows "SIGNED OUT" | | |
| 4.3 | LOG IN opens browser at `music-prod.com/plugin/link?code=…` ("Connect SideChain") | | |
| 4.4 | After sign-in + "Approve this plugin", page shows connected/"go back to SideChain" | | |
| 4.5 | Plugin flips to SIGNED IN (timer ≤ ~1 s) | | |
| 4.6 | Music-Prod+ line matches the account's real subscription | | |
| 4.7 | Close/reopen plugin → still SIGNED IN (token persisted) | | |
| 4.8 | LOG OUT → "SIGNED OUT"; auth file removed | | |
| 4.9 | LOG IN again → flow restarts cleanly | | |

## 5. Logic Pro manual verification (AU) — **NOT TESTED** (user)

| # | Check | Result | Evidence / exact host wording |
|---|---|---|---|
| 5.1 | Plugin loads (Audio Units → Music-Prod → SideChain) | | |
| 5.2 | Main input audio passes (sounding track through plugin) | | |
| 5.3 | Sidechain routing available (exact control wording: ______) | | |
| 5.4 | Trigger track routed → graph shows sidechain activity; main ducks | | |
| 5.5 | No sidechain leakage (solo output vs input) | | |
| 5.6 | Transport PLAY → sidechain active | | |
| 5.7 | Transport RECORD → sidechain active | | |
| 5.8 | STOP → sidechain inactive; gain returns smoothly; no click | | |
| 5.9 | SIDECHAIN WHILE STOPPED OFF → no ducking while stopped | | |
| 5.10 | SIDECHAIN WHILE STOPPED ON → ducking continues while stopped | | |
| 5.11 | Switch back OFF → stopped ducking stops again | | |
| 5.12 | Amount changes depth | | |
| 5.13 | Release changes recovery | | |
| 5.14 | Factory presets recall | | |
| 5.15 | INFO ↔ main view switch clean; graph keeps running | | |
| 5.16 | Auth flow (section 4) inside Logic | | |
| 5.17 | State persists after closing/reopening project | | |
| 5.18 | Stability: no crash/zipper/instability | | |
| 5.19 | Warnings/errors observed (exact text) | | |

## 6. Ableton Live manual verification (VST3) — **NOT TESTED** (user)

| # | Check | Result | Evidence / exact host wording |
|---|---|---|---|
| 6.1 | Plugin loads (Plug-ins → SideChain) | | |
| 6.2 | Main input audio passes | | |
| 6.3 | Sidechain routing available (exact control wording: ______) | | |
| 6.4 | Trigger routed → graph responds; main ducks | | |
| 6.5 | No sidechain leakage | | |
| 6.6 | PLAY → sidechain active | | |
| 6.7 | STOP → sidechain inactive by default; smooth recovery | | |
| 6.8 | SIDECHAIN WHILE STOPPED ON → works while stopped | | |
| 6.9 | Switch OFF again → stopped ducking disabled | | |
| 6.10 | Amount works | | |
| 6.11 | Release works | | |
| 6.12 | Presets work | | |
| 6.13 | INFO page + auth flow | | |
| 6.14 | State persists after close/reopen | | |
| 6.15 | Stability: no crash/zipper/instability | | |
| 6.16 | Warnings/errors observed (exact text) | | |

## 7. Defect protocol

Any FAIL: stop, record exact reproduction steps + evidence here, determine
whether SideChain code is actually the cause, report BEFORE patching. If all
pass: no code changes.

## 8. Final status

MANUAL VERIFICATION PENDING — flips to **FINAL VERIFICATION COMPLETE** when
sections 4, 5, and 6 are all PASS (or FAILs are triaged per §7).

---

# v0.3.0 DSP REDESIGN — Kick-Triggered Pumping (2026-09-29)

## R1. Motivation: old DSP behaviour was musically wrong

A real Logic Pro test (v0.2.x) showed the plugin behaving like a conventional
compressor, not the intended kick-triggered pumping effect.

**Old behaviour**: `DuckEngine::processSample` computed
`trigger = smoothstep(detectorEnv dB, -36..0)` — a continuous LEVEL FOLLOWER.
Any sidechain above −36 dBFS held `trigger ≈ 1` and ducked the main path
continuously (up to ~−60 dB), sustained tones included.

**Exact root cause of continuous ducking**: the trigger was a continuous
function of the *level*, not of an *onset*. Level-following + deep −60 dB
range = sustained ducking. The old automated tests (DSPRegression TEST 5
"sustained: settled ducking", Phase5 timing/depth, Phase4 depth) validated
this wrong model and were adapted alongside the redesign.

## R2. New trigger/detection model (DuckEngine v2)

- Fast envelope follower on the mono sidechain (max L,R): 2 ms attack /
  60 ms release (fixed; not user-facing).
- Onset detector: threshold **−18 dBFS**; a trigger confirms only when the
  envelope rises **≥ 8 dB within a 30 ms window** after crossing.
- Hysteresis re-arm: after a trigger the detector disarms and re-arms only
  when the envelope falls below threshold − 6 dB (−24 dBFS). Sustained
  energy between the two levels NEVER re-fires.
- Retrigger guard: minimum 60 ms between triggers.
- Result: one kick = ONE duck; sustained tone = at most one initial trigger
  (its onset), then zero further triggers and full recovery while the tone
  continues.

## R3. Envelope design (AHDS-style, per trigger)

- Attack: 1.5 ms tau to the target depth.
- Hold: 0.35 × Release, then release begins.
- Release: exponential, tau = Release parameter (50..1000 ms, default 150).
- Depth mapping (musical): `amountToDepthDb(a) = −24 dB × a^1.6` —
  50 % ≈ −8.7 dB, 75 % ≈ −15 dB, 100 % = **−24 dB max** (old range was ~−60 dB).

## R4. Offset / lookahead design

- New parameter `sidechainOffset` (AudioParameterFloat, −30..+30 ms, 1 ms
  steps, default 0). Positive = duck later, negative = duck earlier.
- Main path is delayed by a constant 30 ms lookahead ring buffer; the
  envelope is delayed by (lookahead + offset) samples, so the offset is real
  and audible on both sides of zero.

## R5. Latency / host compensation

- Constant reported latency: 30 ms (1440 samples @ 48 kHz, 1323 @ 44.1 kHz),
  reported to the host via `setLatencySamples()` in `prepareToPlay`
  (`latencySamplesForUi()`). PDC-safe; AU auval PASS, VST3 bus layout PASS.

## R6. Preset redesign (names kept, musical values)

| Preset | Amount | Release | change |
|---|---|---|---|
| Subtle | 15 % | 150 ms | – |
| Gentle | 30 % | 200 ms | – |
| Vocal Duck | 50 % | 300 ms | – |
| Bass Duck | 60 % | 120 ms | release 100→120 |
| Kick Pump | 75 % | 120 ms | release 100→120 |
| Deep Pump | 85 % | 250 ms | – |
| EDM Pump | 90 % | 400 ms | – |
| Hard Pump | 95 % | 150 ms | – |
| Extreme | 100 % | 700 ms | – |
| 100% Duck | 100 % | 150 ms | – |

## R7. Graph redesign

- Duck envelope drawn as a prominent mint fill+stroke labelled
  "DUCK ENVELOPE" from the actual delayed gain curve.
- Trigger tick marks rendered from a real per-block `triggerFired` flag
  (GraphFrame) driven by `getTriggerCount()` deltas.
- No-signal preview depth updated to the real mapping
  (`settledDepthDb = 24 × norm^1.6`).

## R8. Logo

`Branding.h` rewritten: "Side" (white, heavy) + chain-link connector (two
stroked rounded rects, mint) + "Chainer" (mint) + baseline hairline with
mint end-cap. Used in the editor (21 px) and INFO page (34 px).

## R9. Files changed (0.3.0 redesign)

- Source/DuckEngine.h — full v2 rewrite (detector/state machine/offset delay)
- Source/PluginProcessor.h/.cpp — offset param, delay line, triggerFired,
  stateVersion 3, latency reporting
- Source/PresetManager.h — musical preset values
- Source/GraphData.h, GraphComponent.h/.cpp — duck trace, ticks, new depth map
- Source/PluginEditor.h/.cpp — SIDECHAIN OFFSET row, layout, refresh
- Source/Branding.h — SideChainer chain-link wordmark
- Tests/TriggerDSPTests.cpp — NEW suite (57 checks)
- Tests/DSPRegressionTests.cpp, Phase4Tests.cpp, Phase5TimingTests.cpp,
  Phase6Tests.cpp, PresetTests.cpp, Phase8Tests.cpp, CPUBench.cpp — adapted
  to the transient model + latency-aligned gain measurement
- Version 0.3.0 in: SideChain.jucer, JucePluginDefines.h, Info-AU.plist
  (AudioComponents version 768), Info-VST3.plist, project.pbxproj, ui_verify.sh

## R10. Automated test results (2026-09-29)

- TriggerDSPTests (NEW): **57/57 PASS**
- DSPRegressionTests: **33/33 PASS** (adapted)
- Phase4Tests: **13/13 PASS** (adapted)
- Phase5TimingTests: **26/26 PASS** (rewritten for the new envelope)
- Phase6Tests: **48/48 PASS** (adapted, latency-aligned)
- PresetTests: **28/28 PASS** (values + latency-aligned)
- Phase8Tests: **57/57 PASS** (adapted, latency-aligned)
- UI harness: **31/31 PASS** (preview depth 50 %→7.9 dB, 100 %→24 dB)
- auval `-v aufx SdCh Musc`: **PASS**
- VST3BusCheck: **0 failures**
- CPU bench: **0.027 % of one core** (10 s stereo, 48 kHz)
- Release build (arm64+x86_64 universal): SUCCESS; Debug: SUCCESS

Build identifiers (installed, SHA-256):
- AU  `SideChain.component`: 1ece79536f430664d527e6b1fcc75a5ade87f6243e75526c7b6de1dd6aafae4a
- VST3 `SideChain.vst3`:     77ae56c564feea2dbda2decc88e2b0d623c8a8f12d6c0ffe4081ea920d9ef376

## R11. Remaining manual checks (user)

Musical behaviour is NOT marked verified until a real Logic Pro listening
test passes (same protocol as v0.2.1 sections 4–6):
kick pump at various Amounts; sustained pad/bass does NOT continuously duck;
Release feel; SIDECHAIN OFFSET ± ms audibly shifts the pump; preset sweep;
state round-trip; transport gate; auth flow unchanged.

---

# v0.3.0 CONTROL + GRAPH UX REFINEMENT (DUCK LENGTH + SIDECHAIN/ANALYZER views)

Date: 2026-09-30. Same 0.3.0 milestone (no version bump). Built on top of the
validated v0.3.0 trigger DSP: detector, envelope state machine, offset/lookahead
and transport gate are UNCHANGED.

## L1. DUCK LENGTH design

Inspection finding: in the v0.3.0 engine, `release` ALREADY drove most of the
duck duration (hold = 0.35 x Release, release tau = Release), and presets write
Release - so a naive second knob would either duplicate Release or be overridden
by presets.

Design adopted (no duplicate controls):
- NEW user parameter `duckLength` ("Duck Length"), 50..1000 ms, default 500 ms,
  skewed range (centre 200 ms). It is the TOTAL audible duck duration:
  attack + hold + 3 release taus ~= duckLengthMs (3 tau ~ 95 % recovered).
- `release` is REFRAMED as envelope SHAPE / preset character: it sets the hold
  fraction of the total length. `hold = holdFractionForRelease(release) x
  duckLength`, `tau = (duckLength - hold) / 3`. holdFraction maps 50 ms -> 5 %
  .. 1000 ms -> 50 % linearly.
- Defaults reproduce the validated v0.3.0 feel: derived hold 48.7 ms, tau
  150.4 ms at Length 500 / Release 150 (old: 52.5 / 150).
- PRESET INDEPENDENCE: presets set ONLY Amount + Release (applyPresetValues
  untouched); duckLength is never written by preset apply/step/Default. A user
  length survives any preset change (automated + manual assertions).
- Measured audible duck length (kick onset -> 95 % recovery): 60 ms -> 69 ms,
  150 -> 154, 300 -> 295, 600 -> 578, 1000 -> 954 ms (monotonic, ~linear).

## L2. Processor / state

- `kCurrentStateVersion` 3 -> **4** (schema change: new duckLength PARAM).
- State sync loop `paramIds[4]` -> `[5]` incl. "duckLength".
- setStateInformation: duckLength sanitised like the other params (missing in
  v1/v2/v3 states -> default 500 ms; out-of-range -> default).
- `parameterChanged` listens to "duckLength" -> `applyDuckLengthToEngine()`;
  also re-applied in ctor, prepareToPlay and after state restore (engine sync
  everywhere). Host automation + gesture-free restore (undo path) verified.
- Parameter id `duckLength` added AFTER `release` - existing ids untouched
  (AU/VST3 compatibility).

## L3. Graph views (SIDECHAIN default | ANALYZER)

- `GraphComponent::ViewMode { sidechain, analyzer }`, `setViewMode()`; DEFAULT
  is sidechain (also re-selected in the editor ctor).
- SIDECHAIN view: ONLY the ducking envelope (mint fill + stroke) + trigger
  tick marks. Live playback: real reduction history scrolls, multiple duck
  cycles visible, offset shifts the envelope vs the ticks (real DSP shift).
  Stopped/silent: labelled PREVIEW driven by live Amount (depth), Duck Length
  (cycle span, 300 ms -> full width, clamped) and Release (plateau/tail).
  Preview-vs-live distinction stays subtle (dim alpha + PREVIEW label).
- ANALYZER view: the previous multi-trace view (input/sidechain/duck
  fill+ticks/output + 0 dB/-60 labels) preserved unchanged.
- View switch is display-only: history, FIFO and DSP untouched (asserted
  bit-exact in DuckLengthTests).

## L4. UI layout

- View selector: compact segmented [SIDECHAIN | ANALYZER] TextButtons in the
  header, left of INFO (black bg, white type, mint selected, connected edges).
- Controls row is now a deliberate 3-knob hierarchy: SIDECHAIN AMOUNT (large,
  left) / DUCK LENGTH (centre) / RELEASE (SHAPE) (small, right); value labels
  and captions updated; graph remains dominant. Offset arrows, preset arrows,
  SideChainer wordmark, white Music-Prod logo: unchanged (UI harness re-verified).

## L5. Files changed

- Source/DuckEngine.h - duckLength state + mapping, holdFractionForRelease,
  derived-hold/tau diagnostics, header docs
- Source/PluginProcessor.h/.cpp - duckLength param + raw cache + listener +
  applyDuckLengthToEngine, state v4 (save sync + sanitised restore), docs
- Source/GraphComponent.h/.cpp - ViewMode, setPreviewParams(amount, release,
  duckLength), split SIDECHAIN/ANALYZER painting
- Source/PluginEditor.h/.cpp - view selector buttons + selectView, DUCK LENGTH
  knob row, layout rework, timer preview feed
- Tests/DuckLengthTests.cpp (NEW), Tests/run_all.sh (suite 8),
  Tests/Phase5TimingTests.cpp, Tests/Phase6Tests.cpp, Tests/PresetTests.cpp
  (adapted to the shape model + state v4)

## L6. Automated results (2026-09-30)

- run_all.sh (8 suites): DSPRegression 33/33, Phase4 13/13, Phase5Timing
  26/26, Phase6 48/48, PresetTests 28/28, Phase8 57/57, TriggerDSP 57/57,
  **DuckLengthTests 41/41 (NEW)** -> 303/303 PASS
- UI harness: 31/31 PASS (incl. preview depth 50 % -> 7.9 dB, 100 % -> 24 dB;
  logo + wordmark regressions green)
- Release build (arm64+x86_64): SUCCESS; Debug: SUCCESS
- auval `-v aufx SdCh Musc`: **PASS**
- VST3BusCheck: **0 failures** (kMain/kAux/kMain intact, sidechain inactive
  by default)
- CPU bench: **0.029 % of one core** (10 s stereo, 48 kHz)
- Latency: unchanged (lookahead 30 ms = 1440 samples @48k; reported constant)

Installed builds (SHA-256, CFBundleVersion 0.3.0):
- AU  `SideChain.component`: 4f11aaca89bf991c1b13ca985f6f12d7307d117cd32d9700ef8c7f4c5640cd64
- VST3 `SideChain.vst3`:     f4f53c41cb7a78e4a8e6ba84caa9d21be6d5d625277e3a271209190be0ccf7f8

## L7. Remaining manual checks (user, Logic Pro)

Same rule as R11: not complete until the real Logic test passes.
1. Open SideChainer - SIDECHAIN is the default graph view.
2. Graph shows the current duck envelope (PREVIEW while stopped).
3. Start playback with a kick sidechain - each kick draws a duck envelope.
4. Multiple duck cycles visible; graph scrolls naturally.
5. Switch to ANALYZER - old multi-trace view available; switch back - history
   coherent.
6. Select Kick Pump; change DUCK LENGTH - audible duck duration follows the
   knob; graph envelope widens/narrows live.
7. Very short (50-80 ms) and long (800-1000 ms) lengths sound sane.
8. Select another preset - DUCK LENGTH stays where the user set it.
9. LEFT/RIGHT offset moves the envelope relative to the trigger ticks.
10. Transport gate still works (no ducking while stopped unless
    SIDECHAIN WHILE STOPPED is on).

---

# v0.3.0 DEFECT INVESTIGATION (Logic report: "no ducking at all" + UI overlap)

Date: 2026-09-30. Installed build verified IDENTICAL to the local Release build
before any change (SHA-256 AU 4f11aaca…, VST3 f4f53c41…, CFBundleVersion 0.3.0).

## D1. Reproduction methodology (no assumptions)

Two independent probes were built:

1. `Tests/HostProbeAU.cpp` (`run_hostprobe.sh`) — loads the REAL INSTALLED AU
   from `~/Library/Audio/Plug-Ins/Components` through the JUCE hosting API
   (bus activation via setBusesLayout, PLAYING AudioPlayHead, block-wise
   processBlock). NO project code compiled in — this is what a host does.
2. `Tests/LogicFidelityProbe.cpp` — the full processor + REAL editor compiled
   in, reproducing Logic's opening sequence: editor constructed ON TOP of the
   processor, host state restored, user drags via the actual controls.

## D2. Result: NO code defect found in the ducking path

Every scenario PASSES with the exact installed binary:

- Clean-host hosting (real installed AU): defaults 48k/512 **53.1 dB duck**,
  44.1k/128 **64.4 dB duck**, Amount 100 % deep duck, state save/reload ducks.
- Logic-fidelity sequence: editor-open **7.9 dB duck** (Amount 50 % default =
  correct), state-restore-with-editor ducks, preset apply ducks (Kick Pump
  15.1 dB), knob drags reach parameters and engine (Amount 100 % → 24.0 dB),
  toggle + offset arrows work, graph frames carry real reduction + triggers.
- The full 8-suite regression (303 checks incl. TriggerDSPTests detector,
  TriggerDSPTests envelope, Phase5TimingTests, DuckLengthTests) passes.
- Signal-path trace (main bus → sidechain bus → detector → trigger → envelope
  → lookahead/offset → main delay ring → gain → output) verified at every
  stage; latency = 1440 samples @48k as designed.

## D3. Root-cause analysis (user side)

The reported symptom "nothing happens no matter what I do" with an EMPTY graph
(combined) matches a configuration/routing cause, not a DSP regression:

1. **Sidechain bus not routed (most likely).** The graph empty at the same
   time means no sidechain signal reached the detector at all (the plugin's
   PREVIEW would show the parameter curve otherwise; it showed nothing only
   while frames were absent or the parameter depth was 0). In Logic the
   sidechain must be re-selected in the plugin header's Sidechain menu AFTER
   replacing the plugin binary (AU caching can also keep a stale connection).
2. **Stale AU cache.** Logic/AUHostingService can keep the pre-update
   instance alive; `AudioComponentRegistrar` was killed before reinstall,
   but the user's Logic session must be restarted (Cmd+Q fully) to reload.
3. **Transport stopped** with SIDECHAIN WHILE STOPPED off (gate by design).
4. Note: three sidechain-themed plugins coexist in
   `~/Library/Audio/Plug-Ins/Components/` (`SideChain`, `IPlugSideChain`
   subtype 7Sdp, `Phase1 SidechainBusTest` subtype Scb1). Verify the track
   actually hosts "Music-Prod: SideChain" (SdCh).

## D4. Required user-side verification steps (Logic)

1. Fully quit Logic (Cmd+Q) and restart.
2. Remove the plugin from the track and re-insert "Music-Prod: SideChain".
3. In the plugin header, click the Sidechain menu and select the kick track
   bus explicitly.
4. Ensure transport PLAYING, Amount ≥ 50, OFFSET 0.
5. Play an isolated strong kick; each hit must produce one audible duck and
   a visible envelope + trigger tick in the graph.
If step 3's menu is missing/empty or the graph stays empty with steps 3–5
correct, report back — the next step would be a Logic-specific bus diagnostic.

## D5. Defect 2 — UI layout collision (REAL, confirmed and fixed)

Confirmed from the layout maths: at 800x520 the control row measured
knob 104/84/72 px + value labels + captions inside a 158 px area, so the
DUCK LENGTH caption landed inside the 26 px Music-Prod logo band.

Fix (PluginEditor.cpp `resized()` restructure):
- Reserved bottom band (30 px): Music-Prod logo strictly centred in its own
  rect; SIDECHAIN WHILE STOPPED moved onto the same band (left side) instead
  of floating above the logo.
- Control row budgeted: knob sizes 96/76/64 px are clamped by
  `controlArea.height − valueH − captionH − 4`, so knob + value + caption can
  never reach the logo band at any editor size.
- Graph remains dominant (still ~300 px tall).

Regression protection: UIVerifyHarness now asserts REAL component geometry
(friend accessors): DUCK LENGTH knob/value/caption, AMOUNT and RELEASE knobs
must not intersect the logo band nor each other and must be inside the editor
→ UI harness now 37/37.

## D6. Test + build results (2026-09-30)

- run_all.sh (8 suites, rebuilt): **303/303 PASS**
- HostProbeAU (real installed AU): **4/4 PASS**
- LogicFidelityProbe (editor-on-top sequence): **10/10 PASS**
- UIVerifyHarness (incl. new geometry checks): **37/37 PASS**
- Release + Debug universal (x86_64 + arm64): **BUILD SUCCEEDED**
- auval `-v aufx SdCh Musc`: **PASS**
- VST3BusCheck: **0 failures**
- CPU: **0.030 %** of one core; latency unchanged (30 ms lookahead)

Installed (SHA-256, CFBundleVersion 0.3.0 — same milestone, no bump):
- AU  `SideChain.component`: fdebcca744fcb84b38101abe780fe2b3196775d7d5def362d2ab98e7d7ccb5d2
- VST3 `SideChain.vst3`:     8fe6c8f262aff82aec28f655681f136ff6cc3a900fed2c264eef1eca83047ced

## D7. Remaining manual checks (user)

Full manual Logic test per the brief (isolated kick + loop, Amount 100,
Length 500, Release 150, Offset 0, PLAYING) AFTER the D4 steps. Musical
behaviour remains unverified until that passes.

---

# v0.3.0 DEFECT ROUND 2: invisible knobs (REAL, fixed) + no-ducking re-check

Date: 2026-09-30. Part A verification BEFORE any change:

## A. Installed build identity

- `~/Library/Audio/Plug-Ins/Components/SideChain.component` (subtype SdCh,
  CFBundleVersion 0.3.0), SHA-256 fdebcca7… = the previous round's build.
  Other installed AUs enumerated: `IPlugSideChain` (7Sdp), `Phase1
  SidechainBusTest` (Scb1), `HISE`, `ChordEngine`, `VYRE` — Logic must host
  "Music-Prod: SideChain" (SdCh).

## B. Defect 1 (invisible knobs): ROOT CAUSE FOUND AND FIXED

Reproduction (pixel + geometry probes):
- Snapshot analysis: knob-body pixels ~0, accent (mint/gold) pixels = 0 in the
  control row, while labels/values rendered normally.
- Direct knob probe: `AMOUNT: bounds=182 314 96 0` — ALL THREE knobs had
  **height 0** (DUCKLEN 76x0, RELEASE 64x0).

Exact cause: the previous layout fix constructed knob rects at (0,0) and then
called `.withTop(y)`. JUCE's `withTop` keeps the BOTTOM edge and moves the top,
collapsing the height to 0. Labels kept explicit heights, so values/captions
stayed visible — matching the report exactly. The graph had also shrunk to
178 px in the same change (explaining "graph is empty" impressions in the
stopped/preview state).

Fix (PluginEditor.cpp resized(), minimal): knob rects are now constructed
directly as (x, y, w, h) at their target position; value labels narrowed to
knob+40 (no adjacent collisions); control row 152 px; graph back to ~208 px
above it (dominant region).

Regression protection (UIVerifyHarness, now 43/43): all three knobs asserted
non-zero bounds AND non-zero rendered pixels (magenta-sentinel paint test:
AMOUNT 5493 px, DUCK LENGTH 3505 px, RELEASE 2532 px painted). Snapshot pixel
check after fix: knob-body 7420 px, mint-accent 480 px, gold 18 px present.

## C. Defect 2 (no ducking): still NO DSP defect reproduces

Re-ran the full host-level verification on the exact binary:
- HostProbeAU (real installed AU hosting): 4/4 — 53.1 dB duck defaults,
  64.4 dB at 44.1k/128, Amount 100 % deep duck, state reload ducks.
- LogicFidelityProbe (editor-on-top + state restore + real control drags):
  10/10 — triggers fire, envelope depth correct (24.0 dB at Amount 100),
  graph frames carry reduction + trigger flags.
- Bus topology via VST3BusCheck: kMain in / kAux 'Sidechain' (inactive by
  default) / kMain out — 0 failures; the probe also ACTIVATES the aux bus
  explicitly (as Logic does when a sidechain source is selected) and
  non-zero sidechain samples reach the detector.
- Transport gate: PLAYING probe active; STOPPED gating per design; toggle
  override verified.
- DUCK LENGTH chain: parameter -> engine verified; Amount 0/50/100 depth
  progression and length/shape behaviour covered by DuckLengthTests (41/41).
- Full regression: 303/303.

Conclusion: the no-ducking report remains inconsistent with every host-level
reproduction available. Since the knob defect WAS real and shipped, the
broken visuals (zero-height knobs, shrunken graph) plausibly dominated the
session report. The remaining possibility is Logic-session-specific (bus
routing selection lost on plugin replacement, or hosting the wrong AU from
the enumerated list). Per the brief: STOPPED code speculation here.

## D. Build / install

- Release + Debug universal: BUILD SUCCEEDED
- auval `-v aufx SdCh Musc`: PASS · VST3BusCheck: 0 failures · CPU: 0.028 %
- Installed (CFBundleVersion 0.3.0, no bump):
  - AU  f76df599363b50ce03eeb350b1eae4274ffcfa98490905e652cdd55c78af1ea3
  - VST3 b5b51166112e3df1a74e0f1109ea6bbe3316c5a2e9a09638ed699a5d9401053b

## E. Required real Logic verification (user)

1. Fully quit and restart Logic.
2. Re-insert "Music-Prod: SideChain" (SdCh) — NOT IPlugSideChain/Phase1.
3. Select the kick track in the plugin's Sidechain menu.
4. VERIFY VISUALLY FIRST: all three knobs now show bodies/arcs/pointers and
   the graph area is full-height (PREVIEW curve visible while stopped).
5. Then the standard listening test (Amount 100 / Length 500 / Release 150 /
   Offset 0 / PLAYING; one duck per kick, recovery between kicks).
If knobs are visible but ducking still fails, capture: the Sidechain menu
content and whether the graph shows any trigger ticks while playing.

---

# v0.3.0 LOGIC BUS DIAGNOSTIC (temporary instrumented AU installed)

Date: 2026-09-30. Real Logic result after correct setup: knobs visible (layout
fix confirmed), sidechain STILL not ducking, SIDECHAIN graph empty.

## 1. Static analysis (JUCE 6.1.3 AU wrapper + our processor) — no defect found

- Wrapper multi-bus input: `Render()` pulls EVERY input element via
  `pullInputAudio` -> `AUInputElement::PullInput` (fails only with
  kAudioUnitErr_NoConnection if the host never connects bus 1; the wrapper
  then zeroes that bus and continues). Channels reach the processor through
  `CoreAudioBufferList`, whose offsets are rebuilt from the processor's LIVE
  bus layout on every Initialize.
- Layout sync: `syncProcessorWithAudioUnit()` runs at Initialize and on
  stream-format changes; a host-enabled aux bus maps to bus 1 enabled.
- Transport: the wrapper's playhead `getCurrentPosition` reads
  `CallHostTransportState`; `isPlaying` maps 1:1 to our gate. No defect.
- Our processor reads bus 1 via `getBusBuffer(buffer, true, 1)` and gates on
  `isPlaying/isRecording` — semantics verified by Phase8Tests (57/57).
- Graph FIFO push is unconditional per block; an empty graph therefore means
  no reduction data was generated -> consistent with gate closed or no
  sidechain samples, NOT a graph-renderer defect.

## 2. Instrumented diagnostic AU (SDCH_DIAG=1) — INSTALLED

A `GCC_PREPROCESSOR_DEFINITIONS='SDCH_DIAG=1'` Release build writes one line
per ~0.5 s of audio to `/tmp/sidechain_diag.txt`:

  sr= blk= playhead= isPlaying= isRec= gateOpen= scCh= mainPk= mainRms=
  scPk= scRms= trig= minGain= grDb=

where scCh = sidechain channel count as seen by getBusBuffer, scPk/scRms =
pre-gate sidechain level, trig = total trigger count, minGain = current
envelope gain. The production binary contains none of this; the flag was
verified present via `strings` before install.

Installed diagnostic AU SHA-256:
  8a9c7afbf385a2b66fb29fbcb91a7c254943a6042021424db3b957df9c8d6bd3

## 3. How to run the diagnostic (user)

1. Fully quit Logic, restart it (AU caches were flushed on install).
2. Insert "Music-Prod: SideChain" on the main track; select the kick track
   in the plugin's Side Chain menu.
3. Amount 100, DUCK LENGTH 500, RELEASE 150, OFFSET 0, SIDECHAIN WHILE
   STOPPED off.
4. Record the Logic UI facts: exact Side Chain menu contents, selected
   source name, whether the selection persists after reopening the menu,
   plugin name Logic displays.
5. Press PLAY for ~10 seconds (kick + musical loop).
6. Run: `~/Documents/SideChain/SideChain/Tests/read_diag.sh`

The script prints the last lines plus an automatic interpretation:
- isPlaying=0          -> Logic playhead not reaching the plugin (gate issue)
- scCh=0               -> Logic never enabled bus 1 (source selection issue)
- scPk~0               -> bus active but silent (routing/kick level issue)
- scPk>0, trig=0       -> detector threshold not met (report scPk value)
- trig>0, minGain<1    -> plugin DSP IS ducking; issue is downstream (Logic
                          monitoring/output routing)

## 4. Decision rules (agreed)

- If scPk=0 / scCh=0: STOP all DSP work; the signal is lost between Logic's
  Side Chain selection and bus 1. Next step is a host-side fix/workaround
  decision (e.g. investigating the wrapper's bus-activation handshake).
- If scPk>0 and triggers fire: DSP proven inside real Logic; remaining
  failure is Logic monitoring/routing, not the plugin.
- No production code changes in this round beyond the flag-guarded
  diagnostic (verified: production recipe suites still 303/303; the
  flag-off binary contains no diagnostic code).

# DETECTOR ROOT-CAUSE FIX — Logic diagnostic round (0.3.0)

## T1. Diagnostic evidence (installed AU in real Logic, SDCH_DIAG=1 build)
- `scCh=2` — Logic DID enable + connect the sidechain aux bus.
- `scPk≈0.278 (−11.1 dBFS)`, `scRms≈0.129` — real kick signal reached the detector.
- `isPlaying=1`, `gateOpen=1` — transport gate open.
- `trig=0`, `minGain=1.000`, `grDb=0.0` — the DETECTOR rejected the kick. Bus/transport/graph exonerated.

## T2. Exact root cause (reproduced numerically before any change)
The original scheme required the fast follower to rise **+8 dB AFTER crossing the
−18 dBFS absolute threshold**. The follower's own 2 ms attack consumes most of the
kick's rise before that crossing, so the +8 dB evidence could never accumulate:
env crossed at −16.3 dB with a decaying tail behind it → window expired →
`armed_=false` → re-arm needed −24 dBFS which the kick tail delayed → next kick
arrived mid-state. Measured: 4 kicks at Logic level → **0 triggers** (zero-duck
regression reproduced in a standalone detector probe).

## T3. New detector (DuckEngine.h, 0.3.0 detector revision)
- FAST follower: unchanged (2 ms attack / release 60→**30 ms**, tau).
- SLOW BASELINE follower: 200 ms tau on the same mono level.
- Onset = BOTH of:
  (a) fast − baseline ≥ **+12 dB** (rise evidence vs pre-onset floor), and
  (b) fast − trailingFast(30 ms) ≥ **+10 dB** (slew evidence: the level must be
      GROWING now — rejects slow swells whose dB-rise-vs-baseline is a
      linear-ramp artifact).
- Absolute floor gate lowered −18 → **−26 dBFS** (noise-floor only; the rise
  checks do the musical work).
- Re-arm hysteresis: after the 60 ms guard, level must settle below
  **0.75 × trigger-time level** (always true inside the guard for decaying
  transients; impossible for sustained audio).
- Retrigger guard unchanged: 60 ms. Envelope/lookahead/offset/graph: UNCHANGED.

## T4. Results (DetectorTests.cpp, new suite; TriggerDSPTests unchanged assertions)
- Logic-measured kick (pk 0.278 / −11.1 dBFS): 0 → **1 trigger** + real duck.
- Kick matrix pk 0.10–0.95, sub-tail, soft-clipped, stereo: exactly 1 each.
- Trains: 2 kicks → 2; 4 kicks → 4; 16× 8th-note dense → 16 (controlled).
- Sustained tone/noise → 1 initial, full recovery; kick+pad → 2; swell → 0.
- 44.1/48/96 kHz all pass. Trigger latency ≈ 1–2 ms after onset.

## T5. Validation
- 9 suites: **325/325** (DSPRegression 33, Phase4 13, Phase5 26, Phase6 48,
  Preset 28, Phase8 57, TriggerDSP 57, DuckLength 41, **Detector 22**).
- HostProbeAU **4/4** (53.1 dB duck defaults; 44.1k/128 OK), LogicFidelityProbe
  **4/4 sections**, UI harness **43/43**, CPU **0.037 %** core, auval **PASS**,
  VST3BusCheck **0 failures** (bus 1 kAux stereo, defaultActive=no as designed).
- Release+Debug universal x86_64+arm64 BUILD SUCCEEDED. Version **0.3.0**,
  production binary contains no diagnostics.
- Installed: AU SHA-256 `90937890e995c6225ebc8da71206ab20d5e8ab268799c86f99c1bdf4817a61a8`,
  VST3 `01f0dce98d0ab1197d59ee6d9d8834c7cde9bc3681b8666fe4fe8e2f9e40d75c`.

## T6. Remaining manual check (user)
Restart Logic → confirm one duck per kick, SIDECHAIN view cycles, DUCK LENGTH
audibly changes duration, sustained sidechain does not continuously duck.

---

## Recovery investigation — 2026-09-30

Status: **Logic editor root cause NOT CONFIRMED; do not call release-ready.** The clean 0.4.0 AU/VST3 builds are installed and validated by auval/host probes. Logic has since been restarted and the user-selected Sep 30 auto-saved `LetItReign` project reopened; the SideChain host window is present, but its custom canvas is not exposed in Accessibility and actual rendering/pumping remain unverified.
This worktree is not a Git checkout (`git rev-parse` fails at the project,
parent, and Documents levels), so historical source/build comparison is
limited to the local manual ledger and files present on disk. No prior 0.3.x
AU/VST3 binaries were found in the project; the documented 0.3.x hashes are
historical evidence only and cannot be compared against a local known-good
build.

### Baseline identity and build metadata (before this recovery edit)

- Installed AU: `~/Library/Audio/Plug-Ins/Components/SideChain.component`,
  SHA-256 `7d9a582c0af396dc922da81c29887b984e1788adcb56f3d40f56a63c8b55fccf`.
- Installed VST3: `~/Library/Audio/Plug-Ins/VST3/SideChain.vst3`, SHA-256
  `d74d6ba4028dfcd36f2e0817bb2a4244047a664c40449e81a731071e9970cb41`.
- Both installed executables were byte-identical to the local Release
  executables before clean build; both were universal `x86_64 arm64` and
  bundles displayed 0.4.0.
- AU metadata discrepancy before correction: `auval -v aufx SdCh Musc`
  reported **0.3.0 (0x300)** although bundle keys said 0.4.0. Corrected in
  `SideChain.jucer`, `JucePluginDefines.h`, all six target-version settings
  in `project.pbxproj`, UI test macro, AU Component version and description.
  After installation, `auval` reports 0.4.0 (0x400), sees a Cocoa view and
  completes validation successfully. This drift alone did not prove the
  custom editor failure; no regression was isolated to the metadata.

- Host `getName()` and the AU's registered name are intentionally technical
  identity "SideChain" / "Music-Prod: SideChain"; the SideChainer wordmark is
  drawn inside the custom editor. Generic title text alone is not proof of a
  fallback editor.

### Logic reproduction attempt and limits

- Logic Pro was open on the user's project `LetItReign`, with a SideChain
  insert in channel strip `Piano Backed`. The host shell exposed host controls
  (Side Chain selector, bypass, compare, preset and title `SideChain`), but
  the AX tree did not expose the JUCE custom editor canvas. Therefore the
  title is not evidence that a fallback view was actually displayed.
- No project content was intentionally edited. After saving/recovery, the
  user explicitly authorized closing/restarting Logic; when Logic presented
  its recovery prompt, the user chose the auto-saved `LetItReign` version from
  2026-09-30 17:32 (rather than the 2026-08-07 saved version). The project
  reopened, and Logic's `Piano Backed` plugin window is now present with host
  controls and a `SideChain` title. Its accessibility tree exposes an 11-item
  AU host shell and a scroll area/table, but not the custom editor canvas or
  its pixels/controls. Thus the UI cannot yet be classified as rendered vs
  fallback, and actual Logic GUI/audio behavior remains **NOT TESTED** (neither
  PASS nor FAIL). No plugin parameters, routing, or project content were
  changed for this inspection.
- Current AU source path: `createEditor()` returns
  `SideChainAudioProcessorEditor` outside `SIDECHAIN_HEADLESS_TEST`; `hasEditor`
  returns true; editor constructor sets 800x520; its components are added and
  bounded in `resized`; the app has JUCE GUI modules/resources; `auval`
  reports Cocoa Views Available: 1. Automated UI harness paints the actual
  editor class and passes (43/43), but it is not a Logic host editor test.
- No prior known-good editor binary is locally available for exact code-level
  comparison. Existing 0.3.x ledger describes the old external-sidechain
  product and its last local recorded AU/VST3 checksums.

### Proven scheduler defect fixed

- Source inspection found `BeatScheduler::anchorAt()` computed distance to
  the next integer PPQ boundary as `beatFraction * samplesPerQuarter`; for
  fractional PPQ this used elapsed distance. At PPQ 2.25 and 120 BPM it
  scheduled 0.25 quarter-notes (6,000 samples) ahead instead of 0.75
  quarter-notes (18,000 samples), then incremented the saved beat label as
  if it had landed on PPQ 3. This could produce early/off-grid beats after
  non-beat-aligned plugin insertion or locate; exact-beat-start tests did
  not cover it.
- Targeted fix in `Source/BeatScheduler.h`: when not on an exact beat, anchor
  to `(1 - beatFraction) * samplesPerQuarter` and set `nextBeatPpq_` to the
  next integer boundary. Exact-beat block starts still fire at offset 0.
- Added a regression assertion in `Tests/BeatSchedulerTests.cpp`: PPQ 2.25,
  120 BPM, 48 kHz => offsets 18,000 and 42,000 in a 48,000-sample test block.
- After a forced full rebuild of the runner, **227/227** automated checks
  passed (DSP 25, Phase4 13, Phase6 45, Preset 30, Phase8 54, DuckLength 42,
  BeatScheduler 18); UI harness **43/43** passed. These prove the scheduler
  fix and editor render/layout in the offscreen harness, not real Logic GUI.
- Clean universal Release and Debug `SideChain - All` builds both reported
  BUILD SUCCEEDED after explicitly setting `ONLY_ACTIVE_ARCH=NO`; AU and VST3
  products contain `x86_64 arm64`. Release binary hashes are recorded above.
  Built plists and AU resource report 0.4.0/0x400; post-install `auval` now
  reports `Component Version: 0.4.0 (0x400)`, `Cocoa Views Available: 1`,
  `AU VALIDATION SUCCEEDED`. Debug bundles were not installed.
- Original installed AU/VST3 were preserved byte-for-byte in
  `Builds/MacOSX/build/recovery-original-installed/` and sibling
  `.pre-recovery` bundles. The newly clean-built Release 0.4.0 universal AU /
  VST3 were installed. Installed hashes now match the clean builds:
  AU `6c4e8ad40043d3cc5cf28bebd02fe702e4114bbc11e883488ef96e576c0c4570`,
  VST3 `3487813062eaf3960060edc3bdf109242109dd9f00249ea10cc36e647c16dd22`.
  AUHostingService no longer holds the old AU. Logic was subsequently
  restarted with the user's authorization and reopened on the user's chosen
  2026-09-30 auto-saved `LetItReign`; the `Piano Backed` AU host window is
  present, but its custom canvas is not surfaced through Accessibility. No
  visible editor or audio result has yet been established.

### Disposable Logic verification attempt — 2026-09-30 (dual-display session)

**Outcome: verification environment partially established; plugin acceptance tests NOT TESTED.** No production project was opened or edited during this attempt. A new unsaved Logic `Untitled` project was created for setup, but the required sustained audio source and SideChain AU were not inserted. That temporary document was explicitly deleted using Logic's Delete confirmation rather than saved. Testing stopped before audio/DSP checks because the editor had not been visually established.

#### Display isolation

- `system_profiler SPDisplaysDataType` and `NSScreen` identified the LG UltraFine as the external/main display (3200×1800 physical, 3200×1800 logical at 2× backing scale; main display) and the built-in Liquid Retina panel as display 2 (3024×1964 physical, 1800×1169 logical at 2×; desktop origin x=3200).
- Logic's unsaved window was moved/resized onto the built-in display (AX coordinates approx. x=3210, y=497; initial size 1420×650). A display-2-only macOS screenshot directly showed Logic on that panel. The external display did not receive direct input, although the app's display-2 window visually overlaid the extended desktop while positioned there. A transient macOS notification with the requested text `FREEBUFF IS TESTING STUFF` was invoked before GUI interaction; it could not be visually confirmed on the external monitor and no persistent overlay was created.
- Direct visual evidence showed only Logic's blank/default project workspace (instrument track and mixer); no SideChain editor was instantiated. The display-2 capture was inspected directly but not retained as a project artifact.

#### Installed AU identity and duplicate scan

- Intended installed AU executable: `~/Library/Audio/Plug-Ins/Components/SideChain.component/Contents/MacOS/SideChain`.
- Bundle version/short version: **0.4.0 / 0.4.0**; SHA-256: `6c4e8ad40043d3cc5cf28bebd02fe702e4114bbc11e883488ef96e576c0c4570`.
- Mach-O architectures: **x86_64 arm64**; executable timestamp: **2026-09-30 18:30:58 CEST** (4,888,552 bytes).
- Installed plugin scan found the production `SideChain.component` and a preserved `SideChain.component.pre-recovery` copy, plus `IPlugSideChain.component`, `Phase1 SidechainBusTest.component`, and `Bouncer Sidechain Effect.component`. VST3 folders likewise include production `SideChain.vst3`, preserved `SideChain.vst3.pre-recovery`, `Phase1 SidechainBusTest.vst3`, and `Bouncer Sidechain Effect.vst3`. The `.pre-recovery` siblings are byte-preserved rollback bundles but their suffixed bundle names may still be discoverable by some hosts; the exact 0.4.0 AU selected by Logic has not been confirmed because the plugin was not inserted. No `SideChainer.component` was found.
- **Exact AU loaded by Logic: NOT IDENTIFIED.** The disposable session never reached plugin insertion, so the installed identity above is confirmed, but no claim is made about an AU loaded in Logic. The earlier `auval` 0.4.0 PASS applies to the installed AU, not a Logic instance.

#### Disposable test result matrix

| Check | Result | Evidence |
|---|---|---|
| Logic visual / custom editor | **NOT TESTED** | No plugin instance in disposable project; screenshot only showed the blank project workspace. |
| Logic audio / internal beat pumping | **NOT TESTED** | No source audio or SideChain instance; no Logic audio measurements/listening. |
| Live Sidechain graph | **NOT TESTED** | Editor was not instantiated. |
| Duck Length (100/250/500/900 ms) | **NOT TESTED** | Not exercised. |
| Factory presets | **NOT TESTED** | Not exercised. |
| Offset | **NOT TESTED** | Not exercised. |
| Analyzer | **NOT TESTED** | Not exercised. |
| Tempo follow (120/100 BPM) | **NOT TESTED** | Not exercised in plugin. |
| PLAY / RECORD / STOP trigger gating | **NOT TESTED** | No plugin instance. |
| Installed AU `auval` | **PASS (prior recorded result)** | Component 0.4.0 (0x400), Cocoa view available, validation succeeded. |
| Host probe / automated suite / UI harness | **PASS (prior recorded result)** | Host probe 4/4; automated 227/227; offscreen UI harness 43/43. These are not Logic acceptance results. |

No confirmed Logic UI or DSP defect/root cause was established in the first attempt; no source files or DSP code were changed. I was able to move Logic and capture display 2 independently, but the application was intermittently brought behind the Freebuff window during automation, the new-project creation flow opened an audio-track configuration dialog that was cancelled, and adding a new track subsequently exposed a MIDI instrument track rather than a configured audio test track. I stopped instead of using the production project or proceeding without verified setup; that earlier disposable document was explicitly deleted. A true persistent status overlay isolated to the external monitor could not be verified with the available notification mechanism.

#### Focused follow-up attempt — unsaved Logic 12 session

##### Actual Logic picker observation — 2026-09-30

- Mix > Search and Add Plug-in opened Logic's floating `Search Audio Effect Plug-in` picker. A real display-2 capture visually showed the picker over the desktop with the unfiltered entries `Gain`, `ChromaVerb`, `Channel EQ`, `Distortion`, `Limiter`, `Compressor`, `Echo`, `OTT`, and `Adaptive Limiter`.
- This confirms that Logic's generic effect picker opens, but does **not** confirm the Audio Units manufacturer-filter/browser result. No query for `Music-Prod` or `SideChain` was entered; no manufacturer/category or SideChain result was shown; no plugin was selected or inserted. Subsequent screenshot showed only the Logic workspace, so reliable continued foreground interaction was not available.
- **Picker visible: PASS (generic picker only). Music-Prod: SideChain visible: NOT CONFIRMED. Insertion: NOT TESTED. Editor: NOT TESTED.** Stop automated UI attempts here and wait for the user to search the picker.

- Confirmed the LG UltraFine remains display 1 (external/main) and the built-in Retina is display 2. Logic's active document was `Untitled 3`; it is an unsaved disposable session, not `LetItReign`. A display-2-only screenshot visibly showed Logic running on the built-in panel. `FREEBUFF IS TESTING STUFF` was invoked as a macOS notification before the setup; the requested persistent external-screen overlay remains unverified.
- Replaced the prior temporary document without saving and used Logic's New Tracks dialog to select Audio and create a new unsaved workspace. Attempts to keep the setup to a single selected audio track were unreliable: the visible session ended up containing an instrument track (`Brit and Clean`) plus `Audio 2` and `Audio 3`, rather than the requested one-track audio test session. No clips, external sidechain, or routing were added. No project was saved.
- The installed AU identity was re-read directly: `SideChain.component`, CFBundle/short version 0.4.0, AU type `aufx`, subtype `SdCh`, manufacturer code `Musc`, component version 1024, universal `x86_64 arm64`, SHA-256 `6c4e8ad40043d3cc5cf28bebd02fe702e4114bbc11e883488ef96e576c0c4570`, executable timestamp 2026-09-30 18:30:58 CEST. This verifies the intended installed bundle metadata, not Logic's selected instance.
- System scan finds one exact-named production `SideChain.component` in the user Components directory, alongside distinct `IPlugSideChain.component`, `Phase1 SidechainBusTest.component`, and `Bouncer Sidechain Effect.component`. A preserved `SideChain.component.pre-recovery` rollback copy also exists with a nonstandard suffixed folder name; it is not counted as an exact `.component` match, but could still be discoverable by a nonstandard scanner. It is not possible to state it is shadowing or not shadowing without querying Logic's actual AU browser/instance.
- I could not reliably get Logic to remain foregrounded for browser interaction. The menu-bar search/add command was invoked, but each time Freebuff reclaimed focus before an on-screen browser result could be visually confirmed. Accessibility exposed no plugin catalog result, although the picker itself was visible in one real screenshot as described above. The `auval -a` enumeration attempt exceeded the command timeout; this is not evidence that the AU is missing or invalid. The prior targeted `auval -v aufx SdCh Musc` 0.4.0 validation PASS remains the known registration/validation evidence.
- **Logic generic picker: VISUALLY OPENED. Music-Prod AU visibility: NOT CONFIRMED. AU insertion: NOT ACHIEVED.** No name/manufacturer displayed by Logic's plugin browser has been observed, and no exact AU instance was loaded. A display-2 screenshot again showed only Logic's project workspace; no SideChainer editor window exists in that capture. This is a setup/foreground automation block, not proof of an editor or DSP failure.
- Consequently the actual Logic custom editor result, pumping/audio, graph, length, presets, offset, analyzer, tempo, transport, and INFO/auth are all **NOT TESTED**. There is no real Logic screenshot of a plugin instance. No SideChainer source/DSP/auth/preset/architecture files were modified. The disposable Logic project remains unsaved; it must not be saved.

### Release blockers remaining

1. User action required: bring Logic's `Search Audio Effect Plug-in` picker to the built-in display, search `Music-Prod` then `SideChain`, and visually confirm the listing before any insertion. The generic picker appeared once but did not remain available for a verified search. Music-Prod AU visibility: **NOT CONFIRMED**; insertion/editor: **NOT TESTED**.
2. Only after visual confirmation, put one sustained source on the main track with no sidechain routing and verify internal beat triggers, output attenuation, graph frames, presets, Amount, Length, Shape, Offset, Analyzer, tempo changes, and transport gating. These remain **NOT TESTED**; Ableton remains **NOT TESTED**.
3. Automated and host-level evidence remains as above: clean universal Release/Debug builds, `auval` 0.4.0 PASS, VST3BusCheck PASS, host probe 4/4, regression 227/227, UI harness 43/43, CPU benchmark 0.015% one core, and latency 30 ms (1440 samples @48 kHz). These do not satisfy Logic manual acceptance.
4. No known-good 0.3.x bundle exists locally for exact source/binary comparison. Original installed 0.4.0 AU/VST3 copies remain preserved for rollback/reference.

---

# 0.4.0 — INTERNAL TEMPO-SYNCED TRIGGER ARCHITECTURE (product pivot)

**Historical baseline:** the implementation and test results below describe
0.4.0 before the recovery changes documented above. The recovery section is
current for installed identities and remaining manual verification.

## V1. Product model change
- OLD (0.3.x): external sidechain audio -> transient detector -> duck.
  Required Logic "Side Chain -> kick track" routing; a real Logic session
  failed when the detector rejected the user's kick level.
- NEW (0.4.0, authoritative): NO external sidechain anywhere. The DAW
  transport timeline (AudioPlayHead PPQ + BPM + timeInSamples) fires ONE
  duck envelope on EVERY QUARTER-NOTE BEAT, sample-accurately:
      DAW beat -> internal trigger -> duck -> recovery -> next beat.
  Normal operation = insert on a track, press PLAY. No routing.

## V2. Exact beat-timing implementation
- New [BeatScheduler.h](../Source/BeatScheduler.h): pure, host-independent,
  fully deterministic-testable. Tracks `nextBeatHostSample_` (absolute host
  sample frame from timeInSamples) and `nextBeatPpq_`.
- Sample-accurate: within a block, triggers fire at the EXACT sample offset
  (blockStart + delta); multiple beats in one block all fire in order.
- Contiguity check: blockStart must equal the previous block end, otherwise
  (transport jump / loop restart / stop->play) the schedule re-anchors from
  the current PPQ - no stale triggers ever survive a jump.
- Tempo change: each beat step converts PPQ->samples with the CURRENT BPM,
  so the grid follows tempo continuously.
- Fail-safes: BPM <= 0 or PPQ unavailable => NO triggers for that block
  (honest, no fake timing). STOP calls reset() => nothing survives.
- Latency/lookahead unchanged: main path delayed by lookaheadSamples()
  (1440 @48k = 30 ms), reported via setLatencySamples; OFFSET -30..+30 ms
  moves the envelope relative to the BEAT (negative = earlier, real
  lookahead, host-compensated). Envelope delayed by (lookahead + offset).

## V3. Bus topology change
- BusesProperties now: 1x stereo input "Input" + 1x stereo output. The
  auxiliary "Sidechain" input bus is REMOVED (AU + VST3). isBusesLayout-
  Supported enforces strict stereo in/out. VST3BusCheck updated to the new
  contract: 1 kMain stereo in, 1 kMain stereo out, 0 failures.

## V4. Controls (0.4.0)
- AMOUNT (sidechainAmount, 0..100 %, def 50): duck depth, unchanged.
- DUCK LENGTH (duckLength, 50..1000 ms, def 250): total duck duration.
  A preset selection restores that preset's factory base length
  (template semantics); afterwards the user's knob rules until the next
  preset selection. Automation/undo/state all through the parameter.
- RELEASE (release, "Shape", 50..1000 ms, def 150): plateau/tail split
  WITHIN the length (hold = f(shape) x length; hold + 3 tau = length).
  Not a duplicate of Length (verified: Length dominates duration, Shape
  moves the split - DuckLengthTests 1.5).
- OFFSET (sidechainOffset, -30..+30 ms int, def 0): envelope vs beat.
- REMOVED: sidechainWhileStopped - a stopped timeline cannot advance a
  beat grid, so a while-stopped override had no honest audio meaning.
  Stopped => no triggers; the graph shows the parameter-driven PREVIEW.
- State version 5 (sidechainWhileStopped removed, presets own a base
  length). v1-v4 states load safely; the removed PARAM is ignored.

## V5. Factory presets (10 internal trigger shapes, all beat-triggered)
  Micro Kick 60%/150/60ms, Tight Kick 70/120/90, Classic Kick 75/150/250,
  Short Pump 60/120/180, Medium Pump 65/200/350, Wide Pump 70/300/500,
  Deep Pump 85/250/400, EDM Pump 90/400/600, Long Pump 80/500/800,
  Full Beat 95/350/950. All have DISTINCT base lengths (asserted).
  Identity stays value-derived; "Custom" never stored.

## V6. Graph (SIDECHAIN default view)
- Live (playing): the real duck envelope history + trigger ticks; every
  tick IS a quarter-note beat (full-height faint beat lines added);
  displays the live host BPM ("x BPM / BEAT" label).
- Stopped / no transport: clean labelled PREVIEW from Amount/Length/Shape.
- ANALYZER view: unchanged multi-trace (input / sidechain(now mirrors
  input) / duck / output). View switching remains display-only.

## V7. Files changed (0.4.0)
- Source/DuckEngine.h - REWRITTEN: external detector removed; fireTrigger()
  API; envelope/length/shape/offset/depth machinery preserved.
- Source/BeatScheduler.h - NEW (the internal trigger scheduler).
- Source/PluginProcessor.h/.cpp - single bus, scheduler integration,
  pending-trigger buffer (sample-accurate firing), state v5, presets
  (depth, shape, base length), GraphFrame.bpm, whileStopped removed.
- Source/PresetManager.h - 10 trigger presets (triples).
- Source/TransportGate.h - transportActive only (override removed).
- Source/GraphComponent.h/.cpp, GraphData.h - beat lines, BPM label,
  bpm field, preview default 250 ms.
- Source/PluginEditor.h/.cpp - whileStopped control removed, captions
  (AMOUNT / TIMING OFFSET), everything else preserved.
- Builds/MacOSX/Info-AU.plist, Info-VST3.plist - version 0.4.0.
- Tests: BeatSchedulerTests.cpp NEW; DSPRegression/Phase4/Phase6/Preset/
  Phase8/DuckLength adapted; Phase5Timing/TriggerDSP/Detector RETIRED
  (external detector gone; protections carried by the new suites);
  UIVerifyHarness preset names updated; HostCheck/VST3BusCheck.cpp new
  topology; CPUBench trigger-driven.

## V8. Test results (all green)
- DSPRegression 25/25, Phase4 13/13, Phase6 45/45, Preset 30/30,
  Phase8 54/54, DuckLength 42/42, BeatScheduler 17/17 => 226/226.
- Key scheduler proofs: 120 BPM 2 s => exactly 5 beats at exact samples;
  multi-beat blocks fire both at exact offsets; boundary straddles at
  250 BPM => no miss/duplicate; jump => no stale triggers; loop restart
  re-anchors; tempo change followed; BPM/PPQ unavailable => fail-safe;
  PLAY 2.2 s @120 => exactly 5 ducks; STOP => bit-transparent output;
  RECORD ducks; stop->play resync fires immediately; 4 beats => >=4 ducks
  with recovery between.
- UI harness 43/43; HostProbeAU 4/4; LogicFidelityProbe 4/4.
- Builds: AU/VST3 Debug+Release universal x86_64+arm64 SUCCEEDED.
- auval PASS; VST3BusCheck 0 failures (new 1-in/1-out topology);
  CPU 0.020 % of one core (lower: detector removed).
- Version 0.4.0 everywhere; AU SHA-256
  7d9a582c0af396dc922da81c29887b984e1788adcb56f3d40f56a63c8b55fccf,
  VST3 d74d6ba4028dfcd36f2e0817bb2a4244047a664c40449e81a731071e9970cb41.

## V9. Remaining manual check (user, Logic)
Insert on a normal audio track, NO sidechain routing, PLAY: one duck per
beat, repeated cycles in SIDECHAIN view, presets change character/length,
DUCK LENGTH audibly stretches/shortens, OFFSET moves the duck vs the beat
with graph ticks fixed on the beat, tempo change follows, STOP stops the
pumping and shows the preview, ANALYZER intact, auth untouched.

---

## V10. Distribution phase (2026-10-01) — see RELEASE_REPORT_0.4.0_DISTRIBUTION.md
Full distribution/productization report (product page, downloads, Studio
integration, hashes, unapplied migration, remaining work) lives in
`Tests/RELEASE_REPORT_0.4.0_DISTRIBUTION.md`. Plugin binaries untouched
(hashes unchanged: AU 6c4e8ad4..., VST3 34878130..., ZIP 8fc28fe4...).
