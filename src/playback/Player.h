#pragma once
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <atomic>
#include <gst/gst.h>

struct ChapterInfo {
    qint64 startNs = 0;
    QString title;
};

struct TrackInfo {
    int index = 0;
    QString label;
};

// Thin, thread-safe wrapper around GStreamer's playbin.
// Decoding uses whatever plugins are installed on the host (GStreamer registry);
// video frames arrive through an appsink in system memory and are handed to the
// renderer. Audio is played by playbin's own audio sink, which also provides the
// pipeline clock, so appsink(sync=true) presents frames in step with the audio.
class Player : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Loading, Paused, Playing, Error };
    Q_ENUM(State)
    enum class SeekMode { Accurate, Fast };

    explicit Player(QObject* parent = nullptr);
    ~Player() override;

    // Elements the player cannot work without; returns human-readable descriptions of missing ones.
    static QStringList missingEssentialElements();
    // What will not work with this system's GStreamer (non-fatal), e.g. "MP4 / MOV files".
    struct Gap { QString what; QString package; };
    static QList<Gap> missingRecommended();
    QString audioOutput() const { return m_audioOutput; }   // "automatic", a sink name, or "none"
    // Tape and speaker sound (the look's sound settings); applied live.
    void setTapeParams(const struct TapeParams& p);
    bool hasTapeSound() const { return m_tape != nullptr; }
    // Raise hardware video decoders above software ones (or disable them).
    static void applyDecoderPolicy(bool allowHardware);
    static QStringList availableHardwareDecoders();
    // What this computer's GStreamer can open and decode, in Jellyfin's names
    // ("mkv", "h264", "aac"...), for the device profile sent to a Jellyfin server.
    static void localFormats(QStringList* containers, QStringList* videoCodecs, QStringList* audioCodecs);

    bool open(const QString& pathOrUri, bool autoplay = true, qint64 startNs = 0);
    // Extra request headers for http(s) sources (e.g. Jellyfin's Authorization), applied to
    // the next open(). Headers keep credentials out of URLs, logs and window titles.
    void setHttpHeaders(const QList<QPair<QByteArray, QByteArray>>& headers);
    // External subtitle file (path or URI) used by the next open(); empty = none.
    void setExternalSubtitle(const QString& pathOrUri);
    QString externalSubtitle() const { return m_subUri; }
    // Playback speed (0.25 .. 4.0). Voices keep their pitch (scaletempo).
    void setRate(double rate);
    double rate() const { return m_rate; }
    QVector<ChapterInfo> chapters() const { return m_chapters; }
    void close();
    QString currentUri() const { return m_uri; }
    QString currentPath() const;

    void play();
    void pause();
    void togglePause();
    void seek(qint64 posNs, SeekMode mode);
    void seekRelative(qint64 deltaNs);
    void stepFrame(bool forward);
    void seekKeyframe(bool forward);   // to the previous / next keyframe (cut points)

    void setVolume(double cubic01);
    double volume() const { return m_volume; }
    void setMuted(bool m);
    bool isMuted() const { return m_muted; }

    State state() const { return m_state; }
    bool isPlaying() const { return m_state == State::Playing; }
    bool hasMedia() const { return m_pipe != nullptr && m_state != State::Error; }
    qint64 position() const;
    qint64 duration() const { return m_duration; }
    bool isSeeking() const { return m_seekInFlight; }

    QVector<TrackInfo> audioTracks() const { return m_audioTracks; }
    QVector<TrackInfo> subtitleTracks() const { return m_textTracks; }
    int currentAudioTrack() const;
    int currentSubtitleTrack() const;   // -1 = off
    void setAudioTrack(int idx);
    void setSubtitleTrack(int idx);     // -1 = off

    void setHardwareDecoding(bool enabled) { m_hwEnabled = enabled; }
    bool hardwareDecodingEnabled() const { return m_hwEnabled; }
    bool fellBackToSoftware() const { return m_hwRetried; }
    QString videoDecoder() const;
    bool videoDecoderIsHardware() const;
    QString audioDecoder() const;
    QString videoCodec() const { return m_videoCodec; }
    QString audioCodec() const { return m_audioCodec; }
    QString containerFormat() const { return m_container; }
    QString orientationTag() const { return m_orientationTag; }
    QStringList missingPlugins() const { return m_missing; }
    QString clockName() const;
    double frameRate() const;
    qint64 lastFrameStreamTime() const { return m_lastFrameStreamTime; }
    quint64 frameSerial();   // counts decoded frames handed to the display
    bool hasVideoStream() const { return m_nVideo > 0; }

    // Latest decoded frame as a new reference (nullptr if none yet). Several views can
    // read it independently: each keeps the serial it last showed and compares.
    GstSample* latestSample(quint64* serial);
    quint64 sampleSerial() const { return m_serial; }
    // Time (ns) by which a frame is being shown after its scheduled clock time.
    bool frameLateness(GstSample* s, qint64* latenessNs) const;

