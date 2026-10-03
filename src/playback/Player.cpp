#include <functional>
#include "Player.h"
#include "Distro.h"
#include "TapeAudio.h"

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
    m_seekWatchdog.setSingleShot(true);
    m_seekWatchdog.setInterval(1500);
    connect(&m_seekWatchdog, &QTimer::timeout, this, [this] {
        // A flushing seek should always complete with ASYNC_DONE; never leave the UI stuck.
        m_seekInFlight = false;
        if (m_hasPendingSeek) { m_hasPendingSeek = false; doSeek(m_pendingSeek, m_pendingMode); }
    });
}

Player::~Player() { teardown(); }

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
    return openInternal(uri, autoplay, startNs, false);
}

bool Player::openInternal(const QString& uri, bool autoplay, qint64 startNs, bool isRetry)
{
    teardown();
    if (!isRetry) applyDecoderPolicy(m_hwEnabled);
    m_uri = uri;
    m_targetPlaying = autoplay;
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

    m_pipe = gst_element_factory_make("playbin", "crtplayer");
    GstElement* conv = gst_element_factory_make("videoconvert", nullptr);
    m_appsink = gst_element_factory_make("appsink", "crtsink");
    if (!m_pipe || !conv || !m_appsink) {
        if (conv) gst_object_unref(conv);
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
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(conv), "n-threads"))
        g_object_set(conv, "n-threads", 0u, nullptr);
    GstCaps* caps = gst_caps_from_string("video/x-raw, format=(string){ NV12, I420, BGRx, BGRA, RGBx, RGBA }");
    g_object_set(m_appsink, "caps", caps, "sync", TRUE, "max-buffers", 2u, "drop", TRUE,
                 "enable-last-sample", FALSE, "qos", TRUE, nullptr);
    gst_caps_unref(caps);
    GstAppSinkCallbacks cb{};
    cb.new_sample = [](GstAppSink* s, gpointer self) { return onNewSample(GST_ELEMENT(s), self); };
    cb.new_preroll = [](GstAppSink* s, gpointer self) { return onNewPreroll(GST_ELEMENT(s), self); };
    gst_app_sink_set_callbacks(GST_APP_SINK(m_appsink), &cb, this, nullptr);
    gst_bin_add_many(GST_BIN(bin), conv, m_appsink, nullptr);
    gst_element_link(conv, m_appsink);
    GstPad* pad = gst_element_get_static_pad(conv, "sink");
    gst_element_add_pad(bin, gst_ghost_pad_new("sink", pad));
    gst_object_unref(pad);

    g_object_set(m_pipe, "video-sink", bin, "uri", uri.toUtf8().constData(), nullptr);
    if (!m_subUri.isEmpty()) g_object_set(m_pipe, "suburi", m_subUri.toUtf8().constData(), nullptr);
    if (GstElement* as = fallbackAudioSink(&m_audioOutput)) g_object_set(m_pipe, "audio-sink", as, nullptr);
    // Keeps voices at their normal pitch when the speed changes (passthrough at 1x).
    // Audio chain: natural-sounding speed changes, then the look's tape and speaker sound.
    {
        crtTapeRegister();
        GstElement* bin = gst_bin_new("audiochain");
        GstElement* st = gst_element_factory_make("scaletempo", nullptr);
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
    flags |= kFlagText;
    m_subShown = m_subsWanted;
    if (m_deinterlace) flags |= kFlagDeinterlace; else flags &= ~kFlagDeinterlace;
    flags &= ~kFlagSoftColorbalance;  // colour controls live in the shader
    g_object_set(m_pipe, "flags", flags, nullptr);
    m_audioPicked = false;
    m_pendingAudio = -1;
    m_prefMuted = false;
    m_subPicked = m_subUriChosen;
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
        gst_element_set_state(m_pipe, GST_STATE_NULL);
        gst_object_unref(m_pipe);
        m_pipe = nullptr;
        m_appsink = nullptr;
        m_tape = nullptr;   // it belonged to the pipeline
    }
    QMutexLocker lock(&m_mutex);
    if (m_textOverlay) { gst_object_unref(m_textOverlay); m_textOverlay = nullptr; }
    if (m_subOverlay) { gst_object_unref(m_subOverlay); m_subOverlay = nullptr; }
    if (m_deintEl) { gst_object_unref(m_deintEl); m_deintEl = nullptr; }
    if (m_sample) { gst_sample_unref(m_sample); m_sample = nullptr; }
    if (m_lastCaps) { gst_caps_unref(m_lastCaps); m_lastCaps = nullptr; }
    m_videoDecoder.clear();
    m_audioDecoder.clear();
    m_videoDecoderHw = false;
    m_framePending = false;
    m_lastFrameStreamTime = -1;
    m_fpsN = 0; m_fpsD = 1;
}

