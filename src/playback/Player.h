#pragma once
#include <QJsonObject>
#include <QMutex>
#include <QHash>
#include <QSet>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <atomic>
#include <deque>
#include <gst/gst.h>
#include "SubtitleFeed.h"
#include "FrameGovernor.h"
#include "WebSource.h"
#include "TapeAudio.h"

struct ChapterInfo {
    qint64 startNs = 0;
    QString title;
};

struct TrackInfo {
    int index = 0;
    QString label;
    QString lang;   // language code as short as it comes ("en", "ja"), lower case; empty when the file doesn't say
};

// How subtitle text is drawn (text subtitles; picture subtitles such as DVD's come as they are).
struct SubtitleStyle {
    int size = 1;         // 0 small, 1 normal, 2 large, 3 very large
    int color = 0;        // 0 white, 1 yellow
    int background = 0;   // 0 outline and shadow, 1 a dark box behind the text
    int position = 0;     // 0 bottom, 1 raised, 2 top
    bool operator==(const SubtitleStyle& o) const { return size == o.size && color == o.color && background == o.background && position == o.position; }
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
    void setTapeParams(const TapeParams& p);
    bool hasTapeSound() const { return m_tape != nullptr; }
    // Raise hardware video decoders above software ones (or disable them).
    static void applyDecoderPolicy(bool allowHardware);
    static QStringList availableHardwareDecoders();
    // What this computer's GStreamer can open and decode, in Jellyfin's names
    // ("mkv", "h264", "aac"...), for the device profile sent to a Jellyfin server.
    static void localFormats(QStringList* containers, QStringList* videoCodecs, QStringList* audioCodecs);

    bool open(const QString& pathOrUri, bool autoplay = true, qint64 startNs = 0);
    // 2.17: a video from a web site, as one stream or as picture and sound from two addresses.
    bool openWeb(const QList<WebStream>& streams, bool autoplay = true, qint64 startNs = 0);
    bool isWeb() const { return m_uri.startsWith(QLatin1String("crtweb://")); }
    QJsonObject webReport() const;
    // Chapters that are not in the file itself (a web video's, from its page).
    void setChapters(const QVector<ChapterInfo>& chapters);
    // The last error came from the element that reads the video (the network), not from decoding it.
    bool lastErrorFromSource() const { return m_sourceError; }
    // A web video, playing, and the network sends nothing: the picture and the clock wait (2.17).
    bool waitingForData() const { return m_waiting; }
    // Extra request headers for http(s) sources (e.g. Jellyfin's Authorization), applied to
    // the next open(). Headers keep credentials out of URLs, logs and window titles.
    void setHttpHeaders(const QList<QPair<QByteArray, QByteArray>>& headers);
    // External subtitle file (path or URI) used by the next open(); empty = none.
    // chosenByViewer: picked by hand for this video (it is then shown whatever the language preference says).
    void setExternalSubtitle(const QString& pathOrUri, bool chosenByViewer = false);
    // ... while a video is open: true when it is in effect without opening the video again
    bool changeExternalSubtitle(const QString& pathOrUri, bool chosenByViewer);
    // 2.16: a video that is not a file on this computer: the lines of one of its own subtitle tracks, as a
    // file (asked for with embeddedSubtitleFileWanted when such a track is shown; until it is given, and if it
    // never is, the track is drawn by the playback library, as before).
    void setEmbeddedSubtitleFile(int textIndex, const QString& fileUri);
    int embeddedTextCount() const { return m_nEmbeddedText; }
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
    // Where to go on from when the video has to be opened again (a web video whose addresses ran out): the place
    // last asked for while the jump there has not landed (it may have failed, being what met the refusal), or the
    // place it is at. (2.18)
    qint64 resumePosition() const { return m_askedPos >= 0 ? m_askedPos : position(); }
    qint64 duration() const { return m_duration; }
    bool isSeeking() const { return m_seekInFlight; }

    QVector<TrackInfo> audioTracks() const { return m_audioTracks; }
    QVector<TrackInfo> subtitleTracks() const { return m_textTracks; }
    int currentAudioTrack() const;
    int currentSubtitleTrack() const;   // -1 = off
    void setAudioTrack(int idx);
    void setSubtitleTrack(int idx);     // -1 = off

