# Live Admin Source Fingerprint

**Investigation date:** 2026-10-02  
**Scope:** Public HTTP GETs/HEAD-equivalent metadata, local read-only source/build/git inspection, and temporary bundle downloads used only for inspection. No GUI was used and no production write endpoint was called.

## 1. Live bundle identity

| Asset | URL | HTTP | Bytes | SHA-256 | Last-Modified / ETag |
|---|---|---:|---:|---|---|
| Admin lazy chunk | `https://music-prod.com/assets/Admin-CVjFmNKZ.js` | 200 | 952,435 | `7e0fca72841a65d389dc6c9a4a06759c55c0c390ad9a932a9d91d957b83866d2` | `Thu, 01 Oct 2026 21:06:20 GMT` / `W/"e8873-6abecb4c-10ec5229be00d659;gz"` |
| Shared SPA entry | `https://music-prod.com/assets/index-9wpXYaKk.js` | 200 | 422,437 | `8b2d86627619cba3d730e0d12f4816373a87a2e9c36be8f318f607efddb70f0e` | `Thu, 01 Oct 2026 21:06:20 GMT` / `W/"67225-6abecb4c-4ede1eca287c99d0;gz"` |

Both served as `application/x-javascript` with `cache-control: public, max-age=31536000, immutable`. The shell/entry use Vite-style lazy import maps; the entry references the Admin chunk and shared vendor chunks. Response headers say `platform: hostinger`, `panel: hpanel`, `server: hcdn`, and `x-hcdn-cache-status: HIT`. The matching Last-Modified is a file timestamp, not a source revision or confirmed deployment timestamp.

## 2. Bundle fingerprints

Direct string and surrounding-code inspection of the current Admin asset found these independent fingerprints. Strings reflect minified bundle content, not reconstructed source files.

1. UI label `Plugin Releases`; navigation key `pluginreleases`.
2. Release UI labels `Create release`, `Create draft`, and empty state `No releases yet.`
3. Header copy: `Products, releases, files, licenses and audit history for Music-Prod Studio updates.`
4. Direct Supabase read of `release_products.select("*").order("name")`.
5. Direct Supabase read of `product_releases.select("*").order("created_at", {ascending:false})`.
6. Audit-log read `release_audit_log.select("*").order("created_at", {ascending:false}).limit(300)`.
7. Exact API base: `https://wfpeajmdojcjqyrsnxbk.supabase.co/functions/v1/music-prod-studio-api/v1${path}`; public project ref `wfpeajmdojcjqyrsnxbk`.
8. API helper gets the current Supabase session, supplies its bearer access token and anon `apikey`, accepts a JSON `{success,data}` envelope, and returns `data`.
9. Create request uses `POST /admin/releases` and payload keys `productId`, `version`, `buildNumber`, `channel`, `releaseNotes`; detail expects `release.id` and `release.buildNumber`.
10. Release-detail follow-up GET `/:id/validation`; validation issues are read from `issues`.
11. Lifecycle POST suffixes are `validate`, `publish`, `yank`, and `archive`; the UI vocabulary includes `draft`, `uploaded`, `validated`, `published`, `yanked`, and `archived`.
12. Artifact collection uses `POST /:id/artifacts` with `FormData` fields `file`, `fileName`, `platform`, `architecture`, `format`; artifact deletion uses `DELETE /:id/artifacts/:artifactId`.
13. UI artifact slot tuples include `macos/arm64/vst3`, `macos/arm64/au`, `macos/x86_64/vst3`, `macos/x86_64/au`, `macos/universal/vst3`, `macos/universal/au`, and `windows/x86_64/vst3`.
14. Artifact details render `fileName`, formatted `fileSize`, and `sha256` under the `SHA-256` label.
15. Release tabs are `Overview`, `Releases`, `Products`, `Licenses`, `Audit log`; release channels include `stable`, `beta`, and `development`.
16. Minified component structure includes `Gb` (release management page), `Xb` (release detail), and shared API helper `Kb`; these identifiers are build artifacts, not source symbol names.

No `sourceMappingURL`, repository URL, source path comment, commit SHA, or explicit application build/version constant was found in the Admin chunk. Ordinary dependency/sample version strings occur but do not identify the site build.

## 3. Local repository matches

### Strongest frontend candidate: not an authoritative match

