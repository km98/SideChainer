# Hostinger Pre-Deployment Check

**Check date:** 2026-10-02  
**Mode:** Read-only; no production connection, HTTP download, upload, or Hostinger mutation was performed.

> **Important evidence limit:** `public_html` is not mounted or present as a local directory in the workspace. No Hostinger shell/SFTP access was available. Therefore I could not inventory the actual current remote directory or independently inspect its live `deploy-temp`, `assets.2660`, `benchmark-files.6232`, source-export directory, or backup ZIP. The additional names below come from the user's description of the Hostinger screenshot. The complete local comparison is against the separately downloaded ZIP snapshot at `/Users/martin/Downloads/music-prod-hostinger-2026-10-02.zip`; it must not be represented as a fresh live-server listing.

## 1. Current public_html

### User-reported Hostinger screenshot (root-level only)

The screenshot description reports this root-level inventory. Sizes and server modification times were **not available** from the description and were not obtained from Hostinger.

| Name | Type | Size | Server mtime | Assessment |
|---|---|---:|---|---|
| `assets/` | Directory | Not read | Not read | Website asset directory; local snapshot has 369 files. |
| `assets.2660/` | Directory | Not read | Not read | Unknown suffix/role; preserve until inventoried. |
| `benchmark-files/` | Directory | Not read | Not read | Benchmark download/test files; also present in the new dist. |
| `benchmark-files.6232/` | Directory | Not read | Not read | Unknown suffix/role; preserve until inventoried. |
| `deploy-temp/` | Directory | Not read | Not read | Temporary-looking name, contents/owner unknown; preserve until inspected. |
| `Music-Prod-Lovable-Source-Import-2026-10-02/` | Directory | Not read | Not read | Source-export-named directory under the reported document root. |
| `.htaccess` | File | Not read | Not read | Apache rewrite/cache/domain behavior; preserve. |
| `ads.txt` | File | Not read | Not read | Ads/domain declaration; preserve. |
| `apple-touch-icon.png` | File | Not read | Not read | Site icon; preserve. |
| `favicon.png` | File | Not read | Not read | Site icon; preserve. |
| `icon-192.png` | File | Not read | Not read | PWA icon; preserve. |
| `icon-512.png` | File | Not read | Not read | PWA icon; preserve. |
| `index.html` | File | Not read | Not read | Current SPA entry point in the reported listing. |
| `llms-full.txt` | File | Not read | Not read | Site discovery/content file; preserve. |
| `llms.txt` | File | Not read | Not read | Site discovery/content file; preserve. |
| `manifest.json` | File | Not read | Not read | PWA manifest; preserve. |
| `music-prod-hostinger-2026-10-02.zip` | File | Not read | Not read | Backup-named archive under the reported document root. |
| `music-prod-logo.png` | File | Not read | Not read | Site image; preserve. |
| `placeholder.svg` | File | Not read | Not read | Site asset; preserve pending reference check. |
| `robots.txt` | File | Not read | Not read | Search crawling rules; preserve. |
| `sitemap.xml` | File | Not read | Not read | Site sitemap route/config; preserve. |

### Available local snapshot

The local ZIP is `/Users/martin/Downloads/music-prod-hostinger-2026-10-02.zip` (33,570,921 bytes; mtime `2026-10-02T17:39:12.101989+02:00`; SHA-256 `e5fc04e4e45e845bd9a8053f7f88ad2956b74afe5c17c4135280827070e9fad4`). It contains 390 ZIP records: 387 files and three directory records under `public_html/` (root, `assets/`, and `benchmark-files/`). Its contents are dated around 17:29 local time. It does **not** contain the screenshot-reported `assets.2660/`, `benchmark-files.6232/`, `deploy-temp/`, source-export directory, or the named ZIP file itself. This discrepancy prevents treating it as the screenshot's exact/current complete inventory.

The snapshot's root-level entries and metadata are:

