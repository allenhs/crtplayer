#include <functional>
#include "Player.h"
#include "Distro.h"
#include "TapeAudio.h"
#include "ShrinkFilter.h"
#include "FrameGovernor.h"
#include "WebSource.h"

#include <QFileInfo>
#include <cmath>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QUrl>
#include <QDebug>
#include <gst/app/gstappsink.h>
#include <gst/audio/streamvolume.h>
#include <gst/pbutils/pbutils.h>
#include <gst/tag/tag.h>
#include <gst/video/video.h>

namespace {
constexpr int kFlagVideo = 1 << 0;
constexpr int kFlagAudio = 1 << 1;
constexpr int kFlagText = 1 << 2;

// "Nothing for this stretch" notices (GAP events) that must not go on: see where these are installed.
GstPadProbeReturn dropBrokenGap(GstPad*, GstPadProbeInfo* info, gpointer)
{
    GstEvent* ev = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(ev) != GST_EVENT_GAP) return GST_PAD_PROBE_OK;
    GstClockTime at = GST_CLOCK_TIME_NONE, length = GST_CLOCK_TIME_NONE;
    gst_event_parse_gap(ev, &at, &length);
    const GstClockTime sane = GstClockTime(1000) * 3600 * GST_SECOND;   // (a thousand hours)
    const bool broken = !GST_CLOCK_TIME_IS_VALID(at) || at > sane || (GST_CLOCK_TIME_IS_VALID(length) && length > sane);
    return broken ? GST_PAD_PROBE_DROP : GST_PAD_PROBE_OK;
}

GstPadProbeReturn dropGap(GstPad*, GstPadProbeInfo* info, gpointer)
{
    return GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) == GST_EVENT_GAP ? GST_PAD_PROBE_DROP : GST_PAD_PROBE_OK;
}

void onMatroskaPad(GstElement*, GstPad* pad, gpointer)
{
    gchar* name = gst_pad_get_name(pad);
    const bool video = name && g_str_has_prefix(name, "video_");
    g_free(name);
    if (video) gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, &dropGap, nullptr, nullptr);
}
constexpr int kFlagDeinterlace = 1 << 9;
constexpr int kFlagSoftColorbalance = 1 << 10;

bool klassHas(GstElementFactory* f, const char* a, const char* b = nullptr, const char* c = nullptr)
{
    const gchar* k = f ? gst_element_factory_get_metadata(f, GST_ELEMENT_METADATA_KLASS) : nullptr;
    if (!k) return false;
    return strstr(k, a) && (!b || strstr(k, b)) && (!c || strstr(k, c));
}

bool isHwVideoDecoderFactory(GstElementFactory* f) { return klassHas(f, "Decoder", "Video", "Hardware"); }

QHash<QString, guint>& originalRanks()
{
    static QHash<QString, guint> h;
    return h;
}

QString tagString(const GstTagList* tags, const char* tag)
{
    gchar* s = nullptr;
    QString out;
    if (tags && gst_tag_list_get_string(tags, tag, &s)) {
        out = QString::fromUtf8(s);
        g_free(s);
    }
    return out;
}

QString trackLabel(int idx, const GstTagList* tags, const char* codecTag)
{
    QString label = QStringLiteral("Track %1").arg(idx + 1);
    const QString code = tagString(tags, GST_TAG_LANGUAGE_CODE);
    QString lang = tagString(tags, GST_TAG_LANGUAGE_NAME);
    if (lang.isEmpty() && !code.isEmpty()) {
        const gchar* n = gst_tag_get_language_name(code.toUtf8().constData());
        lang = n ? QString::fromUtf8(n) : code;
    }
    const QString title = tagString(tags, GST_TAG_TITLE);
    const QString codec = tagString(tags, codecTag);
    QStringList parts;
    if (!lang.isEmpty()) parts << lang;
    if (!title.isEmpty() && title != lang) parts << title;
    if (!parts.isEmpty()) label += QStringLiteral(": ") + parts.join(QStringLiteral(", "));
    if (!codec.isEmpty()) label += QStringLiteral(" (%1)").arg(codec);
    return label;
}

// The track's language as a short code ("en"; a three-letter code when there is no
// two-letter one), lower case; empty when the file doesn't say.
QString trackLang(const GstTagList* tags)
{
    const QString code = tagString(tags, GST_TAG_LANGUAGE_CODE).toLower();
    if (code.isEmpty() || code == QLatin1String("und")) return QString();
    const gchar* two = gst_tag_get_language_code_iso_639_1(code.toUtf8().constData());
    return two ? QString::fromUtf8(two) : code;
}
} // namespace

Player::Player(QObject* parent) : QObject(parent)
{
    // The end of a video is the pipeline's end-of-stream message. It does not come when one of
    // the streams never reports its end: a subtitle track switched to after the file's last
    // subtitle line had already been read, for one (playbin then waits for an end that was
    // delivered before the switch). The picture has ended, the position stands still: after a
    // second and a half of that, the video is over.
    m_endWatch.setInterval(500);
    connect(&m_endWatch, &QTimer::timeout, this, [this] {
        if (!m_pipe || !m_loaded || m_state != State::Playing || m_seekInFlight || !m_videoDone || m_endReported) { m_endStill = 0; return; }
        const qint64 pos = position();
        m_endStill = pos == m_endLastPos ? m_endStill + 1 : 0;
        m_endLastPos = pos;
        if (m_endStill >= 3) {
            m_endReported = true;
            emit endOfStream();
        }
    });
    m_endWatch.start();
    m_wall.start();
    m_netWatch.setInterval(250);
    connect(&m_netWatch, &QTimer::timeout, this, [this] { watchNetwork(); });
    m_netWatch.start();
    m_seekWatchdog.setSingleShot(true);
    m_seekWatchdog.setInterval(1500);
    connect(&m_seekWatchdog, &QTimer::timeout, this, [this] {
        // A flushing seek should always complete with ASYNC_DONE; never leave the UI stuck.
        m_seekInFlight = false;
        if (m_hasPendingSeek) { m_hasPendingSeek = false; doSeek(m_pendingSeek, m_pendingMode); }
        else if (m_refreshWanted) { m_refreshWanted = false; refreshSubtitles(); }
    });
    // The subtitle lines the player keeps itself: read some minutes ahead of the picture as it moves on.
    m_feed.setNotify([this] { QMetaObject::invokeMethod(this, [this] { onFeedNotify(); }, Qt::QueuedConnection); });
    m_feedTimer.setInterval(500);
    connect(&m_feedTimer, &QTimer::timeout, this, [this] { if (m_pipe && m_loaded && m_feedOn) m_feed.playedTo(position()); });
    m_feedTimer.start();
    m_feedRefresh.setSingleShot(true);
    m_feedRefresh.setInterval(250);
    connect(&m_feedRefresh, &QTimer::timeout, this, [this] { refreshSubtitles(); });
}

Player::~Player()
{
    m_feed.setNotify(nullptr);
    teardown();
    if (!m_webUri.isEmpty()) crt_web_forget(m_webUri);
}

QStringList Player::missingEssentialElements()
{
    // Only what the player cannot run without: all of it is in GStreamer core / base, which
    // every distribution installs with GStreamer itself.
    struct Req { const char* element; Distro::Set set; };
    static const Req reqs[] = {
        {"playbin", Distro::Set::Base}, {"appsink", Distro::Set::Base}, {"videoconvert", Distro::Set::Base},
        {"audioconvert", Distro::Set::Base}, {"audioresample", Distro::Set::Base}, {"typefind", Distro::Set::Base},
    };
    QStringList missing;
    for (const auto& r : reqs) {
        GstElementFactory* f = gst_element_factory_find(r.element);
        if (!f) missing << QStringLiteral("%1 (%2)").arg(QString::fromUtf8(r.element), Distro::package(r.set));
        else gst_object_unref(f);
    }
    return missing;
}

QList<Player::Gap> Player::missingRecommended()
{
    auto have = [](std::initializer_list<const char*> names) {
        for (const char* n : names)
            if (GstElementFactory* f = gst_element_factory_find(n)) { gst_object_unref(f); return true; }
        return false;
    };
    using S = Distro::Set;
    QList<Gap> gaps;
    if (!have({"qtdemux"})) gaps.append({tr("MP4 / MOV files"), Distro::package(S::Good)});
    if (!have({"matroskademux"})) gaps.append({tr("MKV / WebM files"), Distro::package(S::Good)});
    if (!have({"avdec_h264", "openh264dec", "vah264dec", "vaapih264dec", "nvh264dec", "v4l2h264dec"}))
        gaps.append({tr("H.264 video (most videos)"), Distro::package(S::Libav)});
    if (!have({"avdec_aac", "faad", "fdkaacdec"})) gaps.append({tr("AAC audio (most videos)"), Distro::package(S::Libav)});
    if (!have({"souphttpsrc"})) gaps.append({tr("network streams (Jellyfin)"), Distro::package(S::Good)});
    if (!have({"autoaudiosink"})) gaps.append({tr("automatic audio output (falls back to PipeWire, PulseAudio or ALSA)"), Distro::package(S::Good)});
    if (!have({"scaletempo"})) gaps.append({tr("natural-sounding speed changes"), Distro::package(S::Good)});
    if (!have({"textoverlay"})) gaps.append({tr("subtitles (drawing their text)"), Distro::package(S::Text)});
    return gaps;
}

void Player::localFormats(QStringList* containers, QStringList* videoCodecs, QStringList* audioCodecs)
{
    GList* decoders = gst_element_factory_list_get_elements(GST_ELEMENT_FACTORY_TYPE_DECODER, GST_RANK_MARGINAL);
    auto canDecode = [decoders](const char* capsStr) {
        GstCaps* caps = gst_caps_from_string(capsStr);
        bool ok = false;
        for (GList* l = decoders; l && !ok; l = l->next)
            ok = gst_element_factory_can_sink_any_caps(GST_ELEMENT_FACTORY(l->data), caps);
        gst_caps_unref(caps);
        return ok;
    };
    auto have = [](const char* name) {
        GstElementFactory* f = gst_element_factory_find(name);
        if (f) gst_object_unref(f);
        return f != nullptr;
    };
    static const std::pair<const char*, const char*> video[] = {
        {"h264", "video/x-h264"}, {"hevc", "video/x-h265"}, {"vp8", "video/x-vp8"}, {"vp9", "video/x-vp9"},
        {"av1", "video/x-av1"}, {"mpeg2video", "video/mpeg, mpegversion=(int)2, systemstream=(boolean)false"},
        {"mpeg1video", "video/mpeg, mpegversion=(int)1, systemstream=(boolean)false"},
        {"mpeg4", "video/mpeg, mpegversion=(int)4, systemstream=(boolean)false"},
        {"msmpeg4v3", "video/x-msmpeg, msmpegversion=(int)43"}, {"vc1", "video/x-wmv, wmvversion=(int)3"},
        {"wmv3", "video/x-wmv, wmvversion=(int)3"}, {"mjpeg", "image/jpeg"}, {"theora", "video/x-theora"},
        {"h263", "video/x-h263"}};
    static const std::pair<const char*, const char*> audio[] = {
        {"aac", "audio/mpeg, mpegversion=(int)4"}, {"mp3", "audio/mpeg, mpegversion=(int)1, layer=(int)3"},
        {"mp2", "audio/mpeg, mpegversion=(int)1, layer=(int)2"}, {"ac3", "audio/x-ac3"}, {"eac3", "audio/x-eac3"},
        {"dts", "audio/x-dts"}, {"truehd", "audio/x-true-hd"}, {"flac", "audio/x-flac"}, {"opus", "audio/x-opus"},
        {"vorbis", "audio/x-vorbis"}, {"alac", "audio/x-alac"}, {"wmav2", "audio/x-wma, wmaversion=(int)2"},
        {"wmapro", "audio/x-wma, wmaversion=(int)3"}, {"amr_nb", "audio/AMR"}};
    for (const auto& v : video) if (canDecode(v.second)) videoCodecs->append(QString::fromLatin1(v.first));
    for (const auto& a : audio) if (canDecode(a.second)) audioCodecs->append(QString::fromLatin1(a.first));
    // Uncompressed PCM always plays.
    *audioCodecs << QStringLiteral("pcm_s16le") << QStringLiteral("pcm_s24le") << QStringLiteral("pcm_s16be") << QStringLiteral("pcm_s24be");
    static const std::pair<const char*, const char*> demux[] = {
        {"matroskademux", "mkv,webm"}, {"qtdemux", "mp4,m4v,mov,3gp"}, {"avidemux", "avi"},
        {"tsdemux", "ts,mpegts,m2ts"}, {"mpegpsdemux", "mpeg,mpg,vob"}, {"oggdemux", "ogg,ogv"},
        {"flvdemux", "flv"}, {"asfdemux", "asf,wmv"}};
    for (const auto& d : demux) if (have(d.first)) *containers << QString::fromLatin1(d.second).split(',');
    gst_plugin_feature_list_free(decoders);
}

