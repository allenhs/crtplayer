#include "SubtitleFeed.h"

#include "AssScript.h"

#include <QFile>
#include <QJsonArray>
#include <QStringList>
#include <QUrl>
#include <algorithm>
#include <cstring>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

namespace {
const GstClockTime kLookBack = 60 * GST_SECOND;    // a line in force at a place began at most this long before it (longer ones are missed after a jump)
const GstClockTime kLead = 180 * GST_SECOND;       // the lines are read this far ahead of the picture
const GstClockTime kMargin = 3 * GST_SECOND;       // lines are stored in a file up to about this much later than the picture of their time
const GstClockTime kEarly = 20 * GST_SECOND;       // ... or this much earlier (files not written by mkvmerge; seen: 15 s)
const GstClockTime kSoon = 20 * GST_SECOND;        // the reader gets this far by reading on faster than by jumping
const GstClockTime kDefaultLength = 5 * GST_SECOND;   // for a line that does not say how long it stays
const GstClockTime kForever = GST_CLOCK_TIME_NONE;
const gint64 kHoldAtMostUs = 250000;               // the picture after a jump waits for its lines this long at most

// CRTPLAYER_SUBTITLE_TRACE=1: what happens here and when, on the error output.
bool tracing()
{
    static const bool on = g_getenv("CRTPLAYER_SUBTITLE_TRACE") != nullptr;
    return on;
}
#define TRACE(...) do { if (tracing()) { g_printerr("[subs %8.3f] ", g_get_monotonic_time() / 1e6 - traceStart()); g_printerr(__VA_ARGS__); g_printerr("\n"); } } while (0)
double traceStart()
{
    static const double t0 = g_get_monotonic_time() / 1e6;
    return t0;
}
#define SEC(t) (double(t) / GST_SECOND)

bool isSubtitleCaps(const gchar* name)
{
    return g_str_has_prefix(name, "text/") || g_str_has_prefix(name, "subpicture/") || g_str_has_prefix(name, "application/x-subtitle") ||
           g_str_has_prefix(name, "closedcaption/") || !g_strcmp0(name, "application/x-ass") || !g_strcmp0(name, "application/x-ssa") ||
           !g_strcmp0(name, "application/x-teletext");
}

GstCaps* padCaps(GstPad* pad)
{
    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps) caps = gst_pad_query_caps(pad, nullptr);
    return caps;
}

// Links a pad to something that swallows what comes (so that the demuxer never finds all its outputs unconnected).
void swallow(GstElement* pipeline, GstPad* pad)
{
    GstElement* sink = gst_element_factory_make("fakesink", nullptr);
    if (!sink) return;
    g_object_set(sink, "sync", FALSE, "async", FALSE, nullptr);
    gst_bin_add(GST_BIN(pipeline), sink);
    gst_element_sync_state_with_parent(sink);
    if (GstPad* sp = gst_element_get_static_pad(sink, "sink")) {
        gst_pad_link(pad, sp);
        gst_object_unref(sp);
    }
}
} // namespace

SubtitleFeed::SubtitleFeed()
{
    m_worker = std::thread([this] { workerLoop(); });
    m_watch = std::thread([this] { watchLoop(); });
}

SubtitleFeed::~SubtitleFeed()
{
    unbuild();
    post({Cmd::Quit});
    if (m_worker.joinable()) m_worker.join();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_watchQuit = true;
    }
    m_watchCond.notify_all();
    if (m_watch.joinable()) m_watch.join();
    std::lock_guard<std::mutex> lock(m_mutex);
    clearCues();
}

void SubtitleFeed::setNotify(std::function<void()> cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_notify = std::move(cb);
}

void SubtitleFeed::notify()
{
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb = m_notify;
    }
    if (cb) cb();
}

SubtitleFeed::Kind SubtitleFeed::kind() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_kind;
}

qint64 SubtitleFeed::delayNs() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_delay;
}

void SubtitleFeed::setDelayNs(qint64 d)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_delay = d;
}

// The overlay is told to keep quiet only once it has built what draws: before that, "silent" makes it pass
// the picture along with its subtitle input unconnected, and a line arriving then would end the source
// with an error. (Until lines have flowed there is nothing to keep quiet about.)
void SubtitleFeed::setShown(bool shown)
{
    GstElement* overlay = nullptr;
    bool silent = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shown = shown;
        if (!shown) releaseHold(false);
        if (m_overlay) overlay = GST_ELEMENT(gst_object_ref(m_overlay));
        silent = !shown && m_textFlowed;
    }
    if (!overlay) return;
    g_object_set(overlay, "silent", silent ? TRUE : FALSE, nullptr);
    gst_object_unref(overlay);
}

bool SubtitleFeed::takeLateLine()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const bool late = m_late;
    m_late = false;
    return late;
}

// ---- in the playing pipeline ------------------------------------------------------------

