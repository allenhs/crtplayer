#include "LosslessCutter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <gst/pbutils/pbutils.h>

namespace {
QString stamp(qint64 ns)
{
    const qint64 s = ns / 1000000000;
    return QString::asprintf("%02lld-%02lld-%02lld", s / 3600, (s / 60) % 60, s % 60);
}
bool haveElement(const char* name)
{
    GstElementFactory* f = gst_element_factory_find(name);
    if (f) gst_object_unref(f);
    return f != nullptr;
}
} // namespace

LosslessCutter::LosslessCutter(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<LosslessCutter::Result>();
    m_progressTimer.setInterval(200);
    connect(&m_progressTimer, &QTimer::timeout, this, [this] {
        qint64 last = -1;
        {
            QMutexLocker l(&m_lock);
            last = m_lastPts;
        }
        const qint64 from = m_firstVideoPts >= 0 ? m_firstVideoPts : m_a;
        if (last >= 0 && m_b > from) emit progress(std::clamp(double(last - from) / double(m_b - from), 0.0, 1.0));
    });
    // Seek once every track has announced its format (or after 3 s, without the ones that haven't).
    m_capsWait.setInterval(50);
    connect(&m_capsWait, &QTimer::timeout, this, [this] {
        if (!m_pipe) { m_capsWait.stop(); return; }
        bool all = true, any = false;
        {
            QMutexLocker l(&m_lock);
            for (const Stream& s : m_streams) {
                any = true;
                GstCaps* caps = gst_pad_get_current_caps(s.pad);
                if (!caps) all = false;
                else gst_caps_unref(caps);
            }
            if (m_lastPadAdded.isValid() && m_lastPadAdded.elapsed() < 250) all = false;   // more may be coming
        }
        if (any && (all || m_capsClock.elapsed() > 3000)) {
            m_capsWait.stop();
            seekAndLink();
        }
    });
    m_watchdog.setSingleShot(true);
    m_watchdog.setInterval(30000);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        finish(false, m_probeOnly ? tr("The file did not respond to seeking.")
                                  : tr("Nothing arrived for 30 seconds; the file may not support cutting without re-encoding."));
    });
}

LosslessCutter::~LosslessCutter()
{
    teardown();
    if (!m_part.isEmpty()) QFile::remove(m_part);
}

QString LosslessCutter::muxerFor(const QString& sourcePath, QString* ext, QString* containerName)
{
    const QString e = QFileInfo(sourcePath).suffix().toLower();
    struct M { QStringList exts; const char* mux; const char* name; };
    static const M table[] = {
        {{"mkv", "mk3d", "mka"}, "matroskamux", "Matroska"},
        {{"webm"}, "webmmux", "WebM"},
        {{"mp4", "m4v", "m4a"}, "mp4mux", "MP4"},
        {{"mov", "qt"}, "qtmux", "QuickTime"},
        {{"ts", "m2ts", "mts", "m2t"}, "mpegtsmux", "MPEG-TS"},
        {{"avi"}, "avimux", "AVI"},
        {{"ogg", "ogv", "oga"}, "oggmux", "Ogg"},
        {{"flv"}, "flvmux", "FLV"},
        {{"mpg", "mpeg", "vob"}, "mpegpsmux", "MPEG-PS"},
    };
    for (const M& m : table) {
        if (m.exts.contains(e) && haveElement(m.mux)) {
            if (ext) *ext = e;
            if (containerName) *containerName = QString::fromLatin1(m.name);
            return QString::fromLatin1(m.mux);
        }
    }
    // Anything else (or a muxer that isn't installed): Matroska holds nearly every codec.
    if (ext) *ext = QStringLiteral("mkv");
    if (containerName) *containerName = QStringLiteral("Matroska");
    return QStringLiteral("matroskamux");
}