// Without a working autoaudiosink (missing, or no sound device), playbin has no audio output: pick
// the first sink that can actually open, and fall back to silent (but timed) playback.
static GstElement* fallbackAudioSink(QString* name)
{
    // The automatic output is used when it can open a device; with no sound device at all (no
    // speakers, audio service stopped) it would stop playback with an error instead.
#ifdef _WIN32
    // Windows' sinks open without a device and only fail once playing starts: ask for the
    // output devices instead, and play silently (still timed) when there are none.
    {
        GstDeviceMonitor* mon = gst_device_monitor_new();
        gst_device_monitor_add_filter(mon, "Audio/Sink", nullptr);
        GList* devs = gst_device_monitor_get_devices(mon);
        const bool any = devs != nullptr;
        g_list_free_full(devs, gst_object_unref);
        gst_object_unref(mon);
        if (!any) {
            GstElement* f = gst_element_factory_make("fakesink", nullptr);
            if (f) g_object_set(f, "sync", TRUE, nullptr);
            *name = QStringLiteral("none");
            return f;
        }
    }
#endif
    if (GstElement* a = gst_element_factory_make("autoaudiosink", nullptr)) {
        const bool ok = gst_element_set_state(a, GST_STATE_READY) != GST_STATE_CHANGE_FAILURE;
        gst_element_set_state(a, GST_STATE_NULL);
        gst_object_unref(a);
        if (ok) { *name = QStringLiteral("automatic"); return nullptr; }
    }
#ifdef _WIN32
    for (const char* n : {"wasapi2sink", "wasapisink", "directsoundsink"}) {
#else
    for (const char* n : {"pipewiresink", "pulsesink", "alsasink", "osssink"}) {
#endif
        GstElement* e = gst_element_factory_make(n, nullptr);
        if (!e) continue;
        const bool ok = gst_element_set_state(e, GST_STATE_READY) != GST_STATE_CHANGE_FAILURE;
        gst_element_set_state(e, GST_STATE_NULL);
        if (ok) { *name = QString::fromUtf8(n); return e; }
        gst_object_unref(e);
    }
    GstElement* f = gst_element_factory_make("fakesink", nullptr);
    if (f) g_object_set(f, "sync", TRUE, nullptr);
    *name = QStringLiteral("none");
    return f;
}

void Player::applyDecoderPolicy(bool allowHw)
{
    GList* list = gst_registry_get_feature_list(gst_registry_get(), GST_TYPE_ELEMENT_FACTORY);
    for (GList* l = list; l; l = l->next) {
        auto* f = GST_ELEMENT_FACTORY(l->data);
        if (!isHwVideoDecoderFactory(f)) continue;
        const QString name = QString::fromUtf8(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(f)));
        auto& ranks = originalRanks();
        if (!ranks.contains(name)) ranks.insert(name, gst_plugin_feature_get_rank(GST_PLUGIN_FEATURE(f)));
        const guint orig = ranks.value(name);
        guint rank = orig;
        if (!allowHw) {
            rank = GST_RANK_NONE;
        } else if (name.startsWith("vulkan")) {
            // Vulkan decoders output Vulkan images only; this sink needs system memory.
            rank = orig;
        } else if (orig > GST_RANK_NONE) {
            // Enabled hardware decoders should win over libav/dav1d (PRIMARY).
            // The modern "va" plugin is preferred over the legacy "vaapi" one.
            const bool modernVa = name.startsWith("va") && !name.startsWith("vaapi");
            rank = std::max<guint>(orig, GST_RANK_PRIMARY + (modernVa ? 3 : 2));
        }
        gst_plugin_feature_set_rank(GST_PLUGIN_FEATURE(f), rank);
    }
    gst_plugin_feature_list_free(list);
}

QStringList Player::availableHardwareDecoders()
{
    QStringList out;
    GList* list = gst_registry_get_feature_list(gst_registry_get(), GST_TYPE_ELEMENT_FACTORY);
    for (GList* l = list; l; l = l->next) {
        auto* f = GST_ELEMENT_FACTORY(l->data);
        if (isHwVideoDecoderFactory(f))
            out << QString::fromUtf8(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(f)));
    }
    gst_plugin_feature_list_free(list);
    out.sort();
    return out;
}

QString Player::currentPath() const
{
    const QUrl u(m_uri);
    return u.isLocalFile() ? u.toLocalFile() : m_uri;
}

bool Player::open(const QString& in, bool autoplay, qint64 startNs)
{
    QString uri = in;
    if (!in.contains(QStringLiteral("://")))
        uri = QUrl::fromLocalFile(QFileInfo(in).absoluteFilePath()).toString(QUrl::FullyEncoded);
    m_hwRetried = false;
    m_carrySel = -1;
    return openInternal(uri, autoplay, startNs, false);
}

// A video from a web site: one stream, or the picture and the sound as two that belong together (WebSource.h).
bool Player::openWeb(const QList<WebStream>& streams, bool autoplay, qint64 startNs)
{
    if (streams.isEmpty()) return false;
    const QString before = m_webUri;
    m_webUri = crt_web_register(streams);
    const bool ok = open(m_webUri, autoplay, startNs);
    if (!before.isEmpty()) crt_web_forget(before);   // (the pipeline that read them is gone by now)
    return ok;
}

QJsonObject Player::webReport() const
{
    QJsonObject o = crt_web_is(m_uri) ? crt_web_report(m_uri) : QJsonObject{{"streams", 0}};
    o["sourceError"] = m_sourceError;
    o["waiting"] = m_waiting;
    o["waits"] = m_waits;
    if (m_webSource) {
        const QList<WebLevel> levels = crt_web_levels(m_webSource);
        if (!levels.isEmpty()) {
            o["aheadBytes"] = double(levels.first().ahead);
            o["receivedBytes"] = double(levels.first().received);
            o["ended"] = levels.first().ended;
            int requests = 0;
            for (const WebLevel& l : levels) requests += l.requests;
            o["requests"] = requests;
        }
    }
    return o;
}

void Player::setChapters(const QVector<ChapterInfo>& chapters)
{
    m_chapters = chapters;
    std::sort(m_chapters.begin(), m_chapters.end(), [](const ChapterInfo& a, const ChapterInfo& b) { return a.startNs < b.startNs; });
    emit chaptersChanged();
}

