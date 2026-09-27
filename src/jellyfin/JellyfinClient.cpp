#include "JellyfinClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrlQuery>
#include <QUuid>
#include <QVersionNumber>

namespace {
constexpr int kTimeoutMs = 15000;
qint64 nsToTicks(qint64 ns) { return ns / 100; }
} // namespace

QString JfItem::displayName() const
{
    if (type == QLatin1String("Episode") && indexNumber >= 0) {
        const QString se = parentIndexNumber >= 0 ? QString::asprintf("S%02dE%02d", parentIndexNumber, indexNumber)
                                                  : QString::asprintf("E%02d", indexNumber);
        return se + QStringLiteral(" · ") + name;
    }
    if (type == QLatin1String("Movie") && productionYear > 0) return QStringLiteral("%1 (%2)").arg(name).arg(productionYear);
    return name;
}

JfItem JfItem::fromJson(const QJsonObject& o)
{
    JfItem it;
    it.id = o.value("Id").toString();
    it.name = o.value("Name").toString();
    it.type = o.value("Type").toString();
    it.collectionType = o.value("CollectionType").toString();
    it.seriesName = o.value("SeriesName").toString();
    it.overview = o.value("Overview").toString();
    it.mediaType = o.value("MediaType").toString();
    it.isFolder = o.value("IsFolder").toBool();
    it.runTimeTicks = qint64(o.value("RunTimeTicks").toDouble());
    it.indexNumber = o.value("IndexNumber").toInt(-1);
    it.parentIndexNumber = o.value("ParentIndexNumber").toInt(-1);
    it.productionYear = o.value("ProductionYear").toInt(0);
    it.imageTag = o.value("ImageTags").toObject().value("Primary").toString();
    const QJsonObject ud = o.value("UserData").toObject();
    it.positionTicks = qint64(ud.value("PlaybackPositionTicks").toDouble());
    it.played = ud.value("Played").toBool();
    return it;
}

JellyfinClient::JellyfinClient(QObject* parent) : QObject(parent), m_net(new QNetworkAccessManager(this))
{
    m_net->setTransferTimeout(kTimeoutMs);
}

QString JellyfinClient::sessionFilePath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath("jellyfin.json");
}

QString JellyfinClient::normaliseServerUrl(const QString& in)
{
    QString s = in.trimmed();
    if (s.isEmpty()) return {};
    if (!s.contains(QStringLiteral("://"))) s.prepend(QStringLiteral("http://"));
    while (s.endsWith('/')) s.chop(1);
    const QUrl u(s);
    if (!u.isValid() || u.host().isEmpty() || (u.scheme() != "http" && u.scheme() != "https")) return {};
    return s;
}

bool JellyfinClient::usesModernRoutes() const
{
    const QVersionNumber v = QVersionNumber::fromString(m_version);
    return !v.isNull() && v >= QVersionNumber(10, 9);
}

QByteArray JellyfinClient::authorizationHeader() const
{
    QString h = QStringLiteral("MediaBrowser Client=\"CRT Player\", Device=\"%1\", DeviceId=\"%2\", Version=\"%3\"")
                    .arg(QSysInfo::machineHostName().replace('"', ' '), m_deviceId, QCoreApplication::applicationVersion());
    if (!m_token.isEmpty()) h += QStringLiteral(", Token=\"%1\"").arg(m_token);
    return h.toUtf8();
}

QUrl JellyfinClient::url(const QString& path, const QList<QPair<QString, QString>>& query) const
{
    QUrl u(m_server + path);
    if (!query.isEmpty()) {
        QUrlQuery q;
        q.setQueryItems(query);
        u.setQuery(q);
    }
    return u;
}

QNetworkReply* JellyfinClient::request(const QString& method, const QUrl& u, const QByteArray& body)
{
    QNetworkRequest req(u);
    req.setRawHeader("Authorization", authorizationHeader());
    req.setRawHeader("X-Emby-Authorization", authorizationHeader());   // servers before 10.8 read this one
    req.setRawHeader("Accept", "application/json");
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CRT-Player/%1").arg(QCoreApplication::applicationVersion()));
    if (method == QLatin1String("GET")) return m_net->get(req);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    return m_net->post(req, body);
}

