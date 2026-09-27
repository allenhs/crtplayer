#include "ResumeStore.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace { constexpr qint64 kMarginMs = 30000; constexpr int kMaxEntries = 300; }

QString ResumeStore::filePath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("resume.json");
}

ResumeStore::ResumeStore()
{
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QJsonObject e = it.value().toObject();
        m_entries.insert(it.key(), {qint64(e.value("ms").toDouble()), qint64(e.value("used").toDouble())});
    }
}

qint64 ResumeStore::position(const QString& path) const
{
    return m_entries.value(QFileInfo(path).absoluteFilePath()).ms;
}

void ResumeStore::remember(const QString& path, qint64 posMs, qint64 durationMs)
{
    const QString key = QFileInfo(path).absoluteFilePath();
    if (posMs < kMarginMs || (durationMs > 0 && posMs > durationMs - kMarginMs)) { m_entries.remove(key); return; }
    m_entries.insert(key, {posMs, QDateTime::currentMSecsSinceEpoch()});
    if (m_entries.size() > kMaxEntries) {
        auto oldest = std::min_element(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) { return a.used < b.used; });
        m_entries.erase(oldest);
    }
}

void ResumeStore::forget(const QString& path) { m_entries.remove(QFileInfo(path).absoluteFilePath()); }

void ResumeStore::save() const
{
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QJsonObject o;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
        o.insert(it.key(), QJsonObject{{"ms", double(it->ms)}, {"used", double(it->used)}});
    QSaveFile f(filePath());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    f.commit();
}
