#include "PlaybackPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QSignalBlocker>
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
    m_deint = new QCheckBox(tr("Deinterlace interlaced video"));
    m_deint->setToolTip(tr("DVDs and TV recordings are often interlaced: each frame holds two half-pictures taken a moment apart,\n"
                           "which shows as combing on anything that moves. On: such video is deinterlaced; other video is never touched.\n"
                           "Off: the frames are shown as stored."));
    m_deint->setChecked(true);
    connect(m_deint, &QCheckBox::toggled, this, &PlaybackPanel::deinterlaceChanged);
    df->addWidget(m_deint);
    {
        auto* row = new QFormLayout;
        m_videoPath = new QComboBox;
        m_videoPath->addItems({tr("Automatic"), tr("Always"), tr("Never")});
        m_videoPath->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_videoPath->setMinimumContentsLength(10);
        m_videoPath->setToolTip(tr("Without a graphics card (or in a virtual machine without 3D acceleration), drawing is done by the CPU.\n"
                                   "With effects off (B), the fast path has the video converted and scaled on all CPU cores before it is drawn,\n"
                                   "which is what makes HD and 4K play smoothly there.\n"
                                   "Automatic: when no graphics acceleration is found. Always: also with a graphics card. Never: off."));
        connect(m_videoPath, &QComboBox::activated, this, &PlaybackPanel::videoPathChanged);
        row->addRow(tr("CPU fast path"), m_videoPath);
        m_lookDetail = new QComboBox;
        m_lookDetail->addItems({tr("Automatic"), tr("Full size"), tr("Half size (faster)")});
        m_lookDetail->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_lookDetail->setMinimumContentsLength(10);
        m_lookDetail->setToolTip(tr("Without a graphics card, a look is computed pixel by pixel on the CPU. Drawn at half size and enlarged,\n"
                                    "it takes a quarter of the work; fine detail (the shadow mask, thin scanlines) gets coarser.\n"
                                    "Automatic: half size in a large window or fullscreen. With a graphics card this has no effect."));
        connect(m_lookDetail, &QComboBox::activated, this, &PlaybackPanel::lookDetailChanged);
        row->addRow(tr("Look detail"), m_lookDetail);
        df->addLayout(row);
    }
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
    // ---- subtitles
    auto* subBox = new QGroupBox(tr("Subtitles"), this);
    auto* subF = new QFormLayout(subBox);
    subF->setContentsMargins(0, 6, 0, 0);
    m_subsOn = new QCheckBox(tr("Show subtitles (V)"));
    m_subsOn->setToolTip(tr("Off until you turn them on, and then they stay as you set them: from one video to the next,\n"
                            "from channel to channel, and the next time the player starts. The language you last picked\n"
                            "is chosen again when a video has it."));
    connect(m_subsOn, &QCheckBox::toggled, this, &PlaybackPanel::subtitlesWantedChanged);
    subF->addRow(m_subsOn);
    auto combo = [this](const QStringList& items) {
        auto* c = new QComboBox;
        c->addItems(items);
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        c->setMinimumContentsLength(10);
        connect(c, &QComboBox::activated, this, [this] {
            emit subtitleStyleChanged(m_subSize->currentIndex(), m_subColor->currentIndex(), m_subBack->currentIndex(), m_subPos->currentIndex());
        });
        return c;
    };
    m_subSize = combo({tr("Small"), tr("Normal"), tr("Large"), tr("Very large")});
    m_subSize->setCurrentIndex(1);
    m_subColor = combo({tr("White"), tr("Yellow")});
    m_subBack = combo({tr("Outline"), tr("Dark box")});
    m_subPos = combo({tr("Bottom"), tr("Raised"), tr("Top")});
    subF->addRow(tr("Size"), m_subSize);
    subF->addRow(tr("Colour"), m_subColor);
    subF->addRow(tr("Behind the text"), m_subBack);
    subF->addRow(tr("Position"), m_subPos);
    m_subDelay = new QSpinBox;
    m_subDelay->setRange(-60000, 60000);
    m_subDelay->setSingleStep(100);
    m_subDelay->setSuffix(tr(" ms"));
    m_subDelay->setToolTip(tr("Subtitles later (+) or earlier (−) than they are stored. For this video only. Keys: H later, Shift+H earlier."));
    connect(m_subDelay, &QSpinBox::valueChanged, this, &PlaybackPanel::subtitleDelayChanged);
    subF->addRow(tr("Delay"), m_subDelay);
    v->addWidget(subBox);

    auto* soundBox = new QGroupBox(tr("Sound"), this);
    auto* soundV = new QVBoxLayout(soundBox);
    m_night = new QCheckBox(tr("Night mode: even out loud and quiet (D)"));
    m_night->setToolTip(tr("Quiet speech comes up and loud bangs come down, so a film can be followed at low volume."));
    connect(m_night, &QCheckBox::toggled, this, &PlaybackPanel::nightModeChanged);
    soundV->addWidget(m_night);
    {
        auto* row = new QFormLayout;
        m_audioDelay = new QSpinBox;
        m_audioDelay->setRange(-10000, 10000);
        m_audioDelay->setSingleStep(25);
        m_audioDelay->setSuffix(tr(" ms"));
        m_audioDelay->setToolTip(tr("Sound later (+) or earlier (−) than the picture, to fix lips that don't match. It is remembered\n"
                                    "(wireless speakers and TVs add a fixed delay). Keys: Ctrl+= later, Ctrl+− earlier."));
        connect(m_audioDelay, &QSpinBox::valueChanged, this, &PlaybackPanel::audioDelayChanged);
        row->addRow(tr("Sound delay"), m_audioDelay);
        soundV->addLayout(row);
    }
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
    // ---- at the end, and the sleep timer
    auto* endBox = new QGroupBox(tr("When a video ends"), this);
    auto* endF = new QFormLayout(endBox);
    endF->setContentsMargins(0, 6, 0, 0);
    m_autoNext = new QCheckBox(tr("Carry on with the next video in the folder"));
    m_autoNext->setToolTip(tr("When the playlist has nothing more, the next video in the same folder plays (in name order,\n"
                              "Episode 2 before Episode 10). Repeat and shuffle are in the playlist panel."));
    connect(m_autoNext, &QCheckBox::toggled, this, &PlaybackPanel::autoNextChanged);
    endF->addRow(m_autoNext);
    m_sleepCombo = new QComboBox;
    m_sleepCombo->addItem(tr("Off"), 0);
    for (int m : {15, 30, 45, 60, 90, 120}) m_sleepCombo->addItem(tr("%1 minutes").arg(m), m);
    m_sleepCombo->addItem(tr("At the end of this video"), -1);
    m_sleepCombo->setToolTip(tr("Stops playing after this long (the sound fades out first), and lets the screen go to sleep. Key: Shift+Z."));
    connect(m_sleepCombo, &QComboBox::activated, this, [this](int i) { emit sleepTimerChanged(m_sleepCombo->itemData(i).toInt()); });
    m_sleepStatus = new QLabel;
    m_sleepStatus->setObjectName("paramValue");
    endF->addRow(tr("Sleep timer"), m_sleepCombo);
    endF->addRow(QString(), m_sleepStatus);
    v->addWidget(endBox);
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