QNetworkReply* JellyfinClient::get(const QString& path, const QList<QPair<QString, QString>>& query)
{
    return request(QStringLiteral("GET"), url(path, query), {});
}

QNetworkReply* JellyfinClient::post(const QString& path, const QJsonObject& body)
{
    return request(QStringLiteral("POST"), url(path), QJsonDocument(body).toJson(QJsonDocument::Compact));
}

QString JellyfinClient::describeError(QNetworkReply* r) const
{
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401) return tr("The server rejected the credentials (401).");
    if (status == 403) return tr("Access denied by the server (403).");
    if (status == 404) return tr("The server does not know this address (404). Is it a Jellyfin server?");
    if (r->error() == QNetworkReply::SslHandshakeFailedError)
        return tr("Secure connection failed (%1). Self-signed certificates are not accepted; use a certificate from a "
                  "trusted authority, or plain http:// on your home network.").arg(r->errorString());
    if (r->error() == QNetworkReply::OperationCanceledError || r->error() == QNetworkReply::TimeoutError)
        return tr("The server did not answer in time. Check that it is running and reachable from this computer.");
    if (r->error() == QNetworkReply::ConnectionRefusedError)
        return tr("Nothing is listening at this address (connection refused). Check the port — Jellyfin's "
                  "default is 8096 for http:// and 8920 for https://.");
    if (r->error() == QNetworkReply::HostNotFoundError)
        return tr("The server name could not be found. Check the spelling, or use its IP address.");
    if (r->error() == QNetworkReply::NetworkSessionFailedError || r->error() == QNetworkReply::TemporaryNetworkFailureError)
        return tr("The network is not available.");
    if (status > 0) return tr("Server error %1: %2").arg(status).arg(r->errorString());
    return r->errorString();
}

QVector<JfItem> JellyfinClient::parseItems(const QByteArray& json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    const QJsonArray arr = doc.isObject() ? doc.object().value("Items").toArray() : doc.array();
    QVector<JfItem> out;
    out.reserve(arr.size());
    for (const QJsonValue& v : arr) out.push_back(JfItem::fromJson(v.toObject()));
    remember(out);
    return out;
}

void JellyfinClient::remember(const QVector<JfItem>& items)
{
    for (const JfItem& it : items) m_items.insert(it.id, it);
}

const JfItem* JellyfinClient::cachedItem(const QString& id) const
{
    auto it = m_items.constFind(id);
    return it == m_items.constEnd() ? nullptr : &it.value();
}

// ---- session -----------------------------------------------------------------------

void JellyfinClient::saveSession() const
{
    QDir().mkpath(QFileInfo(sessionFilePath()).absolutePath());
    QJsonObject o{{"server", m_server}, {"serverName", m_serverName}, {"serverId", m_serverId}, {"version", m_version},
                  {"userName", m_userName}, {"userId", m_userId}, {"token", m_token}, {"deviceId", m_deviceId}};
    QSaveFile f(sessionFilePath());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);   // the token is a credential
    f.write(QJsonDocument(o).toJson());
    f.commit();
    QFile::setPermissions(sessionFilePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

void JellyfinClient::clearSession(bool keepServer)
{
    m_token.clear();
    m_userId.clear();
    if (!keepServer) { m_server.clear(); m_serverName.clear(); m_version.clear(); }
    m_items.clear();
    saveSession();
    emit sessionChanged();
}

void JellyfinClient::restoreSession()
{
    QFile f(sessionFilePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        m_server = o.value("server").toString();
        m_serverName = o.value("serverName").toString();
        m_serverId = o.value("serverId").toString();
        m_version = o.value("version").toString();
        m_userName = o.value("userName").toString();
        m_userId = o.value("userId").toString();
        m_token = o.value("token").toString();
        m_deviceId = o.value("deviceId").toString();
    }
    if (m_deviceId.isEmpty()) {
        m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        saveSession();
    }
    if (m_token.isEmpty() || m_server.isEmpty()) { emit sessionChanged(); return; }
    // Verify the stored token is still valid (it is revoked when signing out elsewhere).
    QNetworkReply* r = get(QStringLiteral("/Users/%1").arg(m_userId));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401 || status == 403) {
            clearSession(true);
            emit requestFailed(tr("Your Jellyfin sign-in has expired. Please sign in again."));
            return;
        }
        emit sessionChanged();   // offline or fine: keep the session, errors surface on use
    });
    emit sessionChanged();
}

