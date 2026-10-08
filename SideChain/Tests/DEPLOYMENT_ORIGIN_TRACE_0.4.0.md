# Music-Prod Deployment Origin Trace

**Trace date:** 2026-10-02  
**Scope:** Read-only local shell-history, repository, build-output, script/configuration, and prior public-response evidence. No GUI or private external system was accessed. Shell-history inspection suppressed raw command arguments and secret-shaped content. This report is the only file created for this task.

## 1. Shell history evidence

### Files and limits

- `~/.zsh_history` was available: 619,740 bytes, 17,374 physical lines. It has **zero zsh extended-history timestamp markers**, so command dates cannot be established from it.
- `~/.psql_history` was available: 1,974 bytes, 21 lines; none of the deployment/domain/function search terms matched.
- The broad deployment keyword scan of zsh history matched 653 lines; many were copied source/code or other non-deployment text. They were not treated as executed commands. Raw entries and arguments were not reproduced.

### Sanitized command classification

- Keyword counts included 38 lines containing `music-prod.com`, 8 containing `Hostinger`, 5 containing the Supabase project reference, and **zero** containing `music-prod-studio-api`; exact live asset filename hits were zero. These counts include copied text and are not counts of deployment attempts.
- Two command-shaped `ssh` entries were found; no safe host/project association connected them to `music-prod.com`, Hostinger, or the Supabase function. Their arguments were not printed.
- One package-manager build-shaped entry was found, but the nearby history did not establish a Music-Prod source directory or deployment destination. It has no usable timestamp. One `cp`-shaped line matched the broad Hostinger text search and referenced `.env.local`; it was not an upload command and no production destination was established.
- No command-shaped `scp`, `sftp`, `rsync`, `ftp`, or `lftp` transfer to the Music-Prod domain/document root was identified.
- No `supabase functions deploy music-prod-studio-api` or `deno deploy` command was identified.
- No occurrence of either exact live asset filename (`Admin-CVjFmNKZ.js`, `index-9wpXYaKk.js`) was found in shell history.
- Shell-history matches for `music-prod.com` and Hostinger do not form a source-path → build → upload chain. No history entry establishes the production local source path or destination directory. Dates are unavailable, not merely omitted.

## 2. Deployment scripts

### Frontend candidates

- [Music-ProdPROJECT-GitHub/package.json](../../../Music-ProdPROJECT-GitHub/package.json) defines `"build": "vite build"`; [vite.config.ts](../../../Music-ProdPROJECT-GitHub/vite.config.ts) configures Vite/Rollup output and does not set a custom `outDir`, so Vite's default output is `dist/`. This establishes a local build capability, **not that this build was used for production**.
- Its [README.md](../../../Music-ProdPROJECT-GitHub/README.md) is a generic Lovable template whose project URL is still a placeholder; it says Lovable's Share → Publish is the deployment route for that template. It does not identify a Lovable project ID or bind that workflow to the live domain.
- No project-specific GitHub Actions/site CI workflow or local shell/package task that uploads this frontend to `music-prod.com` was found in the inspected candidate roots.
- The Deno/Vite [Music-ProdPROJECT/package.json](../../../Music-ProdPROJECT/package.json) also defines `vite build`; it likewise provides no evidence of a production web upload.

### Supabase function

- [Music-ProdPROJECT/supabase/config.toml](../../../Music-ProdPROJECT/supabase/config.toml) registers `[functions.music-prod-studio-api]` with `verify_jwt = false`, and its [function entry point](../../../Music-ProdPROJECT/supabase/functions/music-prod-studio-api/index.ts) is a Deno Edge Function. Config registration is not a deployment record.
- No `supabase functions deploy music-prod-studio-api` command was found in the inspected shell history or relevant script/config/documentation searches.
- Native workspace [Scripts/publish-2.0.2.sh](../../../Music-Prod%20Studio-Reconstructed/tree-tab2-backend/Scripts/publish-2.0.2.sh) is a *Music-Prod Studio release/artifact publication* script that calls the existing API. It is not an Edge Function deployment command and does not identify the web frontend's source or upload route. It was not executed; its credential-bearing/raw contents are intentionally not reproduced.

