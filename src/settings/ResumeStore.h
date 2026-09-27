#pragma once
#include <QHash>
#include <QString>

// Where each local file was left off, in ~/.local/share/CRTPlayer/resume.json.
// Positions in the first or last 30 seconds are not kept (nothing worth resuming), and
// playing a file to its end clears it. The 300 most recently used files are kept.
class ResumeStore {
public:
    ResumeStore();
    qint64 position(const QString& path) const;          // ms, 0 = none
    void remember(const QString& path, qint64 posMs, qint64 durationMs);
    void forget(const QString& path);
    void save() const;
    static QString filePath();
private:
    struct Entry { qint64 ms = 0; qint64 used = 0; };
    QHash<QString, Entry> m_entries;
};
