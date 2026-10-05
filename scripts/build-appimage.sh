#!/usr/bin/env bash
# Builds CRT Player as an AppImage.
#
# What is bundled: the player and Qt 6 (Core, Gui, Widgets, OpenGL, OpenGLWidgets, DBus,
# Wayland client) with the xcb and Wayland platform plugins. This makes the AppImage
# independent of the host's Qt version.
#
# What is deliberately NOT bundled (taken from the host system):
#   * GStreamer and every GStreamer plugin: the player's promise is to decode with the
#     codecs and hardware decoders installed on YOUR system.
#   * GLib/GObject/GIO: host GStreamer plugins are built against the host GLib; two
#     copies in one process break.
#   * Wayland client libraries, Mesa/NVIDIA GL/EGL/GBM/DRM, libstdc++, glibc, fontconfig,
#     freetype: these must match the host's compositor and drivers.
#   * OpenSSL (libssl/libcrypto): Qt's TLS plugin loads the host's copy, which the
#     distribution keeps patched. HTTPS for Jellyfin therefore needs OpenSSL 3 on the host.
#   * libproxy (and, through it, curl and GnuTLS): part of the host's networking stack.
#     GStreamer's HTTPS streams use the host's GnuTLS via glib-networking, which itself
#     depends on libproxy; a bundled second copy of these would clash in-process.
#
# Host requirements (when built on Ubuntu 24.04, as the released AppImage is):
#   glibc >= 2.38 and libstdc++ from GCC >= 12 (the bundled Qt needs them), and
#   GStreamer >= 1.18 with plugins-base. Met by Bazzite / Fedora 39+, Ubuntu 24.04+,
#   Debian 13, Arch, openSUSE Tumbleweed. Build on an older distro for older targets.
#
# Usage: scripts/build-appimage.sh [output-dir]      (default: ./dist)
# Needs: cmake, a C++17 compiler (and cc), Qt 6 dev packages incl. qt6-wayland, GStreamer dev
# packages, patchelf, curl (to fetch linuxdeploy/appimagetool once), and network access.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mkdir -p "${1:-$HERE/dist}" && cd "${1:-$HERE/dist}" && pwd)
BUILD=$HERE/build-appimage
APPDIR=$BUILD/AppDir
TOOLS=${TOOLS_DIR:-$HERE/.appimage-tools}
VERSION=$(sed -n 's/^project(CRTPlayer VERSION \([0-9.]*\).*/\1/p' "$HERE/CMakeLists.txt")
export APPIMAGE_EXTRACT_AND_RUN=1   # tools work without FUSE (containers, CI)
export ARCH=x86_64 VERSION

mkdir -p "$TOOLS"
fetch() { [ -x "$TOOLS/$2" ] || { curl -fsSL -o "$TOOLS/$2" "$1"; chmod +x "$TOOLS/$2"; }; }
fetch https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage linuxdeploy-x86_64.AppImage
fetch https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage linuxdeploy-plugin-qt-x86_64.AppImage
fetch https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage appimagetool-x86_64.AppImage
export PATH="$TOOLS:$PATH"

echo "== building CRT Player $VERSION"
rm -rf "$APPDIR"
cmake -S "$HERE" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCRTPLAYER_BUILD_TESTS=OFF > /dev/null
cmake --build "$BUILD" -j"$(nproc)"
DESTDIR="$APPDIR" cmake --install "$BUILD" > /dev/null
strip "$APPDIR/usr/bin/crtplayer"
# The helper for NVIDIA's AI methods (it needs nothing of the bundle: its C++ runtime is linked in).
[ -f "$APPDIR/usr/bin/crtplayer-nvfx" ] || { echo "ERROR: crtplayer-nvfx was not built" >&2; exit 1; }
strip "$APPDIR/usr/bin/crtplayer-nvfx"
install -Dm644 "$HERE/packaging/io.github.crtplayer.desktop" "$APPDIR/usr/share/applications/io.github.crtplayer.desktop"
install -Dm644 "$HERE/packaging/crtplayer.png" "$APPDIR/usr/share/icons/hicolor/512x512/apps/crtplayer.png"