QString LosslessCutter::outputPathFor(const QString& sourcePath, qint64 aNs, qint64 bNs, const QString& ext)
{
    const QFileInfo fi(sourcePath);
    QString dir = fi.absolutePath();
    // Can a file be made there? (A real try: permissions alone don't tell about read-only
    // mounts, immutable folders or network shares.)
    auto writable = [](const QString& d) {
        QTemporaryFile t(d + QStringLiteral("/.crtplayer-write-test-XXXXXX"));
        return t.open();
    };
    if (!writable(dir)) {   // e.g. a read-only share: ~/Videos instead
        dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        if (dir.isEmpty()) dir = QDir::homePath();
        QDir().mkpath(dir);
    }
    QString base = fi.completeBaseName();
    base.replace(QRegularExpression("[/\\\\:*?\"<>|]"), "_");
    const QString stem = QStringLiteral("%1 - cut %2 to %3").arg(base, stamp(aNs), stamp(bNs));
    QString path = QDir(dir).filePath(stem + '.' + ext);
    for (int n = 2; QFileInfo::exists(path) || path == fi.absoluteFilePath(); ++n)
        path = QDir(dir).filePath(QStringLiteral("%1 (%2).%3").arg(stem).arg(n).arg(ext));
    return path;
}

void LosslessCutter::findKeyframe(const QString& sourcePath, qint64 aNs, bool after)
{
    if (m_pipe) return;
    m_snapAfter = after;
    m_result = Result();
    m_result.requestedStartNs = aNs;
    m_source = sourcePath;
    m_a = std::max<qint64>(0, aNs);
    m_b = -1;
    m_cutAfterProbe = false;
    startProbe();
}

void LosslessCutter::startProbe()
{
    m_probeOnly = true;
    m_firstVideoPts = m_kRaw = m_lastPts = -1;
    m_armed = false;
    m_pipe = gst_pipeline_new("keyframe-probe");
    GstElement* src = gst_element_factory_make("filesrc", nullptr);
    m_parse = gst_element_factory_make("parsebin", nullptr);
    if (!src || !m_parse) {
        if (src) gst_object_unref(src);
        if (m_parse) gst_object_unref(m_parse);
        gst_object_unref(m_pipe);
        m_pipe = m_parse = nullptr;
        finish(false, tr("GStreamer's parsebin is missing."));
        return;
    }
    g_object_set(src, "location", QFile::encodeName(m_source).constData(), nullptr);
    gst_bin_add_many(GST_BIN(m_pipe), src, m_parse, nullptr);
    gst_element_link(src, m_parse);
    g_signal_connect(m_parse, "pad-added", G_CALLBACK(onPadAdded), this);
    GstBus* bus = gst_element_get_bus(m_pipe);
    m_busWatch = gst_bus_add_watch(bus, onBus, this);
    gst_object_unref(bus);
    m_watchdog.start(10000);
    gst_element_set_state(m_pipe, GST_STATE_PLAYING);
}

bool LosslessCutter::start(const QString& sourcePath, qint64 aNs, qint64 bNs, const QString& outPath)
{
    if (m_pipe) return false;
    m_result = Result();
    m_source = sourcePath;
    m_a = std::max<qint64>(0, aNs);
    m_b = bNs;
    m_result.requestedStartNs = m_a;
    m_result.stopNs = m_b;
    if (!QFileInfo(sourcePath).isFile()) { finish(false, tr("Only files on this computer can be cut.")); return false; }
    if (m_b <= m_a) { finish(false, tr("The end (B) must come after the start (A).")); return false; }
    QString ext, name;
    m_muxName = muxerFor(sourcePath, &ext, &name);
    m_result.container = name;
    m_final = outPath.isEmpty() ? outputPathFor(sourcePath, m_a, m_b, ext) : outPath;
    if (QFileInfo(m_final).absoluteFilePath() == QFileInfo(sourcePath).absoluteFilePath() || QFileInfo::exists(m_final)) {
        finish(false, tr("Refusing to write over an existing file: %1").arg(m_final));   // never overwrite
        return false;
    }
    // First find the keyframe (its own quick pass); the cut then starts its timeline there.
    m_cutAfterProbe = true;
    m_snapAfter = false;
    m_progressTimer.start();
    startProbe();
    return true;
}

