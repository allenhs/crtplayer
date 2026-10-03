#pragma once
#include "edit/GifRecorder.h"
#include <QDialog>
#include <functional>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;

// "Save GIF clip": the A–B section (or the next 5 seconds) as an animated GIF, drawn
// as shown at any size up to 4K, with the CRT look or without, or the desk-mode scene.
class GifDialog : public QDialog {
    Q_OBJECT
public:
    struct Settings { int width = 480, fps = 15; bool look = true; };
    struct Host {
        std::function<qint64()> position, duration, loopA, loopB;   // ns
        std::function<QString()> title;                                // for the file name
        std::function<QString()> folder;                               // where GIFs go
        std::function<bool()> deskMode;
        GifRecorder::Host recorder;                                    // grab() honours look()
        std::function<void(bool)> setLook;                             // recording with / without the look
        std::function<void(bool)> recording;                           // started / ended (loop off meanwhile)
        std::function<void(const Settings&)> saveSettings;
    };
    GifDialog(const Host& host, const Settings& s, QWidget* parent = nullptr);

    void refresh();
    bool record(const QString& path = QString());   // with the current options -> finished
    bool isBusy() const { return m_rec.isBusy(); }
    GifRecorder::Result lastResult() const { return m_last; }
    Settings settings() const;
    static QSize boxFor(int width);

signals:
    void finished(const GifRecorder::Result& r);

private:
    void section(qint64* a, qint64* b) const;

    Host m_host;
    GifRecorder m_rec;
    QLabel* m_range;
    QComboBox* m_width;
    QComboBox* m_fps;
    QComboBox* m_look;
    QLabel* m_status;
    QProgressBar* m_progress;
    QPushButton* m_record;
    QPushButton* m_close;
    QPushButton* m_openFolder;
    GifRecorder::Result m_last;
};
