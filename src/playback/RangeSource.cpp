#include "RangeSource.h"

#include <gst/base/gstbasesrc.h>

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWaitCondition>

#include <algorithm>
#include <cstring>
#include <memory>

namespace {

// CRTPLAYER_WEB_TRACE=1: what the readers ask for and get, and where the demuxers wait (to standard error)
bool tracing()
{
    static const bool on = qEnvironmentVariableIsSet("CRTPLAYER_WEB_TRACE");
    return on;
}
#define WEBTRACE(...) do { if (tracing()) qInfo().noquote() << "web-reader:" << __VA_ARGS__; } while (0)

const quint64 kBlock = 64 * 1024;            // what is kept is kept in blocks of this size
const quint64 kDefaultChunk = 10ull << 20;   // the most one request asks for (yt-dlp's "http_chunk_size" for YouTube)
const quint64 kNear = 2ull << 20;            // a part this close behind the running request's next byte is waited for
const qint64 kGiveUpMs = 30000;              // a network that stays away this long is an error

// What a reader and its element share (the element reads in GStreamer's thread, the reader writes in the network thread).
struct Shared {
    QMutex lock;
    QWaitCondition changed;
    // what to read, and how
    QUrl url;
    QList<QPair<QByteArray, QByteArray>> headers;
    quint64 chunk = kDefaultChunk;
    quint64 ahead = 0, keep = 0;
    // the file
    qint64 size = -1;
    bool answered = false;   // the first answer has come (its size is known, or it never will be)
    QString error;           // set once: the element fails with it
    int status = 0;
    QHash<quint64, QByteArray> blocks;   // complete blocks (the last one of the file may be shorter)
    quint64 partialIndex = 0;            // the block the running request is filling
    QByteArray partial;
    // where the element reads
    quint64 readPos = 0;
    bool flushing = false;
    bool stopped = false;
    // for the network watch and the tests
    quint64 received = 0;
    gint64 lastData = 0;
    int requests = 0;
    quint64 cachedBytes = 0;

    quint64 blockLen(quint64 index) const
    {
        if (size < 0) return kBlock;
        const quint64 start = index * kBlock;
        return start >= quint64(size) ? 0 : std::min<quint64>(kBlock, quint64(size) - start);
    }
    // The first byte at or after `pos` that is not at hand (the file's size where everything to the end is).
    quint64 firstMissing(quint64 pos, quint64 limit) const
    {
        quint64 b = pos / kBlock;
        while (true) {
            const quint64 start = b * kBlock;
            if (size >= 0 && start >= quint64(size)) return quint64(size);
            if (start >= limit) return start;
            auto it = blocks.constFind(b);
            if (it != blocks.constEnd()) { ++b; continue; }
            if (b == partialIndex && !partial.isEmpty()) return std::max<quint64>(pos, start + quint64(partial.size()));
            return std::max<quint64>(pos, start);
        }
    }
    // Copies [from, to) into dst, if all of it is at hand.
    bool copy(quint64 from, quint64 to, guint8* dst) const
    {
        if (firstMissing(from, to) < to) return false;
        quint64 p = from;
        while (p < to) {
            const quint64 b = p / kBlock, off = p % kBlock;
            const QByteArray& data = blocks.contains(b) ? *blocks.constFind(b) : partial;
            const quint64 n = std::min<quint64>(to - p, quint64(data.size()) - off);
            std::memcpy(dst + (p - from), data.constData() + off, n);
            p += n;
        }
        return true;
    }
    // Keeps what is kept within its bounds: the blocks farthest from where the element reads go first.
    void evict()
    {
        if (cachedBytes <= keep) return;
        QList<quint64> order = blocks.keys();
        const quint64 here = readPos / kBlock;
        // (behind the place being read counts double: ahead is what will be played)
        auto dist = [here](quint64 b) { return b >= here ? (b - here) : 2 * (here - b); };
        std::sort(order.begin(), order.end(), [&](quint64 a, quint64 b) { return dist(a) > dist(b); });
        for (quint64 b : order) {
            if (cachedBytes <= keep * 3 / 4) break;
            cachedBytes -= quint64(blocks.value(b).size());
            blocks.remove(b);
        }
    }
};

class SystemProxies : public QNetworkProxyFactory {
public:
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override
    {
        const QList<QNetworkProxy> found = QNetworkProxyFactory::systemProxyForQuery(query);
        return found.isEmpty() ? QList<QNetworkProxy>{QNetworkProxy::NoProxy} : found;
    }
};

// One thread does the network work of every reader.
class NetThread : public QThread {
public:
    static NetThread* get()
    {
        static NetThread* t = [] {
            auto* th = new NetThread;
            th->setObjectName(QStringLiteral("crt-web-net"));
            th->start();
            th->m_ready.acquire();
            return th;
        }();
        return t;
    }
    QNetworkAccessManager* nam() const { return m_nam; }
    QObject* context() const { return m_context; }

protected:
    void run() override
    {
        m_context = new QObject;
        m_nam = new QNetworkAccessManager;
        m_nam->setProxyFactory(new SystemProxies);
        m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
        m_ready.release();
        exec();
    }

private:
    struct Sem {
        QMutex m; QWaitCondition c; bool v = false;
        void release() { QMutexLocker l(&m); v = true; c.wakeAll(); }
        void acquire() { QMutexLocker l(&m); while (!v) c.wait(&m); }
    } m_ready;
    QNetworkAccessManager* m_nam = nullptr;
    QObject* m_context = nullptr;
};

QString reasonOf(int status)
{
    switch (status) {
    case 400: return QStringLiteral("Bad Request");
    case 401: return QStringLiteral("Unauthorized");
    case 403: return QStringLiteral("Forbidden");
    case 404: return QStringLiteral("Not Found");
    case 410: return QStringLiteral("Gone");
    case 429: return QStringLiteral("Too Many Requests");
    default: return QStringLiteral("HTTP error");
    }
}

// Lives in the network thread.
class Reader : public QObject {
public:
    explicit Reader(std::shared_ptr<Shared> sh) : m_sh(std::move(sh))
    {
        m_retry.setSingleShot(true);
        connect(&m_retry, &QTimer::timeout, this, [this] { kick(); });
    }
    ~Reader() override { drop(); }