void JellyfinClient::probeServer(const QString& input)
{
    const QString server = normaliseServerUrl(input);
    if (server.isEmpty()) { emit serverProbed(false, {}, {}, tr("Enter an address such as http://192.168.1.20:8096")); return; }
    QNetworkReply* r = request(QStringLiteral("GET"), QUrl(server + "/System/Info/Public"), {});
    connect(r, &QNetworkReply::finished, this, [this, r, server] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) { emit serverProbed(false, {}, {}, describeError(r)); return; }
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        if (!o.contains("Version") || !o.contains("Id")) {
            emit serverProbed(false, {}, {}, tr("This address answered, but it is not a Jellyfin server."));
            return;
        }
        m_server = server;
        m_serverName = o.value("ServerName").toString();
        m_serverId = o.value("Id").toString();
        m_version = o.value("Version").toString();
        emit serverProbed(true, m_serverName, m_version, {});
    });
}

void JellyfinClient::signIn(const QString& input, const QString& user, const QString& password)
{
    const QString server = normaliseServerUrl(input);
    if (server.isEmpty()) { emit signInFailed(tr("Enter the server address, e.g. http://192.168.1.20:8096")); return; }
    // Probe first (version decides the routes), then authenticate.
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(this, &JellyfinClient::serverProbed, this, [this, conn, user, password](bool ok, const QString&, const QString&, const QString& err) {
        disconnect(*conn);
        if (!ok) { emit signInFailed(err); return; }
        m_token.clear();
        QNetworkReply* r = post(QStringLiteral("/Users/AuthenticateByName"), QJsonObject{{"Username", user}, {"Pw", password}});
        connect(r, &QNetworkReply::finished, this, [this, r] {
            r->deleteLater();
            if (r->error() != QNetworkReply::NoError) {
                const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                emit signInFailed(status == 401 ? tr("Wrong user name or password.") : describeError(r));
                return;
            }
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            m_token = o.value("AccessToken").toString();
            const QJsonObject u = o.value("User").toObject();
            m_userId = u.value("Id").toString();
            m_userName = u.value("Name").toString();
            if (m_token.isEmpty() || m_userId.isEmpty()) { m_token.clear(); emit signInFailed(tr("The server sent an unexpected answer.")); return; }
            saveSession();
            emit signedIn();
            emit sessionChanged();
        });
    });
    probeServer(server);
}

void JellyfinClient::signOut()
{
    if (!m_token.isEmpty()) {
        QNetworkReply* r = post(QStringLiteral("/Sessions/Logout"), {});   // revokes the token on the server
        connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
    }
    clearSession(true);
}

// ---- browsing ------------------------------------------------------------------------

QString JellyfinClient::itemsPath() const
{
    return usesModernRoutes() ? QStringLiteral("/Items") : QStringLiteral("/Users/%1/Items").arg(m_userId);
}

QList<QPair<QString, QString>> JellyfinClient::userQuery() const
{
    if (usesModernRoutes()) return {{"userId", m_userId}};
    return {};
}

