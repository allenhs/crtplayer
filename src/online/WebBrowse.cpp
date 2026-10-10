#include "WebBrowse.h"
#include "OnlineVideo.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
const int kSearchCount = 36;      // three pages of twelve
const int kChannelCount = 48;
const int kFeedKeep = 120;
const int kHistoryKeep = 300;
const int kFeedParallel = 4;

QString dataDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("web"));
}
QString cacheDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("web"));
}
QString safeName(const QString& s)
{
    QString out;
    for (QChar c : s) out += (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')) ? c : QLatin1Char('_');
    return out.left(80);
}
bool isYouTube(const QString& url) { return url.contains(QLatin1String("youtube.com")) || url.contains(QLatin1String("youtu.be")); }
} // namespace

// ---- items

QJsonObject WebItem::toJson() const
{
    QJsonObject o{{"id", id}, {"url", url}, {"title", title}, {"channel", channel}, {"channelId", channelId}, {"thumb", thumb}};
    if (!description.isEmpty()) o["description"] = description.left(600);
    if (seconds > 0) o["seconds"] = seconds;
    if (published > 0) o["published"] = double(published);
    if (views >= 0) o["views"] = double(views);
    if (live) o["live"] = true;
    if (shorts) o["shorts"] = true;
    if (addedAt > 0) o["addedAt"] = double(addedAt);
    return o;
}

WebItem WebItem::fromJson(const QJsonObject& o)
{
    WebItem i;
    i.id = o.value("id").toString();
    i.url = o.value("url").toString();
    i.title = o.value("title").toString();
    i.channel = o.value("channel").toString();
    i.channelId = o.value("channelId").toString();
    i.thumb = o.value("thumb").toString();
    i.description = o.value("description").toString();
    i.seconds = o.value("seconds").toDouble();
    i.published = qint64(o.value("published").toDouble());
    i.views = o.contains("views") ? qint64(o.value("views").toDouble()) : -1;
    i.live = o.value("live").toBool();
    i.shorts = o.value("shorts").toBool();
    i.addedAt = qint64(o.value("addedAt").toDouble());
    return i;
}

QJsonObject WebChannel::toJson() const
{
    QJsonObject o{{"id", id}, {"title", title}, {"avatar", avatar}};
    if (addedAt > 0) o["addedAt"] = double(addedAt);
    if (openedAt > 0) o["openedAt"] = double(openedAt);
    return o;
}

WebChannel WebChannel::fromJson(const QJsonObject& o)
{
    WebChannel c;
    c.id = o.value("id").toString();
    c.title = o.value("title").toString();
    c.avatar = o.value("avatar").toString();
    c.addedAt = qint64(o.value("addedAt").toDouble());
    c.openedAt = qint64(o.value("openedAt").toDouble());
    return c;
}

// ---- the store

WebBrowse::WebBrowse(OnlineResolver* resolver, QObject* parent) : QObject(parent), m_resolver(resolver)
{
    m_net = OnlineResolver::makeNetwork(this);
    auto* disk = new QNetworkDiskCache(this);
    disk->setCacheDirectory(QDir(cacheDir()).filePath(QStringLiteral("thumbs")));
    disk->setMaximumCacheSize(200ll * 1024 * 1024);
    m_net->setCache(disk);
    m_site = qEnvironmentVariable("CRTPLAYER_YT_SITE", QStringLiteral("https://www.youtube.com"));
    while (m_site.endsWith(QLatin1Char('/'))) m_site.chop(1);
    load();
}

void WebBrowse::load()
{
    QFile f(QDir(dataDir()).filePath(QStringLiteral("library.json")));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue& v : o.value("channels").toArray()) m_channels.append(WebChannel::fromJson(v.toObject()));
    for (const QJsonValue& v : o.value("watchLater").toArray()) m_later.append(WebItem::fromJson(v.toObject()));
    for (const QJsonValue& v : o.value("history").toArray()) m_history.append(WebItem::fromJson(v.toObject()));
    m_lastQuery = o.value("lastSearch").toString();
}