    // Decides what to ask the server for: the part the element needs, and then ahead of it.
    void kick()
    {
        if (m_retry.isActive()) return;
        quint64 need = 0, limit = 0;
        bool reading = false;
        {
            QMutexLocker l(&m_sh->lock);
            if (m_sh->stopped || !m_sh->error.isEmpty()) return;
            limit = m_sh->readPos + m_sh->ahead;
            need = m_sh->firstMissing(m_sh->readPos, limit);
            if (m_sh->size >= 0 && need >= quint64(m_sh->size)) return;   // all of the rest is at hand
            reading = m_reply;
        }
        if (reading) {
            // The running request brings it soon: wait for it.
            if (need >= m_next && need <= m_to && need - m_next <= kNear) return;
            if (need >= limit && m_next < limit + m_sh->chunk) return;   // (reading ahead, and not too far)
            drop();
        }
        if (need >= limit) return;   // enough at hand
        ask(need);
    }

private:
    void ask(quint64 from)
    {
        from -= from % kBlock;
        quint64 chunk = std::max<quint64>(kBlock, m_sh->chunk - m_sh->chunk % kBlock);
        QUrl url;
        QList<QPair<QByteArray, QByteArray>> headers;
        {
            QMutexLocker l(&m_sh->lock);
            if (m_sh->size >= 0) chunk = std::min<quint64>(chunk, quint64(m_sh->size) - from);
            m_sh->partial.clear();
            m_sh->partialIndex = from / kBlock;
            ++m_sh->requests;
            url = m_sh->url;
            headers = m_sh->headers;
        }
        m_from = from;
        m_next = from;
        m_to = from + chunk - 1;
        m_skip = 0;
        m_checked = false;
        QNetworkRequest rq(url);
        for (const auto& h : headers) {
            const QByteArray lower = h.first.trimmed().toLower();
            // (the reader's own business: byte ranges, and no compression of what is asked for by range)
            if (lower == "range" || lower == "accept-encoding" || lower == "host" || lower == "connection" || lower == "content-length") continue;
            rq.setRawHeader(h.first.trimmed(), h.second);
        }
        rq.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(m_from) + '-' + QByteArray::number(m_to));
        rq.setRawHeader("Accept-Encoding", "identity");
        rq.setTransferTimeout(15000);
        WEBTRACE(url.fileName() << "ask" << m_from << "-" << m_to);
        m_reply = NetThread::get()->nam()->get(rq);
        connect(m_reply, &QNetworkReply::readyRead, this, [this, r = m_reply] { if (r == m_reply) take(); });
        connect(m_reply, &QNetworkReply::finished, this, [this, r = m_reply] { if (r == m_reply) done(); });
    }

