# PumpCurve 0.4.1 - Windows x64 build result

| item | value |
|---|---|
| repository | km98/SideChainer |
| source revision | 84c925b9b6403936567c6f036bb14c013970859e |
| workflow run | 38130864719 (attempt 1) |
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
47273b7688ecc02ae4970195d53ae558a6768288b033e82d0aa270f8d8eee5f8  MANIFEST-staged.json
4ec9db725b6af994def14e26341864dbdb510a7b36c38cc9ad3945a77e840eae  PumpCurve-0.4.1-Windows-x64-Setup.exe
9ac4447e945a2f4884be7a861d8166ee49727fc545a4a9a2ab96de958e69cd2b  PumpCurve-0.4.1-Windows-x64.zip
75f7f023b072cf2e087d3e3594928b203913b00cb39e0df40464f441652c5b25  RUN-MANIFEST.json
f07ab81b111a1344df8b5fe769545fb5e85bd725eef41d138f87b011fcccff8a  SHA256SUMS
f54359fb88e36ab259418d00f3c3a9f8461c19af7604ea1f4ff33ccb04159d9c  SideChain.jucer.resaved
ba289c5f92f92d6847edbe4a006c515db8ced3ab3ff3e704410c7d048877c6e2  PumpCurve.vst3\Contents\x86_64-win\PumpCurve.vst3
``

## Not verified by this run (do not read it as a release)

- **Code signing: NOT SIGNED.** No Authenticode certificate exists;
  SmartScreen will warn until the installer is signed.
- **DAW acceptance: NOT TESTED.** No DAW runs on the runner, so FL Studio
  and Ableton Live insertion and operation are unverified.
- **Distribution: NOT PUBLISHED.** The website upload credential is not
  available, so no customer download serves this build yet.
