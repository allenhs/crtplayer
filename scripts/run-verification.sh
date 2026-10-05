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
# Without a graphics card the player draws a look at half size in a big window (2.12). The
# checks compare pictures pixel by pixel, so they ask for full size; the no-GPU suite
# tests the half-size drawing itself.
export CRTPLAYER_LOOK_DETAIL=${CRTPLAYER_LOOK_DETAIL:-full}
# NVIDIA's AI methods (2.15) stay out of every suite but their own, whatever is installed here.
export CRTPLAYER_NVFX_OFF=1
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
# 2.10: the Sega CD FMV look.
console_suite() {
  rm -rf "$XDG_CONFIG_HOME"
  run console console.txt
  echo "== Sega CD FMV look checks"
  checker "$O/console-checks.txt" python3 "$HERE/scripts/check-console.py" "$O"
}
# 2.10: Cable TV, with folder channels and one from the mock Jellyfin server.
tv_suite() {
  rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$O/tv-media" "$O/tv-jf"
  mkdir -p "$O/tv-media/movies/sub" "$O/tv-media/toons" "$O/tv-media/idents" "$O/tv-media/empty" "$O/tv-jf"
  cp "$M/sd_4x3_h264.mp4" "$O/tv-media/movies/Big.Movie.1994.mp4"
  cp "$M/hd_16x9_multitrack.mkv" "$O/tv-media/movies/sub/Another_Film.mkv"
  cp "$M/vertical_9x16_h264.mp4" "$O/tv-media/movies/Tall Story.mp4"
  cp "$M/fmv_natural.mp4" "$O/tv-media/toons/Toon Episode 1.mp4"
  cp "$M/ultrawide_64x27_vp9.webm" "$O/tv-media/toons/Toon Episode 2.webm"
  cp "$M/anamorphic_dvd_mpeg2.mkv" "$O/tv-media/toons/Toon Episode 10.mkv"
  ffmpeg -v error -y -i "$M/solid_red.mp4" -t 5 -c copy "$O/tv-media/idents/ident-a.mp4"
  ffmpeg -v error -y -i "$M/solid_white.mp4" -t 4 -c copy "$O/tv-media/idents/ident-b.mp4"
  echo "not a video" > "$O/tv-media/movies/notes.txt"
  local port=$((18900 + RANDOM % 90))
  python3 "$HERE/tests/jellyfin_mock.py" --media "$M" --port $port --version 10.10.3 --log "$O/tv-jf/requests.jsonl" > "$O/tv-jf/mock.log" 2>&1 &
  local mock=$!; sleep 1
  for t in tv tv-restore; do
    render "$t.txt"; sed -i "s#@JF@#http://127.0.0.1:$port#g" "$O/$t.txt"
    echo "== $t"
    timeout 600 "$BIN" --automation "$O/$t.txt" --automation-log "$O/$t.json" > "$O/$t.log" 2>&1
    echo "   exit code $?"
  done
  kill $mock 2>/dev/null
  echo "== Cable TV checks"
  checker "$O/tv-checks.txt" python3 "$HERE/scripts/check-tv.py" "$O" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
}
# 2.11: everyday playback (sticky subtitles and languages, delays, night mode, deinterlacing,
# shuffle / repeat, playlist files, the next video in a folder, the sleep timer).
everyday_suite() {
  rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$O/everyday-media"
  mkdir -p "$O/everyday-media/Episodes" "$O/everyday-media/tv-a" "$O/everyday-media/tv-b"
  cp "$M/short_a.mp4" "$O/everyday-media/Episodes/Episode 1.mp4"
  cp "$M/short_b.mp4" "$O/everyday-media/Episodes/Episode 2.mp4"
  cp "$M/short_c.mp4" "$O/everyday-media/Episodes/Episode 10.mp4"
  echo "not a video" > "$O/everyday-media/Episodes/Episode 3 notes.txt"
  cp "$M/hd_16x9_multitrack.mkv" "$O/everyday-media/tv-a/"
  cp "$M/multitrack_b.mkv" "$O/everyday-media/tv-b/"
  run everyday everyday.txt
  run everyday-restore everyday-restore.txt
  echo "== everyday playback checks"
  checker "$O/everyday-checks.txt" python3 "$HERE/scripts/check-everyday.py" "$O"
}
# 2.12: playback without a graphics card (frames converted and scaled on the CPU's cores).
# Run twice: on the plain window surface a machine without a GPU gets, and on the OpenGL
# widget (the surface a graphics card gets), where the same fast frames must also work.
nogpu_suite() {
  rm -rf "$O/nogpu-media"; mkdir -p "$O/nogpu-media/tv"
  cp "$M/sd_4x3_h264.mp4" "$O/nogpu-media/tv/Feature.mp4"
  local surface
  for surface in raster gl; do
    rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
    sed -e "s#ng-#ng-$surface-#g" "$HERE/tests/automation/nogpu.txt" > "$O/nogpu-$surface.src"
    sed -e "s#@M@#$M#g" -e "s#@O@#$O#g" "$O/nogpu-$surface.src" > "$O/nogpu-$surface.txt"; rm -f "$O/nogpu-$surface.src"
    echo "== nogpu-$surface"
    env -u CRTPLAYER_LOOK_DETAIL -u CRTPLAYER_FAST_PATH CRTPLAYER_VIDEO_SURFACE=$surface timeout 600 "$BIN" --automation "$O/nogpu-$surface.txt" --automation-log "$O/nogpu-$surface.json" > "$O/nogpu-$surface.log" 2>&1
    echo "   exit code $?"
  done
  echo "== playback without a graphics card"
  checker "$O/nogpu-checks.txt" python3 "$HERE/scripts/check-nogpu.py" "$O" "$M"
}
# 2.13: Enhance (sharper upscaling, frame generation). They are for a graphics card; here they
# are forced on with software OpenGL, on the path a graphics card takes (the OpenGL widget,
# frames as decoded). A second, short run checks that they stay off when not forced.
enhance_suite() {
  rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
  run enhance enhance.txt CRTPLAYER_VIDEO_SURFACE=gl CRTPLAYER_FAST_PATH=never CRTPLAYER_ENHANCE_FORCE=1
  rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
  run enhance-unforced enhance-unforced.txt CRTPLAYER_VIDEO_SURFACE=gl CRTPLAYER_FAST_PATH=never
  echo "== Enhance checks"
  checker "$O/enhance-checks.txt" python3 "$HERE/scripts/check-enhance.py" "$O"
}
# 1.8 features; three launches share settings and data so resume can be tested across restarts
polish_suite() {
  rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
  export XDG_CACHE_HOME=$TMPCFG/cache
  run polish polish.txt
  run polish-restore polish-restore.txt
  run polish-restore2 polish-restore2.txt
  echo "== polish checks"
  checker "$O/polish-checks.txt" python3 "$HERE/scripts/check-polish.py" "$O" "$M"
}
# 2.15: NVIDIA's AI methods for Enhance, with a stand-in for NVIDIA's SDK (build/nvfx-mock-sdk,
# or NVFX_MOCK_SDK): the player, its helper program and everything between them are the real
# ones; only the AI models are not. Then the ways it can fail.
nvidia_suite() {
  local sdk=${NVFX_MOCK_SDK:-$HERE/build/nvfx-mock-sdk}
  if [[ ! -f "$sdk/lib/libVideoFX.so" ]]; then echo "== NVIDIA checks skipped: no stand-in SDK at $sdk"; return; fi
  local base=(CRTPLAYER_VIDEO_SURFACE=gl CRTPLAYER_FAST_PATH=never CRTPLAYER_ENHANCE_FORCE=1 CRTPLAYER_NVFX_SDK="$sdk" HOME="$O/nv-home" XDG_CACHE_HOME="$O/nv-home/cache")
  rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$O/nv-home"; mkdir -p "$O/nv-home"
  # (software OpenGL draws too slowly here for draws to count as following one another quickly:
  # in this run they are made to, so that the hand-over from draw to draw is what gets exercised)
  run nvidia nvidia.txt -u CRTPLAYER_NVFX_OFF "${base[@]}" NVFX_MOCK_MARK=1 CRTPLAYER_NVFX_QUICK_MS=1000
  run nvidia-restore nvidia-restore.txt -u CRTPLAYER_NVFX_OFF "${base[@]}" NVFX_MOCK_MARK=1
  local name
  fault() { # name [env...]
    name=$1; shift
    rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
    sed -e "s#@N@#$name#g" "$HERE/tests/automation/nvidia-fault.txt" > "$O/$name.src"
    sed -e "s#@M@#$M#g" -e "s#@O@#$O#g" "$O/$name.src" > "$O/$name.txt"; rm -f "$O/$name.src"
    echo "== $name"
    env -u CRTPLAYER_NVFX_OFF "${base[@]}" NVFX_MOCK_MARK=1 "$@" timeout 600 "$BIN" --automation "$O/$name.txt" --automation-log "$O/$name.json" > "$O/$name.log" 2>&1
    echo "   exit code $?"
  }
  fault nv-fine
  fault nv-nocard NVFX_MOCK_FAIL_CARD=1
  fault nv-slow NVFX_MOCK_DELAY_MS=25
  fault nv-noload NVFX_MOCK_FAIL_LOAD=sr
  fault nv-nocreate NVFX_MOCK_FAIL_CREATE=1
  fault nv-crash NVFX_MOCK_CRASH_AFTER=60
  fault nv-hang NVFX_MOCK_HANG_AFTER=60
  fault nv-nosdk CRTPLAYER_NVFX_SDK=/nonexistent
  fault nv-nohelper CRTPLAYER_NVFX_HELPER=/nonexistent/crtplayer-nvfx PATH=/usr/bin:/bin
  fault nv-switchedoff CRTPLAYER_NVFX_OFF=1
  cp "$O/nv-home/cache/CRTPlayer/CRTPlayer/nvfx.log" "$O/nv-helper.log" 2>/dev/null
  echo "== NVIDIA checks"
  checker "$O/nvidia-checks.txt" python3 "$HERE/scripts/check-nvidia.py" "$O"
}
desk_suite() {
  rm -rf "$XDG_CONFIG_HOME"
  run desk desk.txt
  echo "== desk image checks"
  checker "$O/desk-checks.txt" python3 "$HERE/scripts/check-desk.py" "$O"
}
# 2.3: the 90s CG room; 2.14: your 3D models in every format.
cg_suite() {
  rm -rf "$XDG_CONFIG_HOME"
  run cg cg.txt
  echo "== 90s CG room checks"
  checker "$O/cg-checks.txt" python3 "$HERE/scripts/check-cg.py" "$O"
}
if [[ -n "${RUN_ONLY:-}" ]]; then   # development: just these suites, e.g. RUN_ONLY="tv console edit"
  for s in $RUN_ONLY; do "${s}_suite"; done
  exit 0
fi
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
  polish_suite
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
  console_suite
  if python3 -c "import PIL" 2>/dev/null; then tv_suite; fi
  if python3 -c "import PIL" 2>/dev/null; then everyday_suite; fi
  if python3 -c "import PIL" 2>/dev/null; then nogpu_suite; fi
  if python3 -c "import PIL" 2>/dev/null; then enhance_suite; fi
  if python3 -c "import PIL" 2>/dev/null; then nvidia_suite; fi
  rm -rf "$XDG_CONFIG_HOME"
  run sound sound.txt
  echo "== sound checks"
  checker "$O/sound-checks.txt" python3 "$HERE/scripts/check-sound.py" "$O"
  cg_suite
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
