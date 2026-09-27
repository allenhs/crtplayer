// Preset manager: save, rename, delete, import, export, and built-in protection.
#include "settings/PresetManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <cstdio>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main(int argc, char** argv)
{
    QTemporaryDir home;
    qputenv("XDG_DATA_HOME", home.filePath("data").toUtf8());
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("CRTPlayerTest");
    QCoreApplication::setOrganizationName("CRTPlayerTest");

    PresetManager pm;
    CHECK(pm.builtinNames().size() >= 4);
    CHECK(pm.exists("Clean Broadcast Monitor") && pm.exists("Consumer Television") && pm.exists("Arcade Display") && pm.exists("Worn VHS TV"));
    QString err;
    CrtParams p = pm.find("Arcade Display")->params;
    p.curvature = 0.21f;
    CHECK(!pm.saveUser("Arcade Display", p, &err));            // built-ins are read-only
    CHECK(pm.saveUser("My Arcade", p, &err));
    CHECK(pm.find("My Arcade") && !pm.find("My Arcade")->builtin && qAbs(pm.find("My Arcade")->params.curvature - 0.21f) < 1e-4);
    CHECK(pm.rename("My Arcade", "Cabinet", &err) && !pm.exists("My Arcade") && pm.exists("Cabinet"));
    CHECK(!pm.rename("Cabinet", "Worn VHS TV", &err));          // name collision
    const QString out = home.filePath("cabinet.json");
    CHECK(pm.exportFile("Cabinet", pm.find("Cabinet")->params, out, &err) && QFile::exists(out));
    QString imported;
    CHECK(pm.importFile(out, &imported, &err) && imported == "Cabinet (2)");
    CHECK(pm.find("Cabinet (2)")->params == pm.find("Cabinet")->params);
    CHECK(!pm.remove("Consumer Television", &err));
    CHECK(pm.remove("Cabinet", &err) && !pm.exists("Cabinet"));
    PresetManager again;                                        // persisted on disk
    CHECK(again.exists("Cabinet (2)") && !again.exists("Cabinet"));
    QFile bad(home.filePath("bad.json"));
    bad.open(QIODevice::WriteOnly); bad.write("{\"hello\": 1}"); bad.close();
    CHECK(!pm.importFile(bad.fileName(), &imported, &err) && !err.isEmpty());
    std::printf(failures ? "%d preset test(s) FAILED\n" : "All preset tests passed\n", failures);
    return failures ? 1 : 0;
}
