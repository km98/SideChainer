# PumpCurve 0.4.1 - Windows x64 build result

| item | value |
|---|---|
| repository | km98/SideChainer |
| source revision | 638ee7427332109e81168f11c4a9f95a23de4ce7 |
| workflow run | 38129355552 (attempt 1) |
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
9f3c9efb7d64e7900cc7f06952d6aa0279ef604f1025ac2aaae5b47b7ccc9778  MANIFEST-staged.json
50e1f420ca8bb794b93591c76f0fd195164956cf8f811c1a27fd4ba002a94b09  PumpCurve-0.4.1-Windows-x64.zip
3c105bd3d2b390c738e9ebce1c4c38b90e055bcf7ac88de079e6aec8bfddef1b  RUN-MANIFEST.json
755cc16c141cb8a5a935057de2d4383df4b20075d7d3633163ad6060b015f187  SHA256SUMS
f54359fb88e36ab259418d00f3c3a9f8461c19af7604ea1f4ff33ccb04159d9c  SideChain.jucer.resaved
718e0afb448cee0a909cf2f6fa85c4653a611614f9a0f3ad62e2fbde64abae12  PumpCurve.vst3\Contents\x86_64-win\PumpCurve.vst3
``

## Not verified by this run (do not read it as a release)

- **Code signing: NOT SIGNED.** No Authenticode certificate exists;
  SmartScreen will warn until the installer is signed.
- **DAW acceptance: NOT TESTED.** No DAW runs on the runner, so FL Studio
  and Ableton Live insertion and operation are unverified.
- **Distribution: NOT PUBLISHED.** The website upload credential is not
  available, so no customer download serves this build yet.
