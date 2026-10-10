#pragma once
// Browsing YouTube without signing in (2.18): search, a channel's videos, the new videos of the channels
// the viewer follows, "watch later" and history.
//
// Everything that reads YouTube's own pages goes through yt-dlp (search, channel pages), which its project
// keeps up with YouTube's changes; the player only reads yt-dlp's JSON. The new videos of the channels
// followed come from each channel's feed (an Atom file YouTube has published for many years, read
// directly: fast, and nothing to sign in to); a channel whose feed does not answer is asked of yt-dlp
// instead. What the viewer follows, keeps and has watched stays on this computer (one JSON file).
// The last lists fetched are kept too: when YouTube does not answer, they are shown, with what went wrong.
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class OnlineResolver;
class QNetworkAccessManager;

struct WebItem {                 // a video in a list
    QString id, url, title, channel, channelId, thumb, description;
    double seconds = 0;          // 0: not known
    qint64 published = 0;        // seconds since 1970 (0: not known; from a search, YouTube's "3 days ago")
    qint64 views = -1;
    bool live = false, shorts = false;
    qint64 addedAt = 0;          // when it was put on "watch later", or watched (history)

    bool isValid() const { return !url.isEmpty(); }
    QJsonObject toJson() const;
    static WebItem fromJson(const QJsonObject& o);
};

struct WebChannel {
    QString id, title, avatar;
    qint64 addedAt = 0;
    qint64 openedAt = 0;         // when its videos were last looked at (what is newer counts as new)

    QJsonObject toJson() const;
    static WebChannel fromJson(const QJsonObject& o);
};

struct WebList {                 // what a search, a channel page or the feed brought
    QList<WebItem> items;
    QString error;               // one line for the viewer (empty: fine)
    QString detail;              // what yt-dlp said
    bool cached = false;         // the list kept from last time, shown because fetching failed
    QDateTime fetchedAt;
    WebChannel channel;          // (a channel page: the channel's own name and picture)
    int failedChannels = 0;      // (the feed: channels that could not be read)
};

class WebBrowse : public QObject {
    Q_OBJECT
public:
    explicit WebBrowse(OnlineResolver* resolver, QObject* parent = nullptr);

    // ---- what the viewer keeps (saved at once)
    QList<WebChannel> channels() const { return m_channels; }
    bool follows(const QString& channelId) const;
    void follow(const WebChannel& c);
    void unfollow(const QString& channelId);
    void markOpened(const QString& channelId);
    void updateChannel(const WebChannel& c);   // (a better name or picture, once its page was read)
    QList<WebItem> watchLater() const { return m_later; }
    bool isWatchLater(const QString& url) const;
    void setWatchLater(const WebItem& item, bool on);
    QList<WebItem> history() const { return m_history; }
    void addHistory(const WebItem& item);
    void clearHistory();
    // Google Takeout's subscriptions.csv: the channels in it are followed. Returns how many were new.
    int importTakeout(const QString& path, QString* error);

    // ---- fetching (each answer comes once; a newer question of the same kind does not cancel an older one)
    using ListDone = std::function<void(const WebList&)>;
    void search(const QString& query, ListDone done);
    void channelVideos(const QString& channelId, ListDone done);
    void feed(bool refresh, ListDone done);    // the followed channels' new videos, newest first
    QString lastSearch() const { return m_lastQuery; }
    WebList cachedSearch() const;
    WebList cachedFeed() const;
    void thumbnail(const QString& url, std::function<void(const QImage&)> done);

    // Is there a newer yt-dlp than the one in use? Asked at most once a day. done(installed, newest): newest is
    // empty when not known.
    void checkYtDlp(bool force, std::function<void(const QString& installed, const QString& newest)> done);

    QString site() const { return m_site; }    // https://www.youtube.com (tests: their own site)
    QString channelPage(const QString& channelId) const;
    QJsonObject report() const;

    // ---- pure parts (unit tests)
    static WebList parseListing(const QByteArray& json, const QByteArray& errors, int exitCode);
    static QList<WebItem> parseFeed(const QByteArray& xml, const QString& channelId);
    static QList<WebChannel> parseTakeout(const QByteArray& csv);
    static QString bestThumbnail(const class QJsonArray& thumbs, const QString& id, const QString& url);
    static QString errorLine(const QByteArray& errors, int exitCode);

private:
    void load();
    void save();
    void writeCache(const QString& name, const WebList& list);
    WebList readCache(const QString& name) const;
    void feedStep();

    OnlineResolver* m_resolver;
    QNetworkAccessManager* m_net;
    QString m_site;
    QList<WebChannel> m_channels;
    QList<WebItem> m_later, m_history;
    QString m_lastQuery;
    // the feed being gathered
    struct FeedRun {
        QList<ListDone> waiting;
        QStringList todo;
        int running = 0;
        QHash<QString, QList<WebItem>> got;
        int failed = 0;
        QStringList errors;
    };
    FeedRun* m_feed = nullptr;
    QHash<QString, QList<std::function<void(const QImage&)>>> m_thumbWaiting;
    int m_searches = 0, m_channelReads = 0, m_feedReads = 0, m_feedFallbacks = 0, m_thumbReads = 0;
};
