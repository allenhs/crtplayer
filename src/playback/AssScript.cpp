#include "AssScript.h"

#include <QString>
#include <QStringList>

namespace {
QString decodeText(const QByteArray& data)
{
    const auto* d = reinterpret_cast<const uchar*>(data.constData());
    const qsizetype n = data.size();
    if (n >= 2 && ((d[0] == 0xff && d[1] == 0xfe) || (d[0] == 0xfe && d[1] == 0xff)))
        return QString::fromUtf16(reinterpret_cast<const char16_t*>(data.constData()), n / 2);   // (the mark at its start tells the byte order)
    QByteArray body = (n >= 3 && d[0] == 0xef && d[1] == 0xbb && d[2] == 0xbf) ? data.mid(3) : data;
    const QString utf8 = QString::fromUtf8(body);
    // Not UTF-8 (an old script from Windows): taken letter for letter rather than not at all.
    return utf8.contains(QChar(0xfffd)) ? QString::fromLatin1(body) : utf8;
}

bool assTime(const QString& t, qint64* out)   // H:MM:SS.cc
{
    const QStringList p = t.trimmed().split(':');
    if (p.size() != 3) return false;
    bool a = false, b = false, c = false;
    const int h = p[0].toInt(&a), m = p[1].toInt(&b);
    const double sec = p[2].toDouble(&c);
    if (!a || !b || !c || h < 0 || m < 0 || sec < 0) return false;
    *out = (qint64(h) * 3600 + qint64(m) * 60) * 1000000000LL + qint64(sec * 1000.0 + 0.5) * 1000000LL;
    return true;
}

} // namespace

AssScript parseAssScript(const QByteArray& data)
{
    AssScript out;
    // (a script begins with "[Script Info]"; anything else is left to GStreamer's readers of subtitle files)
    if (!decodeText(data.left(4096)).contains(QStringLiteral("[Script Info]"), Qt::CaseInsensitive)) return out;
    const QString text = decodeText(data);
    const QStringList lines = text.split(QChar('\n'));
    QStringList head;
    QStringList format;          // the fields of a line, as this script names them
    bool inEvents = false, sawEvents = false;
    int order = 0;
    for (QString line : lines) {
        if (line.endsWith(QChar('\r'))) line.chop(1);
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QChar('['))) {
            const QString section = trimmed.toLower();
            if (sawEvents && !section.startsWith(QStringLiteral("[events"))) { inEvents = false; continue; }   // (fonts, pictures: not for here)
            inEvents = section.startsWith(QStringLiteral("[events"));
            if (inEvents) {
                if (sawEvents) continue;
                sawEvents = true;
                head << QStringLiteral("[Events]")
                     << QStringLiteral("Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text");
                continue;
            }
            if (section.startsWith(QStringLiteral("[v4"))) out.ssa = !section.startsWith(QStringLiteral("[v4+"));
            head << line;
            continue;
        }
        if (!sawEvents) { head << line; continue; }
        if (!inEvents) continue;
        if (trimmed.startsWith(QStringLiteral("Format:"), Qt::CaseInsensitive)) {
            format.clear();
            for (const QString& f : trimmed.mid(7).split(QChar(','))) format << f.trimmed().toLower();
            continue;
        }
        if (!trimmed.startsWith(QStringLiteral("Dialogue:"), Qt::CaseInsensitive)) continue;
        if (format.isEmpty())
            format = {QStringLiteral("layer"), QStringLiteral("start"), QStringLiteral("end"), QStringLiteral("style"), QStringLiteral("name"),
                      QStringLiteral("marginl"), QStringLiteral("marginr"), QStringLiteral("marginv"), QStringLiteral("effect"), QStringLiteral("text")};
        // (the text comes last and may hold commas of its own)
        QString rest = line.mid(line.indexOf(QChar(':')) + 1);
        QStringList fields;
        for (int i = 0; i + 1 < format.size(); ++i) {
            const qsizetype comma = rest.indexOf(QChar(','));
            if (comma < 0) break;
            fields << rest.left(comma).trimmed();
            rest = rest.mid(comma + 1);
        }
        if (fields.size() + 1 != format.size()) continue;
        fields << rest;
        auto field = [&](const char* name, const char* fallback) {
            const int i = format.indexOf(QLatin1String(name));
            return i >= 0 ? fields[i] : QString::fromLatin1(fallback);
        };
        AssLine l;
        if (!assTime(field("start", ""), &l.startNs) || !assTime(field("end", ""), &l.endNs) || l.endNs <= l.startNs) continue;
        bool isNumber = false;
        const int layer = field("layer", "0").toInt(&isNumber);   // ("Marked=0" in an SSA script: no layers there)
        const QString chunk = QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9")
                                  .arg(order++).arg(isNumber ? layer : 0).arg(field("style", "Default"), field("name", ""), field("marginl", "0"),
                                                                              field("marginr", "0"), field("marginv", "0"), field("effect", ""), field("text", ""));
        l.chunk = chunk.toUtf8();
        out.lines.push_back(l);
    }
    if (!sawEvents) return out;   // (no styles: the lines are drawn in the default one)
    out.head = head.join(QChar('\n')).toUtf8() + '\n';
    out.ok = true;
    return out;
}