GstElement* SubtitleFeed::build(GstBin* bin)
{
    unbuild();
    GstElement* overlay = gst_element_factory_make("subtitleoverlay", "crt-subtitles");
    GstElement* src = gst_element_factory_make("appsrc", "crt-subtitle-lines");
    if (!overlay || !src) {
        if (overlay) gst_object_unref(overlay);
        if (src) gst_object_unref(src);
        return nullptr;
    }
    // Times are the video's times; the player can ask for any place; nothing is ever too much.
    g_object_set(src, "format", GST_FORMAT_TIME, "stream-type", GST_APP_STREAM_TYPE_SEEKABLE, "is-live", FALSE, "max-bytes", guint64(0),
                 "block", FALSE, nullptr);
    g_signal_connect(src, "need-data", G_CALLBACK(&SubtitleFeed::onNeedData), this);
    g_signal_connect(src, "seek-data", G_CALLBACK(&SubtitleFeed::onSeekData), this);
    g_signal_connect(overlay, "deep-element-added", G_CALLBACK(&SubtitleFeed::onOverlayElement), this);
    gst_bin_add_many(bin, overlay, src, nullptr);
    if (!gst_element_link_pads(src, "src", overlay, "subtitle_sink")) {
        gst_bin_remove(bin, src);
        gst_bin_remove(bin, overlay);
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_overlay = GST_ELEMENT(gst_object_ref(overlay));
    m_appsrc = GST_ELEMENT(gst_object_ref(src));
    m_videoPad = gst_element_get_static_pad(overlay, "video_sink");
    m_textPad = gst_element_get_static_pad(overlay, "subtitle_sink");
    gst_pad_add_probe(m_videoPad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, &SubtitleFeed::onVideoEvent, this, nullptr);
    gst_pad_add_probe(m_textPad, GST_PAD_PROBE_TYPE_BUFFER, &SubtitleFeed::onTextBuffer, this, nullptr);
    if (GstPad* sp = gst_element_get_static_pad(src, "src")) {
        gst_pad_add_probe(sp, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM, &SubtitleFeed::onSourceUpstreamEvent, this, nullptr);
        gst_object_unref(sp);
    }
    m_capsOnSource = false;
    m_textFlowed = false;
    m_sourceErrorsNow = 0;
    m_playPos = m_seekPos = 0;   // (another video)
    m_feedPos = 0;
    m_pushedAny = false;
    ++m_feedEpoch;
    gst_caps_replace(&m_capsSet, nullptr);
    if (m_caps) giveCaps();
    return overlay;
}

void SubtitleFeed::unbuild()
{
    GstElement *overlay = nullptr, *src = nullptr;
    GstPad *vp = nullptr, *tp = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        releaseHold(false);
        overlay = m_overlay; src = m_appsrc; vp = m_videoPad; tp = m_textPad;
        m_overlay = m_appsrc = nullptr;
        m_videoPad = m_textPad = nullptr;
        // (commands for the old source element must not reach the next one)
        m_commands.erase(std::remove_if(m_commands.begin(), m_commands.end(), [](const Command& c) { return c.cmd == Cmd::Align; }), m_commands.end());
    }
    if (src) g_signal_handlers_disconnect_by_data(src, this);
    if (overlay) g_signal_handlers_disconnect_by_data(overlay, this);
    if (vp) gst_object_unref(vp);
    if (tp) gst_object_unref(tp);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        gst_caps_replace(&m_capsSet, nullptr);
    }
    if (src) gst_object_unref(src);
    if (overlay) gst_object_unref(overlay);
}

// The drawing elements inside the overlay must not hold the picture back to wait for a line: lines come
// when they are known, and after a jump the picture is held here, for exactly as long as needed.
void SubtitleFeed::onOverlayElement(GstBin*, GstBin*, GstElement* el, gpointer)
{
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(el), "wait-text")) g_object_set(el, "wait-text", FALSE, nullptr);
}

// The player's jumps reach the video; the source of the lines is then sent to the very place the
// video landed on (onVideoEvent), which a jump to the nearest keyframe only knows afterwards.
GstPadProbeReturn SubtitleFeed::onSourceUpstreamEvent(GstPad*, GstPadProbeInfo* info, gpointer)
{
    GstEvent* ev = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(ev) != GST_EVENT_SEEK) return GST_PAD_PROBE_OK;
    gst_event_unref(ev);
    GST_PAD_PROBE_INFO_DATA(info) = nullptr;
    return GST_PAD_PROBE_HANDLED;
}

GstPadProbeReturn SubtitleFeed::onVideoEvent(GstPad*, GstPadProbeInfo* info, gpointer self)
{
    auto* f = static_cast<SubtitleFeed*>(self);
    GstEvent* ev = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(ev) != GST_EVENT_SEGMENT) return GST_PAD_PROBE_OK;
    const GstSegment* seg = nullptr;
    gst_event_parse_segment(ev, &seg);
    if (!seg || seg->format != GST_FORMAT_TIME) return GST_PAD_PROBE_OK;
    Command c{Cmd::Align};
    c.pos = seg->start;
    c.rate = seg->rate > 0 ? seg->rate : 1.0;
    {
        std::lock_guard<std::mutex> lock(f->m_mutex);
        // A subtitle file counts from the start of the video; the video's own times may begin elsewhere.
        f->m_shift = (GST_CLOCK_TIME_IS_VALID(seg->time) && seg->start >= seg->time) ? gint64(seg->start - seg->time) : 0;
    }
    TRACE("video segment: start %.3f time %.3f rate %.2f", SEC(seg->start), SEC(seg->time), seg->rate);
    f->post(c);
    return GST_PAD_PROBE_OK;
}

void SubtitleFeed::onNeedData(GstElement*, guint, gpointer self)
{
    auto* f = static_cast<SubtitleFeed*>(self);
    std::lock_guard<std::mutex> lock(f->m_mutex);
    ++f->m_holdNeeds;
    TRACE("source asks for lines (%d since it started anew)", f->m_holdNeeds);
    f->pump();
    f->maybeRelease();
}

gboolean SubtitleFeed::onSeekData(GstElement*, guint64 offset, gpointer self)
{
    auto* f = static_cast<SubtitleFeed*>(self);
    std::lock_guard<std::mutex> lock(f->m_mutex);
    f->m_feedPos = offset;
    ++f->m_feedEpoch;
    f->m_pushedAny = false;
    f->m_lastPushed = 0;
    f->m_holdSeekData = true;
    TRACE("source starts anew at %.3f", SEC(offset));
    f->m_holdNeeds = 0;
    f->m_holdTextSeen = 0;
    return TRUE;
}

GstPadProbeReturn SubtitleFeed::onTextBuffer(GstPad*, GstPadProbeInfo*, gpointer self)
{
    auto* f = static_cast<SubtitleFeed*>(self);
    GstElement* quiet = nullptr;
    {
        std::lock_guard<std::mutex> lock(f->m_mutex);
        ++f->m_holdTextSeen;
        TRACE("a line reaches the overlay (%d since the source started anew)", f->m_holdTextSeen);
        f->maybeRelease();
        if (!f->m_textFlowed) {
            f->m_textFlowed = true;   // (the overlay has built what draws: from now on it can be told to keep quiet)
            if (!f->m_shown && f->m_overlay) quiet = GST_ELEMENT(gst_object_ref(f->m_overlay));
        }
    }
    if (quiet) {
        g_object_set(quiet, "silent", TRUE, nullptr);
        gst_object_unref(quiet);
    }
    return GST_PAD_PROBE_OK;
}