    // What the viewer wants from every video, applied as each one opens: subtitles on or
    // off, and which language to pick when there is a choice. (Picking a track by hand
    // in a video holds for that video.)
    void setSubtitlesWanted(bool on);
    bool subtitlesWanted() const { return m_subsWanted; }
    void setPreferredLanguages(const QString& audio, const QString& subtitle);
    // Sound later (+) or earlier (-) than the picture; subtitles later (+) or earlier (-).
    void setAudioDelay(int ms);
    int audioDelay() const { return m_audioDelayMs; }
    void setSubtitleDelay(int ms);
    int subtitleDelay() const { return m_subDelayMs; }
    void setSubtitleStyle(const SubtitleStyle& st);
    SubtitleStyle subtitleStyle() const { return m_subStyle; }
    // Interlaced video (DVDs, TV recordings) is deinterlaced; progressive video is never touched.
    void setDeinterlace(bool on) { m_deinterlace = on; }   // from the next open()
    bool deinterlace() const { return m_deinterlace; }
    bool deinterlacing() const;   // the current video is interlaced and is being deinterlaced

    // Without a graphics card, the picture is cheapest when the video pipeline itself converts
    // it to RGB and scales it to the size it is shown at: that work is spread over all CPU
    // cores (SIMD code, the pipeline's own threads) and off the drawing thread.
    // AsDecoded: frames as decoded (the renderer converts them on the graphics card).
    // Rgb: converted to RGB, at the video's own size. RgbScaled: and scaled to `size` (square pixels).
    enum class Output { AsDecoded, Rgb, RgbScaled };
    void setOutput(Output mode, const QSize& size = QSize());
    Output output() const { return m_output; }
    QSize fastOutput() const { return m_output == Output::RgbScaled ? m_fastSize : QSize(); }
    QJsonObject governorReport() const; // pictures left out before decoding, when the computer cannot keep up (tests)
    void setGovernorEnabled(bool on) { m_governorOn = on; m_governor.setEnabled(on && !qEnvironmentVariableIsSet("CRTPLAYER_GOVERNOR_OFF")); }
    QJsonObject shrinkReport() const;   // the player's own shrinking filter: its factor, size and time a frame (tests)
    // The video as decoded (before any such scaling): size, pixel shape, format name. False until known.
    bool nativeFormat(int* width, int* height, int* parN, int* parD, QString* format = nullptr) const;
    int sinkForeignMemoryEntries() const;
    // The picture is shown this much later than the video's clock says (frame generation shows
    // each frame when the next has come); the sound is held back by as much.
    void setPictureLatencyMs(int ms);
    int pictureLatencyMs() const { return m_pictureLatencyMs; }
    // The frame as decoded that `shown` (a converted or scaled frame) was made from, or null
    // when it is no longer at hand. The caller unrefs it.
    GstSample* nativeSample(GstSample* shown);
    int decoderThreads() const;   // worker threads of the software video decoder (0: unknown / hardware)
    // (for the checks) the delays as the sinks hold them, ms: sound, picture, subtitles; and the sound level going out (dB)
    void appliedOffsets(int* audioSinkMs, int* videoSinkMs, int* textMs) const;
    // 2.16: the subtitle lines the player keeps itself (SubtitleFeed.h), for the report.
    QJsonObject subtitleFeedReport() const;
    float soundLevelDb() const { return crtTapeLevelDb(m_tape); }
    float soundPitchHz() const { return crtTapePitchHz(m_tape); }

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
    void embeddedSubtitleFileWanted(int textIndex);
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
    void waitingForDataChanged(bool waiting);
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
    void applyTrackPreferences();
    // A sound track is only switched while the video is running: switched while paused or
    // while it opens, and then followed by a seek, GStreamer's playbin can lock up for good.
    // Until then the choice waits here (and counts as the current track).
    bool canSwitchAudioNow() const { return m_pipe && m_loaded && m_state == State::Playing && !m_seekInFlight; }
    void switchPendingAudio();
    int m_pendingAudio = -1;
    bool m_prefMuted = false;      // silent until the preferred language is switched in (no burst of the other one)
    void applyOffsets();
    void applySubtitleStyle();
    void applyFastOutput();
    static GstPadProbeReturn onSinkEvent(GstPad*, GstPadProbeInfo* info, gpointer self);
    static GstPadProbeReturn onSinkBuffer(GstPad*, GstPadProbeInfo* info, gpointer self);
    void clearNativeFrames();              // (m_mutex held)
    // The last few frames as decoded, kept while frames are delivered converted or scaled:
    // references to the decoder's buffers, no copies. Guarded by m_mutex.
    struct NativeFrame { GstBuffer* buffer; GstCaps* caps; };
    std::deque<NativeFrame> m_nativeFrames;
    GstCaps* m_natCaps = nullptr;
    std::atomic<bool> m_keepNative{false};
    std::atomic<bool> m_videoDone{false};   // the picture's stream has ended (seen at the video sink)
    QTimer m_endWatch;
    bool m_endReported = false;
    int m_endStill = 0;
    qint64 m_endLastPos = -1;
    int m_pictureLatencyMs = 0;
    gint m_textTrackRead = -1;              // the subtitle track that was selected when the file was last read from (opened, or after a jump)
    GstElement* m_capsFilter = nullptr;    // in the video sink bin (owned by the pipeline)
    GstElement* m_shrink = nullptr;        // the player's own filter, first in the video sink bin (ShrinkFilter.h)
    GstElement* m_videoDec = nullptr;      // the software video decoder (a reference); guarded by m_mutex
    QSize m_fastSize;
    Output m_output = Output::AsDecoded;
    bool m_canScale = false;
    int m_natW = 0, m_natH = 0, m_natParN = 1, m_natParD = 1;   // guarded by m_mutex
    QString m_natFormat;                                        // guarded by m_mutex
    bool m_subsWanted = false;
    QString m_prefAudioLang, m_prefSubLang;
    bool m_audioPicked = false, m_subPicked = false;   // the viewer chose a track in this video
    int m_audioDelayMs = 0, m_subDelayMs = 0;
    SubtitleStyle m_subStyle;
    bool m_deinterlace = true;
    QVector<GstElement*> m_textOverlays;   // the elements drawing subtitle text (references); guarded by m_mutex
    // 2.16: text subtitles come from lines the player keeps itself; drawn by an overlay in the video sink.
    SubtitleFeed m_feed;
    FrameGovernor m_governor;              // leaves pictures out before decoding when the computer cannot keep up
    void watchNetwork();
    void setWaiting(bool waiting);
    GstElement* m_webSource = nullptr;     // the source of the web video that is open (WebSource.h)
    bool m_waiting = false;                // playing, and nothing comes from the network: paused until it does
    bool m_heldByWait = false;             // the pipeline is paused for that (and until it plays again): not "paused" for the viewer
    int m_waits = 0;
    QTimer m_netWatch;
    QElapsedTimer m_wall;
    std::atomic<qint64> m_lastSampleMs{0};
    bool m_twoStreams = false;             // a web video whose picture and sound are two streams, each with its own demuxer
    QString m_webUri;                      // the "crtweb://N" address registered last (WebSource.h)
    bool m_sourceError = false;
    bool m_governorOn = true;
    std::atomic<gint64> m_pictureNs{0};    // how long one picture of the video lasts
    std::atomic<double> m_rateNow{1.0};    // the playback speed, for the streaming threads
    bool m_feedOn = false;                 // this pipeline has the feed's overlay
    QHash<int, QString> m_trackFiles;      // (this video) subtitle tracks whose lines were given as files
    int m_trackFileAsked = -1;
    bool m_trackFileWaiting = false;       // ... asked for, not answered yet
    bool m_playbinText = true;             // playbin's own subtitle path is switched on (for this opening of the video)
    QSet<QString> m_playbinTextFor;        // videos that need it (picture subtitles), found out in this session
    bool m_textReopenPending = false;
    int m_carrySel = -1;                   // the subtitle track showing when the player opened the video again by itself
    bool m_carryPicked = false;
    void reopenWithPlaybinText();
    bool playbinDraws() const;
    int m_lateRefreshes = 0;               // pictures fetched again in a row because a line came after them
    bool m_refreshing = false;
    bool m_refreshWanted = false;          // the picture is to be fetched again with its lines, once the jump under way is over
    int m_subSel = -1;                     // the subtitle track chosen (the file's own, then the subtitle file)
    int m_nEmbeddedText = 0;               // how many of m_textTracks are the file's own
    QTimer m_feedTimer, m_feedRefresh;
    int selectedText() const;
    void selectText(int idx);
    void updateSubtitleFeed();
    void onFeedNotify();
    void refreshSubtitles();
    GstElement* m_subOverlay = nullptr;    // playbin's subtitle overlay bin (a reference); guarded by m_mutex
    bool m_subShown = false;               // subtitles are being shown in this video
    void applySubtitleShown();
    GstElement* m_deintEl = nullptr;       // playbin's deinterlacer (a reference); guarded by m_mutex
    QList<QPair<QByteArray, QByteArray>> m_httpHeaders;   // guarded by m_mutex
    QString m_subUri;
    bool m_subUriChosen = false;
    QString m_audioOutput = QStringLiteral("automatic");
    GstElement* m_tape = nullptr;   // the "crttape" element in the audio chain (owned by the pipeline)
    TapeParams m_tapeParams;
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
    qint64 m_askedPos = -1;          // the place a jump was asked to, until it lands (resumePosition)
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
