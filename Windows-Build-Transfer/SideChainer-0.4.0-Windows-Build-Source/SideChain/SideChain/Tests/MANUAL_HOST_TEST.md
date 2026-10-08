# SideChain — Manual Host Verification Guide

**MANUAL USER VERIFICATION REQUIRED** — these tests must be performed by you (Martin) in the actual host applications. The automated regression suite (`Tests/run_all.sh`) verifies the DSP and parameter plumbing, but only a human in a real host can confirm the sidechain routing, the audible ducking and the on-screen graph behaviour.

Plugin versions used for this guide: the Phase 7A Release builds (Phase 6 UI + Phase 7A state hardening) in

- AU: `Builds/MacOSX/build/Release/SideChain.component` (already installed in `~/Library/Audio/Plug-Ins/Components/`)
- VST3: `Builds/MacOSX/build/Release/SideChain.vst3`

What you should expect to see/hear in general:

- The plugin has **two controls**: a large **SIDECHAIN AMOUNT** knob (mint) and a smaller **RELEASE** knob (gold).
- The graph shows four traces: **DUCK** (red translucent fill), **SIDECHAIN** (amber), **INPUT** (grey), **OUTPUT** (mint).
- Default settings: Amount 50 %, Release 150 ms.
- When the sidechain plays, the DUCK fill drops and the OUTPUT trace dips. When the sidechain stops, the DUCK tail recovers over roughly 0.3–4 s depending on the Release knob (short = fast recovery, long = slow).

---

## Part 1 — Logic Pro 12.0.1 (AU)

### Setup

1. Open Logic Pro and create a new empty project.
2. Add two software/audio tracks:
   - Track **A**: the *music* you want to duck (e.g. a pad or a synth loop — use a Loop from the Loop Browser if you have nothing handy).
   - Track **B**: the *trigger* (e.g. a drum loop or a kick pattern).
3. On Track A, click the **Audio FX** slot and choose **Audio Units → Music-Prod → SideChain → Stereo**.
4. The SideChain window opens. Confirm you see the graph, the large AMOUNT knob and the smaller RELEASE knob.

### Sidechain routing (this is how Logic presents it)

5. In the **top-right corner of the plugin window header**, Logic shows a **"Sidechain" dropdown menu** (this is Logic's generic UI for AU plugins that expose an auxiliary input bus). Click it.
6. The menu lists available sources. Choose **Track B → (your trigger track)**. This is Logic's representation of the AU sidechain (aux) input of our plugin.
   - Note exactly what the menu says on your system and record it in the checklist below — that is the "how Logic presents the sidechain" observation we need.
7. Play the project. You should now hear:
   - Track A's music ducking every time the trigger plays.
   - NO trace of the trigger itself in the output (the sidechain never reaches the output).
8. Watch the graph while it plays: the amber SIDECHAIN trace should spike with the trigger, the red DUCK fill should drop immediately (fast attack), and recover afterwards (slow release tail).

### The checks (tick as you go)

| # | Check | Pass? |
|---|-------|-------|
| 1 | Plugin loads from Audio Units → Music-Prod → SideChain | ☐ |
| 2 | Plugin appears as an audio effect (stereo) | ☐ |
| 3 | Sidechain selector is visible in the plugin header | ☐ (record the exact menu wording: ________) |
| 4 | Track B can be selected as the sidechain source | ☐ |
| 5 | Track A audio reaches the plugin (INPUT trace moves) | ☐ |
| 6 | Sidechain reaches the detector (SIDECHAIN trace moves with Track B) | ☐ |
| 7 | Sidechain is NOT audible at the output (mute Track A — hear only silence) | ☐ |
| 8 | Ducking follows the sidechain (audible + DUCK/OUTPUT traces) | ☐ |
| 9 | Turn the AMOUNT knob: 0 % = no ducking at all; 100 % = very deep | ☐ |
| 10 | Turn the RELEASE knob: left ≈ 50 ms = fast snappy recovery; right ≈ 1000 ms = long slow tail | ☐ |
| 11 | Unassign the sidechain (set the header selector back to "None") → ducking stops completely | ☐ |
| 12 | Automate AMOUNT in Logic (press "A", draw an automation curve over the SideChain → Sidechain Amount lane) — ducking depth follows smoothly, no clicks | ☐ |
| 13 | Automate RELEASE the same way — recovery time follows smoothly | ☐ |
| 14 | Save the project, close it, reopen it — knob positions are restored | ☐ |
| 15 | Plugin remains stable (no crash, no glitching) after all the above | ☐ |

---

## Part 2 — Ableton Live 11 Suite (VST3)

### Setup

1. Open Ableton Live (Session or Arrangement view, both work).
2. Make sure Live scans VST3s: Options → Preferences → Plug-Ins → confirm "Use VST3 Plug-In System Folders" is on, then Rescan.
3. Drag **SideChain** (under Plug-Ins → VST3 → Music-Prod) onto an audio track containing music (Track A).
4. Create a second track (Track B) with a drum loop / kick as the trigger.

### Sidechain routing (this is how Live presents it)

5. In the SideChain device panel, click the **expand triangle** (top-left of the device) so all parameters show.
6. Live shows the plugin's parameters (Sidechain Amount, Release) as sliders in the device. Our custom UI opens when you click the **wrench/pencil icon** on the device title bar.
7. Live's sidechain routing for VST3 aux inputs appears in the **audio routing section of the track**:
   - Set **Audio To** of Track B (the trigger) to **"Tracks & Router"**, then in the dropdown below choose **Track A → the "Sidechain" input**.
   - Live's exact naming for the aux input may show as "Sidechain" or "2-Input" — record what you see in the checklist.
8. Play both tracks. Expected result is the same as Logic: Track A ducks with the trigger, trigger never audible at the output, graph shows the cycle.

### The checks (tick as you go)

| # | Check | Pass? |
|---|-------|-------|
| 1 | VST3 loads (drag from Plug-Ins browser) | ☐ |
| 2 | Sidechain bus is available in Track B's routing ("Audio To" → Track A → Sidechain input) | ☐ (record the exact wording: ________) |
| 3 | Trigger is routed and the SIDECHAIN trace moves with Track B | ☐ |
| 4 | Main audio remains separate (ducking only, no trigger bleed) | ☐ |
| 5 | Ducking follows the trigger (audible + graph) | ☐ |
| 6 | AMOUNT changes depth (0 % = off, 100 % = deep) | ☐ |
| 7 | RELEASE changes recovery (left fast, right slow — clearly audible difference) | ☐ |
| 8 | Remove the routing (set Track B back to "Audio To: Off") → ducking stops | ☐ |
| 9 | Automate Release in Arrangement view — recovery follows smoothly | ☐ |
| 10 | Save/reopen the Set — state restores (both knobs) | ☐ |

---

## Reporting

When done, fill in the checklist marks and the two "exact wording" fields. Those observations complete the host-verification evidence that automation cannot produce. If anything fails, note the step number and what you saw instead — that maps directly to a reproducible bug report.

*This guide was written for a non-programmer. No software installation is required; everything uses the already-built plugin and the already-installed hosts.*
