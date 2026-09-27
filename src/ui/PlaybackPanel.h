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
public:
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
signals:
private:
    QCheckBox* m_hw;
    QComboBox* m_shotType;
    QLineEdit* m_dir;
    QLabel* m_status;
};
