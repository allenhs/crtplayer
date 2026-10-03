#pragma once
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <gst/gst.h>

// Cuts a section out of a local video file without re-encoding (like Avidemux's "copy"
// mode): the streams are copied packet for packet into a new file.
//
// A copied video can only begin on a keyframe (a frame that decodes on its own), so the
// cut starts at the keyframe at or before A; it ends at B. Every track the container can
// hold is kept (all audio and subtitle tracks); the rest are left out and listed.
//
// The original is never written to: the cut goes to a new name next to it (or to
// ~/Videos when that folder is read-only), written as a hidden ".part" file first and
// renamed only when complete, never over an existing file.
//
// GStreamer: filesrc ! parsebin (demuxer and parsers, no decoders) ! queue ! <muxer> ! filesink.
// parsebin's packets are thrown away until every track has announced its format and a
// flushing KEY_UNIT|SNAP_BEFORE seek to A has come through, so the muxer sees nothing
// before the keyframe. The end is cut here, in decode
// order: video packets pass while decoded at or before B, plus the B-frames shown before
// the last kept frame (so every frame up to B, and every frame it refers to, is kept and
// the picture has no gap); other tracks while their time is at most B.
class LosslessCutter : public QObject {
    Q_OBJECT
public:
    struct Result {
        bool ok = false;
        bool cancelled = false;
        QString error;
        QString output;                 // the new file
        qint64 startNs = -1;            // where the cut really starts (the keyframe)
        qint64 requestedStartNs = -1, stopNs = -1;
        qint64 durationNs = -1;         // of the new file
        qint64 bytes = 0;
        QString container;              // "Matroska", "MP4"...
        QStringList kept, dropped;      // tracks, e.g. "video (H.264)", "audio (AAC)"
    };

    explicit LosslessCutter(QObject* parent = nullptr);
    ~LosslessCutter() override;

    // The container and muxer used for a source file: its own when GStreamer can write it,
    // otherwise Matroska. ext receives the output extension.
    static QString muxerFor(const QString& sourcePath, QString* ext, QString* containerName);
    // "Movie - cut 00-01-02 to 00-01-45.mkv" beside the source (or in ~/Videos), not taken yet.
    static QString outputPathFor(const QString& sourcePath, qint64 aNs, qint64 bNs, const QString& ext);

    // Finds the keyframe the cut would start from, at or before aNs (a quick seek on the
    // file), or with after=true the first one after it. -> keyframeFound
    void findKeyframe(const QString& sourcePath, qint64 aNs, bool after = false);
    // Starts the cut. -> progress(0..1), finished(Result). outPath empty = outputPathFor().
    bool start(const QString& sourcePath, qint64 aNs, qint64 bNs, const QString& outPath = QString());
    void cancel();
    bool isRunning() const { return m_pipe != nullptr; }

signals:
    void keyframeFound(qint64 requestedNs, qint64 keyframeNs);   // -1: not found
    void progress(double fraction);
    void finished(const LosslessCutter::Result& result);

private:
    struct Stream { GstPad* pad = nullptr; gulong probe = 0; QString kind, codec; bool video = false, kept = false, done = false, flushed = false, markup = false; qint64 maxPts = -1;
                    GstSegment segment{}; QByteArray mediaType; int dbg = 0; };
    static void onPadAdded(GstElement*, GstPad* pad, gpointer self);
    static GstPadProbeReturn onData(GstPad*, GstPadProbeInfo* info, gpointer self);
    void endStream(Stream& s);   // EOS into its queue (streaming thread, lock held)
    static gboolean onBus(GstBus*, GstMessage* m, gpointer self);
    void seekAndLink();
    void startProbe();
    void startCut();
    void finish(bool ok, const QString& error);
    void teardown();

    // probe mode: find the keyframe only (fakesinks, no muxer)
    bool m_probeOnly = false;
    GstElement* m_pipe = nullptr;
    GstElement* m_parse = nullptr;
    GstElement* m_mux = nullptr;
    guint m_busWatch = 0;
    QMutex m_lock;
    QVector<Stream> m_streams;
    bool m_linked = false;
    bool m_armed = false, m_hasVideo = false;
    QTimer m_capsWait;
    QElapsedTimer m_capsClock, m_lastPadAdded;
    qint64 m_lastPts = -1;
    qint64 m_kRaw = -1;            // the keyframe's own timestamp (from the probe pass)
    bool m_cutAfterProbe = false;
    bool m_snapAfter = false;
    QString m_muxName;
    QString m_source, m_final, m_part;
    qint64 m_a = 0, m_b = 0;
    qint64 m_firstVideoPts = -1;
    QTimer m_progressTimer, m_watchdog;
    Result m_result;
};

Q_DECLARE_METATYPE(LosslessCutter::Result)
