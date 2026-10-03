// cut_tool SRC A_MS B_MS [OUT] : runs LosslessCutter from the command line (tests) and
// prints the result as JSON. With B_MS = keyframe, only finds the keyframe at or before A.
#include "edit/LosslessCutter.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

int main(int argc, char** argv)
{
    gst_init(&argc, &argv);
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    if (a.size() < 4) { std::fprintf(stderr, "usage: cut_tool SRC A_MS B_MS|keyframe [OUT]\n"); return 2; }
    LosslessCutter cut;
    const qint64 aNs = a[2].toLongLong() * 1000000;
    if (a[3] == "keyframe") {
        QObject::connect(&cut, &LosslessCutter::keyframeFound, [&](qint64 req, qint64 k) {
            std::printf("%s\n", QJsonDocument(QJsonObject{{"requestedMs", req / 1e6}, {"keyframeMs", k < 0 ? -1.0 : k / 1e6}}).toJson(QJsonDocument::Compact).constData());
            app.exit(k < 0 ? 1 : 0);
        });
        cut.findKeyframe(a[1], aNs);
        return app.exec();
    }
    QObject::connect(&cut, &LosslessCutter::finished, [&](const LosslessCutter::Result& r) {
        QJsonObject o{{"ok", r.ok}, {"error", r.error}, {"output", r.output}, {"startMs", r.startNs / 1e6},
                      {"requestedStartMs", r.requestedStartNs / 1e6}, {"stopMs", r.stopNs / 1e6},
                      {"durationMs", r.durationNs / 1e6}, {"bytes", double(r.bytes)}, {"container", r.container},
                      {"kept", QJsonArray::fromStringList(r.kept)}, {"dropped", QJsonArray::fromStringList(r.dropped)}};
        std::printf("%s\n", QJsonDocument(o).toJson(QJsonDocument::Compact).constData());
        app.exit(r.ok ? 0 : 1);
    });
    cut.start(a[1], aNs, a[3].toLongLong() * 1000000, a.value(4));
    return app.exec();
}