quint64 SubtitleFeed::aboutToSeek(qint64 posNs)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    const GstClockTime pos = GstClockTime(std::max<qint64>(0, posNs));
    m_seekPos = m_playPos = pos;
    m_wantUntil = pos + kLead;
    ++m_holdEpoch;
    m_holdSeekData = false;
    m_holdGaveUp = false;
    m_aligned = false;
    m_late = false;
    m_lateEpoch = 0;
    // The picture that comes after the jump waits at the overlay's door until its lines are in.
    m_holdArrivedUs = 0;
    if (m_shown && m_videoPad && m_kind != Kind::None && m_kind != Kind::Unsupported && m_kind != Kind::Failed && !m_holdProbe) {
        // (Not while the lines are only being looked for, as right after another track was picked: that takes as
        // long as it takes. The picture goes ahead; a line that comes after it is told of.)
        if (m_textFlowed && m_kind == Kind::Text) {
            m_holdProbe = gst_pad_add_probe(m_videoPad, GstPadProbeType(GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER), &SubtitleFeed::onHeldPicture, this, nullptr);
            ++m_nHolds;
        } else {
            m_holdGaveUp = true;   // (the overlay is not ready for lines yet: a line that comes after the picture is told of)
        }
    }
    TRACE("jump to %.3f%s", SEC(pos), m_holdProbe ? " (the picture waits for its lines)" : "");
    // The reader goes there too, unless it has been there or is about to be.
    // (A subtitle file is read whole, from its start, once. Having read a video to its end does not mean having
    // read it from its start.)
    if (m_reader && m_textIndex >= 0 && (m_kind == Kind::Text || m_kind == Kind::Reading)) {
        const GstClockTime from = pos > kLookBack ? pos - kLookBack : 0;            // the lines are needed from here
        const GstClockTime target = from > kEarly ? from - kEarly : 0;              // ... and may be stored from here
        // Where the reader's run began (as far as its lines are known) and where it has got to.
        const GstClockTime a = m_spanOpen ? m_spanA : 0, b = m_spanOpen ? m_spanB : 0;
        const bool fromStart = m_readerFromStart && !GST_CLOCK_TIME_IS_VALID(m_pendingReaderSeek);
        // Known already, right up to the place? Or is the reader on its way there: its run began early enough, and
        // it gets to the place sooner by reading on than by jumping?
        const Span* known = feedSpan(pos);
        bool have = known && (known->b == kForever || known->b > pos + GST_SECOND);
        if (!have && !m_readerEos && (m_spanOpen || fromStart) && (fromStart || a <= from + 2 * GST_SECOND) && pos <= b + kLookBack + kSoon) have = true;
        if (!m_padChosen) {
            // (It has not found the stream yet: it goes there when it has, or stays at the start.)
            m_pendingReaderSeek = (have || target == 0) ? GST_CLOCK_TIME_NONE : target;
        } else if (!have) {
            Command c{Cmd::ReaderSeek};
            c.pos = target;
            lock.unlock();
            post(c);
            lock.lock();
        }
    }
    m_cond.notify_all();
    return m_holdEpoch;
}

// The first picture after a jump has arrived and waits. How long at most is counted from here (how long the
// video took to get to it has nothing to do with the lines).
GstPadProbeReturn SubtitleFeed::onHeldPicture(GstPad*, GstPadProbeInfo*, gpointer self)
{
    auto* f = static_cast<SubtitleFeed*>(self);
    {
        std::lock_guard<std::mutex> lock(f->m_mutex);
        if (f->m_holdProbe && !f->m_holdArrivedUs) {
            f->m_holdArrivedUs = g_get_monotonic_time();
            TRACE("the picture after the jump is here and waits for its lines");
        }
    }
    f->m_watchCond.notify_all();
    return GST_PAD_PROBE_OK;
}

// Lets a waiting picture go when its lines have not come in a quarter of a second (they are still being read:
// they are drawn when they come, and the player is told, for a picture that stands).
void SubtitleFeed::watchLoop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    for (;;) {
        if (m_watchQuit) return;
        if (!m_holdProbe || !m_holdArrivedUs) { m_watchCond.wait(lock); continue; }
        const gint64 left = m_holdArrivedUs + kHoldAtMostUs - g_get_monotonic_time();
        if (left > 0) { m_watchCond.wait_for(lock, std::chrono::microseconds(left)); continue; }
        releaseHold(true);
    }
}

// The player's jump did not happen after all.
void SubtitleFeed::jumpFailed(quint64 epoch)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (epoch == m_holdEpoch) releaseHold(false);
}

void SubtitleFeed::releaseHold(bool timedOut)
{
    if (!m_holdProbe) return;
    if (m_videoPad) gst_pad_remove_probe(m_videoPad, m_holdProbe);
    m_holdProbe = 0;
    if (!m_holdArrivedUs) return;   // (no picture was waiting)
    const double ms = (g_get_monotonic_time() - m_holdArrivedUs) / 1000.0;
    m_holdArrivedUs = 0;
    TRACE("the picture is let through after %.0f ms%s", ms, timedOut ? " (given up waiting)" : "");
    ++m_nHeld;
    m_holdMsSum += ms;
    m_holdMsMax = std::max(m_holdMsMax, ms);
    if (timedOut) { ++m_nHoldTimeouts; m_holdGaveUp = true; }
}

// The span in which the lines for a source started at `pos` are found: it must reach back far enough
// before pos to hold the lines already in force there.
const SubtitleFeed::Span* SubtitleFeed::feedSpan(GstClockTime pos) const
{
    const GstClockTime from = pos > kLookBack ? pos - kLookBack : 0;
    for (const Span& s : m_spans)
        if (s.a <= from + 2 * GST_SECOND && (s.b == kForever || s.b > from)) return &s;
    return nullptr;
}

