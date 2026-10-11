# PumpCurve 0.4.1 - Windows x64 build result

| item | value |
|---|---|
| repository | km98/SideChainer |
| source revision | 81bea9b50916c6062ca072083c545a0a04cd55da |
| workflow run | 38130471153 (attempt 1) |
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
9f746145a662eda7c832f5edf847a3d8b55471dfe38b461d1bc432d407ae5f6f  MANIFEST-staged.json
7178ae493cc596fd4c25b97be662c6a052087f74b140c772d6f76baaa4a123ab  PumpCurve-0.4.1-Windows-x64-Setup.exe
7d337bf830a05126e9e92b4c5a4caf3dace712154e3af164ba7d9663398fd43f  PumpCurve-0.4.1-Windows-x64.zip
68347e8703561e345c298b3b73973f5e8ab6183a7b5a5b6a375529d12cc41987  RUN-MANIFEST.json
48b54f4e63938fd13c4f27119597b0b8ef45c1c3bb6a1cf177ffcadb209bbb00  SHA256SUMS
f54359fb88e36ab259418d00f3c3a9f8461c19af7604ea1f4ff33ccb04159d9c  SideChain.jucer.resaved
03a5984e5fe1654acb0ed29add281f13a3d414efb714991f0c6182c9073a03cd  PumpCurve.vst3\Contents\x86_64-win\PumpCurve.vst3
``

## Not verified by this run (do not read it as a release)

- **Code signing: NOT SIGNED.** No Authenticode certificate exists;
  SmartScreen will warn until the installer is signed.
- **DAW acceptance: NOT TESTED.** No DAW runs on the runner, so FL Studio
  and Ableton Live insertion and operation are unverified.
- **Distribution: NOT PUBLISHED.** The website upload credential is not
  available, so no customer download serves this build yet.
