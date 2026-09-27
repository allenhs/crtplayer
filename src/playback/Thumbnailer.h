#pragma once
#include <QImage>
#include <QObject>
#include <QString>
#include <condition_variable>
#include <mutex>
#include <thread>

typedef struct _GstElement GstElement;

// Seek-bar previews: a separate, video-only GStreamer pipeline on a worker thread that
// decodes the frame near a requested time into a small image. Only the latest request
// is served, so fast hovering never queues work; playback is never touched.
class Thumbnailer : public QObject {
    Q_OBJECT
public:
    explicit Thumbnailer(QObject* parent = nullptr);
    ~Thumbnailer() override;
    void request(const QString& uri, qint64 ns);   // any thread; latest wins
    static constexpr int kWidth = 240;
signals:
    void ready(const QString& uri, qint64 ns, const QImage& image);
private:
    void run();
    QImage grab(const QString& uri, qint64 ns);
    void teardown();
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop = false, m_has = false;
    QString m_reqUri;
    qint64 m_reqNs = 0;
    // worker-thread only
    GstElement* m_pipe = nullptr;
    GstElement* m_sink = nullptr;
    QString m_uri;
};
