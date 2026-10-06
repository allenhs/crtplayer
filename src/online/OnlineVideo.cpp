#include "OnlineVideo.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

#include <zlib.h>

namespace {

#ifdef Q_OS_WIN
const char kProgram[] = "yt-dlp.exe";
#else
const char kProgram[] = "yt-dlp";
#endif

// The environment for a program of the system's own. Inside an AppImage the player's environment points
// at the libraries packed with it; a system program (yt-dlp, and the Python or JavaScript it runs) must
// not pick those up.
QProcessEnvironment systemEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString appDir = env.value(QStringLiteral("APPDIR"));
    if (!appDir.isEmpty()) {
        for (const char* name : {"LD_LIBRARY_PATH", "PYTHONPATH", "GI_TYPELIB_PATH", "PATH"}) {
            const QString key = QString::fromLatin1(name);
            if (!env.contains(key)) continue;
            QStringList keep;
            for (const QString& part : env.value(key).split(QLatin1Char(':'), Qt::SkipEmptyParts))
                if (!part.startsWith(appDir)) keep << part;
            if (keep.isEmpty()) env.remove(key); else env.insert(key, keep.join(QLatin1Char(':')));
        }
        env.remove(QStringLiteral("PYTHONHOME"));
        env.remove(QStringLiteral("LD_PRELOAD"));
    }
    return env;
}

QString findInSystem(const QString& name)
{
    // (for the tests: these folders and no others, so that a run does not depend on what the machine has)
    if (qEnvironmentVariableIsSet("CRTPLAYER_TOOLS_PATH"))
        return QStandardPaths::findExecutable(name, qEnvironmentVariable("CRTPLAYER_TOOLS_PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts));
    QString found = QStandardPaths::findExecutable(name);
    if (!found.isEmpty()) return found;
    // Places that are often not on the path a desktop launcher hands a program.
    const QStringList more = {QDir::homePath() + QStringLiteral("/.local/bin"), QStringLiteral("/home/linuxbrew/.linuxbrew/bin"),
                              QStringLiteral("/usr/local/bin"), QDir::homePath() + QStringLiteral("/.deno/bin"),
                              QDir::homePath() + QStringLiteral("/bin")};
    return QStandardPaths::findExecutable(name, more);
}

QString codecName(const QString& c)
{
    const QString l = c.toLower();
    if (l.isEmpty() || l == QLatin1String("none")) return {};
    if (l.startsWith(QLatin1String("avc")) || l.startsWith(QLatin1String("h264"))) return QStringLiteral("H.264");
    if (l.startsWith(QLatin1String("vp09")) || l.startsWith(QLatin1String("vp9"))) return QStringLiteral("VP9");
    if (l.startsWith(QLatin1String("av01")) || l == QLatin1String("av1")) return QStringLiteral("AV1");
    if (l.startsWith(QLatin1String("hev")) || l.startsWith(QLatin1String("hvc")) || l.startsWith(QLatin1String("h265"))) return QStringLiteral("HEVC");
    if (l.startsWith(QLatin1String("vp8"))) return QStringLiteral("VP8");
    if (l.startsWith(QLatin1String("mp4a"))) return QStringLiteral("AAC");
    if (l == QLatin1String("opus")) return QStringLiteral("Opus");
    if (l == QLatin1String("vorbis")) return QStringLiteral("Vorbis");
    if (l.startsWith(QLatin1String("mp3"))) return QStringLiteral("MP3");
    if (l.startsWith(QLatin1String("ac-3")) || l == QLatin1String("ac3")) return QStringLiteral("AC-3");
    if (l.startsWith(QLatin1String("ec-3")) || l == QLatin1String("eac3")) return QStringLiteral("E-AC-3");
    return c.section(QLatin1Char('.'), 0, 0);
}

QList<QPair<QByteArray, QByteArray>> headersOf(const QJsonObject& f)
{
    QList<QPair<QByteArray, QByteArray>> out;
    const QJsonObject h = f.value(QStringLiteral("http_headers")).toObject();
    for (auto it = h.begin(); it != h.end(); ++it) out.append({it.key().toUtf8(), it.value().toString().toUtf8()});
    // Cookies as yt-dlp writes them: "name=value; Domain=...; Path=/; ..." one after the other.
    static const QStringList attributes = {QStringLiteral("domain"), QStringLiteral("path"), QStringLiteral("expires"), QStringLiteral("max-age"),
                                           QStringLiteral("secure"), QStringLiteral("httponly"), QStringLiteral("samesite"), QStringLiteral("comment"),
                                           QStringLiteral("version")};
    QStringList cookies;
    for (const QString& part : f.value(QStringLiteral("cookies")).toString().split(QStringLiteral("; "), Qt::SkipEmptyParts)) {
        const QString name = part.section(QLatin1Char('='), 0, 0).trimmed().toLower();
        if (!attributes.contains(name) && part.contains(QLatin1Char('='))) cookies << part.trimmed();
    }
    if (!cookies.isEmpty()) out.append({"Cookie", cookies.join(QStringLiteral("; ")).toUtf8()});
    return out;
}

QString lastErrorLine(const QByteArray& errors)
{
    QString last;
    for (const QByteArray& line : errors.split('\n')) {
        const QString l = QString::fromUtf8(line).trimmed();
        if (l.startsWith(QLatin1String("ERROR:"))) last = l.mid(6).trimmed();
    }
    return last;
}

qint64 cueTime(const QString& t)   // "00:01:02.345" or "01:02.345", in ms
{
    const QStringList p = t.split(QLatin1Char(':'));
    if (p.size() < 2) return -1;
    double s = p.last().toDouble();
    s += p[p.size() - 2].toInt() * 60.0;
    if (p.size() > 2) s += p[p.size() - 3].toInt() * 3600.0;
    return qint64(s * 1000.0 + 0.5);
}

QString cueStamp(qint64 ms)
{
    return QStringLiteral("%1:%2:%3.%4").arg(ms / 3600000, 2, 10, QLatin1Char('0')).arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0')).arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

} // namespace

OnlineResolver::OnlineResolver(QObject* parent) : QObject(parent) {}

namespace {
// The system's proxy for each request, where it has one (a company's network; yt-dlp and GStreamer read
// the same settings by themselves).
class SystemProxies : public QNetworkProxyFactory {
public:
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override
    {
        const QList<QNetworkProxy> found = QNetworkProxyFactory::systemProxyForQuery(query);
        return found.isEmpty() ? QList<QNetworkProxy>{QNetworkProxy::NoProxy} : found;
    }
};
} // namespace

QNetworkAccessManager* OnlineResolver::network()
{
    if (!m_net) {
        m_net = new QNetworkAccessManager(this);
        m_net->setProxyFactory(new SystemProxies);
    }
    return m_net;
}

OnlineResolver::~OnlineResolver() { cancel(); }

QString OnlineResolver::ownCopyPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("tools/") + QString::fromLatin1(kProgram));
}