int SubtitleFeed::activeAt(GstClockTime pos) const
{
    if (m_kind != Kind::Text) return m_kind == Kind::Reading ? -1 : 0;
    const Span* s = feedSpan(pos);
    if (!s || (s->b != kForever && s->b <= pos)) return -1;
    const gint64 p = gint64(pos);
    for (const Cue& c : m_cues) {
        const gint64 start = gint64(c.pts) + timeOffset(), end = gint64(c.end) + timeOffset();
        if (start > p) break;
        if (end > p) return 1;
    }
    return 0;
}

// The picture after a jump is let through when its lines are with the overlay: when the source has started
// anew and there is no line in force; or when a line in force has been taken in (a second one is on its
// way behind it, or the source has been asked for more).
void SubtitleFeed::maybeRelease()
{
    if (!m_holdProbe || !m_holdSeekData || m_holdNeeds < 1) return;
    const int active = activeAt(m_feedPos);
    if (active == 0 || (active == 1 && (m_holdTextSeen >= 2 || (m_holdTextSeen >= 1 && m_holdNeeds >= 2)))) releaseHold(false);
}

// Tells the source what kind of lines are coming. For another kind than before, the overlay has to build what
// draws them anew, which it does when the next picture comes by: until a line has got through, a picture must
// not be held back for its lines (maybeRelease would wait for something that waits for the picture).
void SubtitleFeed::giveCaps()
{
    gst_app_src_set_caps(GST_APP_SRC(m_appsrc), m_caps);
    m_capsOnSource = true;
    if (!m_capsSet || !gst_caps_is_equal(m_capsSet, m_caps)) {
        gst_caps_replace(&m_capsSet, m_caps);
        m_textFlowed = false;
        if (m_holdProbe) releaseHold(true);
    }
}

// Hands the source every line that is known and due: from the place it started at, in order, as far as
// the lines are known without a gap.
void SubtitleFeed::pump()
{
    if (!m_appsrc || m_kind != Kind::Text || !m_caps) return;
    const Span* span = feedSpan(m_feedPos);
    if (!span) return;
    if (!m_capsOnSource) giveCaps();
    const gint64 pos = gint64(m_feedPos);
    const gint64 offset = timeOffset();
    // A line in force at the starting place that is handed over after the picture was let through: the picture
    // on screen (if the video is paused) lacks it.
    auto late = [this] {
        if (!m_shown || !m_holdGaveUp || m_holdProbe || m_lateEpoch == m_feedEpoch) return;
        m_late = true;
        m_lateEpoch = m_feedEpoch;
        ++m_nLate;
    };
    for (Cue& c : m_cues) {
        if (span->b != kForever && c.pts >= span->b) break;
        if (c.pushedEpoch == m_feedEpoch) continue;
        c.pushedEpoch = m_feedEpoch;
        if (gint64(c.end) + offset <= pos) continue;      // over before the place the source started at
        const bool inForce = gint64(c.pts) + offset <= pos;
        if (m_pushedAny && c.pts < m_lastPushed) {        // turned up behind lines already handed over
            TRACE("line of %.3f - %.3f turned up behind lines already handed over%s", SEC(c.pts), SEC(c.end), inForce ? " (in force)" : "");
            if (inForce) late();
            continue;
        }
        GstBuffer* b = gst_buffer_copy(c.buf);
        // A line already in force is handed over as beginning at the place the source started at, and lasting
        // what is left of it: what draws the lines cuts off the beginning by itself, but the ASS renderer then
        // leaves the length as it was, and the line stays on screen for too long.
        const gint64 begin = std::max<gint64>(inForce ? pos : 0, gint64(c.pts) + offset);
        GST_BUFFER_PTS(b) = GstClockTime(begin);
        GST_BUFFER_DTS(b) = GST_CLOCK_TIME_NONE;
        GST_BUFFER_DURATION(b) = GstClockTime(std::max<gint64>(1, gint64(c.end) + offset - begin));
        if (inForce) late();
        TRACE("line of %.3f - %.3f handed over%s", SEC(c.pts), SEC(c.end), inForce ? " (in force)" : "");
        gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), b);
        m_lastPushed = c.pts;
        m_pushedAny = true;
        ++m_nPushed;
    }
}

// ---- the lines ---------------------------------------------------------------------------

void SubtitleFeed::clearCues()
{
    for (Cue& c : m_cues) gst_buffer_unref(c.buf);
    m_cues.clear();
    m_spans.clear();
    gst_caps_replace(&m_caps, nullptr);
    m_capsOnSource = false;
    m_spanOpen = false;
}

void SubtitleFeed::setSource(const QString& uri, int textIndex, const QList<QPair<QByteArray, QByteArray>>& headers)
{
    quint64 token = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_kind != Kind::None && uri == m_uri && textIndex == m_textIndex) return;
        clearCues();
        m_uri = uri;
        m_textIndex = textIndex;
        m_headers = headers;
        m_kind = uri.isEmpty() ? Kind::None : Kind::Reading;
        m_error.clear();
        m_capsName.clear();
        m_late = false;
        m_pushedAny = false;   // (what was handed over was the other source's)
        m_lastPushed = 0;
        token = ++m_sourceToken;
        m_commands.erase(std::remove_if(m_commands.begin(), m_commands.end(),
                                        [](const Command& c) { return c.cmd == Cmd::Start || c.cmd == Cmd::Stop || c.cmd == Cmd::ReaderSeek; }),
                         m_commands.end());
    }
    Command c{uri.isEmpty() ? Cmd::Stop : Cmd::Start};
    c.token = token;
    post(c);
}

void SubtitleFeed::clearSource() { setSource(QString(), -1); }