void Player::close()
{
    teardown();
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

void Player::play()
{
    if (!m_pipe) return;
    m_targetPlaying = true;
    gst_element_set_state(m_pipe, GST_STATE_PLAYING);
}

void Player::pause()
{
    if (!m_pipe) return;
    m_targetPlaying = false;
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
    if (mode == SeekMode::Accurate) flags |= GST_SEEK_FLAG_ACCURATE;
    else flags |= GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_SNAP_NEAREST;
    m_seekTarget = posNs;
    m_seekInFlight = true;
    m_seekWatchdog.start();
    if (!gst_element_seek(m_pipe, m_rate, GST_FORMAT_TIME, GstSeekFlags(flags), GST_SEEK_TYPE_SET, posNs,
                          GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
        m_seekInFlight = false;
        m_seekWatchdog.stop();
    }
}

void Player::seekKeyframe(bool forward)
{
    if (!m_pipe) return;
    const qint64 pos = position();
    // Just past the current frame, snapping to the keyframe after it (or before it).
    const qint64 target = forward ? pos + 40000000 : std::max<qint64>(0, pos - 1000000);
    const int flags = GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT | (forward ? GST_SEEK_FLAG_SNAP_AFTER : GST_SEEK_FLAG_SNAP_BEFORE);
    m_seekTarget = target;
    m_seekInFlight = true;
    m_seekWatchdog.start();
    if (!gst_element_seek(m_pipe, m_rate, GST_FORMAT_TIME, GstSeekFlags(flags), GST_SEEK_TYPE_SET, target,
                          GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
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
    m_subsWanted = on;
    m_subPicked = false;
    m_subShown = on;
    if (!m_pipe) return;
    applySubtitleShown();
    if (on) applyTrackPreferences();
}

// Shows or hides the subtitles of the current video (the overlay draws them or stays silent).
void Player::applySubtitleShown()
{
    GstElement* ov = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        if (m_subOverlay) ov = GST_ELEMENT(gst_object_ref(m_subOverlay));
    }
    if (!ov) return;
    g_object_set(ov, "silent", m_subShown ? FALSE : TRUE, nullptr);
    // The subtitle delay: an offset on the overlay's subtitle input, so it holds for every
    // kind of subtitle (playbin's own text-offset misses subtitles stored inside the video).
    if (GstPad* pad = gst_element_get_static_pad(ov, "subtitle_sink")) {
        gst_pad_set_offset(pad, gint64(m_subDelayMs) * GST_MSECOND);
        gst_object_unref(pad);
    }
    gst_object_unref(ov);
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
        gint cur = -1;
        g_object_get(m_pipe, "current-text", &cur, nullptr);
        const bool curMatches = cur >= 0 && cur < m_textTracks.size() && m_textTracks[cur].lang == m_prefSubLang;
        if (!curMatches)
            for (const TrackInfo& t : m_textTracks)
                if (t.lang == m_prefSubLang) { g_object_set(m_pipe, "current-text", t.index, nullptr); break; }
    }
}

void Player::setAudioDelay(int ms)
{
    m_audioDelayMs = std::clamp(ms, -10000, 10000);
    applyOffsets();
}

void Player::setSubtitleDelay(int ms)
{
    m_subDelayMs = std::clamp(ms, -60000, 60000);
    applyOffsets();
}

void Player::applyOffsets()
{
    if (!m_pipe) return;
    // playbin's av-offset holds the picture back when positive, so the sound's delay is its negative.
    g_object_set(m_pipe, "av-offset", -gint64(m_audioDelayMs) * GST_MSECOND, nullptr);
    applySubtitleShown();   // (the subtitle delay lives on the overlay)
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
    GstElement* ov = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        if (m_textOverlay) ov = GST_ELEMENT(gst_object_ref(m_textOverlay));
    }
    if (!ov) return;
    const guint color = m_subStyle.color == 1 ? 0xFFFFE94Du : 0xFFFFFFFFu;
    const bool box = m_subStyle.background == 1;
    g_object_set(ov, "font-desc", font.constData(), "color", color, "outline-color", 0xFF000000u, "draw-outline", TRUE,
                 "draw-shadow", box ? FALSE : TRUE, "shaded-background", box ? TRUE : FALSE, "shading-value", 150u, nullptr);
    // valignment: 1 bottom, 2 top, 3 a position down the picture (0..1)
    if (m_subStyle.position == 2) g_object_set(ov, "valignment", 2, "ypad", 25, nullptr);
    else if (m_subStyle.position == 1) g_object_set(ov, "valignment", 3, "ypos", 0.80, nullptr);
    else g_object_set(ov, "valignment", 1, "ypad", 25, nullptr);
    gst_object_unref(ov);
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

void Player::setRate(double rate)
{
    rate = std::clamp(rate, 0.25, 4.0);
    if (std::abs(rate - m_rate) < 1e-6) return;
    m_rate = rate;
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
    gint cur = -1;
    g_object_get(m_pipe, "current-text", &cur, nullptr);
    return cur;
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
    if (m_subShown) {
        gint cur = -1;
        g_object_get(m_pipe, "current-text", &cur, nullptr);
        if (cur != idx) g_object_set(m_pipe, "current-text", idx, nullptr);
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
                if (p->m_textOverlay) gst_object_unref(p->m_textOverlay);
                p->m_textOverlay = GST_ELEMENT(gst_object_ref(el));
            }
            QMetaObject::invokeMethod(p, [p] { p->applySubtitleStyle(); }, Qt::QueuedConnection);
            return;
        }
        if (g_strcmp0(fname, "subtitleoverlay") == 0) {
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
        if (klassHas(f, "Video")) { p->m_videoDecoder = name; p->m_videoDecoderHw = isHwVideoDecoderFactory(f); }
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
    for (const auto& h : headers) gst_structure_set(st, h.first.constData(), G_TYPE_STRING, h.second.constData(), nullptr);
    g_object_set(source, "extra-headers", st, nullptr);
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
        bool hwInvolved = videoDecoderIsHardware();
        if (GST_IS_ELEMENT(GST_MESSAGE_SRC(m))) {
            GstElementFactory* f = gst_element_get_factory(GST_ELEMENT(GST_MESSAGE_SRC(m)));
            if (f && isHwVideoDecoderFactory(f)) hwInvolved = true;
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
        emit endOfStream();
        break;
    case GST_MESSAGE_STATE_CHANGED: {
        GstState oldS, newS, pending;
        gst_message_parse_state_changed(m, &oldS, &newS, &pending);
        if (newS == GST_STATE_PLAYING) { setState(State::Playing); switchPendingAudio(); }
        else if (newS == GST_STATE_PAUSED && m_loaded) setState(State::Paused);
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
    m_textTracks = t;
    applyTrackPreferences();
    emit tracksChanged();
}
