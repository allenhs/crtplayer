// Damaged model files: each given file is loaded as it is, then many times over with
// random damage (changed bytes, cut short, runs repeated or removed). The loaders must
// refuse or load each one; build with -fsanitize=address,undefined to catch a bad read.
// Usage: fuzz_models ROUNDS FILE...
#include "render/ModelLibrary.h"
#include "render/ModelRaw.h"
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdio>
#include <random>

int main(int argc, char** argv)
{
    if (argc < 3) { std::printf("usage: fuzz_models ROUNDS FILE...\n"); return 2; }
    const int rounds = std::atoi(argv[1]);
    QTemporaryDir tmp;
    std::mt19937 rng(12345);
    long loaded = 0, refused = 0;
    for (int a = 2; a < argc; ++a) {
        const QString src = QString::fromLocal8Bit(argv[a]);
        QFile f(src);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QByteArray whole = f.read(4 << 20);   // large files: their first 4 MB
        const QString path = tmp.path() + "/model." + QFileInfo(src).suffix();
        for (int r = 0; r < rounds; ++r) {
            QByteArray d = whole;
            const int kind = r == 0 ? -1 : int(rng() % 5);
            if (kind == 0 && !d.isEmpty()) for (int i = 0, n = 1 + int(rng() % 8); i < n; ++i) d[int(rng() % d.size())] = char(rng());
            else if (kind == 1 && !d.isEmpty()) d.truncate(int(rng() % d.size()));
            else if (kind == 2 && d.size() > 8) { const int at = int(rng() % (d.size() - 4)); d.remove(at, 1 + int(rng() % 64)); }
            else if (kind == 3 && d.size() > 8) { const int at = int(rng() % (d.size() - 4)); d.insert(at, d.mid(at, 1 + int(rng() % 64))); }
            else if (kind == 4 && d.size() > 8) { const int at = int(rng() % (d.size() - 4)); const quint32 v = rng() % 3 ? 0xFFFFFFFFu : 0x7FFFFFFFu; d.replace(at, 4, QByteArray(reinterpret_cast<const char*>(&v), 4)); }
            { QFile o(path); o.open(QIODevice::WriteOnly | QIODevice::Truncate); o.write(d); }
            if (path.endsWith(".tga") || path.endsWith(".png") || path.endsWith(".jpg")) {   // a model's picture
                if (!modelPicture(d).isNull()) ++loaded; else ++refused;
                continue;
            }
            QString err;
            const ModelLibrary::Mesh m = ModelLibrary::load(path, &err, ModelLibrary::Up(r % 7));
            if (m.triangles > 0) ++loaded; else ++refused;
        }
    }
    std::printf("%ld loaded, %ld refused, none crashed\n", loaded, refused);
    return 0;
}
