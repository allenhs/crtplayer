#!/usr/bin/env bash
# Runs the packaged Windows folder on its own (a stripped PATH: none of MSYS2's libraries
# or plugins can help), with Mesa's software OpenGL beside it because the build machine has
# no graphics card. Plays a generated video, applies a look, loads a subtitle file, and opens
# desk mode on the arcade cabinet; then checks the results. Then grabs the screen itself to see
# what Windows really shows (tests/automation/onscreen.txt, scripts/check-onscreen.py).
#   smoke-test.sh PACKAGED_DIR WORK_DIR
set -uo pipefail
PKG=$(cd "$1" && pwd); WORK=$2
PREFIX=${MINGW_PREFIX:-/ucrt64}
rm -rf "$WORK"; mkdir -p "$WORK/app" "$WORK/media" "$WORK/out"
cp -r "$PKG"/. "$WORK/app/"
# Software OpenGL (test only; users have their graphics card's driver).
# Qt always takes "opengl32" from System32 for desktop OpenGL, so Mesa goes in as Qt's software
# renderer instead: opengl32sw.dll, picked with QT_OPENGL=software.
for f in libgallium_wgl.dll libglapi.dll; do [ -e "$PREFIX/bin/$f" ] && cp "$PREFIX/bin/$f" "$WORK/app/" && echo "software OpenGL: $f"; done
cp "$PREFIX/bin/opengl32.dll" "$WORK/app/opengl32sw.dll" && echo "software OpenGL: opengl32.dll as opengl32sw.dll"
pacman -Ql mingw-w64-ucrt-x86_64-mesa 2>/dev/null | grep -i '\.dll$' | sed 's/^/mesa package: /' | head -20
# Mesa's own libraries' dependencies (LLVM and so on), so software OpenGL loads in the test copy.
for f in opengl32sw.dll libgallium_wgl.dll; do
  [ -e "$WORK/app/$f" ] || continue
  ldd "$WORK/app/$f" 2>/dev/null | awk -v p="$PREFIX/bin/" 'index($3, p) == 1 { print $3 }' | while read -r d; do
    [ -e "$WORK/app/$(basename "$d")" ] || { cp "$d" "$WORK/app/"; echo "software OpenGL needs: $(basename "$d")"; }
  done
done

# Test media, made with MSYS2's GStreamer (the packaged player never sees it).
gst-launch-1.0 -q -e videotestsrc num-buffers=240 pattern=smpte ! video/x-raw,width=640,height=480,framerate=30/1 \
  ! x264enc ! h264parse ! mux. audiotestsrc num-buffers=350 ! audioconvert ! vorbisenc ! mux. \
  matroskamux name=mux ! filesink location="$WORK/media/test.mkv"
printf '1\n00:00:00,000 --> 00:01:00,000\nSUBTITLE TEST\n' > "$WORK/media/test.srt"
M=$(cygpath -m "$WORK/media"); O=$(cygpath -m "$WORK/out")
sed -e "s#@M@#$M#g" -e "s#@O@#$O#g" "$(dirname "$0")/smoke.txt" > "$WORK/smoke.txt"

APP="$WORK/app/crtplayer.exe"
CLEAN_PATH="/c/Windows/System32:/c/Windows"
export LOCALAPPDATA="$(cygpath -w "$WORK/localappdata")"; mkdir -p "$WORK/localappdata"
env PATH="$CLEAN_PATH" "$APP" --version > "$WORK/out/version.txt" 2>&1; echo "version rc=$?"
env PATH="$CLEAN_PATH" "$APP" --check-gstreamer > "$WORK/out/gstreamer.txt" 2>&1; echo "check-gstreamer rc=$?"
cat "$WORK/out/version.txt" "$WORK/out/gstreamer.txt"
# (MSYS2's timeout, by its full path: with the stripped PATH, "timeout" would be Windows' TIMEOUT.EXE)
export QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_RULES="qt.qpa.gl=true" GST_DEBUG="${GST_DEBUG:-2,wasapi*:1,waveform*:1,video-info:1}"
# Mesa's plain CPU renderer: the runner's D3D12 "Basic Render Driver" path is not dependable.
export GALLIUM_DRIVER=llvmpipe
/usr/bin/timeout 300 env PATH="$CLEAN_PATH" QT_OPENGL=software "$APP" --automation "$(cygpath -m "$WORK/smoke.txt")" \
  --automation-log "$O/smoke.json" > "$WORK/out/app.log" 2>&1