void SubtitleFeed::addCue(GstBuffer* buf)
{
    Cue c;
    c.pts = GST_BUFFER_PTS(buf);
    c.end = c.pts + (GST_BUFFER_DURATION_IS_VALID(buf) && GST_BUFFER_DURATION(buf) > 0 ? GST_BUFFER_DURATION(buf) : kDefaultLength);
    auto at = std::lower_bound(m_cues.begin(), m_cues.end(), c.pts, [](const Cue& x, GstClockTime t) { return x.pts < t; });
    // (read a second time after the reader went back: already here)
    const gsize size = gst_buffer_get_size(buf);
    for (auto it = at; it != m_cues.end() && it->pts == c.pts; ++it)
        if (it->end == c.end && gst_buffer_get_size(it->buf) == size) {
            GstMapInfo a, b;
            bool same = false;
            if (gst_buffer_map(it->buf, &a, GST_MAP_READ)) {
                if (gst_buffer_map(buf, &b, GST_MAP_READ)) {
                    same = a.size == b.size && std::memcmp(a.data, b.data, a.size) == 0;
                    gst_buffer_unmap(buf, &b);
                }
                gst_buffer_unmap(it->buf, &a);
            }
            if (same) return;
        }
    while (at != m_cues.end() && at->pts == c.pts) ++at;   // (lines of one moment keep the file's order)
    TRACE("line of %.3f - %.3f read", SEC(c.pts), SEC(c.end));
    c.buf = gst_buffer_ref(buf);
    m_cues.insert(at, c);
}

// The reader has got as far as `pts` in the file (by its picture, or its sound).
void SubtitleFeed::progress(GstClockTime pts)
{
    if (!m_spanOpen) {
        m_spanOpen = true;
        m_spanA = m_readerFromStart ? 0 : pts + kEarly;   // (lines of before may be stored before where this run began)
        m_spanB = m_spanA;
        m_lastPumpB = m_spanA;
        m_spans.push_back({m_spanA, m_spanB});
    }
    const GstClockTime b = pts > kMargin ? pts - kMargin : 0;
    if (b <= m_spanB) return;
    m_spanB = b;
    // The reader's own span grows; spans it has run into are taken in.
    Span cur{m_spanA, m_spanB};
    std::vector<Span> out;
    for (const Span& s : m_spans) {
        const bool touches = !(s.b != kForever && s.b < cur.a) && !(cur.b < s.a);
        if (!touches) { out.push_back(s); continue; }
        cur.a = std::min(cur.a, s.a);
        cur.b = (s.b == kForever) ? kForever : std::max(cur.b, s.b);
    }
    out.push_back(cur);
    m_spans.swap(out);
    m_spanA = cur.a;
    if (cur.b != kForever) m_spanB = std::max(m_spanB, cur.b);
}

// ---- the reader --------------------------------------------------------------------------

void SubtitleFeed::post(const Command& c)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (c.cmd == Cmd::Align || c.cmd == Cmd::ReaderSeek)   // (only the newest of its kind counts)
            m_commands.erase(std::remove_if(m_commands.begin(), m_commands.end(), [&c](const Command& x) { return x.cmd == c.cmd; }), m_commands.end());
        m_commands.push_back(c);
    }
    m_cond.notify_all();
}

void SubtitleFeed::workerLoop()
{
    for (;;) {
        Command c;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cond.wait(lock, [this] { return !m_commands.empty(); });
            c = m_commands.front();
            m_commands.pop_front();
        }
        switch (c.cmd) {
        case Cmd::Quit:
            stopReader();
            return;
        case Cmd::Stop:
            stopReader();
            break;
        case Cmd::Start:
            stopReader();
            startReader(c.token);
            break;
        case Cmd::ReaderSeek: {
            GstElement* reader = nullptr;
            bool startOver = false;
            quint64 token = 0;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_reader || m_readerToken != m_sourceToken) break;
                if (!m_padChosen) { m_pendingReaderSeek = c.pos; break; }   // (not until the stream is found)
                ++m_nReaderSeeks;
                if (c.pos == 0) {
                    // From the very start: by starting over. (A jump to the start goes to the first place the file's
                    // index knows, and lines may be stored before that.)
                    startOver = true;
                    token = m_readerToken;
                } else {
                    reader = GST_ELEMENT(gst_object_ref(m_reader));
                    ++m_readerEpoch;   // whoever waits in the reader's threads lets go
                    m_spanOpen = false;
                    m_readerFromStart = false;
                    m_readerEos = false;
                }
            }
            if (startOver) {
                TRACE("the reader starts over");
                stopReader();
                startReader(token, true);
                break;
            }
            TRACE("the reader goes to %.3f", SEC(c.pos));
            m_cond.notify_all();
            const gboolean ok = gst_element_seek(reader, 1.0, GST_FORMAT_TIME, GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_SNAP_BEFORE),
                                                 GST_SEEK_TYPE_SET, gint64(c.pos), GST_SEEK_TYPE_NONE, -1);
            gst_object_unref(reader);
            if (!ok) {
                // It cannot jump: it goes on from where it is (and what lies before stays unknown).
                std::lock_guard<std::mutex> lock(m_mutex);
                m_error = QStringLiteral("the subtitle reader could not jump");
            }
            break;
        }
        case Cmd::Align: {
            GstElement* src = nullptr;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_appsrc) break;
                src = GST_ELEMENT(gst_object_ref(m_appsrc));
                m_aligned = true;
                ++m_nAligns;
            }
            TRACE("the source is sent to %.3f", SEC(c.pos));
            // The source starts anew at the place the video is at (its lines up to there are dropped by the
            // overlay, the ones in force come again).
            gst_element_seek(src, c.rate, GST_FORMAT_TIME, GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE), GST_SEEK_TYPE_SET, gint64(c.pos),
                             GST_SEEK_TYPE_NONE, -1);
            gst_object_unref(src);
            break;
        }
        }
    }
}

void SubtitleFeed::stopReader()
{
    GstElement* reader = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        reader = m_reader;
        m_reader = nullptr;
        m_readerStop = true;
        ++m_readerEpoch;
    }
    m_cond.notify_all();
    if (!reader) return;
    GstBus* bus = gst_element_get_bus(reader);
    gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
    gst_object_unref(bus);
    gst_element_set_state(reader, GST_STATE_NULL);
    gst_object_unref(reader);
}

