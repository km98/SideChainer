#!/bin/bash
# Reads the Logic bus diagnostic written by the SDCH_DIAG AU build.
# Usage: ./read_diag.sh [n]   (n = last n lines, default 12)
F=/tmp/sidechain_diag.txt
if [ ! -f "$F" ]; then
    echo "NO DIAGNOSTIC FILE YET ($F)."
    echo "1) Logic: fully quit and restart"
    echo "2) Insert 'Music-Prod: SideChain', select kick track as Side Chain source"
    echo "3) Amount=100, DUCK LENGTH=500, RELEASE=150, OFFSET=0"
    echo "4) Press PLAY for ~10 seconds"
    echo "5) Run ./read_diag.sh again"
    exit 1
fi
echo "=== last ${1:-12} diagnostic lines (one per ~0.5 s of audio) ==="
tail "-${1:-12}" "$F"
echo
echo "=== INTERPRETATION ==="
L=$(tail -1 "$F")
echo "$L"
python3 - "$L" << 'PYEOF'
import sys, re
line = sys.argv[1]
d = dict(re.findall(r'(\w+)=([-\w.]+)', line))
sc_pk   = float(d.get('scPk', '0'))
sc_ch   = d.get('scCh', '?')
playing = d.get('isPlaying', '?')
gate    = d.get('gateOpen', '?')
trig    = int(float(d.get('trig', '0')))
main_pk = float(d.get('mainPk', '0'))
min_gain= float(d.get('minGain', '1'))
print()
if playing in ('0', '?'):
    print(">> PLAYHEAD NOT PLAYING. Logic is not reporting playback to the plugin")
    print("   (CallHostTransportState). The transport gate therefore keeps the")
    print("   detector silent -> no ducking. Report back: this is a Logic/playhead issue.")
elif sc_ch in ('0', '?'):
    print(">> SIDECHAIN BUS INACTIVE (scCh=0). Logic has NOT enabled/connected bus 1.")
    print("   The Side Chain source selection did not reach the plugin. Report back.")
elif sc_pk < 0.001:
    print(">> SIDECHAIN BUS ACTIVE BUT SILENT (scPk~0). Bus 1 is enabled but no kick")
    print("   audio arrives. Check the kick track output / Side Chain source selection.")
elif trig == 0:
    print(">> SIDECHAIN AUDIO ARRIVES (scPk>0) BUT NO TRIGGER FIRES.")
    print("   Kick peak =", sc_pk, "- the -18 dBFS threshold + 8 dB rise may not be met.")
    print("   Try a louder, sharper kick. Report back with scPk value.")
elif min_gain > 0.99:
    print(">> TRIGGERS FIRE BUT ENVELOPE DOES NOT DUCK. Report back (unexpected).")
else:
    print(">> WORKING: sidechain arrives, triggers fire, ducking occurs (minGain",
          str(min_gain) + "). If you still hear NO ducking in Logic, the issue is")
    print("   Logic's output routing/monitoring, not the plugin DSP.")
PYEOF