| Name | Type | Bytes | ZIP-record mtime (local) |
|---|---|---:|---|
| `.htaccess` | File | 1,312 | 2026-10-02 15:29:20 |
| `ads.txt` | File | 59 | 2026-10-02 15:29:20 |
| `apple-touch-icon.png` | File | 6,794 | 2026-10-02 15:29:20 |
| `benchmark-files/` | Directory (4 files) | 11,022,544 total | 2026-10-02 15:29:20 |
| `favicon.png` | File | 2,192 | 2026-10-02 15:29:20 |
| `icon-192.png` | File | 7,246 | 2026-10-02 15:29:20 |
| `icon-512.png` | File | 32,669 | 2026-10-02 15:29:20 |
| `llms-full.txt` | File | 6,919 | 2026-10-02 15:29:20 |
| `llms.txt` | File | 3,171 | 2026-10-02 15:29:20 |
| `manifest.json` | File | 372 | 2026-10-02 15:29:20 |
| `music-prod-logo.png` | File | 10,188 | 2026-10-02 15:29:20 |
| `placeholder.svg` | File | 3,253 | 2026-10-02 15:29:20 |
| `robots.txt` | File | 704 | 2026-10-02 15:29:20 |
| `sitemap.xml` | File | 3,535 | 2026-10-02 15:29:20 |
| `assets/` | Directory (369 files) | 26,674,257 total | 2026-10-02 15:29:40 |
| `index.html` | File | 4,199 | 2026-10-02 15:29:40 |

All 369 `assets/` records in this snapshot have ZIP mtime `2026-10-02 15:29:40`. The current Admin chunk is `assets/Admin-zGWO0fFg.js` (962,874 bytes; SHA-256 `16a89c25b6b5975d0ddddf7adcbc3b2db153cecaea4b14c5c099b95a55c95d0f`); its current main entry is `assets/index-CEmG8n1G.js` (419,225 bytes; SHA-256 `00b56f0d1cd72a9767fde3e69dd7b52884e21ddd48c1630ace41e30a73574f55`). It is an older build than the verified new dist, but the snapshot Admin chunk already contains Plugin Releases/approval markers.

The four benchmark directory entries in the snapshot are `README.md` (601 bytes), `ableton-live-benchmark.zip` (78,237 bytes), `fl-studio-benchmark.zip` (2,597,407 bytes), and `logic-pro-benchmark.zip` (8,346,299 bytes); each has ZIP mtime `2026-10-02 15:29:20`.

## 2. Publicly exposed temporary material

- **Source export directory:** According to the screenshot description it is inside `public_html`, the confirmed document root. Treat files beneath that path as **potentially publicly retrievable** by known URL unless Hostinger access controls/deny rules are verified. The snapshot `.htaccess` inspected here contains caching, sitemap redirect, and SPA fallback rules, but no authentication or deny rule. I did not request the directory or any file from the live site. The current remote directory contents, including whether it contains `.env` or other sensitive files, remain unverified. **Recommended later action:** preserve a private backup, then move the export outside the document root or apply a verified deny rule; do not merely assume directory listing is disabled protects known file URLs.
- **Backup ZIP:** According to the screenshot description it is also beneath `public_html`; the direct path would be `/music-prod-hostinger-2026-10-02.zip`. Treat it as potentially downloadable by anyone who knows/guesses that URL. It is not protected by a deny rule in the inspected snapshot `.htaccess`. Preserve a private copy outside the document root before later moving/removing the web-root copy.
- **Other reported names:** `deploy-temp/`, `assets.2660/`, and `benchmark-files.6232/` are not present in the local snapshot, so their contents and purpose cannot be determined. Do not delete or overwrite them based only on their names.
- The separately inspected ZIP itself is in `/Users/martin/Downloads/`, not a locally mounted `public_html`. Its local existence does not prove whether the screenshot-reported server copy is accessible.

## 3. Current website entry

The snapshot's `public_html/index.html` is 4,199 bytes, SHA-256 `2e6141291141881b6ade2c659806d12e2dca78e9dacfe4fb5ce685611000436f`. It loads:

- `/assets/index-CEmG8n1G.js`
- `/assets/vendor-react-tIoPkRwd.js`
- `/assets/vendor-query-REUEbryQ.js`
- `/assets/vendor-ui-DCCPEWBm.js`
- `/assets/vendor-supabase-iuIh2pDr.js`
- `/assets/index-LxA2VeD9.css`
- `/favicon.png`, `/apple-touch-icon.png`, and `/manifest.json`

The new `dist/index.html` is also 4,199 bytes but differs in hash and Vite output references. HTML element/tag structure, title, SEO/social metadata, analytics tag, and all non-asset root files match the snapshot; the script and two vendor preload filenames differ. The new index references `/assets/index-hH4NbVkK.js`, `/assets/vendor-react-tIoPkRwd.js`, `/assets/vendor-query-REUEbryQ.js`, `/assets/vendor-ui-BVtOlGvj.js`, `/assets/vendor-supabase-BcbvpzCe.js`, and `/assets/index-LxA2VeD9.css`. It does not name the lazy Admin chunk directly; the new main entry references `Admin-DeBqz6BP.js`.

## 4. New frontend build

- Build directory: `/Users/martin/Documents/Music-Prod-Lovable-Source-Import-2026-10-02/dist/`.
- New index: `dist/index.html`, 4,199 bytes; it references the new main entry and new vendor chunks above.
- Admin: `dist/assets/Admin-DeBqz6BP.js`, 955,908 bytes; SHA-256 `655609317ae03f8cfa33b72c28cbd57323050a09d729acdaf1473d3c1292d891`.
- Main entry: `dist/assets/index-hH4NbVkK.js`, 422,437 bytes; SHA-256 `08b2a6c307749816409d4407dcab9c919147fb01925d582c8e69cc35f7dc5d74`.
- The new main bundle references the new Admin chunk; the old captured main bundle references the old Admin chunk.

## 5. Differences

Comparison below is between the local ZIP snapshot (not a live-server read) and the new dist:

- Snapshot: 387 files; new dist: 386 files.
- Both contain the same two directories (`assets/`, `benchmark-files/`) and exactly the same set of 14 top-level file names. All shared top-level files other than `index.html` are byte-identical, including `.htaccess` and the site/SEO files.
- `assets/`: snapshot has 369 files / 26,674,257 bytes; new dist has 368 files / 26,698,663 bytes. There are 299 new-only asset paths and 300 snapshot-only asset paths; 73 asset paths have the same name and byte content. Most differences are expected content-hashed bundle filenames. The snapshot also has `assets/ChordEngineFeedback-RNVZwLGI.js`, absent in new dist; preserve old assets pending reference/retention review rather than deleting wholesale.
- `index.html` is the only shared file with different bytes. Its structural tags and SEO/analytics content match, while the main entry and vendor UI/Supabase chunk references change to the new build.
- The new dist and snapshot both include `.htaccess`, `ads.txt`, `apple-touch-icon.png`, `favicon.png`, `icon-192.png`, `icon-512.png`, `llms-full.txt`, `llms.txt`, `manifest.json`, `music-prod-logo.png`, `placeholder.svg`, `robots.txt`, `sitemap.xml`, and all four `benchmark-files/` entries. Each checked file is byte-identical between the two.
- The screenshot-reported extra directories and ZIP are not part of dist and do not occur in the snapshot archive. Their contents cannot be assessed here.

## 6. Files requiring preservation

Preserve these root/domain/SEO files and directory unless a separate approved review explicitly changes them: `.htaccess`, `robots.txt`, `sitemap.xml`, `ads.txt`, `manifest.json`, `llms.txt`, `llms-full.txt`, `favicon.png`, `apple-touch-icon.png`, `icon-192.png`, `icon-512.png`, `music-prod-logo.png`, `placeholder.svg`, and `benchmark-files/`. They are present in the new dist and byte-match the local snapshot, but still should not be removed as a side effect of a blanket cleanup.