void PlaybackPanel::setSubtitlesWanted(bool on) { QSignalBlocker b(m_subsOn); m_subsOn->setChecked(on); }
void PlaybackPanel::setSubtitleStyle(int size, int color, int background, int position)
{
    const QSignalBlocker b1(m_subSize), b2(m_subColor), b3(m_subBack), b4(m_subPos);
    m_subSize->setCurrentIndex(size); m_subColor->setCurrentIndex(color); m_subBack->setCurrentIndex(background); m_subPos->setCurrentIndex(position);
}
void PlaybackPanel::setSubtitleDelay(int ms) { QSignalBlocker b(m_subDelay); m_subDelay->setValue(ms); }
void PlaybackPanel::setAudioDelay(int ms) { QSignalBlocker b(m_audioDelay); m_audioDelay->setValue(ms); }
void PlaybackPanel::setNightMode(bool on) { QSignalBlocker b(m_night); m_night->setChecked(on); }
void PlaybackPanel::setDeinterlace(bool on) { QSignalBlocker b(m_deint); m_deint->setChecked(on); }
void PlaybackPanel::setVideoPath(int mode) { QSignalBlocker b(m_videoPath); m_videoPath->setCurrentIndex(mode); }
void PlaybackPanel::setLookDetail(int mode) { QSignalBlocker b(m_lookDetail); m_lookDetail->setCurrentIndex(mode); }
void PlaybackPanel::setAutoNext(bool on) { QSignalBlocker b(m_autoNext); m_autoNext->setChecked(on); }
void PlaybackPanel::setSleepTimer(int minutes, const QString& status)
{
    QSignalBlocker b(m_sleepCombo);
    int idx = m_sleepCombo->findData(minutes);
    if (idx < 0) idx = minutes > 0 ? m_sleepCombo->findData(120) : 0;   // (a custom length shows as the longest)
    m_sleepCombo->setCurrentIndex(idx);
    m_sleepStatus->setText(status);
    m_sleepStatus->setVisible(!status.isEmpty());
}

void PlaybackPanel::setHardwareDecoding(bool on) { QSignalBlocker b(m_hw); m_hw->setChecked(on); }
void PlaybackPanel::setScreenshotFiltered(bool f) { QSignalBlocker b(m_shotType); m_shotType->setCurrentIndex(f ? 0 : 1); }
void PlaybackPanel::setScreenshotDir(const QString& d) { m_dir->setText(d); }
void PlaybackPanel::setDecoderStatus(const QString& t) { m_status->setText(t); }