| Local path | Branch / HEAD | Remote | Matching files / result | Assessment |
|---|---|---|---|---|
| [`Music-ProdPROJECT-GitHub`](../../../Music-ProdPROJECT-GitHub) | `main`, `9a1097f6de39d1ca81f4a5ec0db295220b7537f9` (2026-08-26); tracking `origin/main` points to same commit | `https://github.com/km98/pack-playhouse-9a0ed397.git` | [`src/pages/Admin.tsx`](../../../Music-ProdPROJECT-GitHub/src/pages/Admin.tsx), [`src/components/admin/AdminSidebar.tsx`](../../../Music-ProdPROJECT-GitHub/src/components/admin/AdminSidebar.tsx), Vite config and `dist` exist; no Plugin Releases component, nav key, distinctive table query, endpoint, or live labels. Local `dist/assets/Admin-wmAbaBlX.js` is 920,995 bytes, SHA-256 `fc42691997e16cc18cdea168f45aac98458546d5a9e0f16e81e468dd7ba56609`, and lacks all tested release fingerprints. | **Possible related site only; not source-identifying.** It has the general React/Vite SPA and `/admin`, but differs materially from live content and generated chunk. Checkout has unrelated dirty/untracked edits; they were left untouched. |
| same checkout, local extra branch | `phase8b-auth-only`, `827695ec6e464b9171d9b9091d34b8330e4bf98d` (2026-09-27), parent `9a1097f6` | Same origin | Exact source-string search and Admin/sidebar diff against main did not find the feature. | Newer local history exists, but is not the live Plugin Releases source. |
| [`Music-ProdPROJECT`](../../../Music-ProdPROJECT) | no usable `.git` metadata | none available | Deno function source under [`supabase/functions/music-prod-studio-api`](../../../Music-ProdPROJECT/supabase/functions/music-prod-studio-api) and local Vite app; `dist/assets/Admin-CURDpXPu.js` is 775,214 bytes, SHA-256 `dc5d4f0133cde7a8cb71b2f2f9f47f856ca78456e5e1831598445dd11e2b3e52`, without release UI fingerprints. | **Closest local Edge Function shape, but diverges from live API; no revision authority.** |
| [`C6.9A.11-Lovable-Handoff`](../../../C6.9A.11-Lovable-Handoff) | no Git metadata | none | Contains a Deno release router/service handoff, same older `ready/commit/withdraw` contract as local function. | **Possible backend lineage only; mismatch and no deployed revision evidence.** |
| [`Music-ProdStudio-Backend-tab2`](../../../Music-ProdStudio-Backend-tab2) | `studio-backend-jwt`, `f2b30ea2c0965aaa0ad15bc8117bb613a28e9930` (2026-09-30) | no configured remote | Node server API routes are under `/api/admin/releases`; no live `music-prod-studio-api/v1` URL, validation/publish/yank/archive contract, or Plugin Releases frontend. | Not evidenced as the live Edge Function or UI source. |
| [`Music-ProdStudio-Backend`](../../../Music-ProdStudio-Backend) | `main`, `b093eb93a14313d2aebb0603a2f23058b43831a2` (2026-09-28) | no configured remote | Shared Git worktree repository with the tab2 checkout; Node backend, not the live UI path. | Not evidenced as the deployed function source. |

Other discovered non-Git website snapshots include `Music-ProdPROJECT-old-20260816-103909`, `Music-ProdPROJECT-old-20260816-160746`, and `Music-ProdPROJECT-backup-20260816-162410`. Git-backed reconstructed native-app checkouts exist at `Music-Prod Studio-Reconstructed/tree` (`main`, `d51d4de73d32b976998244204cbb6e21441b940e`) and `tree-tab1-ui` (`studio-ui`, `ab959f67473b8d871a80c46a911be8adb8ed6bb5`), `tree-tab2-backend` (`studio-backend`, `eeb423b26115d659207b64d134c09248246d9eaf`), `tree-tab3-updater-qa` (`studio-updater-qa`, `c8ecf8f6053ed21673b9049929ff7188dd90c53c`), `tree-tab4-build` (`studio-tab4`, `41b2ab9c579e8c20fe0a4cc5ae5a92e3fe3a3db4`), `tree-tab5` (`studio-tab5`, `1c6aac4d78566591eb3891d5000c9f1593db8d16`), `tree-tab6` (`studio-tab6`, `e81f54a9e7a241b4257390e8dc8eac6918dacac4`), `tree-tab7` (`studio-tab7`, `1fa725ae8827ceecd2368f1daab151d670936e34`), and `tree-tab8` (`studio-tab8`, `5d6667ac1d93bdd3576bd2b2f8ea8a14aa6ac008`). They are native app worktrees/mirrors, not a found web Admin source. Their nested website snapshots were also searched for release UI strings and did not match. Their shared release catalog is desktop updater code, not the live web Admin bundle.

