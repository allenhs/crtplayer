#pragma once
#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QMap>
#include <QMutex>
#include <QQueue>
#include <QString>
#include <QWaitCondition>
#include <atomic>
#include <thread>
#include <vector>

// An animated GIF writer (GIF89a, looping). Each frame gets its own 256-colour palette
// (median cut over a 15-bit histogram), Floyd–Steinberg dithering, and LZW compression.
// Frames are encoded on worker threads, in parallel, and written in order.
class GifEncoder {
public:
    ~GifEncoder();
    bool open(const QString& path, int width, int height, bool loop = true);
    // The frame is scaled to the GIF's size if needed. delayCs: how long it shows, in
    // hundredths of a second (GIF's unit).
    void addFrame(const QImage& frame, int delayCs);
    int pendingFrames() const { return m_pending.load(); }
    int framesAdded() const { return m_added; }
    bool close();          // waits for the encoding, writes the end; false on a write error
    void abort();          // stops and deletes the file

    // One frame, encoded (a graphic control extension + image descriptor + palette + data).
    static QByteArray encodeFrame(const QImage& rgb, int delayCs, bool dither = true);
    static QImage fitTo(const QImage& frame, const QSize& size);

private:
    void worker();
    struct Job { int index; QImage image; int delay; };
    QFile m_file;
    int m_w = 0, m_h = 0, m_added = 0, m_nextToWrite = 0;
    std::vector<std::thread> m_threads;
    QMutex m_lock;
    QWaitCondition m_jobReady, m_done;
    QQueue<Job> m_jobs;
    QMap<int, QByteArray> m_encoded;
    std::atomic<int> m_pending{0};
    bool m_stop = false, m_writeError = false;
};
