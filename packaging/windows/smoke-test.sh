#!/usr/bin/env bash
# Runs the packaged Windows folder on its own (a stripped PATH: none of MSYS2's libraries
# or plugins can help), with Mesa's software OpenGL beside it because the build machine has
# no graphics card. Plays a generated video, applies a look, loads a subtitle file, and opens
# desk mode on the arcade cabinet; then checks the results.
#   smoke-test.sh PACKAGED_DIR WORK_DIR
set -uo pipefail
PKG=$(cd "$1" && pwd); WORK=$2
PREFIX=${MINGW_PREFIX:-/ucrt64}
rm -rf "$WORK"; mkdir -p "$WORK/app" "$WORK/media" "$WORK/out"
cp -r "$PKG"/. "$WORK/app/"
# Software OpenGL (test only; users have their graphics card's driver).
for f in opengl32.dll libgallium_wgl.dll libglapi.dll; do [ -e "$PREFIX/bin/$f" ] && cp "$PREFIX/bin/$f" "$WORK/app/"; done

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
env PATH="$CLEAN_PATH" QT_OPENGL=desktop timeout 300 "$APP" --automation "$(cygpath -m "$WORK/smoke.txt")" \
  --automation-log "$O/smoke.json" > "$WORK/out/app.log" 2>&1
echo "automation rc=$?"
tail -20 "$WORK/out/app.log"
python "$(dirname "$0")/check-smoke.py" "$WORK/out"
