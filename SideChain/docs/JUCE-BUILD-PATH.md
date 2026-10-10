# External JUCE (HISE) build path

The project does not contain JUCE. The Xcode project and the shell test scripts
compile local sources that `#include` the JUCE modules from a separate checkout
(the HISE copy of JUCE is used on this machine).

That location is a single overridable setting, shared by every build and test
entry point:

| Setting | Default |
| --- | --- |
| `SIDECHAIN_JUCE_ROOT` | the JUCE checkout next to the repository (`<repo>/../HISE/JUCE`) |

From it the project and scripts derive:

* `modules`
* `modules/juce_audio_plugin_client`
* `modules/juce_audio_processors/format_types/VST3_SDK`

`SideChain/Tests/validation_common.sh` implements the shell side once:
`resolve_juce_root` / `resolve_juce_modules`. Every script sources it, so no
script contains a machine-specific absolute path.

* When `SIDECHAIN_JUCE_ROOT` is set it is used verbatim; the resolver verifies
  that `modules/juce_core` and `modules/juce_audio_plugin_client` exist and
  fails with an actionable message otherwise. An invalid override is never
  silently ignored or replaced by a fallback.
* When it is unset the standard layout is used, i.e. `<repo>/../HISE/JUCE`,
  which mirrors the default of `SIDECHAIN_JUCE_ROOT` in the Xcode project and
  the `.jucer` module paths. A clone kept elsewhere has no sibling JUCE
  directory and must pass the override explicitly.

## Standard development layout

Nothing to do: a checkout that keeps its existing distance from the JUCE install
builds exactly as before.

## Build a clone that lives somewhere else

```sh
SIDECHAIN_JUCE_ROOT=/path/to/HISE/JUCE \
  xcodebuild -project SideChain/Builds/MacOSX/SideChain.xcodeproj \
             -scheme "SideChain - All" -configuration Release \
             ARCHS="arm64 x86_64" ONLY_ACTIVE_ARCH=NO \
             build
```

With Xcode's default `ARCHS`, Release builds are single-architecture, so
`ARCHS="arm64 x86_64" ONLY_ACTIVE_ARCH=NO` is required for a universal binary.
Avoid spaces in the JUCE path: the value is used unquoted by the AU `Rez` step.

## Tests and validation

```sh
# Full regression suite (forced rebuild), explicit JUCE location
SIDECHAIN_JUCE_ROOT=/path/to/HISE/JUCE SideChain/Tests/run_all.sh --rebuild

# One focused suite
SideChain/Tests/run_all.sh --suite PumpCurveDspTests --rebuild

# Everything: focused suites, full regression, UI, universal build, formats
SIDECHAIN_JUCE_ROOT=/path/to/HISE/JUCE SideChain/Tests/validate_all.sh
```

`SideChain/Tests/run_all.sh`

* `--rebuild` always recompiles every selected suite.
* Without `--rebuild` a suite is reused only when the recorded fingerprint of
  its compile command, flags, JUCE location and input file contents still
  matches; timestamps alone are never trusted, so a stale binary cannot produce
  a false pass.
* `--suite NAME` runs a single suite, `--list` lists them, `--dry-run` prints
  the compile/reuse plan, `--out-dir DIR` relocates the outputs.
* Binaries, build stamps and compile logs go to `SideChain/Tests/.build/`
  (ignored), so running the suites leaves the checkout clean.

`SideChain/Tests/validate_all.sh` is the reusable entry point. It can be called
from any working directory, keeps all logs in one output directory (a unique
temporary directory unless `--out-dir` names an empty one) and reports ten
stages independently:

1. prechecks (resolver, tools, project, output-dir safety, infrastructure tests)
2. focused PumpCurve preset/state and DSP integration suites
3. full regression with forced rebuild, per-suite totals
4. UI verification harness
5. universal Release build of AU + VST3 (`arm64 x86_64`)
6. architecture verification of both products (`lipo`)
7. VST3 factory/bus harness against the freshly built plugin
8. AU structural verification (metadata, factory symbol, slices, no installation)
8b. customer-visible product-name staging: the built `SideChain.*` bundles are
    staged as `PumpCurve.component` / `PumpCurve.vst3` with the rename proven to
    change no byte, no bundle identifier, no Audio Unit registration metadata and
    no architecture (see `docs/PUMPCURVE-RELEASE-NAMING.md`)
9. optional official Steinberg validator (reported as a limitation when absent;
   validates the customer-named bundle once staging succeeded)
10. summary, non-zero exit if any required stage failed

`SideChain/Tests/validation_infra_tests.sh` covers the infrastructure itself:
resolver default/override/invalid-path behaviour, the universal-architecture
enforcement and the runner's rebuild guarantees.

## Keeping Projucer in sync

The setting is part of the `.jucer` source of truth, so re-saving the project does
not lose it:

* `customXcodeFlags="SIDECHAIN_JUCE_ROOT=$(SRCROOT)/../../../../HISE/JUCE"` on each
  configuration defines the default in the generated build settings.
* The paths above are listed in each configuration's `headerPath`, written in the
  form `/$(SIDECHAIN_JUCE_ROOT)/modules`. The leading `/` is intentional: the
  Projucer Xcode exporter prefixes `$(SRCROOT)/` to any entry that does not look
  absolute, and that prefix would break an absolute override.

`SideChain/Builds/MacOSX/SideChain.xcodeproj/project.pbxproj` is committed, so a
normal build does not require Projucer to be installed or run.

## Known limitations

* `auval` is not part of the automated workflow: validating the AU would require
  installing or replacing the component in `~/Library/Audio/Plug-Ins/Components`.
  `validate_all.sh` therefore checks the AU structurally and verifies that the
  installed bundles did not change.
* The official standalone Steinberg VST3 validator is not installed on this
  machine; `validate_all.sh` reports that as a limitation and never labels the
  project's own factory/bus harness as official validation.