bool Player::openInternal(const QString& uri, bool autoplay, qint64 startNs, bool isRetry)
{
    teardown();
    if (!isRetry) applyDecoderPolicy(m_hwEnabled);
    m_uri = uri;
    m_targetPlaying = autoplay;
    m_governor.reset();
    m_governor.setEnabled(m_governorOn && !qEnvironmentVariableIsSet("CRTPLAYER_GOVERNOR_OFF"));
    m_governor.setPlaying(autoplay);
    m_pictureNs = 0;
    m_startPos = startNs;
    m_loaded = false;
    m_duration = -1;
    m_missing.clear();
    m_audioTracks.clear();
    m_textTracks.clear();
    m_chapters.clear();
    emit chaptersChanged();
    m_nVideo = 0;
    m_videoCodec.clear(); m_audioCodec.clear(); m_container.clear();
    m_orientationTag = QStringLiteral("rotate-0");
    emit orientationChanged(m_orientationTag);
    ++m_generation;

    if (m_output == Output::RgbScaled) m_output = Output::Rgb;   // (a size chosen for the last video does not fit the next)
    m_fastSize = QSize();
    m_pipe = gst_element_factory_make("playbin", "crtplayer");
    // Scaling first, in the video's own format, then conversion at the smaller size: measured
    // half as fast again as the two in one element for 4K shown at 1080p. Both are multi-threaded.
    GstElement* conv = gst_element_factory_make("videoconvert", nullptr);
    GstElement* scale = gst_element_factory_make("videoscale", nullptr);   // (optional: without it, no fast path)
    // 2.17: first of all, the player's own filter. When frames are converted on the CPU it shrinks the picture by a
    // whole factor and to 8 bits a sample in one cheap pass, so that the scaler and the converter get a small
    // 8-bit picture (ShrinkFilter.h). (CRTPLAYER_SHRINK_OFF: as before 2.17.)
    GstElement* shrink = qEnvironmentVariableIsSet("CRTPLAYER_SHRINK_OFF") ? nullptr : crt_shrink_new();
    GstElement* capsf = gst_element_factory_make("capsfilter", nullptr);
    // The size and format asked for change while the video plays (the window is resized, a look
    // is switched on). Frames already on their way in the old format must still be let through,
    // or the stream stops with "not negotiated".
    if (capsf) gst_util_set_object_arg(G_OBJECT(capsf), "caps-change-mode", "delayed");
    m_appsink = gst_element_factory_make("appsink", "crtsink");
    if (!m_pipe || !conv || !m_appsink || !capsf) {
        if (conv) gst_object_unref(conv);
        if (scale) gst_object_unref(scale);
        if (shrink) gst_object_unref(shrink);
        if (capsf) gst_object_unref(capsf);
        if (m_appsink) gst_object_unref(m_appsink);
        if (m_pipe) gst_object_unref(m_pipe);
        m_pipe = m_appsink = nullptr;
        m_tape = nullptr;
        setState(State::Error);
        emit errorOccurred(tr("GStreamer is incomplete"),
                           tr("Required elements are missing:\n%1\n\nInstall with:\n  %2")
                               .arg(missingEssentialElements().join('\n'), Distro::installCommand({Distro::package(Distro::Set::Base)})));
        return false;
    }

    // Video sink: videoconvert only converts when upstream cannot provide one of the
    // formats the shader understands (NV12/I420/RGBx variants); otherwise it is passthrough.
    GstElement* bin = gst_bin_new("crt-video-sink");
    // n-threads 0: as many as there are CPU cores.
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(conv), "n-threads"))
        g_object_set(conv, "n-threads", 0u, nullptr);
    if (scale && g_object_class_find_property(G_OBJECT_GET_CLASS(scale), "n-threads"))
        g_object_set(scale, "n-threads", 0u, nullptr);
    // The multi-tap bilinear filter: shrinking 4K to a window with the default two-tap one leaves
    // fine detail jagged and noisy; this one averages every source pixel in (6% slower, measured).
    if (scale) gst_util_set_object_arg(G_OBJECT(scale), "method", "bilinear2");
    // The whole frame onto the whole size asked for. (By default the scaler adds borders when the
    // shapes differ by a rounding, which shrank an anamorphic picture by a fraction of a line.)
    if (scale) g_object_set(scale, "add-borders", FALSE, nullptr);
    GstCaps* caps = gst_caps_from_string("video/x-raw, format=(string){ NV12, I420, BGRx, BGRA, RGBx, RGBA }");
    g_object_set(m_appsink, "caps", caps, "sync", TRUE, "max-buffers", 2u, "drop", TRUE,
                 "enable-last-sample", FALSE, "qos", TRUE, nullptr);
    gst_caps_unref(caps);
    GstAppSinkCallbacks cb{};
    cb.new_sample = [](GstAppSink* s, gpointer self) { return onNewSample(GST_ELEMENT(s), self); };
    cb.new_preroll = [](GstAppSink* s, gpointer self) { return onNewPreroll(GST_ELEMENT(s), self); };
    gst_app_sink_set_callbacks(GST_APP_SINK(m_appsink), &cb, this, nullptr);
    gst_bin_add_many(GST_BIN(bin), conv, capsf, m_appsink, nullptr);
    if (scale) {
        gst_bin_add(GST_BIN(bin), scale);
        gst_element_link_many(scale, conv, capsf, m_appsink, nullptr);
    } else {
        gst_element_link_many(conv, capsf, m_appsink, nullptr);
    }
    GstElement* head = scale ? scale : conv;   // the first element of the chain: the video as decoded arrives at its sink pad
    if (shrink) {
        gst_bin_add(GST_BIN(bin), shrink);
        gst_element_link(shrink, head);
        head = shrink;
    }
    m_capsFilter = capsf;
    m_shrink = shrink;
    m_canScale = scale != nullptr;
    {
        QMutexLocker lock(&m_mutex);
        m_natW = m_natH = 0;
        m_natFormat.clear();
    }
    applyFastOutput();
    GstPad* pad = gst_element_get_static_pad(head, "sink");
    // The video as decoded is seen here, whatever is delivered after conversion and scaling.
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, &Player::onSinkEvent, this, nullptr);
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, &Player::onSinkBuffer, this, nullptr);
    // What the sink says about each picture it shows (how early it was there) goes to the governor.
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM, [](GstPad*, GstPadProbeInfo* info, gpointer self) -> GstPadProbeReturn {
        GstEvent* ev = GST_PAD_PROBE_INFO_EVENT(info);
        if (GST_EVENT_TYPE(ev) == GST_EVENT_QOS) {
            auto* p = static_cast<Player*>(self);
            GstClockTimeDiff late = 0;
            gst_event_parse_qos(ev, nullptr, nullptr, &late, nullptr);
            // (at twice the speed a picture has half the time)
            p->m_governor.arrived(-late, gint64(double(p->m_pictureNs.load()) / std::max(0.25, p->m_rateNow.load())));
        }
        return GST_PAD_PROBE_OK;
    }, this, nullptr);
    // What the sink would like best (RGB at the screen's size, say) is kept from what is upstream:
    // playbin's own converter and scaler would otherwise oblige, on a single thread and at the
    // video's full size. Asked what it accepts, the sink says "any raw video"; it then does the
    // work itself, scaling first and on all cores.
    // Any raw video in ordinary memory, that is. The scaler's template also lists video in any
    // kind of memory (it can pass such frames through untouched); offered that, a hardware
    // decoder keeps its frames on the graphics card (CUDA or OpenGL memory), where the scaler and
    // converter cannot read them, and the decoder fails (2.12.0, with NVIDIA's decoders).
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM, [](GstPad* p, GstPadProbeInfo* info, gpointer) -> GstPadProbeReturn {
        GstQuery* q = GST_PAD_PROBE_INFO_QUERY(info);
        if (GST_QUERY_TYPE(q) != GST_QUERY_CAPS) return GST_PAD_PROBE_OK;
        GstCaps* filter = nullptr;
        gst_query_parse_caps(q, &filter);
        GstCaps* tmpl = gst_pad_get_pad_template_caps(p);
        GstCaps* any = gst_caps_new_empty();
        for (guint i = 0; i < gst_caps_get_size(tmpl); ++i) {
            const GstCapsFeatures* f = gst_caps_get_features(tmpl, i);
            if (f && (gst_caps_features_is_any(f) || !gst_caps_features_is_equal(f, GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY))) continue;
            gst_caps_append_structure(any, gst_structure_copy(gst_caps_get_structure(tmpl, i)));
        }
        gst_caps_unref(tmpl);
        GstCaps* res = filter ? gst_caps_intersect_full(filter, any, GST_CAPS_INTERSECT_FIRST) : gst_caps_ref(any);
        gst_query_set_caps_result(q, res);
        gst_caps_unref(res);
        gst_caps_unref(any);
        return GST_PAD_PROBE_HANDLED;
    }, nullptr, nullptr);
    // 2.16: text subtitles are drawn here, from the lines the player keeps (SubtitleFeed.h), before anything
    // else is done to the picture. (CRTPLAYER_SUBTITLE_FEED_OFF: GStreamer's own subtitle path for everything.)
    GstElement* subs = qEnvironmentVariableIsSet("CRTPLAYER_SUBTITLE_FEED_OFF") ? nullptr : m_feed.build(GST_BIN(bin));
    GstPad* in = subs ? gst_element_get_static_pad(subs, "video_sink") : nullptr;
    if (subs && in && gst_element_link_pads(subs, "src", head, "sink")) {
        m_feedOn = true;
        m_feed.playedTo(std::max<qint64>(0, m_startPos));   // (where this video is going to start: its lines are read from around there)
        gst_element_add_pad(bin, gst_ghost_pad_new("sink", in));
    } else {
        m_feedOn = false;
        if (subs) m_feed.unbuild();
        gst_element_add_pad(bin, gst_ghost_pad_new("sink", pad));
    }
    if (in) gst_object_unref(in);
    gst_object_unref(pad);
    m_subSel = -1;
    m_nEmbeddedText = 0;
    m_trackFiles.clear();
    m_trackFileAsked = -1;
    m_trackFileWaiting = false;
    m_textReopenPending = false;

    g_object_set(m_pipe, "video-sink", bin, "uri", uri.toUtf8().constData(), nullptr);
    // (A subtitle file is read by the player's own feed; without the feed, by playbin.)
    if (!m_subUri.isEmpty() && !m_feedOn) g_object_set(m_pipe, "suburi", m_subUri.toUtf8().constData(), nullptr);
    if (GstElement* as = fallbackAudioSink(&m_audioOutput)) g_object_set(m_pipe, "audio-sink", as, nullptr);
    // Keeps voices at their normal pitch when the speed changes (passthrough at 1x).
    // Audio chain: natural-sounding speed changes, then the look's tape and speaker sound.
    {
        crtTapeRegister();
        GstElement* bin = gst_bin_new("audiochain");
        GstElement* st = gst_element_factory_make("scaletempo", nullptr);
        // At another speed, scaletempo recalculates the time of a "nothing for this stretch" notice passing through it,
        // and for one that lies before the place just jumped to (MPEG-TS and some Matroska files send such) the
        // result is a time hundreds of years ahead. The sound output then waits for that time, in a way that the
        // next jump cannot interrupt: the player would stand still for good. Such a notice goes no further.
        if (st)
            if (GstPad* sp = gst_element_get_static_pad(st, "src")) {
                gst_pad_add_probe(sp, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, &dropBrokenGap, nullptr, nullptr);
                gst_object_unref(sp);
            }
        GstElement* c1 = gst_element_factory_make("audioconvert", nullptr);
        GstElement* tape = gst_element_factory_make("crttape", nullptr);
        GstElement* c2 = gst_element_factory_make("audioconvert", nullptr);
        if (c1 && tape && c2) {
            if (st) gst_bin_add(GST_BIN(bin), st);
            gst_bin_add_many(GST_BIN(bin), c1, tape, c2, nullptr);
            if (st) gst_element_link(st, c1);
            gst_element_link_many(c1, tape, c2, nullptr);
            GstPad* in = gst_element_get_static_pad(st ? st : c1, "sink");
            GstPad* out = gst_element_get_static_pad(c2, "src");
            gst_element_add_pad(bin, gst_ghost_pad_new("sink", in));
            gst_element_add_pad(bin, gst_ghost_pad_new("src", out));
            gst_object_unref(in); gst_object_unref(out);
            g_object_set(m_pipe, "audio-filter", bin, nullptr);
            m_tape = tape;
            crtTapeSetParams(m_tape, m_tapeParams);
        } else {
            gst_object_unref(bin);
            if (c1) gst_object_unref(c1);
            if (tape) gst_object_unref(tape);
            if (c2) gst_object_unref(c2);
            if (st) g_object_set(m_pipe, "audio-filter", st, nullptr);
            m_tape = nullptr;
        }
    }
    gint flags = 0;
    g_object_get(m_pipe, "flags", &flags, nullptr);
    flags |= kFlagVideo | kFlagAudio;
    // The subtitle path is always built, so subtitles can come on at any moment; while they
    // are not wanted the overlay is told to stay silent (applySubtitleShown).
    // 2.16: with the lines in the player's own hands, playbin's subtitle path stays switched off: nothing of the
    // video's subtitle streams then flows through the playing pipeline (where a stream of lines waiting for its
    // pictures could bring a jump to a standstill). A video that turns out to need it (picture subtitles) is
    // opened again with it on: it is set here, before anything flows, and never while the pipeline runs
    // (switched on then, playbin now and again never delivered another picture).
    m_playbinText = !m_feedOn || m_playbinTextFor.contains(m_uri);
    if (m_playbinText) flags |= kFlagText; else flags &= ~kFlagText;
    m_subShown = m_subsWanted;
    if (m_deinterlace) flags |= kFlagDeinterlace; else flags &= ~kFlagDeinterlace;
    flags &= ~kFlagSoftColorbalance;  // colour controls live in the shader
    g_object_set(m_pipe, "flags", flags, nullptr);
    m_audioPicked = false;
    m_pendingAudio = -1;
    m_prefMuted = false;
    m_subPicked = m_subUriChosen || (m_carrySel >= 0 && m_carryPicked);
    applyOffsets();
    applySubtitleStyle();
    gst_stream_volume_set_volume(GST_STREAM_VOLUME(m_pipe), GST_STREAM_VOLUME_FORMAT_CUBIC, m_volume);
    g_object_set(m_pipe, "mute", m_muted ? TRUE : FALSE, nullptr);

    GstBus* bus = gst_element_get_bus(m_pipe);
    gst_bus_set_sync_handler(bus, &Player::busSyncHandler, this, nullptr);
    gst_object_unref(bus);
    g_signal_connect(m_pipe, "deep-element-added", G_CALLBACK(&Player::onDeepElementAdded), this);
    g_signal_connect(m_pipe, "audio-changed", G_CALLBACK(&Player::onStreamsChanged), this);
    g_signal_connect(m_pipe, "text-changed", G_CALLBACK(&Player::onStreamsChanged), this);
    g_signal_connect(m_pipe, "video-changed", G_CALLBACK(&Player::onStreamsChanged), this);
    g_signal_connect(m_pipe, "source-setup", G_CALLBACK(&Player::onSourceSetup), this);
    // A web video whose picture and sound are two streams has two demuxers, and a jump must reach both.
    // playbin hands a jump to its picture's sink only (with one demuxer, that moves everything: the
    // sound's stream would stay where it was, and fall silent until the clock caught up with it;
    // measured). Its sink is told to hand events to every sink, as any other pipeline does.
    m_twoStreams = crt_web_is(uri) && crt_web_report(uri).value("streams").toInt() > 1;
    if (m_twoStreams) {
        if (GstElement* sink = gst_bin_get_by_name(GST_BIN(m_pipe), "playsink")) {
            if (g_object_class_find_property(G_OBJECT_GET_CLASS(sink), "send-event-mode")) g_object_set(sink, "send-event-mode", 0, nullptr);
            gst_object_unref(sink);
        }
    }

    setState(State::Loading);
    const GstStateChangeReturn r = gst_element_set_state(m_pipe, GST_STATE_PAUSED);
    if (r == GST_STATE_CHANGE_FAILURE) {
        // The bus will carry the precise error; keep the generic message as a fallback.
        qWarning() << "playbin failed to pause for" << uri;
    }
    return true;
}

void Player::teardown()
{
    m_seekWatchdog.stop();
    m_seekInFlight = false;
    m_hasPendingSeek = false;
    if (m_pipe) {
        GstBus* bus = gst_element_get_bus(m_pipe);
        gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
        gst_object_unref(bus);
        g_signal_handlers_disconnect_by_data(m_pipe, this);
        m_feed.setShown(false);   // (a picture held back for its subtitle lines is let go)
        gst_element_set_state(m_pipe, GST_STATE_NULL);
        m_feed.unbuild();
        m_feedOn = false;
        gst_object_unref(m_pipe);
        m_pipe = nullptr;
        m_appsink = nullptr;
        m_capsFilter = nullptr;
        m_shrink = nullptr;
        m_tape = nullptr;   // it belonged to the pipeline
    }
    if (m_waiting) setWaiting(false);
    m_heldByWait = false;
    QMutexLocker lock(&m_mutex);
    if (m_webSource) { gst_object_unref(m_webSource); m_webSource = nullptr; }
    if (m_videoDec) { gst_object_unref(m_videoDec); m_videoDec = nullptr; }
    for (GstElement* e : m_textOverlays) gst_object_unref(e);
    m_textOverlays.clear();
    if (m_subOverlay) { gst_object_unref(m_subOverlay); m_subOverlay = nullptr; }
    if (m_deintEl) { gst_object_unref(m_deintEl); m_deintEl = nullptr; }
    if (m_sample) { gst_sample_unref(m_sample); m_sample = nullptr; }
    if (m_lastCaps) { gst_caps_unref(m_lastCaps); m_lastCaps = nullptr; }
    clearNativeFrames();
    gst_caps_replace(&m_natCaps, nullptr);
    m_videoDecoder.clear();
    m_audioDecoder.clear();
    m_videoDecoderHw = false;
    m_framePending = false;
    m_lastFrameStreamTime = -1;
    m_fpsN = 0; m_fpsD = 1;
    m_videoDone = false;
    m_endReported = false;
    m_endStill = 0;
}