echo "== deploying Qt (host GStreamer/GLib/Wayland/GL are excluded)"
export QMAKE=${QMAKE:-$(command -v qmake6 || echo /usr/lib/qt6/bin/qmake)}
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so"
export EXTRA_QT_MODULES="waylandclient"
# The EGL client buffer integration is what makes OpenGL work on Wayland; it is not
# pulled in automatically together with the platform plugin.
export EXTRA_QT_PLUGINS="wayland-graphics-integration-client;wayland-shell-integration;wayland-decoration-client"
EXCLUDES=(libgst\* liborc-\* libglib-2.0\* libgobject-2.0\* libgio-2.0\* libgmodule-2.0\* libgthread-2.0\*
          libwayland-\* libdw.so\* libelf.so\* libunwind\* libdrm\* libgbm\* libEGL\* libGL\* libGLX\* libOpenGL\*
          libffi.so\* libpcre2-8.so\*   # GLib's own dependencies
          libssl.so\* libcrypto.so\*    # OpenSSL: the host's, kept current by the system
          libproxy.so\* libpxbackend\*  # host networking stack (glib-networking depends on it);
          libcurl\* libgnutls\*)        # bundling these would put a second GnuTLS in the process
EXARGS=()
for e in "${EXCLUDES[@]}"; do EXARGS+=(--exclude-library "$e"); done
linuxdeploy-x86_64.AppImage --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/crtplayer" \
    --desktop-file "$APPDIR/usr/share/applications/io.github.crtplayer.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/512x512/apps/crtplayer.png" \
    --plugin qt "${EXARGS[@]}" > "$BUILD/linuxdeploy.log" 2>&1 || { tail -40 "$BUILD/linuxdeploy.log"; exit 1; }

