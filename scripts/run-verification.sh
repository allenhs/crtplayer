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
# 2.17: videos from web sites. Video sites cannot be reached from where the tests run, so the site is
# tests/web_mock.py and yt-dlp is tests/fake_ytdlp.py, which answers the way the real one answers for YouTube (the
# picture and the sound as two addresses, subtitles, automatic captions, chapters, playlists, addresses that stop
# working). The real yt-dlp, where there is one, is run against the mock site's pages too. "Get yt-dlp" fetches
# from the mock site.
online_suite() {
  if [[ ! -f "$M/web/v_big.mp4" ]]; then echo "== online checks skipped: no web clips in $M (scripts/make-test-media.sh)"; return; fi
  local T="$O/online-tools" port=$((18600 + RANDOM % 300)) site mock rel
  rm -rf "$T" "$O"/online*; mkdir -p "$T/fake" "$T/old" "$T/none" "$T/real"
  cp "$HERE/tests/fake_ytdlp.py" "$T/fake/yt-dlp"; cp "$HERE/tests/fake_ytdlp.py" "$T/old/yt-dlp"
  printf '#!/bin/sh\necho v22.0.0\n' > "$T/fake/node"; cp "$T/fake/node" "$T/old/node"; chmod +x "$T"/fake/* "$T"/old/*
  site="http://127.0.0.1:$port"; rel="$site/release"
  python3 "$HERE/tests/web_mock.py" --media "$M" --port $port --log "$O/online-requests.jsonl" --ytdlp "$HERE/tests/fake_ytdlp.py" > "$O/online-mock.log" 2>&1 &
  mock=$!; sleep 1
  on_run() { # script tools [env...]
    local name=$1 tools=$2; shift 2
    mkdir -p "$O/$name"
    sed -e "s#@W@#$site#g" -e "s#@N@#$name#g" -e "s#@T@#$T#g" -e "s#@M@#$M#g" -e "s#@O@#$O#g" "$HERE/tests/automation/$name.txt" > "$O/$name.txt"
    echo "== $name"
    env CRTPLAYER_TOOLS_PATH="$T/$tools" FAKE_YTDLP_LOG="$O/$name-ytdlp.jsonl" "$@" timeout 900 "$BIN" --automation "$O/$name.txt" --automation-log "$O/$name.json" > "$O/$name.log" 2>&1
    echo "   exit code $?"
  }
  export XDG_CACHE_HOME=$TMPCFG/cache
  rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
  on_run online fake
  on_run online-restore fake
  rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
  on_run online-none none CRTPLAYER_YTDLP_RELEASE="$rel/yt-dlp" CRTPLAYER_DENO_RELEASE="$rel/deno"
  rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
  on_run online-badsum none CRTPLAYER_YTDLP_RELEASE="$rel/bad" CRTPLAYER_DENO_RELEASE="$rel/deno"
  rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
  on_run online-baddeno none CRTPLAYER_YTDLP_RELEASE="$rel/yt-dlp" CRTPLAYER_DENO_RELEASE="$rel/bad"
  rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
  on_run online-late fake CRTPLAYER_WEB_CONNECT_DELAY_MS=150
  # The real yt-dlp of this computer (outside the player's package), with whichever JavaScript runtime there is.
  local real; real=$(env -u APPDIR bash -c 'command -v yt-dlp' 2>/dev/null || true)
  if [[ -n "$real" && "${ONLINE_NO_REAL:-0}" != 1 ]]; then
    ln -sf "$real" "$T/real/yt-dlp"
    for rt in deno node bun qjs; do command -v $rt >/dev/null 2>&1 && ln -sf "$(command -v $rt)" "$T/real/$rt"; done
    rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
    on_run online-real real
  fi
  kill $mock 2>/dev/null
  rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
  echo "== videos from web sites"
  checker "$O/online-checks.txt" python3 "$HERE/scripts/check-online.py" "$O" "$site"
}
# 2.18: the browser of web videos, against the stand-in site's channels (tests/web_catalog.py)
browse_suite() {
  if [[ ! -d "$M/web/thumbs" ]]; then echo "== browse checks skipped: no pictures in $M/web/thumbs (scripts/make-test-media.sh)"; return; fi
  local T="$O/browse-tools" port=$((18900 + RANDOM % 300)) site mock
  rm -rf "$T" "$O"/browse*; mkdir -p "$T/fake" "$O/browse"
  cp "$HERE/tests/fake_ytdlp.py" "$T/fake/yt-dlp"; printf '#!/bin/sh\necho v22.0.0\n' > "$T/fake/node"; chmod +x "$T"/fake/*
  site="http://127.0.0.1:$port"
  python3 "$HERE/tests/web_mock.py" --media "$M" --port $port --log "$O/browse-requests.jsonl" --ytdlp "$HERE/tests/fake_ytdlp.py" > "$O/browse-mock.log" 2>&1 &
  mock=$!; sleep 1
  # Google Takeout's list of subscriptions, as it writes it (one of them followed already, a line that is no channel)
  printf 'Channel Id,Channel Url,Channel Title\nUCretrotubelabxxxxxxxxxx,http://www.youtube.com/channel/UCretrotubelabxxxxxxxxxx,Retro Tube Lab\nUCnightdrivefmxxxxxxxxxx,http://www.youtube.com/channel/UCnightdrivefmxxxxxxxxxx,Night Drive FM\nUCpixelkitchenxxxxxxxxxx,http://www.youtube.com/channel/UCpixelkitchenxxxxxxxxxx,"Pixel Kitchen"\nUClighthousecinemaxxxxxx,http://www.youtube.com/channel/UClighthousecinemaxxxxxx,Lighthouse Cinema\nnot a channel,http://example.com,Nothing\n' > "$O/browse/subscriptions.csv"
  sed -e "s#@W@#$site#g" -e "s#@N@#browse#g" -e "s#@M@#$M#g" -e "s#@O@#$O#g" "$HERE/tests/automation/browse.txt" > "$O/browse.txt"
  export XDG_CACHE_HOME=$TMPCFG/cache
  rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME"
  echo "== browse"
  env CRTPLAYER_TOOLS_PATH="$T/fake" FAKE_YTDLP_LOG="$O/browse-ytdlp.jsonl" FAKE_YTDLP_SITE="$site" CRTPLAYER_YT_SITE="$site" \
      CRTPLAYER_YTDLP_LATEST="$site/api/latest?tag=2026.10.09" CRTPLAYER_YTDLP_RELEASE="$site/release/yt-dlp" CRTPLAYER_DENO_RELEASE="$site/release/deno" \
      timeout 600 "$BIN" --automation "$O/browse.txt" --automation-log "$O/browse.json" > "$O/browse.log" 2>&1
  echo "   exit code $?"
  kill $mock 2>/dev/null
  rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
  echo "== the browser of web videos"
  checker "$O/browse-checks.txt" python3 "$HERE/scripts/check-browse.py" "$O" "$site" "$HERE/tests"
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
# 2.16: the right subtitle line after every jump: subtitles in the video (Matroska with SubRip and with ASS,
# MP4), subtitle files beside it (SubRip, ASS), a recording whose times do not begin at zero; other tracks,
# delays, files picked while the video plays. Every picture is read by scripts/check-subtitles.py.
subtitles_suite() {
  if [[ ! -f "$M/jump_srt.mkv" ]]; then echo "== subtitle checks skipped: no jump_* clips in $M (scripts/make-test-media.sh)"; return; fi
  local runs=() spec tag file fast dir
  sub_run() { # script tag [file [fast]]
    dir=$1${2:+-$2}
    rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$O/$dir"; mkdir -p "$O/$dir"
    sed -e "s#@N@#$dir#g" -e "s#@F@#${3:-}#g" -e "s#@FAST@#${4-fast}#g" -e "s#@M@#$M#g" -e "s#@O@#$O#g" "$HERE/tests/automation/subs-$1.txt" > "$O/subs-$dir.txt"
    echo "== subs-$dir"
    timeout 600 "$BIN" --automation "$O/subs-$dir.txt" --automation-log "$O/subs-$dir.json" > "$O/subs-$dir.log" 2>&1
    echo "   exit code $?"
    runs+=("$dir")
  }
  # (a jump "to the nearest keyframe" is left out for the recording: MPEG-TS has no list of its keyframes, and with
  # ten seconds between them the picture after such a jump is grey until the next one)
  for spec in srt:jump_srt.mkv:fast ass:jump_ass.mkv:fast text:jump_text.mp4:fast side:jump_side.mp4:fast sideass:jump_sideass.mkv:fast offset:jump_offset.ts:; do
    IFS=: read -r tag file fast <<< "$spec"
    sub_run jump "$tag" "$file" "$fast"
  done
  for spec in srt:jump_srt.mkv ass:jump_ass.mkv text:jump_text.mp4; do
    IFS=: read -r tag file <<< "$spec"
    sub_run tracks "$tag" "$file"
  done
  sub_run files
  sub_run pictures
  echo "== subtitle checks"
  checker "$O/subtitles-checks.txt" python3 "$HERE/scripts/check-subtitles.py" "$O" "${runs[@]}"
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
# 2.16.1: what is really on the screen (grabs of the screen itself, not of the player's own drawing):
# the look selector's list over the video, in a window and in full screen, and desk mode's
# see-through window in front of a plain backdrop. Once on the plain window surface and once on
# the OpenGL widget. X11 only: a Wayland compositor does not let a program grab the screen.
onscreen_suite() {
  local surface flags=""
  # (a see-through window needs a compositing manager on the test display)
  pgrep -x picom >/dev/null || pgrep -x xcompmgr >/dev/null || flags="--info-see-through"
  for surface in raster gl; do
    rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
    sed -e "s#@M@#$M#g" -e "s#@O@#$O#g" -e "s#@V@#sd_4x3_h264.mp4#g" -e "s#@N@#onscreen-$surface#g" \
      "$HERE/tests/automation/onscreen.txt" > "$O/onscreen-$surface.txt"
    echo "== onscreen-$surface"
    CRTPLAYER_VIDEO_SURFACE=$surface timeout 300 "$BIN" --automation "$O/onscreen-$surface.txt" --automation-log "$O/onscreen-$surface.json" > "$O/onscreen-$surface.log" 2>&1
    echo "   exit code $?"
  done
  echo "== on-screen checks"
  checker "$O/onscreen-checks.txt" python3 "$HERE/scripts/check-onscreen.py" "$O" onscreen-raster onscreen-gl $flags
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
  if python3 -c "import PIL" 2>/dev/null; then subtitles_suite; fi
  if python3 -c "import PIL" 2>/dev/null; then onscreen_suite; fi
  if python3 -c "import PIL" 2>/dev/null; then online_suite; fi
  if python3 -c "import PIL" 2>/dev/null; then browse_suite; fi
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