void LosslessCutter::startCut()
{
    m_probeOnly = false;
    m_firstVideoPts = m_lastPts = -1;
    m_armed = false;
    m_part = QFileInfo(m_final).absolutePath() + QStringLiteral("/.") + QFileInfo(m_final).fileName() + QStringLiteral(".part");
    QFile::remove(m_part);
    m_pipe = gst_pipeline_new("lossless-cut");
    GstElement* src = gst_element_factory_make("filesrc", nullptr);
    m_parse = gst_element_factory_make("parsebin", nullptr);
    m_mux = gst_element_factory_make(m_muxName.toLatin1().constData(), "mux");
    GstElement* sink = gst_element_factory_make("filesink", nullptr);
    if (!src || !m_parse || !m_mux || !sink) {
        if (src) gst_object_unref(src);
        if (m_parse) gst_object_unref(m_parse);
        if (m_mux) gst_object_unref(m_mux);
        if (sink) gst_object_unref(sink);
        gst_object_unref(m_pipe);
        m_pipe = m_parse = m_mux = nullptr;
        finish(false, tr("GStreamer can't write %1 files here (%2 is missing).").arg(m_result.container, m_muxName));
        return;
    }
    g_object_set(src, "location", QFile::encodeName(m_source).constData(), nullptr);
    g_object_set(sink, "location", QFile::encodeName(m_part).constData(), "sync", FALSE, nullptr);
    gst_bin_add_many(GST_BIN(m_pipe), src, m_parse, m_mux, sink, nullptr);
    gst_element_link(src, m_parse);
    gst_element_link(m_mux, sink);
    g_signal_connect(m_parse, "pad-added", G_CALLBACK(onPadAdded), this);
    GstBus* bus = gst_element_get_bus(m_pipe);
    m_busWatch = gst_bus_add_watch(bus, onBus, this);
    gst_object_unref(bus);
    m_watchdog.start(30000);
    if (gst_element_set_state(m_pipe, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        finish(false, tr("The file could not be opened."));
}

void LosslessCutter::cancel()
{
    if (!m_pipe) return;
    m_result.cancelled = true;
    finish(false, tr("Cancelled"));
}

// ---- streaming-thread callbacks -------------------------------------------------------

void LosslessCutter::onPadAdded(GstElement*, GstPad* pad, gpointer self)
{
    auto* c = static_cast<LosslessCutter*>(self);
    QMutexLocker l(&c->m_lock);
    Stream s;
    gst_segment_init(&s.segment, GST_FORMAT_UNDEFINED);
    s.pad = GST_PAD(gst_object_ref(pad));
    // Until the seek to A has flushed this track, its packets are thrown away (the demuxer
    // runs on meanwhile, so every track gets to announce its format). Events pass.
    s.probe = gst_pad_add_probe(pad, GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST |
                                                     GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_EVENT_FLUSH),
                                onData, self, nullptr);
    c->m_streams.push_back(s);
    c->m_lastPadAdded.start();
    QMetaObject::invokeMethod(c, [c] { if (!c->m_capsWait.isActive() && !c->m_armed) { c->m_capsClock.start(); c->m_capsWait.start(); } },
                              Qt::QueuedConnection);
}

namespace {
// Matroska's SRT arrives as Pango markup (& < > escaped); muxers take plain UTF-8 text.
void unescapeMarkup(GstBuffer*& buf)
{
    GstMapInfo m;
    if (!gst_buffer_map(buf, &m, GST_MAP_READ)) return;
    QByteArray t(reinterpret_cast<const char*>(m.data), int(m.size));
    gst_buffer_unmap(buf, &m);
    if (!t.contains('&')) return;
    t.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"").replace("&apos;", "'").replace("&amp;", "&");
    GstBuffer* out = gst_buffer_new_memdup(t.constData(), gsize(t.size()));
    gst_buffer_copy_into(out, buf, GstBufferCopyFlags(GST_BUFFER_COPY_METADATA), 0, -1);
    gst_buffer_unref(buf);
    buf = out;
}

// Whether a packet is a B-frame, for tracks whose packets carry no display time (AVI):
// H.264 (slice_type of the first slice) and MPEG-4 Part 2 / Xvid (vop_coding_type).
// -1: can't tell.
int bFrameKind(GstBuffer* buf, const QByteArray& codec)
{
    GstMapInfo m;
    if (!gst_buffer_map(buf, &m, GST_MAP_READ)) return -1;
    const guint8* d = m.data;
    const gsize n = m.size;
    int result = -1;
    auto startCode = [&](gsize i) { return i + 3 < n && d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1; };
    if (codec == "video/mpeg") {
        for (gsize i = 0; i + 4 < n; ++i)
            if (startCode(i) && d[i + 3] == 0xB6) { result = (d[i + 4] >> 6) == 2 ? 1 : 0; break; }
    } else if (codec == "video/x-h264") {
        // NAL units: start codes (byte-stream) or 4-byte lengths (avc).
        QVector<gsize> nals;
        bool annexB = n > 4 && d[0] == 0 && d[1] == 0 && (d[2] == 1 || (d[2] == 0 && d[3] == 1));
        if (annexB) { for (gsize i = 0; i + 3 < n; ++i) if (startCode(i)) nals << i + 3; }
        else for (gsize i = 0; i + 4 < n;) { const gsize len = (gsize(d[i]) << 24) | (d[i + 1] << 16) | (d[i + 2] << 8) | d[i + 3]; nals << i + 4; i += 4 + len; }
        for (gsize p : nals) {
            if (p >= n) break;
            const int type = d[p] & 0x1f;
            if (type != 1 && type != 5) continue;
            // Exp-Golomb: first_mb_in_slice, then slice_type.
            gsize bit = (p + 1) * 8;
            auto readBit = [&]() -> int { if (bit / 8 >= n) return -1; const int b = (d[bit / 8] >> (7 - bit % 8)) & 1; ++bit; return b; };
            auto ue = [&]() -> long {
                int zeros = 0, b;
                while ((b = readBit()) == 0 && zeros < 32) ++zeros;
                if (b < 0) return -1;
                long v = 1;
                for (int k = 0; k < zeros; ++k) { const int x = readBit(); if (x < 0) return -1; v = (v << 1) | x; }
                return v - 1;
            };
            if (ue() < 0) break;
            const long st = ue();
            if (st >= 0) result = (st % 5) == 1 ? 1 : 0;
            break;
        }
    }
    gst_buffer_unmap(buf, &m);
    return result;
}
} // namespace