void Player::close()
{
    teardown();
    m_feed.clearSource();
    m_uri.clear();
    m_duration = -1;
    m_audioTracks.clear();
    m_textTracks.clear();
    setState(State::Idle);
    emit tracksChanged();
    emit decoderChanged();
}

void Player::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

// A web video's reader has run dry (the network is slow, or away): without this the clock ran on while
// the picture stood, and when the network came back the video went on at the clock's place, with
// everything in between never shown (measured: a 6 s gap skipped 7.5 s of video). Now the video waits:
// paused, without being "paused" for the viewer, until there is enough at hand again.
void Player::watchNetwork()
{
    if (!m_pipe || !m_loaded || !m_webSource) { if (m_waiting) setWaiting(false); return; }
    const QList<WebLevel> levels = crt_web_levels(m_webSource);
    if (levels.isEmpty()) return;
    const WebLevel& v = levels.first();   // the picture's stream (or the only one)
    if (!m_waiting) {
        // Dry: the server has sent nothing for half a second, though the file has not ended, and less than two
        // seconds of the video are at hand. (What is at hand then is part of the picture being waited for.)
        const quint64 little = v.kbps > 0 ? std::max<quint64>(256 * 1024, quint64(v.kbps) * 250) : 512 * 1024;
        const bool dry = !v.ended && v.quietMs > 500 && v.ahead < std::min<quint64>(v.room / 2, little);
        const bool still = m_wall.elapsed() - m_lastSampleMs.load() > 700;   // and no new picture has come out either
        if (dry && still && m_state == State::Playing && m_targetPlaying && !m_seekInFlight && !m_videoDone && !m_endReported) {
            m_waiting = true;
            m_heldByWait = true;
            ++m_waits;
            gst_element_set_state(m_pipe, GST_STATE_PAUSED);
            emit waitingForDataChanged(true);
        }
        return;
    }
    // On again with about three seconds of the video at hand (or all there is).
    const quint64 want = v.kbps > 0 ? std::clamp<quint64>(quint64(v.kbps) * 125 * 3, 128 * 1024, 2 * 1024 * 1024) : 768 * 1024;
    if (v.ended || v.ahead >= want) {
        setWaiting(false);
        // The clock ran on for a moment after the last picture (until the wait began): back to that picture, so
        // that nothing of the video is left out. (The sound of that moment is heard twice.)
        const qint64 last = lastFrameStreamTime(), pos = position();
        if (!m_seekInFlight && last >= 0 && pos - last > 200000000LL && pos - last < 5000000000LL) doSeek(last, SeekMode::Accurate);
        if (m_targetPlaying && m_pipe) gst_element_set_state(m_pipe, GST_STATE_PLAYING);
    }
}

void Player::setWaiting(bool waiting)
{
    if (m_waiting == waiting) return;
    m_waiting = waiting;
    emit waitingForDataChanged(waiting);
}

void Player::play()
{
    if (!m_pipe) return;
    // (a web video that failed: its addresses are what failed, and starting its pipeline again would only ask
    // for them again; the window asks yt-dlp for new ones instead)
    if (m_state == State::Error && crt_web_is(m_uri)) return;
    m_targetPlaying = true;
    m_governor.setPlaying(true);
    gst_element_set_state(m_pipe, GST_STATE_PLAYING);
}

void Player::pause()
{
    if (!m_pipe) return;
    m_targetPlaying = false;
    m_governor.setPlaying(false);   // (a paused picture is always the exact one)
    if (m_heldByWait) { m_heldByWait = false; setWaiting(false); setState(State::Paused); }   // (it was waiting for the network: now it is simply paused)
    gst_element_set_state(m_pipe, GST_STATE_PAUSED);
}

void Player::togglePause()
{
    if (!m_pipe) return;
    if (m_targetPlaying) pause(); else play();
}

qint64 Player::position() const
{
    if (!m_pipe) return 0;
    if (m_seekInFlight) return m_hasPendingSeek ? m_pendingSeek : m_seekTarget;
    gint64 pos = 0;
    if (gst_element_query_position(m_pipe, GST_FORMAT_TIME, &pos)) return pos;
    return 0;
}

void Player::seek(qint64 posNs, SeekMode mode)
{
    if (!m_pipe || !m_loaded) { m_startPos = posNs; return; }
    if (!m_refreshing) m_lateRefreshes = 0;
    if (m_duration > 0) posNs = std::clamp<qint64>(posNs, 0, m_duration);
    else posNs = std::max<qint64>(posNs, 0);
    if (m_seekInFlight) {
        // Coalesce: while a seek is being processed only remember the newest target,
        // so dragging the seek bar never queues up a backlog of flushes.
        m_hasPendingSeek = true;
        m_pendingSeek = posNs;
        if (mode == SeekMode::Accurate || m_pendingMode != SeekMode::Accurate) m_pendingMode = mode;
        return;
    }
    doSeek(posNs, mode);
}