void SubtitleFeed::startReader(quint64 token, bool fromStart)
{
    QByteArray uri;
    int textIndex = -1;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (token != m_sourceToken || m_uri.isEmpty()) return;   // (the source has changed again meanwhile)
        uri = m_uri.toUtf8();
        textIndex = m_textIndex;
        m_readerToken = token;
        m_readerStop = false;
        m_readerEos = false;
        m_spanOpen = false;
        m_readerFromStart = true;
        m_subPads = 0;
        m_padChosen = false;
        m_readerHasClock = m_clockIsVideo = false;
        m_clockPad = nullptr;
        m_pendingReaderSeek = GST_CLOCK_TIME_NONE;
        // Where the picture is now: the reader goes there once it has found the stream.
        if (!fromStart && textIndex >= 0 && m_playPos > kLookBack + kEarly + kSoon) m_pendingReaderSeek = m_playPos - kLookBack - kEarly;
        m_wantUntil = m_playPos + kLead;
    }
    if (textIndex < 0 && loadScriptFile(QString::fromUtf8(uri), token)) return;
    GstElement* pipe = gst_pipeline_new("crt-subtitle-reader");
    GstElement* src = gst_element_factory_make("urisourcebin", nullptr);
    GstElement* tf = gst_element_factory_make("typefind", nullptr);
    if (!pipe || !src || !tf) {
        if (pipe) gst_object_unref(pipe);
        if (src) gst_object_unref(src);
        if (tf) gst_object_unref(tf);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_kind = Kind::Failed;
            m_error = QStringLiteral("GStreamer lacks urisourcebin or typefind");
        }
        notify();
        return;
    }
    g_object_set(src, "uri", uri.constData(), nullptr);
    g_signal_connect(src, "source-setup", G_CALLBACK(&SubtitleFeed::onSourceSetup), this);
    gst_bin_add_many(GST_BIN(pipe), src, tf, nullptr);
    g_signal_connect(src, "pad-added", G_CALLBACK(&SubtitleFeed::onSourcePad), tf);
    g_signal_connect(tf, "have-type", G_CALLBACK(&SubtitleFeed::onTypeFound), this);
    GstBus* bus = gst_element_get_bus(pipe);
    gst_bus_set_sync_handler(bus, &SubtitleFeed::onReaderMessage, this, nullptr);
    gst_object_unref(bus);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_reader = pipe;
    }
    gst_element_set_state(pipe, GST_STATE_PLAYING);
}

// A subtitle file that is an ASS / SSA script: read here, whole (true when it was one).
bool SubtitleFeed::loadScriptFile(const QString& uri, quint64 token)
{
    const QUrl u(uri);
    if (!u.isLocalFile()) return false;
    QFile file(u.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) return false;
    const AssScript script = parseAssScript(file.readAll());
    if (!script.ok) return false;
    GstBuffer* head = gst_buffer_new_allocate(nullptr, gsize(script.head.size()), nullptr);
    gst_buffer_fill(head, 0, script.head.constData(), gsize(script.head.size()));
    GstCaps* caps = gst_caps_new_simple(script.ssa ? "application/x-ssa" : "application/x-ass", "codec_data", GST_TYPE_BUFFER, head, nullptr);
    gst_buffer_unref(head);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (token != m_sourceToken) { gst_caps_unref(caps); return true; }
        gst_caps_replace(&m_caps, caps);
        m_capsOnSource = false;
        for (const AssLine& l : script.lines) {
            GstBuffer* b = gst_buffer_new_allocate(nullptr, gsize(l.chunk.size()), nullptr);
            gst_buffer_fill(b, 0, l.chunk.constData(), gsize(l.chunk.size()));
            GST_BUFFER_PTS(b) = GstClockTime(l.startNs);
            GST_BUFFER_DURATION(b) = GstClockTime(l.endNs - l.startNs);
            addCue(b);
            gst_buffer_unref(b);
        }
        m_spans.clear();
        m_spans.push_back({0, kForever});
        m_readerEos = true;
        m_padChosen = true;
        m_capsName = QString::fromLatin1(script.ssa ? "application/x-ssa (file)" : "application/x-ass (file)");
        m_kind = Kind::Text;
        pump();
        maybeRelease();
    }
    gst_caps_unref(caps);
    notify();
    return true;
}

void SubtitleFeed::onSourceSetup(GstElement*, GstElement* source, gpointer self)
{
    auto* f = static_cast<SubtitleFeed*>(self);
    GObjectClass* klass = G_OBJECT_GET_CLASS(source);
    if (g_object_class_find_property(klass, "user-agent")) g_object_set(source, "user-agent", "CRT-Player", nullptr);
    QList<QPair<QByteArray, QByteArray>> headers;
    {
        std::lock_guard<std::mutex> lock(f->m_mutex);
        headers = f->m_headers;
    }
    if (headers.isEmpty() || !g_object_class_find_property(klass, "extra-headers")) return;
    GstStructure* st = gst_structure_new_empty("extra-headers");
    for (const auto& h : headers) gst_structure_set(st, h.first.constData(), G_TYPE_STRING, h.second.constData(), nullptr);
    g_object_set(source, "extra-headers", st, nullptr);
    gst_structure_free(st);
}

void SubtitleFeed::onSourcePad(GstElement*, GstPad* pad, gpointer typefind)
{
    if (GstPad* sink = gst_element_get_static_pad(GST_ELEMENT(typefind), "sink")) {
        if (!gst_pad_is_linked(sink)) gst_pad_link(pad, sink);
        gst_object_unref(sink);
    }
}