GstPadProbeReturn LosslessCutter::onData(GstPad* pad, GstPadProbeInfo* info, gpointer self)
{
    auto* c = static_cast<LosslessCutter*>(self);
    QMutexLocker l(&c->m_lock);
    Stream* st = nullptr;
    for (Stream& s : c->m_streams) if (s.pad == pad) st = &s;
    if (!st) return GST_PAD_PROBE_OK;

    if (info->type & (GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_EVENT_FLUSH)) {
        GstEvent* ev = GST_PAD_PROBE_INFO_EVENT(info);
        if (GST_EVENT_TYPE(ev) == GST_EVENT_FLUSH_STOP && c->m_armed) st->flushed = true;
        if (GST_EVENT_TYPE(ev) == GST_EVENT_SEGMENT && c->m_armed && !c->m_probeOnly && c->m_kRaw >= 0) {
            // Some demuxers start the section at A but send from the keyframe before it;
            // muxers would throw those frames away. The section starts at the keyframe.
            GstSegment seg;
            gst_event_copy_segment(ev, &seg);
            if (seg.format == GST_FORMAT_TIME && seg.rate > 0 && qint64(seg.start) > c->m_kRaw) {
                const guint64 d = seg.start - guint64(c->m_kRaw);
                seg.start = guint64(c->m_kRaw);
                seg.time = seg.time > d ? seg.time - d : 0;
                seg.position = seg.start;
                GstEvent* repl = gst_event_new_segment(&seg);
                gst_event_set_seqnum(repl, gst_event_get_seqnum(ev));
                gst_event_unref(ev);
                GST_PAD_PROBE_INFO_DATA(info) = repl;
                ev = repl;
            }
        }
        if (GST_EVENT_TYPE(ev) == GST_EVENT_SEGMENT) {
            gst_event_copy_segment(ev, &st->segment);
            if (qEnvironmentVariableIsSet("CRT_CUT_DEBUG"))
                qDebug() << "cut: segment" << st->mediaType << "start" << st->segment.start / 1e6 << "time" << st->segment.time / 1e6 << "armed" << c->m_armed;
        }
        if (GST_EVENT_TYPE(ev) == GST_EVENT_CAPS && st->markup) {
            GstEvent* repl = gst_event_new_caps(gst_caps_from_string("text/x-raw, format=(string)utf8"));
            gst_event_unref(ev);
            GST_PAD_PROBE_INFO_DATA(info) = repl;
        }
        return GST_PAD_PROBE_OK;
    }
    if (!(c->m_armed && st->flushed)) return GST_PAD_PROBE_DROP;   // before the section
    if (st->done) return GST_PAD_PROBE_DROP;
    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buf) return GST_PAD_PROBE_OK;   // (a buffer list: passed as it is)
    // Times on the player's timeline (stream time: e.g. MPEG-TS timestamps don't start at 0).
    auto stream = [st](GstClockTime ts) -> GstClockTime {
        if (!GST_CLOCK_TIME_IS_VALID(ts) || st->segment.format != GST_FORMAT_TIME || !GST_CLOCK_TIME_IS_VALID(st->segment.start)) return ts;
        const qint64 v = qint64(st->segment.time) + (qint64(ts) - qint64(st->segment.start));
        return GstClockTime(std::max<qint64>(0, v));
    };
    const GstClockTime pts = stream(GST_BUFFER_PTS(buf)), dts = stream(GST_BUFFER_DTS(buf));
    const GstClockTime t = GST_CLOCK_TIME_IS_VALID(pts) ? pts : dts;
    if (!GST_CLOCK_TIME_IS_VALID(t)) return GST_PAD_PROBE_OK;
    if (qEnvironmentVariableIsSet("CRT_CUT_DEBUG") && st->video && st->dbg++ < 4)
        qDebug() << "cut: video buffer pts" << GST_BUFFER_PTS(buf) / 1e6 << "dts" << GST_BUFFER_DTS(buf) / 1e6 << "stream" << t / 1e6
                 << (GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT) ? "delta" : "KEY");
    if (c->m_firstVideoPts < 0 && (st->video || !c->m_hasVideo)) {
        c->m_firstVideoPts = qint64(t);
        const GstClockTime raw = GST_BUFFER_PTS_IS_VALID(buf) ? GST_BUFFER_PTS(buf) : GST_BUFFER_DTS(buf);
        if (c->m_probeOnly) c->m_kRaw = qint64(raw);
        if (c->m_probeOnly) {
            const qint64 k = qint64(t);
            QMetaObject::invokeMethod(c, [c, k] {
                c->m_result.startNs = k;
                c->finish(true, QString());
            }, Qt::QueuedConnection);
        }
    }
    if (c->m_probeOnly || c->m_b <= 0) return GST_PAD_PROBE_OK;
    // The end. Video in decode order: the first packet decoded after B that also shows
    // after everything kept ends it. (A prefix of the stream, so nothing kept refers to a
    // dropped frame; the B-frames shown before the last kept frame are kept, so the picture
    // has no gap.) Other tracks by time.
    bool past;
    if (st->video && GST_CLOCK_TIME_IS_VALID(dts) && GST_CLOCK_TIME_IS_VALID(pts))
        past = qint64(dts) > c->m_b && qint64(pts) >= st->maxPts;
    else if (st->video && !GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(buf)) && qint64(t) > c->m_b)
        // No display times (AVI): the B-frames that follow the last kept frame in decode
        // order are shown before it, so they stay; the next I/P frame ends it.
        past = bFrameKind(buf, st->mediaType) != 1;
    else
        past = qint64(t) > c->m_b;
    if (st->video && !past && GST_CLOCK_TIME_IS_VALID(pts)) st->maxPts = std::max(st->maxPts, qint64(pts));
    if (past) {
        c->endStream(*st);
        // Once every video/audio track has ended, sparse ones (subtitles) end too.
        bool mainDone = true;
        for (const Stream& s : c->m_streams)
            if (s.kept && !s.done && s.kind != tr("subtitles") && s.kind != tr("data")) mainDone = false;
        if (mainDone) for (Stream& s : c->m_streams) if (s.kept && !s.done) c->endStream(s);
        return GST_PAD_PROBE_DROP;
    }
    if (st->markup) {
        unescapeMarkup(buf);
        GST_PAD_PROBE_INFO_DATA(info) = buf;
    }
    // A subtitle still showing at B ends at B (otherwise it would make the new file longer).
    if (st->kind == tr("subtitles") && GST_BUFFER_DURATION_IS_VALID(buf) && qint64(t + GST_BUFFER_DURATION(buf)) > c->m_b) {
        buf = gst_buffer_make_writable(buf);
        GST_BUFFER_DURATION(buf) = GstClockTime(std::max<qint64>(0, c->m_b - qint64(t)));
        GST_PAD_PROBE_INFO_DATA(info) = buf;
    }
    if (st->video || !c->m_hasVideo) c->m_lastPts = std::max(c->m_lastPts, qint64(t));
    return GST_PAD_PROBE_OK;
}