void WebBrowse::save()
{
    QDir().mkpath(dataDir());
    QJsonArray ch, later, hist;
    for (const WebChannel& c : m_channels) ch.append(c.toJson());
    for (const WebItem& i : m_later) later.append(i.toJson());
    for (const WebItem& i : m_history) hist.append(i.toJson());
    QSaveFile f(QDir(dataDir()).filePath(QStringLiteral("library.json")));
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(QJsonObject{{"version", 1}, {"channels", ch}, {"watchLater", later}, {"history", hist}, {"lastSearch", m_lastQuery}})
                .toJson(QJsonDocument::Indented));
    f.commit();
}

bool WebBrowse::follows(const QString& channelId) const
{
    return std::any_of(m_channels.begin(), m_channels.end(), [&](const WebChannel& c) { return c.id == channelId; });
}

void WebBrowse::follow(const WebChannel& c)
{
    if (c.id.isEmpty() || follows(c.id)) return;
    WebChannel n = c;
    n.addedAt = QDateTime::currentSecsSinceEpoch();
    if (n.openedAt == 0) n.openedAt = n.addedAt;
    m_channels.append(n);
    std::sort(m_channels.begin(), m_channels.end(), [](const WebChannel& a, const WebChannel& b) { return a.title.compare(b.title, Qt::CaseInsensitive) < 0; });
    save();
}

void WebBrowse::unfollow(const QString& channelId)
{
    m_channels.erase(std::remove_if(m_channels.begin(), m_channels.end(), [&](const WebChannel& c) { return c.id == channelId; }), m_channels.end());
    save();
}

void WebBrowse::markOpened(const QString& channelId)
{
    for (WebChannel& c : m_channels)
        if (c.id == channelId) { c.openedAt = QDateTime::currentSecsSinceEpoch(); save(); return; }
}

void WebBrowse::updateChannel(const WebChannel& c)
{
    for (WebChannel& o : m_channels)
        if (o.id == c.id) {
            bool changed = false;
            if (!c.title.isEmpty() && c.title != o.title) { o.title = c.title; changed = true; }
            if (!c.avatar.isEmpty() && c.avatar != o.avatar) { o.avatar = c.avatar; changed = true; }
            if (changed) save();
            return;
        }
}

bool WebBrowse::isWatchLater(const QString& url) const
{
    return std::any_of(m_later.begin(), m_later.end(), [&](const WebItem& i) { return i.url == url; });
}

void WebBrowse::setWatchLater(const WebItem& item, bool on)
{
    m_later.erase(std::remove_if(m_later.begin(), m_later.end(), [&](const WebItem& i) { return i.url == item.url; }), m_later.end());
    if (on) {
        WebItem n = item;
        n.addedAt = QDateTime::currentSecsSinceEpoch();
        m_later.prepend(n);
    }
    save();
}

void WebBrowse::addHistory(const WebItem& item)
{
    if (!item.isValid()) return;
    WebItem n = item;
    // (what is known already is kept: a video opened from a link knows less than one from a list)
    for (const WebItem& o : m_history)
        if (o.url == item.url) {
            if (n.thumb.isEmpty()) n.thumb = o.thumb;
            if (n.channelId.isEmpty()) n.channelId = o.channelId;
            if (n.channel.isEmpty()) n.channel = o.channel;
            if (n.seconds <= 0) n.seconds = o.seconds;
            if (n.published <= 0) n.published = o.published;
            break;
        }
    m_history.erase(std::remove_if(m_history.begin(), m_history.end(), [&](const WebItem& i) { return i.url == item.url; }), m_history.end());
    n.addedAt = QDateTime::currentSecsSinceEpoch();
    m_history.prepend(n);
    while (m_history.size() > kHistoryKeep) m_history.removeLast();
    save();
}

void WebBrowse::clearHistory()
{
    m_history.clear();
    save();
}