QString OnlineResolver::program() const
{
    if (!m_configured.isEmpty() && QFileInfo(m_configured).isExecutable()) return m_configured;
    // The copy the player fetched itself comes first: it is the one its "Update" button keeps current.
    const QString own = ownCopyPath();
    if (QFileInfo(own).isExecutable()) return own;
#ifdef Q_OS_WIN
    const QString beside = QDir(QCoreApplication::applicationDirPath()).filePath(QString::fromLatin1(kProgram));
    if (QFileInfo::exists(beside)) return beside;
#endif
    return findInSystem(QStringLiteral("yt-dlp"));
}

bool OnlineResolver::programIsOwnCopy() const { return program() == ownCopyPath(); }

QString OnlineResolver::ownRuntimePath()
{
#ifdef Q_OS_WIN
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("tools/deno.exe"));
#else
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("tools/deno"));
#endif
}

QString OnlineResolver::jsRuntime()
{
    QString path = findInSystem(QStringLiteral("deno"));
    if (path.isEmpty() && QFileInfo(ownRuntimePath()).isExecutable()) path = ownRuntimePath();
    if (!path.isEmpty()) return QStringLiteral("deno:") + path;
    for (const auto& rt : {std::pair<const char*, const char*>{"node", "node"}, {"bun", "bun"}, {"qjs", "quickjs"}}) {
        path = findInSystem(QString::fromLatin1(rt.first));
        if (!path.isEmpty()) return QString::fromLatin1(rt.second) + QLatin1Char(':') + path;
    }
    return {};
}

void OnlineResolver::cancel()
{
    ++m_serial;
    if (m_process) {
        QProcess* p = m_process;
        m_process = nullptr;
        p->disconnect(this);
        p->kill();
        p->deleteLater();
    }
}

