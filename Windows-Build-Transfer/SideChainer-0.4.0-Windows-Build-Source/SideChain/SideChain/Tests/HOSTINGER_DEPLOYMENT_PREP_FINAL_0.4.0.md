# Hostinger Deployment Preparation

**Preparation date:** 2026-10-02  
**Scope:** Verify existing local frontend package, create an isolated upload tree, check for the exact local rollback copy, and write manual instructions. No Hostinger access or deployment was performed.

## 1. Frontend source

- Authoritative source root: `/Users/martin/Documents/Music-Prod-Lovable-Source-Import-2026-10-02`.
- Verified production build: `/Users/martin/Documents/Music-Prod-Lovable-Source-Import-2026-10-02/dist/`.
- The build was already verified (`npm ci`, `npm run build`). It was not run again during this preparation.
- Source branch/commit metadata is not available for the export.

## 2. Deployment package

- Existing package: `/Users/martin/Documents/Music-Prod-Lovable-Source-Import-2026-10-02/music-prod-frontend-hostinger-2026-10-02.zip`.
- Size: **22,461,752 bytes**.
- SHA-256: `1a1f00ba224bc94cce7bbf68a4dfc05646f496ce6bcca77c7dcafbaced57574f` (matches the known hash).
- Archive inspection passed: 370 entries including the `assets/` directory entry; `index.html` and `assets/` are at the ZIP root; no `dist/` wrapper; ZIP CRC/integrity test passed.
- No source, `.env`, `node_modules`, Git metadata, tests, docs, or development files were found in the archive.
- This package was not modified in this task.
- It contains only `index.html` and `assets/`. `benchmark-files/`, `.htaccess`, robots/sitemap/ads files, icons, and other Hostinger-managed root files are excluded.

## 3. Deployment directory

Created new isolated directory:

`/Users/martin/Documents/Music-Prod-Lovable-Source-Import-2026-10-02/HOSTINGER_DEPLOY_2026-10-02/`

Upload tree:

`/Users/martin/Documents/Music-Prod-Lovable-Source-Import-2026-10-02/HOSTINGER_DEPLOY_2026-10-02/UPLOAD_TO_PUBLIC_HTML/`

It contains exactly:

- `index.html` — 4,199 bytes
- `assets/` — 368 files

The upload tree was copied from the verified `dist/`. No `benchmark-files`, `.htaccess`, robots.txt, sitemap.xml, ads.txt, icons, source, `.env`, `node_modules`, tests, docs, or Git metadata are included. The new index references its generated hashed entry/vendor/CSS files. The main bundle references the Admin lazy chunk.

## 4. Rollback package

**Not created.** The exact requested local source path `/Users/martin/Documents/Music-Prod-Lovable-Source-Import-2026-10-02/deploy-temp/public_html/` does not exist, and a bounded local search of Documents/Downloads found no directory matching `*/public_html/deploy-temp/public_html`. No Hostinger connection was available to retrieve or inspect it. Accordingly, no rollback ZIP path, size, or hash exists, and no substitute was invented.

The user-reported `public_html/deploy-temp/public_html/` remains remote/unverified for this task. Do not modify or delete `deploy-temp`; verify the actual backup contents in Hostinger before relying on it for rollback.

## 5. Manual deployment procedure

The concise, user-facing procedure is in [HOSTINGER_DEPLOY_2026-10-02/README.md](Music-Prod-Lovable-Source-Import-2026-10-02/HOSTINGER_DEPLOY_2026-10-02/README.md). In summary:

1. Keep the current Hostinger `public_html` unchanged until ready.
2. Upload the verified deployment ZIP to `public_html/` and extract its contents directly there, merging them into the existing root.
3. Allow overwrite of `index.html` and matching files. Do not delete `assets/` first or prune old hashed assets.
4. Do not replace `.htaccess`, robots.txt, sitemap.xml, ads.txt, icons, or unrelated files.
5. Verify the new `index.html` and Admin bundle, then test `https://music-prod.com/admin`.
6. If broken, restore `index.html` and any required files from a verified previous-site backup. Since no local rollback package could be prepared, first confirm that the Hostinger `deploy-temp/public_html/` copy is complete and usable.
7. Remove the uploaded deployment ZIP from the public web root after extraction using a separately authorized Hostinger action; retain the private local package.

This describes a future manual action only; nothing was uploaded or deployed here.

## 6. Post-deployment verification

If deployment is separately authorized, verify through Hostinger and a read-only site check:

- `public_html/index.html` exists and its entry/preload URLs reference the new bundle set.
- `public_html/assets/Admin-DeBqz6BP.js` exists; expected SHA-256: `655609317ae03f8cfa33b72c28cbd57323050a09d729acdaf1473d3c1292d891`.
- `public_html/assets/index-hH4NbVkK.js` exists; expected SHA-256: `08b2a6c307749816409d4407dcab9c919147fb01925d582c8e69cc35f7dc5d74`.
- Other referenced hashed vendor/CSS assets exist, and `https://music-prod.com/admin` loads the Plugin Releases approval UI.
- Existing `.htaccess`, robots.txt, sitemap.xml, ads.txt, icons, and unrelated root files remain in place.
- The uploaded deployment ZIP is no longer publicly reachable from `public_html/` after extraction. No deployment or live HTTP check was made during this preparation.

## 7. Rollback procedure

1. Stop further file changes; do not delete old hashed assets.
2. Confirm the contents of the previous website copy at `public_html/deploy-temp/public_html/` in Hostinger and confirm it includes the prior `index.html`, referenced assets, and `.htaccess` if the latter needs restoration.
3. Restore the previous `index.html` first/alongside its required prior assets so its references resolve. Restore other root files only if changed and from verified backup copies.
4. Keep both old and new hashed files until the prior page is confirmed working. Do not delete or repurpose `deploy-temp/` as part of rollback.
5. Recheck the site entry and the required user paths after restoration.

This is guidance only; the backup contents and rollback capability were not remotely verified.

## 8. Important preserved files

The new ZIP/upload tree intentionally excludes these files because they are separately managed at the Hostinger root: `.htaccess`, `robots.txt`, `sitemap.xml`, `ads.txt`, `manifest.json`, `llms.txt`, `llms-full.txt`, `favicon.png`, `apple-touch-icon.png`, `icon-192.png`, `icon-512.png`, `music-prod-logo.png`, `placeholder.svg`, `benchmark-files/`, and unrelated existing files. Do not replace or delete them as part of the frontend extraction. Preserve old hashed assets and the reported `deploy-temp/` backup material.

No direct Hostinger access was available; this report does not claim any current remote directory contents or successful deployment.

## 9. Change confirmation

No Hostinger upload or deployment was performed. No live website, database, storage, release, or production state was changed. Existing deploy-temp backup material was left untouched.
