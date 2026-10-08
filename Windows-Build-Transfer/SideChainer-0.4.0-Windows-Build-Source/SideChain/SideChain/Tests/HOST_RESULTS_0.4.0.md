# SideChainer 0.4.0 Host Results

## Release Candidate
- **Version:** 0.4.0
- **Mac artifact:** `SideChainer-0.4.0-macOS-SIGNED-NOTARIZED.zip`
- **SHA-256:** `10a4b89ecb8c0db66af77aa2851cf0affdf9ab764f4adf2334c0b38364904317`
- **Architecture:** Universal `x86_64 arm64` (AU and VST3)
- **Signing:** Developer ID Application, Team `3A4R5EKM7V`; Hardened Runtime and secure timestamp
- **Notarization:** Apple submission `c3b77e78-9dd2-4a11-8777-b77ba83c4d20` — **Accepted**; AU/VST3 tickets stapled

## Host Acceptance

| Host | Format | Actual GUI editor verified? | Playback verified? | Controls verified? | Result | Notes |
|------|--------|-----------------------------|---------------------|--------------------|--------|-------|
| Logic Pro | AU | NOT TESTED | NOT TESTED | NOT TESTED | NOT TESTED | No proven SideChainer instance or real Logic audio/editor result. |
| Ableton Live | VST3 | NOT TESTED | NOT TESTED | NOT TESTED | NOT TESTED | VST3 is installed and hash-matched; Live was not launched or tested. |
| Headless JUCE AU probe (not a DAW) | AU | NOT APPLICABLE | Automated signal scenarios: PASS (4/4) | Automated Amount/state scenarios: PASS | AUTOMATED PASS ONLY | Headless probe is not Logic/Ableton acceptance or GUI evidence. |

## Automated Validation
- **AU / `auval`: PASS** — `auval -v aufx SdCh Musc`; version 0.4.0, Cocoa view available, latency property passes.
- **VST3 topology: PASS** — zero failures; one stereo main input and one stereo output. No external sidechain bus is documented.
- **Universal binaries: PASS** — AU and VST3 each contain `x86_64 arm64`.
- **Code signing: PASS** — strict Developer ID signature verification; Team `3A4R5EKM7V`; Hardened Runtime and timestamp.
- **Notarization: PASS** — submission Accepted; stapled tickets validate for both bundles.
- **Gatekeeper: PASS for plugin payloads** — quarantined extracted AU/VST3 accepted by install assessment as Notarized Developer ID; distribution checks pass. ZIP itself is not a code object.
- **Installed candidate: PASS** — installed AU/VST3 executable hashes match signed candidate hashes.
- **Headless AU host probe: PASS, 4/4** — beat-triggered ducking, Amount, sample-rate/block scenarios, and state save/reload. Not a real DAW test.
- **Regression suite: PASS, 227/227** — seven suites in `Tests/run_all.sh`.
- **Editor harness: PASS, 43/43** — offscreen harness only; not real-host GUI evidence.
- **CPU benchmark:** 0.016% of one core in a synthetic 10 s, 48 kHz / block 512 optimized test; not a DAW measurement.

## Manual Logic Test Checklist
Use a new disposable, unsaved project only; record observations, do not assume routing support.
- [ ] New empty project
- [ ] Audio track
- [ ] Audio FX → Audio Units → Music-Prod → SideChain → Stereo
- [ ] Actual SideChainer editor opens
- [ ] Playback behavior
- [ ] Graph response
- [ ] Amount
- [ ] Duck Length
- [ ] Release
- [ ] Any host routing/sidechain controls actually exposed by Logic (record exact labels/options)
- [ ] No false assumption that an external sidechain bus exists

## Manual Ableton Test Checklist
Use a new disposable, unsaved Live Set only; record observations, do not assume routing support.
- [ ] New empty Live Set
- [ ] VST3 loads
- [ ] Actual SideChainer editor opens
- [ ] Playback behavior
- [ ] Graph response
- [ ] Amount
- [ ] Duck Length
- [ ] Release
- [ ] Any routing controls actually exposed by Live (record exact labels/options)
- [ ] Do not assume external sidechain routing exists

## Release Blocking Items
- Real Logic Pro and Ableton Live editor/playback/control acceptance is **NOT TESTED**.
- Public website/download is **NOT LIVE**; `/sidechainer` is documented as “Link Not Found” and the intended ZIP URL returns website HTML/404. No artifact upload has occurred.
- Music-Prod Studio has **no published SideChainer release/catalog item**; private storage and release/catalog migrations remain pending/unapplied.
- Windows is **NOT AVAILABLE / NOT TESTED**: no Windows build, artifact, or host validation is documented.

## Current Status
- **Verified:** signed/notarized universal macOS candidate; Gatekeeper assessment of plugin payloads; installed AU/VST3 identity; AU validation; VST3 topology; automated regression, UI harness, and headless AU checks.
- **Not verified:** real Logic/Ableton GUI, editor, playback, controls, or host routing behavior; public distribution and Studio catalog publication; Windows build/host support.
- **Before public release:** complete manual disposable Logic and Ableton acceptance; upload and verify the exact release artifact; deploy and verify the website/download; complete and verify the Studio release/catalog publication workflow. Windows remains unavailable until built and host-tested.

**Documentation caveat:** `Tests/MANUAL_HOST_TEST.md` describes an older Phase 7A control layout and external-sidechain workflow. Treat it as stale for 0.4.0: the current release report records a one-stereo-input/one-stereo-output VST3 topology and does not establish real Logic/Ableton routing UI behavior.
