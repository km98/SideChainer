# PumpCurve 0.4.1 - Windows x64 build result

| item | value |
|---|---|
| repository | km98/SideChainer |
| source revision | 939d60be06148bab29dfb8cf2353df248deb9c24 |
| workflow run | 38128087207 (attempt 1) |
| runner | Windows X64 (win22) |
| signed | no - unsigned installer, no Authenticode certificate in this pipeline |
| step: validate  | success |
| step: juce      | success |
| step: projucer  | success |
| step: build     | success |
| step: inno      | success |
| step: installer | failure |
| step: lifecycle | skipped |
| step: verifier  | skipped |

## Artifact hashes (SHA-256)

``
d3a08862c9110bed0984ec4f6881cce9c3c392f9f9280375022d699207ad6ca8  MANIFEST-staged.json
508596e3d5c42468497787be839fec876517e2d39d740e796788adc0675a3b0c  PumpCurve-0.4.1-Windows-x64.zip
44929a9c417fd2e064288726220987b6ba1e7977c264ed25583c5486747d4502  RUN-MANIFEST.json
aa3d98090fe0e34b7c2235f3638f1b0892cee476a511e223362c9104b3e808e2  SHA256SUMS
f54359fb88e36ab259418d00f3c3a9f8461c19af7604ea1f4ff33ccb04159d9c  SideChain.jucer.resaved
a14b57f576b27c735bf2551e3ac7a2aea1a8a2cbd8bf9b1ef485eb285cbc0c3a  PumpCurve.vst3\Contents\x86_64-win\PumpCurve.vst3
``

## Not verified by this run (do not read it as a release)

- **Code signing: NOT SIGNED.** No Authenticode certificate exists;
  SmartScreen will warn until the installer is signed.
- **DAW acceptance: NOT TESTED.** No DAW runs on the runner, so FL Studio
  and Ableton Live insertion and operation are unverified.
- **Distribution: NOT PUBLISHED.** The website upload credential is not
  available, so no customer download serves this build yet.
