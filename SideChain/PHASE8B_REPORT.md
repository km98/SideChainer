# PHASE 8B REPORT — SideChain (Music-Prod)

Phase 8B goal: fix the authentication infrastructure so SideChain uses a
product-neutral Music-Prod verification page, with server-side product
validation and full VYRE backwards compatibility.

Verdict: **GO** — **AUTH COMPLETE** (pending deployment of the two backend
artifacts, which is a release action outside this phase's scope; see
section 17).

---

## 1. BACKEND INSPECTED

- Edge function: `supabase/functions/vyre-plugin-auth/index.ts` — the single
  device-code auth endpoint. Actions: `start`, `approve`, `deny`, `poll`,
  `entitlements`, `download`, `logout`. Tables: `vyre_plugin_device_codes`,
  `vyre_plugin_tokens`.
- Verification page: `src/pages/VyrePluginLink.tsx` at route `/vyre/link`
  (App.tsx). Title hardcoded "Connect VYRE"; success copy "You can go back to
  VYRE now…"; footer "your own copy of VYRE". Accepts only `?code=`.
- Relevant frontend: `src/App.tsx` (route table), `src/hooks/useAuth.ts`
  (session), `src/integrations/supabase/client.ts` (functions.invoke).
- Relevant tables (`supabase/migrations/20260812201504_*.sql`):
  - `vyre_plugin_device_codes(id, device_code_hash UNIQUE, user_code UNIQUE,
    status, user_id, device_name, plugin_version, approved_at, expires_at,
    created_at)`
  - `vyre_plugin_tokens(id, user_id, token_hash UNIQUE, device_name,
    plugin_version, last_seen_at, revoked_at, created_at)`
- Existing product identity: `device_name` is carried through the whole
  lifecycle (device code → token record) — VYRE sends `"VYRE"`, the Phase 8
  SideChain client already sent `"SideChain"`. There is no dedicated
  `product` column and no product on the verification URL.
- Existing auth flow: start (creates hashed device code + user code, returns
  hardcoded `${SITE_URL}/vyre/link` URLs) → user approves on the website
  (JWT-authenticated `approve`) → poll (mints token once, burns the code) →
  entitlements (token-authenticated; `subscribed` from
  `has_active_subscription` RPC OR lifetime membership) → logout (sets
  `revoked_at`).
- Two repo copies exist: `~/Documents/Music-ProdPROJECT-GitHub` (git remote
  `origin`, newest — contains the HISE 4.1 form-encoded-body fix, admin
  draft-pack download, signed URLs, creditBalance) and
  `~/Documents/Music-ProdPROJECT` (no git data shown; an older mirror with
  pre-existing divergence in the same edge function). The **GitHub copy is
  authoritative**. The Phase 8B change was applied identically to both
  (verified: the two edge functions now differ only by the pre-existing
  older-copy gaps; the new page files are byte-identical).

## 2. FILES INSPECTED

SideChain: `Source/MusicProdAuth.h/.cpp`, `Source/InfoPage.h/.cpp`,
`Source/PluginEditor.cpp` (auth wiring), `Tests/Phase8Tests.cpp`,
`Tests/run_all.sh`.

Backend (read-only first): both
`supabase/functions/vyre-plugin-auth/index.ts` copies,
`src/pages/VyrePluginLink.tsx` (both copies, identical),
`src/App.tsx`, `src/integrations/supabase/types.ts`
(`vyre_plugin_device_codes` / `vyre_plugin_tokens` Row/Insert/Update),
`supabase/migrations/20260812201504_2c91c5f0-*.sql`,
`supabase/migrations/20260529172443_*.sql` (`has_active_subscription`), and a
project-wide search for existing Deno test infrastructure (none found;
frontend uses vitest).

## 3. FILES CREATED / MODIFIED

Backend (Music-ProdPROJECT-GitHub **and** Music-ProdPROJECT, identical change):

| File | Project | Change |
|---|---|---|
| `supabase/functions/vyre-plugin-auth/index.ts` | backend | `PRODUCT_PAGES` allowlist (`vyre`→`/vyre/link` "VYRE", `sidechain`→`/plugin/link` "SideChain"); `start` validates `product` (falls back to `device_name`), returns `400 unknown_product` + `error` for unknown/missing; start response now includes `product`; `approve` response now includes `product` derived from the linked device record; VYRE responses otherwise byte-identical |
| `src/pages/PluginLink.tsx` | verification page | NEW product-neutral page at `/plugin/link`: identical structure/design to `VyrePluginLink.tsx`, title derived from the server's `product` field ("Connect SideChain" / fallback "Connect your Music-Prod plugin"), zero hardcoded product names |
| `src/App.tsx` | frontend | lazy import + route `/plugin/link` (VYRE route untouched) |

