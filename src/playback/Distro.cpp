#include "Distro.h"

#include <QFile>
#include <QHash>
#include <QSysInfo>
#include <QTextStream>

namespace {
QHash<QString, QString> readOsRelease()
{
    QHash<QString, QString> kv;
    const QString over = qEnvironmentVariable("CRTPLAYER_OS_RELEASE");
    const QStringList files = over.isEmpty() ? QStringList{"/etc/os-release", "/usr/lib/os-release"} : QStringList{over};
    for (const QString& path : files) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QTextStream in(&f);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            const int eq = line.indexOf('=');
            if (eq <= 0 || line.startsWith('#')) continue;
            QString v = line.mid(eq + 1).trimmed();
            if (v.size() >= 2 && (v.startsWith('"') || v.startsWith('\''))) v = v.mid(1, v.size() - 2);
            kv.insert(line.left(eq), v);
        }
        break;
    }
    return kv;
}
} // namespace

namespace Distro {

Family family()
{
    static const Family f = [] {
#ifdef _WIN32
        if (qEnvironmentVariableIsEmpty("CRTPLAYER_OS_RELEASE")) return Family::Windows;
#endif
        const auto kv = readOsRelease();
        const QStringList ids = (kv.value("ID") + ' ' + kv.value("ID_LIKE")).toLower().split(' ', Qt::SkipEmptyParts);
        auto has = [&](std::initializer_list<const char*> names) {
            for (const char* n : names) if (ids.contains(QLatin1String(n))) return true;
            return false;
        };
        if (has({"arch", "archlinux", "manjaro", "endeavouros", "cachyos", "garuda", "artix"})) return Family::Arch;
        if (has({"fedora", "rhel", "centos", "nobara", "bazzite", "ultramarine"})) {
            const bool atomic = QFile::exists("/run/ostree-booted") || kv.value("VARIANT_ID").contains("atomic") ||
                                kv.value("ID").toLower() == "bazzite";
            return atomic ? Family::FedoraAtomic : Family::Fedora;
        }
        if (has({"debian", "ubuntu", "linuxmint", "pop", "elementary", "zorin", "neon"})) return Family::Debian;
        if (has({"suse", "opensuse", "opensuse-tumbleweed", "opensuse-leap"})) return Family::Suse;
        return Family::Other;
    }();
    return f;
}

QString familyName(Family f)
{
    switch (f) {
    case Family::Arch: return QStringLiteral("Arch family");
    case Family::Fedora: return QStringLiteral("Fedora family");
    case Family::FedoraAtomic: return QStringLiteral("Fedora Atomic family (rpm-ostree)");
    case Family::Debian: return QStringLiteral("Debian/Ubuntu family");
    case Family::Suse: return QStringLiteral("openSUSE family");
    case Family::Windows: return QStringLiteral("Windows");
    default: return QStringLiteral("other");
    }
}

QString prettyName()
{
    if (family() == Family::Windows) return QSysInfo::prettyProductName();
    const QString n = readOsRelease().value("PRETTY_NAME");
    return n.isEmpty() ? QStringLiteral("unknown Linux") : n;
}

QString package(Set s)
{
    switch (family()) {
    case Family::Arch:
        switch (s) {
        case Set::Base: return "gst-plugins-base";
        case Set::Good: return "gst-plugins-good";
        case Set::Bad: return "gst-plugins-bad";
        case Set::Ugly: return "gst-plugins-ugly";
        case Set::Libav: return "gst-libav";
        case Set::PipeWire: return "gst-plugin-pipewire";
        case Set::Text: return "gst-plugins-base";
        }
        break;
    case Family::Fedora:
    case Family::FedoraAtomic:
        switch (s) {
        case Set::Base: return "gstreamer1-plugins-base";
        case Set::Good: return "gstreamer1-plugins-good";
        case Set::Bad: return "gstreamer1-plugins-bad-free";
        case Set::Ugly: return "gstreamer1-plugins-ugly-free";
        case Set::Libav: return "gstreamer1-plugin-libav";
        case Set::PipeWire: return "pipewire-gstreamer";
        case Set::Text: return "gstreamer1-plugins-base";
        }
        break;
    case Family::Debian:
        switch (s) {
        case Set::Base: return "gstreamer1.0-plugins-base";
        case Set::Good: return "gstreamer1.0-plugins-good";
        case Set::Bad: return "gstreamer1.0-plugins-bad";
        case Set::Ugly: return "gstreamer1.0-plugins-ugly";
        case Set::Libav: return "gstreamer1.0-libav";
        case Set::PipeWire: return "gstreamer1.0-pipewire";
        case Set::Text: return "gstreamer1.0-x";   // Debian and Ubuntu ship the pango plugin here
        }
        break;
    case Family::Suse:
        switch (s) {
        case Set::Base: return "gstreamer-plugins-base";
        case Set::Good: return "gstreamer-plugins-good";
        case Set::Bad: return "gstreamer-plugins-bad";
        case Set::Ugly: return "gstreamer-plugins-ugly";
        case Set::Libav: return "gstreamer-plugins-libav";
        case Set::PipeWire: return "gstreamer-plugin-pipewire";
        case Set::Text: return "gstreamer-plugins-base";
        }
        break;
    case Family::Other:
        break;
    }
    switch (s) {   // generic descriptions
    case Set::Base: return "GStreamer base plugins";
    case Set::Good: return "GStreamer good plugins";
    case Set::Bad: return "GStreamer bad plugins";
    case Set::Ugly: return "GStreamer ugly plugins";
    case Set::Libav: return "GStreamer libav (FFmpeg) plugin";
    case Set::PipeWire: return "GStreamer PipeWire plugin";
    case Set::Text: return "GStreamer pango plugin";
    }
    return {};
}

QString installCommand(const QStringList& p)
{
    const QString list = p.join(' ');
    switch (family()) {
    case Family::Arch: return "sudo pacman -S --needed " + list;
    case Family::Fedora: return "sudo dnf install " + list;
    case Family::FedoraAtomic: return "rpm-ostree install " + list + "   (then reboot)";
    case Family::Debian: return "sudo apt install " + list;
    case Family::Suse: return "sudo zypper install " + list;
    case Family::Windows: return QStringLiteral("Reinstall CRT Player: its own GStreamer (in the gstreamer folder beside it) is incomplete.");
    default: return "Install with your package manager: " + p.join(", ");
    }
}

QString fullCodecCommand()
{
    return installCommand({package(Set::Good), package(Set::Bad), package(Set::Ugly), package(Set::Libav)});
}

QString codecNote()
{
    switch (family()) {
    case Family::Fedora:
    case Family::FedoraAtomic:
        return QStringLiteral("Fedora's own packages leave out some patented codecs (e.g. H.265). The complete set comes from "
                              "RPM Fusion (gstreamer1-plugins-bad-freeworld, gstreamer1-plugins-ugly). Bazzite and Nobara "
                              "already include it.");
    case Family::Suse:
        return QStringLiteral("For the complete codec set, use the Packman repository's versions of these packages.");
    default:
        return {};
    }
}

} // namespace Distro
