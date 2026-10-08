# SideChainer Required Platform Audit

Audit scope: local source inspection of `/Users/martin/Documents/Music-Prod-Lovable-Source-Import-2026-10-02` only. No production database was queried.

## 1. Schema

- Table: `public.release_products`.
- Field: `required_artifacts`.
- Introduced in `supabase/migrations/20261002145039_98da9001-8359-49bf-ba12-cc8be508cf10.sql` as `jsonb NOT NULL DEFAULT '[]'::jsonb`.
- Expected value: an array of objects with `platform`, `architecture`, and optional `label`; for the claimed SideChainer rule this would be approximately `[ {"platform":"macos","architecture":"universal","label":"macOS"}, {"platform":"windows","architecture":"x86_64","label":"Windows"} ]`.
- Behavior: a newly added column defaults to an empty array. That means no product-specific platform slots unless data explicitly populates the column.
- The earlier table creation migration, `supabase/migrations/20260912145043_36cbe1ed-8428-465d-8e55-d4c9fb932673.sql`, predates this column.

## 2. Migrations

Relevant migrations inspected:

- `20260912145043_36cbe1ed-8428-465d-8e55-d4c9fb932673.sql` creates `release_products` and seeds Music-Prod Studio, VYRE, and ChordEngine. It does not seed SideChainer or required artifacts.
- `20261001210355_63645f05-4b32-43e8-956a-4aeff5e2746c.sql` inserts `('sidechain', 'SideChainer', 'plugin') ON CONFLICT (slug) DO NOTHING`; it does not provide `required_artifacts`.
- `20261002145039_98da9001-8359-49bf-ba12-cc8be508cf10.sql` adds the `required_artifacts` column/default and updates trigger logic; it does not insert or update the SideChainer requirements.

A search across all imported migration SQL found no migration that assigns `sidechain.required_artifacts` to macOS Universal and Windows x64. Other matches for platform/architecture terms were unrelated. Therefore the SideChainer row is not configured by the inspected migrations; the default for the new field is `[]`.

## 3. Seeds/configuration

- The export has no separate `supabase/seed.sql` or product seed/config file that supplies this requirement.
- Search of source, scripts, migrations, JSON/config data, and product definitions found no runtime constant assigning the two slots to SideChainer.
- `src/integrations/supabase/types.ts` describes the generated database field as `Json`; it does not supply row data.
- The only SQL insert for the SideChainer product sets slug/name/kind and leaves required artifacts at the column default.

## 4. Backend

- `supabase/functions/music-prod-studio-api/types.ts` models the product column as optional/nullable `RequiredArtifact[]`.
- `supabase/functions/music-prod-studio-api/releases.ts` contains a generic `productRequirements(product)` resolver. It reads `product.required_artifacts`, accepts valid nonempty platform/architecture entries, and normalizes those values to lowercase. It does not special-case `sidechain`.
- `computeReleaseReadiness` loads the product row by release `product_id`, uses the resolver, and checks for a matching valid artifact for each returned requirement.
- Missing/null/non-array configuration resolves to `[]`; malformed entries are filtered out. No default platforms or missing-configuration error is applied. With an empty list, `requirements` is empty and the per-platform `every(...)` check passes vacuously; the general release validation still applies, including its general artifact checks, but it does not require a macOS Universal artifact plus a Windows x64 artifact.
- `artifactStorage.ts` handles upload/storage metadata generically and does not define SideChainer's required-platform policy. `adminTypes.ts` only defines authenticated admin identity/result types and contains no product requirement configuration.

## 5. Frontend

- `src/components/admin/PluginReleasesSection.tsx` reads products for the admin product list, but the required slot list for a release comes from the backend `/admin/releases/:id/validation` response (`requirements`). The component does not special-case SideChainer.
- When returned requirements are nonempty, the UI renders each required label/platform/architecture, missing or invalid state, and only enables approval when backend readiness is true.
- If the backend returns an empty requirements array, the required-rows mapping renders nothing and the required-files banner is not shown. Its generic platform/format upload slots (including macOS Universal and Windows x64 examples) are not themselves marked required.

## 6. Tests

- `src/test/pluginReleaseExtension.test.ts` uses in-memory fake product rows. The base SideChainer row has only `id`, `slug`, `name`, `kind`, `active`, and timestamps.
- In the approval workflow and failed-validation audit suites, the test locally defines `SIDE_REQ` as macOS `universal` and Windows `x86_64`, then manually sets `w.products.find(...slug === 'sidechain')!.required_artifacts = SIDE_REQ`.
- Those tests verify behavior of the generic resolver when requirements are injected into the fake; they do not read actual database metadata or prove the source migration configures the real row.
- `src/test/pluginEntitlement.test.ts` also uses an in-memory SideChainer row without `required_artifacts`; it tests licensing/entitlements, not required platform setup.

## 7. Documentation

- `docs/plugin-release-approval-workflow.md` states the intended configuration as `sidechain.required_artifacts = [{macos, universal, "macOS"}, {windows, x86_64, "Windows"}]` and says other products use an empty list.
- The documentation calls this product configuration “data, not schema,” and says the approval migration was applied to the live database. However, the matching imported migration only adds the column/default and trigger logic; it does not populate the SideChainer row. The test's matching requirement is an in-memory fixture. Documentation is not proof that the imported runtime seed/migration defines it, and this audit did not query production.

## 8. Final classification

**C) DOCUMENTATION/TESTS ONLY** for the SideChainer → macOS Universal + Windows x64 assignment in the imported source export.

The export contains a generic runtime mechanism for enforcing product requirements, but no source migration/seed/static runtime config that assigns these requirements to `sidechain`. The field default is an empty array. Whether the live database has been manually populated outside the export cannot be established without a database query, which was explicitly excluded.

## 9. Exact next step

Add a reviewed, version-controlled migration that updates `release_products.required_artifacts` for `slug = 'sidechain'` to require macOS `universal` and Windows `x86_64`; do not apply it until separately authorized and reviewed against the target database.

## 10. Change confirmation

No source, database, migration, deployment, artifact, release, or production state was changed during this audit.
