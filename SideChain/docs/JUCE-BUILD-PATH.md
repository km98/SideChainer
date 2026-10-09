# External JUCE (HISE) build path

The Xcode project does not contain JUCE. It compiles the `SideChain/JuceLibraryCode`
wrappers, which `#include` the JUCE modules from a separate JUCE checkout (the HISE
copy of JUCE is used on this machine).

That location is a single overridable build setting:

| Setting | Default |
| --- | --- |
| `SIDECHAIN_JUCE_ROOT` | `$(SRCROOT)/../../../../HISE/JUCE` |

`$(SRCROOT)` is `SideChain/Builds/MacOSX`, so the default resolves to the JUCE
checkout that sits next to the repository, e.g.
`/Users/martin/Documents/HISE/JUCE`. From it the project derives:

* `modules`
* `modules/juce_audio_plugin_client`
* `modules/juce_audio_processors/format_types/VST3_SDK`

## Standard development layout

Nothing to do. A checkout that keeps its existing distance from the JUCE install
builds exactly as before, because the default value resolves to the same directory
the project used previously.

## Build a clone that lives somewhere else

Pass the path to the existing JUCE checkout. Command-line build settings take
precedence over the project value, so no project file needs editing:

```sh
xcodebuild -project SideChain/Builds/MacOSX/SideChain.xcodeproj \
           -scheme "SideChain - All" -configuration Release \
           ARCHS="arm64 x86_64" ONLY_ACTIVE_ARCH=NO \
           SIDECHAIN_JUCE_ROOT=/path/to/HISE/JUCE \
           build
```

The same setting can be entered once in Xcode (Build Settings → search for
`SIDECHAIN_JUCE_ROOT`) instead of on the command line. A relocated build also needs
the second part of the command line above: with Xcode's default `ARCHS`, Release
builds are single-architecture, so `ARCHS="arm64 x86_64" ONLY_ACTIVE_ARCH=NO` is
required for a universal binary.

Avoid spaces in the JUCE path: the value is used unquoted by the AU `Rez` step.

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

## Known limitation

The regression and UI scripts under `SideChain/Tests` resolve the JUCE modules from
a fixed absolute path of their own; the override above only covers the Xcode build.