Exact source and structural fingerprint searches across discovered local Music-Prod website sources, candidate built Admin chunks, native mirrors, backend source, and available history found **no local file** matching the distinctive UI/API combinations. The two local built Admin assets use similar hashed Vite chunk naming, but have different names, sizes, hashes, source content, and chunk references. No local output matched the live hash.

The GitHub origin of the likely website checkout was tested with non-mutating `git ls-remote`; the request returned HTTP 403 and no refs. No fetch/clone was attempted. Thus its current remote-head accessibility/newer history cannot be independently confirmed from that request. Its locally recorded `origin/main` equals the checked-out August commit; its extra September branch is local-only in the available refs.

## 4. Deployment configuration

- **Confirmed delivery platform:** Live static assets carry Hostinger/HCDN headers (`platform: hostinger`, `panel: hpanel`, `server: hcdn`, `x-hcdn-*`). This confirms delivery through Hostinger/HCDN, not the source upload method.
- **Candidate build system:** Available Music-Prod web candidates use Vite/Rollup and `npm run build` (`vite build`), with Terser minification and manual React/query/UI/Supabase vendor chunks. Vite's default output is `dist`; local generated Admin chunk names are content-hashed. This is consistent with general bundle shape, not proof these candidates produced production assets.
- **Deployment mechanism:** **UNKNOWN.** Candidate source/config/workflow searches did not find Hostinger/HCDN-specific deployment instructions, FTP/SFTP, rsync, a music-prod.com deployment target, Vercel/Netlify/Cloudflare site config, or exact live asset names. No relevant GitHub Actions workflow was found in the likely frontend checkout. The host headers do not establish whether Hostinger Git deployment, GitHub-connected build, FTP/SFTP, or another CI/manual upload was used.
- **Build/revision evidence:** The live shared SPA/Admin asset timestamp is October 1, 2026. No exposed build ID, commit, artifact manifest, asset manifest, or deployment identifier ties that timestamp to a repository/branch/commit.

## 5. Git history

- `Music-ProdPROJECT-GitHub` known `main` commit is `9a1097f6` (`feat: add hierarchical VYRE preset browser`, 2026-08-26). A local branch `phase8b-auth-only` at `827695ec` (2026-09-27, `feat: product-neutral plugin auth (SideChain via /plugin/link)`) is newer than that inspected main commit, but neither branch's Admin files contain Plugin Releases. Exact `git log --all -S` searches for the live UI labels, table names, API endpoint, and response marker found no introducing commit.
- The Deno frontend/backend candidate `Music-ProdPROJECT` has no usable Git metadata, so there is no verified commit/branch/history for that snapshot.
- Backend native/node Git history contains unrelated release subsystem commits (e.g. `1680c97`, `2026-09-19`, “Backend baseline before JWT”; `f169b3e`, `2026-09-22`, “Add publication-invariant backstop and close preflight blind spots”; `7d0870c`, `2026-09-29`, “Add the release-candidate backend contract pack”). Those do not contain the live `/validation`, `/publish`, `/yank`, `/archive` contract and do not prove deployment to the Supabase function.
- The multiple reconstructed native worktrees have commit histories for the desktop update/catalog client. No commit there introduces the web bundle fingerprints.

## 6. Live backend fingerprint

