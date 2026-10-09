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
    void videoPathChanged(int mode);
    void lookDetailChanged(int mode);       // 0 automatic, 1 full size, 2 half size        // 0 automatic, 1 always the fast path, 2 never
    // 2.13: Enhance
    void enhanceUpscaleChanged(bool on);
    void enhanceSharpnessChanged(double v);   // 0 .. 1
    void nvidiaChanged(bool on, int quality, int mode);   // 2.15
    void smoothMotionChanged(bool on);
    void autoNextChanged(bool on);
    void sleepTimerChanged(int minutes);    // 0 off, -1 at the end of this video
    void onlineHeightChanged(int height);   // 2.17: the largest picture asked of a web site; 0 = this screen's
    void ytDlpRequested();                  // "Get yt-dlp" / "Update yt-dlp"
public:
    void setSubtitlesWanted(bool on);
    void setSubtitleStyle(int size, int color, int background, int position);
    void setSubtitleDelay(int ms);
    void setAudioDelay(int ms);
    void setNightMode(bool on);
    void setDeinterlace(bool on);
    void setVideoPath(int mode);
    void setLookDetail(int mode);
    void setEnhance(bool upscale, double sharpness, bool smoothMotion);
    void setEnhanceStatus(bool available, const QString& status);
    void setNvidia(bool on, int quality, int mode);
    void setNvidiaStatus(bool installed, const QString& status);
    void setAutoNext(bool on);
    void setOnlineHeight(int height);
    void setOnlineStatus(const QString& status, const QString& button, bool buttonEnabled);
    QString onlineStatus() const;
    QString onlineButton() const;   // its words; empty while it cannot be pressed
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
    QCheckBox* m_enhUp = nullptr;
    QCheckBox* m_enhMotion = nullptr;
    class QSlider* m_enhSharp = nullptr;
    class QLabel* m_enhStatus = nullptr;
    QCheckBox* m_nvOn = nullptr;
    class QComboBox* m_nvQuality = nullptr;
    class QComboBox* m_nvMode = nullptr;
    class QLabel* m_nvStatus = nullptr;
    class QWidget* m_nvRows = nullptr;
    bool m_enhAvailable = true, m_nvInstalled = false;
    QComboBox* m_videoPath = nullptr;
    QComboBox* m_lookDetail = nullptr;
    QCheckBox* m_autoNext = nullptr;
    QComboBox* m_sleepCombo = nullptr;
    QLabel* m_sleepStatus = nullptr;
    QComboBox* m_onlineHeight = nullptr;
    QLabel* m_onlineStatus = nullptr;
    class QPushButton* m_onlineGet = nullptr;
signals:
private:
    QCheckBox* m_hw;
    QComboBox* m_shotType;
    QLineEdit* m_dir;
    QLabel* m_status;
};