QList<WebChannel> WebBrowse::parseTakeout(const QByteArray& csv)
{
    // "Channel Id,Channel Url,Channel Title" (the names are in the account's language: the columns are found
    // by what is in them)
    QList<WebChannel> out;
    const QList<QByteArray> lines = csv.split('\n');
    for (const QByteArray& raw : lines) {
        QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(QChar(0xfeff))) line.remove(0, 1);
        if (line.isEmpty()) continue;
        QStringList cells;
        QString cur;
        bool quoted = false;
        for (int i = 0; i < line.size(); ++i) {
            const QChar c = line[i];
            if (quoted) {
                if (c == QLatin1Char('"') && i + 1 < line.size() && line[i + 1] == QLatin1Char('"')) { cur += c; ++i; }
                else if (c == QLatin1Char('"')) quoted = false;
                else cur += c;
            } else if (c == QLatin1Char('"')) quoted = true;
            else if (c == QLatin1Char(',')) { cells << cur; cur.clear(); }
            else cur += c;
        }
        cells << cur;
        WebChannel ch;
        for (const QString& cell : cells) {
            const QString t = cell.trimmed();
            if (ch.id.isEmpty() && t.size() == 24 && t.startsWith(QLatin1String("UC"))) ch.id = t;
        }
        if (ch.id.isEmpty()) continue;
        for (const QString& cell : cells) {
            const QString t = cell.trimmed();
            if (t != ch.id && !t.startsWith(QLatin1String("http")) && !t.isEmpty()) { ch.title = t; break; }
        }
        if (ch.title.isEmpty()) ch.title = ch.id;
        out.append(ch);
    }
    return out;
}

int WebBrowse::importTakeout(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = tr("The file could not be read: %1").arg(f.errorString());
        return -1;
    }
    const QList<WebChannel> found = parseTakeout(f.read(4 * 1024 * 1024));
    if (found.isEmpty()) {
        if (error) *error = tr("No channels were found in this file. It is \"subscriptions.csv\" from Google Takeout (YouTube and YouTube Music → subscriptions).");
        return -1;
    }
    int added = 0;
    for (const WebChannel& c : found)
        if (!follows(c.id)) {
            WebChannel n = c;
            n.addedAt = n.openedAt = QDateTime::currentSecsSinceEpoch();
            m_channels.append(n);
            ++added;
        }
    std::sort(m_channels.begin(), m_channels.end(), [](const WebChannel& a, const WebChannel& b) { return a.title.compare(b.title, Qt::CaseInsensitive) < 0; });
    save();
    return added;
}

// ---- reading what yt-dlp says

QString WebBrowse::bestThumbnail(const QJsonArray& thumbs, const QString& id, const QString& url)
{
    // A 16:9 picture of at least 300 pixels across, the smallest such (the tiles are about that size);
    // YouTube's own medium picture when none is named.
    QString best;
    double bestScore = 1e18;
    for (const QJsonValue& v : thumbs) {
        const QJsonObject t = v.toObject();
        const QString u = t.value("url").toString();
        if (u.isEmpty() || u.contains(QLatin1String(".webp"))) continue;
        const double w = t.value("width").toDouble(), h = t.value("height").toDouble();
        double score = 5000;
        if (w > 0 && h > 0) {
            const double aspect = w / h;
            score = (std::abs(aspect - 16.0 / 9.0) > 0.1 ? 10000 : 0) + (w < 300 ? 20000 - w : w);
        }
        if (score < bestScore) { bestScore = score; best = u; }
    }
    if (best.isEmpty() && !id.isEmpty() && isYouTube(url)) best = QStringLiteral("https://i.ytimg.com/vi/%1/mqdefault.jpg").arg(id);
    return best;
}

QString WebBrowse::errorLine(const QByteArray& errors, int exitCode)
{
    if (errors == "no-program") return tr("yt-dlp is not on this computer");
    QString last;
    for (const QByteArray& line : errors.split('\n')) {
        const QString l = QString::fromUtf8(line).trimmed();
        if (l.startsWith(QLatin1String("ERROR:"))) last = l.mid(6).trimmed();
    }
    if (!last.isEmpty()) return last;
    if (exitCode == -1) return tr("yt-dlp did not answer in time (YouTube may be slow, or blocked)");
    return tr("yt-dlp could not read this from YouTube");
}

