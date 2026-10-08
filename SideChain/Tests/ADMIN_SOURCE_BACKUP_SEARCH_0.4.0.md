# Admin Source Backup Search

**Search date:** 2026-10-02  
**Scope:** Read-only inventory, source/string searches, Git metadata inspection, generated-build hash comparison, metadata filenames/schema, and archive member inspection. No archive was extracted over a project. No credentials/secrets were inspected or recorded. No GUI was used.

## 1. Search locations

The available workspace root is `/Users/martin/Documents` (from local Freebuff project metadata; it contained no Music-Prod thread/worktree paths). The local search covered these Music-Prod areas and nearby development/backup locations:

- Website source checkouts/snapshots: [`Music-ProdPROJECT-GitHub`](../../../Music-ProdPROJECT-GitHub), [`Music-ProdPROJECT`](../../../Music-ProdPROJECT), [`Music-ProdPROJECT-old-20260816-103909`](../../../Music-ProdPROJECT-old-20260816-103909), [`Music-ProdPROJECT-old-20260816-160746`](../../../Music-ProdPROJECT-old-20260816-160746), [`Music-ProdPROJECT-backup-20260816-162410`](../../../Music-ProdPROJECT-backup-20260816-162410), and `Music-ProdPROJECT-backup-20260816-162410/.workspace`.
- Backend workspaces: [`Music-ProdStudio-Backend`](../../../Music-ProdStudio-Backend), [`Music-ProdStudio-Backend-tab2`](../../../Music-ProdStudio-Backend-tab2), and backend source in the two dated baseline archives below.
- Reconstructed native workspace: [`Music-Prod Studio-Reconstructed`](../../../Music-Prod%20Studio-Reconstructed), its eight Git worktrees (`tree`, `tree-tab1-ui`, `tree-tab2-backend`, `tree-tab3-updater-qa`, `tree-tab4-build`, `tree-tab5`, `tree-tab6`, `tree-tab7`, `tree-tab8`), nested website source snapshots, and `quarantine`.
- User backup folder: [`Backups`](../../../Backups) (only PlugInspect SQL files; no Music-Prod web project backup).
- Agent/workspace folders: [`Codex`](../../../Codex), [`Claude`](../../../Claude), `.freebuff` project/thread metadata in query-only mode, and repository `.lovable` metadata filenames. No prompt/message bodies or credentials were used to discover project paths. Freebuff project metadata only reported root `/Users/martin/Documents`; no matching project thread path was recorded.
- Other workspace directories were inventoried by project/repository and archive filenames to identify additional Music-Prod material. Unrelated DAW/plugin media and source trees were excluded from source-content searching.

Local hidden Lovable plans exist in the five website candidate folders, with file metadata ranging from August 16–17. Their contents were not available through the workspace file reader, so no claim is made from them. No editor workspace file or project metadata disclosed another Music-Prod source location.

## 2. Repositories discovered

