#!/usr/bin/env python3
"""Install hints per distribution: the family detected from os-release and the exact
command shown to the user. Usage: check-portability.py BINARY"""
import os, subprocess, sys, pathlib
binary = sys.argv[1]
here = pathlib.Path(__file__).resolve().parent.parent / 'tests' / 'os-release'
expect = {
    'arch': ('Arch family', 'sudo pacman -S --needed gst-plugins-good gst-plugins-bad gst-plugins-ugly gst-libav'),
    'manjaro': ('Arch family', 'sudo pacman -S --needed gst-plugins-good'),
    'cachyos': ('Arch family', 'sudo pacman -S --needed gst-plugins-good'),
    'fedora': ('Fedora family', 'sudo dnf install gstreamer1-plugins-good gstreamer1-plugins-bad-free'),
    'bazzite': ('Fedora Atomic family (rpm-ostree)', 'rpm-ostree install gstreamer1-plugins-good'),
    'ubuntu': ('Debian/Ubuntu family', 'sudo apt install gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav'),
    'mint': ('Debian/Ubuntu family', 'sudo apt install gstreamer1.0-plugins-good'),
    'opensuse': ('openSUSE family', 'sudo zypper install gstreamer-plugins-good'),
    'unknown': ('other', 'Install with your package manager: GStreamer good plugins'),
}
fails = 0
for name, (family, cmd) in expect.items():
    env = dict(os.environ, CRTPLAYER_OS_RELEASE=str(here / name))
    out = subprocess.run([binary, '--print-distro'], env=env, capture_output=True, text=True, timeout=30).stdout.splitlines()
    ok = len(out) >= 2 and out[0] == family and out[1].startswith(cmd)
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {out[0] if out else '?'} -> {out[1] if len(out) > 1 else '?'}")
print(f"\n{fails} portability check(s) failed" if fails else "\nAll portability checks passed")
sys.exit(1 if fails else 0)
