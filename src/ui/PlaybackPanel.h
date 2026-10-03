#pragma once
#include <QWidget>
class QCheckBox;
class QComboBox;
class QLineEdit;
class QLabel;

class PlaybackPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlaybackPanel(QWidget* parent = nullptr);
    void setHardwareDecoding(bool on);
    void setScreenshotFiltered(bool filtered);
    void setScreenshotDir(const QString& dir);
    void setDecoderStatus(const QString& text);
signals:
    void hardwareDecodingChanged(bool on);
    void screenshotFilteredChanged(bool filtered);
    void screenshotDirChanged(const QString& dir);
    void systemReportRequested();
    void keepAwakeChanged(bool on);
    void lookSoundChanged(bool on);
    void noiseVolumeChanged(double v);      // 0 .. 2
    void effectStrengthChanged(double v);   // 0 .. 1
    // 2.11: subtitles, delays, night mode, deinterlacing, what happens at the end, sleep timer
    void subtitlesWantedChanged(bool on);
    void subtitleStyleChanged(int size, int color, int background, int position);
    void subtitleDelayChanged(int ms);
    void audioDelayChanged(int ms);
    void nightModeChanged(bool on);
    void deinterlaceChanged(bool on);
    void autoNextChanged(bool on);
    void sleepTimerChanged(int minutes);    // 0 off, -1 at the end of this video
public:
    void setSubtitlesWanted(bool on);
    void setSubtitleStyle(int size, int color, int background, int position);
    void setSubtitleDelay(int ms);
    void setAudioDelay(int ms);
    void setNightMode(bool on);
    void setDeinterlace(bool on);
    void setAutoNext(bool on);
    void setSleepTimer(int minutes, const QString& status);   // the choice, and what is left ("" when off)
    void setKeepAwake(bool on);
    void setLookSound(bool on);
    void setNoiseVolume(double v);
    void setEffectStrength(double v);
private:
    class QCheckBox* m_keepAwake = nullptr;
    class QCheckBox* m_lookSound = nullptr;
    class QSlider* m_noiseVolume = nullptr;
    class QSlider* m_effectStrength = nullptr;
    class QLabel* m_noiseLabel = nullptr;
    class QLabel* m_strengthLabel = nullptr;
    class QWidget* m_soundLevels = nullptr;
    QCheckBox* m_subsOn = nullptr;
    QComboBox* m_subSize = nullptr;
    QComboBox* m_subColor = nullptr;
    QComboBox* m_subBack = nullptr;
    QComboBox* m_subPos = nullptr;
    class QSpinBox* m_subDelay = nullptr;
    class QSpinBox* m_audioDelay = nullptr;
    QCheckBox* m_night = nullptr;
    QCheckBox* m_deint = nullptr;
    QCheckBox* m_autoNext = nullptr;
    QComboBox* m_sleepCombo = nullptr;
    QLabel* m_sleepStatus = nullptr;
signals:
private:
    QCheckBox* m_hw;
    QComboBox* m_shotType;
    QLineEdit* m_dir;
    QLabel* m_status;
};