void Player::doSeek(qint64 posNs, SeekMode mode)
{
    int flags = GST_SEEK_FLAG_FLUSH;
    // Two demuxers (a web video's picture and sound as two streams): a jump "to the nearest keyframe" would
    // take each to a keyframe of its own, the picture's seconds apart, the sound's everywhere, and leave
    // the two that far out of step (measured: 1.4 s). Every jump is an exact one there.
    if (m_twoStreams) mode = SeekMode::Accurate;
    if (mode == SeekMode::Accurate) flags |= GST_SEEK_FLAG_ACCURATE;
    else flags |= GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_SNAP_NEAREST;
    m_seekTarget = posNs;
    m_seekInFlight = true;
    g_object_get(m_pipe, "current-text", &m_textTrackRead, nullptr);   // (the file is read again with this track selected)
    m_endReported = false;
    m_videoDone = false;
    m_seekWatchdog.start();
    // The subtitle lines follow the jump: the picture after it waits (a few milliseconds; a quarter of a second
    // at the very most) until the lines in force there have been handed to the overlay.
    const quint64 hold = m_feedOn ? m_feed.aboutToSeek(posNs) : 0;
    m_refreshWanted = false;   // (the jump brings the lines as they now are)
    if (!gst_element_seek(m_pipe, m_rate, GST_FORMAT_TIME, GstSeekFlags(flags), GST_SEEK_TYPE_SET, posNs,
                          GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
        m_seekInFlight = false;
        m_seekWatchdog.stop();
        if (m_feedOn) m_feed.jumpFailed(hold);
    }
}

void Player::seekKeyframe(bool forward)
{
    if (!m_pipe) return;
    if (m_twoStreams) { seek(position() + (forward ? 5000000000LL : -5000000000LL), SeekMode::Accurate); return; }   // (see doSeek)
    const qint64 pos = position();
    // Just past the current frame, snapping to the keyframe after it (or before it).
    const qint64 target = forward ? pos + 40000000 : std::max<qint64>(0, pos - 1000000);
    const int flags = GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT | (forward ? GST_SEEK_FLAG_SNAP_AFTER : GST_SEEK_FLAG_SNAP_BEFORE);
    m_seekTarget = target;
    m_seekInFlight = true;
    m_endReported = false;
    m_videoDone = false;
    m_seekWatchdog.start();
    const quint64 hold = m_feedOn ? m_feed.aboutToSeek(target) : 0;
    m_refreshWanted = false;
    if (!gst_element_seek(m_pipe, m_rate, GST_FORMAT_TIME, GstSeekFlags(flags), GST_SEEK_TYPE_SET, target,
                          GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
        if (m_feedOn) m_feed.jumpFailed(hold);
        m_seekInFlight = false;
        m_seekWatchdog.stop();
    }
}

void Player::setTapeParams(const TapeParams& p)
{
    m_tapeParams = p;
    if (m_tape) crtTapeSetParams(m_tape, p);
}

// ---- what carries over from video to video ---------------------------------------------

void Player::setSubtitlesWanted(bool on)
{
    const bool was = m_subShown;
    m_subsWanted = on;
    m_subPicked = false;
    m_subShown = on;
    if (!m_pipe) return;
    if (!m_feedOn) applySubtitleShown();   // (with the feed, updateSubtitleFeed does it, once the lines' source is set)
    if (on) applyTrackPreferences();
    updateSubtitleFeed();
    if (m_feedOn && was != on && m_loaded) refreshSubtitles();   // (the picture on screen gets its line, or loses it)
}

// The subtitle track chosen: among the file's own (playbin's numbering), or the subtitle file after them.
int Player::selectedText() const
{
    if (m_feedOn) return m_subSel;
    gint cur = -1;
    if (m_pipe) g_object_get(m_pipe, "current-text", &cur, nullptr);
    return cur;
}

void Player::selectText(int idx)
{
    m_subSel = idx;
    if (!m_pipe || idx < 0 || m_feedOn) return;   // (with the feed, playbin hears of it only for picture subtitles)
    gint cur = -1;
    g_object_get(m_pipe, "current-text", &cur, nullptr);
    if (cur != idx) g_object_set(m_pipe, "current-text", idx, nullptr);
}

// Where the feed takes its lines from: the chosen track, while subtitles are shown.
void Player::updateSubtitleFeed()
{
    if (!m_feedOn) return;
    if (!m_subShown || m_subSel < 0 || m_subSel >= m_textTracks.size()) m_feed.clearSource();
    else if (m_subSel >= m_nEmbeddedText) m_feed.setSource(m_subUri, -1);
    else if (m_trackFiles.contains(m_subSel)) m_feed.setSource(m_trackFiles.value(m_subSel), -1);
    else if (QUrl(m_uri).isLocalFile()) m_feed.setSource(m_uri, m_subSel);
    else {
        // Not a file on this computer: reading it a second time, for its lines, would fetch the whole video twice.
        // Whoever knows the server is asked for the track as a file; until then playbin draws it (applySubtitleShown).
        m_feed.clearSource();
        // (Asked once the video has opened: until then its list of subtitle tracks may be incomplete, and the
        // answer depends on it.)
        if (m_loaded && m_trackFileAsked != m_subSel) {
            m_trackFileAsked = m_subSel;
            m_trackFileWaiting = true;
            const int idx = m_subSel;
            const quint64 gen = m_generation;
            QMetaObject::invokeMethod(this, [this, idx] { emit embeddedSubtitleFileWanted(idx); }, Qt::QueuedConnection);
            // (no answer in five seconds is taken for a no)
            QTimer::singleShot(5000, this, [this, idx, gen] { if (gen == m_generation && m_trackFileWaiting && m_trackFileAsked == idx) setEmbeddedSubtitleFile(idx, QString()); });
        }
    }
    applySubtitleShown();
}

// The answer to embeddedSubtitleFileWanted: the track's lines as a file, or nothing (it cannot be had).
void Player::setEmbeddedSubtitleFile(int textIndex, const QString& fileUri)
{
    if (!m_pipe || !m_feedOn || textIndex < 0) return;
    if (textIndex == m_trackFileAsked) m_trackFileWaiting = false;
    if (!fileUri.isEmpty()) m_trackFiles.insert(textIndex, fileUri);
    if (textIndex != m_subSel) return;
    updateSubtitleFeed();
    if (m_loaded && !fileUri.isEmpty()) refreshSubtitles();
}

void Player::onFeedNotify()
{
    if (!m_pipe) return;
    applySubtitleShown();
    // A line for the picture that stands on screen arrived after it: the picture is fetched again, with it.
    // (not again and again: three times in a row at most, until the viewer jumps somewhere himself)
    if (m_feed.takeLateLine() && m_loaded && m_subShown && m_state == State::Paused && !m_targetPlaying && m_lateRefreshes < 3) {
        ++m_lateRefreshes;
        refreshSubtitles();
    }
}

// The picture on screen once more, with the subtitle lines as they now are (a jump to where the video stands).
// While a jump is under way the picture on screen is not where the video is going: it is done once that jump
// is over (if no other follows it, which brings the lines by itself).
void Player::refreshSubtitles()
{
    if (!m_pipe || !m_loaded || (m_state != State::Playing && m_state != State::Paused)) return;
    if (m_seekInFlight) { m_refreshWanted = true; return; }
    const qint64 shown = m_lastFrameStreamTime;
    m_refreshing = true;
    seek(shown >= 0 ? shown + 1000000 : position(), SeekMode::Accurate);
    m_refreshing = false;
}

QJsonObject Player::subtitleFeedReport() const
{
    QJsonObject o = m_feed.report();
    o["on"] = m_feedOn;
    o["videoOpens"] = double(m_generation);
    o["selected"] = m_subSel;
    o["ownTracks"] = m_nEmbeddedText;
    return o;
}

// Shows or hides the subtitles of the current video. Text subtitles are drawn by the feed's overlay; picture
// subtitles (and everything, without the feed) by playbin's. The one that is not drawing stays silent.
void Player::applySubtitleShown()
{
    const SubtitleFeed::Kind kind = m_feedOn ? m_feed.kind() : SubtitleFeed::Kind::None;
    const bool viaFeed = m_feedOn && kind == SubtitleFeed::Kind::Text;
    const bool viaPlaybin = playbinDraws();
    if (m_feedOn) {
        m_feed.setDelayNs(qint64(m_subDelayMs) * GST_MSECOND);
        m_feed.setShown(m_subShown && (viaFeed || kind == SubtitleFeed::Kind::Reading));
        // Picture subtitles (or lines the player cannot have): playbin's own path draws them.
        const bool want = m_subShown && viaPlaybin;
        const quint64 gen = m_generation;
        if (want && !m_playbinText) {
            if (!m_textReopenPending) {
                m_textReopenPending = true;
                QMetaObject::invokeMethod(this, [this, gen] { if (gen == m_generation) reopenWithPlaybinText(); }, Qt::QueuedConnection);
            }
        } else if (want) {
            gint cur = -1;
            g_object_get(m_pipe, "current-text", &cur, nullptr);
            if (cur != m_subSel) {
                g_object_set(m_pipe, "current-text", m_subSel, nullptr);
                // (the file is read again from here, with this track: its lines already read went elsewhere)
                if (m_loaded) QMetaObject::invokeMethod(this, [this, gen] { if (gen == m_generation) refreshSubtitles(); }, Qt::QueuedConnection);
            }
        }
    }
    GstElement* ov = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        if (m_subOverlay) ov = GST_ELEMENT(gst_object_ref(m_subOverlay));
    }
    if (!ov) return;
    g_object_set(ov, "silent", (m_subShown && viaPlaybin) ? FALSE : TRUE, nullptr);
    // The subtitle delay: an offset on the overlay's subtitle input, so it holds for every
    // kind of subtitle (playbin's own text-offset misses subtitles stored inside the video).
    if (GstPad* pad = gst_element_get_static_pad(ov, "subtitle_sink")) {
        gst_pad_set_offset(pad, gint64(m_subDelayMs) * GST_MSECOND);
        gst_object_unref(pad);
    }
    gst_object_unref(ov);
}

// Must playbin's own subtitle path draw the track that is showing? Picture subtitles; a track the player's
// reader failed on; a track of a video not on this computer that the server does not hand out as a file
// (asked, and answered no). Not: a track whose lines are still being looked for, or simply none set yet.
bool Player::playbinDraws() const
{
    if (!m_feedOn) return true;
    if (m_subSel < 0 || m_subSel >= m_nEmbeddedText) return false;   // (a subtitle file, or nothing)
    const SubtitleFeed::Kind kind = m_feed.kind();
    if (kind == SubtitleFeed::Kind::Unsupported || kind == SubtitleFeed::Kind::Failed) return true;
    return kind == SubtitleFeed::Kind::None && !QUrl(m_uri).isLocalFile() && m_trackFileAsked == m_subSel && !m_trackFileWaiting &&
           !m_trackFiles.contains(m_subSel);
}

// This video needs playbin's own subtitle path: it is opened again, where it stands, with that path set up
// from the start (and so is every later opening of it in this session).
void Player::reopenWithPlaybinText()
{
    m_textReopenPending = false;
    if (!m_pipe || m_uri.isEmpty() || m_playbinText) return;
    if (!m_subShown || !playbinDraws()) return;   // (not needed after all)
    m_playbinTextFor.insert(m_uri);
    m_carrySel = m_subSel;
    m_carryPicked = m_subPicked;
    const qint64 pos = m_loaded ? position() : m_startPos;
    const bool shown = m_subShown;
    openInternal(m_uri, m_targetPlaying, pos, true);
    m_subShown = shown;
}

void Player::setPreferredLanguages(const QString& audio, const QString& subtitle)
{
    m_prefAudioLang = audio.toLower();
    m_prefSubLang = subtitle.toLower();
}

// Called whenever the video's tracks become known: the preferred languages are picked,
// unless the viewer has already chosen a track in this video.
void Player::applyTrackPreferences()
{
    if (!m_pipe) return;
    if (!m_audioPicked && !m_prefAudioLang.isEmpty() && m_audioTracks.size() > 1 && m_pendingAudio < 0) {
        gint cur = -1;
        g_object_get(m_pipe, "current-audio", &cur, nullptr);
        const bool curMatches = cur >= 0 && cur < m_audioTracks.size() && m_audioTracks[cur].lang == m_prefAudioLang;
        if (!curMatches)
            for (const TrackInfo& t : m_audioTracks)
                if (t.lang == m_prefAudioLang) {
                    m_pendingAudio = t.index;
                    if (!canSwitchAudioNow() && !m_prefMuted) {   // still opening: silent until it is switched in
                        m_prefMuted = true;
                        g_object_set(m_pipe, "mute", TRUE, nullptr);
                    }
                    switchPendingAudio();
                    break;
                }
    }
    if (!m_subPicked && m_subShown && !m_prefSubLang.isEmpty() && m_textTracks.size() > 1) {
        const int cur = selectedText();
        const bool curMatches = cur >= 0 && cur < m_textTracks.size() && m_textTracks[cur].lang == m_prefSubLang;
        if (!curMatches)
            for (const TrackInfo& t : m_textTracks)
                if (t.lang == m_prefSubLang) { selectText(t.index); break; }
    }
}

void Player::setAudioDelay(int ms)
{
    m_audioDelayMs = std::clamp(ms, -10000, 10000);
    applyOffsets();
}

void Player::setSubtitleDelay(int ms)
{
    const int before = m_subDelayMs;
    m_subDelayMs = std::clamp(ms, -60000, 60000);
    applyOffsets();
    // (the feed's lines already handed over carry the old delay: they are handed over anew, once the value has settled)
    if (m_feedOn && m_loaded && before != m_subDelayMs && m_subShown) m_feedRefresh.start();
}

void Player::applyOffsets()
{
    if (!m_pipe) return;
    // playbin's av-offset holds the picture back when positive, so the sound's delay is its negative.
    g_object_set(m_pipe, "av-offset", -gint64(m_audioDelayMs + m_pictureLatencyMs) * GST_MSECOND, nullptr);
    applySubtitleShown();   // (the subtitle delay lives on the overlay)
}

void Player::setPictureLatencyMs(int ms)
{
    ms = std::clamp(ms, 0, 200);
    if (ms == m_pictureLatencyMs) return;
    m_pictureLatencyMs = ms;
    applyOffsets();
}

void Player::setSubtitleStyle(const SubtitleStyle& st)
{
    m_subStyle = st;
    applySubtitleStyle();
}

void Player::applySubtitleStyle()
{
    if (!m_pipe) return;
    // (Sizes are for a 640-wide picture; the overlay scales them with the video.)
    static const int kSizes[4] = {13, 18, 24, 31};
    const QByteArray font = QStringLiteral("Sans Bold %1").arg(kSizes[std::clamp(m_subStyle.size, 0, 3)]).toUtf8();
    g_object_set(m_pipe, "subtitle-font-desc", font.constData(), nullptr);
    if (m_feedOn && m_feed.overlay()) g_object_set(m_feed.overlay(), "font-desc", font.constData(), nullptr);
    QVector<GstElement*> overlays;
    {
        QMutexLocker lock(&m_mutex);
        for (GstElement* e : m_textOverlays) overlays.push_back(GST_ELEMENT(gst_object_ref(e)));
    }
    const guint color = m_subStyle.color == 1 ? 0xFFFFE94Du : 0xFFFFFFFFu;
    const bool box = m_subStyle.background == 1;
    for (GstElement* ov : overlays) {
        g_object_set(ov, "font-desc", font.constData(), "color", color, "outline-color", 0xFF000000u, "draw-outline", TRUE,
                     "draw-shadow", box ? FALSE : TRUE, "shaded-background", box ? TRUE : FALSE, "shading-value", 150u, nullptr);
        // valignment: 1 bottom, 2 top, 3 a position down the picture (0..1)
        if (m_subStyle.position == 2) g_object_set(ov, "valignment", 2, "ypad", 25, nullptr);
        else if (m_subStyle.position == 1) g_object_set(ov, "valignment", 3, "ypos", 0.80, nullptr);
        else g_object_set(ov, "valignment", 1, "ypad", 25, nullptr);
        gst_object_unref(ov);
    }
}

// ---- the fast path without a graphics card ---------------------------------------------

GstPadProbeReturn Player::onSinkEvent(GstPad*, GstPadProbeInfo* info, gpointer self)
{
    GstEvent* ev = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(ev) == GST_EVENT_CAPS) {
        GstCaps* caps = nullptr;
        gst_event_parse_caps(ev, &caps);
        GstVideoInfo vi;
        if (caps && gst_video_info_from_caps(&vi, caps)) {
            auto* p = static_cast<Player*>(self);
            QMutexLocker lock(&p->m_mutex);
            p->m_natW = GST_VIDEO_INFO_WIDTH(&vi);
            p->m_natH = GST_VIDEO_INFO_HEIGHT(&vi);
            p->m_natParN = GST_VIDEO_INFO_PAR_N(&vi) > 0 ? GST_VIDEO_INFO_PAR_N(&vi) : 1;
            p->m_natParD = GST_VIDEO_INFO_PAR_D(&vi) > 0 ? GST_VIDEO_INFO_PAR_D(&vi) : 1;
            p->m_natFormat = QString::fromUtf8(gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&vi)));
            gst_caps_replace(&p->m_natCaps, caps);
            p->m_pictureNs = GST_VIDEO_INFO_FPS_N(&vi) > 0 && GST_VIDEO_INFO_FPS_D(&vi) > 0
                                 ? gint64(gst_util_uint64_scale(GST_SECOND, GST_VIDEO_INFO_FPS_D(&vi), GST_VIDEO_INFO_FPS_N(&vi))) : 0;
        }
    } else if (GST_EVENT_TYPE(ev) == GST_EVENT_FLUSH_STOP) {
        auto* p = static_cast<Player*>(self);
        p->m_videoDone = false;
        QMutexLocker lock(&p->m_mutex);
        p->clearNativeFrames();   // (a jump: what was kept is no longer what is shown)
    } else if (GST_EVENT_TYPE(ev) == GST_EVENT_EOS || GST_EVENT_TYPE(ev) == GST_EVENT_STREAM_GROUP_DONE) {
        static_cast<Player*>(self)->m_videoDone = true;    // the picture has ended (see m_endWatch)
    } else if (GST_EVENT_TYPE(ev) == GST_EVENT_STREAM_START) {
        static_cast<Player*>(self)->m_videoDone = false;
    }
    return GST_PAD_PROBE_OK;
}