- **Endpoint:** `https://wfpeajmdojcjqyrsnxbk.supabase.co/functions/v1/music-prod-studio-api`; live Admin bundle appends `/v1${path}`. Supabase project reference is `wfpeajmdojcjqyrsnxbk`.
- **Runtime evidence:** Public GET responses include `x-served-by: supabase-edge-runtime`, `x-sb-edge-region: eu-central-1`, and per-request `x-deno-execution-id`. No revision/build value appeared in headers or body.
- **Safe response observations:** Public GET `/v1/updates?product=sidechain&current=0.4.0&platform=macos` returned HTTP 200, 418-byte JSON. Envelope is `{success:true,data:{...}}`; data includes `product`, `currentVersion`, `latestVersion`, `updateAvailable`, `decision`, `mustUpdate`, `reason`, `mandatory`, `minimumSupportedVersion`, `releaseNotes`, `artifacts`, and extra fields `channel`, `latestBuildNumber`, `latestReleaseId`, `publishedAt`, `status`. `status` was `noPublishedRelease`; this is an endpoint response, not proof about private draft state. Public GET `/v1/admin/releases` returned HTTP 401 with `{success:false,error:"authentication is required for this endpoint",errorCode:"unauthenticated"}`. Only GETs were made.
- **Metadata probes:** GETs to function root, `/openapi.json`, `/swagger.json`, `/version`, and `/health` returned 404 `invalidRequest`; none exposed version or source metadata. Responses did not disclose deployment timestamp or source version.
- **Closest local source:** [`Music-ProdPROJECT/supabase/functions/music-prod-studio-api/router.ts`](../../../Music-ProdPROJECT/supabase/functions/music-prod-studio-api/router.ts) and its `releases.ts`/`artifactStorage.ts` are the closest runtime/path/table/envelope match. They implement a Deno Supabase Edge Function and release tables, but use `draft -> ready -> published -> withdrawn`, routes `/ready`, `/commit`, `/withdraw`, and old create fields such as `mandatory`/`minimumSupportedVersion`; local update response omits the live `channel`, `latestBuildNumber`, `latestReleaseId`, `publishedAt`, and `status` fields. They have no `/validation`, `/publish`, `/yank`, or `/archive` route and do not match the live lifecycle states. `C6.9A.11-Lovable-Handoff` is the same older contract. The Node service is still less close because it is a separate `/api/...` server.
- **Conclusion/confidence:** **STRONG EVIDENCE** that the named live backend is a Supabase Edge Function in the stated project; **UNKNOWN** deployed source repository and exact Edge Function revision. The local Deno snapshot is the nearest structural candidate, not confirmed production source.

## 7. Source map / manifest findings

- GET `Admin-CVjFmNKZ.js.map` and `index-9wpXYaKk.js.map`: HTTP 200 but both returned the 4,199-byte SPA HTML fallback, not map JSON. Neither JS bundle had a `sourceMappingURL` comment.
- `/manifest.json`: HTTP 200, 372-byte PWA manifest identifying only Music-Prod name/icons/start URL; no build ID.
- `/site.webmanifest`, `/manifest.webmanifest`, `/asset-manifest.json`, `/assets/manifest.json`, `/sw.js`, and `/service-worker.js` resolved to the SPA HTML fallback, not separate manifest/service-worker files.
- No public Vite asset manifest, commit/build metadata, or source map was found by these safe public GETs. Fallback HTTP 200 is not evidence those resources exist.

## 8. Authoritative source conclusion

- **Live bundle identity and behavior — CONFIRMED:** the publicly served asset at the URL and SHA-256 in §1 contains the UI/API fingerprints in §2.
- **Live static delivery via Hostinger/HCDN — CONFIRMED:** response headers identify the platform/CDN.
- **`Music-ProdPROJECT-GitHub` as the source repository — POSSIBLE only:** it is a related Vite Music-Prod site, but exact strings, structure, and current generated bundle do not match; no commit is attributable to the live asset.
- **Hostinger deployment mechanism — UNKNOWN:** headers identify hosting/CDN only; no public evidence proves Git integration vs manual/FTP/other CI.
- **Supabase Edge Function service/project — CONFIRMED:** public response headers and the live Admin bundle identify runtime/project/function.
- **Deployed Edge Function repository/revision — UNKNOWN:** no revision metadata; available local Deno source materially diverges.
- **Exact frontend source repository/branch/commit — UNKNOWN.** No exact local output hash or source match was found. Do not infer authority from product naming or Vite similarity.

## 9. Exact next implementation target

**Not identified. No frontend or backend source file is named as a safe implementation target.** Obtain the source checkout/revision that generated `Admin-CVjFmNKZ.js` and the deployed `music-prod-studio-api` revision before editing. In particular, do not assume the current `Music-ProdPROJECT-GitHub` Admin files or stale local Deno router are authoritative.

## 10. Change confirmation

**No source, database, storage, artifact, migration, release, or deployment state was changed during this investigation.** Temporary downloaded bundles were confined to temporary workspace directories and removed after inspection. No GUI application was opened or used.