void LosslessCutter::endStream(Stream& s)
{
    s.done = true;
    if (GstPad* peer = gst_pad_get_peer(s.pad)) {   // the track's queue
        gst_pad_send_event(peer, gst_event_new_eos());
        gst_object_unref(peer);
    }
}

// ---- main thread ----------------------------------------------------------------------

void LosslessCutter::seekAndLink()
{
    if (!m_pipe) return;
    QVector<Stream> streams;
    {
        QMutexLocker l(&m_lock);
        streams = m_streams;
    }
    GstPad* seekPad = nullptr;
    for (Stream& s : streams) {
        GstCaps* caps = gst_pad_get_current_caps(s.pad);
        const GstStructure* st = caps ? gst_caps_get_structure(caps, 0) : nullptr;
        const QString media = st ? QString::fromUtf8(gst_structure_get_name(st)) : QString();
        s.video = media.startsWith("video/") || media.startsWith("image/");
        s.mediaType = media.toLatin1();
        s.kind = s.video ? tr("video") : media.startsWith("audio/") ? tr("audio")
               : (media.startsWith("text/") || media.startsWith("subpicture/") || media.startsWith("application/x-ssa") ||
                  media.startsWith("application/x-ass") || media.contains("subtitle") || media.contains("pgs") || media.contains("dvd")) ? tr("subtitles")
               : tr("data");
        if (caps) {
            gchar* d = gst_pb_utils_get_codec_description(caps);
            s.codec = d ? QString::fromUtf8(d) : media;
            g_free(d);
        } else {
            s.codec = tr("unknown");
        }
        const QString label = QStringLiteral("%1 (%2)").arg(s.kind, s.codec);
        GstPad* sinkPad = nullptr;
        if (!m_probeOnly && caps) sinkPad = gst_element_get_compatible_pad(m_mux, s.pad, caps);
        // Not as it is now, but the parser can hand it over another way (e.g. H.264 from
        // "avc" to the "byte-stream" form MPEG-TS wants): same packets, other framing.
        bool reformat = false;
        if (!m_probeOnly && caps && !sinkPad && st && (s.video || s.kind == tr("audio"))) {
            GstCaps* bare = gst_caps_new_empty_simple(gst_structure_get_name(st));
            for (GList* l = GST_ELEMENT_GET_CLASS(m_mux)->padtemplates; l && !sinkPad; l = l->next) {
                auto* t = GST_PAD_TEMPLATE(l->data);
                if (GST_PAD_TEMPLATE_DIRECTION(t) != GST_PAD_SINK) continue;
                GstCaps* tc = gst_pad_template_get_caps(t);
                if (gst_caps_can_intersect(tc, bare)) {
                    sinkPad = gst_element_request_pad(m_mux, t, nullptr, nullptr);
                    reformat = sinkPad != nullptr;
                }
                gst_caps_unref(tc);
            }
            gst_caps_unref(bare);
        }
        s.markup = false;
        if (!m_probeOnly && !sinkPad && st && media == QLatin1String("text/x-raw") &&
            QString::fromUtf8(gst_structure_get_string(st, "format")) == QLatin1String("pango-markup")) {
            // SRT from Matroska: offered to the muxer as plain UTF-8 text (unescaped in onData).
            GstCaps* plain = gst_caps_from_string("text/x-raw, format=(string)utf8");
            GstPadTemplate* tmpl = nullptr;
            for (GList* l = GST_ELEMENT_GET_CLASS(m_mux)->padtemplates; l && !tmpl; l = l->next) {
                auto* t = GST_PAD_TEMPLATE(l->data);
                if (GST_PAD_TEMPLATE_DIRECTION(t) != GST_PAD_SINK) continue;
                GstCaps* tc = gst_pad_template_get_caps(t);
                if (gst_caps_can_intersect(tc, plain)) tmpl = t;
                gst_caps_unref(tc);
            }
            if (tmpl) {
                sinkPad = gst_element_request_pad(m_mux, tmpl, nullptr, plain);
                s.markup = sinkPad != nullptr;
            }
            gst_caps_unref(plain);
        }
        bool linked = false;
        if (sinkPad) {
            // A queue per track: the demuxer feeds every track from one thread, and the
            // muxer waits for data on all of them.
            GstElement* q = gst_element_factory_make("queue", nullptr);
            g_object_set(q, "max-size-buffers", 0u, "max-size-bytes", 0u, "max-size-time", guint64(10 * GST_SECOND), nullptr);
            gst_bin_add(GST_BIN(m_pipe), q);
            // Another framing of the same packets (H.264/HEVC "avc" <-> "byte-stream"): a
            // second parser of our own converts (parsebin's own is fixed to what it chose).
            GstElement* conv = nullptr;
            if (reformat && st) {
                const QByteArray name = gst_structure_get_name(st);
                const char* parser = name == "video/x-h264" ? "h264parse" : name == "video/x-h265" ? "h265parse" : nullptr;
                if (parser && (conv = gst_element_factory_make(parser, nullptr))) gst_bin_add(GST_BIN(m_pipe), conv);
            }
            GstPad* qs = gst_element_get_static_pad(q, "sink");
            GstPad* qo = gst_element_get_static_pad(q, "src");
            linked = gst_pad_link(qo, sinkPad) == GST_PAD_LINK_OK;
            if (linked && conv) {
                GstPad* cs = gst_element_get_static_pad(conv, "sink");
                GstPad* co = gst_element_get_static_pad(conv, "src");
                linked = gst_pad_link(co, qs) == GST_PAD_LINK_OK && gst_pad_link(s.pad, cs) == GST_PAD_LINK_OK;
                gst_object_unref(cs);
                gst_object_unref(co);
                gst_element_sync_state_with_parent(conv);
            } else if (linked) {
                linked = gst_pad_link_full(s.pad, qs, s.markup ? GST_PAD_LINK_CHECK_NOTHING : GST_PAD_LINK_CHECK_DEFAULT) == GST_PAD_LINK_OK;
            }
            gst_object_unref(qs);
            gst_object_unref(qo);
            gst_element_sync_state_with_parent(q);
            if (!linked) gst_element_release_request_pad(m_mux, sinkPad);
        }
        s.kept = linked;
        if (linked) {
            m_result.kept << label;
        } else {
            // Not wanted (keyframe probe) or not something this container can hold: discarded.
            GstElement* fake = gst_element_factory_make("fakesink", nullptr);
            g_object_set(fake, "sync", FALSE, "async", FALSE, nullptr);
            gst_bin_add(GST_BIN(m_pipe), fake);
            gst_element_sync_state_with_parent(fake);
            GstPad* fp = gst_element_get_static_pad(fake, "sink");
            gst_pad_link(s.pad, fp);
            gst_object_unref(fp);
            if (!m_probeOnly) m_result.dropped << label;
            if (qEnvironmentVariableIsSet("CRT_CUT_DEBUG") && caps) qDebug() << "cut: dropped" << gst_caps_to_string(caps);
        }
        if (sinkPad) gst_object_unref(sinkPad);
        if (caps) gst_caps_unref(caps);
        if (s.video && !seekPad) seekPad = s.pad;
    }
    {
        QMutexLocker l(&m_lock);
        for (int i = 0; i < streams.size() && i < m_streams.size(); ++i) {
            m_streams[i].video = streams[i].video;
            m_streams[i].kind = streams[i].kind;
            m_streams[i].codec = streams[i].codec;
            m_streams[i].kept = streams[i].kept;
            m_streams[i].markup = streams[i].markup;
            m_streams[i].mediaType = streams[i].mediaType;
            m_streams[i].flushed = false;   // packets pass once the seek's flush has come by
        }
        m_hasVideo = seekPad != nullptr;
        m_armed = true;
    }
    if (!m_probeOnly && m_result.kept.isEmpty()) {
        finish(false, tr("None of the tracks can be copied into a %1 file.").arg(m_result.container));
        return;
    }
    if (!seekPad && !streams.isEmpty()) seekPad = streams.first().pad;
    // To the keyframe at or before A (the end is cut in onData). After the seek's flush,
    // each track's packets flow into the muxer.
    const GstSeekFlags flags = GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT |
                                            (m_probeOnly && m_snapAfter ? GST_SEEK_FLAG_SNAP_AFTER : GST_SEEK_FLAG_SNAP_BEFORE));
    GstEvent* seek = gst_event_new_seek(1.0, GST_FORMAT_TIME, flags, GST_SEEK_TYPE_SET, m_a, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
    const bool ok = seekPad && gst_pad_send_event(seekPad, seek);
    if (!seekPad) gst_event_unref(seek);
    if (!ok && m_a > 0) finish(false, tr("This file can't be searched, so it can't be cut without re-encoding."));
}