// Each frame as decoded passes here on its way to being converted or scaled. The last few are
// remembered, so that the full frame behind the picture on screen is at hand (an original-frame
// screenshot; a paused picture that is now wanted at another size) without seeking for it.
GstPadProbeReturn Player::onSinkBuffer(GstPad*, GstPadProbeInfo* info, gpointer self)
{
    auto* p = static_cast<Player*>(self);
    GstBuffer* b = GST_PAD_PROBE_INFO_BUFFER(info);
    QMutexLocker lock(&p->m_mutex);
    if (!p->m_keepNative || !p->m_natCaps || !b) {
        p->clearNativeFrames();
        return GST_PAD_PROBE_OK;
    }
    p->m_nativeFrames.push_back({gst_buffer_ref(b), gst_caps_ref(p->m_natCaps)});
    while (p->m_nativeFrames.size() > 4) {   // (the sink holds two, the screen one)
        gst_buffer_unref(p->m_nativeFrames.front().buffer);
        gst_caps_unref(p->m_nativeFrames.front().caps);
        p->m_nativeFrames.pop_front();
    }
    return GST_PAD_PROBE_OK;
}

void Player::clearNativeFrames()
{
    for (NativeFrame& f : m_nativeFrames) { gst_buffer_unref(f.buffer); gst_caps_unref(f.caps); }
    m_nativeFrames.clear();
}

GstSample* Player::nativeSample(GstSample* shown)
{
    GstBuffer* b = shown ? gst_sample_get_buffer(shown) : nullptr;
    if (!b || !GST_BUFFER_PTS_IS_VALID(b)) return nullptr;
    QMutexLocker lock(&m_mutex);
    for (auto it = m_nativeFrames.rbegin(); it != m_nativeFrames.rend(); ++it)
        if (GST_BUFFER_PTS(it->buffer) == GST_BUFFER_PTS(b))
            return gst_sample_new(it->buffer, it->caps, gst_sample_get_segment(shown), nullptr);
    return nullptr;
}

// What the video sink tells the decoder it accepts: the number of entries that are not plain
// raw video in ordinary memory (must be 0), or -1 when there is no sink to ask.
int Player::sinkForeignMemoryEntries() const
{
    if (!m_pipe) return -1;
    GstElement* sink = nullptr;
    g_object_get(m_pipe, "video-sink", &sink, nullptr);
    if (!sink) return -1;
    GstPad* pad = gst_element_get_static_pad(sink, "sink");
    gst_object_unref(sink);
    if (!pad) return -1;
    GstCaps* caps = gst_pad_query_caps(pad, nullptr);
    gst_object_unref(pad);
    if (!caps) return -1;
    int foreign = gst_caps_is_any(caps) ? 1 : 0;
    for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
        const GstCapsFeatures* f = gst_caps_get_features(caps, i);
        const bool plain = !f || (!gst_caps_features_is_any(f) && gst_caps_features_is_equal(f, GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY));
        if (!plain || !gst_structure_has_name(gst_caps_get_structure(caps, i), "video/x-raw")) ++foreign;
    }
    if (gst_caps_is_empty(caps)) foreign = -1;
    gst_caps_unref(caps);
    return foreign;
}

bool Player::nativeFormat(int* width, int* height, int* parN, int* parD, QString* format) const
{
    QMutexLocker lock(&m_mutex);
    if (m_natW <= 0 || m_natH <= 0) return false;
    *width = m_natW; *height = m_natH; *parN = m_natParN; *parD = m_natParD;
    if (format) *format = m_natFormat;
    return true;
}

void Player::setOutput(Output mode, const QSize& size)
{
    if (mode == Output::RgbScaled && (size.isEmpty() || !m_canScale)) mode = Output::Rgb;
    const QSize s = mode == Output::RgbScaled ? size : QSize();
    if (mode == m_output && s == m_fastSize) return;
    m_output = mode;
    m_fastSize = s;
    m_keepNative = mode != Output::AsDecoded;
    applyFastOutput();
}

void Player::applyFastOutput()
{
    if (!m_capsFilter) return;
    GstCaps* caps;
    if (m_output == Output::AsDecoded) {
        caps = gst_caps_from_string("video/x-raw, format=(string){ NV12, I420, BGRx, BGRA, RGBx, RGBA }");
    } else if (m_output == Output::Rgb) {
        caps = gst_caps_from_string("video/x-raw, format=(string)BGRx");
    } else {
        caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGRx", "width", G_TYPE_INT, m_fastSize.width(),
                                   "height", G_TYPE_INT, m_fastSize.height(), "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, nullptr);
    }
    g_object_set(m_capsFilter, "caps", caps, nullptr);   // takes effect with the next frame
    gst_caps_unref(caps);
    // The player's own filter ahead of the scaler: untouched frames for a graphics card; 8 bits a sample when
    // the CPU converts; and shrunk by a whole factor on the way when the picture is shown much smaller.
    if (m_output == Output::AsDecoded) crt_shrink_set(m_shrink, ShrinkMode::Off);
    else if (m_output == Output::Rgb) crt_shrink_set(m_shrink, ShrinkMode::Depth);
    else crt_shrink_set(m_shrink, ShrinkMode::Fit, m_fastSize.width(), m_fastSize.height());
}

QJsonObject Player::governorReport() const { return m_governor.report(); }

QJsonObject Player::shrinkReport() const
{
    const ShrinkState st = crt_shrink_state(m_shrink);
    return QJsonObject{{"present", m_shrink != nullptr}, {"frames", double(st.frames)}, {"factor", st.factor},
                       {"width", st.outWidth}, {"height", st.outHeight}, {"msPerFrame", st.msPerFrame}};
}

int Player::decoderThreads() const
{
    GstElement* el = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        if (m_videoDec) el = GST_ELEMENT(gst_object_ref(m_videoDec));
    }
    if (!el) return 0;
    int n = 0;
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(el), "max-threads")) {
        g_object_get(el, "max-threads", &n, nullptr);
        if (n == 0) n = int(std::min<guint>(g_get_num_processors(), 16u));   // libav's "auto"
    }
    gst_object_unref(el);
    return n;
}

// The first element inside `e` (or `e` itself) that is a sink with a "ts-offset" property; its value in ms.
static bool sinkOffsetMs(GstElement* e, int* ms)
{
    if (!e) return false;
    if (GST_IS_BIN(e)) {
        bool found = false;
        GstIterator* it = gst_bin_iterate_sinks(GST_BIN(e));
        GValue v = G_VALUE_INIT;
        while (!found && gst_iterator_next(it, &v) == GST_ITERATOR_OK) {
            found = sinkOffsetMs(GST_ELEMENT(g_value_get_object(&v)), ms);
            g_value_reset(&v);
        }
        g_value_unset(&v);
        gst_iterator_free(it);
        return found;
    }
    if (!g_object_class_find_property(G_OBJECT_GET_CLASS(e), "ts-offset")) return false;
    gint64 off = 0;
    g_object_get(e, "ts-offset", &off, nullptr);
    *ms = int(off / GST_MSECOND);
    return true;
}

void Player::appliedOffsets(int* audioSinkMs, int* videoSinkMs, int* textMs) const
{
    *audioSinkMs = *videoSinkMs = *textMs = 0;
    if (!m_pipe) return;
    GstElement* as = nullptr;
    g_object_get(m_pipe, "audio-sink", &as, nullptr);
    {
        QMutexLocker lock(&m_mutex);
        if (m_subOverlay) {
            if (GstPad* pad = gst_element_get_static_pad(m_subOverlay, "subtitle_sink")) {
                *textMs = int(gst_pad_get_offset(pad) / GST_MSECOND);
                gst_object_unref(pad);
            }
        }
    }
    if (m_feedOn && m_feed.kind() == SubtitleFeed::Kind::Text) *textMs = int(m_feed.delayNs() / GST_MSECOND);
    if (as) { sinkOffsetMs(as, audioSinkMs); gst_object_unref(as); }
    if (m_appsink) sinkOffsetMs(m_appsink, videoSinkMs);
}

bool Player::deinterlacing() const
{
    GstElement* el = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        if (m_deintEl) el = GST_ELEMENT(gst_object_ref(m_deintEl));
    }
    if (!el) return false;
    // In its automatic mode the deinterlacer passes progressive video through untouched:
    // it is at work when interlaced video goes in and progressive comes out.
    auto mode = [el](const char* padName) {
        QString m;
        if (GstPad* pad = gst_element_get_static_pad(el, padName)) {
            if (GstCaps* caps = gst_pad_get_current_caps(pad)) {
                const gchar* v = gst_structure_get_string(gst_caps_get_structure(caps, 0), "interlace-mode");
                m = v ? QString::fromUtf8(v) : QStringLiteral("progressive");
                gst_caps_unref(caps);
            }
            gst_object_unref(pad);
        }
        return m;
    };
    const QString in = mode("sink"), out = mode("src");
    gst_object_unref(el);
    return !in.isEmpty() && in != QLatin1String("progressive") && out == QLatin1String("progressive");
}

void Player::setExternalSubtitle(const QString& in, bool chosenByViewer)
{
    m_subUriChosen = chosenByViewer && !in.isEmpty();
    if (in.isEmpty()) { m_subUri.clear(); return; }
    m_subUri = in.contains(QStringLiteral("://")) ? in
                                                  : QUrl::fromLocalFile(QFileInfo(in).absoluteFilePath()).toString(QUrl::FullyEncoded);
}

// A subtitle file picked, or put away, while the video is open. True: it is in effect (the lines are the
// player's own to hand over, the video goes on as it is). False: the video has to be opened again for it.
bool Player::changeExternalSubtitle(const QString& in, bool chosenByViewer)
{
    setExternalSubtitle(in, chosenByViewer);
    if (!m_pipe || !m_feedOn) return false;
    // (Also while the video is still opening: the file is simply there by the time its tracks are known. Opening
    // it again then would start from wherever the player happened to stand, not from where it was asked to.)
    m_subPicked = m_subUriChosen;
    m_feed.clearSource();   // (the same name may hold other lines now)
    if (m_subUri.isEmpty()) m_subSel = -1;   // (refreshTracks picks as at opening)
    refreshTracks();
    if (!m_subUri.isEmpty()) {
        m_subSel = m_nEmbeddedText;
        updateSubtitleFeed();
        emit tracksChanged();
    }
    if (m_loaded) refreshSubtitles();
    return true;
}

void Player::setRate(double rate)
{
    rate = std::clamp(rate, 0.25, 4.0);
    if (std::abs(rate - m_rate) < 1e-6) return;
    m_rate = rate;
    m_rateNow = rate;
    // A flushing seek to the current position applies the new rate.
    if (m_pipe && m_loaded) doSeek(position(), SeekMode::Accurate);
}

void Player::readToc(GstToc* toc)
{
    m_chapters.clear();
    std::function<void(GList*)> walk = [&](GList* entries) {
        for (GList* l = entries; l; l = l->next) {
            auto* e = static_cast<GstTocEntry*>(l->data);
            if (gst_toc_entry_get_entry_type(e) == GST_TOC_ENTRY_TYPE_CHAPTER) {
                gint64 start = 0, stop = 0;
                gst_toc_entry_get_start_stop_times(e, &start, &stop);
                ChapterInfo c;
                c.startNs = std::max<gint64>(0, start);
                if (const GstTagList* tags = gst_toc_entry_get_tags(e)) {
                    gchar* title = nullptr;
                    if (gst_tag_list_get_string(tags, GST_TAG_TITLE, &title)) { c.title = QString::fromUtf8(title); g_free(title); }
                }
                m_chapters.push_back(c);
            }
            walk(gst_toc_entry_get_sub_entries(e));
        }
    };
    walk(gst_toc_get_entries(toc));
    std::sort(m_chapters.begin(), m_chapters.end(), [](const ChapterInfo& a, const ChapterInfo& b) { return a.startNs < b.startNs; });
    for (int i = 0; i < m_chapters.size(); ++i)
        if (m_chapters[i].title.isEmpty()) m_chapters[i].title = tr("Chapter %1").arg(i + 1);
    emit chaptersChanged();
}

void Player::seekRelative(qint64 deltaNs)
{
    seek(position() + deltaNs, SeekMode::Accurate);
}

double Player::frameRate() const
{
    const int n = m_fpsN, d = m_fpsD;
    return (n > 0 && d > 0) ? double(n) / d : 0.0;
}