| Path | Branch / HEAD | Purpose / relevance | Fingerprint result |
|---|---|---|---|
| [`Music-ProdPROJECT-GitHub`](../../../Music-ProdPROJECT-GitHub) | `main` / `9a1097f6de39d1ca81f4a5ec0db295220b7537f9` (Aug 26); extra local branch `phase8b-auth-only` / `827695ec6e464b9171d9b9091d34b8330e4bf98d` (Sep 27). `origin/main` is locally recorded at main HEAD. | Related Vite/React website and Admin route. Existing checkout has unrelated uncommitted changes; all left untouched. Remote `git ls-remote` from prior Phase 1D inspection returned 403; no fetch was performed. | Exact visible strings, route key, table/query combination, live endpoint, and response fields absent from source, both local refs, and generated Admin chunk. **POSSIBLE site lineage only; no production link.** |
| [`Music-ProdStudio-Backend`](../../../Music-ProdStudio-Backend) | `main` / `b093eb93a14313d2aebb0603a2f23058b43831a2` | Node backend, shared linked worktree with tab2. | Has a release API subsystem but not the live Supabase function route/contract. **NO MATCH** to the live Admin. |
| [`Music-ProdStudio-Backend-tab2`](../../../Music-ProdStudio-Backend-tab2) | `studio-backend-jwt` / `f2b30ea2c0965aaa0ad15bc8117bb613a28e9930` | Node backend worktree. No configured remote. | Release routes are under `/api/admin/releases`, unlike the live Edge Function URL and lifecycle/validation contract. **NO MATCH** to live Admin source. |
| [`Music-Prod Studio-Reconstructed/tree`](../../../Music-Prod%20Studio-Reconstructed/tree) | `main` / `d51d4de73d32b976998244204cbb6e21441b940e` | Native Studio reconstruction baseline. | Desktop update client and Deno snapshot mirror only; no web Admin component. **NO MATCH.** |
| `Music-Prod Studio-Reconstructed/tree-tab1-ui` | `studio-ui` / `ab959f67473b8d871a80c46a911be8adb8ed6bb5` | Native Studio UI worktree. | No web Admin fingerprints. **NO MATCH.** |
| `Music-Prod Studio-Reconstructed/tree-tab2-backend` | `studio-backend` / `eeb423b26115d659207b64d134c09248246d9eaf` | Native Studio backend worktree. | Release-client/backend references are not the live web admin UI. **NO MATCH.** |
| `Music-Prod Studio-Reconstructed/tree-tab3-updater-qa` | `studio-updater-qa` / `c8ecf8f6053ed21673b9049929ff7188dd90c53c` | Native update QA and reports. | Contains SideChainer distribution/update documentation; no live web source or exact live Admin fingerprints. **POSSIBLE contextual docs, not source.** |
| `Music-Prod Studio-Reconstructed/tree-tab4-build` | `studio-tab4` / `41b2ab9c579e8c20fe0a4cc5ae5a92e3fe3a3db4` | Native build worktree. | No web Admin source. **NO MATCH.** |
| `Music-Prod Studio-Reconstructed/tree-tab5` | `studio-tab5` / `1c6aac4d78566591eb3891d5000c9f1593db8d16` | Native app worktree. | No web Admin source. **NO MATCH.** |
| `Music-Prod Studio-Reconstructed/tree-tab6` | `studio-tab6` / `e81f54a9e7a241b4257390e8dc8eac6918dacac4` | Native release/download safety worktree. | Desktop release client, not live web Admin. **NO MATCH.** |
| `Music-Prod Studio-Reconstructed/tree-tab7` | `studio-tab7` / `1fa725ae8827ceecd2368f1daab151d670936e34` | Native Studio worktree. | No web Admin source. **NO MATCH.** |
| `Music-Prod Studio-Reconstructed/tree-tab8` | `studio-tab8` / `5d6667ac1d93bdd3576bd2b2f8ea8a14aa6ac008` | Native Studio worktree. | No web Admin source. **NO MATCH.** |
| `Music-ProdPROJECT` root checkout and its nested reconstruction mirrors | No usable Git metadata at root; Deno/Vite source snapshot. | Website and `music-prod-studio-api` candidate. Nested copies occur in native workspace mirrors. | Backend tables/routes share lineage but live lifecycle and response fields differ. No Admin UI strings. **POSSIBLE backend ancestor only.** |

The `phase8b-auth-only` worktree path in Git metadata points to `/private/tmp/mp-phase9-auth`, which is currently absent/prunable; its stored commit object was searched directly with `git grep` and does not contain live Admin strings. No checkout or worktree restoration was attempted.

## 3. Archives discovered

The two relevant Music-Prod baseline archives were inspected by listing members and streaming safe text/source members directly; neither was extracted.