QStringList OnlineResolver::arguments(const QString& url, const OnlineOptions& o, const QStringList& jsRuntimes)
{
    // Not what this computer cannot decode.
    QString vf, af;
    if (!o.videoCodecs.isEmpty()) {
        if (!o.videoCodecs.contains(QStringLiteral("av1"))) vf += QStringLiteral("[vcodec!^=av01]");
        if (!o.videoCodecs.contains(QStringLiteral("vp9"))) vf += QStringLiteral("[vcodec!^=vp09][vcodec!^=vp9]");
        if (!o.videoCodecs.contains(QStringLiteral("hevc"))) vf += QStringLiteral("[vcodec!^=hev1][vcodec!^=hvc1]");
    }
    if (!o.audioCodecs.isEmpty()) {
        if (!o.audioCodecs.contains(QStringLiteral("opus"))) af += QStringLiteral("[acodec!=opus]");
        if (!o.audioCodecs.contains(QStringLiteral("aac"))) af += QStringLiteral("[acodec!^=mp4a]");
    }
    const QString h = QStringLiteral("[height<=%1]").arg(o.maxHeight > 0 ? o.maxHeight : 1080);
    const QString http = QStringLiteral("[protocol~='^https?$']");   // (not "http_dash_segments": a file read whole, by byte ranges)
    // In this order: picture and sound as two files (how the large sites serve anything above 360p; no HDR,
    // which the player does not map to an ordinary screen), one file with both, a stream GStreamer plays by
    // itself (HLS), and then whatever there is.
    const QString pick = QStringLiteral("bv") + http + vf + QStringLiteral("[dynamic_range=SDR]") + h + QStringLiteral("+ba") + http + af
                       + QStringLiteral("/bv") + http + vf + h + QStringLiteral("+ba") + http + af
                       + QStringLiteral("/b") + http + vf + h
                       + QStringLiteral("/bv") + vf + h + QStringLiteral("+ba") + af
                       + QStringLiteral("/b") + vf + h
                       + QStringLiteral("/bv+ba/b");
    QStringList a = {QStringLiteral("--dump-single-json"), QStringLiteral("--flat-playlist"), QStringLiteral("--no-playlist"),
                     QStringLiteral("--no-progress"), QStringLiteral("--socket-timeout"), QStringLiteral("15"),
                     // (what it says is read as UTF-8; on Windows it would otherwise write in the system's code page)
                     QStringLiteral("--encoding"), QStringLiteral("utf-8"),
                     QStringLiteral("-f"), pick};
    // The largest picture within the limit; among those of one size, the codec this computer handles best
    // (given as the best codec to consider: yt-dlp ranks anything "better" than it last).
    if (o.preferH264) a << QStringLiteral("-S") << QStringLiteral("res,vcodec:h264");
    else if (!o.videoCodecs.isEmpty() && !o.videoCodecs.contains(QStringLiteral("av1"))) a << QStringLiteral("-S") << QStringLiteral("res,fps,vcodec:vp9");
    for (const QString& r : jsRuntimes) a << QStringLiteral("--js-runtimes") << r;
    a << QStringLiteral("--") << url;
    return a;
}