    // Drops the running request (what it brought is kept, but for its last, unfinished block).
    void drop()
    {
        if (!m_reply) return;
        QNetworkReply* r = m_reply;
        m_reply = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
        QMutexLocker l(&m_sh->lock);
        m_sh->partial.clear();
    }

    bool check()
    {
        if (m_checked) return true;
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 0) return false;   // (no answer yet)
        m_checked = true;
        WEBTRACE(m_reply->url().fileName() << "status" << status << m_reply->rawHeader("Content-Range") << "length" << m_reply->rawHeader("Content-Length"));
        QMutexLocker l(&m_sh->lock);
        m_sh->status = status;
        if (status == 206) {
            static const QRegularExpression re(QStringLiteral("bytes\\s+(\\d+)-(\\d+)/(\\d+|\\*)"));
            const auto m = re.match(QString::fromLatin1(m_reply->rawHeader("Content-Range")));
            if (m.hasMatch()) {
                if (m.captured(3) != QLatin1String("*")) m_sh->size = m.captured(3).toLongLong();
                const quint64 start = m.captured(1).toULongLong();
                if (start != m_from) { m_skip = 0; m_next = start; m_from = start; }   // (not where asked: taken as it comes, if on a block's edge)
            }
            m_sh->answered = true;
        } else if (status == 200) {
            // The server sends the whole file, whatever was asked: read on from its start to the part wanted.
            const qint64 len = m_reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
            if (len > 0) m_sh->size = len;
            m_skip = m_from;
            m_next = m_from;
            m_to = m_sh->size > 0 ? quint64(m_sh->size) - 1 : ~0ull;
            m_sh->answered = true;
        }
        m_sh->changed.wakeAll();
        return true;
    }

    void take()
    {
        if (!check()) return;
        const int status = m_sh->status;
        if (status != 200 && status != 206) return;   // (an error: when it has finished)
        QByteArray data = m_reply->readAll();
        if (data.isEmpty()) return;
        QMutexLocker l(&m_sh->lock);
        m_sh->received += quint64(data.size());
        m_sh->lastData = g_get_monotonic_time();
        if (m_skip > 0) {
            const quint64 n = std::min<quint64>(m_skip, quint64(data.size()));
            data.remove(0, qsizetype(n));
            m_skip -= n;
            if (data.isEmpty()) return;
        }
        qsizetype at = 0;
        while (at < data.size()) {
            const quint64 b = m_next / kBlock;
            if (b != m_sh->partialIndex) { m_sh->partialIndex = b; m_sh->partial.clear(); }
            const quint64 want = m_sh->blockLen(b);
            const qsizetype n = qsizetype(std::min<quint64>(want - quint64(m_sh->partial.size()), quint64(data.size() - at)));
            m_sh->partial.append(data.constData() + at, n);
            at += n;
            m_next += quint64(n);
            if (quint64(m_sh->partial.size()) >= want) {
                if (!m_sh->blocks.contains(b)) {
                    m_sh->cachedBytes += quint64(m_sh->partial.size());
                    m_sh->blocks.insert(b, m_sh->partial);
                }
                m_sh->partial.clear();
                m_sh->partialIndex = b + 1;
            }
        }
        m_sh->evict();
        m_sh->changed.wakeAll();
    }

    void done()
    {
        QNetworkReply* r = m_reply;
        take();
        m_reply = nullptr;
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError err = r->error();
        WEBTRACE(r->url().fileName() << "done" << status << "error" << int(err) << r->errorString() << "next" << m_next);
        {
            QMutexLocker l(&m_sh->lock);
            m_sh->partial.clear();
            if (status == 416) {   // asked past the end: the file ends where it was asked
                if (m_sh->size < 0) m_sh->size = qint64(m_from);
                m_sh->answered = true;
                m_sh->changed.wakeAll();
                return;
            }
            if (status >= 400 && status < 500) {   // refused: an address that has run out, or never worked
                m_sh->error = QStringLiteral("%1 (%2)").arg(reasonOf(status)).arg(status);
                m_sh->answered = true;
                m_sh->changed.wakeAll();
                return;
            }
        }
        if (err == QNetworkReply::NoError && (status == 200 || status == 206)) {
            m_failing.invalidate();
            m_failures = 0;
            kick();
            return;
        }
        // The network, or the server, failed: asked again a moment later, for half a minute.
        if (!m_failing.isValid()) m_failing.start();
        if (m_failing.elapsed() > kGiveUpMs) {
            QMutexLocker l(&m_sh->lock);
            m_sh->error = status >= 500 ? QStringLiteral("%1 (%2)").arg(QStringLiteral("Server error"), QString::number(status))
                                        : QStringLiteral("Could not connect: %1").arg(r->errorString());
            m_sh->answered = true;
            m_sh->changed.wakeAll();
            return;
        }
        m_retry.start(std::min(4000, 300 << std::min(m_failures++, 4)));
    }

    std::shared_ptr<Shared> m_sh;
    QNetworkReply* m_reply = nullptr;
    quint64 m_from = 0, m_next = 0, m_to = 0, m_skip = 0;
    bool m_checked = false;
    QTimer m_retry;
    QElapsedTimer m_failing;
    int m_failures = 0;
};

} // namespace

