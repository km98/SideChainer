# PHASE 9 REPORT — FINAL (deployment + live verification)

Update of `PHASE9_REPORT.md` after the Lovable publish and live verification.
No code was changed during this phase; no unrelated VYRE work was touched.

## FINAL STATUS: **COMPLETE**

(One interactive step — browser sign-in + code approval during a live login —
is inherently user-performed; see §5. Everything automatable was verified
against production, and it all passed.)

---

## 1. DEPLOYMENT TARGET

- Production Supabase project (Lovable Cloud–managed): **`wfpeajmdojcjqyrsnxbk`**
- The local Supabase link (PlugInspect, `wwzcgqfqeppwtoaoqata`) was never used
- Deployment mechanism: **Lovable publish** (the supported Lovable Cloud path —
  deploys the website and its managed edge functions together). The earlier
  direct dashboard edit did not persist because out-of-band Supabase changes
  are not part of the Lovable Cloud pipeline.
- Frontend source: the published Lovable project reflects the Phase 8B design
  (see §2 for the observed delta vs. our `PluginLink.tsx` — functionally
  equivalent, product map identical)

## 2. FRONTEND PUBLISH RESULT — LIVE AND VERIFIED

Production site `music-prod.com` now serves the newly published build
(`assets/index--0ud_HHy.js`, replaced the old `index-CqTHIeGe.js`):

- `/plugin/link` — **live, HTTP 200**, renders the product-neutral page
- `/vyre/link` — **still live, HTTP 200** (VYRE untouched, HTTP 200)
- SideChain identity: the page component resolves the product from its route
  (`product:"sidechain"` passed by the `/plugin/link` route module) and
  renders **"Connect SideChain"**; success copy "You can go back to SideChain
  now."; warning copy "Only approve a code you can see inside your own copy
  of SideChain…"
- **No VYRE branding on the SideChain page** (verified from the shipped
  chunk: SideChain copy table only; the string "Connect VYRE" does not exist
  in the live bundle for this route)
- Product-aware auth client behaviour present: the page sends `product` with
  every approve/deny call, verifies the server-returned `product` matches the
  page's own product before accepting `product_name`, and derives its
  per-product sign-in redirect from the product map
  (`vyre → /vyre/link`, `sidechain → /plugin/link`); product is never taken
  from a user-manipulable URL parameter

Observed delta vs. our committed `PluginLink.tsx`: Lovable's publish rebuilt
the page from its project workspace — the live page is a unified
product-parameterised component (both routes render the same page with
different product props) rather than our separate-file version. The product
map, server-derived naming, product-scoped redirects, and cross-product
protection are equivalent. Functionally verified item-by-item below; no
correction required.

## 3. BACKEND DEPLOYMENT RESULT — LIVE AND VERIFIED

The same Lovable publish deployed the Phase 8B edge function to
`wfpeajmdojcjqyrsnxbk`. Verified against production at 2026-09-27 22:33 CEST:

| Test | Expected | Live result |
|---|---|---|
| `start` product=`sidechain` | 200; URL `/plugin/link`; product metadata | **PASS** — `verification_url: …/plugin/link`, `verification_url_complete: …/plugin/link?code=…`, `product:"sidechain"`, `product_name:"SideChain"` |
| `start` unknown product (`zzz`, numeric, null) | 400 `unknown_product` | **PASS** — 400 `{"error":"unknown_product"}` (numeric product also rejected → no type confusion) |
| `start` missing product (plain `{"action":"start"}`) | 400 `unknown_product` | **PASS** |
| **Legacy VYRE 2.0.1 compat**: missing product + `device_name:"VYRE"` | 200; URL `/vyre/link` | **PASS** — 200, `/vyre/link` URLs, `product:"vyre"`, `product_name:"VYRE"` |
| `start` explicit `product:"vyre"` | 200 | **PASS** |
| `poll` before approval (fresh sidechain device code) | 200 `{"status":"pending"}` | **PASS** |
| `approve` without session | 401 `sign_in_required` | **PASS** |
| Malformed body (`not-json`) | safe rejection | **PASS** — 400 `unknown_action`, no 5xx |
| `poll` with bogus device code | 404 `invalid` | **PASS** |
| `entitlements` with invalid token | 401 `invalid_token` | **PASS** |
| `logout` with invalid token | safe 200 | **PASS** (idempotent by design) |
| `bogus` action | 400 `unknown_action` | **PASS** |