OnlineResult OnlineResolver::parse(const QByteArray& json, const QByteArray& errors, int exitCode, const OnlineOptions&)
{
    OnlineResult r;
    const QString said = QString::fromUtf8(errors).trimmed();
    r.detail = said;
    if (said.contains(QLatin1String("JavaScript runtime"), Qt::CaseInsensitive))
        r.notes << QObject::tr("yt-dlp found no JavaScript runtime. For YouTube it needs one (Deno is the one it uses by default): without it, fewer "
                               "versions of a video are offered, or none.");
    QJsonParseError pe{};
    const QJsonObject root = QJsonDocument::fromJson(json, &pe).object();
    if (root.isEmpty()) {
        const QString line = lastErrorLine(errors);
        r.error = !line.isEmpty() ? line : exitCode != 0 ? QObject::tr("yt-dlp stopped with an error (code %1)").arg(exitCode)
                                                          : QObject::tr("yt-dlp gave no answer for this address");
        return r;
    }
    if (root.value(QStringLiteral("_type")).toString() == QLatin1String("playlist")) {
        r.playlist = true;
        r.playlistTitle = root.value(QStringLiteral("title")).toString();
        for (const QJsonValue& v : root.value(QStringLiteral("entries")).toArray()) {
            const QJsonObject e = v.toObject();
            OnlineEntry entry;
            entry.url = e.value(QStringLiteral("url")).toString();
            if (entry.url.isEmpty()) entry.url = e.value(QStringLiteral("webpage_url")).toString();
            if (!entry.url.startsWith(QLatin1String("http"))) continue;
            entry.title = e.value(QStringLiteral("title")).toString();
            entry.seconds = e.value(QStringLiteral("duration")).toDouble();
            r.entries.append(entry);
        }
        r.ok = !r.entries.isEmpty();
        if (!r.ok) r.error = QObject::tr("This playlist has no videos that can be played");
        return r;
    }

    OnlineVideo& v = r.video;
    v.page = root.value(QStringLiteral("webpage_url")).toString();
    if (v.page.isEmpty()) v.page = root.value(QStringLiteral("original_url")).toString();
    v.id = root.value(QStringLiteral("id")).toString();
    v.title = root.value(QStringLiteral("title")).toString();
    v.uploader = root.value(QStringLiteral("uploader")).toString();
    if (v.uploader.isEmpty()) v.uploader = root.value(QStringLiteral("channel")).toString();
    v.site = root.value(QStringLiteral("extractor_key")).toString();
    v.seconds = root.value(QStringLiteral("duration")).toDouble();
    v.live = root.value(QStringLiteral("is_live")).toBool();

    QList<QJsonObject> chosen;
    for (const QJsonValue& f : root.value(QStringLiteral("requested_formats")).toArray()) chosen.append(f.toObject());
    if (chosen.isEmpty() && !root.value(QStringLiteral("url")).toString().isEmpty()) chosen.append(root);
    if (chosen.isEmpty()) {
        const QString line = lastErrorLine(errors);
        r.error = line.isEmpty() ? QObject::tr("yt-dlp found no stream to play on this page") : line;
        return r;
    }
    // The picture first.
    std::stable_sort(chosen.begin(), chosen.end(), [](const QJsonObject& a, const QJsonObject& b) {
        const bool av = a.value(QStringLiteral("vcodec")).toString() != QLatin1String("none"), bv = b.value(QStringLiteral("vcodec")).toString() != QLatin1String("none");
        return av && !bv;
    });
    bool direct = chosen.size() <= 2;
    for (const QJsonObject& f : chosen) {
        const QString proto = f.value(QStringLiteral("protocol")).toString();
        if (proto != QLatin1String("http") && proto != QLatin1String("https")) direct = false;
    }
    v.headers = headersOf(chosen.first());
    QString vcodec, acodec;
    double fps = 0;
    for (const QJsonObject& f : chosen) {
        const QString vc = f.value(QStringLiteral("vcodec")).toString(), ac = f.value(QStringLiteral("acodec")).toString();
        if (vc != QLatin1String("none") && !vc.isEmpty() && vcodec.isEmpty()) {
            vcodec = vc;
            v.width = f.value(QStringLiteral("width")).toInt();
            v.height = f.value(QStringLiteral("height")).toInt();
            fps = f.value(QStringLiteral("fps")).toDouble();
        }
        if (ac != QLatin1String("none") && !ac.isEmpty() && acodec.isEmpty()) acodec = ac;
    }
    if (direct) {
        for (const QJsonObject& f : chosen) {
            WebStream s;
            s.url = f.value(QStringLiteral("url")).toString();
            s.headers = headersOf(f);
            s.audioOnly = f.value(QStringLiteral("vcodec")).toString() == QLatin1String("none");
            s.kbps = qRound(f.value(QStringLiteral("tbr")).toDouble());
            if (!s.url.isEmpty()) v.streams.append(s);
        }
    } else {
        for (const QJsonObject& f : chosen)
            if (v.manifest.isEmpty()) v.manifest = f.value(QStringLiteral("manifest_url")).toString();
        const QString proto = chosen.first().value(QStringLiteral("protocol")).toString();
        if (v.manifest.isEmpty() && chosen.size() == 1 && proto.startsWith(QLatin1String("m3u8"))) v.manifest = chosen.first().value(QStringLiteral("url")).toString();
    }
    if (v.streams.isEmpty() && v.manifest.isEmpty()) {
        r.error = QObject::tr("This site sends the video in a way the player cannot play yet (%1)").arg(chosen.first().value(QStringLiteral("protocol")).toString());
        return r;
    }
    QStringList what;
    if (v.height > 0) what << QStringLiteral("%1p%2").arg(v.height).arg(fps > 31 ? QString::number(qRound(fps)) : QString());
    QStringList codecs;
    if (!codecName(vcodec).isEmpty()) codecs << codecName(vcodec);
    if (!codecName(acodec).isEmpty()) codecs << codecName(acodec);
    if (!codecs.isEmpty()) what << codecs.join(QStringLiteral(" + "));
    v.what = what.join(QStringLiteral(" · "));

    // Subtitle files: the ones people wrote, in every language; the automatic ones in the video's own language only
    // (a site offers those translated by machine into a hundred more).
    auto bestFile = [](const QJsonArray& files, OnlineSubtitle* s) {
        for (const char* want : {"vtt", "srt", "ass", "ssa"})
            for (const QJsonValue& fv : files) {
                const QJsonObject f = fv.toObject();
                if (f.value(QStringLiteral("ext")).toString() == QLatin1String(want) && !f.value(QStringLiteral("url")).toString().isEmpty() &&
                    !f.value(QStringLiteral("protocol")).toString().startsWith(QLatin1String("m3u8"))) {
                    s->url = f.value(QStringLiteral("url")).toString();
                    s->ext = QString::fromLatin1(want);
                    if (s->label.isEmpty()) s->label = f.value(QStringLiteral("name")).toString();
                    return true;
                }
            }
        return false;
    };
    const QJsonObject subs = root.value(QStringLiteral("subtitles")).toObject();
    for (auto it = subs.begin(); it != subs.end(); ++it) {
        if (it.key() == QLatin1String("live_chat")) continue;
        OnlineSubtitle s;
        s.language = it.key();
        if (!bestFile(it.value().toArray(), &s)) continue;
        v.subtitles.append(s);
    }
    const QString own = root.value(QStringLiteral("language")).toString();
    const QJsonObject autos = root.value(QStringLiteral("automatic_captions")).toObject();
    // (the captions made from the video's own sound are marked "-orig"; those first)
    for (int pass = 0; pass < 2; ++pass) {
        for (auto it = autos.begin(); it != autos.end(); ++it) {
            const bool marked = it.key().endsWith(QLatin1String("-orig"));
            if (marked != (pass == 0) || (!marked && (own.isEmpty() || it.key() != own))) continue;
            OnlineSubtitle s;
            s.language = marked ? it.key().chopped(5) : it.key();
            s.automatic = true;
            bool have = false;
            for (const OnlineSubtitle& o : v.subtitles) have = have || (o.automatic && o.language == s.language);
            if (have || !bestFile(it.value().toArray(), &s)) continue;
            s.label = s.label.section(QStringLiteral(" (Original)"), 0, 0);   // (whoever shows it says that it is automatic)
            v.subtitles.append(s);
        }
    }
    for (const QJsonValue& cv : root.value(QStringLiteral("chapters")).toArray()) {
        const QJsonObject c = cv.toObject();
        v.chapters.append({qint64(c.value(QStringLiteral("start_time")).toDouble() * 1e9), c.value(QStringLiteral("title")).toString()});
    }
    r.ok = true;
    return r;
}

