#include "PlaybackPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <cmath>
#include <QFontMetrics>
#include <QSlider>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include "WheelGuard.h"

void PlaybackPanel::setLookSound(bool on)
{
    QSignalBlocker b(m_lookSound);
    m_lookSound->setChecked(on);
    m_soundLevels->setEnabled(on);
}

void PlaybackPanel::setNoiseVolume(double v)
{
    QSignalBlocker b(m_noiseVolume);
    m_noiseVolume->setValue(int(std::lround(v * 100)));
    m_noiseLabel->setText(QStringLiteral("%1%").arg(m_noiseVolume->value()));
}

void PlaybackPanel::setEffectStrength(double v)
{
    QSignalBlocker b(m_effectStrength);
    m_effectStrength->setValue(int(std::lround(v * 100)));
    m_strengthLabel->setText(QStringLiteral("%1%").arg(m_effectStrength->value()));
}

void PlaybackPanel::setKeepAwake(bool on)
{
    QSignalBlocker b(m_keepAwake);
    m_keepAwake->setChecked(on);
}

PlaybackPanel::PlaybackPanel(QWidget* parent) : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(14, 10, 14, 14);

    auto* decBox = new QGroupBox(tr("Decoding"), this);
    auto* df = new QVBoxLayout(decBox);
    df->setContentsMargins(0, 6, 0, 0);
    m_hw = new QCheckBox(tr("Use hardware decoding when available"));
    m_hw->setToolTip(tr("Prefers VA-API / NVDEC / V4L2 GStreamer decoders installed on this system. "
                        "If a hardware decoder fails, playback restarts in software automatically."));
    df->addWidget(m_hw);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setObjectName("paramValue");
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    df->addWidget(m_status);
    v->addWidget(decBox);

    auto* shotBox = new QGroupBox(tr("Screenshots"), this);
    auto* sf = new QFormLayout(shotBox);
    sf->setContentsMargins(0, 6, 0, 0);
    m_shotType = new QComboBox;
    m_shotType->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_shotType->setMinimumContentsLength(14);
    m_shotType->addItem(tr("Filtered (as shown, with CRT effects)"));
    m_shotType->addItem(tr("Original frame (no effects, native size)"));
    sf->addRow(tr("S key saves"), m_shotType);
    auto* dirRow = new QHBoxLayout();
    m_dir = new QLineEdit;
    auto* browse = new QPushButton(tr("Browse…"));
    dirRow->addWidget(m_dir, 1);
    dirRow->addWidget(browse);
    sf->addRow(tr("Folder"), dirRow);
    v->addWidget(shotBox);
    m_keepAwake = new QCheckBox(tr("Keep the screen awake while playing"));
    m_keepAwake->setToolTip(tr("No dimming, screen lock or automatic sleep while a video plays. Paused or stopped, "
                               "the normal power settings apply again."));
    m_keepAwake->setChecked(true);
    connect(m_keepAwake, &QCheckBox::toggled, this, &PlaybackPanel::keepAwakeChanged);
    v->addWidget(m_keepAwake);
    auto* soundBox = new QGroupBox(tr("Sound"), this);
    auto* soundV = new QVBoxLayout(soundBox);
    m_lookSound = new QCheckBox(tr("The look's sound (tape hiss, TV speaker, film crackle)"));
    m_lookSound->setToolTip(tr("Looks can change the sound as well as the picture: a VHS tape hisses and wobbles, a TV "
                               "speaker is small and boxy, a film print crackles. Adjust it under CRT → Sound."));
    m_lookSound->setChecked(true);
    connect(m_lookSound, &QCheckBox::toggled, this, &PlaybackPanel::lookSoundChanged);
    soundV->addWidget(m_lookSound);
    // How loud the added noise is, and how strongly the sound is changed.
    m_soundLevels = new QWidget(this);
    auto* levelForm = new QFormLayout(m_soundLevels);
    levelForm->setContentsMargins(22, 0, 0, 0);
    auto level = [this, levelForm](const QString& name, const QString& tip, int max, QSlider*& slider, QLabel*& label) {
        auto* row = new QHBoxLayout;
        slider = new QSlider(Qt::Horizontal, m_soundLevels);
        slider->setRange(0, max);
        slider->setValue(100);
        slider->setToolTip(tip);
        label = new QLabel(QStringLiteral("100%"), m_soundLevels);
        label->setMinimumWidth(QFontMetrics(label->font()).horizontalAdvance(QStringLiteral("200%")) + 4);
        row->addWidget(slider, 1);
        row->addWidget(label);
        levelForm->addRow(name, row);
    };
    level(tr("Noise volume"), tr("How loud the tape hiss and film crackle are (0% silent, 200% twice as loud)"), 200, m_noiseVolume, m_noiseLabel);
    level(tr("Effect strength"), tr("How strongly the sound is changed: the TV speaker, treble loss, pitch wobble, saturation and dropouts"),
          100, m_effectStrength, m_strengthLabel);
    connect(m_noiseVolume, &QSlider::valueChanged, this, [this](int x) {
        m_noiseLabel->setText(QStringLiteral("%1%").arg(x));
        emit noiseVolumeChanged(x / 100.0);
    });
    connect(m_effectStrength, &QSlider::valueChanged, this, [this](int x) {
        m_strengthLabel->setText(QStringLiteral("%1%").arg(x));
        emit effectStrengthChanged(x / 100.0);
    });
    connect(m_lookSound, &QCheckBox::toggled, m_soundLevels, &QWidget::setEnabled);
    soundV->addWidget(m_soundLevels);
    v->addWidget(soundBox);
    auto* diag = new QGroupBox(tr("Help with a problem"), this);
    auto* dl = new QVBoxLayout(diag);
    auto* dtext = new QLabel(tr("Copies a summary of this system (graphics, GStreamer, decoders, the current video's "
                                "format) to the clipboard, to paste into a bug report. It contains no file names, "
                                "paths or Jellyfin addresses."));
    dtext->setWordWrap(true);
    dtext->setObjectName("paramValue");
    auto* dbtn = new QPushButton(tr("Copy system report"));
    connect(dbtn, &QPushButton::clicked, this, &PlaybackPanel::systemReportRequested);
    dl->addWidget(dtext);
    dl->addWidget(dbtn);
    v->addWidget(diag);
    v->addStretch(1);

    WheelGuard::install(this);
    connect(m_hw, &QCheckBox::toggled, this, &PlaybackPanel::hardwareDecodingChanged);
    connect(m_shotType, &QComboBox::currentIndexChanged, this, [this](int i) { emit screenshotFilteredChanged(i == 0); });
    connect(m_dir, &QLineEdit::editingFinished, this, [this] { emit screenshotDirChanged(m_dir->text()); });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Screenshot folder"), m_dir->text());
        if (!d.isEmpty()) { m_dir->setText(d); emit screenshotDirChanged(d); }
    });
}

void PlaybackPanel::setHardwareDecoding(bool on) { QSignalBlocker b(m_hw); m_hw->setChecked(on); }
void PlaybackPanel::setScreenshotFiltered(bool f) { QSignalBlocker b(m_shotType); m_shotType->setCurrentIndex(f ? 0 : 1); }
void PlaybackPanel::setScreenshotDir(const QString& d) { m_dir->setText(d); }
void PlaybackPanel::setDecoderStatus(const QString& t) { m_status->setText(t); }