void Player::stepFrame(bool forward)
{
    if (!m_pipe || !m_loaded) return;
    if (m_targetPlaying) pause();
    if (forward) {
        gst_element_send_event(m_pipe, gst_event_new_step(GST_FORMAT_BUFFERS, 1, 1.0, TRUE, FALSE));
    } else {
        const double fps = frameRate() > 0 ? frameRate() : 25.0;
        const qint64 frame = qint64(1e9 / fps);
        qint64 cur = m_lastFrameStreamTime;
        if (cur < 0) cur = position();
        // After an accurate seek the decoder clips the shown frame's timestamp to the
        // seek position, so snap to the start of the frame interval first; otherwise
        // repeated steps back would land on the same frame.
        const qint64 frameStart = qint64(std::floor(double(cur) / frame + 1e-3)) * frame;
        // Land in the middle of the previous frame's display interval.
        seek(std::max<qint64>(0, frameStart - frame / 2), SeekMode::Accurate);
    }
}

void Player::setVolume(double v)
{
    m_volume = std::clamp(v, 0.0, 1.0);
    if (m_pipe) gst_stream_volume_set_volume(GST_STREAM_VOLUME(m_pipe), GST_STREAM_VOLUME_FORMAT_CUBIC, m_volume);
}

void Player::setMuted(bool m)
{
    m_muted = m;
    if (m_pipe && !m_prefMuted) g_object_set(m_pipe, "mute", m ? TRUE : FALSE, nullptr);
}

int Player::currentAudioTrack() const
{
    if (!m_pipe) return -1;
    if (m_pendingAudio >= 0) return m_pendingAudio;   // chosen, and switched in as soon as the video runs
    gint cur = -1;
    g_object_get(m_pipe, "current-audio", &cur, nullptr);
    return cur;
}

int Player::currentSubtitleTrack() const
{
    if (!m_pipe || !m_subShown || m_textTracks.isEmpty()) return -1;
    return selectedText();
}

void Player::setAudioTrack(int idx)
{
    if (!m_pipe || idx < 0 || idx >= m_audioTracks.size()) return;
    m_audioPicked = true;
    m_pendingAudio = idx;
    switchPendingAudio();
}

void Player::switchPendingAudio()
{
    if (m_pendingAudio < 0 || !canSwitchAudioNow()) return;
    gint cur = -1;
    g_object_get(m_pipe, "current-audio", &cur, nullptr);
    if (cur != m_pendingAudio) g_object_set(m_pipe, "current-audio", m_pendingAudio, nullptr);
    m_pendingAudio = -1;
    if (m_prefMuted) {   // the sound comes back once what was queued of the other track has played out
        const quint64 gen = m_generation;
        QTimer::singleShot(350, this, [this, gen] {
            if (gen != m_generation || !m_prefMuted) return;
            m_prefMuted = false;
            if (m_pipe) g_object_set(m_pipe, "mute", m_muted ? TRUE : FALSE, nullptr);
        });
    }
    emit tracksChanged();
}

void Player::setSubtitleTrack(int idx)
{
    if (!m_pipe) return;
    m_subPicked = true;
    m_subShown = idx >= 0 && idx < m_textTracks.size();
    if (m_feedOn) {
        // The feed takes its lines from the track chosen; the picture on screen is fetched again with them.
        if (m_subShown) selectText(idx);
        updateSubtitleFeed();
        if (m_loaded) refreshSubtitles();
        return;
    }
    if (m_subShown) {
        gint cur = -1;
        g_object_get(m_pipe, "current-text", &cur, nullptr);
        if (cur != idx) g_object_set(m_pipe, "current-text", idx, nullptr);
        // The new track's lines that the file has already delivered went to the track that was
        // selected when they were read (a short clip may have delivered them all). The file is
        // read again from here.
        if (idx != m_textTrackRead && m_loaded && !m_seekInFlight && (m_state == State::Playing || m_state == State::Paused)) {
            const qint64 shown = m_lastFrameStreamTime;
            seek(shown >= 0 ? shown + 1000000 : position(), SeekMode::Accurate);
        }
    }
    applySubtitleShown();
}

QString Player::videoDecoder() const { QMutexLocker l(&m_mutex); return m_videoDecoder; }
bool Player::videoDecoderIsHardware() const { QMutexLocker l(&m_mutex); return m_videoDecoderHw; }
QString Player::audioDecoder() const { QMutexLocker l(&m_mutex); return m_audioDecoder; }

QString Player::clockName() const
{
    if (!m_pipe) return {};
    GstClock* c = gst_element_get_clock(m_pipe);
    if (!c) return {};
    QString n = QString::fromUtf8(GST_OBJECT_NAME(c));
    gst_object_unref(c);
    return n;
}

quint64 Player::frameSerial()
{
    QMutexLocker lock(&m_mutex);
    return m_serial;
}

GstSample* Player::latestSample(quint64* serial)
{
    QMutexLocker lock(&m_mutex);
    m_framePending = false;
    if (serial) *serial = m_serial;
    return m_sample ? gst_sample_ref(m_sample) : nullptr;
}

bool Player::frameLateness(GstSample* s, qint64* out) const
{
    if (!m_pipe || !s) return false;
    GstClock* clock = gst_element_get_clock(m_pipe);
    if (!clock) return false;
    const GstClockTime now = gst_clock_get_time(clock);
    gst_object_unref(clock);
    const GstClockTime base = gst_element_get_base_time(m_pipe);
    GstBuffer* b = gst_sample_get_buffer(s);
    const GstSegment* seg = gst_sample_get_segment(s);
    if (!b || !seg || !GST_BUFFER_PTS_IS_VALID(b)) return false;
    const guint64 rt = gst_segment_to_running_time(seg, GST_FORMAT_TIME, GST_BUFFER_PTS(b));
    if (rt == GST_CLOCK_TIME_NONE) return false;
    *out = qint64(now - base) - qint64(rt);
    return true;
}

// ---- streaming-thread callbacks ------------------------------------------

void Player::storeSample(GstSample* s)
{
    m_lastSampleMs = m_wall.elapsed();
    GstBuffer* b = gst_sample_get_buffer(s);
    const GstSegment* seg = gst_sample_get_segment(s);
    GstCaps* caps = gst_sample_get_caps(s);
    bool emitNow = false;
    {
        QMutexLocker lock(&m_mutex);
        if (b && seg && GST_BUFFER_PTS_IS_VALID(b)) {
            const guint64 st = gst_segment_to_stream_time(seg, GST_FORMAT_TIME, GST_BUFFER_PTS(b));
            if (st != GST_CLOCK_TIME_NONE) m_lastFrameStreamTime = qint64(st);
        }
        if (caps && caps != m_lastCaps) {
            gst_caps_replace(&m_lastCaps, caps);
            const GstStructure* st = gst_caps_get_structure(caps, 0);
            gint n = 0, d = 1;
            if (gst_structure_get_fraction(st, "framerate", &n, &d)) { m_fpsN = n; m_fpsD = d; }
        }
        if (m_sample) gst_sample_unref(m_sample);
        m_sample = s;
        ++m_serial;
        emitNow = !m_framePending.exchange(true);
    }
    if (emitNow) emit frameReady();   // queued to the GUI thread
}

GstFlowReturn Player::onNewSample(GstElement* sink, gpointer self)
{
    GstSample* s = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (s) static_cast<Player*>(self)->storeSample(s);
    return GST_FLOW_OK;
}

GstFlowReturn Player::onNewPreroll(GstElement* sink, gpointer self)
{
    GstSample* s = gst_app_sink_pull_preroll(GST_APP_SINK(sink));
    if (s) static_cast<Player*>(self)->storeSample(s);
    return GST_FLOW_OK;
}

void Player::onDeepElementAdded(GstBin*, GstBin*, GstElement* el, gpointer self)
{
    auto* p = static_cast<Player*>(self);
    GstElementFactory* f = gst_element_get_factory(el);
    if (f) {   // the elements whose settings the player adjusts: the subtitle text drawer, the deinterlacer
        const gchar* fname = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(f));
        if (g_strcmp0(fname, "textoverlay") == 0) {
            {
                QMutexLocker lock(&p->m_mutex);
                if (!p->m_textOverlays.contains(el)) {
                    if (p->m_textOverlays.size() >= 8) gst_object_unref(p->m_textOverlays.takeFirst());
                    p->m_textOverlays.push_back(GST_ELEMENT(gst_object_ref(el)));
                }
            }
            QMetaObject::invokeMethod(p, [p] { p->applySubtitleStyle(); }, Qt::QueuedConnection);
            return;
        }
        // Matroska files whose subtitle lines are stored well ahead of the picture (as many programs other than
        // mkvmerge write them): on meeting such a line the demuxer takes the picture to be lagging far behind and
        // tells it "nothing for this stretch". The decoder, told so, throws away what it holds, and the picture
        // then stands still until the next keyframe: for seconds, most visibly right after a jump. The picture is
        // not lagging, its frames follow: the notice is dropped on its way out of the demuxer.
        if (g_strcmp0(fname, "matroskademux") == 0) {
            g_signal_connect(el, "pad-added", G_CALLBACK(&onMatroskaPad), nullptr);
            return;
        }
        if (g_strcmp0(fname, "subtitleoverlay") == 0) {
            if (el == p->m_feed.overlay()) return;   // (the feed's own: set up by applySubtitleShown)
            {
                QMutexLocker lock(&p->m_mutex);
                if (p->m_subOverlay) gst_object_unref(p->m_subOverlay);
                p->m_subOverlay = GST_ELEMENT(gst_object_ref(el));
            }
            // Silent from the start when subtitles are not wanted: not even the first line shows.
            g_object_set(el, "silent", p->m_subShown ? FALSE : TRUE, nullptr);
            QMetaObject::invokeMethod(p, [p] { p->applySubtitleShown(); }, Qt::QueuedConnection);
            return;
        }
        if (g_strcmp0(fname, "deinterlace") == 0) {
            QMutexLocker lock(&p->m_mutex);
            if (p->m_deintEl) gst_object_unref(p->m_deintEl);
            p->m_deintEl = GST_ELEMENT(gst_object_ref(el));
            return;
        }
    }
    if (!f || !klassHas(f, "Decoder")) return;
    const QString name = QString::fromUtf8(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(f)));
    {
        QMutexLocker lock(&p->m_mutex);
        if (klassHas(f, "Video")) {
            p->m_videoDecoder = name;
            p->m_videoDecoderHw = isHwVideoDecoderFactory(f);
            if (p->m_videoDec) gst_object_unref(p->m_videoDec);
            p->m_videoDec = p->m_videoDecoderHw ? nullptr : GST_ELEMENT(gst_object_ref(el));
            p->m_governor.attach(el);   // (2.17: leaves pictures out before decoding when the computer cannot keep up)
        }
        else if (klassHas(f, "Audio")) p->m_audioDecoder = name;
        else return;
    }
    emit p->decoderChanged();
}

void Player::setHttpHeaders(const QList<QPair<QByteArray, QByteArray>>& headers)
{
    QMutexLocker lock(&m_mutex);
    m_httpHeaders = headers;
}

void Player::onSourceSetup(GstElement*, GstElement* source, gpointer self)
{
    auto* p = static_cast<Player*>(self);
    if (crt_web_is_source(source)) {   // (its streams' levels are watched: watchNetwork)
        QMutexLocker lock(&p->m_mutex);
        if (p->m_webSource) gst_object_unref(p->m_webSource);
        p->m_webSource = GST_ELEMENT(gst_object_ref(source));
    }
    GObjectClass* klass = G_OBJECT_GET_CLASS(source);
    if (g_object_class_find_property(klass, "user-agent"))
        g_object_set(source, "user-agent", "CRT-Player", nullptr);
    QList<QPair<QByteArray, QByteArray>> headers;
    {
        QMutexLocker lock(&p->m_mutex);
        headers = p->m_httpHeaders;
    }
    if (headers.isEmpty() || !g_object_class_find_property(klass, "extra-headers")) return;
    GstStructure* st = gst_structure_new_empty("extra-headers");
    for (const auto& h : headers) {
        const QByteArray lower = h.first.toLower();
        // (a web site's own headers, 2.17: who is asking is a property of the source, and what is the
        // source's own business is left to it)
        if (lower == "user-agent") { g_object_set(source, "user-agent", h.second.constData(), nullptr); continue; }
        if (lower == "range" || lower == "accept-encoding" || lower == "host" || lower == "connection" || lower == "content-length") continue;
        gst_structure_set(st, h.first.constData(), G_TYPE_STRING, h.second.constData(), nullptr);
    }
    if (gst_structure_n_fields(st) > 0) g_object_set(source, "extra-headers", st, nullptr);
    gst_structure_free(st);
}