### Historical Git clues

The local website Git history has domain/hosting-related commit subjects, but none supplies a complete publish job or a live-asset-to-commit mapping:

- `2ecb498d` (2026-08-12, “Moved admin to music-prod.com”) changes only `src/components/admin/AdminSidebar.tsx` according to its commit stat; it is not a static-site upload record.
- `9b359864` (2026-03-02, “Migrate hosting to Hostinger”) changes three application components' URLs, not a deployment workflow.
- `75be2e33` (2026-02-25, “Add SPA redirect for Hostinger”) adds `public/.htaccess` with an Apache rewrite fallback to `index.html`. This is evidence the checkout was configured for SPA routing on an Apache-style host, but does not identify its deployed directory, how files were transferred, or whether this copy is the currently served site.
- `aa76d7d0` (“Deployed admin-operations & migs”) and `aa401607` (“Verified deploy and credit migration”) have database/function-related files in their commit stats; they do not record the current static bundle publication or the deployed `music-prod-studio-api` revision.

No command or record closes the chain `source directory → build → dist → Hostinger upload/deploy → live asset hashes`.

## 3. Hostinger evidence

- **Live delivery:** the prior public asset trace recorded `platform: hostinger`, `panel: hpanel`, `server: hcdn`, and `x-hcdn-*` headers on the live site. This confirms Hostinger/HCDN delivery, not a repository binding or upload mechanism.
- **Historical local configuration:** the website Git history contains the Hostinger-titled commits above; `public/.htaccess` defines an SPA rewrite. [The local CORS allowlist](../../../Music-ProdPROJECT-GitHub/supabase/functions/_shared/cors.ts#L19-L25) includes `music-prod.com` and labels `limegreen-dunlin-192240.hostingersite.com` as a legacy Hostinger domain. These are domain/hosting clues only; the CORS file does not configure file deployment.
- Searches of relevant local Music-Prod source, scripts, docs, package tasks, and tracked workflow/config candidates did not identify a Music-Prod FTP/SFTP/SSH host, `public_html`/`htdocs`/`www` document root, Hostinger deployment job, or upload target. No Hostinger account was accessed.
- Separate Hostinger deployment notes found in a nested `charge-mate-rewards` project concern a different application/domain and are not evidence about `music-prod.com`.
- **Production mechanism: UNKNOWN.** Git-connected Hostinger deployment, Lovable publish, manual file-manager upload, FTP/SFTP, and another CI/manual process cannot be distinguished from the available evidence.

## 4. Frontend build origin

### Exact live fingerprints

From [LIVE_ADMIN_SOURCE_FINGERPRINT_0.4.0.md](LIVE_ADMIN_SOURCE_FINGERPRINT_0.4.0.md):

| Live asset | Bytes | SHA-256 | HTTP Last-Modified |
|---|---:|---|---|
| `Admin-CVjFmNKZ.js` | 952,435 | `7e0fca72841a65d389dc6c9a4a06759c55c0c390ad9a932a9d91d957b83866d2` | 2026-10-01 21:06:20 GMT |
| `index-9wpXYaKk.js` | 422,437 | `8b2d86627619cba3d730e0d12f4816373a87a2e9c36be8f318f607efddb70f0e` | 2026-10-01 21:06:20 GMT |

The timestamp is HTTP file metadata, not a proven build or deployment time.

### Local output comparison

| Candidate path | Local Admin output | Bytes / SHA-256 | Local mtime | Comparison |
|---|---|---|---|---|
| [Music-ProdPROJECT-GitHub/dist](../../../Music-ProdPROJECT-GitHub/dist) | `assets/Admin-wmAbaBlX.js` | 920,995 / `fc42691997e16cc18cdea168f45aac98458546d5a9e0f16e81e468dd7ba56609` | Oct 1 2026, 23:24 CEST (21:24 UTC) | Not the live size/hash; lacks the checked Plugin Releases UI/API fingerprints. |
| [Music-ProdPROJECT/dist](../../../Music-ProdPROJECT/dist) | `assets/Admin-CURDpXPu.js` | 775,214 / `dc5d4f0133cde7a8cb71b2f2f9f47f856ca78456e5e1831598445dd11e2b3e52` | Oct 1 2026, 23:20 CEST (21:20 UTC) | Not the live size/hash; lacks the checked Plugin Releases UI/API fingerprints. |

The two inspected local output directories have different index chunks too; neither output contains the live Admin filename or matching SHA. Their recorded local mtimes are 14–18 minutes *after* the live `Last-Modified` time, so they are not copies of those exact live assets. That timing does not rule out an earlier build from related source. No exact live Admin/index match or suitable adjacent live-chunk manifest was found in the inspected Music-Prod candidate outputs.

- **Best frontend repository candidate — POSSIBLE, not confirmed origin:** [`Music-ProdPROJECT-GitHub`](../../../Music-ProdPROJECT-GitHub) is a related Vite/React site. Its two available local refs are `main` at `9a1097f6de39d1ca81f4a5ec0db295220b7537f9` (2026-08-26) and `phase8b-auth-only` at `827695ec6e464b9171d9b9091d34b8330e4bf98d` (2026-09-27). The current checked-out Admin source/build does not contain Plugin Releases. The checkout was already dirty; it was left untouched. Its configured origin is `github.com/km98/pack-playhouse-9a0ed397.git`; remote refs were not fetched, and an earlier read-only `git ls-remote` had returned 403.
- Historical Hostinger/domain commits show that this repository has been associated with the Music-Prod website and static SPA hosting. They do **not** prove that this checkout/revision generated the October 1 assets.

## 5. Edge Function deployment origin

- **Live function identity — CONFIRMED:** prior read-only public response evidence and the live Admin bundle identify Supabase project `wfpeajmdojcjqyrsnxbk`, Edge Function `music-prod-studio-api`, and Supabase Edge Runtime. This names the live service, not its source revision.
- **Closest local function candidate — POSSIBLE lineage:** [`Music-ProdPROJECT/supabase/functions/music-prod-studio-api`](../../../Music-ProdPROJECT/supabase/functions/music-prod-studio-api), with `index.ts`, `router.ts`, `releases.ts`, and `artifactStorage.ts`; [config.toml](../../../Music-ProdPROJECT/supabase/config.toml) registers the same function name. This snapshot has no usable Git commit metadata and its API lifecycle/response differs from the live bundle/response: local routes use `ready`/`commit`/`withdraw`, while the live Admin expects `validation`/`publish`/`yank`/`archive` and additional build/channel/status fields. It is not a source revision match.
- [Music-ProdPROJECT-GitHub/supabase/config.toml](../../../Music-ProdPROJECT-GitHub/supabase/config.toml) does not register `music-prod-studio-api`; no deployment command for that function was found in its tracked sources/history or the inspected local deployment references.
- Native Studio runbooks/scripts reference the same live endpoint and a planned publication workflow, but they are Studio client/release documentation, not evidence of which checkout deployed the Edge Function. No deploy timestamp, deployment ID, or source SHA was available locally.
- **Deployed function repository/branch/commit: UNKNOWN.** No source → `supabase functions deploy` → project/revision chain was established.

## 6. Repository candidates

Paths below are relative to the workspace root. Git metadata are local-only; no refs were fetched. “Deployment evidence” distinguishes historical hosting clues from proof of this live deployment.

| Candidate | Local repository/path and ref | Deployment evidence | Classification |
|---|---|---|---|
| Frontend | [`Music-ProdPROJECT-GitHub`](../../../Music-ProdPROJECT-GitHub) — `main` `9a1097f6de39d1ca81f4a5ec0db295220b7537f9` (2026-08-26); local `phase8b-auth-only` `827695ec6e464b9171d9b9091d34b8330e4bf98d` (2026-09-27). | Vite build to `dist`, Hostinger SPA `.htaccess` in history, Hostinger/domain-related commit subjects. No production upload record; no Plugin Releases source/hash match. | **POSSIBLE** related site lineage; live source origin **UNKNOWN**. |
| Deno/website snapshot | [`Music-ProdPROJECT`](../../../Music-ProdPROJECT) — no usable Git root/branch/HEAD. | Vite build and matching function config/source name; Deno API is older than live contract. No deploy command or source revision. | **POSSIBLE** backend ancestor; deployed source **UNKNOWN**. |
| Older website snapshots | [`Music-ProdPROJECT-old-20260816-103909`](../../../Music-ProdPROJECT-old-20260816-103909), [`Music-ProdPROJECT-old-20260816-160746`](../../../Music-ProdPROJECT-old-20260816-160746), [`Music-ProdPROJECT-backup-20260816-162410`](../../../Music-ProdPROJECT-backup-20260816-162410) — no usable Git metadata. | No concrete site deployment target or exact live asset match identified. | **UNKNOWN / not evidenced as origin.** |
| Lovable handoff | [`C6.9A.11-Lovable-Handoff`](../../../C6.9A.11-Lovable-Handoff) — no usable Git metadata. | Older Deno release API handoff; no live Admin bundle or upload/deploy record. | **POSSIBLE** historical backend lineage only. |
| Node backend worktrees | [`Music-ProdStudio-Backend`](../../../Music-ProdStudio-Backend), `main` `b093eb93a14313d2aebb0603a2f23058b43831a2` (2026-09-28); [`Music-ProdStudio-Backend-tab2`](../../../Music-ProdStudio-Backend-tab2), `studio-backend-jwt` `f2b30ea2c0965aaa0ad15bc8117bb613a28e9930` (2026-09-30). Neither has a configured remote. | Node `/api/admin/releases` service, no production site/Edge Function deployment record; contract differs from live Supabase endpoint. | **UNKNOWN / not evidenced as origin.** |
| Native reconstruction baseline | [`Music-Prod Studio-Reconstructed/tree`](../../../Music-Prod%20Studio-Reconstructed/tree) — `main`, HEAD `d51d4de73d32b976998244204cbb6e21441b940e` (2026-09-19). | Native desktop baseline; no configured remote or web deployment config. | **UNKNOWN / not frontend origin.** |
| Native UI worktree | [`tree-tab1-ui`](../../../Music-Prod%20Studio-Reconstructed/tree-tab1-ui) — `studio-ui`, HEAD `ab959f67473b8d871a80c46a911be8adb8ed6bb5` (2026-09-30). | Native UI worktree; no configured remote or web deployment config. | **UNKNOWN / not frontend origin.** |
| Native backend worktree | [`tree-tab2-backend`](../../../Music-Prod%20Studio-Reconstructed/tree-tab2-backend) — `studio-backend`, HEAD `eeb423b26115d659207b64d134c09248246d9eaf` (2026-09-19). | Native/backend worktree; runbooks reference the API, but no web deployment config or Edge deploy command was found. | **UNKNOWN / not frontend origin.** |
| Native updater/QA worktree | [`tree-tab3-updater-qa`](../../../Music-Prod%20Studio-Reconstructed/tree-tab3-updater-qa) — `studio-updater-qa`, HEAD `c8ecf8f6053ed21673b9049929ff7188dd90c53c` (2026-09-30). | QA/release docs and client-side endpoint references; no web frontend deploy config. | **UNKNOWN / not frontend origin.** |
| Native build worktree | [`tree-tab4-build`](../../../Music-Prod%20Studio-Reconstructed/tree-tab4-build) — `studio-tab4`, HEAD `41b2ab9c579e8c20fe0a4cc5ae5a92e3fe3a3db4` (2026-09-30). | Desktop build worktree; no web deployment config. | **UNKNOWN / not frontend origin.** |
| Native Studio worktree | [`tree-tab5`](../../../Music-Prod%20Studio-Reconstructed/tree-tab5) — `studio-tab5`, HEAD `1c6aac4d78566591eb3891d5000c9f1593db8d16` (2026-09-30). | Desktop client/release tooling; `Scripts/publish-2.0.2.sh` targets the existing API for artifact publication, not function deployment or website upload. | **UNKNOWN / not frontend origin.** |
| Native Studio worktree | [`tree-tab6`](../../../Music-Prod%20Studio-Reconstructed/tree-tab6) — `studio-tab6`, HEAD `e81f54a9e7a241b4257390e8dc8eac6918dacac4` (2026-09-30). | Native client/release safety worktree; no web deployment config. | **UNKNOWN / not frontend origin.** |
| Native Studio worktree | [`tree-tab7`](../../../Music-Prod%20Studio-Reconstructed/tree-tab7) — `studio-tab7`, HEAD `1fa725ae8827ceecd2368f1daab151d670936e34` (2026-09-29). | Native Studio worktree; no web deployment config. | **UNKNOWN / not frontend origin.** |
| Native Studio worktree | [`tree-tab8`](../../../Music-Prod%20Studio-Reconstructed/tree-tab8) — `studio-tab8`, HEAD `5d6667ac1d93bdd3576bd2b2f8ea8a14aa6ac008` (2026-09-29). | Native Studio worktree; no web deployment config. | **UNKNOWN / not frontend origin.** |

These native worktrees have no configured remotes. Repeated nested docs/build trees are worktree copies, not independent deployment evidence.

The bounded file/history searches also found unrelated Hostinger instructions in the separate `charge-mate-rewards` app; those are excluded as candidates. Other workspace roots were not promoted to candidates without a Music-Prod deployment clue.

## 7. Exact source target

**Not identified.** The exact frontend repository, branch, commit, source directory, and deployment process that produced the live Admin and SPA chunks remain unknown. No exact local output hash exists among the inspected candidate builds. The live function's repository and deployed source revision also remain unknown.

Do not treat [`Music-ProdPROJECT-GitHub/src/pages/Admin.tsx`](../../../Music-ProdPROJECT-GitHub/src/pages/Admin.tsx) or [`Music-ProdPROJECT/supabase/functions/music-prod-studio-api/router.ts`](../../../Music-ProdPROJECT/supabase/functions/music-prod-studio-api/router.ts) as authoritative edit targets: available source/build behavior materially differs from production.

## 8. Recommended next step

Request a **sanitized read-only export of the October 1 site deployment record from the person/operator who published it**—the source checkout/commit, build command/output directory, and Hostinger deploy job or transfer record. That is the missing evidence needed to connect a local project to the live hashes; do not infer the method from response headers or the historical `.htaccess` alone.

## 9. Change confirmation

**No source, database, storage, artifact, release, deployment, or production configuration was changed during this investigation.** No shell history, source, configuration, Git ref, build output, or external system was modified. This requested report is the only file created.

## 10. A–G answers

**A) Was the deployment origin found?** No. Historical Hostinger/static-SPA clues were found, but no end-to-end producer/deploy record ties them to the current live assets.

