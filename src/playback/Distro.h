#pragma once
#include <QString>
#include <QStringList>

// Which Linux distribution family we run on, and its GStreamer package names, so that
// every "please install" message gives the right names and the right command.
// Reads /etc/os-release (ID, ID_LIKE): derivatives map to their family (Manjaro,
// EndeavourOS, CachyOS -> Arch; Nobara, Bazzite -> Fedora; Mint, Pop!_OS -> Debian).
// CRTPLAYER_OS_RELEASE=<file> overrides the file (for tests).
namespace Distro {
// Windows: the player carries its own GStreamer, so "install" advice is "reinstall CRT Player".
enum class Family { Arch, Fedora, FedoraAtomic, Debian, Suse, Other, Windows };

Family family();
QString familyName(Family f);
QString prettyName();                    // PRETTY_NAME from os-release

// GStreamer plugin sets as package names for this family.
enum class Set { Base, Good, Bad, Ugly, Libav, PipeWire, Text };   // Text: the pango plugin (subtitle drawing)
QString package(Set s);
QString installCommand(const QStringList& packages);   // e.g. "sudo pacman -S --needed a b"
// The usual full playback set (good, bad, ugly, libav), as one command.
QString fullCodecCommand();
QString codecNote();                      // family-specific remark (e.g. RPM Fusion), may be empty
} // namespace Distro