rc=$?
echo "automation rc=$rc"
if [ $rc -ne 0 ] && command -v gdb >/dev/null; then
  # A crash: run it again under the debugger and keep the backtrace.
  GDB=$(command -v gdb)
  /usr/bin/timeout 300 env PATH="$CLEAN_PATH" QT_OPENGL=software "$GDB" -batch -q -ex "set breakpoint pending on" -ex "break ExitProcess" -ex "break TerminateProcess" -ex "break RtlExitUserProcess" -ex run -ex "bt 40" -ex "info registers rip" \
    --args "$APP" --automation "$(cygpath -m "$WORK/smoke.txt")" --automation-log "$O/smoke-gdb.json" > "$WORK/out/gdb.log" 2>&1
  echo "---- under the debugger:"
  grep -v "^\[New Thread\|^\[Thread .* exited" "$WORK/out/gdb.log" | tail -60
fi
echo "---- the player's output:"
cat "$WORK/out/app.log"
python "$(dirname "$0")/check-smoke.py" "$WORK/out"
smoke_rc=$?

# What is really on the screen (the build machine has a desktop, composed by Windows as usual):
# the look selector's list over the video, and desk mode's see-through window. Once as the test
# machine draws the video by default (no OpenGL window), once with the OpenGL window a graphics
# card gets.
HERE=$(cd "$(dirname "$0")/../.." && pwd)
onscreen() { # name [VARIABLE=VALUE...]
  local name=$1; shift
  rm -rf "$WORK/localappdata"; mkdir -p "$WORK/localappdata"
  sed -e "s#@M@#$M#g" -e "s#@O@#$O#g" -e "s#@V@#test.mkv#g" -e "s#@N@#$name#g" "$HERE/tests/automation/onscreen.txt" > "$WORK/$name.txt"
  /usr/bin/timeout 240 env PATH="$CLEAN_PATH" QT_OPENGL=software "$@" "$APP" --automation "$(cygpath -m "$WORK/$name.txt")" \
    --automation-log "$O/$name.json" > "$WORK/out/$name.log" 2>&1
  echo "on-screen run $name rc=$?"
}
onscreen plain CRTPLAYER_VIDEO_SURFACE=raster
onscreen opengl CRTPLAYER_VIDEO_SURFACE=gl
echo "---- on the screen:"
python "$HERE/scripts/check-onscreen.py" "$WORK/out" plain opengl ${ONSCREEN_FLAGS:-} 2>&1 | sed 's/^/ON-SCREEN /'
onscreen_rc=${PIPESTATUS[0]}
if [ -n "${ONSCREEN_OLD_WAY:-}" ]; then   # for comparison: as before 2.16.1 (results do not count)
  onscreen oldway CRTPLAYER_VIDEO_SURFACE=gl CRTPLAYER_FULLSCREEN_BORDER=0
  python "$HERE/scripts/check-onscreen.py" "$WORK/out" oldway 2>&1 | sed -e 's/^FAIL/WAS-NO/' -e 's/^PASS/WAS-OK/' -e 's/^/ON-SCREEN /'
fi

# Videos from web sites (2.17): the picture and the sound of one video from two addresses of a small site on
# this machine (tests/web_mock.py, run with MSYS2's Python). Then, reported but not counted: "Get yt-dlp" from
# the internet, a page through the real yt-dlp.exe, and a video on YouTube.
mkdir -p "$WORK/webmedia/web"
gst-launch-1.0 -q -e videotestsrc num-buffers=600 pattern=ball ! video/x-raw,width=640,height=360,framerate=30/1 \
  ! x264enc key-int-max=60 ! h264parse ! mp4mux faststart=true ! filesink location="$WORK/webmedia/web/v_h264.mp4"
gst-launch-1.0 -q -e audiotestsrc num-buffers=862 samplesperbuffer=1024 ! audio/x-raw,rate=44100,channels=2 ! audioconvert \
  ! avenc_aac ! aacparse ! mp4mux faststart=true ! filesink location="$WORK/webmedia/web/a_aac.m4a"
cat > "$WORK/webmedia/web/dash.mpd" <<'MPD'
<?xml version="1.0" encoding="UTF-8"?>
<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="static" mediaPresentationDuration="PT20S" minBufferTime="PT2S" profiles="urn:mpeg:dash:profile:isoff-on-demand:2011">
 <Period>
  <AdaptationSet mimeType="video/mp4"><Representation id="v" codecs="avc1.64001e" width="640" height="360" frameRate="30" bandwidth="800000"><BaseURL>v_h264.mp4</BaseURL></Representation></AdaptationSet>
  <AdaptationSet mimeType="audio/mp4" lang="en"><Representation id="a" codecs="mp4a.40.2" audioSamplingRate="44100" bandwidth="128000"><BaseURL>a_aac.m4a</BaseURL></Representation></AdaptationSet>
 </Period>