// ---- the element

struct CrtRangeSrc {
    GstBaseSrc parent;
    std::shared_ptr<Shared>* sh;   // (a GObject's memory: the pointer is made and destroyed by hand)
    QPointer<QObject>* reader;
};
struct CrtRangeSrcClass { GstBaseSrcClass parent_class; };

G_DEFINE_TYPE(CrtRangeSrc, crt_range_src, GST_TYPE_BASE_SRC)

static GstStaticPadTemplate crt_range_src_template = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static void crt_range_kick(CrtRangeSrc* self)
{
    QPointer<QObject> r = *self->reader;
    if (!r) return;
    QMetaObject::invokeMethod(r, [r] { if (r) static_cast<Reader*>(r.data())->kick(); }, Qt::QueuedConnection);
}

// Reading starts as soon as the element is ready (the picture's and the sound's at the same time).
static void crt_range_open(CrtRangeSrc* self)
{
    if (*self->reader) return;
    std::shared_ptr<Shared> sh = *self->sh;
    {
        QMutexLocker l(&sh->lock);
        sh->stopped = false;
        sh->lastData = g_get_monotonic_time();
    }
    NetThread* net = NetThread::get();
    QPointer<QObject>* holder = self->reader;
    QMetaObject::invokeMethod(net->context(), [sh, holder] {
        auto* r = new Reader(sh);
        *holder = r;
        r->kick();
    }, Qt::BlockingQueuedConnection);
}

static void crt_range_close(CrtRangeSrc* self)
{
    std::shared_ptr<Shared> sh = *self->sh;
    {
        QMutexLocker l(&sh->lock);
        sh->stopped = true;
        sh->changed.wakeAll();
    }
    QPointer<QObject> r = *self->reader;
    *self->reader = nullptr;
    if (r) QMetaObject::invokeMethod(r, [r] { delete r.data(); }, Qt::BlockingQueuedConnection);
}

static gboolean crt_range_src_start(GstBaseSrc* src)
{
    auto* self = reinterpret_cast<CrtRangeSrc*>(src);
    WEBTRACE("start");
    crt_range_open(self);
    std::shared_ptr<Shared> sh = *self->sh;
    QMutexLocker l(&sh->lock);
    QElapsedTimer t;
    t.start();
    while (!sh->answered && !sh->flushing && t.elapsed() < 45000) sh->changed.wait(&sh->lock, 200);
    if (!sh->error.isEmpty() || !sh->answered) {
        const QByteArray what = (sh->error.isEmpty() ? QStringLiteral("The server did not answer") : sh->error).toUtf8();
        l.unlock();
        GST_ELEMENT_ERROR(src, RESOURCE, OPEN_READ, ("%s", what.constData()), ("%s", self->parent.element.object.name));
        return FALSE;
    }
    return TRUE;
}

