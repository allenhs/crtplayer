#pragma once
// Subtitle lines, kept by the player itself (2.16).
//
// GStreamer hands a video's subtitle lines over once, as the file is read. After a jump
// that is not enough: a line that began before the place jumped to is not sent again (the
// subtitle stays away until the next line), and a subtitle file beside the video is read
// by a second reader that does not land where the video lands (the wrong line shows).
//
// So the lines are read by the player, with a small pipeline of its own that takes only the
// subtitle stream out of the video (or reads the subtitle file), and are kept with their
// times. In the playing pipeline they come from this store: an overlay in the player's
// video sink draws them into the picture, fed by a source that, after every jump, starts
// again from the lines in force at the place the video landed on. The picture after a jump
// is held back for the instant it takes until its lines have arrived.
//
// Text subtitles only (SubRip and the like, timed text, ASS / SSA with their styles, as
// before). Picture subtitles (DVD, Blu-ray) stay with GStreamer's own path, as before.
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <condition_variable>
#include <deque>
#include <functional>
#include <gst/gst.h>
#include <mutex>
#include <thread>
#include <vector>

class SubtitleFeed {
public:
    enum class Kind {
        None,          // no source
        Reading,       // looking for the subtitle stream
        Text,          // lines are (or will be) coming from here
        Unsupported,   // not text: left to GStreamer's own path
        Failed         // could not be read
    };

    SubtitleFeed();
    ~SubtitleFeed();
    SubtitleFeed(const SubtitleFeed&) = delete;
    SubtitleFeed& operator=(const SubtitleFeed&) = delete;

    // The two pieces for the player's video sink: the overlay (returned; the video goes in at
    // its "video_sink" and comes out at its "src") and the source that feeds it. Both are added
    // to `bin` and linked to each other. Null when GStreamer lacks an element for it.
    GstElement* build(GstBin* bin);
    void unbuild();   // the playing pipeline is going away (before it is destroyed)
    bool built() const { return m_overlay != nullptr; }
    GstElement* overlay() const { return m_overlay; }

    // Where the lines come from: the video itself (its subtitle stream number `textIndex`,
    // counted from 0) or a subtitle file (textIndex -1).
    void setSource(const QString& uri, int textIndex, const QList<QPair<QByteArray, QByteArray>>& httpHeaders = {});
    void clearSource();
    Kind kind() const;
    void setShown(bool shown);         // the overlay draws (the picture is held back after a jump only then)
    void setDelayNs(qint64 delayNs);   // lines later (+) or earlier (-); takes effect with the next jump
    qint64 delayNs() const;

    // Called from any thread when kind() changes, or when a line for the place the video
    // stands at turned up after its picture had already been let through.
    void setNotify(std::function<void()> cb);
    bool takeLateLine();   // (true once per such line)

    quint64 aboutToSeek(qint64 posNs);   // right before the player's flushing seek; returns a number for jumpFailed
    void jumpFailed(quint64 epoch);      // the seek was refused: nothing is going to arrive
    void playedTo(qint64 posNs);         // now and then: the lines are read some minutes ahead of this

    // An error message of the playing pipeline: true when it comes from the source of the lines (it is
    // noted here, and the source is started again where the picture is).
    bool sourceFailed(GstObject* from);

    QJsonObject report() const;

private:
    struct Cue {
        GstClockTime pts = 0, end = 0;
        GstBuffer* buf = nullptr;
        quint64 pushedEpoch = 0;
    };
    struct Span { GstClockTime a = 0, b = 0; };   // every line that begins in [a, b) is known
    enum class Cmd { Start, Stop, ReaderSeek, Align, Quit };
    struct Command { Cmd cmd; GstClockTime pos = 0; double rate = 1.0; quint64 token = 0; };

    void post(const Command& c);
    void workerLoop();
    void watchLoop();
    void startReader(quint64 token, bool fromStart = false);
    bool loadScriptFile(const QString& uri, quint64 token);
    void stopReader();
    void notify();
    // (all of these with m_mutex held)
    void pump();
    void giveCaps();
    void maybeRelease();
    void releaseHold(bool timedOut);
    int activeAt(GstClockTime pos) const;   // 1 a line is in force, 0 none is, -1 not known yet
    const Span* feedSpan(GstClockTime pos) const;
    void addCue(GstBuffer* buf);
    void progress(GstClockTime pts);
    void clearCues();

