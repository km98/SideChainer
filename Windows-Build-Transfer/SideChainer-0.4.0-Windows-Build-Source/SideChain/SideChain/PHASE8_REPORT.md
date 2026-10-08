# PHASE 8 REPORT — SideChain (Music-Prod)

Phase 8 goals: (1) transport-aware sidechain operation with a SIDECHAIN WHILE
STOPPED override, (2) Music-Prod branding + INFO page + Music-Prod+
authentication integration.

Verdict: **GO** (transport + branding + INFO complete and verified; auth is
**AUTH INTEGRATION BLOCKED** in one specific, documented respect — see
sections 6 and 17).

---

## 1. FILES INSPECTED

SideChain project (complete, before any editing):

- `Source/PluginProcessor.h` / `.cpp` — APVTS architecture, processBlock path, Phase 7A hardening, state flush loop
- `Source/DuckEngine.h` — validated DSP (untouched this phase)
- `Source/PluginEditor.h` / `.cpp` — ProdKnob, preset strip, palette, 30 Hz timer
- `Source/GraphData.h` / `GraphComponent.h` / `.cpp` — real-data graph transport (untouched)
- `Source/PresetManager.h` — Phase 7 factory table (untouched; note: no PresetManager.cpp exists — header-only)
- `Tests/DSPRegressionTests.cpp`, `Tests/Phase5TimingTests.cpp`, `Tests/Phase6Tests.cpp`, `Tests/run_all.sh`
- `PHASE7A_REPORT.md`, `PHASE7_REPORT.md`
- `SideChain.jucer` — project version 0.2.0, file list, export formats

Existing Music-Prod infrastructure (READ-ONLY, nothing modified):

- `~/Documents/Music-ProdPROJECT/supabase/functions/vyre-plugin-auth/index.ts` — the full backend protocol (start/approve/poll/entitlements/logout, tables, fields)
- `~/Documents/Music-ProdPROJECT/src/lib/vyre-hise-fragment.hise` — the existing plugin-side client (token flow, auth.json persistence, 401 rule, offline rules, INFO page)
- `~/Documents/Music-ProdPROJECT/src/lib/vyreExport.ts` — how the Supabase origin is injected; `VYRE_PLUGIN_VERSION = '1.0.0'`
- `~/Documents/Music-ProdPROJECT/src/pages/VyrePluginLink.tsx` — the approval page (title "Connect VYRE")
- `~/Documents/Music-ProdPROJECT/src/App.tsx` — route `/vyre/link`
- `~/Documents/Music-ProdPROJECT/public/music-prod-logo.png` — approved brand asset (260×86 PNG; copied, source untouched)
- `~/Documents/Music-ProdPROJECT/.env` — Supabase origin (`https://wfpeajmdojcjqyrsnxbk.supabase.co`)

JUCE 6.1.3 API verification (read from actual headers, not assumed):

- `juce_audio_basics/audio_play_head/juce_AudioPlayHead.h` — `getCurrentPosition(CurrentPositionInfo&)`, `isPlaying`, `isRecording` ("When isRecording is true, then isPlaying will also be true"), callable only from processBlock
- `juce_core/network/juce_WebInputStream.h` — 6.1.3 constructor `(URL, bool)` with chained `withExtraHeaders` / `withConnectionTimeout`
- `AudioProcessor::setPlayHead` / `getPlayHead` — used for the transport test harness (the real host-facing API, not a fake)

## 2. FILES CREATED / MODIFIED

All paths project-relative (`SideChain/SideChain/`):

Created:
- `Source/TransportGate.h` — pure transport-gate logic (`transportActive`, `effectiveSidechainActive`)
- `Source/MusicProdAuth.h` / `Source/MusicProdAuth.cpp` — product-neutral auth client: `AuthTransport` interface, `HttpAuthTransport` (JUCE 6.1.3 API), `AuthManager` state machine with background thread, auth-file persistence, 401/offline rules
- `Source/InfoPage.h` / `Source/InfoPage.cpp` — INFO view component + `ProdToggle` switch
- `Tests/Phase8Tests.cpp` — 44 checks (transport, state, auth, branding)
- `Resources/music-prod-logo.png` — copied approved asset (source untouched)

