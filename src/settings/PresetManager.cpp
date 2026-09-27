#include "PresetManager.h"

#include <QDir>
#include <QObject>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

PresetManager::PresetManager()
{
    m_dir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("presets");
    QDir().mkpath(m_dir);
    reload();
}

void PresetManager::reload()
{
    m_presets = builtinPresets();
    QDir d(m_dir);
    QVector<CrtPreset> user;
    for (const QFileInfo& fi : d.entryInfoList({"*.json"}, QDir::Files, QDir::Name)) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        CrtPreset p;
        if (!doc.isObject() || !presetFromJson(doc.object(), &p.name, &p.params, nullptr)) continue;
        if (find(p.name)) continue;   // never shadow a built-in or duplicate
        p.filePath = fi.absoluteFilePath();
        m_presets.push_back(p);
    }
}

QStringList PresetManager::builtinNames() const
{
    QStringList n;
    for (const auto& p : m_presets) if (p.builtin) n << p.name;
    return n;
}

QStringList PresetManager::userNames() const
{
    QStringList n;
    for (const auto& p : m_presets) if (!p.builtin) n << p.name;
    n.sort(Qt::CaseInsensitive);
    return n;
}

const CrtPreset* PresetManager::find(const QString& name) const
{
    for (const auto& p : m_presets)
        if (p.name.compare(name, Qt::CaseInsensitive) == 0) return &p;
    return nullptr;
}

QString PresetManager::uniqueName(const QString& base) const
{
    if (!exists(base)) return base;
    for (int i = 2;; ++i) {
        const QString n = QStringLiteral("%1 (%2)").arg(base).arg(i);
        if (!exists(n)) return n;
    }
}

QString PresetManager::fileFor(const QString& name) const
{
    QString safe = name;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9 _.-]")), QStringLiteral("_"));
    safe = safe.trimmed();
    if (safe.isEmpty()) safe = QStringLiteral("preset");
    QString path = QDir(m_dir).filePath(safe + ".json");
    for (int i = 2; QFile::exists(path); ++i) {
        // Reuse the file if it already holds this preset.
        if (const CrtPreset* p = find(name); p && p->filePath == path) break;
        path = QDir(m_dir).filePath(QStringLiteral("%1-%2.json").arg(safe).arg(i));
    }
    return path;
}

static bool writeJson(const QString& path, const QJsonObject& o, QString* error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

bool PresetManager::saveUser(const QString& nameIn, const CrtParams& p, QString* error)
{
    const QString name = nameIn.trimmed();
    if (name.isEmpty()) { if (error) *error = QObject::tr("Enter a preset name."); return false; }
    const CrtPreset* existing = find(name);
    if (existing && existing->builtin) {
        if (error) *error = QObject::tr("\"%1\" is a built-in preset. Choose another name.").arg(existing->name);
        return false;
    }
    const QString path = existing ? existing->filePath : fileFor(name);
    if (!writeJson(path, presetToJson(existing ? existing->name : name, p), error)) return false;
    reload();
    return true;
}

bool PresetManager::rename(const QString& oldName, const QString& newNameIn, QString* error)
{
    const QString newName = newNameIn.trimmed();
    const CrtPreset* p = find(oldName);
    if (!p || p->builtin) { if (error) *error = QObject::tr("Only user presets can be renamed."); return false; }
    if (newName.isEmpty()) { if (error) *error = QObject::tr("Enter a preset name."); return false; }
    if (const CrtPreset* o = find(newName); o && o != p) {
        if (error) *error = QObject::tr("A preset named \"%1\" already exists.").arg(o->name);
        return false;
    }
    const CrtParams params = p->params;
    const QString oldPath = p->filePath;
    const QString newPath = fileFor(newName);
    if (!writeJson(newPath, presetToJson(newName, params), error)) return false;
    if (newPath != oldPath) QFile::remove(oldPath);
    reload();
    return true;
}

bool PresetManager::remove(const QString& name, QString* error)
{
    const CrtPreset* p = find(name);
    if (!p || p->builtin) { if (error) *error = QObject::tr("Built-in presets cannot be deleted."); return false; }
    if (!QFile::remove(p->filePath)) { if (error) *error = QObject::tr("Could not delete %1").arg(p->filePath); return false; }
    reload();
    return true;
}

bool PresetManager::importFile(const QString& path, QString* importedName, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = f.errorString(); return false; }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject()) { if (error) *error = QObject::tr("Not valid JSON: %1").arg(pe.errorString()); return false; }
    QString name;
    CrtParams params;
    if (!presetFromJson(doc.object(), &name, &params, error)) return false;
    name = uniqueName(name);
    if (!writeJson(fileFor(name), presetToJson(name, params), error)) return false;
    reload();
    if (importedName) *importedName = name;
    return true;
}

bool PresetManager::exportFile(const QString& name, const CrtParams& p, const QString& path, QString* error) const
{
    return writeJson(path, presetToJson(name, p), error);
}
