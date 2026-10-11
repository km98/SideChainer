# PumpCurve 0.4.1 - Windows x64 build result

| item | value |
|---|---|
| repository | km98/SideChainer |
| source revision | 878a080797ae05e6166c9ab279efe15729fc0937 |
| workflow run | 38128134745 (attempt 1) |
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
31296d7720aa9917f169930985e71d2e76c212ec5e84579cd08db926f2f9abda  MANIFEST-staged.json
d2a444209be21274618ccf6ce1de8cf2f5f0b8b313d72fa527e3480d52a5c130  PumpCurve-0.4.1-Windows-x64.zip
cf9a21c9288af9cd94178110565053a0553cb1897e94715376d9c4e482edea14  RUN-MANIFEST.json
453fefcaa570938fcfecac27188f2f74de3f99b471138b26dd23f02f4f6eb9d9  SHA256SUMS
f54359fb88e36ab259418d00f3c3a9f8461c19af7604ea1f4ff33ccb04159d9c  SideChain.jucer.resaved
6b3590efcaf28ef576eec3862025ca7b4593c76e64ca39939089b000f3bfc90e  PumpCurve.vst3\Contents\x86_64-win\PumpCurve.vst3
``

## Not verified by this run (do not read it as a release)

- **Code signing: NOT SIGNED.** No Authenticode certificate exists;
  SmartScreen will warn until the installer is signed.
- **DAW acceptance: NOT TESTED.** No DAW runs on the runner, so FL Studio
  and Ableton Live insertion and operation are unverified.
- **Distribution: NOT PUBLISHED.** The website upload credential is not
  available, so no customer download serves this build yet.
