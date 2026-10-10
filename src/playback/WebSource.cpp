#include "WebSource.h"
#include "RangeSource.h"

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

// Each stream's reader keeps this much in memory around the place being played, and may read this much
// ahead of it. Read ahead generously: the MP4 demuxer looks at the next fragment's header while it still
// plays the current one, megabytes on.
const quint64 kVideoKeep = 64u * 1024 * 1024, kVideoAhead = 24u * 1024 * 1024;
const quint64 kAudioKeep = 8u * 1024 * 1024, kAudioAhead = 3u * 1024 * 1024;

QList<WebStream> lookup(const QString& uri)
{
    QMutexLocker lock(&g_lock);
    return g_streams.value(uri);
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

// One reader for each address (RangeSource.h); their outputs become this element's pads.
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
        GstElement* reader = crt_range_src_new(st, st.audioOnly ? kAudioAhead : kVideoAhead, st.audioOnly ? kAudioKeep : kVideoKeep);
        if (!reader) {
            GST_ELEMENT_ERROR(self, CORE, FAILED, ("The web stream reader could not be made."), (nullptr));
            return FALSE;
        }
        g_object_set_data(G_OBJECT(reader), "crt-audio", GINT_TO_POINTER(st.audioOnly ? 1 : 0));
        g_object_set_data(G_OBJECT(reader), "crt-kbps", GINT_TO_POINTER(st.kbps));
        gst_bin_add(GST_BIN(self), reader);
        GstPad* out = gst_element_get_static_pad(reader, "src");
        gchar* name = g_strdup_printf("src_%u", n++);
        GstPadTemplate* templ = gst_static_pad_template_get(&crt_web_src_template);
        GstPad* ghost = gst_ghost_pad_new_from_template(name, out, templ);
        gst_object_unref(templ);
        g_free(name);
        gst_object_unref(out);
        g_object_set_data(G_OBJECT(reader), "crt-ghost", ghost);   // (added to the element once it is paused)
    }
    self->built = TRUE;
    return TRUE;
}

static GstStateChangeReturn crt_web_src_change_state(GstElement* element, GstStateChange transition)
{
    CrtWebSrc* self = reinterpret_cast<CrtWebSrc*>(element);
    if (transition == GST_STATE_CHANGE_NULL_TO_READY && !crt_web_src_build(self)) return GST_STATE_CHANGE_FAILURE;
    const GstStateChangeReturn ret = GST_ELEMENT_CLASS(crt_web_src_parent_class)->change_state(element, transition);
    if (ret == GST_STATE_CHANGE_FAILURE) return ret;
    if (transition == GST_STATE_CHANGE_READY_TO_PAUSED) {
        // (for the tests: the decoding bins are connected this much later, so that the readers have their
        // first bytes long before. In 2.17 a reader could then push into a pad not yet connected, and the
        // video did not open; the readers now never push: the demuxers read from them, RangeSource.cpp.)
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
    GValue item = G_VALUE_INIT;
    GstIterator* it = gst_bin_iterate_elements(GST_BIN(source));
    while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
        GstElement* e = GST_ELEMENT(g_value_get_object(&item));
        if (crt_range_src_is(e)) {
            const RangeStats st = crt_range_src_stats(e);
            WebLevel l;
            l.audioOnly = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(e), "crt-audio")) != 0;
            l.ahead = st.ahead;
            l.room = st.room;
            l.received = st.received;
            l.ended = st.ended;
            l.quietMs = st.quietMs;
            l.kbps = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(e), "crt-kbps"));
            l.requests = st.requests;
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