WebList WebBrowse::parseListing(const QByteArray& json, const QByteArray& errors, int exitCode)
{
    WebList l;
    l.fetchedAt = QDateTime::currentDateTime();
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        l.error = errorLine(errors, exitCode);
        l.detail = QString::fromUtf8(errors).trimmed().right(2000);
        return l;
    }
    const QJsonObject o = doc.object();
    // A channel's page: its own name and picture.
    l.channel.id = o.value("channel_id").toString();
    l.channel.title = o.value("channel").toString();
    if (l.channel.title.isEmpty()) l.channel.title = o.value("uploader").toString();
    for (const QJsonValue& v : o.value("thumbnails").toArray()) {
        const QJsonObject t = v.toObject();
        const QString tid = t.value("id").toString();
        if (tid == QLatin1String("avatar_uncropped")) { l.channel.avatar = t.value("url").toString(); break; }
        if (l.channel.avatar.isEmpty() && t.value("width").toDouble() > 0 && t.value("width").toDouble() == t.value("height").toDouble())
            l.channel.avatar = t.value("url").toString();
    }
    const QJsonArray entries = o.value("entries").toArray();
    for (const QJsonValue& v : entries) {
        const QJsonObject e = v.toObject();
        // (a channel's page can hold its playlists, or other channels, too: only videos here)
        const QString url = e.value("url").toString(e.value("webpage_url").toString());
        if (url.isEmpty() || url.contains(QLatin1String("/playlist?")) || url.contains(QLatin1String("/channel/")) || url.contains(QLatin1String("/@"))) continue;
        WebItem i;
        i.id = e.value("id").toString();
        i.url = url;
        i.title = e.value("title").toString();
        i.channel = e.value("channel").toString(e.value("uploader").toString());
        if (i.channel.isEmpty()) i.channel = l.channel.title;
        i.channelId = e.value("channel_id").toString();
        if (i.channelId.isEmpty()) i.channelId = l.channel.id;
        i.description = e.value("description").toString();
        i.seconds = e.value("duration").toDouble();
        i.published = qint64(e.value("timestamp").toDouble());
        if (i.published == 0 && e.value("release_timestamp").toDouble() > 0) i.published = qint64(e.value("release_timestamp").toDouble());
        i.views = e.contains("view_count") && !e.value("view_count").isNull() ? qint64(e.value("view_count").toDouble()) : -1;
        const QString ls = e.value("live_status").toString();
        i.live = ls == QLatin1String("is_live") || ls == QLatin1String("is_upcoming");
        i.shorts = url.contains(QLatin1String("/shorts/"));
        i.thumb = bestThumbnail(e.value("thumbnails").toArray(), i.id, url);
        if (i.title.isEmpty()) i.title = i.id;
        l.items.append(i);
    }
    if (l.items.isEmpty() && exitCode != 0) {
        l.error = errorLine(errors, exitCode);
        l.detail = QString::fromUtf8(errors).trimmed().right(2000);
    }
    return l;
}

QList<WebItem> WebBrowse::parseFeed(const QByteArray& xml, const QString& channelId)
{
    QList<WebItem> out;
    QXmlStreamReader r(xml);
    WebItem cur;
    bool inEntry = false;
    QString feedAuthor;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const QStringView n = r.name();
            if (n == QLatin1String("entry")) { inEntry = true; cur = WebItem(); cur.channelId = channelId; }
            else if (!inEntry && n == QLatin1String("name")) feedAuthor = r.readElementText();
            else if (inEntry && n == QLatin1String("videoId")) cur.id = r.readElementText();
            else if (inEntry && n == QLatin1String("channelId")) cur.channelId = r.readElementText();
            else if (inEntry && n == QLatin1String("title") && r.namespaceUri() != QLatin1String("http://search.yahoo.com/mrss/")) cur.title = r.readElementText();
            else if (inEntry && n == QLatin1String("title") && cur.title.isEmpty()) cur.title = r.readElementText();
            else if (inEntry && n == QLatin1String("link") && r.attributes().value(QLatin1String("rel")) == QLatin1String("alternate")) cur.url = r.attributes().value(QLatin1String("href")).toString();
            else if (inEntry && n == QLatin1String("name")) cur.channel = r.readElementText();
            else if (inEntry && n == QLatin1String("published")) cur.published = QDateTime::fromString(r.readElementText(), Qt::ISODate).toSecsSinceEpoch();
            else if (inEntry && n == QLatin1String("thumbnail")) {
                const QString u = r.attributes().value(QLatin1String("url")).toString();
                // (YouTube names its 480×360 picture, black above and below: its 16:9 one is beside it)
                cur.thumb = u.endsWith(QLatin1String("/hqdefault.jpg")) ? u.left(u.size() - 14) + QStringLiteral("/mqdefault.jpg") : u;
            } else if (inEntry && n == QLatin1String("description")) cur.description = r.readElementText();
            else if (inEntry && n == QLatin1String("statistics")) cur.views = r.attributes().value(QLatin1String("views")).toLongLong();
        } else if (r.isEndElement() && r.name() == QLatin1String("entry")) {
            inEntry = false;
            if (cur.url.isEmpty() && !cur.id.isEmpty()) cur.url = QStringLiteral("https://www.youtube.com/watch?v=%1").arg(cur.id);
            cur.shorts = cur.url.contains(QLatin1String("/shorts/"));
            if (cur.channel.isEmpty()) cur.channel = feedAuthor;
            if (!cur.url.isEmpty()) out.append(cur);
        }
    }
    return out;
}

