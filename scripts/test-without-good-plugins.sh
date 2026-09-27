#!/usr/bin/env bash
# Recreates a system WITHOUT the GStreamer "good" plugin set (reported on Arch), runs the
# player, and restores the plugins. Needs root and a dpkg-based system (it uses the
# package's file list); the player's behaviour it checks is the same everywhere.
# Usage: sudo scripts/test-without-good-plugins.sh BINARY MEDIA_DIR OUT_DIR
set -uo pipefail
BIN=$(readlink -f "$1"); M=$(readlink -f "$2"); O=$(mkdir -p "$3" && readlink -f "$3")
HERE=$(cd "$(dirname "$0")/.." && pwd)
command -v dpkg > /dev/null || { echo "SKIP  needs dpkg to find the good plugins"; exit 0; }
H=$(mktemp -d)
restore() { for f in "$H"/*.so; do [ -e "$f" ] && mv "$f" "$(cat "$H/$(basename "$f").from")"; done; }
trap restore EXIT INT TERM
for f in $(dpkg -L gstreamer1.0-plugins-good | grep '\.so$'); do
    [ -e "$f" ] || continue
    dirname "$f" > "$H/$(basename "$f").from"
    mv "$f" "$H/"
done
export GST_REGISTRY=$(mktemp -u).bin
export CRTPLAYER_OS_RELEASE="$HERE/tests/os-release/arch"   # the messages as an Arch user sees them
"$BIN" --check-gstreamer > "$O/nogood-check.txt" 2>&1
cat > "$O/nogood.txt" <<EOS
open $M/base_only.ogv
waitstate playing 15000
wait 2500
report ogv
open $M/sd_4x3_h264.mp4
wait 4000
report mp4
quit
EOS
XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) timeout 90 "$BIN" --automation "$O/nogood.txt" --automation-log "$O/nogood.json" > "$O/nogood.log" 2>&1
echo "player exit code $?"
python3 - "$O" <<'EOS'
import json, sys, pathlib
out = pathlib.Path(sys.argv[1]); fails = 0
def check(n, ok, d):
    global fails; fails += 0 if ok else 1; print(f"{'PASS' if ok else 'FAIL'}  {n}: {d}")
chk = (out / 'nogood-check.txt').read_text()
check('--check-gstreamer: nothing essential missing', 'Missing essential elements: none' in chk, 'the core is there')
check('--check-gstreamer: names the Arch package and command', 'sudo pacman -S --needed gst-plugins-good' in chk, 'pacman command shown')
r = {e['label']: e for e in json.load(open(out / 'nogood.json')) if e['cmd'] == 'report'}
check('the player starts without the good plugins', 'ogv' in r, 'it ran instead of refusing')
check('an Ogg (Theora/Vorbis) file plays with the base plugins', r['ogv']['state'] == 'playing' and r['ogv']['framesPresented'] > 0,
      f"{r['ogv']['state']}, {r['ogv']['framesPresented']} frames")
check('audio uses a fallback output', r['ogv']['audioOutput'] != 'automatic', r['ogv']['audioOutput'])
miss = ' | '.join(r['ogv']['missingRecommended'])
check('it reports what will not work', 'MP4 / MOV files [gst-plugins-good]' in miss and 'automatic audio output' in miss, miss[:120])
err = r['mp4'].get('lastError') or ''
check('an MP4 fails with the Arch fix in the message', r['mp4']['state'] == 'error' and 'sudo pacman -S --needed gst-plugins-good' in err,
      err.split('\n')[1] if '\n' in err else err[:80])
print(f"\n{fails} no-good-plugins check(s) failed" if fails else "\nAll no-good-plugins checks passed")
sys.exit(1 if fails else 0)
EOS
