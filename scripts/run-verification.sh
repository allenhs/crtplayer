#!/usr/bin/env bash
# Runs the scripted verification suites against a built player.
# Usage: scripts/run-verification.sh <crtplayer-binary> <media-dir> <output-dir>
# Uses whatever display is current (X11 via DISPLAY, or Wayland via WAYLAND_DISPLAY +
# QT_QPA_PLATFORM=wayland). Settings are isolated in a temporary config dir, so your
# own CRT Player settings and presets are not touched.
# Set RUN_WAYLAND_ONLY=1 to run just the short Wayland suite.
# Runs a checker, showing and saving its output. A checker that fails without reporting a
# FAIL line (it crashed, or a helper it needs is missing) is reported as a FAIL too, so it
# can never drop out of the results unnoticed.
checker() {
  local out="$1"; shift
  "$@" > "$out.tmp" 2>&1
  local rc=$?
  if [[ $rc -ne 0 ]] && ! grep -q '^FAIL' "$out.tmp"; then
    echo "FAIL  checker did not complete: $(basename "$out" .txt) (exit $rc; see above)" >> "$out.tmp"
  fi
  cat "$out.tmp"; mv "$out.tmp" "$out"
}

set -uo pipefail
BIN=$(readlink -f "${1:?binary}"); M=$(readlink -f "${2:?media dir}"); O=$(mkdir -p "$3" && readlink -f "$3")
HERE=$(cd "$(dirname "$0")/.." && pwd)
TMPCFG=$(mktemp -d); export XDG_CONFIG_HOME=$TMPCFG/config XDG_DATA_HOME=$TMPCFG/data
trap 'rm -rf "$TMPCFG"' EXIT
render() { sed -e "s#@M@#$M#g" -e "s#@O@#$O#g" "$HERE/tests/automation/$1" > "$O/$1"; }
run() { # name script [env...]
  local name=$1 script=$2; shift 2
  render "$script"
  echo "== $name"
  env "$@" timeout 600 "$BIN" --automation "$O/$script" --automation-log "$O/$name.json" > "$O/$name.log" 2>&1
  echo "   exit code $?"
}
# 2.9: lossless cut and GIF clips, on copies of the media (cuts land next to them).
edit_suite() {
  [[ -d "$O/edit-ro" && $(id -u) == 0 ]] && chattr -i "$O/edit-ro" 2>/dev/null
  rm -rf "$XDG_CONFIG_HOME" "$O/edit-media" "$O/edit-ro" "$O/edit-home"
  mkdir -p "$O/edit-media" "$O/edit-ro" "$O/edit-home"
  for f in sd_4x3_h264.mp4 hd_16x9_multitrack.mkv ultrawide_64x27_vp9.webm anamorphic_dvd_mpeg2.mkv; do cp "$M/$f" "$O/edit-media/"; done
  # A folder no one can write to (as root, only the immutable attribute stops writes).
  cp "$M/sd_4x3_h264.mp4" "$O/edit-ro/" && chmod 555 "$O/edit-ro"
  [[ $(id -u) == 0 ]] && chattr +i "$O/edit-ro" 2>/dev/null
  (cd "$O" && sha256sum edit-media/* edit-ro/* > edit-before.sha256)
  run edit edit.txt HOME="$O/edit-home"
  [[ $(id -u) == 0 ]] && chattr -i "$O/edit-ro" 2>/dev/null
  chmod 755 "$O/edit-ro"
  echo "== cut and GIF checks"
  checker "$O/edit-checks.txt" python3 "$HERE/scripts/check-edit.py" "$O" "$M"
}
desk_suite() {
  rm -rf "$XDG_CONFIG_HOME"
  run desk desk.txt
  echo "== desk image checks"
  checker "$O/desk-checks.txt" python3 "$HERE/scripts/check-desk.py" "$O"
}
if [[ "${RUN_WAYLAND_ONLY:-0}" == 1 ]]; then
  run wayland wayland.txt
  desk_suite
  echo "== composited screen (what the compositor shows, via Weston's screenshooter)"
  checker "$O/composited-checks.txt" python3 "$HERE/scripts/check-composited.py" "$BIN" "$M" wayland
else
  run full full.txt
  run frame-step frame-step.txt
  rm -rf "$XDG_CONFIG_HOME"
  run persist-1 persist-1.txt
  run persist-2 persist-2.txt
  # Hide every H.265 decoder to provoke the missing-plugin path.
  rm -rf "$XDG_CONFIG_HOME"
  RANKS=$(gst-inspect-1.0 2>/dev/null | awk -F: '{gsub(/ /,"",$2); print $2}' | grep -E '^(avdec_h265|avdec_hevc|libde265dec|vulkanh265dec|vah265dec|vaapih265dec|nvh265dec|nvh265sldec|v4l2slh265dec|v4l2h265dec|openh265dec)$' | sed 's/$/:NONE/' | paste -sd, -)
  run missing-codec missing-codec.txt GST_PLUGIN_FEATURE_RANK="$RANKS"
  rm -rf "$XDG_CONFIG_HOME"
  run effects effects.txt
  echo "== effect image checks"
  checker "$O/effects-checks.txt" python3 "$HERE/scripts/check-effects.py" "$O"
  rm -rf "$XDG_CONFIG_HOME"
  run sim sim.txt
  echo "== simulation image checks"
  checker "$O/sim-checks.txt" python3 "$HERE/scripts/check-sim.py" "$O"
  # 1.8 features; three launches share settings and data so resume can be tested across restarts
  rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
  export XDG_CACHE_HOME=$TMPCFG/cache
  run polish polish.txt
  run polish-restore polish-restore.txt
  run polish-restore2 polish-restore2.txt
  echo "== polish checks"
  checker "$O/polish-checks.txt" python3 "$HERE/scripts/check-polish.py" "$O" "$M"
  # 2.0 desk-mode scenes
  rm -rf "$XDG_CONFIG_HOME"
  run scene scene.txt
  echo "== scene checks"
  checker "$O/scene-checks.txt" python3 "$HERE/scripts/check-scene.py" "$O"
  rm -rf "$XDG_CONFIG_HOME"
  run wall wall.txt
  echo "== wall checks"
  checker "$O/wall-checks.txt" python3 "$HERE/scripts/check-wall.py" "$O"
  rm -rf "$XDG_CONFIG_HOME"
  run theater theater.txt
  echo "== theater checks"
  checker "$O/theater-checks.txt" python3 "$HERE/scripts/check-theater.py" "$O"
  rm -rf "$XDG_CONFIG_HOME"
  run arcade arcade.txt
  echo "== arcade cabinet checks"
  checker "$O/arcade-checks.txt" python3 "$HERE/scripts/check-arcade.py" "$O"
  edit_suite
  rm -rf "$XDG_CONFIG_HOME"
  run sound sound.txt
  echo "== sound checks"
  checker "$O/sound-checks.txt" python3 "$HERE/scripts/check-sound.py" "$O"
  rm -rf "$XDG_CONFIG_HOME"
  run cg cg.txt
  echo "== 90s CG room checks"
  checker "$O/cg-checks.txt" python3 "$HERE/scripts/check-cg.py" "$O"
  # 1.9: gamepad (SDL virtual controller), room backdrop, Game Mode, MPRIS
  rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
  run gamepad gamepad.txt
  rm -rf "$XDG_CONFIG_HOME"
  run gamescope gamescope.txt GAMESCOPE_WAYLAND_DISPLAY=gamescope-0
  echo "== Bazzite checks"
  checker "$O/bazzite-checks.txt" python3 "$HERE/scripts/check-bazzite.py" "$O"
  echo "== MPRIS checks"
  checker "$O/mpris-checks.txt" python3 "$HERE/scripts/check-mpris.py" "$BIN" "$M"
  echo "== keep awake (no dimming / sleep while playing)"
  checker "$O/inhibit-checks.txt" python3 "$HERE/scripts/check-inhibit.py" "$BIN" "$M"
  echo "== portability (install hints per distribution)"
  checker "$O/portability-checks.txt" python3 "$HERE/scripts/check-portability.py" "$BIN"
  if [[ "${RUN_SYSTEM_TESTS:-0}" == 1 ]]; then   # modifies the system temporarily; release runs only
    echo "== without the good plugins (as on the Arch report)"
    checker "$O/nogood-checks.txt" "$HERE/scripts/test-without-good-plugins.sh" "$BIN" "$M" "$O"
  fi
  desk_suite
  if python3 -c "import PIL" 2>/dev/null; then
    echo "== jellyfin (mock server, 10.8 and 10.10 APIs)"
    "$HERE/scripts/run-jellyfin-tests.sh" "$BIN" "$M" "$O" | grep -E "FAIL|checks? (passed|failed)"
  fi
fi
python3 "$HERE/scripts/summarize.py" "$O"