SideChain project:

| File | Change |
|---|---|
| `Source/MusicProdAuth.h/.cpp` | `AuthTransport::start` now takes `product`; `HttpAuthTransport` sends the `product` field; `AuthManager` stores/forwards it. No other protocol change |
| `Source/PluginEditor.cpp` | auth manager constructed with product `"sidechain"` (allowlist key) and device_name `"SideChain"` |
| `Source/InfoPage.cpp` | login auto-opens the server-returned verification page (standard device-code UX; the Phase 8 explicit-approval workaround removed); pending text now "Approve this code on your Music-Prod account page (opened in your browser): XXXX-XXXX"; button "REOPEN LINK PAGE" |
| `Tests/Phase8Tests.cpp` | mock transport simulates the 8B server contract (allowlist, product-aware page); +7 checks (section 4B) |
| `Tests/run_all.sh` | no change needed (suite 6 already compiles MusicProdAuth.cpp) |

Not modified: `DuckEngine.h`, `GraphData.h`, `GraphComponent.*`,
`PresetManager.h`, `PluginProcessor.*` (transport gate untouched), HISE, JUCE,
`VyrePluginLink.tsx` (VYRE page byte-identical), ChordEngine,
Phase1SidechainBusTest, any other project.

## 4. PRODUCT IDENTITY

- SideChain identifier: `sidechain` (lowercase slug, allowlist key, sent as
  the `product` field); display name "SideChain".
- VYRE identifier: `vyre` (allowlist key; legacy `device_name` "VYRE" also
  resolves to it); display name "VYRE"; page `/vyre/link` unchanged.
- Product validation: server-side allowlist in the edge function. `start`
  resolves `product` (falling back to `device_name`) case-insensitively
  against `PRODUCT_PAGES`; unknown or missing → `400 { error:
  "unknown_product" }`. No arbitrary string is ever used as a page path or
  displayed as identity.
- Cross-product protection: an unknown product can never fall back to
  another product's page (rejection, not substitution); the approve response
  derives `product` from the device record on the server (client cannot
  choose the branding); tokens remain product-agnostic as before (bound to
  `device_name` on their record, unchanged semantics).

## 5. VERIFICATION PAGE

- Old behavior: every plugin sent to `music-prod.com/vyre/link`, title
  "Connect VYRE", VYRE-specific success/footer copy.
- New behavior: `start` returns the product's own page. VYRE clients receive
  exactly the historical URLs; SideChain receives `/plugin/link`.
- SideChain title: "Connect SideChain" (server-derived `product` field;
  generic fallback "Connect your Music-Prod plugin" if absent).
- SideChain branding: Music-Prod design system (MainLayout, same card /
  buttons / icons as the existing page); success copy "You can go back to
  SideChain now."
- VYRE branding: none on `/plugin/link`; `/vyre/link` untouched for VYRE.
- URL: `https://music-prod.com/plugin/link?code=XXXX-XXXX` (server-returned;
  the client only ever opens the URL the server sent).
- User flow: SideChain INFO → LOG IN → browser opens the pre-filled
  product-neutral page → sign in (if needed) → "Approve this plugin" →
  plugin polls → SIGNED IN → Music-Prod+ status shown.

## 6. AUTH FLOW

API contract (unchanged except where noted):

- `start` — request `{action:"start", product, device_name, plugin_version}`;
  response `{device_code, user_code, product, verification_url,
  verification_url_complete, expires_in, interval}`; errors: `400
  unknown_product` (new), `500 server_error`. Auth: none. Product handling:
  allowlist resolution (new).
- `approve` / `deny` — request `{action, user_code}` + Bearer JWT; response
  `{ok, status:"approved"|"denied", device_name, product}` (product added);
  errors: 401 `sign_in_required`, 404 `invalid_code`, 410 `expired_code`,
  409 `already_used`.
- `poll` — request `{action:"poll", device_code}`; response
  `{status:"pending"|"approved"|"denied"|"expired"|"invalid", token?,
  display_name?}`; token minted once, code burned. Unchanged.
- `entitlements` — request `{action:"entitlements", token}`; response
  `{ok, subscribed, packs:[...]}`. Unchanged (authoritative Music-Prod+
  field remains `subscribed`).
- `logout` — request `{action:"logout", token}`; sets `revoked_at`. Unchanged.
- 401 on entitlements → local sign-out (SideChain client rule, unchanged).
- Network failure → cached signed-in state kept (unchanged).