void JellyfinClient::loadHome()
{
    auto resumeQ = userQuery();
    resumeQ << qMakePair(QStringLiteral("MediaTypes"), QStringLiteral("Video"))
            << qMakePair(QStringLiteral("Limit"), QStringLiteral("12"))
            << qMakePair(QStringLiteral("Fields"), QStringLiteral("Overview"));
    const QString resumePath = usesModernRoutes() ? QStringLiteral("/UserItems/Resume") : QStringLiteral("/Users/%1/Items/Resume").arg(m_userId);
    const QString viewsPath = usesModernRoutes() ? QStringLiteral("/UserViews") : QStringLiteral("/Users/%1/Views").arg(m_userId);
    QNetworkReply* r1 = get(resumePath, resumeQ);
    connect(r1, &QNetworkReply::finished, this, [this, r1, viewsPath] {
        r1->deleteLater();
        const QVector<JfItem> resume = r1->error() == QNetworkReply::NoError ? parseItems(r1->readAll()) : QVector<JfItem>();
        QNetworkReply* r2 = get(viewsPath, userQuery());
        connect(r2, &QNetworkReply::finished, this, [this, r2, resume] {
            r2->deleteLater();
            if (r2->error() != QNetworkReply::NoError) { emit requestFailed(describeError(r2)); return; }
            emit homeLoaded(resume, parseItems(r2->readAll()));
        });
    });
}

void JellyfinClient::loadChildren(const QString& parentId)
{
    auto q = userQuery();
    q << qMakePair(QStringLiteral("ParentId"), parentId)
      << qMakePair(QStringLiteral("SortBy"), QStringLiteral("ParentIndexNumber,IndexNumber,SortName"))
      << qMakePair(QStringLiteral("SortOrder"), QStringLiteral("Ascending"))
      << qMakePair(QStringLiteral("Fields"), QStringLiteral("Overview"))
      << qMakePair(QStringLiteral("Limit"), QStringLiteral("500"));
    QNetworkReply* r = get(itemsPath(), q);
    connect(r, &QNetworkReply::finished, this, [this, r, parentId] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) { emit requestFailed(describeError(r)); return; }
        emit itemsLoaded(parentId, parseItems(r->readAll()));
    });
}

void JellyfinClient::search(const QString& term)
{
    auto q = userQuery();
    q << qMakePair(QStringLiteral("SearchTerm"), term) << qMakePair(QStringLiteral("Recursive"), QStringLiteral("true"))
      << qMakePair(QStringLiteral("IncludeItemTypes"), QStringLiteral("Movie,Episode,Series,Video,MusicVideo"))
      << qMakePair(QStringLiteral("Fields"), QStringLiteral("Overview")) << qMakePair(QStringLiteral("Limit"), QStringLiteral("100"));
    QNetworkReply* r = get(itemsPath(), q);
    connect(r, &QNetworkReply::finished, this, [this, r, term] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) { emit requestFailed(describeError(r)); return; }
        emit searchResults(term, parseItems(r->readAll()));
    });
}

void JellyfinClient::fetchItem(const QString& itemId)
{
    const QString path = usesModernRoutes() ? QStringLiteral("/Items/%1").arg(itemId)
                                            : QStringLiteral("/Users/%1/Items/%2").arg(m_userId, itemId);
    QNetworkReply* r = get(path, userQuery());
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) return;
        const JfItem it = JfItem::fromJson(QJsonDocument::fromJson(r->readAll()).object());
        remember({it});
        emit itemLoaded(it);
    });
}

void JellyfinClient::fetchSubtitles(const QString& itemId)
{
    const QString path = usesModernRoutes() ? QStringLiteral("/Items/%1").arg(itemId)
                                            : QStringLiteral("/Users/%1/Items/%2").arg(m_userId, itemId);
    auto q = userQuery();
    q << qMakePair(QStringLiteral("Fields"), QStringLiteral("MediaSources"));
    QNetworkReply* r = get(path, q);
    connect(r, &QNetworkReply::finished, this, [this, r, itemId] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) return;
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        const QJsonArray sources = o.value("MediaSources").toArray();
        QList<QPair<QString, QUrl>> subs;
        if (!sources.isEmpty()) {
            const QJsonObject src = sources.first().toObject();
            const QString sourceId = src.value("Id").toString(itemId);
            for (const QJsonValue& v : src.value("MediaStreams").toArray()) {
                const QJsonObject st = v.toObject();
                if (st.value("Type").toString() != QLatin1String("Subtitle") || !st.value("IsExternal").toBool()) continue;
                const QString codec = st.value("Codec").toString().toLower();
                const QString fmt = (codec == "ass" || codec == "ssa") ? "ass" : (codec == "webvtt" || codec == "vtt") ? "vtt" : "srt";
                QString label = st.value("DisplayTitle").toString();
                if (label.isEmpty()) label = st.value("Language").toString(tr("External subtitle"));
                subs.append({label, url(QStringLiteral("/Videos/%1/%2/Subtitles/%3/0/Stream.%4")
                                            .arg(itemId, sourceId).arg(st.value("Index").toInt()).arg(fmt))});
            }
        }
        emit subtitlesLoaded(itemId, subs);
    });
}

