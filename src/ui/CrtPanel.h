#pragma once
#include "settings/CrtParams.h"
#include <QHash>
#include <QWidget>

class QComboBox;
class QSlider;
class QLabel;
class QSpinBox;
class QCheckBox;
class QPushButton;

// CRT controls. Built from floatParamDescs() so every parameter has exactly one control.
class CrtPanel : public QWidget {
    Q_OBJECT
public:
    explicit CrtPanel(QWidget* parent = nullptr);
    void setParams(const CrtParams& p);          // does not emit paramsChanged
    CrtParams params() const { return m_params; }
    void setPresetList(const QStringList& names, const QStringList& userNames);
    void setCurrentPreset(const QString& name, bool modified, bool builtin);
    QPushButton* bypassButton() const { return m_bypass; }
    QPushButton* compareButton() const { return m_compare; }

signals:
    void paramsChanged(const CrtParams& p);
    void presetSelected(const QString& name);
    void saveAsRequested();
    void saveRequested();
    void renameRequested();
    void deleteRequested();
    void importRequested();
    void exportRequested();
    void revertRequested();

private:
    void emitChanged();
    struct Row { QSlider* slider; QLabel* value; const ParamDesc* desc; };
    QVector<Row> m_rows;
    CrtParams m_params;
    QComboBox* m_presetCombo;
    QLabel* m_presetState;
    QPushButton *m_save, *m_saveAs, *m_rename, *m_delete, *m_import, *m_export, *m_revert;
    QPushButton *m_bypass, *m_compare;
    QComboBox* m_maskType = nullptr;
    QComboBox* m_scanType = nullptr;
    QComboBox* m_resPreset = nullptr;
    QSpinBox* m_resRows = nullptr;
    QSpinBox* m_resCols = nullptr;
    QComboBox* m_pixFilter = nullptr;
    QComboBox* m_colorDepth = nullptr;
    QComboBox* m_dither = nullptr;
    QComboBox* m_videoStd = nullptr;
    QCheckBox* m_power = nullptr;
    QCheckBox* m_static = nullptr;
    QCheckBox* m_vcrOsd = nullptr;
    void syncResPreset();
    QSpinBox* m_lines = nullptr;
    QCheckBox* m_includeBars = nullptr;
    bool m_updating = false;
};
