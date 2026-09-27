#include "DisplayPanel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include "WheelGuard.h"

namespace {
struct AspectChoice { const char* label; double value; };
const AspectChoice kAspects[] = {
    {"4:3", 4.0 / 3.0}, {"16:9", 16.0 / 9.0}, {"1.85:1", 1.85}, {"2.39:1", 2.39},
    {"21:9", 21.0 / 9.0}, {"1:1", 1.0}, {"4:5", 0.8}, {"9:16", 9.0 / 16.0},
};
}

DisplayPanel::DisplayPanel(QWidget* parent) : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(14, 10, 14, 14);

    auto* scaleBox = new QGroupBox(tr("Scaling"), this);
    auto* sf = new QFormLayout(scaleBox);
    sf->setContentsMargins(0, 6, 0, 0);
    m_mode = new QComboBox;
    m_mode->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_mode->setMinimumContentsLength(14);
    m_mode->addItem(tr("Original size (1:1, shrinks if larger than the window)"), int(ScaleMode::Original));
    m_mode->addItem(tr("Fit (whole picture, bars if needed)"), int(ScaleMode::Fit));
    m_mode->addItem(tr("Fill (no bars, crops edges)"), int(ScaleMode::Fill));
    m_mode->addItem(tr("Crop (adjustable below)"), int(ScaleMode::Crop));
    sf->addRow(tr("Mode"), m_mode);
    v->addWidget(scaleBox);

    auto* cropBox = new QGroupBox(tr("Crop"), this);
    auto* cf = new QFormLayout(cropBox);
    cf->setContentsMargins(0, 6, 0, 0);
    const char* names[4] = {"Left", "Top", "Right", "Bottom"};
    for (int i = 0; i < 4; ++i) {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        m_crop[i] = new QSlider(Qt::Horizontal);
        m_crop[i]->setRange(0, 450);   // 0..45.0 %
        m_cropVal[i] = new QLabel("0.0%");
        m_cropVal[i]->setObjectName("paramValue");
        h->addWidget(m_crop[i], 1);
        h->addWidget(m_cropVal[i]);
        cf->addRow(tr(names[i]), row);
        connect(m_crop[i], &QSlider::valueChanged, this, [this, i](int val) {
            m_cropVal[i]->setText(QString::number(val / 10.0, 'f', 1) + "%");
            if (!m_updating) emitCrop();
        });
    }
    auto* quick = new QHBoxLayout();
    m_cropAspect = new QComboBox;
    m_cropAspect->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_cropAspect->setMinimumContentsLength(14);
    m_cropAspect->addItem(tr("Crop to aspect…"));
    for (const auto& a : kAspects) m_cropAspect->addItem(a.label, a.value);
    m_resetCrop = new QPushButton(tr("Reset crop"));
    quick->addWidget(m_cropAspect, 1);
    quick->addWidget(m_resetCrop);
    cf->addRow(quick);
    auto* cropNote = new QLabel(tr("Adjusting the crop switches the scaling mode to Crop. The cropped picture is fitted without distortion."));
    cropNote->setWordWrap(true);
    cropNote->setObjectName("paramValue");
    cf->addRow(cropNote);
    v->addWidget(cropBox);

    auto* aspBox = new QGroupBox(tr("Aspect ratio"), this);
    auto* af = new QFormLayout(aspBox);
    af->setContentsMargins(0, 6, 0, 0);
    m_aspect = new QComboBox;
    m_aspect->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_aspect->setMinimumContentsLength(14);
    m_aspect->addItem(tr("From file (recommended)"), 0.0);
    for (const auto& a : kAspects) m_aspect->addItem(tr("Force %1").arg(a.label), a.value);
    m_aspect->setToolTip(tr("Only for files with wrong aspect metadata. Forcing a ratio stretches the picture."));
    af->addRow(tr("Display aspect"), m_aspect);
    m_info = new QLabel;
    m_info->setWordWrap(true);
    m_info->setObjectName("paramValue");
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    af->addRow(m_info);
    v->addWidget(aspBox);
    v->addStretch(1);

    WheelGuard::install(this);
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_updating) emit scaleModeChanged(ScaleMode(m_mode->currentData().toInt()));
    });
    connect(m_cropAspect, &QComboBox::activated, this, [this](int i) {
        if (i > 0) emit cropToAspectRequested(m_cropAspect->itemData(i).toDouble());
        QSignalBlocker b(m_cropAspect);
        m_cropAspect->setCurrentIndex(0);
    });
    connect(m_resetCrop, &QPushButton::clicked, this, [this] { emit cropChanged(CropFractions{}); });
    connect(m_aspect, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_updating) emit aspectOverrideChanged(m_aspect->currentData().toDouble());
    });
}

void DisplayPanel::emitCrop()
{
    CropFractions c;
    c.left = m_crop[0]->value() / 1000.0;
    c.top = m_crop[1]->value() / 1000.0;
    c.right = m_crop[2]->value() / 1000.0;
    c.bottom = m_crop[3]->value() / 1000.0;
    emit cropChanged(c);
}

void DisplayPanel::setScaleMode(ScaleMode m)
{
    m_updating = true;
    m_mode->setCurrentIndex(m_mode->findData(int(m)));
    m_updating = false;
}

void DisplayPanel::setCrop(const CropFractions& c)
{
    m_updating = true;
    const double vals[4] = {c.left, c.top, c.right, c.bottom};
    for (int i = 0; i < 4; ++i) m_crop[i]->setValue(qRound(vals[i] * 1000));
    m_updating = false;
}

void DisplayPanel::setAspectOverride(double a)
{
    m_updating = true;
    int best = 0;
    for (int i = 1; i < m_aspect->count(); ++i)
        if (a > 0 && std::abs(m_aspect->itemData(i).toDouble() - a) < 1e-3) best = i;
    m_aspect->setCurrentIndex(best);
    m_updating = false;
}

void DisplayPanel::setSourceInfo(const QString& text) { m_info->setText(text); }
