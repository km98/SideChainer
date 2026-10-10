# PumpCurve release naming contract

The product is **PumpCurve**; the Xcode target (and therefore every built bundle
directory) is still called **SideChain**. That mismatch is invisible inside the
Audio Unit — its registered name is `Music-Prod: PumpCurve` — but it is visible
wherever a host derives the displayed name from the **bundle filename**.
FL Studio's VST3 database does exactly that: with the bundle installed as
`SideChain.vst3`, FL's own database file contained

```
ps_name=SideChain
ps_file_filename_0=/Users/<user>/Library/Audio/Plug-Ins/VST3/SideChain.vst3
```

even though the VST3 class inside the binary is already `PumpCurve`.

## The rule

Everything a customer receives is named after the product:

| Artifact | Customer-visible name |
|---|---|
| macOS Audio Unit | `PumpCurve.component` |
| macOS VST3 | `PumpCurve.vst3` |
| Windows VST3 | `PumpCurve.vst3` |
| macOS installer | `PumpCurve-<version>-macOS-Universal.pkg` |
| macOS disk image | `PumpCurve-<version>-macOS-Universal.dmg` |

Technical identifiers are **not** renamed, because they are the upgrade identity
and changing them would break existing projects and registrations:

* bundle identifier `com.musicprod.sidechain`
* Audio Unit `aufx` / `SdCh` / `Musc`, version `1025`
* AU factory symbol `SideChainAUFactory`
* VST3 class/factory identity and the class UID inside the module
* parameter IDs, authentication identity, preset/state schema
* the executable file name inside the bundle (`Contents/MacOS/SideChain`)

## Why renaming the bundle is safe

A bundle name is not an identity: hosts resolve an Audio Unit through the
component registry (type/subtype/manufacturer) and a VST3 through its class UID.
Because the module binary is **not touched** by the rename, the class UID is
identical; because the bundle identifier and AU metadata are untouched, the
Audio Unit is the same component as before. The rename changes only the directory
name a host may display.

## How it is produced and verified

`SideChain/Tests/validation_common.sh` provides
`stage_customer_product_names <products_dir> <stage_dir>`; `validate_all.sh`
runs it as stage **8b** and `Builds/MacOSX/package_pumpcurve_release.sh` runs it
before packaging. The helper fails the build unless all of the following hold:

1. the built `SideChain.component` / `SideChain.vst3` exist;
2. the destination bundle does not already exist (a stale staging directory is
   never reused silently);
3. the staged bundle is **byte-identical** to the built bundle (per-file SHA-256
   manifest of the whole tree, so the rename provably changed nothing);
4. the bundle identifier is preserved and still `com.musicprod.sidechain`;
5. the Audio Unit registration metadata (`AudioComponents`) is unchanged;
6. `CFBundleName` and `CFBundleDisplayName` are `PumpCurve`;
7. both bundles stay universal (`arm64` + `x86_64`).

A staged manifest (`SHA256SUMS`) is written next to the staged bundles.
`Tests/validation_infra_tests.sh` covers the helper itself with fixtures: a valid
pair is staged, an existing destination is refused, a foreign bundle identifier is
rejected, a bundle that does not display as `PumpCurve` is rejected, and a missing
built bundle fails the stage.

## Upgrade behaviour

Earlier beta builds shipped the *same* Audio Unit identity inside bundles named
`SideChain.*` (and, before that, `SideChainer.*`). Two bundles carrying one
identity would both register, so the installer removes the old ones:

* `Builds/MacOSX/installer-scripts/preinstall` deletes a legacy bundle **only**
  when its `CFBundleIdentifier` is `com.musicprod.sidechain`, at both the system
  level (`/Library`) and in every user's `~/Library`. Anything else — including a
  third-party bundle that happens to share a legacy name — is left untouched and
  reported in the installer log.
* `Builds/MacOSX/installer-scripts/postinstall` asks the per-user audio component
  registrar to re-read plug-ins, so hosts do not keep showing the previous name
  from a cached registration. Measured behaviour: the registrar keeps serving the
  old name until it rescans a directory change, and restarting the *user-level*
  registrar is enough — no reboot or administrator action is required.

## Windows

The same rule applies to the Windows VST3. The reproducible Windows handoff
(`Tests/WINDOWS_BUILD_HANDOFF_0.4.1.md`) stages the built module as
`PumpCurve.vst3` and its packaging script installs that name, removes a
previously installed `SideChain.vst3` / `SideChainer.vst3`, verifies the installed
binary hash and leaves every other plug-in in place. No Windows binary or
installer is claimed until it has actually been built and verified on Windows.