void Player::onStreamsChanged(GstElement*, gpointer self)
{
    auto* p = static_cast<Player*>(self);
    QMetaObject::invokeMethod(p, [p] { p->refreshTracks(); }, Qt::QueuedConnection);
}

GstBusSyncReply Player::busSyncHandler(GstBus*, GstMessage* m, gpointer self)
{
    auto* p = static_cast<Player*>(self);
    // The source of the subtitle lines failing is no reason to stop the video: it is started again with the next jump.
    if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR && p->m_feed.sourceFailed(GST_MESSAGE_SRC(m))) return GST_BUS_DROP;
    switch (GST_MESSAGE_TYPE(m)) {
    case GST_MESSAGE_ERROR: case GST_MESSAGE_WARNING: case GST_MESSAGE_EOS:
    case GST_MESSAGE_STATE_CHANGED: case GST_MESSAGE_ASYNC_DONE: case GST_MESSAGE_DURATION_CHANGED:
    case GST_MESSAGE_TAG: case GST_MESSAGE_ELEMENT: case GST_MESSAGE_STEP_DONE: case GST_MESSAGE_TOC:
        break;
    default:
        return GST_BUS_DROP;
    }
    if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_STATE_CHANGED && GST_MESSAGE_SRC(m) != GST_OBJECT(p->m_pipe))
        return GST_BUS_DROP;
    gst_message_ref(m);
    const quint64 gen = p->m_generation;
    QPointer<Player> guard(p);
    QMetaObject::invokeMethod(p, [guard, m, gen] {
        if (guard) guard->handleMessage(m, gen);
        gst_message_unref(m);
    }, Qt::QueuedConnection);
    return GST_BUS_DROP;
}

// ---- GUI-thread message handling ------------------------------------------

QString Player::describeMissing() const
{
    if (m_missing.isEmpty()) return {};
    QString s = tr("GStreamer on this system has no plugin for:\n");
    for (const QString& m : m_missing) s += QStringLiteral("  • ") + m + '\n';
    s += tr("\nThe usual full set of GStreamer plugins for %1 installs with:\n  %2\n")
             .arg(Distro::prettyName(), Distro::fullCodecCommand());
    const QString note = Distro::codecNote();
    if (!note.isEmpty()) s += '\n' + note + '\n';
    s += tr("\nTo see what is installed: gst-inspect-1.0 | grep -i -E 'dec|demux'");
    return s;
}

void Player::retryInSoftware()
{
    const qint64 pos = position();
    const bool play = m_targetPlaying;
    const QString dec = videoDecoder();
    m_hwRetried = true;
    applyDecoderPolicy(false);   // for this media only; the next open() restores the policy
    m_carrySel = m_subSel;       // (the subtitle track that was showing stays)
    m_carryPicked = m_subPicked;
    openInternal(m_uri, play, pos, true);
    emit warningOccurred(tr("Hardware decoder %1 failed; switched to software decoding.").arg(dec));
}

void Player::handleMessage(GstMessage* m, quint64 generation)
{
    if (generation != m_generation || !m_pipe) return;
    switch (GST_MESSAGE_TYPE(m)) {
    case GST_MESSAGE_ERROR: {
        GError* err = nullptr;
        gchar* dbg = nullptr;
        gst_message_parse_error(m, &err, &dbg);
        const QString msg = err ? QString::fromUtf8(err->message) : tr("Unknown error");
        const QString debug = dbg ? QString::fromUtf8(dbg) : QString();
        g_clear_error(&err);
        g_free(dbg);
        // One failure brings several messages (the element that failed, then those that waited for it: with
        // the picture and the sound as two streams, twice over). The first one says what happened.
        if (m_state == State::Error) { qWarning() << "GStreamer error (after the one reported):" << msg; break; }
        bool hwInvolved = videoDecoderIsHardware();
        m_sourceError = false;
        if (GST_IS_ELEMENT(GST_MESSAGE_SRC(m))) {
            GstElementFactory* f = gst_element_get_factory(GST_ELEMENT(GST_MESSAGE_SRC(m)));
            if (f && isHwVideoDecoderFactory(f)) hwInvolved = true;
            // (the element that reads from the network: an address that has run out, a server that refuses)
            if (f && klassHas(f, "Source")) m_sourceError = true;
        }
        if (m_hwEnabled && hwInvolved && !m_hwRetried) {
            qWarning() << "Hardware decoding error, retrying in software:" << msg;
            retryInSoftware();
            return;
        }
        QString details = msg;
        const QString missing = describeMissing();
        if (!missing.isEmpty()) details = missing + QStringLiteral("\n\nGStreamer said: ") + msg;
        if (!debug.isEmpty()) details += QStringLiteral("\n\nDetails: ") + debug;
        const QString title = missing.isEmpty() ? tr("Cannot play this file") : tr("Missing codec or GStreamer plugin");
        gst_element_set_state(m_pipe, GST_STATE_NULL);
        setState(State::Error);
        emit errorOccurred(title, details);
        break;
    }
    case GST_MESSAGE_TOC: {
        GstToc* toc = nullptr;
        gst_message_parse_toc(m, &toc, nullptr);
        if (toc) { readToc(toc); gst_toc_unref(toc); }
        break;
    }
    case GST_MESSAGE_WARNING: {
        GError* err = nullptr;
        gchar* dbg = nullptr;
        gst_message_parse_warning(m, &err, &dbg);
        qWarning() << "GStreamer warning:" << (err ? err->message : "") << (dbg ? dbg : "");
        g_clear_error(&err);
        g_free(dbg);
        break;
    }
    case GST_MESSAGE_ELEMENT:
        if (gst_is_missing_plugin_message(m)) {
            gchar* d = gst_missing_plugin_message_get_description(m);
            const QString desc = QString::fromUtf8(d ? d : "unknown capability");
            g_free(d);
            if (!m_missing.contains(desc)) m_missing << desc;
        }
        break;
    case GST_MESSAGE_EOS:
        if (m_endReported) break;   // (the end watch was first)
        m_endReported = true;
        emit endOfStream();
        break;
    case GST_MESSAGE_STATE_CHANGED: {
        GstState oldS, newS, pending;
        gst_message_parse_state_changed(m, &oldS, &newS, &pending);
        if (m_state == State::Error) break;   // (what a pipeline that failed still had to say about its states)
        if (newS == GST_STATE_PLAYING) { m_heldByWait = false; setState(State::Playing); switchPendingAudio(); }
        // (waiting for the network is not "paused" for the viewer, nor is the moment it takes to play on)
        else if (newS == GST_STATE_PAUSED && m_loaded && !m_heldByWait) setState(State::Paused);
        break;
    }
    case GST_MESSAGE_STEP_DONE:
        break;
    case GST_MESSAGE_ASYNC_DONE: {
        if (!m_loaded) {
            m_loaded = true;
            gint64 dur = -1;
            if (gst_element_query_duration(m_pipe, GST_FORMAT_TIME, &dur)) m_duration = dur;
            emit durationChanged(m_duration);
            g_object_get(m_pipe, "current-text", &m_textTrackRead, nullptr);   // (the track the file began to be read with)
            refreshTracks();
            emit mediaLoaded();
            if (m_startPos <= 0 && std::abs(m_rate - 1.0) > 1e-6) doSeek(position(), SeekMode::Accurate);   // keep the speed
            if (!m_missing.isEmpty())
                emit warningOccurred(tr("Playing without: %1 (GStreamer plugin not installed)").arg(m_missing.join(QStringLiteral(", "))));
            if (m_nVideo == 0)
                emit warningOccurred(tr("This file has no video stream the player can show."));
            if (m_startPos > 0) {
                doSeek(m_startPos, SeekMode::Accurate);
                m_startPos = 0;
            }
            if (m_targetPlaying) gst_element_set_state(m_pipe, GST_STATE_PLAYING);
            else setState(State::Paused);
            break;
        }
        m_seekWatchdog.stop();
        m_seekInFlight = false;
        if (m_hasPendingSeek) {
            m_hasPendingSeek = false;
            doSeek(m_pendingSeek, m_pendingMode);
        } else {
            emit seekFinished();
            switchPendingAudio();
            if (m_refreshWanted) { m_refreshWanted = false; refreshSubtitles(); }
        }
        break;
    }
    case GST_MESSAGE_DURATION_CHANGED: {
        gint64 dur = -1;
        if (gst_element_query_duration(m_pipe, GST_FORMAT_TIME, &dur) && dur != m_duration) {
            m_duration = dur;
            emit durationChanged(dur);
        }
        break;
    }
    case GST_MESSAGE_TAG: {
        GstTagList* tags = nullptr;
        gst_message_parse_tag(m, &tags);
        if (!tags) break;
        const QString orient = tagString(tags, GST_TAG_IMAGE_ORIENTATION);
        if (!orient.isEmpty() && orient != m_orientationTag) {
            m_orientationTag = orient;
            emit orientationChanged(orient);
        }
        const QString vc = tagString(tags, GST_TAG_VIDEO_CODEC);
        if (!vc.isEmpty()) m_videoCodec = vc;
        const QString ac = tagString(tags, GST_TAG_AUDIO_CODEC);
        if (!ac.isEmpty() && m_audioCodec.isEmpty()) m_audioCodec = ac;
        const QString cf = tagString(tags, GST_TAG_CONTAINER_FORMAT);
        if (!cf.isEmpty()) m_container = cf;
        gst_tag_list_unref(tags);
        break;
    }
    default:
        break;
    }
}

void Player::refreshTracks()
{
    if (!m_pipe) return;
    gint nA = 0, nT = 0, nV = 0;
    g_object_get(m_pipe, "n-audio", &nA, "n-text", &nT, "n-video", &nV, nullptr);
    m_nVideo = nV;
    QVector<TrackInfo> a, t;
    for (int i = 0; i < nA; ++i) {
        GstTagList* tags = nullptr;
        g_signal_emit_by_name(m_pipe, "get-audio-tags", i, &tags);
        a.push_back({i, trackLabel(i, tags, GST_TAG_AUDIO_CODEC), trackLang(tags)});
        if (tags) gst_tag_list_unref(tags);
    }
    for (int i = 0; i < nT; ++i) {
        GstTagList* tags = nullptr;
        g_signal_emit_by_name(m_pipe, "get-text-tags", i, &tags);
        t.push_back({i, trackLabel(i, tags, GST_TAG_SUBTITLE_CODEC), trackLang(tags)});
        if (tags) gst_tag_list_unref(tags);
    }
    for (int i = 0; i < nV && m_orientationTag == QLatin1String("rotate-0"); ++i) {
        GstTagList* tags = nullptr;
        g_signal_emit_by_name(m_pipe, "get-video-tags", i, &tags);
        const QString orient = tagString(tags, GST_TAG_IMAGE_ORIENTATION);
        if (tags) gst_tag_list_unref(tags);
        if (!orient.isEmpty() && orient != m_orientationTag) {
            m_orientationTag = orient;
            emit orientationChanged(orient);
        }
    }
    m_audioTracks = a;
    // With the feed, a subtitle file is not playbin's: it is listed after the file's own tracks, as playbin did.
    if (nT != m_nEmbeddedText && !m_trackFileWaiting) m_trackFileAsked = -1;   // (more tracks have turned up: the answer may be another now)
    m_nEmbeddedText = nT;
    if (m_feedOn && !m_subUri.isEmpty()) t.push_back({nT, trackLabel(nT, nullptr, GST_TAG_SUBTITLE_CODEC), QString()});
    m_textTracks = t;
    if (m_feedOn && m_carrySel >= 0 && m_carrySel < m_textTracks.size()) {   // (opened again by the player itself: the track that was showing)
        m_subSel = m_carrySel;
        m_carrySel = -1;
    }
    if (m_feedOn && (m_subSel < 0 || m_subSel >= m_textTracks.size()) && !m_textTracks.isEmpty()) {
        // As playbin chose: the subtitle file if there is one, else the file's first track.
        gint cur = 0;
        g_object_get(m_pipe, "current-text", &cur, nullptr);
        m_subSel = !m_subUri.isEmpty() ? nT : std::clamp(int(cur), 0, int(m_textTracks.size()) - 1);
    }
    applyTrackPreferences();
    updateSubtitleFeed();
    emit tracksChanged();
}