Also preserve the screenshot-reported `assets.2660/`, `benchmark-files.6232/`, and `deploy-temp/` until their contents, references, and ownership have been read-only inventoried. Keep old hashed files in `assets/` initially: existing browser caches/old HTML may still use them, and the unmatched old bundle `ChordEngineFeedback-RNVZwLGI.js` needs a reference/retention check. The source export and backup ZIP should not remain publicly reachable; preserve an off-root/private copy before a later authorized move.

`og-image.png` is referenced by the existing Open Graph/Twitter metadata but is absent from both the snapshot and new dist. This is an existing content/reference issue, not a reason to remove the metadata or to change files during this check; verify the production URL separately before any deployment.

## 7. Safe deployment procedure (future only; not executed)

1. Obtain an authorized read-only listing of the actual Hostinger `public_html` and each reported extra directory, recording recursive names, sizes, mtimes, and hashes. The local ZIP snapshot is not a substitute for this current remote inventory.
2. Preserve a complete rollback copy **outside** the document root and verify its inventory. Privately retain the source export and the backup ZIP outside the web root; do not expose them as part of the website upload.
3. Stage the verified `dist/` contents separately. Compare hashes and confirm all referenced hashed files are staged. Preserve `.htaccess`, SEO/domain files, `benchmark-files/`, and any separately validated required content. Do not deploy `node_modules`, source files, `.env`, backup archives, or temporary folders.
4. Upload/add the new hashed `assets/` files first; do not delete the old hashes in the same step. Keep the existing `.htaccess` (its snapshot bytes match the new dist and it supplies sitemap routing plus SPA fallback) unless separately reviewed.
5. Update `index.html` only after every file it references is confirmed present and hash-verified. This makes the new entry point the final switch and avoids a window where it references missing chunks. Then perform an authorized read-only HTTP smoke check and preserve the old assets through a rollback/cache-retention window.
6. Only after checking live references, backup integrity, CDN/cache behavior, and the unknown extra directories should a separate approved cleanup plan address stale hashed assets or temporary material. No blanket replacement of the entire root is recommended from current evidence.

## 8. Items NOT to delete

Do not delete or overwrite during a later deployment without an explicit inventory/reference review: `.htaccess`; the SEO/domain files and icons listed in §6; `benchmark-files/`; existing hashed files under `assets/` (including `ChordEngineFeedback-RNVZwLGI.js`) during initial rollout; or screenshot-reported `assets.2660/`, `benchmark-files.6232/`, and `deploy-temp/`. Do not discard the backup ZIP until a separate verified private rollback copy exists. The source-export directory and any web-root ZIP copy are potential exposure risks and should later be moved outside the document root or access-restricted after preserving private copies, not blindly deleted as part of deployment.

## 9. Change confirmation

No files were moved, deleted, overwritten, uploaded, or deployed during this check.

## A–E answers

**A) Current inventory:** The screenshot-described list is captured in §1, but current remote sizes/mtimes and recursive entries could not be read. A local archive snapshot inventory and its exact metadata are recorded in §1; it differs from the screenshot list, so it cannot certify live state.

**B) Source/ZIP exposure:** If the screenshot locations are accurate, both are under the confirmed document root and should be treated as potentially public. The source folder and ZIP are not present in the inspected local snapshot. No live URL was requested.

**C) Preserve:** `.htaccess`, all SEO/domain/manifest/icon files, `benchmark-files/`, old hashed assets initially, and the unknown screenshot-reported directories until reviewed. Preserve a private/off-root copy of the source export and backup ZIP before restricting/moving them.

**D) Safest procedure:** First obtain a current authorized read-only Hostinger inventory and private rollback backup. Then stage/verify dist, add new hashed assets without pruning old ones, preserve the root/domain/SEO/benchmark files and reviewed extras, keep `.htaccess`, and switch `index.html` last. Do not replace the entire root or delete unknown folders based on this snapshot.

**E) Change confirmation:** No files were moved, deleted, overwritten, uploaded, or deployed during this check.