signals:
    void chaptersChanged();
    void frameReady();
    void stateChanged(Player::State s);
    void durationChanged(qint64 ns);
    void tracksChanged();
    void orientationChanged(const QString& tag);
    void decoderChanged();
    void mediaLoaded();
    void endOfStream();
    void seekFinished();
    void errorOccurred(const QString& title, const QString& details);
    void warningOccurred(const QString& message);

private:
    bool openInternal(const QString& uri, bool autoplay, qint64 startNs, bool isRetry);
    void teardown();
    void setState(State s);
    void handleMessage(GstMessage* m, quint64 generation);
    void refreshTracks();
    void doSeek(qint64 posNs, SeekMode mode);
    QString describeMissing() const;
    void retryInSoftware();

    static GstBusSyncReply busSyncHandler(GstBus*, GstMessage*, gpointer);
    static GstFlowReturn onNewSample(GstElement* sink, gpointer self);
    static GstFlowReturn onNewPreroll(GstElement* sink, gpointer self);
    static void onDeepElementAdded(GstBin*, GstBin*, GstElement*, gpointer self);
    static void onStreamsChanged(GstElement*, gpointer self);
    static void onSourceSetup(GstElement*, GstElement* source, gpointer self);
    QList<QPair<QByteArray, QByteArray>> m_httpHeaders;   // guarded by m_mutex
    QString m_subUri;
    QString m_audioOutput = QStringLiteral("automatic");
    GstElement* m_tape = nullptr;   // the "crttape" element in the audio chain (owned by the pipeline)
    float m_tapeValues[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    double m_rate = 1.0;
    QVector<ChapterInfo> m_chapters;
    void readToc(GstToc* toc);
    void storeSample(GstSample* s);

    GstElement* m_pipe = nullptr;
    GstElement* m_appsink = nullptr;
    quint64 m_generation = 0;
    QString m_uri;
    State m_state = State::Idle;
    bool m_targetPlaying = true;
    bool m_loaded = false;
    qint64 m_startPos = 0;
    qint64 m_duration = -1;

    double m_volume = 0.8;
    bool m_muted = false;

    bool m_hwEnabled = true;
    bool m_hwRetried = false;

    std::atomic<bool> m_seekInFlight{false};
    qint64 m_seekTarget = 0;
    bool m_hasPendingSeek = false;
    qint64 m_pendingSeek = 0;
    SeekMode m_pendingMode = SeekMode::Accurate;
    QTimer m_seekWatchdog;

    mutable QMutex m_mutex;           // protects fields below touched by streaming threads
    GstSample* m_sample = nullptr;
    QString m_videoDecoder, m_audioDecoder;
    bool m_videoDecoderHw = false;
    std::atomic<bool> m_framePending{false};
    std::atomic<quint64> m_serial{0};
    std::atomic<qint64> m_lastFrameStreamTime{-1};
    std::atomic<int> m_fpsN{0}, m_fpsD{1};
    GstCaps* m_lastCaps = nullptr;

    QVector<TrackInfo> m_audioTracks, m_textTracks;
    int m_nVideo = 0;
    QString m_videoCodec, m_audioCodec, m_container, m_orientationTag;
    QStringList m_missing;
};