gboolean LosslessCutter::onBus(GstBus*, GstMessage* m, gpointer self)
{
    auto* c = static_cast<LosslessCutter*>(self);
    switch (GST_MESSAGE_TYPE(m)) {
    case GST_MESSAGE_EOS:
        if (!c->m_probeOnly && c->m_lastPts < 0) c->finish(false, tr("There is nothing to copy between A and B (past the end of the video?)."));
        else if (!c->m_probeOnly) c->finish(true, QString());
        else c->finish(false, tr("No keyframe found"));
        break;
    case GST_MESSAGE_ERROR: {
        GError* err = nullptr;
        gchar* dbg = nullptr;
        gst_message_parse_error(m, &err, &dbg);
        const QString msg = err ? QString::fromUtf8(err->message) : QStringLiteral("GStreamer error");
        qWarning() << "Lossless cut:" << msg << (dbg ? dbg : "");
        g_clear_error(&err);
        g_free(dbg);
        c->finish(false, msg);
        break;
    }
    case GST_MESSAGE_STATE_CHANGED:
    case GST_MESSAGE_ASYNC_DONE:
        if (c->m_armed) c->m_watchdog.start();   // alive
        break;
    default:
        break;
    }
    return TRUE;
}

void LosslessCutter::teardown()
{
    m_progressTimer.stop();
    m_watchdog.stop();
    if (m_pipe) {
        gst_element_set_state(m_pipe, GST_STATE_NULL);
        if (m_busWatch) g_source_remove(m_busWatch);
        m_busWatch = 0;
        QMutexLocker l(&m_lock);
        for (Stream& s : m_streams) if (s.pad) gst_object_unref(s.pad);
        m_streams.clear();
        gst_object_unref(m_pipe);
    }
    m_pipe = m_parse = m_mux = nullptr;
    m_armed = false;
    m_capsWait.stop();
}

