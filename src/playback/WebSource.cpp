#include "WebSource.h"

#include <QHash>
#include <QJsonArray>
#include <QMutex>
#include <QStringList>

#include <algorithm>
#include <atomic>

#include <cstring>

namespace {
QMutex g_lock;
QHash<QString, QList<WebStream>> g_streams;
int g_next = 1;

// Each stream's buffer: how much of the file is kept in memory around the place being played, and how much
// of that may lie ahead of it (the rest stays behind it, for a jump back). Read ahead generously: the MP4
// demuxer looks at the next fragment's header while it still plays the current one, megabytes on; with
// GStreamer's usual 2 MB of reading ahead that was a new request to the server every few seconds.
const guint64 kVideoRing = 32u * 1024 * 1024, kVideoAhead = 24u * 1024 * 1024;
const guint64 kAudioRing = 4u * 1024 * 1024, kAudioAhead = 3u * 1024 * 1024;

QList<WebStream> lookup(const QString& uri)
{
    QMutexLocker lock(&g_lock);
    return g_streams.value(uri);
}

// What a reader has delivered, kept beside its ring buffer.
struct StreamStats {
    std::atomic<quint64> received{0};
    std::atomic<gint64> lastData{0};   // g_get_monotonic_time()
    std::atomic<bool> ended{false};
    bool audioOnly = false;
    int kbps = 0;
};

GstPadProbeReturn onReaderData(GstPad*, GstPadProbeInfo* info, gpointer data)
{
    auto* st = static_cast<StreamStats*>(data);
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
        st->received += gst_buffer_get_size(GST_PAD_PROBE_INFO_BUFFER(info));
        st->lastData = g_get_monotonic_time();
        st->ended = false;
    } else if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
        const GstEventType type = GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info));
        if (type == GST_EVENT_EOS) st->ended = true;
        else if (type == GST_EVENT_SEGMENT || type == GST_EVENT_FLUSH_STOP) { st->ended = false; st->lastData = g_get_monotonic_time(); }
    }
    return GST_PAD_PROBE_OK;
}
} // namespace

struct CrtWebSrc {
    GstBin parent;
    gchar* uri;
    gboolean built;
};
struct CrtWebSrcClass { GstBinClass parent_class; };

static void crt_web_src_uri_handler_init(gpointer iface, gpointer);

G_DEFINE_TYPE_WITH_CODE(CrtWebSrc, crt_web_src, GST_TYPE_BIN, G_IMPLEMENT_INTERFACE(GST_TYPE_URI_HANDLER, crt_web_src_uri_handler_init))

static GstStaticPadTemplate crt_web_src_template = GST_STATIC_PAD_TEMPLATE("src_%u", GST_PAD_SRC, GST_PAD_SOMETIMES, GST_STATIC_CAPS_ANY);