// ---- caches of the last lists

void WebBrowse::writeCache(const QString& name, const WebList& list)
{
    QDir().mkpath(cacheDir());
    QJsonArray items;
    for (const WebItem& i : list.items) items.append(i.toJson());
    QSaveFile f(QDir(cacheDir()).filePath(name + QStringLiteral(".json")));
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(QJsonObject{{"items", items}, {"fetchedAt", list.fetchedAt.toString(Qt::ISODate)}, {"channel", list.channel.toJson()},
                                      {"query", name == QLatin1String("search") ? m_lastQuery : QString()}})
                .toJson(QJsonDocument::Compact));
    f.commit();
}

WebList WebBrowse::readCache(const QString& name) const
{
    WebList l;
    QFile f(QDir(cacheDir()).filePath(name + QStringLiteral(".json")));
    if (!f.open(QIODevice::ReadOnly)) return l;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue& v : o.value("items").toArray()) l.items.append(WebItem::fromJson(v.toObject()));
    l.fetchedAt = QDateTime::fromString(o.value("fetchedAt").toString(), Qt::ISODate);
    l.channel = WebChannel::fromJson(o.value("channel").toObject());
    l.cached = true;
    return l;
}

WebList WebBrowse::cachedSearch() const { return readCache(QStringLiteral("search")); }
WebList WebBrowse::cachedFeed() const { return readCache(QStringLiteral("feed")); }

// ---- fetching

void WebBrowse::search(const QString& query, ListDone done)
{
    m_lastQuery = query.trimmed();
    save();
    ++m_searches;
    const QStringList args = {QStringLiteral("--dump-single-json"), QStringLiteral("--flat-playlist"), QStringLiteral("--no-progress"),
                              QStringLiteral("--extractor-args"), QStringLiteral("youtubetab:approximate_date")};
    const QString q = m_lastQuery;
    m_resolver->ask(args, QStringLiteral("ytsearch%1:%2").arg(kSearchCount).arg(q), 60000, [this, q, done](const QByteArray& out, const QByteArray& err, int code) {
        WebList l = parseListing(out, err, code);
        if (l.error.isEmpty()) writeCache(QStringLiteral("search"), l);
        done(l);
    });
}

QString WebBrowse::channelPage(const QString& channelId) const
{
    return m_site + QStringLiteral("/channel/") + channelId + QStringLiteral("/videos");
}

