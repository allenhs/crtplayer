#pragma once
#include "render/Geometry.h"
#include <QWidget>

class QComboBox;
class QSlider;
class QLabel;
class QPushButton;

// Scaling mode, crop and aspect override.
class DisplayPanel : public QWidget {
    Q_OBJECT
public:
    explicit DisplayPanel(QWidget* parent = nullptr);
    void setScaleMode(ScaleMode m);
    void setCrop(const CropFractions& c);
    void setAspectOverride(double a);
    void setSourceInfo(const QString& text);
signals:
    void scaleModeChanged(ScaleMode m);
    void cropChanged(const CropFractions& c);
    void cropToAspectRequested(double aspect);
    void aspectOverrideChanged(double a);
private:
    void emitCrop();
    QComboBox* m_mode;
    QSlider* m_crop[4];
    QLabel* m_cropVal[4];
    QComboBox* m_cropAspect;
    QPushButton* m_resetCrop;
    QComboBox* m_aspect;
    QLabel* m_info;
    bool m_updating = false;
};