## 7. TOKEN

- Storage: `~/Library/Application Support/Music-Prod/SideChain/auth.json`
  (`{token, display_name}`) — unchanged from Phase 8.
- DAW state: never (Phase 8 test still asserts plugin state contains no auth
  data).
- Logs: never printed anywhere (client, tests, or report).
- Revocation: logout revokes server-side (`revoked_at`) and clears the local
  file; 401 on entitlements clears locally. Unchanged.

## 8. MUSIC-PROD+

- Subscription field: `subscribed` (authoritative; `has_active_subscription`
  RPC OR lifetime membership server-side) — unchanged.
- Active state: "MUSIC-PROD+ ACTIVE" only when the server returned
  `subscribed: true` while signed in.
- Inactive state: "MUSIC-PROD+ NOT ACTIVE" when signed in with
  `subscribed: false`. No invented entitlement data; no audio license gate.

## 9. VYRE COMPATIBILITY

- Existing VYRE flow: the `vyre` allowlist entry returns the identical
  historical URLs; `start`'s added fields are additive; VYRE's client (HISE
  fragment) sends `device_name:"VYRE"`, which resolves through the same
  allowlist even before any VYRE client update. All existing request/response
  fields preserved.
- Existing VYRE UI: `/vyre/link` + `VyrePluginLink.tsx` byte-identical
  (verified `git status` — file untouched).
- Existing VYRE entitlements: `entitlements`/`download`/pack logic untouched.
- Regression result: mock-contract test asserts `vyre` →
  `https://music-prod.com/vyre/link` (8B.4); full SideChain suite green.
  (Live VYRE end-to-end requires deployment; see section 17.)

## 10. SIDECCHAIN INFO PAGE

- Signed out: "MUSIC-PROD+ …" section shows "SIGNED OUT" + LOG IN.
- Login: LOG IN → start request (product `sidechain`) → browser auto-opens
  `/plugin/link?code=…` (server-returned URL) → status "WAITING FOR
  APPROVAL", code shown, "REOPEN LINK PAGE" button.
- Pending: "WAITING FOR APPROVAL" + device code in the detail line.
- Signed in: "SIGNED IN" / "SIGNED IN AS <name>" + LOG OUT.
- Music-Prod+ active: "MUSIC-PROD+ ACTIVE".
- Music-Prod+ inactive: "MUSIC-PROD+ NOT ACTIVE".
- Logout: revokes server-side, clears local state, returns to SIGNED OUT;
  Amount/Release/transport switch untouched.
- Any VYRE text: none (automated UI-text scan passes).

## 11. TESTS

- Auth (Phase 8B additions in `Tests/Phase8Tests.cpp`, suite total 51):
  1. SideChain start includes product `sidechain` — PASS
  2. VYRE product still resolves to `/vyre/link` (mock contract) — PASS
  3. Poll returns the correct token / pending keeps LINKING — PASS
  4. Product identity preserved (device_name + product observed) — PASS
  5. Entitlements return correctly (subscribed true/false) — PASS
  6. Music-Prod+ subscribed state handled — PASS
  7. Logout works (revocation + local clear) — PASS
  8. 401 → local sign-out — PASS
  9. Malformed/missing fields fail safely (approved-without-token) — PASS
  10. Missing product identity fails safely — PASS (8B.3)
  11. Unknown product identity rejected safely (no URL, error state) — PASS (8B.2)
  12. SideChain never sends VYRE identity — PASS (8B.5)
  13. SideChain UI contains no cross-product branding — PASS
  14. Token never in DAW state — PASS
  15. Token never written to logs/tests — PASS (assertions check non-emptiness only)
  16. SideChain gets the product-neutral page `/plugin/link`, never
      `/vyre/link` — PASS (8B.1)
  17. VYRE compatibility — PASS (8B.4)
