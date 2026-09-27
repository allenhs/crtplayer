#pragma once
#include "settings/CrtParams.h"
#include <QString>
#include <QVector>

// Built-in presets are read-only. User presets are JSON files in
// ~/.local/share/CRTPlayer/presets/ (one file per preset).
class PresetManager {
public:
    PresetManager();
    void reload();
    QString directory() const { return m_dir; }
    const QVector<CrtPreset>& presets() const { return m_presets; }
    QStringList builtinNames() const;
    QStringList userNames() const;
    const CrtPreset* find(const QString& name) const;
    bool exists(const QString& name) const { return find(name) != nullptr; }
    QString uniqueName(const QString& base) const;

    bool saveUser(const QString& name, const CrtParams& p, QString* error);   // creates or overwrites a user preset
    bool rename(const QString& oldName, const QString& newName, QString* error);
    bool remove(const QString& name, QString* error);
    bool importFile(const QString& path, QString* importedName, QString* error);
    bool exportFile(const QString& name, const CrtParams& p, const QString& path, QString* error) const;

private:
    QString fileFor(const QString& name) const;
    QString m_dir;
    QVector<CrtPreset> m_presets;
};