# The Wayland EGL client integration (OpenGL on Wayland) is not deployed by the Qt
# plugin, so copy it in and point it at the bundled Qt libraries.
QT_PLUGIN_DIR=$("$QMAKE" -query QT_INSTALL_PLUGINS)
for dir in wayland-graphics-integration-client; do
    mkdir -p "$APPDIR/usr/plugins/$dir"
    for so in "$QT_PLUGIN_DIR/$dir"/*.so; do
        cp "$so" "$APPDIR/usr/plugins/$dir/"
        patchelf --set-rpath '$ORIGIN/../../lib' "$APPDIR/usr/plugins/$dir/$(basename "$so")"
    done
done
# Every Qt library these plugins need must already be in the bundle.
for so in "$APPDIR"/usr/plugins/wayland-graphics-integration-client/*.so; do
    for need in $(readelf -d "$so" | sed -n 's/.*NEEDED.*\[\(libQt6[^]]*\)\]/\1/p'); do
        [ -f "$APPDIR/usr/lib/$need" ] || { echo "ERROR: $(basename "$so") needs $need, not bundled" >&2; exit 1; }
    done
done

# The helper must not depend on the bundle (it runs NVIDIA's libraries, with the system's own beneath them).
patchelf --remove-rpath "$APPDIR/usr/bin/crtplayer-nvfx"
if readelf -d "$APPDIR/usr/bin/crtplayer-nvfx" | grep -E "RUNPATH|RPATH|libstdc\+\+|libQt6" ; then
    echo "ERROR: crtplayer-nvfx is tied to the bundle or to a C++ runtime" >&2; exit 1
fi
# Safety net: nothing from the host-only list may end up in the bundle.
for pat in "${EXCLUDES[@]}"; do
    find "$APPDIR/usr/lib" -maxdepth 1 -name "$pat" -print -delete | sed 's/^/   removed host-only library: /'
done
# Prune orphans: keep only libraries reachable (via NEEDED) from the player and the
# bundled Qt plugins. Excluding a library leaves its private dependencies behind, and
# those would otherwise still be shipped (and demand host libraries of their own).
needed_closure() {
    local -A keep=()
    local queue=("$APPDIR/usr/bin/crtplayer")
    while IFS= read -r -d '' f; do queue+=("$f"); done < <(find "$APPDIR/usr/plugins" -name '*.so' -print0)
    while [ ${#queue[@]} -gt 0 ]; do
        local f=${queue[0]}; queue=("${queue[@]:1}")
        for n in $(readelf -d "$f" 2>/dev/null | sed -n 's/.*NEEDED.*\[\(.*\)\]/\1/p'); do
            if [ -f "$APPDIR/usr/lib/$n" ] && [ -z "${keep[$n]:-}" ]; then keep[$n]=1; queue+=("$APPDIR/usr/lib/$n"); fi
        done
    done
    printf '%s\n' "${!keep[@]}"
}
mapfile -t KEEP < <(needed_closure)
for f in "$APPDIR"/usr/lib/*.so*; do
    b=$(basename "$f")
    printf '%s\n' "${KEEP[@]}" | grep -qxF "$b" || { echo "   pruned unused library: $b"; rm -f "$f"; }
done
# Anything in usr/lib that links GStreamer or GLib-only plugin code must not be there.
if ls "$APPDIR"/usr/lib/libgst* > /dev/null 2>&1; then echo "ERROR: GStreamer got bundled" >&2; exit 1; fi
if ls "$APPDIR"/usr/lib/libgnutls* "$APPDIR"/usr/lib/libssl* > /dev/null 2>&1; then echo "ERROR: a TLS library got bundled" >&2; exit 1; fi
# Plugins the player cannot work without, on either display server.
for req in platforms/libqxcb.so platforms/libqwayland-egl.so wayland-shell-integration/libxdg-shell.so \
           xcbglintegrations/libqxcb-glx-integration.so tls/libqopensslbackend.so; do
    [ -f "$APPDIR/usr/plugins/$req" ] || { echo "ERROR: missing Qt plugin $req" >&2; exit 1; }
done
ls "$APPDIR"/usr/plugins/wayland-graphics-integration-client/*egl*.so > /dev/null 2>&1 || {
    echo "ERROR: missing Wayland EGL client integration (OpenGL would fail on Wayland)" >&2; exit 1; }

# Fallback libproxy (only used when the system has none; see packaging/appimage/).
mkdir -p "$APPDIR/usr/lib/fallback" "$APPDIR/apprun-hooks"
cc -shared -fPIC -O2 -Wl,--version-script="$HERE/packaging/appimage/libproxy-stub.map" -Wl,-soname,libproxy.so.1 \
   -o "$APPDIR/usr/lib/fallback/libproxy.so.1" "$HERE/packaging/appimage/libproxy-stub.c"
install -m644 "$HERE/packaging/appimage/crtplayer-libproxy.sh" "$APPDIR/apprun-hooks/crtplayer-libproxy.sh"
# A second copy of the binary that cannot see the bundled Qt (no RUNPATH): used when the
# system has a suitable Qt (see packaging/appimage/AppRun).
mkdir -p "$APPDIR/usr/libexec/crtplayer"
cp "$APPDIR/usr/bin/crtplayer" "$APPDIR/usr/libexec/crtplayer/crtplayer"
patchelf --remove-rpath "$APPDIR/usr/libexec/crtplayer/crtplayer"
# linuxdeploy leaves AppRun as a plain link to the binary: use a launcher that runs the hooks.
rm -f "$APPDIR/AppRun"
install -m755 "$HERE/packaging/appimage/AppRun" "$APPDIR/AppRun"
grep -q "apprun-hooks" "$APPDIR/AppRun" || { echo "ERROR: AppRun does not run apprun-hooks" >&2; exit 1; }

ls "$APPDIR/usr/lib" > "$BUILD/bundled-libraries.txt"
find "$APPDIR/usr/plugins" -name '*.so' | sed "s#$APPDIR/usr/plugins/##" | sort > "$BUILD/bundled-qt-plugins.txt"
echo "   bundled libraries: $(wc -l < "$BUILD/bundled-libraries.txt"), Qt plugins: $(wc -l < "$BUILD/bundled-qt-plugins.txt")"

echo "== packing"
OUTFILE="$OUT/CRT_Player-$VERSION-x86_64.AppImage"
appimagetool-x86_64.AppImage --no-appstream "$APPDIR" "$OUTFILE" > "$BUILD/appimagetool.log" 2>&1 || { tail -30 "$BUILD/appimagetool.log"; exit 1; }
chmod +x "$OUTFILE"
cp "$BUILD/bundled-libraries.txt" "$BUILD/bundled-qt-plugins.txt" "$OUT/"
( cd "$OUT" && sha256sum "$(basename "$OUTFILE")" > "$(basename "$OUTFILE").sha256" )
echo "Built: $OUTFILE ($(du -h "$OUTFILE" | cut -f1))"