void WebBrowse::channelVideos(const QString& channelId, ListDone done)
{
    ++m_channelReads;
    // A channel's link ("youtube.com/@name", ".../channel/UC…", "@name"): its videos tab.
    QString target = channelPage(channelId);
    const QString t = channelId.trimmed();
    if (t.startsWith(QLatin1Char('@'))) target = m_site + QLatin1Char('/') + t + QStringLiteral("/videos");
    else if (t.contains(QLatin1Char('/')) || t.contains(QLatin1Char('.'))) {
        QString link = t.contains(QStringLiteral("://")) ? t : QStringLiteral("https://") + t;
        while (link.endsWith(QLatin1Char('/'))) link.chop(1);
        static const QRegularExpression tab(QStringLiteral("/(videos|streams|shorts|featured|playlists|community|about)$"));
        link.remove(tab);
        target = link + QStringLiteral("/videos");
    }
    const QStringList args = {QStringLiteral("--dump-single-json"), QStringLiteral("--flat-playlist"), QStringLiteral("--no-progress"),
                              QStringLiteral("--playlist-end"), QString::number(kChannelCount),
                              QStringLiteral("--extractor-args"), QStringLiteral("youtubetab:approximate_date")};
    const QString name = QStringLiteral("channel-") + safeName(channelId);
    m_resolver->ask(args, target, 60000, [this, name, channelId, done](const QByteArray& out, const QByteArray& err, int code) {
        WebList l = parseListing(out, err, code);
        if (l.channel.id.isEmpty()) l.channel.id = channelId;
        if (l.error.isEmpty()) {
            writeCache(name, l);
            updateChannel(l.channel);
        } else {
            WebList kept = readCache(name);
            if (!kept.items.isEmpty()) { kept.error = l.error; kept.detail = l.detail; l = kept; }
        }
        done(l);
    });
}

void WebBrowse::feed(bool refresh, ListDone done)
{
    if (!refresh) {
        WebList kept = cachedFeed();
        if (kept.fetchedAt.isValid() && kept.fetchedAt.secsTo(QDateTime::currentDateTime()) < 15 * 60) {
            kept.cached = false;
            QTimer::singleShot(0, this, [done, kept] { done(kept); });
            return;
        }
    }
    if (m_feed) { m_feed->waiting.append(done); return; }   // (already being gathered)
    if (m_channels.isEmpty()) {
        WebList l;
        l.fetchedAt = QDateTime::currentDateTime();
        QTimer::singleShot(0, this, [done, l] { done(l); });
        return;
    }
    m_feed = new FeedRun;
    m_feed->waiting.append(done);
    for (const WebChannel& c : m_channels) m_feed->todo << c.id;
    for (int i = 0; i < kFeedParallel; ++i) feedStep();
}

void WebBrowse::feedStep()
{
    if (!m_feed) return;
    if (m_feed->todo.isEmpty()) {
        if (m_feed->running > 0) return;
        // All read: newest first.
        WebList l;
        l.fetchedAt = QDateTime::currentDateTime();
        QHash<QString, bool> seen;
        for (const auto& items : std::as_const(m_feed->got))
            for (const WebItem& i : items)
                if (!seen.contains(i.url)) { seen.insert(i.url, true); l.items.append(i); }
        std::sort(l.items.begin(), l.items.end(), [](const WebItem& a, const WebItem& b) { return a.published > b.published; });
        while (l.items.size() > kFeedKeep) l.items.removeLast();
        l.failedChannels = m_feed->failed;
        if (m_feed->failed > 0 && m_feed->failed == int(m_channels.size())) {
            l.error = tr("None of the channels could be read");
            l.detail = m_feed->errors.join(QLatin1Char('\n'));
            WebList kept = cachedFeed();
            if (!kept.items.isEmpty()) { kept.error = l.error; kept.detail = l.detail; kept.failedChannels = l.failedChannels; l = kept; }
        } else {
            writeCache(QStringLiteral("feed"), l);
        }
        const QList<ListDone> waiting = m_feed->waiting;
        delete m_feed;
        m_feed = nullptr;
        for (const ListDone& d : waiting) d(l);
        return;
    }
    const QString id = m_feed->todo.takeFirst();
    ++m_feed->running;
    ++m_feedReads;
    QNetworkRequest rq(QUrl(m_site + QStringLiteral("/feeds/videos.xml?channel_id=") + id));
    rq.setTransferTimeout(20000);
    rq.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    QNetworkReply* reply = m_net->get(rq);
    connect(reply, &QNetworkReply::finished, this, [this, reply, id] {
        reply->deleteLater();
        if (!m_feed) return;
        const QList<WebItem> items = reply->error() == QNetworkReply::NoError ? parseFeed(reply->readAll(), id) : QList<WebItem>();
        if (reply->error() == QNetworkReply::NoError) {
            m_feed->got.insert(id, items);
            --m_feed->running;
            feedStep();
            return;
        }
        // The channel's feed did not answer: its page, through yt-dlp.
        ++m_feedFallbacks;
        const QStringList args = {QStringLiteral("--dump-single-json"), QStringLiteral("--flat-playlist"), QStringLiteral("--no-progress"),
                                  QStringLiteral("--playlist-end"), QStringLiteral("15"),
                                  QStringLiteral("--extractor-args"), QStringLiteral("youtubetab:approximate_date")};
        const QString why = reply->errorString();
        m_resolver->ask(args, channelPage(id), 60000, [this, id, why](const QByteArray& out, const QByteArray& err, int code) {
            if (!m_feed) return;
            const WebList l = parseListing(out, err, code);
            if (l.error.isEmpty()) {
                m_feed->got.insert(id, l.items);
                updateChannel(l.channel);
            } else {
                ++m_feed->failed;
                m_feed->errors << QStringLiteral("%1: %2; %3").arg(id, why, l.error);
            }
            --m_feed->running;
            feedStep();
        });
    });
}