void JellyfinClient::fetchBytes(const QUrl& u, std::function<void(const QByteArray&, const QString&)> done)
{
    QNetworkReply* r = request(QStringLiteral("GET"), u, {});
    connect(r, &QNetworkReply::finished, this, [this, r, done] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) done({}, describeError(r));
        else done(r->readAll(), {});
    });
}

void JellyfinClient::fetchImage(const JfItem& item, int maxHeight)
{
    if (item.imageTag.isEmpty()) return;
    const QString key = item.id + ':' + QString::number(maxHeight);
    if (m_images.contains(key)) { emit imageReady(item.id, m_images.value(key)); return; }
    if (m_pendingImages.contains(key)) return;
    m_pendingImages.insert(key);
    QNetworkReply* r = get(QStringLiteral("/Items/%1/Images/Primary").arg(item.id),
                           {{"maxHeight", QString::number(maxHeight)}, {"tag", item.imageTag}, {"quality", "90"}});
    const QString id = item.id;
    connect(r, &QNetworkReply::finished, this, [this, r, key, id] {
        r->deleteLater();
        m_pendingImages.remove(key);
        QPixmap pm;
        if (r->error() == QNetworkReply::NoError && pm.loadFromData(r->readAll())) {
            m_images.insert(key, pm);
            emit imageReady(id, pm);
        }
    });
}

QUrl JellyfinClient::streamUrl(const QString& itemId) const
{
    // The original file, untouched ("static"): decoded locally by the host's GStreamer.
    // Authentication travels in the Authorization header (see Player::setHttpHeaders).
    return url(QStringLiteral("/Videos/%1/stream").arg(itemId),
               {{"static", "true"}, {"mediaSourceId", itemId}, {"deviceId", m_deviceId}});
}

// ---- playback reporting --------------------------------------------------------------

void JellyfinClient::reportStart(const QString& itemId, qint64 positionNs, bool paused)
{
    if (!isSignedIn()) return;
    m_playSession = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QNetworkReply* r = post(QStringLiteral("/Sessions/Playing"),
                            {{"ItemId", itemId}, {"MediaSourceId", itemId}, {"PositionTicks", double(nsToTicks(positionNs))},
                             {"IsPaused", paused}, {"CanSeek", true}, {"PlayMethod", "DirectPlay"}, {"PlaySessionId", m_playSession}});
    connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
}

void JellyfinClient::reportProgress(const QString& itemId, qint64 positionNs, bool paused, const QString& event)
{
    if (!isSignedIn()) return;
    QJsonObject o{{"ItemId", itemId}, {"MediaSourceId", itemId}, {"PositionTicks", double(nsToTicks(positionNs))},
                  {"IsPaused", paused}, {"CanSeek", true}, {"PlayMethod", "DirectPlay"}, {"PlaySessionId", m_playSession}};
    if (!event.isEmpty()) o.insert("EventName", event);
    QNetworkReply* r = post(QStringLiteral("/Sessions/Playing/Progress"), o);
    connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
    if (m_items.contains(itemId)) m_items[itemId].positionTicks = nsToTicks(positionNs);
}

QNetworkReply* JellyfinClient::reportStopped(const QString& itemId, qint64 positionNs)
{
    if (!isSignedIn()) return nullptr;
    QNetworkReply* r = post(QStringLiteral("/Sessions/Playing/Stopped"),
                            {{"ItemId", itemId}, {"MediaSourceId", itemId}, {"PositionTicks", double(nsToTicks(positionNs))},
                             {"PlaySessionId", m_playSession}});
    connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
    if (m_items.contains(itemId)) m_items[itemId].positionTicks = nsToTicks(positionNs);
    return r;
}
