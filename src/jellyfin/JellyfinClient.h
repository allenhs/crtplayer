#pragma once
#include <QHash>
#include <functional>
#include <QJsonObject>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

// One entry from a Jellyfin listing (library, folder, series, season, movie, episode...).
struct JfItem {
    QString id, name, type, collectionType, seriesName, overview, imageTag, mediaType;
    bool isFolder = false;
    bool played = false;
    qint64 runTimeTicks = 0;        // 1 tick = 100 ns
    qint64 positionTicks = 0;       // resume position
    int indexNumber = -1, parentIndexNumber = -1, productionYear = 0;
    bool isVideo() const { return !isFolder && (mediaType == QLatin1String("Video") || type == QLatin1String("Movie") ||
                                                 type == QLatin1String("Episode") || type == QLatin1String("Video") ||
                                                 type == QLatin1String("MusicVideo")); }
    QString displayName() const;    // "S01E02 · Title" for episodes
    static JfItem fromJson(const QJsonObject& o);
};

// What the server decided for one playback (POST /Items/{id}/PlaybackInfo).
struct JfPlayback {
    QString itemId;
    QUrl url;                       // what to open
    bool transcode = false;         // the server converts the video (HLS); otherwise the original file
    bool tokenInUrl = false;        // HLS segments are fetched separately, so the token travels in the address
    QString playSessionId, mediaSourceId;
    QStringList transcodeReasons;   // e.g. VideoCodecNotSupported, ContainerBitrateExceedsLimit
    QList<QPair<QString, QUrl>> subtitles;   // subtitle files the server offers for this playback
    QString error;                  // PlaybackInfo failed (the caller falls back to the original file)
};

// Minimal Jellyfin API client: sign-in, browsing, image loading, stream URLs and
// playback reporting. Talks JSON over HTTP(S) with QNetworkAccessManager.
//
// Credentials: only the access token is stored (never the password), in
// ~/.config/CRTPlayer/CRTPlayer/jellyfin.json with owner-only permissions (0600). Streams are
// authenticated with a request header. The exception is a video the server converts: that
// arrives as HLS, whose segments are fetched separately, so the token goes in its address
// (as Jellyfin's own clients do). Such addresses are never logged or shown.
//
// Jellyfin 10.9 moved several user endpoints; the client picks the route set from the
// server version reported by /System/Info/Public.
class JellyfinClient : public QObject {
    Q_OBJECT
public:
    explicit JellyfinClient(QObject* parent = nullptr);

    static QString normaliseServerUrl(const QString& input);
    static QString sessionFilePath();

    bool isSignedIn() const { return !m_token.isEmpty(); }
    QString serverUrl() const { return m_server; }
    QString serverName() const { return m_serverName; }
    QString serverVersion() const { return m_version; }
    QString userName() const { return m_userName; }
    QString userId() const { return m_userId; }
    bool usesModernRoutes() const;   // Jellyfin >= 10.9

    void restoreSession();                         // from the session file; verifies the token
    void probeServer(const QString& url);          // -> serverProbed
    void signIn(const QString& url, const QString& user, const QString& password);
    void signOut();

    void loadHome();                               // -> homeLoaded
    // A folder's contents arrive a page at a time: startIndex 0 first, then more as the
    // list is scrolled. -> itemsLoaded(parentId, items, startIndex, total)
    void loadChildren(const QString& parentId, int startIndex = 0);
    static constexpr int kPageSize = 100;
    void search(const QString& term);              // -> searchResults
    void fetchItem(const QString& itemId);         // -> itemLoaded
    void fetchSubtitles(const QString& itemId);    // -> subtitlesLoaded (external subtitle files)
    // Authenticated download (e.g. a subtitle file); the callback runs on the GUI thread.
    void fetchBytes(const QUrl& url, std::function<void(const QByteArray& data, const QString& error)> done);
    void fetchImage(const JfItem& item, int maxHeight);   // -> imageReady (cached)

    QUrl streamUrl(const QString& itemId) const;   // original file, decoded locally

    // Asks the server how to play an item. The device profile lists what this computer can
    // decode; anything else, or anything over maxBitrate (bits/s, 0 = no limit), is converted
    // by the server. forceTranscode skips the original file (used after it failed to play).
    // -> the callback, on the GUI thread
    struct Capabilities { QStringList containers, videoCodecs, audioCodecs; bool h264 = true; };
    void requestPlayback(const QString& itemId, qint64 maxBitrate, bool forceTranscode, const Capabilities& caps,
                         std::function<void(const JfPlayback&)> done);
    static QJsonObject deviceProfile(const Capabilities& caps, qint64 maxBitrate);
    void stopTranscode(const QString& playSessionId);   // ends the server's conversion
    QByteArray authorizationHeader() const;        // "MediaBrowser Client=..., Token=..."
    const JfItem* cachedItem(const QString& id) const;

    // Playback reporting keeps resume points and "played" state in sync on the server.
    // playMethod: "DirectPlay" or "Transcode"; playSessionId: the server's (from PlaybackInfo),
    // or empty for a new one of our own.
    void setPlayMethod(const QString& playMethod, const QString& playSessionId, const QString& mediaSourceId);
    void reportStart(const QString& itemId, qint64 positionNs, bool paused);
    void reportProgress(const QString& itemId, qint64 positionNs, bool paused, const QString& event = QString());
    QNetworkReply* reportStopped(const QString& itemId, qint64 positionNs);

signals:
    void serverProbed(bool ok, const QString& name, const QString& version, const QString& error);
    void signedIn();
    void signInFailed(const QString& message);
    void sessionChanged();
    void homeLoaded(const QVector<JfItem>& resume, const QVector<JfItem>& libraries);
    void itemsLoaded(const QString& parentId, const QVector<JfItem>& items, int startIndex, int total);
    void searchResults(const QString& term, const QVector<JfItem>& items);
    void itemLoaded(const JfItem& item);
    void subtitlesLoaded(const QString& itemId, const QList<QPair<QString, QUrl>>& subtitles);
    void imageReady(const QString& itemId, const QPixmap& pixmap);
    void requestFailed(const QString& message);

private:
    QNetworkReply* get(const QString& path, const QList<QPair<QString, QString>>& query = {});
    QNetworkReply* post(const QString& path, const QJsonObject& body);
    QNetworkReply* request(const QString& method, const QUrl& url, const QByteArray& body);
    QUrl url(const QString& path, const QList<QPair<QString, QString>>& query = {}) const;
    QString itemsPath() const;       // version-dependent routes
    QList<QPair<QString, QString>> userQuery() const;
    QVector<JfItem> parseItems(const QByteArray& json, int* total = nullptr);
    void remember(const QVector<JfItem>& items);
    void saveSession() const;
    void clearSession(bool keepServer);
    QString describeError(QNetworkReply* r) const;

    QNetworkAccessManager* m_net;
    QString m_server, m_serverName, m_serverId, m_version;
    QString m_userName, m_userId, m_token;
    QString m_deviceId;
    QString m_playSession, m_playMethod = QStringLiteral("DirectPlay"), m_serverPlaySession, m_mediaSourceId;
    QHash<QString, JfItem> m_items;          // everything seen, by id (titles, resume points)
    QHash<QString, QPixmap> m_images;        // "id:height" -> pixmap
    QSet<QString> m_pendingImages;
};
