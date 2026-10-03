#pragma once
#include "GifEncoder.h"
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>

// Records a section of the video as an animated GIF, exactly as it is shown: with the CRT
// look (or without), or the whole desk-mode scene. It plays the section once and grabs
// frames at a steady rate; each frame shows for as long as the video really took to get
// to the next one, so the GIF runs at the true speed even if grabbing falls behind.
class GifRecorder : public QObject {
    Q_OBJECT
public:
    struct Host {
        std::function<QImage()> grab;           // the picture as shown
        std::function<qint64()> position;       // ns
        std::function<bool()> isPlaying;
        std::function<void(qint64)> seekAndPlay;
    };
    struct Result {
        bool ok = false;
        QString path, error;
        int frames = 0, width = 0, height = 0;
        qint64 startNs = 0, endNs = 0, bytes = 0;
        double seconds = 0;
        bool truncated = false;                 // longer than the limit: its start was kept
    };
    static constexpr qint64 kMaxLengthNs = 30'000'000'000LL;

    explicit GifRecorder(const Host& host, QObject* parent = nullptr);
    ~GifRecorder() override;
    bool start(qint64 startNs, qint64 endNs, int fps, int width, const QString& path);
    void cancel();
    bool isBusy() const { return m_state != State::Idle; }
    bool isRecording() const { return m_state == State::Waiting || m_state == State::Recording; }
    // "Title_00-01-02-to-00-01-07.gif" in dir, not taken yet.
    static QString pathFor(const QString& dir, const QString& title, qint64 startNs, qint64 endNs);

signals:
    void progress(double recordedSeconds, double totalSeconds);
    void encoding(int framesLeft);
    void finished(const GifRecorder::Result& r);

private:
    enum class State { Idle, Waiting, Recording, Encoding };
    void tick();
    void stopRecording();
    void finish(bool ok, const QString& error);

    Host m_host;
    State m_state = State::Idle;
    std::unique_ptr<GifEncoder> m_enc;
    QTimer m_timer;
    QElapsedTimer m_clock;
    Result m_r;
    qint64 m_start = 0, m_end = 0, m_lastPos = -1;
    int m_width = 480, m_fps = 15;
    QImage m_held;          // the last grab, written once the next shows how long it lasted
    qint64 m_heldPos = -1;
};

Q_DECLARE_METATYPE(GifRecorder::Result)