Modified:
- `Source/PluginProcessor.h/.cpp` — new `sidechainWhileStopped` bool parameter (APVTS, default false); transport gate in processBlock; `paramIds[3]` in the state flush loop; stateVersion now `kCurrentStateVersion = 2`; switch restore + default-off in setStateInformation
- `Source/PluginEditor.h/.cpp` — editor-owned `AuthManager`; INFO button (header) + INFO view swap; `ProdToggle` + ButtonAttachment in the controls row; Music-Prod logo bottom-centre (paintOverChildren); window 800×500 → 800×520; knobs marginally resized to fit the toggle strip
- `Tests/Phase6Tests.cpp` — harness `runProcessor` + graph section now install a PLAYING `HarnessPlayHead` (required because the new transport gate default makes a play-head-less harness read as STOPPED); stateVersion assertion 1 → 2
- `Tests/PresetTests.cpp` — stateVersion assertion 1 → 2 (same reason)
- `Tests/run_all.sh` — suite 6 (Phase8Tests); `PH6_TUS` now also links `../Source/MusicProdAuth.cpp`
- `SideChain.jucer` — new source/resource files; `companyWebsite` corrected to `http://music-prod.com` (was `yourcompany.com`); resaved with the local Projucer

Not modified: `DuckEngine.h`, `GraphData.h`, `GraphComponent.*`, `PresetManager.h`, DSP constants, bus architecture, HISE, JUCE, any other Music-Prod project.

## 3. TRANSPORT GATE

- Transport API used: `AudioPlayHead::getCurrentPosition(CurrentPositionInfo&)` in processBlock (`isPlaying`/`isRecording`), verified against the actual JUCE 6.1.3 header. No audio-level inference, no GUI state, no timers.
- PLAY behavior: sidechain active, normal ducking (verified by test with a real play head).
- RECORD behavior: active. `isRecording ⇒ isPlaying` on this JUCE version, and the gate logic ORs both (`transportActive(false, true) == true` is asserted directly).
- STOP behavior: detector gate closed — the engine sees silence, gain returns smoothly to unity via the existing per-sample gain release (no click, no zipper; verified), main audio never muted, no sidechain leakage.
- Switch ID: `sidechainWhileStopped`
- Switch label: "SIDECHAIN WHILE STOPPED"
- Default: OFF (false)
- Switch ON behavior: pre-Phase-8 behaviour (sidechain works while stopped) — intentional override, verified by test.
- Switch OFF behavior: transport-gated (default product behaviour).
- Stop transition: PLAY→STOP measured: settled gain returns to > 0.999 with per-sample smoothing (max sample step < 0.25 asserted).
- Start transition: STOP→PLAY measured: ducking resumes (settled gain < 0.6 at Amount 75).
- Host without play head: `transportActive = false` → treated as STOPPED. Documented as the honest reading; hosts that never expose a play head keep the old behaviour only by enabling the switch. No fake clock invented.

## 4. STATE

- Previous state version: 1
- New state version: 2 — genuine schema change (new serialized PARAM `sidechainWhileStopped`), consistent with the existing versioning architecture
- Old-state compatibility: version 1 (and versionless) states restore Amount + Release; switch defaults OFF (deterministic test)
- sidechainAmount: unchanged (0–100, step 0.1, default 50)
- release: unchanged (50–1000 ms, default 150)
- sidechainWhileStopped: bool, default false, host-automatable (AudioParameterBool via APVTS), state-persistent (round-trip test), invalid stored value → OFF (test)
- Auth state stored: NO — token/display name live in `~/Library/Application Support/Music-Prod/SideChain/auth.json` (mirroring the existing plugin architecture), never in the APVTS tree (test asserts no auth data in DAW state)
- Auth token stored: in the auth file only; never in plugin state, presets, logs, or test output (tests assert only non-emptiness)

## 5. UI