| Archive | Format / file metadata | Relevant contents | Result |
|---|---|---|---|
| [`Music-Prod Studio-Reconstructed-baseline-before-git-2026-09-19.tar.gz`](../../../Music-Prod%20Studio-Reconstructed-baseline-before-git-2026-09-19.tar.gz) | gzip tar; 791,661 bytes; filesystem timestamp 2026-09-19 10:33:50 | Contains a Music-Prod native reconstruction, nested Music-Prod website snapshots, and older Node release routes/tests. | No `Plugin Releases`, `Create release`, `No releases yet.`, `/validation`, `latestBuildNumber`, `music-prod-studio-api/v1`, or live Admin chunk. Release-table names occur in backend migration/docs/source; they do not match the live Admin UI. No generated Vite Admin/index chunk member was found. |
| [`Music-ProdStudio-Backend-baseline-before-git-2026-09-19.tar.gz`](../../../Music-ProdStudio-Backend-baseline-before-git-2026-09-19.tar.gz) | gzip tar; 5,956,691 bytes; filesystem timestamp 2026-09-19 11:15:46 | Historical Node backend, `src/routes/admin-releases.ts`, release services/tests, and backend JS source maps. | Contains older Node release API only. No live Admin bundle/UI fingerprints. Its generated maps are Node backend maps, not source maps for the web chunk. |

Other archive filenames in the workspace were enumerated. They were predominantly plugin binaries, benchmark/sample content, or SideChainer product zips. SideChainer ZIPs were inspected only as archives for member names/code assets and contained no live Admin JavaScript or source. No Music-Prod source archive was found in `Backups/PlugInspect` or the `Codex` task directories. The Deno Edge Function source appears in `Music-ProdPROJECT` snapshot and repeated reconstruction copies rather than a distinct source archive.

## 4. Build outputs discovered

| Build output | Admin asset | Bytes / SHA-256 | Live SHA match? | Fingerprint result |
|---|---|---:|---|---|
| [`Music-ProdPROJECT-GitHub/dist`](../../../Music-ProdPROJECT-GitHub/dist) | `assets/Admin-wmAbaBlX.js` | 920,995 / `fc42691997e16cc18cdea168f45aac98458546d5a9e0f16e81e468dd7ba56609` | **No** | Does not contain `Plugin Releases`, `No releases yet.`, release table queries or live endpoint. `dist/index.html` references another index chunk, `index-ypcZZx3-.js` (485,288 bytes; `c6d737ac…`), plus vendor chunks.
| [`Music-ProdPROJECT/dist`](../../../Music-ProdPROJECT/dist) | `assets/Admin-CURDpXPu.js` | 775,214 / `dc5d4f0133cde7a8cb71b2f2f9f47f856ca78456e5e1831598445dd11e2b3e52` | **No** | No distinctive Plugin Releases UI/API fingerprints. `dist/index.html` references `index-VPbDm_aT.js` (347,970 bytes; `4f608725…`), plus vendor chunks.
| Older Aug 2026 website snapshots | No matching `dist` Admin bundle was present at the inspected output paths. | N/A | **No match found** | Source searches found no live UI strings/table access in these snapshots. |
| Dated native/Node build outputs and archive members | Node JS output and JS maps exist for backend; native `.build` app bundles are unrelated. | N/A | **No** | No Admin web bundle or exact chunk identity. |

A candidate output inventory found only the two local website Admin chunks above (outside duplicate reconstructed mirrors); no exact filename `Admin-CVjFmNKZ.js` or `index-9wpXYaKk.js` was found. Hash comparison of both outputs against live Admin SHA-256 found no exact match. No compatible Vite manifest/source map or chunk metadata tied a local build to the live files.

## 5. Source matches

### Exact visible/source strings

Searched source, documentation, historical worktree content, generated output, and archive members for `Plugin Releases`, `Create release`, `No releases yet.`, plus structural fingerprints `release_products`, `product_releases`, `release_artifacts`, `music-prod-studio-api/v1`, `/validation`, `/publish`, `/yank`, `/archive`, `latestBuildNumber`, and `noPublishedRelease`.