static gboolean crt_range_src_stop(GstBaseSrc* src)
{
    crt_range_close(reinterpret_cast<CrtRangeSrc*>(src));
    return TRUE;
}

static gboolean crt_range_src_get_size(GstBaseSrc* src, guint64* size)
{
    auto* self = reinterpret_cast<CrtRangeSrc*>(src);
    QMutexLocker l(&(*self->sh)->lock);
    if ((*self->sh)->size < 0) return FALSE;
    *size = guint64((*self->sh)->size);
    return TRUE;
}

static gboolean crt_range_src_is_seekable(GstBaseSrc* src)
{
    auto* self = reinterpret_cast<CrtRangeSrc*>(src);
    QMutexLocker l(&(*self->sh)->lock);
    return (*self->sh)->size >= 0;
}

static gboolean crt_range_src_unlock(GstBaseSrc* src)
{
    auto* self = reinterpret_cast<CrtRangeSrc*>(src);
    WEBTRACE("unlock (flushing)");
    QMutexLocker l(&(*self->sh)->lock);
    (*self->sh)->flushing = true;
    (*self->sh)->changed.wakeAll();
    return TRUE;
}

static gboolean crt_range_src_unlock_stop(GstBaseSrc* src)
{
    auto* self = reinterpret_cast<CrtRangeSrc*>(src);
    QMutexLocker l(&(*self->sh)->lock);
    (*self->sh)->flushing = false;
    return TRUE;
}

static GstFlowReturn crt_range_src_fill(GstBaseSrc* src, guint64 offset, guint length, GstBuffer* buf)
{
    auto* self = reinterpret_cast<CrtRangeSrc*>(src);
    std::shared_ptr<Shared> sh = *self->sh;
    QMutexLocker l(&sh->lock);
    if (sh->size >= 0 && offset >= quint64(sh->size)) return GST_FLOW_EOS;
    const quint64 end = sh->size >= 0 ? std::min<quint64>(offset + length, quint64(sh->size)) : offset + length;
    const quint64 before = sh->readPos / kBlock;
    sh->readPos = offset;
    bool asked = false;
    while (true) {
        if (sh->flushing) return GST_FLOW_FLUSHING;
        if (!sh->error.isEmpty()) {
            const QByteArray what = sh->error.toUtf8();
            l.unlock();
            GST_ELEMENT_ERROR(src, RESOURCE, READ, ("%s", what.constData()), ("%s", self->parent.element.object.name));
            return GST_FLOW_ERROR;
        }
        if (sh->stopped) return GST_FLOW_FLUSHING;
        GstMapInfo map;
        if (sh->firstMissing(offset, end) >= end) {
            if (!gst_buffer_map(buf, &map, GST_MAP_WRITE)) return GST_FLOW_ERROR;
            sh->copy(offset, end, map.data);
            gst_buffer_unmap(buf, &map);
            gst_buffer_set_size(buf, gssize(end - offset));
            GST_BUFFER_OFFSET(buf) = offset;
            GST_BUFFER_OFFSET_END(buf) = end;
            sh->readPos = end;
            // (on to the next block: time to look at reading ahead)
            if (end / kBlock != before || asked) { l.unlock(); crt_range_kick(self); }
            return GST_FLOW_OK;
        }
        if (!asked) {
            asked = true;
            WEBTRACE(sh->url.fileName() << "fill waits" << offset << "+" << length << "missing at" << sh->firstMissing(offset, end));
            l.unlock();
            crt_range_kick(self);
            l.relock();
            continue;
        }
        sh->changed.wait(&sh->lock, 250);
        if (sh->firstMissing(offset, end) < end && !sh->flushing) { l.unlock(); crt_range_kick(self); l.relock(); }
    }
}

static GstStateChangeReturn crt_range_src_change_state(GstElement* element, GstStateChange transition)
{
    auto* self = reinterpret_cast<CrtRangeSrc*>(element);
    if (transition == GST_STATE_CHANGE_NULL_TO_READY) crt_range_open(self);
    const GstStateChangeReturn ret = GST_ELEMENT_CLASS(crt_range_src_parent_class)->change_state(element, transition);
    if (transition == GST_STATE_CHANGE_READY_TO_NULL) crt_range_close(self);
    return ret;
}

