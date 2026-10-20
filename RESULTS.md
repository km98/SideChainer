# PumpCurve 0.5.0 - Windows x64 build result

| item | value |
|---|---|
| repository | km98/SideChainer |
| source revision | 1e9bcde21a20902b39156403fe34f5e2c963f0f3 |
| workflow run | 39437564866 (attempt 1) |
| runner | Windows X64 (win22) |
| signed | no - unsigned installer, no Authenticode certificate in this pipeline |
| step: validate  | success |
| step: juce      | success |
| step: projucer  | success |
| step: build     | success |
| step: inno      | success |
| step: installer | success |
| step: lifecycle | success |
| step: verifier  | success |

## Artifact hashes (SHA-256)

``
34549076408ffa3da4ebd8b206d65e0acc92f6352ede3649d8af7422a7552502  MANIFEST-staged.json
a29de71a7fda6589a8e214f02929cff57313bfaa9f97cbc55d4dd7037d4c6760  PumpCurve-0.5.0-Windows-x64-Setup.exe
70b581cbe9a398426b22a0e03385b395ceef6f73fcfe77f9736fe0a02f254573  PumpCurve-0.5.0-Windows-x64.zip
4cce6593e06786cd91650a4e7ebcce4d81397f84e4eb622c939f2c42556d1339  RUN-MANIFEST.json
c9bfb7f661f9385fb17320ef1776a8883bc65e58e219b157865ad9b08d8f9fdc  SHA256SUMS
34999276f9f56381b7e0838ca49c9e008391842242c899e7a2b227d1ebc10410  SideChain.jucer.resaved
40e0e30452dd63c3dd3955f5367329346d114d3393b759bfcf4b9176c2c39ed6  PumpCurve.vst3\Contents\x86_64-win\PumpCurve.vst3
``

## Not verified by this run (do not read it as a release)

- **Code signing: NOT SIGNED.** No Authenticode certificate exists;
  SmartScreen will warn until the installer is signed.
- **DAW acceptance: NOT TESTED.** No DAW runs on the runner, so FL Studio
  and Ableton Live insertion and operation are unverified.
- **Distribution: NOT PUBLISHED.** The website upload credential is not
  available, so no customer download serves this build yet.
