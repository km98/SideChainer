# Live Music-Prod Admin Frontend Trace

**Trace date:** 2026-10-02  
**Scope:** Public unauthenticated HTTP GETs and static inspection of publicly served bundles only. No authenticated admin API call, state-changing request, GUI/browser UI, or repository/production mutation was performed.

## 1. Live page

- **URL:** `https://music-prod.com/admin`
- **HTTP status:** `200`; the root `/` also returns `200`.
- **Response behavior:** `/admin` serves the same SPA HTML shell as `/`, with identical `last-modified` and `ETag` values. It is not server-rendered admin markup; the route and admin UI are rendered by JavaScript.
- **Observed hosting/CDN headers:** `platform: hostinger`, `panel: hpanel`, `server: hcdn`, and `x-hcdn-*` request/cache/upstream headers. This is direct evidence of Hostinger/HCDN delivery, not a repository or deployment-project identifier.
- **Relevant `/admin` headers (2026-10-02 12:36 UTC):** `content-type: text/html`; `last-modified: Thu, 01 Oct 2026 21:06:20 GMT`; `etag: W/"1067-6abecb4c-54ae7bffde468bad;gz"`; `cache-control: no-cache, no-store, must-revalidate`; `x-hcdn-cache-status: DYNAMIC`.
- **Root and `/admin` match:** both have the same shell ETag/last-modified timestamp and the same JS/CSS references below. This supports that admin is a route within the delivered SPA, not a separate HTML application at `/admin`.

## 2. HTML shell

The live root and `/admin` HTML reference:

- Entry module: [`/assets/index-9wpXYaKk.js`](https://music-prod.com/assets/index-9wpXYaKk.js)
- Module preloads: `/assets/vendor-react-tIoPkRwd.js`, `/assets/vendor-query-REUEbryQ.js`, `/assets/vendor-ui-BVtOlGvj.js`, `/assets/vendor-supabase-BcbvpzCe.js`
- Stylesheet: [`/assets/index-LxA2VeD9.css`](https://music-prod.com/assets/index-LxA2VeD9.css)
- Manifest: `/manifest.json` (Music-Prod PWA manifest; no build ID)

The entry JS contains a Vite-style dynamic-import map and lazy route import for `./Admin-CVjFmNKZ.js`. That exact public lazy route chunk is the Plugin Releases owner:

- **Plugin Releases chunk:** [`/assets/Admin-CVjFmNKZ.js`](https://music-prod.com/assets/Admin-CVjFmNKZ.js)
- Chunk response: HTTP 200, `content-type: application/x-javascript`, `last-modified: Thu, 01 Oct 2026 21:06:20 GMT`, `etag: W/"e8873-6abecb4c-10ec5229be00d659;gz"`, immutable one-year public cache, and Hostinger/HCDN headers.
- **Source maps:** no `sourceMappingURL` comment was present in the entry or Admin chunk. Requests for likely `.js.map` paths returned the SPA HTML shell, not source maps. No Vite manifest, asset manifest, build JSON, or version JSON was exposed (unknown asset routes also fall back to the HTML shell).
- **Build/deployment ID:** no commit SHA or deployment ID was found in the shell, manifest, chunk or response headers. The shared `last-modified` value is an asset timestamp, not proof of a deployment time or source commit.

## 3. Plugin Releases bundle

The string `Plugin Releases` is in `Admin-CVjFmNKZ.js`, inside the lazy admin module. The same bundle contains `Create release`, `No releases yet.`, tabs `Overview`, `Releases`, `Products`, `Licenses`, `Audit log`, upload/validation/publish labels, and the API strings described below.

The entry chunk dynamically imports `Admin-CVjFmNKZ.js` for its `/admin` React route. The available local website checkout has a similar general SPA route, but this exact asset hash/name and Plugin Releases implementation were not found in local source or its local dist bundles.

## 4. Plugin Releases logic

### Product selector, release list, and empty state

The live Admin chunk defines the release page component (minified symbol `Gb`). On load it runs, in parallel, Supabase client selects equivalent to:

- `release_products.select("*").order("name")`
- `product_releases.select("*").order("created_at", { ascending: false })`

It stores both result sets in UI state. Product names are resolved by matching a release's `product_id` to a product row. The Releases tab filters releases by product ID; `all` means no filter. The Create release dialog filters products to `active` before populating its selector.

Therefore the code-level explanation is: **SideChainer can appear because the product catalog query returns an active SideChainer `release_products` row; “No releases yet.” is rendered when the locally loaded `product_releases` rows, after the current product filter, have length zero.** The page copy and query implementation are present in the live bundle; this trace did not run an authenticated query to inspect its private/admin result set.

### Create release

The Create release dialog collects:

- `productId`
- `version`
- optional `buildNumber` (converted to number or `null`)
- `channel` (`stable`, `beta`, or `development`)
- optional `releaseNotes`

Clicking the action would call `POST /admin/releases` with JSON shaped like:

```json
{
  "productId": "<selected product UUID>",
  "version": "<entered version>",
  "buildNumber": 47,
  "channel": "stable",
  "releaseNotes": "<notes or null>"
}
```

That is **static code evidence only**; Create release was not clicked or called. The client expects the API helper to unwrap `{ success, data }` and then reads `data.release.id`. It displays a draft-created toast and refreshes the listing.

### Release detail and artifacts

The same chunk statically implements:

- GET release detail from `/admin/releases/:id` and, for statuses `draft`, `uploaded`, or `validated`, GET `/admin/releases/:id/validation`.
- Artifact slots declared in the UI: macOS ARM64 AU/VST3, macOS Intel (`x86_64`) AU/VST3, macOS Universal AU/VST3, and Windows x64 VST3.
- Upload sends `POST /admin/releases/:id/artifacts` as `FormData` containing `file`, `fileName`, `platform`, `architecture`, and `format`.
- Remove sends `DELETE /admin/releases/:id/artifacts/:artifactId`.
- The details view displays artifact filename, formatted size, and SHA-256. It does not itself establish that a code signature/notarization was checked.
- Status actions in this live UI are `validate`, `publish`, `yank`, and `archive`; publication is explicitly separate from validation in the UI. The displayed lifecycle vocabulary is `draft`, `uploaded`, `validated`, `published`, `yanked`, `archived`—not the older local migration's `draft`, `ready`, `published`, `withdrawn` enum.
- The live component contains **no `APPROVE RELEASE` or `READY FOR APPROVAL` UI string**. It presents a `Publish` action for `validated` releases. This live bundle therefore does not implement the requested distinct approval step.

The audit tab reads `release_audit_log` with a direct Supabase select, latest 300 rows. This proves the bundle expects that table and displays an audit log; it does not prove live table availability, write mechanism, or action coverage.

## 5. API

### Admin API helper and authentication

The Plugin Releases chunk defines an API helper that calls:

`https://wfpeajmdojcjqyrsnxbk.supabase.co/functions/v1/music-prod-studio-api/v1${path}`

It obtains the current Supabase session, sends its access token as `Authorization: Bearer <access_token>`, and includes a public Supabase anon `apikey` header. JSON requests set `Content-Type: application/json`; multipart uploads omit it so the browser can set the boundary. The helper parses JSON, throws on non-2xx or `success === false`, and returns the `data` member.

The project ref embedded in the public anon-key JWT `ref` claim is **`wfpeajmdojcjqyrsnxbk`**. The full key is intentionally not repeated in this report.

### Requests visible in the bundle

| Purpose | Method/path | Request/response evidence |
|---|---|---|
| List products and releases | Direct Supabase client reads of `release_products` and `product_releases` | `select('*')`; products ordered by name, releases by descending `created_at` |
| Create release | `POST /admin/releases` | JSON `productId`, `version`, `buildNumber`, `channel`, `releaseNotes`; client expects `data.release.id` |
| Read release details | `GET /admin/releases/:id` | Client expects `data.release`, including `productId`, `version`, `status`, `channel`, `artifacts` |
| Get validation issues | `GET /admin/releases/:id/validation` | Client expects `data.issues`; UI disables Validate when issues are non-empty |
| Upload artifact | `POST /admin/releases/:id/artifacts` | multipart file plus filename/platform/architecture/format; client expects success data, then refreshes detail |
| Delete artifact | `DELETE /admin/releases/:id/artifacts/:artifactId` | Client refreshes detail after success |
| Lifecycle actions | `POST /admin/releases/:id/{validate,publish,yank,archive}` | no body specified by the client; success response followed by detail refresh |
| Audit log | Direct Supabase `release_audit_log.select('*')` | order descending by `created_at`, limit 300 |

No state-changing API call was made during this trace. Only the public `/updates` GET and unauthenticated `/admin/releases` GET described below were safely probed.

## 6. Backend

- **Backend named by the actual Plugin Releases chunk:** Supabase Edge Function `music-prod-studio-api` at project `wfpeajmdojcjqyrsnxbk`.
- **Evidence:** the chunk literally constructs the URL under `...supabase.co/functions/v1/music-prod-studio-api/v1...` and calls that helper for create/detail/validation/artifact/lifecycle routes. Its API response contract is an envelope with `success` and `data`.
- **Safe live response check:** GET `/functions/v1/music-prod-studio-api/updates?...` returned HTTP 200 with no published SideChainer release; unauthenticated GET `/functions/v1/music-prod-studio-api/admin/releases` returned HTTP 401. The live response headers include `x-served-by: supabase-edge-runtime` and `x-sb-edge-region: eu-central-1`, confirming the URL is served by Supabase Edge Runtime. These checks did not authenticate and did not mutate state.
- **Node backend comparison:** the available `Music-ProdStudio-Backend-tab2` serves paths such as `/api/admin/releases`; the live browser bundle instead uses the Supabase function URL above. There is no evidence that the Node service handles this Plugin Releases UI.
- **Local Deno source drift:** available [Music-ProdPROJECT function source](../../../Music-ProdPROJECT/supabase/functions/music-prod-studio-api/router.ts) is not an exact match for the live contract. The inspected local router exposes `ready`, `commit`, and `withdraw` transitions; it does not define the live `/validation`, `/publish`, `/yank`, `/archive` routes or the live `uploaded`/`validated`/`yanked`/`archived` status vocabulary. The older local release schema also lacks the live `build_number`/`channel` shape. Thus the **deployed service name/project are identified, but its deployed source revision is not**; do not treat the stale local Deno implementation as authoritative code for this UI.
- **Storage limitation:** the UI calls the backend upload endpoint; it does not name a bucket. Local older Deno code intends private `releases` storage and signed URLs, while prior read-only checking found that bucket absent in the configured project. The live bundle and unauthenticated probes do not prove the actual deployed upload bucket or its privacy settings. Exact live storage remains **unverified**.

## 7. Repository/deployment identification

- **Frontend framework/bundle evidence:** Vite-style hashed `/assets/*.js` chunks and a lazy Admin chunk. The `music-prod.com` root and `/admin` use the same SPA shell. This is strong evidence the Plugin Releases UI is part of the site's React/Vite single-page app, not a separate HTML application at `/admin`.
- **Hosting:** live headers identify Hostinger/HCDN. They do not disclose an account/project, linked Git repository, deployment workflow, or commit.
- **Local candidate:** `Music-ProdPROJECT-GitHub` is the closest website checkout: same Music-Prod SPA/admin route and configured Supabase project, but branch `main` HEAD `9a1097f6` does not contain Plugin Releases, and its local build asset names differ. No match for the live hash/chunk names was found in local dist/source. Confidence it is the exact deployed source: **low / not established**.
- **Backend repo:** `Music-ProdStudio-Backend-tab2` is not the service referenced in the UI bundle. `Music-ProdPROJECT` contains an older local Edge Function implementation but not the live route/status contract. The exact repository/branch/commit that produced the live Admin bundle and the currently deployed Edge Function remains **unknown**.
- **Time/build evidence:** root and admin shell, entry bundle, and Admin chunk report `last-modified: Thu, 01 Oct 2026 21:06:20 GMT`; not proof of deployment time. No build ID, source map, commit SHA, or deployment ID is exposed. `/admin` and `/` use matching shell ETags, corroborating the shared SPA.

## 8. Separate admin application

There is **no evidence of a separate deployed admin application**. `/admin` returns the same HTML shell/assets as `/`; the shared entry bundle lazy-loads `Admin-CVjFmNKZ.js`, and that chunk owns Plugin Releases. The admin screen is a lazy route/feature of the public site's SPA. A separate source repository or build pipeline for that chunk cannot be ruled out, but none is evidenced by public responses.

## 9. Exact next implementation target

The live UI owner is now identified at the bundle level as `Admin-CVjFmNKZ.js`, but its source file/repository is **not present in the available local checkouts**. The live API is named as the Supabase function `music-prod-studio-api` in project `wfpeajmdojcjqyrsnxbk`, but its deployed source revision is not matched to the old local function code.

Therefore the next implementation phase still has **no safe source-file target**. Do not change the local `Music-ProdPROJECT-GitHub/src/pages/Admin.tsx`, either backend implementation, or the stale local Deno routes based on guesswork. Obtain the exact source checkout/revision that produced this entry/Admin chunk and the deployed function revision before implementing. When source is obtained, it must preserve the live existing contracts and explicitly add the missing approval step; current live UI presents `validated -> publish` and has no approval action.

## 10. Change confirmation

**No source, database, storage, artifact, migration, release, or deployment state was changed during this investigation.** The only live application requests were public GETs for HTML/assets/update metadata and an unauthenticated GET that returned 401. No source bundle was written to the repository or an inspection file. No GUI application was opened or used.