// The demuxer reads where it likes (pull mode) from the start: nothing is pushed into a pad that is not
// connected yet (see WebSource.cpp).
static gboolean crt_range_src_activate(GstPad* pad, GstObject*)
{
    if (gst_pad_activate_mode(pad, GST_PAD_MODE_PULL, TRUE)) { WEBTRACE("activated: pull"); return TRUE; }
    WEBTRACE("activated: push");
    return gst_pad_activate_mode(pad, GST_PAD_MODE_PUSH, TRUE);
}

static void crt_range_src_finalize(GObject* object)
{
    auto* self = reinterpret_cast<CrtRangeSrc*>(object);
    crt_range_close(self);
    delete self->sh;
    delete self->reader;
    G_OBJECT_CLASS(crt_range_src_parent_class)->finalize(object);
}

static void crt_range_src_class_init(CrtRangeSrcClass* klass)
{
    GstElementClass* element = GST_ELEMENT_CLASS(klass);
    GstBaseSrcClass* base = GST_BASE_SRC_CLASS(klass);
    G_OBJECT_CLASS(klass)->finalize = crt_range_src_finalize;
    element->change_state = crt_range_src_change_state;
    base->start = crt_range_src_start;
    base->stop = crt_range_src_stop;
    base->get_size = crt_range_src_get_size;
    base->is_seekable = crt_range_src_is_seekable;
    base->unlock = crt_range_src_unlock;
    base->unlock_stop = crt_range_src_unlock_stop;
    base->fill = crt_range_src_fill;
    gst_element_class_add_static_pad_template(element, &crt_range_src_template);
    gst_element_class_set_static_metadata(element, "CRT Player web stream reader", "Source/Network",
                                          "Reads one stream of a web video by byte ranges, in pieces, keeping what it read", "CRT Player");
}

static void crt_range_src_init(CrtRangeSrc* self)
{
    self->sh = new std::shared_ptr<Shared>(std::make_shared<Shared>());
    self->reader = new QPointer<QObject>();
    gst_base_src_set_format(GST_BASE_SRC(self), GST_FORMAT_BYTES);
    gst_base_src_set_blocksize(GST_BASE_SRC(self), 64 * 1024);
    gst_pad_set_activate_function(GST_BASE_SRC_PAD(self), crt_range_src_activate);
}

GstElement* crt_range_src_new(const WebStream& stream, quint64 aheadBytes, quint64 keepBytes)
{
    static gsize once = 0;
    if (g_once_init_enter(&once)) {
        gst_element_register(nullptr, "crtrangesrc", GST_RANK_NONE, crt_range_src_get_type());
        g_once_init_leave(&once, 1);
    }
    GstElement* e = gst_element_factory_make("crtrangesrc", nullptr);
    if (!e) return nullptr;
    auto* self = reinterpret_cast<CrtRangeSrc*>(e);
    Shared& sh = **self->sh;
    sh.url = QUrl(stream.url);
    sh.headers = stream.headers;
    if (stream.chunkBytes > 0) sh.chunk = std::max<quint64>(kBlock, quint64(stream.chunkBytes));
    sh.ahead = aheadBytes;
    sh.keep = std::max(keepBytes, aheadBytes + 4 * kBlock);
    return e;
}

bool crt_range_src_is(GstElement* element) { return element && G_OBJECT_TYPE(element) == crt_range_src_get_type(); }

RangeStats crt_range_src_stats(GstElement* element)
{
    RangeStats s;
    if (!crt_range_src_is(element)) return s;
    auto* self = reinterpret_cast<CrtRangeSrc*>(element);
    Shared& sh = **self->sh;
    QMutexLocker l(&sh.lock);
    const quint64 limit = sh.readPos + sh.ahead;
    const quint64 missing = sh.firstMissing(sh.readPos, limit);
    s.ahead = missing > sh.readPos ? missing - sh.readPos : 0;
    s.room = sh.ahead;
    s.received = sh.received;
    s.size = sh.size;
    s.ended = sh.size >= 0 && missing >= quint64(sh.size);
    s.quietMs = (g_get_monotonic_time() - sh.lastData) / 1000;
    s.requests = sh.requests;
    return s;
}