// What kind of file it is decides what takes it apart: Matroska and MP4 by their own demuxers alone (nothing
// of the picture or the sound is looked into), anything else by parsebin (which also knows subtitle files).
void SubtitleFeed::onTypeFound(GstElement* typefind, guint, GstCaps* caps, gpointer self)
{
    auto* f = static_cast<SubtitleFeed*>(self);
    GstElement* pipe = GST_ELEMENT(gst_element_get_parent(typefind));
    if (!pipe) return;
    const gchar* name = gst_structure_get_name(gst_caps_get_structure(caps, 0));
    const char* factory = "parsebin";
    if (!g_strcmp0(name, "video/x-matroska") || !g_strcmp0(name, "video/webm") || !g_strcmp0(name, "audio/x-matroska")) factory = "matroskademux";
    else if (!g_strcmp0(name, "video/quicktime") || !g_strcmp0(name, "audio/x-m4a") || !g_strcmp0(name, "application/x-3gp")) factory = "qtdemux";
    GstElement* demux = gst_element_factory_make(factory, nullptr);
    if (!demux && g_strcmp0(factory, "parsebin")) demux = gst_element_factory_make("parsebin", nullptr);
    if (!demux) {
        {
            std::lock_guard<std::mutex> lock(f->m_mutex);
            f->m_kind = Kind::Failed;
            f->m_error = QStringLiteral("GStreamer has nothing to take this file apart with");
        }
        gst_object_unref(pipe);
        f->notify();
        return;
    }
    g_signal_connect(demux, "pad-added", G_CALLBACK(&SubtitleFeed::onReaderPad), f);
    gst_bin_add(GST_BIN(pipe), demux);
    gst_element_link(typefind, demux);
    gst_element_sync_state_with_parent(demux);
    gst_object_unref(pipe);
}

void SubtitleFeed::onReaderPad(GstElement* demux, GstPad* pad, gpointer self)
{
    auto* f = static_cast<SubtitleFeed*>(self);
    GstElement* pipe = GST_ELEMENT(gst_element_get_parent(demux));
    if (!pipe) return;
    GstCaps* caps = padCaps(pad);
    const GstStructure* st = caps && !gst_caps_is_empty(caps) && !gst_caps_is_any(caps) ? gst_caps_get_structure(caps, 0) : nullptr;
    const gchar* name = st ? gst_structure_get_name(st) : "";
    bool take = false, changed = false, seekNow = false;
    const char* parser = nullptr;
    if (!isSubtitleCaps(name)) {
        // The picture (or, without one, the sound) tells how far into the file the reader is.
        const bool video = g_str_has_prefix(name, "video/") || g_str_has_prefix(name, "image/");
        std::lock_guard<std::mutex> lock(f->m_mutex);
        if (!f->m_readerHasClock || (video && !f->m_clockIsVideo)) {
            f->m_readerHasClock = true;
            f->m_clockIsVideo = video;
            f->m_clockPad = pad;
            gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, &SubtitleFeed::onClockBuffer, f, nullptr);
        }
    } else {
        std::lock_guard<std::mutex> lock(f->m_mutex);
        const int index = f->m_subPads++;
        if (!f->m_padChosen && (f->m_textIndex < 0 || index == f->m_textIndex)) {
            f->m_padChosen = true;
            const bool ass = !g_strcmp0(name, "application/x-ass") || !g_strcmp0(name, "application/x-ssa");
            if (g_str_has_prefix(name, "text/x-raw") || (ass && gst_structure_has_field(st, "codec_data"))) take = true;
            else if (ass) { take = true; parser = "ssaparse"; }
            else if (g_str_has_prefix(name, "application/x-subtitle")) { take = true; parser = "subparse"; }
            f->m_capsName = QString::fromUtf8(name);
            f->m_kind = take ? Kind::Text : Kind::Unsupported;
            TRACE("the reader found the subtitle stream: %s%s", name, take ? "" : " (not text)");
            changed = true;
            seekNow = take && GST_CLOCK_TIME_IS_VALID(f->m_pendingReaderSeek);
        }
    }
    if (take) {
        GstElement* sink = gst_element_factory_make("appsink", nullptr);
        GstElement* parse = parser ? gst_element_factory_make(parser, nullptr) : nullptr;
        if (!sink || (parser && !parse)) {
            if (sink) gst_object_unref(sink);
            if (parse) gst_object_unref(parse);
            {
                std::lock_guard<std::mutex> lock(f->m_mutex);
                f->m_kind = Kind::Unsupported;   // (GStreamer's own path may still know what to do with it)
            }
            take = false;
            seekNow = false;
        } else {
            g_object_set(sink, "sync", FALSE, "async", FALSE, "emit-signals", TRUE, "max-buffers", 0u, nullptr);
            g_signal_connect(sink, "new-sample", G_CALLBACK(&SubtitleFeed::onCueSample), f);
            gst_bin_add(GST_BIN(pipe), sink);
            if (parse) {
                gst_bin_add(GST_BIN(pipe), parse);
                gst_element_link(parse, sink);
                gst_element_sync_state_with_parent(sink);
                gst_element_sync_state_with_parent(parse);
                if (GstPad* sp = gst_element_get_static_pad(parse, "sink")) { gst_pad_link(pad, sp); gst_object_unref(sp); }
            } else {
                gst_element_sync_state_with_parent(sink);
                if (GstPad* sp = gst_element_get_static_pad(sink, "sink")) { gst_pad_link(pad, sp); gst_object_unref(sp); }
            }
        }
    }
    if (!take) swallow(pipe, pad);
    if (caps) gst_caps_unref(caps);
    gst_object_unref(pipe);
    if (seekNow) {
        Command c{Cmd::ReaderSeek};
        {
            std::lock_guard<std::mutex> lock(f->m_mutex);
            c.pos = f->m_pendingReaderSeek;
            f->m_pendingReaderSeek = GST_CLOCK_TIME_NONE;
        }
        f->post(c);
    }
    if (changed) f->notify();
}