- **No source match** was found for the distinctive live UI copy, live API base, live update response field, or live lifecycle combination in any local frontend project, historical frontend Git refs, backup snapshot, or baseline archive.
- **Partial backend matches — POSSIBLE lineage only:** [`Music-ProdPROJECT/supabase/functions/music-prod-studio-api/releases.ts`](../../../Music-ProdPROJECT/supabase/functions/music-prod-studio-api/releases.ts), its [`router.ts`](../../../Music-ProdPROJECT/supabase/functions/music-prod-studio-api/router.ts), and `artifactStorage.ts` use the same release table names and the `{success,data}` envelope. But local lifecycle is `draft -> ready -> published -> withdrawn` via `/ready`, `/commit`, `/withdraw`; it lacks `/validation`, `/publish`, `/yank`, `/archive`, the live status vocabulary, and live `channel`/`latestBuildNumber`/`latestReleaseId`/`status` output. This is not a live source match.
- [`Music-ProdStudio-Backend-tab2/src/routes/admin-releases.ts`](../../../Music-ProdStudio-Backend-tab2/src/routes/admin-releases.ts) and Node backend history have the generic `/api/admin/releases` system, but not the exact live Edge Function endpoint or UI. **NO MATCH** for the live frontend.
- [`Music-ProdPROJECT-GitHub/supabase/migrations/20261001140000_sidechain_studio_release_catalog.sql`](../../../Music-ProdPROJECT-GitHub/supabase/migrations/20261001140000_sidechain_studio_release_catalog.sql) contains SideChainer catalog terminology and release table names, but no Admin UI. It establishes local planned/catalog relationship only, not production bundle authorship.
- Release-related docs in reconstructed native worktrees describe the desktop updater/API and SideChainer packaging; they are documentation, not proof of live UI implementation/deployment.

At least 15 independent live bundle fingerprints (UI labels, audit/product/release selects, API base and envelope, create payload keys, validation/issues, artifact multipart fields, delete route, lifecycle suffixes/statuses, supported platform/architecture/format slots, audit log query, and update response fields) were checked against these available sources and outputs. The combination did not match any frontend source.

## 6. Git history findings

- `Music-ProdPROJECT-GitHub` history across locally available refs was searched with `git log --all -S` / `git grep`. No commit introduces `Plugin Releases`, `No releases yet.`, the live API URL, `release_products`/`product_releases` queries, or live `latestBuildNumber` response handling in website source. Current `main` is `9a1097f6` (2026-08-26); local extra branch is `827695ec` (2026-09-27), which changes product-neutral plugin auth but does not add the live Admin feature.
- The GitHub remote was not fetched. A previous read-only `git ls-remote` attempt returned HTTP 403; remote accessibility and any unadvertised/unavailable remote history remain unknown.
- `Music-ProdPROJECT` has no usable Git metadata. Historical release source cannot be attributed to a particular commit there.
- Backend Git history has older release management commits (for example `1680c97`, 2026-09-19, “Backend baseline before JWT”; `f169b3e`, 2026-09-22, “Add publication-invariant backstop and close preflight blind spots”; `7d0870c`, 2026-09-29, “Add the release-candidate backend contract pack”). They are Node backend/desktop release work and do not introduce the live Admin component or current Edge API contract.
- The reconstructed native worktree commits from September are for desktop updater/release components. No web Admin implementation matching live strings appears in their tracked source.

## 7. Backend history findings

- **Present local Deno version:** [`Music-ProdPROJECT/supabase/functions/music-prod-studio-api`](../../../Music-ProdPROJECT/supabase/functions/music-prod-studio-api) with `index.ts`, `router.ts`, `releases.ts`, `artifactStorage.ts`, auth/types/errors, and tests. This is the nearest implementation by function name, Supabase Edge runtime, table vocabulary and response envelope, but is structurally older than live API as described above. It has no usable Git SHA at that source root.
- **Dated Deno handoff:** [`C6.9A.11-Lovable-Handoff`](../../../C6.9A.11-Lovable-Handoff) has an older Deno router/release-service snapshot with the same older transitions. It does not match live response/routes.
- **Node versions:** Current Node backend repo/worktrees and the 2026-09-19 backend baseline archive include `/api/admin/releases` logic and artifacts/releases tables. These do not prove a deployment to the named Supabase Edge Function.
- **Remote deployed source/revision:** **UNKNOWN.** The public Edge response metadata from the prior Phase 1D trace exposed Supabase runtime and region only. No source SHA, function version or deploy timestamp was available.