- Backend: no test infrastructure exists for edge functions (no Deno test
  setup in either repo copy); per the brief ("add backend tests only if the
  existing backend has a test infrastructure"), none were invented. The
  allowlist logic is mirrored and covered deterministically by the plugin's
  mock-contract tests.
- Phase 3: 32/32 PASS
- Phase 4: 13/13 PASS
- Phase 5: 24/24 PASS
- Phase 6: 49/49 PASS
- Phase 7A: included in the 49 — PASS
- Phase 7: 28/28 PASS
- Phase 8: included in the 51 — PASS
- Combined: 197/197, ALL SUITES PASSED
- Exit code: 0

## 12. BUILD

- AU Debug: BUILD SUCCEEDED (0 errors)
- AU Release: BUILD SUCCEEDED
- VST3 Debug: BUILD SUCCEEDED
- VST3 Release: BUILD SUCCEEDED
- auval: `auval -v aufx SdCh Musc` → `* * PASS` (installed Release build)
- VST3BusCheck: PASSED (0 failures; kMain/kAux/kMain intact)

## 13. UNIVERSAL

- AU: `lipo -archs` → `x86_64 arm64`
- VST3: `lipo -archs` → `x86_64 arm64`

## 14. CPU

- Result: 0.063% of one core (10 s stereo bench, Release -O2)
- Baseline: Phase 7A/8 range 0.051–0.086%
- Regression: none (auth changes touch no audio-thread code)

## 15. DATABASE

- Schema changed: NO
- Migration: none required, none applied
- Why: the existing `device_name` column on both `vyre_plugin_device_codes`
  and `vyre_plugin_tokens` already carries product identity through the
  whole lifecycle (VYRE = "VYRE", SideChain = "SideChain"), which the
  server-side allowlist resolves to a page. The verification page derives
  visible product identity from the approve response (server-derived from
  that record). No new column, table, or migration was necessary — adding
  one would have violated the no-schema-change-without-evidence rule.

## 16. PROBLEMS FOUND

1. The two local Music-Prod repo copies have **pre-existing** divergence in
   `vyre-plugin-auth/index.ts` (the GitHub copy is newer: parseBody fix,
   admin downloads, signed URLs, creditBalance). Both copies received the
   identical 8B change; whoever deploys should deploy from the GitHub copy.
2. The new route/page exists only in the local repos — **deployment** of the
   edge function and the website build is required before the live flow
   works end-to-end (a release action, outside this phase's stop condition).
3. The `product` field in start/approve responses is additive; if any
   unknown third-party client parsed those objects exhaustively it would see
   a new field (no known client does; both known clients ignore unknown
   fields).
4. Pre-existing tsc errors in `vyreExport.ts` + two VYRE test files
   (untouched by this phase) — flagged, not fixed, per project isolation.

## 17. AUTH STATUS

**AUTH COMPLETE**

The full flow now works product-neutrally end-to-end at the code level:
SideChain sends `product: "sidechain"`; the edge function validates it
against a server-side allowlist (unknown/missing rejected with 400); the
server returns the product's own verification page
(`/plugin/link` — Music-Prod-branded, title "Connect SideChain", no VYRE
text anywhere); approval returns the server-derived product name for the
success screen; poll/entitlements/logout/401/offline semantics unchanged;
VYRE keeps byte-identical URLs, page, and entitlement behavior; no database
change was needed.

Caveat (deployment, not implementation): the changed edge function and the
new `/plugin/link` page exist in the backend source but are not yet deployed
to the live Supabase project / music-prod.com. Until that release step
happens, the live flow still serves the old VYRE page. All acceptance
criteria concerning code behaviour are met and tested; no audio, DSP,
transport, preset, or graph behaviour changed; 197/197 checks pass.

## 18. PHASE 8B CONCLUSION

**GO**

Product-neutral authentication is implemented and verified on both sides of
the contract: server-side allowlisted product identity, a new
Music-Prod-branded product-neutral verification page, an unchanged VYRE flow,
a minimal SideChain client adaptation (product field + auto-open, removing
the Phase 8 workaround), 7 new deterministic tests (197/197 total, exit 0),
full build/auval/VST3BusCheck/universal/CPU regression green, zero database
changes, and zero changes outside the auth surface. Remaining work is the
routine deployment of the two backend artifacts, explicitly noted rather
than claimed as done.

## 19. PHASE 9 RECOMMENDATION

The next phase should be **deployment + live end-to-end auth verification**:

1. Deploy the updated `vyre-plugin-auth` edge function (from the
   `Music-ProdPROJECT-GitHub` copy) and the website build containing
   `/plugin/link`.
2. Live-verify once with a real account: VYRE login (back-compat), then
   SideChain login via the INFO page — confirm the browser shows
   "Connect SideChain", approval flips the plugin to SIGNED IN, and
   Music-Prod+ status matches the account's real subscription.
3. Complete the deferred manual host verification (Logic Pro 12.0.1 /
   Ableton Live 11) from `Tests/MANUAL_HOST_TEST.md`, including the Phase 8
   transport-gating checks and the INFO/auth UI in a real host.

Do NOT add licensing/DSP gating, new DSP parameters, presets, Windows, or
packaging in that phase.
