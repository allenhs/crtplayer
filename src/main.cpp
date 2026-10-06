#include "app/Automation.h"
#include "app/MainWindow.h"
#include "app/WinWindow.h"
#include "playback/Distro.h"
#include <QTimer>
#include "playback/Player.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QSurfaceFormat>
#include <QTextStream>
#include <gst/gst.h>
#ifdef _WIN32
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <windows.h>
#endif

#ifdef _WIN32
// The Windows download is a self-contained folder: crtplayer.exe with Qt's and GStreamer's
// libraries and GStreamer's plugin scanner beside it, lib\\gstreamer-1.0 (codec plugins) and
// lib\\gio\\modules (TLS, for https). Point GStreamer there before gst_init, and keep its
// plugin cache apart from any other GStreamer on the PC.
static void useBundledGStreamer()
{
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    const QDir root(QFileInfo(QString::fromWCharArray(buf, int(n))).absolutePath());
    const QString plugins = root.absoluteFilePath(QStringLiteral("lib/gstreamer-1.0"));
    if (!QFileInfo::exists(plugins)) return;   // a development build: use whatever GStreamer is installed
    auto set = [](const char* name, const QString& value) {
        if (qEnvironmentVariableIsEmpty(name)) qputenv(name, QDir::toNativeSeparators(value).toUtf8());
    };
    set("GST_PLUGIN_SYSTEM_PATH_1_0", plugins);
    set("GST_PLUGIN_SCANNER_1_0", root.absoluteFilePath(QStringLiteral("gst-plugin-scanner.exe")));
    set("GIO_MODULE_DIR", root.absoluteFilePath(QStringLiteral("lib/gio/modules")));
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    if (!local.isEmpty()) {
        QDir().mkpath(local + QStringLiteral("/CRTPlayer"));
        set("GST_REGISTRY_1_0", local + QStringLiteral("/CRTPlayer/gstreamer-registry.bin"));
    }
}
#endif

int main(int argc, char** argv)
{
#ifdef _WIN32
    useBundledGStreamer();
#endif
    gst_init(&argc, &argv);

    // Diagnostics that must work without a display (e.g. over SSH or in a TTY).
    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]) == "--print-distro") {   // for tests: what the install hints will say
            QTextStream out(stdout);
            out << Distro::familyName(Distro::family()) << "\n" << Distro::fullCodecCommand() << "\n";
            return 0;
        }
        if (QByteArray(argv[i]) == "--check-gstreamer") {
            const QStringList missing = Player::missingEssentialElements();
            const QStringList hw = Player::availableHardwareDecoders();
            QTextStream out(stdout);
            out << gst_version_string() << "\n";
            out << "Missing essential elements: " << (missing.isEmpty() ? QStringLiteral("none") : missing.join(", ")) << "\n";
            out << "Hardware video decoders: " << (hw.isEmpty() ? QStringLiteral("none registered") : hw.join(", ")) << "\n";
            out << "System: " << Distro::prettyName() << " (" << Distro::familyName(Distro::family()) << ")\n";
            const auto gaps = Player::missingRecommended();
            if (gaps.isEmpty()) out << "Everything for common formats is installed.\n";
            else {
                out << "Will not work until more plugins are installed:\n";
                QStringList pkgs;
                for (const auto& g : gaps) { out << "  - " << g.what << "  [" << g.package << "]\n"; if (!pkgs.contains(g.package)) pkgs << g.package; }
                out << "Install with:\n  " << Distro::installCommand(pkgs) << "\n";
            }
            if (!missing.isEmpty()) out << "Install the core with:\n  " << Distro::installCommand({Distro::package(Distro::Set::Base)}) << "\n";
            return missing.isEmpty() ? 0 : 1;
        }
    }

    // The renderer needs a desktop OpenGL 3.3 core context (works on Mesa and NVIDIA,
    // under both Wayland (EGL) and X11 (GLX/EGL)).
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication::setApplicationName("CRTPlayer");
    QApplication::setOrganizationName("CRTPlayer");
    QApplication::setApplicationDisplayName("CRT Player");
    QApplication::setDesktopFileName("io.github.crtplayer");
    QApplication app(argc, argv);
    QApplication::setApplicationVersion(CRTPLAYER_VERSION);
    Theme::apply(app);
    WinWindow::plainPopups();   // (Windows)

    QCommandLineParser cli;
    cli.setApplicationDescription("Desktop video player with a GPU CRT presentation, using the host's GStreamer codecs.");
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption fsOpt({"f", "fullscreen"}, "Start in fullscreen.");
    QCommandLineOption presetOpt("preset", "Select a CRT preset by name.", "name");
    QCommandLineOption noHwOpt("no-hw", "Disable hardware decoding for this session.");
    QCommandLineOption autoOpt("automation", "Run a verification script (see docs/AUTOMATION.md).", "script");
    QCommandLineOption autoLogOpt("automation-log", "Where the automation JSON log is written.", "file", "automation-log.json");
    QCommandLineOption checkOpt("check-gstreamer", "Print GStreamer capabilities and exit.");
    cli.addOptions({fsOpt, presetOpt, noHwOpt, autoOpt, autoLogOpt, checkOpt});
    cli.addPositionalArgument("files", "Media files or URIs to play.", "[files...]");
    cli.process(app);

    const QStringList missing = Player::missingEssentialElements();
    if (!missing.isEmpty()) {
        QMessageBox box(QMessageBox::Critical, "GStreamer is incomplete",
                        "CRT Player uses the system's GStreamer installation, but these core parts are missing:\n\n" +
                            missing.join('\n') + "\n\nInstall them on " + Distro::prettyName() + " with:\n\n    " +
                            Distro::installCommand({Distro::package(Distro::Set::Base)}) + "\n\nthen start the player again.");
        box.setTextInteractionFlags(Qt::TextSelectableByMouse);
        box.exec();
        return 2;
    }

    MainWindow w;
    if (cli.isSet(noHwOpt)) w.player()->setHardwareDecoding(false);
    if (cli.isSet(presetOpt)) w.selectPreset(cli.value(presetOpt));
    // Steam Game Mode shows one app at a time on the whole screen: start fullscreen there.
    // Fullscreen is requested as the window is first shown (not right after): a window
    // shown normal and then switched can receive the window manager's late "normal" state
    // and drop out of fullscreen again on a slow start.
    if (cli.isSet(fsOpt) || MainWindow::inGamescope()) w.setFullscreen(true);   // shows it, fullscreen
    else w.show();
    if (!cli.positionalArguments().isEmpty()) w.openFiles(cli.positionalArguments(), true);

    // A missing plugin set is not fatal: the player runs and says what won't work, and how to fix it.
    if (!cli.isSet(autoOpt)) QTimer::singleShot(600, &w, [&w] { w.showPluginNotice(); });
    Automation* automation = nullptr;
    if (cli.isSet(autoOpt)) {
        automation = new Automation(&w, cli.value(autoOpt), cli.value(autoLogOpt), &w);
        if (!automation->start()) {
            QTextStream(stderr) << "Cannot read automation script " << cli.value(autoOpt) << "\n";
            return 3;
        }
    }
    const int rc = app.exec();
    return rc;
}