void OnlineResolver::resolve(const QString& url, const OnlineOptions& options, Done done)
{
    cancel();
    const QString prog = program();
    if (prog.isEmpty()) {
        OnlineResult r;
        r.error = tr("yt-dlp is not on this computer");
        r.detail = QStringLiteral("no-program");
        QTimer::singleShot(0, this, [done, r] { done(r); });
        return;
    }
    // Told where the runtime is, always: yt-dlp looks for Deno by itself, but only on the path a desktop
    // launcher hands the player, and for the others not at all.
    QStringList runtimes;
    const QString which = prog + QLatin1Char('|') + QFileInfo(prog).lastModified().toString(Qt::ISODate);
    const bool knowsRuntimes = m_noRuntimeOption != which;
    if (knowsRuntimes && !jsRuntime().isEmpty()) runtimes << jsRuntime();
    const QStringList args = arguments(url, options, runtimes);
    auto* p = new QProcess(this);
    m_process = p;
    const quint64 serial = m_serial;
    p->setProcessEnvironment(systemEnvironment());
    p->setProgram(prog);
    p->setArguments(args);
    ++m_runs;
    m_lastCommand = QFileInfo(prog).fileName() + QLatin1Char(' ') + args.join(QLatin1Char(' '));
    auto finish = [this, p, serial, url, options, done, which, knowsRuntimes](int code, bool timedOut) {
        if (serial != m_serial || m_process != p) return;
        const QByteArray out = p->readAllStandardOutput(), err = p->readAllStandardError();
        m_process = nullptr;
        p->deleteLater();
        if (knowsRuntimes && err.contains("no such option") && err.contains("js-runtimes")) {
            m_noRuntimeOption = which;   // an older yt-dlp: once more without it
            resolve(url, options, done);
            return;
        }
        OnlineResult r = parse(out, err, code, options);
        if (timedOut && !r.ok) r.error = tr("yt-dlp took too long to answer (the site may be slow, or blocked)");
        done(r);
    };
    connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [finish](int code, QProcess::ExitStatus st) {
        finish(st == QProcess::NormalExit ? code : -1, false);
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, serial, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || serial != m_serial || m_process != p) return;
        m_process = nullptr;
        p->deleteLater();
        OnlineResult r;
        r.error = tr("yt-dlp could not be started (%1)").arg(p->program());
        done(r);
    });
    QTimer::singleShot(90000, p, [p, finish] {
        if (p->state() == QProcess::NotRunning) return;
        p->disconnect();
        p->kill();
        p->waitForFinished(1000);
        finish(-1, true);
    });
    p->start(QIODevice::ReadOnly);
}

void OnlineResolver::version(std::function<void(const QString&)> done)
{
    const QString prog = program();
    if (prog.isEmpty()) { QTimer::singleShot(0, this, [done] { done(QString()); }); return; }
    auto* p = new QProcess(this);
    p->setProcessEnvironment(systemEnvironment());
    connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [p, done](int code, QProcess::ExitStatus st) {
        const QString v = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
        p->deleteLater();
        done(st == QProcess::NormalExit && code == 0 ? v : QString());
    });
    connect(p, &QProcess::errorOccurred, this, [p, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        p->deleteLater();
        done(QString());
    });
    QTimer::singleShot(20000, p, [p] { if (p->state() != QProcess::NotRunning) p->kill(); });
    p->start(prog, {QStringLiteral("--version")}, QIODevice::ReadOnly);
}

void OnlineResolver::fetch(const QString& url, const QList<QPair<QByteArray, QByteArray>>& headers,
                           std::function<void(const QByteArray&, const QString&)> done)
{
    network();
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(20000);
    for (const auto& h : headers)
        if (h.first.toLower() != "accept-encoding") req.setRawHeader(h.first, h.second);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { done({}, reply->errorString()); return; }
        const QByteArray data = reply->read(20 * 1024 * 1024);
        done(data, QString());
    });
}

void OnlineResolver::download(const QString& url, std::function<void(qint64, qint64)> progress,
                              std::function<void(const QByteArray&, const QString&)> done)
{
    network();
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "CRTPlayer");
    QNetworkReply* reply = m_net->get(req);
    if (progress) connect(reply, &QNetworkReply::downloadProgress, this, progress);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) done({}, reply->errorString());
        else done(reply->readAll(), QString());
    });
}

namespace {
// A program put in place: written beside its place first, so that a download cut short never replaces one that works.
bool putInPlace(const QString& path, const QByteArray& data, QString* error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile tmp(path + QStringLiteral(".new"));
    if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate) || tmp.write(data) != data.size()) {
        *error = QObject::tr("Could not write %1").arg(tmp.fileName());
        return false;
    }
    tmp.close();
    tmp.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner | QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                       QFileDevice::ReadOther | QFileDevice::ExeOther);
    QFile::remove(path);
    if (!tmp.rename(path)) { *error = QObject::tr("Could not put the program in place (%1)").arg(path); return false; }
    return true;
}