// One HTTP reader and its ring buffer for each address; the buffers' outputs become this element's pads.
static gboolean crt_web_src_build(CrtWebSrc* self)
{
    if (self->built) return TRUE;
    const QList<WebStream> streams = lookup(QString::fromUtf8(self->uri ? self->uri : ""));
    if (streams.isEmpty()) {
        GST_ELEMENT_ERROR(self, RESOURCE, NOT_FOUND, ("No streams are known for this address."), ("%s", self->uri ? self->uri : "(none)"));
        return FALSE;
    }
    guint n = 0;
    for (const WebStream& st : streams) {
        GstElement* http = gst_element_factory_make("souphttpsrc", nullptr);
        GstElement* ring = gst_element_factory_make("queue2", nullptr);
        if (!http || !ring) {
            if (http) gst_object_unref(http);
            if (ring) gst_object_unref(ring);
            GST_ELEMENT_ERROR(self, CORE, MISSING_PLUGIN, ("GStreamer's HTTP source (souphttpsrc) or queue2 is missing."), (nullptr));
            return FALSE;
        }
        g_object_set(http, "location", st.url.toUtf8().constData(), "automatic-redirect", TRUE, nullptr);
        GstStructure* extra = gst_structure_new_empty("extra-headers");
        QStringList cookies;
        for (const auto& h : st.headers) {
            const QByteArray name = h.first.trimmed(), lower = name.toLower();
            if (lower == "user-agent") g_object_set(http, "user-agent", h.second.constData(), nullptr);
            else if (lower == "cookie") cookies << QString::fromUtf8(h.second).split(QStringLiteral("; "), Qt::SkipEmptyParts);
            // (the reader's own business: byte ranges, and no compression of what is asked for by range)
            else if (lower == "range" || lower == "accept-encoding" || lower == "host" || lower == "connection" || lower == "content-length") continue;
            else if (!name.isEmpty()) gst_structure_set(extra, name.constData(), G_TYPE_STRING, h.second.constData(), nullptr);
        }
        if (gst_structure_n_fields(extra) > 0) g_object_set(http, "extra-headers", extra, nullptr);
        gst_structure_free(extra);
        if (!cookies.isEmpty()) {
            GPtrArray* arr = g_ptr_array_new_with_free_func(g_free);
            for (const QString& c : cookies) g_ptr_array_add(arr, g_strdup(c.toUtf8().constData()));
            g_ptr_array_add(arr, nullptr);
            g_object_set(http, "cookies", reinterpret_cast<gchar**>(arr->pdata), nullptr);
            g_ptr_array_free(arr, TRUE);
        }
        // A ring buffer: what was read stays at hand (a short jump needs no new request), and downstream
        // may ask for any part of the file, which the buffer fetches by a byte range.
        g_object_set(ring, "ring-buffer-max-size", st.audioOnly ? kAudioRing : kVideoRing, "max-size-bytes", guint(st.audioOnly ? kAudioAhead : kVideoAhead),
                     "max-size-buffers", 0u, "max-size-time", guint64(0), "use-buffering", FALSE, nullptr);
        g_object_set_data(G_OBJECT(http), "crt-http", GINT_TO_POINTER(1));
        auto* stats = new StreamStats;
        stats->audioOnly = st.audioOnly;
        stats->kbps = st.kbps;
        stats->lastData = g_get_monotonic_time();
        g_object_set_data_full(G_OBJECT(ring), "crt-stats", stats, [](gpointer p) { delete static_cast<StreamStats*>(p); });
        gst_bin_add_many(GST_BIN(self), http, ring, nullptr);
        if (!gst_element_link(http, ring)) return FALSE;
        // (on the ring buffer's input: what the reader has handed over, and when)
        GstPad* in = gst_element_get_static_pad(ring, "sink");
        gst_pad_add_probe(in, GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_EVENT_FLUSH), onReaderData, stats, nullptr);
        gst_object_unref(in);
        GstPad* out = gst_element_get_static_pad(ring, "src");
        gchar* name = g_strdup_printf("src_%u", n++);
        GstPadTemplate* templ = gst_static_pad_template_get(&crt_web_src_template);
        GstPad* ghost = gst_ghost_pad_new_from_template(name, out, templ);
        gst_object_unref(templ);
        g_free(name);
        gst_object_unref(out);
        g_object_set_data(G_OBJECT(ring), "crt-ghost", ghost);   // (added to the element once it is paused)
    }
    self->built = TRUE;
    return TRUE;
}

static GstPadProbeReturn crt_web_src_hold_cb(GstPad*, GstPadProbeInfo*, gpointer) { return GST_PAD_PROBE_OK; }

