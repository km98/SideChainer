# Music-Prod Plugin Releases Deployment Trace

**Trace date:** 2026-10-02  
**Scope:** Read-only source/configuration inspection and unauthenticated HTTP GETs only. No GUI interaction, admin mutation, DB write, migration, upload, deployment, or publication was performed.

## 1. Production frontend

- **Production domain:** `https://music-prod.com`; `GET /admin` returned HTTP 200 with the generic Music-Prod HTML shell/title. The readable response contained no route/component/build metadata, so it does not identify which JavaScript bundle is currently served.
- **Deployment platform:** **UNKNOWN.** The available website repository has no discoverable Vercel/Netlify/Cloudflare deployment manifest or GitHub workflow. Its [README.md](../../../Music-ProdPROJECT-GitHub/README.md) is a generic Lovable template with `REPLACE_WITH_PROJECT_ID`, not a production project ID. Lovable handoff documents exist elsewhere, but describe a transfer/deployment process, not conclusive proof of the current `music-prod.com` frontend deployment.
- **Authoritative repository / branch / commit:** **UNKNOWN.** The closest current website checkout is `Music-ProdPROJECT-GitHub`, branch `main`, HEAD `9a1097f6` (remote `km98/pack-playhouse-9a0ed397.git`). It contains the `/admin` route, but its source and local bundles do not contain the reported Plugin Releases UI. It cannot be called the authoritative source for that feature based on available evidence.
- **Available local route:** [App.tsx](../../../Music-ProdPROJECT-GitHub/src/App.tsx#L184) maps `/admin` to `<Admin />`. [Admin.tsx](../../../Music-ProdPROJECT-GitHub/src/pages/Admin.tsx) applies the shared logged-in/admin gate and renders sections from a local switch. [AdminSidebar.tsx](../../../Music-ProdPROJECT-GitHub/src/components/admin/AdminSidebar.tsx) contains the available sidebar sections. None wires a Plugin Releases component.
- **Exact deployed route file and Plugin Releases component:** **NOT FOUND / UNKNOWN.** Exact string searches for `Plugin Releases` and `No releases yet` found no UI matches in the accessible site sources, snapshots, or generated bundles. `Create release` matches found in broad searches were unrelated release prose/comments, not a page/component. No page/component path is reported as authoritative.

## 2. Plugin Releases UI

The live `/admin` request only exposed the generic SPA shell; no browser/GUI was opened. Static inspection therefore cannot inspect the authenticated page the user reported.

- **Product selector source:** Unknown. The active `SideChainer` row exists in the configured release catalog, but no available source proves whether the UI reads it directly from `release_products`, through an API, or from another source.
- **Release-list query/source:** Unknown. No available frontend code contains the reported component, its empty state, or a query for `product_releases` / `release_artifacts`.
- **“No releases yet” implementation:** Unknown; no such local component string was found. It must not be attributed to a guessed query or page.
- **Create release action:** Not invoked. No frontend event handler was found. The available Deno API source supports `POST /admin/releases` with JSON `{ productId, version, mandatory?, minimumSupportedVersion?, releaseNotes? }`; `created_by` is derived from the authenticated admin. Its response is an envelope containing a draft release. **There is no evidence the reported frontend calls this route.** The Deno router has no `GET /admin/products` route; product listing may be provided elsewhere, but that is unverified.

The current local website Admin gate is implemented by `useAuth()` in [Admin.tsx](../../../Music-ProdPROJECT-GitHub/src/pages/Admin.tsx); [AuthContext.tsx](../../../Music-ProdPROJECT-GitHub/src/contexts/AuthContext.tsx) calls the project's `check-admin` Edge Function and falls back to a `user_roles.role='admin'` query. This verifies the available checkout's general admin gate, not the unavailable Plugin Releases frontend's own gate.

## 3. Production backend

- **Observed production service URL:** `https://wfpeajmdojcjqyrsnxbk.supabase.co/functions/v1/music-prod-studio-api`.
- **Live endpoint evidence:** unauthenticated `GET /updates?product=sidechain&current=0.4.0&platform=macos` returned HTTP 200, with `latestVersion: null`, `artifacts: []`, and `status: noPublishedRelease`. Unauthenticated `GET /admin/releases` returned HTTP 401. These are read-only observations. They establish a live function endpoint and an admin-authenticated listing route, but not a call from the reported Plugin Releases UI.
- **Most strongly evidenced release-serving implementation:** the Deno Supabase Edge Function `music-prod-studio-api` in the local [Music-ProdPROJECT](../../../Music-ProdPROJECT/supabase/functions/music-prod-studio-api/index.ts) snapshot. The function is wired to the [router.ts](../../../Music-ProdPROJECT/supabase/functions/music-prod-studio-api/router.ts) release routes, [releases.ts](../../../Music-ProdPROJECT/supabase/functions/music-prod-studio-api/releases.ts) store/service, [artifactStorage.ts](../../../Music-ProdPROJECT/supabase/functions/music-prod-studio-api/artifactStorage.ts), and [auth.ts](../../../Music-ProdPROJECT/supabase/functions/music-prod-studio-api/auth.ts). It uses `SUPABASE_URL` and the server-side service-role key.
- **Live revision match:** **NOT VERIFIED.** The observed live `/updates` body includes fields such as `channel`, `latestBuildNumber`, `latestReleaseId`, and `status` that are not in the corresponding local router response shape. This is concrete evidence of source/revision drift (or an alternate deployed implementation), not evidence of the deployed commit. No deployed revision or timestamp was exposed.
- **Separate Node implementation:** `Music-ProdStudio-Backend-tab2` (branch `studio-backend-jwt`, HEAD `f2b30ea`) has a Node server at `src/server.ts` and API handlers at `src/routes/admin-releases.ts`. Its environment config is generic and no frontend URL, production domain mapping, or deployment record connects it to `music-prod.com` Plugin Releases. It is not selected as the production Plugin Releases API.

Available Deno release endpoints include:

| Method | Route | Available source behavior |
|---|---|---|
| GET | `/admin/releases` | Admin-only list of releases |
| POST | `/admin/releases` | Admin-only create draft; body uses `productId`, `version`, optional release settings; response wraps `release` |
| GET/PATCH | `/admin/releases/:id` | Admin detail/edit draft |
| POST | `/admin/releases/:id/ready` | Draft → ready |
| POST | `/admin/releases/:id/commit` | Ready → published |
| POST | `/admin/releases/:id/withdraw` | Published → withdrawn |
| POST/GET | `/admin/releases/:id/artifacts` | Admin upload/list artifact |
| GET | `/downloads/authorize?artifactId=...` | Product policy plus published/active checks, then signed URL |
| GET | `/updates?product=...&current=...&platform=...` | Published update metadata |

The admin list/detail code reads `release_products`, `product_releases`, and `release_artifacts`; upload writes private Storage then artifact metadata; public update selection reads only published releases. This describes the locally inspected implementation. It does not prove the absent frontend uses those calls or that every local file matches production.

## 4. Production database

- **Project reference:** `wfpeajmdojcjqyrsnxbk` is strongly indicated by the available website `.env` / client config, the native Studio release-service configuration, live Edge Function URL, and read-only REST/API observations. This is the configured project the inspected application/function is intended to use.
- **Release entities in available implementation:** `release_products`, `product_releases`, `release_artifacts`.
- **Live evidence:** the production `/updates` endpoint resolves product slug `sidechain` and reports no published release. Prior read-only public REST checks found an active SideChainer product and Studio published releases; public RLS only exposes published releases, so it cannot establish whether a private draft exists.
- **Migration state: UNKNOWN.** The CLI-linked project was different; the correct project migration-ledger query was denied for lack of `database_write`; local DB was unavailable. Do not infer an applied/unapplied ledger from that failure. Prior direct schema probes established the `releases` bucket, `sidechain_versions` table, and plugin-token product-binding column absent in this project, but these do not identify the exact release-schema migration ledger history.

## 5. Production storage

- **Intended implementation:** Supabase Storage bucket `releases`, private. [artifactStorage.ts](../../../Music-ProdPROJECT/supabase/functions/music-prod-studio-api/artifactStorage.ts) derives object keys server-side from product/version/format/platform/architecture/filename, hashes actual uploaded bytes, uploads with `upsert: false`, and stores metadata in `release_artifacts`.
- **Upload route:** `POST /admin/releases/:id/artifacts` (multipart `file` plus format/platform/architecture/fileName; optional checksum). Admin auth is required. The function computes SHA-256 and byte size, uploads to private storage, then inserts metadata, compensating for insert failures.
- **Download route:** `GET /downloads/authorize?artifactId=...`; server rechecks the release is published and product active, then creates a 300-second signed URL. Storage keys are not returned as public metadata. The product download policy makes Studio public without a JWT while VYRE and ChordEngine require a user JWT; plugin access does not imply a public permanent URL.
- **Current production storage state:** Prior read-only bucket API check for the configured project returned `404 Bucket not found` for `releases`. Therefore the intended private bucket is **not currently present** there. No test object, bucket, or upload was created. Whether the live Plugin Releases UI uses another storage system is unknown because its frontend source is missing.

## 6. Admin authorization

- **Available frontend gate:** the current website `AuthProvider` exposes `isAdmin`; `Admin.tsx` refuses unauthenticated and non-admin users. `AuthContext.tsx` gets Supabase Auth session, calls `check-admin` with Bearer JWT, and falls back to the `user_roles` table.
- **Deno release backend gate:** `index.ts` wires `requireAdmin` from the shared auth module to all `/admin/releases` routes. [auth.ts](../../../Music-ProdPROJECT/supabase/functions/music-prod-studio-api/auth.ts) documents reuse of the existing Supabase Auth JWT → `user_roles.admin` infrastructure. `supabase/config.toml` does not enable gateway JWT verification for this function; the function performs route-specific auth itself (public update route, admin routes guarded in app code).
- **Observed live check:** unauthenticated GET `/admin/releases` returned 401. This is consistent with the Deno implementation, but does not alone prove the exact deployed role lookup implementation.
- **Node alternative:** its `src/auth/admin.ts` independently uses GoTrue and `user_roles.role='admin'`; no production connection was found.

## 7. Deployment evidence

- Local website checkout `Music-ProdPROJECT-GitHub`: branch `main`, HEAD `9a1097f6`; dirty/untracked unrelated workspace content was present during inspection. It has a generic Lovable project README, Vite/React source, and Supabase config; no discoverable deploy manifest/workflow or non-placeholder Lovable project identifier establishing the production domain binding.
- Local `Music-ProdPROJECT` contains the Deno function implementation and migrations used for inspection, but is a source snapshot without usable Git commit/branch metadata in this workspace.
- The Git-tracked reconstructed native workspace documents the `music-prod-studio-api` endpoint and has a Lovable Cloud handoff naming the `Music-ProdPROJECT` source path. That proves an intended handoff/source relationship, not that a specific revision is currently deployed.
- Live function URL and API responses are direct deployment evidence for the function name and service behavior. The response-shape mismatch with local source means **deployed commit/build ID, deployment time, and exact source revision remain unknown**.
- No frontend deployment timestamp, build ID, script chunk hash mapping from the live site, or host/project identifier could be verified from safe response metadata.

## 8. Implementation target

**No exact authoritative frontend files can currently be named.** The reported Plugin Releases page/component and its requests are absent from every accessible frontend checkout/bundle searched. Do not edit [App.tsx](../../../Music-ProdPROJECT-GitHub/src/App.tsx), [Admin.tsx](../../../Music-ProdPROJECT-GitHub/src/pages/Admin.tsx), or [AdminSidebar.tsx](../../../Music-ProdPROJECT-GitHub/src/components/admin/AdminSidebar.tsx) on the assumption they own the reported feature.

The local Deno release API files listed in §3 are concrete existing implementation files, but the deployed response differs from that source and no evidence ties the missing admin UI to it. They are **not designated next-phase edit targets** until production source/revision is verified. Therefore the exact files for the next implementation phase are **UNRESOLVED**; editing either backend or adding a duplicate UI would be speculative.

## 9. Unresolved items

1. Which repository/project and branch builds the current `music-prod.com` admin frontend.
2. The exact frontend route/component, product selector query, empty-state logic, create handler, endpoint, request body, and response consumer for Plugin Releases.
3. Whether the UI calls the Deno Edge Function, Node API, direct Supabase REST, or another API.
4. The deployed Edge Function source revision/commit and the reason its live update response shape differs from local source.
5. Frontend deployment platform/project ID, production branch, deployed build ID/time.
6. Exact target project's migration ledger; prior role/connection limits prevent proving it.
7. Whether another production artifact bucket exists or whether the live UI uses a different storage provider. The configured project's `releases` bucket was previously observed absent.

## 10. Change confirmation

**No source, database, storage, artifact, migration, release, or deployment state was changed during this trace.** Only read-only local inspection, public HTTP reads, and this report file creation were performed. No GUI application was opened or used.