// The 64 hexadecimal digits in a checksum file, whichever way it is written ("<sum>  <name>" lines, or the
// "Hash : <SUM>" PowerShell writes). With a name: the sum on that name's line.
QByteArray checksumIn(const QByteArray& text, const QByteArray& name)
{
    static const QRegularExpression hex(QStringLiteral("\\b[0-9a-fA-F]{64}\\b"));
    QByteArray any;
    int sums = 0;
    for (const QByteArray& raw : text.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        const QRegularExpressionMatch m = hex.match(line);
        if (!m.hasMatch()) continue;
        const QByteArray sum = m.captured(0).toLower().toLatin1();
        const QStringList words = line.split(QRegularExpression(QStringLiteral("\\s+")));
        if (!name.isEmpty() && words.size() == 2 && (words[1] == QString::fromUtf8(name) || words[1] == QLatin1Char('*') + QString::fromUtf8(name))) return sum;
        any = sum;
        ++sums;
    }
    return name.isEmpty() && sums == 1 ? any : QByteArray();
}
} // namespace

void OnlineResolver::install(bool program, bool runtime, std::function<void(const QString&, qint64, qint64)> progress,
                             std::function<void(bool, const QString&, const QString&)> done)
{
    if (m_installing) return;
    const QString cpu = QSysInfo::currentCpuArchitecture();
    // The official builds that need nothing else installed (no Python).
    QString asset, denoAsset;
#if defined(Q_OS_WIN)
    asset = cpu == QLatin1String("arm64") ? QStringLiteral("yt-dlp_arm64.exe") : QStringLiteral("yt-dlp.exe");
    if (cpu == QLatin1String("x86_64")) denoAsset = QStringLiteral("deno-x86_64-pc-windows-msvc.zip");
#elif defined(Q_OS_LINUX)
    asset = cpu == QLatin1String("arm64") ? QStringLiteral("yt-dlp_linux_aarch64") : QStringLiteral("yt-dlp_linux");
    denoAsset = cpu == QLatin1String("arm64") ? QStringLiteral("deno-aarch64-unknown-linux-gnu.zip")
              : cpu == QLatin1String("x86_64") ? QStringLiteral("deno-x86_64-unknown-linux-gnu.zip") : QString();
#else
    asset = QStringLiteral("yt-dlp");
#endif
    QString base = qEnvironmentVariable("CRTPLAYER_YTDLP_RELEASE");
    if (base.isEmpty()) base = QStringLiteral("https://github.com/yt-dlp/yt-dlp/releases/latest/download");
    QString denoBase = qEnvironmentVariable("CRTPLAYER_DENO_RELEASE");
    if (denoBase.isEmpty()) denoBase = QStringLiteral("https://github.com/denoland/deno/releases/latest/download");
    if (denoAsset.isEmpty()) runtime = false;
    if (!program && !runtime) { QTimer::singleShot(0, this, [done] { done(false, tr("There is nothing to fetch for this kind of computer"), QString()); }); return; }
    m_installing = true;

    // Second (or only) step: Deno, one program in a zip archive.
    auto fetchRuntime = [this, denoBase, denoAsset, progress, done](const QString& sofar) {
        const QString name = tr("Deno");
        download(denoBase + QLatin1Char('/') + denoAsset + QStringLiteral(".sha256sum"), nullptr,
                 [this, denoBase, denoAsset, progress, done, sofar, name](const QByteArray& sums, const QString& err) {
            auto fail = [this, done, sofar](const QString& why) {
                m_installing = false;
                // yt-dlp itself is in place: that much worked.
                if (sofar.isEmpty()) done(false, why, QString());
                else done(true, sofar, tr("Deno (which yt-dlp needs for YouTube) could not be fetched: %1").arg(why));
            };
            if (!err.isEmpty()) { fail(tr("could not reach the download (%1)").arg(err)); return; }
            QByteArray want = checksumIn(sums, denoAsset.toUtf8());
            if (want.isEmpty()) want = checksumIn(sums, QByteArray());
            if (want.size() != 64) { fail(tr("the release lists no checksum for %1").arg(denoAsset)); return; }
            download(denoBase + QLatin1Char('/') + denoAsset, [progress, name](qint64 got, qint64 total) { if (progress) progress(name, got, total); },
                     [this, want, fail, done, sofar](const QByteArray& zip, const QString& err2) {
                if (!err2.isEmpty()) { fail(tr("the download failed (%1)").arg(err2)); return; }
                if (QCryptographicHash::hash(zip, QCryptographicHash::Sha256).toHex() != want) { fail(tr("the download does not match the release's checksum; it was not kept")); return; }
                const QString path = ownRuntimePath();
                const QByteArray exe = unzipOne(zip, QFileInfo(path).fileName());
                if (exe.isEmpty()) { fail(tr("the archive does not hold the program")); return; }
                QString why;
                if (!putInPlace(path, exe, &why)) { fail(why); return; }
                m_installing = false;
                done(true, sofar.isEmpty() ? tr("Deno is in place") : tr("yt-dlp and Deno are in place"), QString());
            });
        });
    };
    if (!program) { fetchRuntime(QString()); return; }

    // First the release's list of checksums, then the program itself, which must match it.
    download(base + QStringLiteral("/SHA2-256SUMS"), nullptr, [this, base, asset, runtime, fetchRuntime, progress, done](const QByteArray& sums, const QString& err) {
        auto fail = [this, done](const QString& why) { m_installing = false; done(false, why, QString()); };
        if (!err.isEmpty()) { fail(tr("Could not reach the download (%1)").arg(err)); return; }
        const QByteArray want = checksumIn(sums, asset.toUtf8());
        if (want.size() != 64) { fail(tr("The release lists no checksum for %1").arg(asset)); return; }
        const QString name = QStringLiteral("yt-dlp");
        download(base + QLatin1Char('/') + asset, [progress, name](qint64 got, qint64 total) { if (progress) progress(name, got, total); },
                 [this, want, fail, runtime, fetchRuntime, done](const QByteArray& data, const QString& err2) {
            if (!err2.isEmpty()) { fail(tr("The download failed (%1)").arg(err2)); return; }
            if (QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() != want) {
                fail(tr("The downloaded program does not match the release's checksum; it was not kept"));
                return;
            }
            QString why;
            if (!putInPlace(ownCopyPath(), data, &why)) { fail(why); return; }
            const QString sofar = tr("yt-dlp is in place");
            if (runtime) { fetchRuntime(sofar); return; }
            m_installing = false;
            done(true, sofar, QString());
        });
    });
}

