#pragma once
#include "GifEncoder.h"
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>

// Renders a section of the video as an animated GIF, as it is shown: with the CRT look
// (or without), or the whole desk-mode scene.
//
// The section is stepped through frame by frame while paused, and each chosen frame is
// rendered for the GIF's own size (so 4K is real 4K, not a blown-up window grab). That
// makes the frame rate exact on any machine: a slow one just takes longer. Each frame
// shows for as long as the video's own timestamps say, so the speed is the video's.
class GifRecorder : public QObject {
    Q_OBJECT
public:
    struct Host {
        std::function<QSize(const QSize& box)> pictureSize;       // the output size for the current picture
        std::function<QImage(const QSize& size, double clock)> render;   // the newest frame, for `size` (or a multiple of it)
        std::function<qint64()> framePts;       // ns, of the newest decoded frame
        std::function<quint64()> frameSerial;   // counts decoded frames
        std::function<bool()> isSeeking;
        std::function<bool()> isPlaying;
        std::function<double()> effectClock;    // seconds
        std::function<void(qint64)> seekPaused; // pause and go to exactly ns
        std::function<void()> step;             // one frame forward
        std::function<void(bool)> restore;      // done: resume playing if it was
    };
    struct Result {
        bool ok = false;
        QString path, error;
        int frames = 0, width = 0, height = 0;
        qint64 startNs = 0, endNs = 0, bytes = 0;
        double seconds = 0;                     // how long the GIF plays
        double fps = 0;                         // frames per second in the GIF
        double tookSeconds = 0;                 // how long making it took
        bool truncated = false;                 // longer than the limit: its start was kept
    };
    static constexpr qint64 kMaxLengthNs = 30'000'000'000LL;

    explicit GifRecorder(const Host& host, QObject* parent = nullptr);
    ~GifRecorder() override;
    // box: the GIF fits inside it (height 0: by width only).
    bool start(qint64 startNs, qint64 endNs, int fps, const QSize& box, const QString& path);
    void cancel();
    bool isBusy() const { return m_state != State::Idle; }
    bool isRecording() const { return m_state == State::Seeking || m_state == State::Stepping || m_state == State::Frame; }
    // "Title_00-01-02.0_to_00-01-07.0.gif" in dir, not taken yet.
    static QString pathFor(const QString& dir, const QString& title, qint64 startNs, qint64 endNs);

signals:
    void progress(double doneSeconds, double totalSeconds, int frames, qint64 bytesSoFar);
    void encoding(int framesLeft);
    void finished(const GifRecorder::Result& r);

private:
    enum class State { Idle, Seeking, Frame, Stepping, Encoding };
    void tick();
    void takeFrame();
    void stopRecording();
    void fail(const QString& error);
    void finish(bool ok, const QString& error);

    Host m_host;
    State m_state = State::Idle;
    std::unique_ptr<GifEncoder> m_enc;
    QTimer m_timer;
    QElapsedTimer m_wait, m_total;
    Result m_r;
    qint64 m_start = 0, m_end = 0;
    QSize m_box;
    int m_fps = 15;
    bool m_wasPlaying = false;
    double m_clock0 = 0;
    quint64 m_serial = 0;
    qint64 m_lastPts = -1;
    double m_nextTarget = 0;   // ns: the next GIF frame is due at this video time
    QImage m_held;             // the last render, written once the next shows how long it lasted
    qint64 m_heldPts = -1;
    qint64 m_writtenCs = 0;    // centiseconds given out so far (rounding never drifts)
};

Q_DECLARE_METATYPE(GifRecorder::Result)