// The HTTP readers are held back (they connect, and wait with their first bytes) or let go.
//
// Why: the readers start as soon as this element is paused, and at that moment nothing is connected to the
// ring buffers' outputs yet. The decoding bins are connected a moment later, and switch the ring buffers to
// random access; a reader that hands over a buffer just then is told "flushing", and stops for good: the
// video never opens. (Seen once in about fifteen starts with a server on the same computer, which answers
// within a millisecond; a server on the internet leaves more time. Now it cannot happen.)
static void crt_web_src_hold(CrtWebSrc* self, gboolean hold)
{
    GValue item = G_VALUE_INIT;
    GstIterator* it = gst_bin_iterate_elements(GST_BIN(self));
    while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
        GstElement* e = GST_ELEMENT(g_value_get_object(&item));
        if (g_object_get_data(G_OBJECT(e), "crt-http")) {
            GstPad* pad = gst_element_get_static_pad(e, "src");
            const gulong held = gulong(GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(pad), "crt-hold")));
            if (hold && !held) {
                const gulong id = gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, crt_web_src_hold_cb, nullptr, nullptr);
                g_object_set_data(G_OBJECT(pad), "crt-hold", GSIZE_TO_POINTER(gsize(id)));
            } else if (!hold && held) {
                g_object_set_data(G_OBJECT(pad), "crt-hold", nullptr);
                gst_pad_remove_probe(pad, held);
            }
            gst_object_unref(pad);
        }
        g_value_reset(&item);
    }
    g_value_unset(&item);
    gst_iterator_free(it);
}

static GstStateChangeReturn crt_web_src_change_state(GstElement* element, GstStateChange transition)
{
    CrtWebSrc* self = reinterpret_cast<CrtWebSrc*>(element);
    if (transition == GST_STATE_CHANGE_NULL_TO_READY && !crt_web_src_build(self)) return GST_STATE_CHANGE_FAILURE;
    if (transition == GST_STATE_CHANGE_READY_TO_PAUSED) crt_web_src_hold(self, TRUE);
    if (transition == GST_STATE_CHANGE_PAUSED_TO_READY) crt_web_src_hold(self, FALSE);
    const GstStateChangeReturn ret = GST_ELEMENT_CLASS(crt_web_src_parent_class)->change_state(element, transition);
    if (ret == GST_STATE_CHANGE_FAILURE) { crt_web_src_hold(self, FALSE); return ret; }
    if (transition == GST_STATE_CHANGE_READY_TO_PAUSED) {
        // (for the tests: the decoding bins are connected this much later, so that the readers have their
        // first bytes long before; without the hold above, no video then opens at all)
        if (const gchar* late = g_getenv("CRTPLAYER_WEB_CONNECT_DELAY_MS")) g_usleep(gulong(CLAMP(g_ascii_strtoll(late, nullptr, 10), 0, 2000)) * 1000);
        // The pads appear now, one after the other: whoever uses this source (uridecodebin) sets up a
        // decoding bin for each as it appears. (Pads that were there from the start would be taken for
        // one stream with a choice of outputs.)
        GValue item = G_VALUE_INIT;
        GstIterator* it = gst_bin_iterate_elements(GST_BIN(self));
        QList<GstPad*> pads;
        while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
            GstElement* e = GST_ELEMENT(g_value_get_object(&item));
            if (auto* ghost = static_cast<GstPad*>(g_object_steal_data(G_OBJECT(e), "crt-ghost"))) pads.append(ghost);
            g_value_reset(&item);
        }
        g_value_unset(&item);
        gst_iterator_free(it);
        std::sort(pads.begin(), pads.end(), [](GstPad* a, GstPad* b) { return std::strcmp(GST_PAD_NAME(a), GST_PAD_NAME(b)) < 0; });
        for (GstPad* p : pads) {
            gst_pad_set_active(p, TRUE);
            gst_element_add_pad(element, p);
        }
        if (!pads.isEmpty()) gst_element_no_more_pads(element);
        // Everything downstream is connected and reads the way it means to: the readers may deliver now.
        crt_web_src_hold(self, FALSE);
    }
    return ret;
}

static void crt_web_src_finalize(GObject* object)
{
    g_free(reinterpret_cast<CrtWebSrc*>(object)->uri);
    G_OBJECT_CLASS(crt_web_src_parent_class)->finalize(object);
}

static void crt_web_src_class_init(CrtWebSrcClass* klass)
{
    GstElementClass* element = GST_ELEMENT_CLASS(klass);
    G_OBJECT_CLASS(klass)->finalize = crt_web_src_finalize;
    element->change_state = crt_web_src_change_state;
    gst_element_class_add_static_pad_template(element, &crt_web_src_template);
    gst_element_class_set_static_metadata(element, "CRT Player web streams", "Source/Network",
                                          "The picture and the sound of a web video, each from its own address", "CRT Player");
}