QByteArray OnlineResolver::unzipOne(const QByteArray& zip, const QString& name)
{
    auto u16 = [&zip](qsizetype at) { return quint32(quint8(zip[at])) | quint32(quint8(zip[at + 1])) << 8; };
    auto u32 = [&zip](qsizetype at) {
        return quint32(quint8(zip[at])) | quint32(quint8(zip[at + 1])) << 8 | quint32(quint8(zip[at + 2])) << 16 | quint32(quint8(zip[at + 3])) << 24;
    };
    // The list of what the archive holds is at its end.
    const qsizetype end = zip.lastIndexOf(QByteArray("PK\x05\x06", 4));
    if (end < 0 || end + 22 > zip.size()) return {};
    const quint32 count = u16(end + 10);
    qsizetype at = u32(end + 16);
    const QByteArray want = name.toUtf8();
    for (quint32 i = 0; i < count; ++i) {
        if (at < 0 || at + 46 > zip.size() || u32(at) != 0x02014b50u) return {};
        const quint32 method = u16(at + 10), packed = u32(at + 20), size = u32(at + 24);
        const quint32 nameLen = u16(at + 28), extraLen = u16(at + 30), commentLen = u16(at + 32);
        const qsizetype local = u32(at + 42);
        if (at + 46 + qsizetype(nameLen) > zip.size()) return {};
        const QByteArray entry = zip.mid(at + 46, nameLen);
        at += 46 + nameLen + extraLen + commentLen;
        if (entry != want && !entry.endsWith('/' + want)) continue;
        if (packed == 0xffffffffu || size == 0xffffffffu || size > 600u * 1024 * 1024) return {};   // (no 4 GB archives here)
        if (local < 0 || local + 30 > zip.size() || u32(local) != 0x04034b50u) return {};
        const qsizetype data = local + 30 + u16(local + 26) + u16(local + 28);
        if (data < 0 || data + qsizetype(packed) > zip.size()) return {};
        if (method == 0) return packed == size ? zip.mid(data, size) : QByteArray();
        if (method != 8) return {};
        QByteArray out(qsizetype(size), Qt::Uninitialized);
        z_stream z{};
        if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return {};
        z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(zip.constData() + data));
        z.avail_in = packed;
        z.next_out = reinterpret_cast<Bytef*>(out.data());
        z.avail_out = size;
        const int r = inflate(&z, Z_FINISH);
        const bool ok = r == Z_STREAM_END && z.total_out == size;
        inflateEnd(&z);
        if (!ok) return {};
        if (quint32(crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(out.constData()), size)) != u32(at - (46 + nameLen + extraLen + commentLen) + 16)) return {};
        return out;
    }
    return {};
}

bool OnlineResolver::isMediaType(const QString& contentType)
{
    const QString t = contentType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    if (t.startsWith(QLatin1String("video/")) || t.startsWith(QLatin1String("audio/"))) return true;
    static const QStringList media = {QStringLiteral("application/vnd.apple.mpegurl"), QStringLiteral("application/x-mpegurl"), QStringLiteral("application/dash+xml"),
                                      QStringLiteral("application/ogg"), QStringLiteral("application/octet-stream"), QStringLiteral("binary/octet-stream"),
                                      QStringLiteral("application/mp4"), QStringLiteral("application/x-matroska"), QStringLiteral("application/mxf"),
                                      QStringLiteral("application/vnd.rn-realmedia"), QStringLiteral("application/x-flac")};
    return media.contains(t);
}

