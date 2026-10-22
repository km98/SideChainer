# PumpCurve 0.5.2 - Windows x64 build result

| item | value |
|---|---|
| repository | km98/SideChainer |
| source revision | d0d1b495d491d89419c8b5ee8cdc1dadb37382d6 |
| workflow run | 39740890911 (attempt 1) |
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
5190c0148fa398a282f36c9015695706117a8a4c826d62a05aca1001edd41e04  MANIFEST-staged.json
056d417a87d350fc0ebbf50ab73166e02aa56800fd65e8c99ceac32cf48f253a  PumpCurve-0.5.2-Windows-x64-Setup.exe
3d4feb653e96765e3b13f8876fe32a01fea9ba1dec7cc339f0d093f457a2fc6e  PumpCurve-0.5.2-Windows-x64.zip
7dfaf55b658bb2155f061edde7db7f56906944b5c5c32783964329f8a22d88cb  RUN-MANIFEST.json
c54e9e8bba6e17576902f77eec5d310705abc84db0ac1171470e468eb3459f0f  SHA256SUMS
dc4b8f727bf7d9910d42b98ce36e765f11fa694330f846e7ff8b7e2ee14816da  SideChain.jucer.resaved
a01d62cbb34b49f80043c94b473814819828dc8c4e8c9220b5417d543165da2a  PumpCurve.vst3\Contents\x86_64-win\PumpCurve.vst3
``

## Not verified by this run (do not read it as a release)

- **Code signing: NOT SIGNED.** No Authenticode certificate exists;
  SmartScreen will warn until the installer is signed.
- **DAW acceptance: NOT TESTED.** No DAW runs on the runner, so FL Studio
  and Ableton Live insertion and operation are unverified.
- **Distribution: NOT PUBLISHED.** The website upload credential is not
  available, so no customer download serves this build yet.