## 8. Workspace/deployment clues

- The filesystem includes Lovable plan metadata in the website candidate roots; file names/size/timestamps were observed, but contents were inaccessible via the provided file reader. No conclusion about project ID or deploy linkage is drawn from those files.
- Freebuff local project metadata recorded only the shared Documents root and no Music-Prod-specific thread path. Codex task directories and workspace names were inspected; none corresponded to another Music-Prod web project.
- Candidate Vite configs use React/SWC, Vite/Rollup hashed `dist/assets` chunks, Terser, and manual shared vendor chunks. This is consistent with general live Vite-style bundle structure, but both current generated websites have different outputs and absent UI strings. No Hostinger deployment script, music-prod.com upload target, FTP/SFTP, rsync, CI site deploy workflow, or live chunk manifest was located.
- Existing SideChainer distribution reports mention the site/release catalog and production URL context but do not name a source workspace or prove the live Admin bundle came from a local revision.
- Live delivery headers identifying Hostinger/HCDN remain evidence of hosting/CDN only, not CI or source-repository identity.

## 9. Live source conclusion

| Claim | Classification | Reason |
|---|---|---|
| The known public Admin chunk is the supplied live asset identity | **CONFIRMED** | Prior phase measured its exact URL, byte size, SHA-256 and static bundle contents. |
| Live site belongs to a Music-Prod React/Vite-style SPA served from Hostinger/HCDN | **STRONG EVIDENCE** | Shared SPA shell, Vite-like hashed/lazy chunks, and Hostinger/HCDN response headers. This does not identify the source workspace or deployment method. |
| `Music-ProdPROJECT-GitHub` is a related website checkout | **POSSIBLE** | It has the general SPA/Admin route and Vite build system, but source and output materially differ and no live Admin fingerprints were found. |
| A local frontend source, local build, archive, or historical commit generated the exact live Admin bundle | **UNKNOWN / NO MATCH FOUND** | No exact SHA, file name, distinctive source/API structure, or introducing Git commit was found in searched local candidates or inspected archives. |
| The exact live frontend repository/branch/commit can now be identified | **UNKNOWN — NOT IDENTIFIED** | No authoritative source path/revision evidence found. |
| The deployed Edge Function repository/revision | **UNKNOWN — NOT IDENTIFIED** | Local Deno candidate diverges; live runtime gives no revision/build identifiers. |

**The live Admin source was not found in the searched local repositories, build outputs, archives, workspace metadata, or local Git history.** This is bounded to the accessible `/Users/martin/Documents` workspace and listed archives; it cannot rule out an external/remote workspace or source-control history that was not locally available.

## 10. Exact next implementation target

**None identified.** No source file, repository/branch, or commit can be safely named as the producer of the live Admin asset. Do not edit the current website Admin files or older Deno release router on this evidence. Obtain the original Lovable/site workspace or authoritative source/revision plus deployed Edge Function revision before implementation.

## 11. Unresolved items

1. Which repository/workspace and commit produced `Admin-CVjFmNKZ.js` and `index-9wpXYaKk.js`.
2. Whether the unavailable local Lovable plan metadata or a remote Lovable workspace identifies that source project.
3. Whether an older/deleted local directory or remote Git history outside this workspace retains the UI source.
4. Which build/deploy path copied the Vite output to Hostinger/HCDN.
5. The exact deployed `music-prod-studio-api` source revision and why its live response/lifecycle differs from all available local versions.
6. Whether the relevant source is held in an external account/service; no private account or credential was accessed.

## 12. Change confirmation

No source, database, storage, artifact, migration, release, deployment, or production configuration was changed during this search.