int OnlineResolver::versionAgeDays(const QString& version, const QDate& today)
{
    const QStringList p = version.trimmed().split(QLatin1Char('.'));
    if (p.size() < 3) return -1;
    const QDate d(p[0].toInt(), p[1].toInt(), p[2].toInt());
    return d.isValid() && d.year() >= 2000 ? int(d.daysTo(today)) : -1;
}

void OnlineResolver::probe(const QString& url, std::function<void(Kind)> done)
{
    network();
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(8000);
    req.setRawHeader("User-Agent", "CRT-Player");
    QNetworkReply* reply = m_net->get(req);
    // Only the answer's first lines are wanted: the request is dropped as soon as they are there.
    auto answered = std::make_shared<bool>(false);
    auto answer = [reply, done, answered](bool known) {
        if (*answered) return;
        *answered = true;
        const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString().section(QLatin1Char(';'), 0, 0).trimmed().toLower();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (!known || status < 200 || status >= 300) done(Kind::Unknown);
        else if (isMediaType(type)) done(Kind::Media);
        else done(type == QLatin1String("text/html") || type == QLatin1String("application/xhtml+xml") ? Kind::Page : Kind::Unknown);
    };
    connect(reply, &QNetworkReply::metaDataChanged, this, [reply, answer] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 300 && status < 400) return;   // (on its way somewhere else)
        answer(true);
        reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, answer] {
        answer(reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
    });
}

bool OnlineResolver::isPage(const QString& url)
{
    const QUrl u(url);
    if (u.scheme() != QLatin1String("http") && u.scheme() != QLatin1String("https")) return false;
    static const QStringList media = {"mp4", "mkv", "webm", "mov", "avi", "m4v", "ts", "m2ts", "mts", "mpg", "mpeg", "flv", "wmv", "ogv", "m3u8", "mpd",
                                      "mp3", "m4a", "flac", "ogg", "opus", "wav", "aac", "3gp", "vob", "divx"};
    return !media.contains(QFileInfo(u.path()).suffix().toLower());
}

QString OnlineResolver::reference(const QString& page, const QString& title)
{
    QString p = page;
    p.replace(QLatin1Char('#'), QStringLiteral("%23"));
    return QStringLiteral("web:") + p + (title.isEmpty() ? QString() : QLatin1Char('#') + QString::fromUtf8(QUrl::toPercentEncoding(title)));
}

QString OnlineResolver::referencePage(const QString& entry)
{
    if (!isReference(entry)) return entry;
    QString p = entry.mid(4).section(QLatin1Char('#'), 0, 0);
    p.replace(QStringLiteral("%23"), QStringLiteral("#"));
    return p;
}

QString OnlineResolver::referenceTitle(const QString& entry)
{
    return isReference(entry) && entry.contains(QLatin1Char('#')) ? QUrl::fromPercentEncoding(entry.section(QLatin1Char('#'), 1).toUtf8()) : QString();
}

QByteArray OnlineResolver::tidyCaptions(const QByteArray& vtt)
{
    if (!vtt.contains("<c>") && !vtt.contains("<c.")) return vtt;   // (not the rolling kind)
    static const QRegularExpression timing(QStringLiteral(R"(^((?:\d+:)?\d\d:\d\d\.\d\d\d)\s+-->\s+((?:\d+:)?\d\d:\d\d\.\d\d\d))"));
    static const QRegularExpression tag(QStringLiteral("<[^>]*>"));
    struct Line { qint64 start, end; QString text; };
    QList<Line> lines;
    qint64 start = -1, end = -1;
    QString last;
    auto close = [&] {
        if (start < 0) return;
        if (end - start >= 50 && !last.isEmpty()) {   // (the 10 ms cues only repeat the finished line)
            if (!lines.isEmpty() && lines.last().text == last) lines.last().end = end;
            else lines.append({start, end, last});
        }
        start = -1;
        last.clear();
    };
    for (const QByteArray& raw : vtt.split('\n')) {
        QString l = QString::fromUtf8(raw);
        if (l.endsWith(QLatin1Char('\r'))) l.chop(1);
        const QRegularExpressionMatch m = timing.match(l);
        if (m.hasMatch()) {
            close();
            start = cueTime(m.captured(1));
            end = cueTime(m.captured(2));
            continue;
        }
        if (start < 0) continue;   // (the header)
        l.remove(tag);
        l = l.trimmed();
        if (!l.isEmpty()) last = l;   // the line at the bottom is the one being spoken
    }
    close();
    QByteArray out = "WEBVTT\n\n";
    for (int i = 0; i < lines.size(); ++i) {
        qint64 e = lines[i].end;
        if (i + 1 < lines.size()) e = std::min(e, lines[i + 1].start);
        if (e <= lines[i].start) continue;
        out += cueStamp(lines[i].start).toUtf8() + " --> " + cueStamp(e).toUtf8() + '\n' + lines[i].text.toUtf8() + "\n\n";
    }
    return out;
}

QJsonObject OnlineResolver::report() const
{
    return QJsonObject{{"program", program()}, {"ownCopy", programIsOwnCopy()}, {"runs", m_runs}, {"busy", busy()}, {"lastCommand", m_lastCommand},
                       {"jsRuntime", jsRuntime()}, {"installing", m_installing}};
}