    static void onSourceSetup(GstElement* bin, GstElement* source, gpointer self);
    static void onSourcePad(GstElement* bin, GstPad* pad, gpointer typefind);
    static void onTypeFound(GstElement* typefind, guint probability, GstCaps* caps, gpointer self);
    static void onReaderPad(GstElement* demux, GstPad* pad, gpointer self);
    static GstFlowReturn onCueSample(GstElement* sink, gpointer self);
    static GstPadProbeReturn onClockBuffer(GstPad* pad, GstPadProbeInfo* info, gpointer self);
    static GstBusSyncReply onReaderMessage(GstBus* bus, GstMessage* msg, gpointer self);
    static void onNeedData(GstElement* src, guint length, gpointer self);
    static gboolean onSeekData(GstElement* src, guint64 offset, gpointer self);
    static GstPadProbeReturn onHeldPicture(GstPad* pad, GstPadProbeInfo* info, gpointer self);
    static GstPadProbeReturn onVideoEvent(GstPad* pad, GstPadProbeInfo* info, gpointer self);
    static GstPadProbeReturn onTextBuffer(GstPad* pad, GstPadProbeInfo* info, gpointer self);
    static GstPadProbeReturn onSourceUpstreamEvent(GstPad* pad, GstPadProbeInfo* info, gpointer self);
    static void onOverlayElement(GstBin* bin, GstBin* sub, GstElement* el, gpointer self);

    mutable std::mutex m_mutex;
    std::condition_variable m_cond;

    // ---- in the playing pipeline
    GstElement* m_overlay = nullptr;     // (references)
    GstElement* m_appsrc = nullptr;
    GstPad* m_videoPad = nullptr;        // the overlay's video input
    GstPad* m_textPad = nullptr;         // the overlay's subtitle input
    gulong m_holdProbe = 0;
    bool m_shown = false;
    bool m_textFlowed = false;           // a line has reached the overlay since it was built
    quint64 m_feedEpoch = 1;             // counts the times the source started anew
    GstClockTime m_feedPos = 0;          // where it started from
    GstClockTime m_lastPushed = 0;
    bool m_pushedAny = false;
    quint64 m_holdEpoch = 0;
    bool m_holdSeekData = false, m_aligned = false;
    bool m_holdGaveUp = false;           // the picture after the last jump was let through without its lines
    gint64 m_holdArrivedUs = 0;          // when that picture arrived (0: not yet)
    int m_holdNeeds = 0, m_holdTextSeen = 0;
    GstClockTime m_seekPos = 0;          // where the player last asked to go
    GstClockTime m_playPos = 0;          // where the picture is, as last heard
    // A subtitle file counts from the start of the video; the video's own times may begin elsewhere.
    gint64 timeOffset() const { return m_delay + (m_textIndex < 0 ? m_shift : 0); }
    gint64 m_delay = 0;
    gint64 m_shift = 0;                  // a subtitle file's times are the video's times from its start; this is where the video's own begin
    bool m_late = false;
    quint64 m_lateEpoch = 0;
    std::function<void()> m_notify;

    // ---- the lines
    Kind m_kind = Kind::None;
    QString m_uri;
    int m_textIndex = -1;
    QList<QPair<QByteArray, QByteArray>> m_headers;
    std::vector<Cue> m_cues;             // by time
    std::vector<Span> m_spans;
    GstCaps* m_caps = nullptr;           // what the lines are (as the reader delivers them)
    bool m_capsOnSource = false;
    GstCaps* m_capsSet = nullptr;        // what the overlay was last told is coming
    quint64 m_sourceToken = 0;           // counts setSource / clearSource

    // ---- the reader
    std::thread m_worker;
    std::thread m_watch;                 // lets a waiting picture go after a while
    std::condition_variable m_watchCond;
    bool m_watchQuit = false;
    std::deque<Command> m_commands;
    GstElement* m_reader = nullptr;      // (worker thread only, but for stopping)
    quint64 m_readerToken = 0;           // the source the reader was started for
    quint64 m_readerEpoch = 0;           // counts its jumps
    bool m_readerStop = false, m_readerEos = false, m_spanOpen = false, m_readerFromStart = true;
    GstClockTime m_spanA = 0, m_spanB = 0, m_lastPumpB = 0;
    GstClockTime m_wantUntil = 0;        // it reads up to here and then waits
    GstClockTime m_pendingReaderSeek = GST_CLOCK_TIME_NONE;
    int m_subPads = 0;
    bool m_padChosen = false, m_readerHasClock = false, m_clockIsVideo = false;
    GstPad* m_clockPad = nullptr;        // (not a reference: compared only)

    // ---- for the report
    quint64 m_nHeld = 0, m_nHolds = 0, m_nHoldTimeouts = 0, m_nAligns = 0, m_nPushed = 0, m_nReaderSeeks = 0, m_nLate = 0, m_nSourceErrors = 0;
    int m_sourceErrorsNow = 0;           // ... since the pieces were built
    double m_holdMsSum = 0, m_holdMsMax = 0;
    QString m_capsName, m_error;
};
