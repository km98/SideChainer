# PumpCurve 0.4.1 - Windows x64 build result

| item | value |
|---|---|
| repository | km98/SideChainer |
| source revision | 003d55516695435a2d938f33e3479dc5d87b1a74 |
| workflow run | 38130079403 (attempt 1) |
| runner | Windows X64 (win22) |
| signed | no - unsigned installer, no Authenticode certificate in this pipeline |
| step: validate  | success |
| step: juce      | success |
| step: projucer  | success |
| step: build     | success |
| step: inno      | success |
| step: installer | success |
| step: lifecycle | success |
| step: verifier  | failure |

## Artifact hashes (SHA-256)

``
f28bb91a7002e02bd15516eced8c0be73f9ed84b3a9954cfc77a0f99940a3f61  MANIFEST-staged.json
3c336fb0ee8345c75c7e07409b8645bf215f33f0e4230cd8644738552ff66b32  PumpCurve-0.4.1-Windows-x64-Setup.exe
a505c5e863dbafdf891be030fe7a9573c1ce43cb2a1439c0cd1f6418b5b086d1  PumpCurve-0.4.1-Windows-x64.zip
3fd556dc4e9f81bd23a7d58b9e5382dd8779c4afcd25e91c63ea6246ee2b88bc  RUN-MANIFEST.json
ff446e1eb115794c663307e2835e59fd41361909550ac1b0b9597f47ef795c2e  SHA256SUMS
f54359fb88e36ab259418d00f3c3a9f8461c19af7604ea1f4ff33ccb04159d9c  SideChain.jucer.resaved
c30166e0bddb575eae244afff15297b934819cf6c1d9b6b280b3bd1f4d247795  PumpCurve.vst3\Contents\x86_64-win\PumpCurve.vst3
``

## Not verified by this run (do not read it as a release)

- **Code signing: NOT SIGNED.** No Authenticode certificate exists;
  SmartScreen will warn until the installer is signed.
- **DAW acceptance: NOT TESTED.** No DAW runs on the runner, so FL Studio
  and Ableton Live insertion and operation are unverified.
- **Distribution: NOT PUBLISHED.** The website upload credential is not
  available, so no customer download serves this build yet.
