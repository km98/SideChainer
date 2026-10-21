# PumpCurve 0.5.1 - Windows x64 build result

| item | value |
|---|---|
| repository | km98/SideChainer |
| source revision | 41d4dcd5cca7c39bde9be8bc65ac60b166515b37 |
| workflow run | 39620222559 (attempt 1) |
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
82874440048e0bfe5d9386e223ac7b0a9d4ceb1436341f3e6cf3ae931986a4ba  MANIFEST-staged.json
09bfb1a190c5182bef683666aaeaa4e571f5e62f270dacd0a29f4764e9c5a8c3  PumpCurve-0.5.1-Windows-x64-Setup.exe
d53e2ff042db30fcadfa27251e39d26b1fac5895d90455d2104561857c7618ec  PumpCurve-0.5.1-Windows-x64.zip
86d6b9d81c2deeca5621ee8b32542493a0f9d946cab4751b4aa12820fcbedeed  RUN-MANIFEST.json
aef783734c55dde0bfdea5649e47d67804e4d2cb5036a0b2b81272a5474e1bd6  SHA256SUMS
74da0a82c697503ff174581d68726480a39eee137af13d7a378bc191bd8d39c4  SideChain.jucer.resaved
12731d5d9633cc758661fb40c1a65db212fdbdcdf0dbecd70b590220a57ec03c  PumpCurve.vst3\Contents\x86_64-win\PumpCurve.vst3
``

## Not verified by this run (do not read it as a release)

- **Code signing: NOT SIGNED.** No Authenticode certificate exists;
  SmartScreen will warn until the installer is signed.
- **DAW acceptance: NOT TESTED.** No DAW runs on the runner, so FL Studio
  and Ableton Live insertion and operation are unverified.
- **Distribution: NOT PUBLISHED.** The website upload credential is not
  available, so no customer download serves this build yet.