static void crt_web_src_init(CrtWebSrc* self)
{
    self->uri = nullptr;
    self->built = FALSE;
    GST_OBJECT_FLAG_SET(self, GST_ELEMENT_FLAG_SOURCE);
}

static GstURIType crt_web_src_uri_type(GType) { return GST_URI_SRC; }
static const gchar* const* crt_web_src_protocols(GType)
{
    static const gchar* const protocols[] = {"crtweb", nullptr};
    return protocols;
}
static gchar* crt_web_src_get_uri(GstURIHandler* handler) { return g_strdup(reinterpret_cast<CrtWebSrc*>(handler)->uri); }
static gboolean crt_web_src_set_uri(GstURIHandler* handler, const gchar* uri, GError** error)
{
    CrtWebSrc* self = reinterpret_cast<CrtWebSrc*>(handler);
    if (GST_STATE(self) > GST_STATE_READY || self->built) {
        g_set_error_literal(error, GST_URI_ERROR, GST_URI_ERROR_BAD_STATE, "The address cannot be changed once the streams are set up");
        return FALSE;
    }
    g_free(self->uri);
    self->uri = g_strdup(uri);
    return TRUE;
}
static void crt_web_src_uri_handler_init(gpointer iface, gpointer)
{
    auto* h = static_cast<GstURIHandlerInterface*>(iface);
    h->get_type = crt_web_src_uri_type;
    h->get_protocols = crt_web_src_protocols;
    h->get_uri = crt_web_src_get_uri;
    h->set_uri = crt_web_src_set_uri;
}

void crt_web_init()
{
    static gsize once = 0;
    if (g_once_init_enter(&once)) {
        gst_element_register(nullptr, "crtwebsrc", GST_RANK_PRIMARY, crt_web_src_get_type());
        g_once_init_leave(&once, 1);
    }
}

QString crt_web_register(const QList<WebStream>& streams)
{
    crt_web_init();
    QMutexLocker lock(&g_lock);
    const QString uri = QStringLiteral("crtweb://%1").arg(g_next++);
    g_streams.insert(uri, streams);
    return uri;
}

void crt_web_forget(const QString& uri)
{
    QMutexLocker lock(&g_lock);
    g_streams.remove(uri);
}

bool crt_web_is(const QString& uri) { return uri.startsWith(QStringLiteral("crtweb://")); }

bool crt_web_is_source(GstElement* element) { return element && G_OBJECT_TYPE(element) == crt_web_src_get_type(); }

QList<WebLevel> crt_web_levels(GstElement* source)
{
    QList<WebLevel> out;
    if (!crt_web_is_source(source)) return out;
    const gint64 now = g_get_monotonic_time();
    GValue item = G_VALUE_INIT;
    GstIterator* it = gst_bin_iterate_elements(GST_BIN(source));
    while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
        GstElement* e = GST_ELEMENT(g_value_get_object(&item));
        if (auto* st = static_cast<StreamStats*>(g_object_get_data(G_OBJECT(e), "crt-stats"))) {
            WebLevel l;
            guint ahead = 0;
            g_object_get(e, "current-level-bytes", &ahead, nullptr);
            l.audioOnly = st->audioOnly;
            l.ahead = ahead;
            l.room = st->audioOnly ? kAudioAhead : kVideoAhead;
            l.received = st->received;
            l.ended = st->ended;
            l.quietMs = (now - st->lastData) / 1000;
            l.kbps = st->kbps;
            out.append(l);
        }
        g_value_reset(&item);
    }
    g_value_unset(&item);
    gst_iterator_free(it);
    std::stable_sort(out.begin(), out.end(), [](const WebLevel& a, const WebLevel& b) { return !a.audioOnly && b.audioOnly; });   // the picture first
    return out;
}

QJsonObject crt_web_report(const QString& uri)
{
    const QList<WebStream> streams = lookup(uri);
    QJsonArray kinds;
    for (const WebStream& s : streams) kinds.append(s.audioOnly ? QStringLiteral("sound") : QStringLiteral("picture"));
    return QJsonObject{{"streams", streams.size()}, {"kinds", kinds}};
}