No 5xx and no unexpected response in any probe. VYRE behaviour is intact
(explicit and legacy device-name paths both resolve to `/vyre/link`).

## 4. LIVE VERIFICATION PAGE (detailed)

- URL `https://music-prod.com/plugin/link?code=XXXX-XXXX` (exactly what the
  server's `verification_url_complete` returns for SideChain)
- Title: "Connect SideChain" (product map; server `product_name` may refine it)
- Branding: Music-Prod design system (MainLayout, same card/buttons/icons)
- VYRE branding: none on this page
- Device code: pre-filled from `?code=`, 8-char formatted input
- Approval: "Approve this plugin" / deny "I did not request this" — both send
  `product:"sidechain"` with the user code
- Success state: server-verified `product === "sidechain"` required before
  the product name is displayed; shows "Plugin connected — You can go back
  to SideChain now."

## 5. LIVE SIDECCHAIN AUTH FLOW

Automated against production (§3): start, pending poll, approval auth gate,
401/400/404 handling, invalid-token entitlements/logout — all pass.

The one inherently interactive step — signing into the Music-Prod account in
the browser and clicking "Approve this plugin" — is a user action Freebuff
cannot perform (no browser session with the account). Consequences in the
plugin when a user completes it: the plugin's poll receives
`{status:"approved", token, display_name}`, stores the token in
`~/Library/Application Support/Music-Prod/SideChain/auth.json`, flips INFO to
SIGNED IN, and fetches `subscribed` for the Music-Prod+ line. This exact
sequence (plus reload-persistence, logout/revocation, expired-code and
network-failure paths) is covered by the 51-check mock-contract suite against
a byte-matching protocol simulation. Not claimed as live-observed.

## 6. SECURITY RESULTS

- Product allowlist: server-side; SideChain cannot get VYRE's page and
  vice versa (both directions probed live) — OK
- Cross-product approval: approve carries `product`; server binds approval
  to the device record's product; the page additionally rejects a server
  `product` that doesn't match its own — OK
- Token: never in DAW plugin state (automated test), never printed in any
  log/test/report — OK
- Audio thread: no network/allocation/lock; auth on a background thread — OK
- 401 handling: invalid token → `invalid_token` 401; plugin clears cached
  auth on 401 — OK
- Legacy VYRE compat hole: none — `device_name:"VYRE"` fallback works only
  for `vyre`; a SideChain-less plugin cannot claim another product
- Carried non-critical findings: no rate limit on `start`/`approve`;
  plaintext auth.json (VYRE parity). No critical issues.

## 7. REGRESSION (re-run this phase, code unchanged)

- Automated suites: **197/197, exit 0** (32 + 13 + 24 + 49 + 28 + 51)
- AU Debug / AU Release / VST3 Debug / VST3 Release: BUILD SUCCEEDED
- auval `aufx SdCh Musc`: `* * PASS`
- VST3BusCheck: PASSED (0 failures; kMain / kAux 'Sidechain' inactive by
  default / kMain output intact)
- Universal: AU `x86_64 arm64`, VST3 `x86_64 arm64`
- CPU: 0.056% of one core (baseline 0.051–0.086%)
- Plugin endpoint wiring confirmed pointed at production
  (`wfpeajmdojcjqyrsnxbk`), product `sidechain`, and it reads
  `verification_url_complete` — i.e. the live `/plugin/link` flow
- Token not in DAW state / not in logs: verified (automated + manual grep)

## 8. REMAINING MANUAL HOST CHECKS

Logic Pro 12.0.1 (29 steps) and Ableton Live 11 Suite (20 steps):
**MANUAL USER VERIFICATION REQUIRED** — this environment has no interactive
host GUI automation; no GUI result is claimed. Suggested order after opening
each host: insert plugin → transport-gate checks (default OFF-while-stopped,
override ON) → Amount/Release/presets → INFO login with a real account
(completing the interactive approval step) → logout.

## 9. CONCLUSION

**COMPLETE** for this phase's automatable scope: the product-neutral auth
frontend is live and verified on production, the Phase 8B backend is live and
verified on production (sidechain accepted with `/plugin/link`, unknown and
missing products rejected with 400, legacy VYRE device-name compatibility
intact, cross-product isolation enforced, no 5xx under hostile input), the
security invariants hold, and the full SideChain regression remains green.
The only items not automatable here are the interactive browser-approval
login (user-performed when logging in) and the Logic/Ableton host checks —
both explicitly classified, neither blocking the deployment/verification
objective of this phase.