GstFlowReturn SubtitleFeed::onCueSample(GstElement* sink, gpointer self)
{
    auto* f = static_cast<SubtitleFeed*>(self);
    GstSample* s = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!s) return GST_FLOW_OK;
    GstBuffer* buf = gst_sample_get_buffer(s);
    GstCaps* caps = gst_sample_get_caps(s);
    bool late = false;
    if (buf && GST_BUFFER_PTS_IS_VALID(buf)) {
        std::lock_guard<std::mutex> lock(f->m_mutex);
        if (caps && (!f->m_caps || !gst_caps_is_equal(caps, f->m_caps))) {
            gst_caps_replace(&f->m_caps, caps);
            f->m_capsOnSource = false;
        }
        f->addCue(buf);
        // A subtitle file has no picture to tell how far the reader is: what it has read so far is taken as known
        // up to the line it has reached (the lines come in order), and to the end once it has ended.
        if (!f->m_readerHasClock) f->progress(GST_BUFFER_PTS(buf) + kMargin);
        f->pump();
        f->maybeRelease();
        late = f->m_late;
    }
    gst_sample_unref(s);
    if (late) f->notify();
    return GST_FLOW_OK;
}

GstPadProbeReturn SubtitleFeed::onClockBuffer(GstPad* pad, GstPadProbeInfo* info, gpointer self)
{
    auto* f = static_cast<SubtitleFeed*>(self);
    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    GstClockTime pts = buf ? GST_BUFFER_PTS(buf) : GST_CLOCK_TIME_NONE;
    if (!GST_CLOCK_TIME_IS_VALID(pts) && buf) pts = GST_BUFFER_DTS(buf);
    if (!GST_CLOCK_TIME_IS_VALID(pts)) return GST_PAD_PROBE_OK;
    bool late = false;
    {
        std::unique_lock<std::mutex> lock(f->m_mutex);
        if (pad != f->m_clockPad) return GST_PAD_PROBE_OK;
        f->progress(pts);
        // (lines become due as the reader gets on: looked at every second of video read, and while a picture waits)
        if (f->m_holdProbe || f->m_spanB >= f->m_lastPumpB + GST_SECOND) {
            f->m_lastPumpB = f->m_spanB;
            f->pump();
            f->maybeRelease();
            late = f->m_late;
        }
        // Far enough ahead of the picture: the reader waits here until the picture has moved on (or it is sent elsewhere).
        const quint64 epoch = f->m_readerEpoch;
        f->m_cond.wait(lock, [f, pts, epoch] { return f->m_readerStop || f->m_readerEpoch != epoch || pts <= f->m_wantUntil; });
    }
    if (late) f->notify();
    return GST_PAD_PROBE_OK;
}

GstBusSyncReply SubtitleFeed::onReaderMessage(GstBus*, GstMessage* msg, gpointer self)
{
    auto* f = static_cast<SubtitleFeed*>(self);
    bool changed = false, late = false;
    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
        std::lock_guard<std::mutex> lock(f->m_mutex);
        f->m_readerEos = true;
        TRACE("the reader is at the end of the file");
        if (!f->m_padChosen) {
            f->m_kind = Kind::Failed;
            f->m_error = QStringLiteral("no such subtitle stream in the file");
            changed = true;
        } else if (f->m_kind == Kind::Text) {
            // Read to the end: from where this run began, every line is known.
            if (!f->m_spanOpen) f->progress(f->m_readerFromStart ? 0 : f->m_playPos);
            Span cur{f->m_spanA, kForever};
            std::vector<Span> out;
            for (const Span& s : f->m_spans)
                if (s.b != kForever && s.b < cur.a) out.push_back(s);
                else cur.a = std::min(cur.a, s.a);
            out.push_back(cur);
            f->m_spans.swap(out);
            f->pump();
            f->maybeRelease();
            late = f->m_late;
        }
    } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
        GError* err = nullptr;
        gst_message_parse_error(msg, &err, nullptr);
        std::lock_guard<std::mutex> lock(f->m_mutex);
        f->m_error = err ? QString::fromUtf8(err->message) : QStringLiteral("error");
        if (err) g_error_free(err);
        if (f->m_kind == Kind::Reading || (f->m_kind == Kind::Text && f->m_cues.empty())) {
            f->m_kind = Kind::Failed;
            changed = true;
        }
    }
    if (changed || late) f->notify();
    gst_message_unref(msg);
    return GST_BUS_DROP;
}

void SubtitleFeed::playedTo(qint64 posNs)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_playPos = GstClockTime(std::max<qint64>(0, posNs));
        m_wantUntil = m_playPos + kLead;
    }
    m_cond.notify_all();
}

bool SubtitleFeed::sourceFailed(GstObject* from)
{
    Command c{Cmd::Align};
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_appsrc || from != GST_OBJECT(m_appsrc)) return false;
        ++m_nSourceErrors;
        if (++m_sourceErrorsNow > 5) return true;   // (not again and again)
        c.pos = m_playPos;
    }
    post(c);
    return true;
}

QJsonObject SubtitleFeed::report() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    static const char* const kinds[] = {"none", "reading", "text", "unsupported", "failed"};
    QJsonArray spans;
    for (const Span& s : m_spans)
        spans.append(QJsonArray{double(s.a) / GST_SECOND, s.b == kForever ? -1.0 : double(s.b) / GST_SECOND});
    return QJsonObject{{"built", m_overlay != nullptr}, {"kind", kinds[int(m_kind)]}, {"caps", m_capsName}, {"error", m_error},
                       {"textIndex", m_textIndex}, {"shown", m_shown}, {"lines", int(m_cues.size())}, {"known", spans},
                       {"readToEnd", m_readerEos}, {"readerJumps", double(m_nReaderSeeks)}, {"handedOver", double(m_nPushed)},
                       {"restarts", double(m_nAligns)}, {"holds", double(m_nHeld)}, {"holdTimeouts", double(m_nHoldTimeouts)},
                       {"holdMsMean", m_nHeld ? m_holdMsSum / double(m_nHeld) : 0.0}, {"holdMsMax", m_holdMsMax},
                       {"lateLines", double(m_nLate)}, {"sourceErrors", double(m_nSourceErrors)}, {"delayMs", double(m_delay) / GST_MSECOND}, {"startedAtMs", double(m_feedPos) / GST_MSECOND}};
}