void WebBrowse::thumbnail(const QString& url, std::function<void(const QImage&)> done)
{
    if (url.isEmpty()) { QTimer::singleShot(0, this, [done] { done(QImage()); }); return; }
    auto it = m_thumbWaiting.find(url);
    if (it != m_thumbWaiting.end()) { it->append(done); return; }
    m_thumbWaiting.insert(url, {done});
    ++m_thumbReads;
    QNetworkRequest rq{QUrl(url)};
    rq.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    rq.setTransferTimeout(20000);
    QNetworkReply* reply = m_net->get(rq);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        reply->deleteLater();
        QImage img;
        if (reply->error() == QNetworkReply::NoError) img = QImage::fromData(reply->readAll());
        const auto waiting = m_thumbWaiting.take(url);
        for (const auto& d : waiting) d(img);
    });
}

void WebBrowse::checkYtDlp(bool force, std::function<void(const QString&, const QString&)> done)
{
    QSettings s;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 asked = s.value(QStringLiteral("online/ytDlpCheckedAt")).toLongLong();
    const QString known = s.value(QStringLiteral("online/ytDlpNewest")).toString();
    auto finish = [this, done](const QString& newest) {
        m_resolver->version([done, newest](const QString& installed) { done(installed, newest); });
    };
    if (!force && now - asked < 24 * 3600) { finish(known); return; }
    const QString url = qEnvironmentVariable("CRTPLAYER_YTDLP_LATEST", QStringLiteral("https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest"));
    QNetworkRequest rq{QUrl(url)};
    rq.setRawHeader("Accept", "application/vnd.github+json");
    rq.setTransferTimeout(15000);
    QNetworkReply* reply = m_net->get(rq);
    connect(reply, &QNetworkReply::finished, this, [reply, finish, now, known] {
        reply->deleteLater();
        QString newest = known;
        if (reply->error() == QNetworkReply::NoError) {
            const QString tag = QJsonDocument::fromJson(reply->readAll()).object().value("tag_name").toString();
            if (!tag.isEmpty()) {
                newest = tag;
                QSettings s;
                s.setValue(QStringLiteral("online/ytDlpCheckedAt"), now);
                s.setValue(QStringLiteral("online/ytDlpNewest"), tag);
            }
        }
        finish(newest);
    });
}

QJsonObject WebBrowse::report() const
{
    QJsonArray ch;
    for (const WebChannel& c : m_channels) ch.append(c.toJson());
    return QJsonObject{{"site", m_site}, {"channels", ch}, {"watchLater", int(m_later.size())}, {"history", int(m_history.size())},
                       {"lastSearch", m_lastQuery}, {"searches", m_searches}, {"channelReads", m_channelReads}, {"feedReads", m_feedReads},
                       {"feedFallbacks", m_feedFallbacks}, {"thumbReads", m_thumbReads}, {"feedRunning", m_feed != nullptr}};
}
