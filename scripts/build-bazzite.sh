#!/usr/bin/env bash
# Builds CRT Player natively for Bazzite (or any Fedora Atomic desktop) inside a
# distrobox that matches the host's Fedora release. The resulting binary links
# against exactly the Qt and GStreamer versions of your host and runs OUTSIDE the
# box, so it uses the host's installed GStreamer plugins and codecs.
#
# Usage: scripts/build-bazzite.sh            (from the source directory)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
. /etc/os-release
FEDORA=${VERSION_ID%%.*}
BOX=crtplayer-build-f$FEDORA
IMAGE=registry.fedoraproject.org/fedora-toolbox:$FEDORA

if ! command -v distrobox >/dev/null; then
  echo "distrobox not found (it ships with Bazzite). Install it or use toolbox with the same commands." >&2
  exit 1
fi
if ! distrobox list | grep -q " $BOX "; then
  distrobox create --yes --name "$BOX" --image "$IMAGE"
fi
distrobox enter "$BOX" -- bash -c "
  set -e
  sudo dnf install -y --setopt=install_weak_deps=False \
    cmake gcc-c++ make pkgconf-pkg-config \
    qt6-qtbase-devel gstreamer1-devel gstreamer1-plugins-base-devel
  cmake -S '$HERE' -B '$HERE/build-fedora' -DCMAKE_BUILD_TYPE=Release
  cmake --build '$HERE/build-fedora' -j\$(nproc)
  (cd '$HERE/build-fedora' && ctest --output-on-failure)
"
echo
echo "Built: $HERE/build-fedora/crtplayer"
echo "Run it on the host (not inside the box):  $HERE/build-fedora/crtplayer"
