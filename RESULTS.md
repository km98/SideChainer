# PumpCurve 0.4.1 - Windows x64 build result

| item | value |
|---|---|
| repository | km98/SideChainer |
| source revision | a83a083056513d380f19a988a02b2eda2c91bdf6 |
| workflow run | 38129702104 (attempt 1) |
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
1e60eb400c8980a51f876824310092b38b7ec82bf8287e2aebc1e2aee6f5376e  MANIFEST-staged.json
d0d9328e7345ea4a23122b19d1ca7c73668bf3c21217e5119ec0b8a4c01fa954  PumpCurve-0.4.1-Windows-x64-Setup.exe
0f540f02c8baccd0988763e351a5168c5fec10ee3bac6e282a9156bd1cdd83f2  PumpCurve-0.4.1-Windows-x64.zip
fb09a0f12a9ed5dccd7ac08acdc1245d3a3e4f533737160743de74559a82a1df  RUN-MANIFEST.json
fc55eae074910ff755fdaca1538286d3a6a9897b053bb77716d0464cf2869828  SHA256SUMS
f54359fb88e36ab259418d00f3c3a9f8461c19af7604ea1f4ff33ccb04159d9c  SideChain.jucer.resaved
83c4fada924dcabed7cd47fed79d7ae434487736781f09b99e16fadaf95d8c93  PumpCurve.vst3\Contents\x86_64-win\PumpCurve.vst3
``

## Not verified by this run (do not read it as a release)

- **Code signing: NOT SIGNED.** No Authenticode certificate exists;
  SmartScreen will warn until the installer is signed.
- **DAW acceptance: NOT TESTED.** No DAW runs on the runner, so FL Studio
  and Ableton Live insertion and operation are unverified.
- **Distribution: NOT PUBLISHED.** The website upload credential is not
  available, so no customer download serves this build yet.