**B) Exact source path/repository, if found:** None confirmed. `Music-ProdPROJECT-GitHub` is a **POSSIBLE** related frontend repository only. The closest local Edge Function snapshot is `Music-ProdPROJECT`, also **POSSIBLE** lineage only.

**C) Deployment mechanism:** Hostinger/HCDN delivery is **CONFIRMED**; how files reached Hostinger is **UNKNOWN**. Supabase serves the named Edge Function/project, but the function deploy command/source origin is **UNKNOWN**.

**D) Strongest frontend evidence:** The live Admin and SPA chunk hashes/sizes/mtime are known; Hostinger/HCDN response headers establish delivery. The website Git history includes a Hostinger SPA fallback and a domain-migration lineage, but no source/build/upload record. Both inspected local Admin outputs have nonmatching hashes/content and later filesystem mtimes.

**E) Strongest backend evidence:** The live bundle and public Edge response identify `wfpeajmdojcjqyrsnxbk` / `music-prod-studio-api`. `Music-ProdPROJECT` registers and implements that function locally, but its no-Git snapshot has an older lifecycle/response contract, and no deploy command/revision links it to production.

**F) Exact files to modify next:** None can be named safely until the deployed source revision is identified. Do not modify the candidate Admin or Deno router on current evidence.

**G) Change confirmation:** No production/source/backend state was changed; only this requested report was created. No GUI was used and no secrets were disclosed.
