#!/usr/bin/env bash
# Builds the self-contained Windows folder from an MSYS2 UCRT64 build.
#   package.sh BUILD_DIR OUT_DIR
# OUT_DIR gets crtplayer.exe with every library it needs beside it (Qt, GStreamer, the MinGW
# runtime), GStreamer's plugin scanner, lib/gstreamer-1.0 (the plugins that play video and
# audio), lib/gio/modules (TLS for https streams) and SDL2.dll (game controllers).
set -euo pipefail
BUILD=$1; OUT=$2
PREFIX=${MINGW_PREFIX:-/ucrt64}
rm -rf "$OUT"; mkdir -p "$OUT/lib/gstreamer-1.0" "$OUT/lib/gio/modules"
cp "$BUILD/crtplayer.exe" "$OUT/"

# Qt: its libraries and plugins (platform, styles, image formats, TLS) beside the player.
WDQ=""
for n in windeployqt6 windeployqt-qt6 windeployqt; do command -v "$n" >/dev/null 2>&1 && { WDQ=$n; break; }; done
[ -z "$WDQ" ] && [ -x "$PREFIX/share/qt6/bin/windeployqt.exe" ] && WDQ="$PREFIX/share/qt6/bin/windeployqt.exe"
[ -n "$WDQ" ] || { echo "windeployqt not found" >&2; exit 1; }
"$WDQ" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime --dir "$OUT" "$OUT/crtplayer.exe"

# GStreamer: the scanner, and the plugins. Plugins that pull in other toolkits or are only for
# encoding / production are left out (they are not used for playback, and would add hundreds
# of megabytes).
cp "$PREFIX/libexec/gstreamer-1.0/gst-plugin-scanner.exe" "$OUT/"
skip='gtk|qml|qt5|qt6|python|opencv|webrtc|srtp|rtmp|ladspa|lv2|fluidsynth|frei0r|x264|x265|svtav1|svthevc|aom|lame|twolame|voaacenc|fdkaac|openexr|opengl|vulkan|wpe|zxing|onnx|va$|msdk|qsv|ldac|sctp|dtls|nice|webp|gme|openni|teletext|zbar|chromaprint|curl|cdparanoia|dvdread|resindvd|wayland|x11|xvimage|ximage|dc1394|shout|jack|pulse|alsa|v4l2'
for f in "$PREFIX"/lib/gstreamer-1.0/libgst*.dll; do
  name=$(basename "$f" .dll); name=${name#libgst}
  if echo "$name" | grep -Eq "($skip)"; then continue; fi
  cp "$f" "$OUT/lib/gstreamer-1.0/"
done
cp "$PREFIX"/lib/gio/modules/*.dll "$OUT/lib/gio/modules/" 2>/dev/null || true
cp "$PREFIX/bin/SDL2.dll" "$OUT/"   # loaded at runtime, so no dependency scan finds it

# Every library those need, from the MinGW prefix, until nothing new turns up.
collect() {
  local changed=1
  while [ $changed = 1 ]; do
    changed=0
    while IFS= read -r dll; do
      local base; base=$(basename "$dll")
      if [ ! -e "$OUT/$base" ]; then cp "$dll" "$OUT/"; changed=1; fi
    done < <(find "$OUT" -name '*.dll' -o -name '*.exe' | xargs -d '\n' ldd 2>/dev/null \
               | awk -v p="$PREFIX/bin/" 'index($3, p) == 1 { print $3 }' | sort -u)
  done
}
collect
echo "Packaged: $(find "$OUT" -type f | wc -l) files, $(du -sh "$OUT" | cut -f1)"
echo "GStreamer plugins: $(ls "$OUT/lib/gstreamer-1.0" | wc -l)"