void LosslessCutter::finish(bool ok, const QString& error)
{
    if (!m_pipe && m_probeOnly && !m_cutAfterProbe) { emit keyframeFound(m_a, -1); return; }
    if (!m_pipe && m_probeOnly && m_cutAfterProbe) { m_cutAfterProbe = false; m_probeOnly = false; }
    if (!m_pipe && !error.isEmpty() && m_result.error.isEmpty()) {   // failed before starting
        m_result.ok = false;
        m_result.error = error;
        const Result r = m_result;
        QMetaObject::invokeMethod(this, [this, r] { emit finished(r); }, Qt::QueuedConnection);
        return;
    }
    if (!m_pipe) return;
    const bool probe = m_probeOnly;
    teardown();
    if (probe) {
        if (m_cutAfterProbe) {
            m_cutAfterProbe = false;
            if (!ok) m_kRaw = -1;   // no keyframe found: cut as the demuxer sees fit
            startCut();
            return;
        }
        emit keyframeFound(m_a, ok ? m_result.startNs : -1);
        return;
    }
    m_result.ok = ok;
    m_result.error = error;
    if (ok) {
        m_result.startNs = m_firstVideoPts >= 0 ? m_firstVideoPts : m_a;
        // Completed: give it its name, never over an existing file.
        QString target = m_final;
        for (int n = 2; !QFile::rename(m_part, target); ++n) {
            if (!QFileInfo::exists(target) || n > 99) {
                m_result.ok = false;
                m_result.error = tr("Could not save %1").arg(target);
                break;
            }
            const QFileInfo fi(m_final);
            target = fi.absolutePath() + '/' + QStringLiteral("%1 (%2).%3").arg(fi.completeBaseName()).arg(n).arg(fi.suffix());
        }
        if (m_result.ok) {
            m_result.output = target;
            m_result.bytes = QFileInfo(target).size();
            if (GstDiscoverer* d = gst_discoverer_new(5 * GST_SECOND, nullptr)) {
                gchar* uri = gst_filename_to_uri(QFile::encodeName(target).constData(), nullptr);
                GstDiscovererInfo* info = uri ? gst_discoverer_discover_uri(d, uri, nullptr) : nullptr;
                g_free(uri);
                if (info) {
                    m_result.durationNs = qint64(gst_discoverer_info_get_duration(info));
                    g_object_unref(info);
                }
                g_object_unref(d);
            }
        }
    }
    if (!m_result.ok) QFile::remove(m_part);
    m_part.clear();
    emit finished(m_result);
}