</MPD>
MPD
# (2.18) pictures for the browser's tiles: GStreamer's test patterns
mkdir -p "$WORK/webmedia/web/thumbs"
for i in $(seq 0 47); do
  gst-launch-1.0 -q videotestsrc num-buffers=1 pattern=$((i % 25)) ! video/x-raw,width=640,height=360 ! jpegenc ! filesink location="$(printf "$WORK/webmedia/web/thumbs/t%02d.jpg" $i)"
done
for i in $(seq 0 11); do
  gst-launch-1.0 -q videotestsrc num-buffers=1 pattern=$((i + 3)) ! video/x-raw,width=176,height=176 ! jpegenc ! filesink location="$(printf "$WORK/webmedia/web/thumbs/a%02d.jpg" $i)"
done
printf 'Channel Id,Channel Url,Channel Title\r\nUCretrotubelabxxxxxxxxxx,http://www.youtube.com/channel/UCretrotubelabxxxxxxxxxx,Retro Tube Lab\r\nUCnightdrivefmxxxxxxxxxx,http://www.youtube.com/channel/UCnightdrivefmxxxxxxxxxx,Night Drive FM\r\nUCpixelkitchenxxxxxxxxxx,http://www.youtube.com/channel/UCpixelkitchenxxxxxxxxxx,Pixel Kitchen\r\n' > "$O/subscriptions.csv"
ls -la "$WORK/webmedia/web"
PORT=18650; SITE="http://127.0.0.1:$PORT"
python "$HERE/tests/web_mock.py" --media "$WORK/webmedia" --port $PORT --log "$WORK/out/web-requests.jsonl" > "$WORK/out/web-mock.log" 2>&1 &
MOCK=$!; sleep 2
online() { # name script timeout
  rm -rf "$WORK/localappdata" "$WORK/appdata"; mkdir -p "$WORK/localappdata" "$WORK/appdata"
  sed -e "s#@W@#$SITE#g" -e "s#@O@#$O#g" "$(dirname "$0")/$2" > "$WORK/$1.txt"
  APPDATA="$(cygpath -w "$WORK/appdata")" /usr/bin/timeout "$3" env PATH="$CLEAN_PATH" QT_OPENGL=software "$APP" --automation "$(cygpath -m "$WORK/$1.txt")" \
    --automation-log "$O/$1.json" > "$WORK/out/$1.log" 2>&1
  echo "web-video run $1 rc=$?"
}
online online smoke-online.txt 300
# (2.18) the browser: channels from Google Takeout's list, their new videos from the site's feeds, their pictures
rm -rf "$WORK/localappdata" "$WORK/appdata"; mkdir -p "$WORK/localappdata" "$WORK/appdata"
sed -e "s#@W@#$SITE#g" -e "s#@O@#$(cygpath -m "$O")#g" "$(dirname "$0")/smoke-browse.txt" > "$WORK/browse.txt"
APPDATA="$(cygpath -w "$WORK/appdata")" LOCALAPPDATA="$(cygpath -w "$WORK/localappdata")" CRTPLAYER_YT_SITE="$SITE" /usr/bin/timeout 300 env PATH="$CLEAN_PATH" QT_OPENGL=software \
  "$APP" --automation "$(cygpath -m "$WORK/browse.txt")" --automation-log "$O/browse.json" > "$WORK/out/browse.log" 2>&1
echo "web-video run browse rc=$?"
online online-net smoke-online-net.txt 900
kill $MOCK 2>/dev/null
echo "---- web videos, the player's output:"
grep -v "custom-downstream-sticky" "$WORK/out/online.log" | tail -40
echo "---- web videos:"
python "$(dirname "$0")/check-smoke-online.py" "$WORK/out" 2>&1 | sed 's/^/WEB /'
online_rc=${PIPESTATUS[0]}
ls -la "$WORK/appdata/CRTPlayer/CRTPlayer/tools" 2>/dev/null | sed 's/^/WEB tools: /'
grep -i "yt-dlp\|deno\|error\|warn" "$WORK/out/online-net.log" | grep -v "custom-downstream-sticky" | tail -25 | sed 's/^/WEB net log: /'
[ $smoke_rc -eq 0 ] && [ $onscreen_rc -eq 0 ] && [ $online_rc -eq 0 ]