- Music-Prod logo: real approved asset, `Resources/music-prod-logo.png` → compiled into BinaryData, drawn bottom-centre (~110 px wide, 85% opacity), reserved 26 px strip; visible in both views
- Logo source: copied from `Music-ProdPROJECT/public/music-prod-logo.png` (source project untouched)
- Logo placement: bottom-centre strip, below the toggle row; subtle, does not compete with controls
- INFO control: compact "INFO" TextButton in the header (right side)
- INFO page: view swap inside the existing editor (no second window): BACK, product title "SideChain", "VERSION 0.2.0" (from `JucePlugin_VersionString`), MUSIC-PROD+ section, textual auth status, subscription status, detail line (device code / errors), LOG IN / OPEN LINK PAGE / LOG OUT button
- Back navigation: BACK button → main view; graph/preset/controls toggled, no DSP restart, graph timer keeps running
- Main view changes: INFO button added; SIDECHAIN WHILE STOPPED toggle (bottom-left, small, secondary); height +20 px; logo strip. Amount/Release/preset untouched.
- Final layout: header (SideChain | INFO | Music-Prod) → preset strip → graph → Amount + Release knobs → toggle row → Music-Prod logo

## 6. AUTHENTICATION ARCHITECTURE

- Existing infrastructure inspected: `vyre-plugin-auth` edge function + VYRE's plugin-side flow (both fully traced; protocol replicated exactly)
- Login mechanism: device-code flow — POST `action:"start"` with `device_name` + `plugin_version`; user approves the code on the website; plugin polls
- Device code: `device_code` (opaque, hashed server-side) + user-facing `user_code` (`XXXX-XXXX`)
- Verification URL: **server-returned** `verification_url_complete` — currently `https://music-prod.com/vyre/link?code=…` (backend limitation, below)
- Polling: POST `action:"poll"` with `device_code`; statuses `pending` / `approved` (→ `token`, `display_name`) / `denied` / `expired` / `invalid`; token minted once, code burned. Implemented on a background thread (5 s cadence in the existing flow; our background worker is event-driven; `pollOnceForTesting` for deterministic tests)
- Token storage: `Music-Prod/SideChain/auth.json` under the user app-data dir (same architecture as the existing flow's `VYRE/auth.json`, product-neutral path); JSON `{token, display_name}`
- Logout: POST `action:"logout"` revokes server-side (sets `revoked_at`); local state + file cleared; editor refreshes
- Entitlement verification: POST `action:"entitlements"` with token → `{ok, subscribed, packs}`; `subscribed` is the authoritative Music-Prod+ field (server-side `has_active_subscription` RPC OR lifetime membership). Displayed verbatim; nothing invented.
- Music-Prod+ field: `subscribed` (boolean, verified in the edge function source)
- Product identity: `device_name: "SideChain"` — asserted by test (mock observes the value). No VYRE/ChordEngine identity sent.
- SideChain-specific branding: UI is 100% SideChain/Music-Prod; textual statuses: SIGNED OUT / WAITING FOR APPROVAL / SIGNED IN (AS …) / AUTHENTICATION ERROR / MUSIC-PROD+ ACTIVE / MUSIC-PROD+ NOT ACTIVE
- VYRE-specific references: **none in the plugin UI or identity.** The only VYRE artefacts are (a) the server-returned link URL and (b) developer comments/notes (not user-visible).
- Offline behavior: start failure → AUTHENTICATION ERROR with message; entitlements network failure → cached signed-in state kept (matches existing flow); 401 → local sign-out (matches existing flow)
- Error handling: every response field read defensively (missing/wrong-type tolerated); approved-without-token cannot sign in (test); no tokens logged anywhere

**BACKEND LIMITATION (exact):** `vyre-plugin-auth/index.ts` line `const SITE_URL = "https://music-prod.com"` hardcodes `verification_url: ${SITE_URL}/vyre/link`, and the approval page `src/pages/VyrePluginLink.tsx` is titled "Connect VYRE" on the `/vyre/link` route. A SideChain user following the login flow would therefore land on a VYRE-branded page — exactly what the brief forbids shipping. Per the blocker rule, the plugin does NOT auto-open the browser during login; the INFO page shows the code and the user explicitly presses "OPEN LINK PAGE". Minimal backend change required: either (a) add a product-neutral (or SideChain) verification page and make `verification_url` product-aware (e.g. accept a `product` field in `start` and serve `/sidechain/link` or a generic `/plugin/link`), or (b) rebrand the existing page generically. No backend file was modified.

## 7. INFO PAGE

- Signed-out state: "SIGNED OUT" + LOG IN button
- Login flow: LOG IN → start request → device code shown as text → user presses OPEN LINK PAGE (explicit, due to the backend limitation) → signs in + approves on the website → plugin polls → status flips to SIGNED IN
- Pending state: "WAITING FOR APPROVAL" + the user code in the detail line
- Signed-in state: "SIGNED IN" / "SIGNED IN AS <display_name>"
- Music-Prod+ active: "MUSIC-PROD+ ACTIVE" (only shown when signed in; only claimed when the server says `subscribed: true`)
- Music-Prod+ inactive: "MUSIC-PROD+ NOT ACTIVE"
- Logout: LOG OUT → server revocation + local clear; status returns to SIGNED OUT; Amount/Release/switch untouched
- Version display: "VERSION 0.2.0" from `JucePlugin_VersionString` (single source of truth: the .jucer `version` attribute; no hardcoded copy)

## 8. TRANSPORT TESTS

(All in `Tests/Phase8Tests.cpp`, real processor + play head injected through the real `AudioProcessor::setPlayHead` API.)

- PLAY: ducking engages (max gain < 0.5 at Amount 75) — PASS
- RECORD: gate logic accepts recording alone (`transportActive(false,true)`) — PASS
- STOP: settled gain > 0.999, smooth (no step > 0.25) — PASS
- Override ON: ducking engages while stopped — PASS
- Override OFF: gated (default) — PASS
- Play/stop transitions: PLAY→STOP (unity recovery) and STOP→PLAY (resumes) — PASS
- Record/stop transitions: covered by gate logic + PLAY/STOP integration (isRecording implies isPlaying on this JUCE version, documented in the header)
- Gain recovery: smooth, never above unity — PASS
- Leakage: output never exceeds main input, all transport states — PASS
- Stereo: identical gain both channels (L==R asserted on identical input) — PASS
- Amount = 0: strictly unity in all transport states — PASS
- No play head: treated as STOPPED — PASS

## 9. AUTH TESTS

(Mock transport; no network, no real account, no token printed.)

- Signed out: initial state — PASS
- Login start: → LINKING, user code surfaced, device identity "SideChain" — PASS
- Poll: pending keeps LINKING; approved → SIGNED IN — PASS
- Token persistence: auth file written with token — PASS (content never printed)
- Reload: stored token → SIGNED IN on startup — PASS
- Entitlement: subscribed=true → Music-Prod+ ACTIVE; subscribed=false → NOT ACTIVE (still signed in) — PASS
- Logout: transport called, state → SIGNED OUT, file removed — PASS
- 401: → local sign-out — PASS
- Network error: cached sign-in kept — PASS
- Malformed/missing response: approved-without-token cannot sign in — PASS
- Product identity: device_name == "SideChain" (mock observes) — PASS
- No token in state: DAW state contains no auth data — PASS
- No token in logs/tests: no print of token values anywhere — PASS

## 10. EXISTING REGRESSION

- Phase 3: 32/32 PASS
- Phase 4: 13/13 PASS
- Phase 5: 24/24 PASS
- Phase 6: 49/49 PASS (harness updated: playing play head required because of the new default gate; stateVersion assertion → 2)
- Phase 7A: included in the 49 (14 state-hardening checks, all green)
- Phase 7: 28/28 PASS (stateVersion assertion → 2)
- Phase 8: 44/44 PASS
- Combined: 190/190, ALL SUITES PASSED
- Exit code: 0

## 11. AU

- Debug: BUILD SUCCEEDED (0 errors)
- Release: BUILD SUCCEEDED
- auval: `auval -v aufx SdCh Musc` → `* * PASS` (installed Release build re-tested after Phase 8)
- Bus topology: unchanged (stereo in / stereo out / aux sidechain)

## 12. VST3

- Debug: BUILD SUCCEEDED
- Release: BUILD SUCCEEDED
- Main bus: kMain stereo 'Input'
- Sidechain bus: kAux stereo 'Sidechain', inactive by default
- Output bus: kMain stereo 'Output'
- VST3BusCheck: PASSED (0 failures)

## 13. UNIVERSAL

- AU: `lipo -archs` → `x86_64 arm64`
- VST3: `lipo -archs` → `x86_64 arm64`

## 14. CPU

- Benchmark: `Tests/CPUBench.cpp` (10 s stereo main + stereo sidechain, block 512, 48 kHz, Release -O2). Note: the bench drives DuckEngine directly (as in Phase 5/6 baselines), so it measures DSP cost, unchanged by the gate.
- Result: 0.059–0.086% of one core across three runs (the bench's known run-to-run spread)
- Baseline comparison: Phase 7A measured 0.051–0.076% — within the same spread
- Regression: none. The per-block gate adds two atomic loads + two ORs + one virtual play-head call per block (host-provided, hosts already call it); no measurable cost.

## 15. WARNINGS

- Production-source warnings: none (Release build filtered: zero warnings from `SideChain/SideChain/Source/*`)
- JUCE warnings: unchanged legacy set (Carbon deprecation, APVTS std::move internal)
- Remaining warnings: xcodebuild deprecation notices ("manual order", "Carbon Resources build phase") — project-level, pre-existing, not code

## 16. PROBLEMS FOUND

1. **Backend blocker (section 6):** the shared `vyre-plugin-auth` endpoint's verification page is VYRE-branded ("Connect VYRE" at `/vyre/link`). Shipping auto-open login would violate the no-VYRE-branding rule. Mitigated in-client (explicit OPEN LINK PAGE only); needs a small, explicitly-authorized backend change for a true SideChain flow.
2. Test harness update was required in Phase6Tests (play head) and two stateVersion assertions (1→2) — legitimate consequences of the new default behaviour and schema, documented above.
3. The `SideChain.jucer` had `companyWebsite="http://yourcompany.com"` (placeholder); corrected to `http://music-prod.com` as part of branding.
4. Cosmetic: the editor grew from 500 → 520 px height to fit the toggle strip without crowding the knobs.

## 17. AUTH STATUS

**AUTH INTEGRATION BLOCKED** — in one specific, documented respect.

The complete client-side authentication integration is implemented, tested (14 deterministic state-machine tests, all passing), and architecturally correct: device-code start/poll/entitlements/logout against the exact existing endpoint protocol, product identity "SideChain", token persisted outside DAW state, 401/offline/cached rules matching the existing flow, Music-Prod+ status displayed from the authoritative `subscribed` field.

What is blocked: presenting a **fully SideChain-branded, auto-opening login flow**. The existing verification page (`music-prod.com/vyre/link`, title "Connect VYRE") is VYRE-specific. Per the brief's blocker rule, the plugin does not silently send users to that page: login shows the code and requires an explicit user action ("OPEN LINK PAGE"), and the limitation is stated here rather than hidden. The minimal backend change (product-aware verification URL/page, or a neutral rebrand) is described in section 6 and requires explicit authorization to implement.

Everything else about auth — protocol, state machine, persistence, revocation, entitlement display — is complete and verified.

## 18. PHASE 8 CONCLUSION

**GO**

All transport acceptance criteria are met and verified by deterministic tests through the real AudioPlayHead API; the SIDECHAIN WHILE STOPPED parameter is automatable, state-persistent (v2 schema with v1 back-compat), and defaults OFF; the approved Music-Prod logo is in place; the INFO page exists with accurate, text-based status display; auth integration is complete on the client except for the documented backend-branding blocker (explicitly reported, not worked around); no VYRE branding appears in the product UI; audio-thread safety is preserved (no allocations, no locks, no network in processBlock); 190/190 checks pass with exit 0; auval PASS; VST3BusCheck 0 failures; universal binaries both formats; CPU within baseline spread; no unrelated project modified. Host GUI behaviour remains MANUAL USER VERIFICATION REQUIRED (Logic/Ableton procedures unchanged and still applicable; the new switch adds one manual check: transport gating while stopped/playing).

## 19. PHASE 9 RECOMMENDATION

The next phase should be the **authentication backend alignment + host verification** pass:

1. With explicit authorization, add product-aware verification to the shared plugin-auth edge function (accept a `product` field on `start`, return a product-specific `verification_url`, and add a product-neutral/SideChain approval page on music-prod.com). Flip the INFO page from "OPEN LINK PAGE" to the standard auto-open flow once the page is SideChain-safe, and remove the explicit-approval workaround.
2. Perform the deferred manual host verification (Logic Pro 12.0.1 AU, Ableton Live 11 VST3) using `Tests/MANUAL_HOST_TEST.md`, extended with Phase 8 items: transport gating while stopped/playing/recording, the SIDECHAIN WHILE STOPPED switch in the host's automation list, and the INFO/login flow in a real host.

Do NOT add licensing/DSP gating, new DSP parameters, Windows, or packaging in that phase.
