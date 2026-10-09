#pragma once
// Videos from web sites, found with yt-dlp (2.17).
//
// yt-dlp is a program of its own, kept up to date by its own project (video sites change every few
// weeks, and it follows them). The player does not contain it: it runs the yt-dlp on this computer,
// asks it what a page holds, and plays the streams it names. Nothing is downloaded to disk.
#include "playback/WebSource.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QNetworkAccessManager;


struct OnlineSubtitle {
    QString label;            // the site's name for it ("English"), if it gave one
    QString language;
    QString url;
    QString ext;              // vtt, srt, ass
    bool automatic = false;
};

struct OnlineEntry {          // one video of a playlist
    QString url;
    QString title;
    double seconds = 0;
};

struct OnlineChapter {
    qint64 startNs = 0;
    QString title;
};

struct OnlineVideo {
    QString page;                                   // the video's own page (what a playlist entry or a resume point is kept under)
    QString id, title, uploader, site;
    double seconds = 0;
    bool live = false;
    QList<WebStream> streams;                       // one address, or the picture's and the sound's...
    QString manifest;                               // ...or a manifest GStreamer plays by itself (HLS, DASH)
    QList<QPair<QByteArray, QByteArray>> headers;   // what the site wants sent along (for the manifest and for subtitle files)
    QString what;                                   // "1080p · VP9 + Opus"
    int width = 0, height = 0;
    QList<OnlineSubtitle> subtitles;
    QVector<OnlineChapter> chapters;
};

struct OnlineResult {
    bool ok = false;
    QString error;            // one line for the viewer
    QString detail;           // what yt-dlp said
    bool playlist = false;
    QString playlistTitle;
    QList<OnlineEntry> entries;
    OnlineVideo video;
    QStringList notes;        // things worth telling once (no JavaScript runtime for YouTube, ...)
};

struct OnlineOptions {
    int maxHeight = 1080;                 // the largest picture asked for
    QStringList videoCodecs;              // what this computer decodes: "h264", "vp9", "av1", "hevc"
    QStringList audioCodecs;              // "aac", "opus", "vorbis", "mp3"
    bool preferH264 = false;              // no graphics card: the codec that is cheapest to decode on the CPU
};

class OnlineResolver : public QObject {
    Q_OBJECT
public:
    explicit OnlineResolver(QObject* parent = nullptr);
    ~OnlineResolver() override;

    // Where yt-dlp is, or empty: the path set by the viewer, the copy the player fetched, the system's.
    void setConfiguredProgram(const QString& path) { m_configured = path; m_lookedAt = 0; }
    QString program() const;
    static QString ownCopyPath();         // where the player keeps a copy it fetched itself
    bool programIsOwnCopy() const;
    // The JavaScript runtime yt-dlp is pointed at, as "name:path" (YouTube needs one), or empty: the system's
    // Deno, the copy the player fetched, then Node, Bun, QuickJS.
    static QString jsRuntime();
    static QString ownRuntimePath();

    // Asks yt-dlp about a page. One question at a time: a new one drops the one before it.
    using Done = std::function<void(const OnlineResult&)>;
    void resolve(const QString& url, const OnlineOptions& options, Done done);
    void cancel();
    bool busy() const { return m_process != nullptr; }

    // "2025.11.12", or empty when there is no yt-dlp (or it does not run).
    void version(std::function<void(const QString&)> done);

    // Fetches the official yt-dlp program for this system into the player's own folder, or updates that
    // copy (`program`); and Deno, the JavaScript runtime yt-dlp uses for YouTube, when the computer has none
    // (`runtime`). Each is checked against the checksum its release publishes before it is kept.
    void install(bool program, bool runtime, std::function<void(const QString& what, qint64 got, qint64 total)> progress,
                 std::function<void(bool ok, const QString& message, const QString& problem)> done);   // problem: what could not be fetched, though the rest was
    bool installing() const { return m_installing; }

    // Is this address a media file or stream itself, or a page? Asked of the server: the type it names for
    // the address. Unknown: it gave no answer, or named a type that is neither.
    enum class Kind { Media, Page, Unknown };
    void probe(const QString& url, std::function<void(Kind)> done);

    // A small file over HTTP with the site's headers (a subtitle file).
    void fetch(const QString& url, const QList<QPair<QByteArray, QByteArray>>& headers, std::function<void(const QByteArray&, const QString& error)> done);

    // ---- pure parts (unit tests)
    static QStringList arguments(const QString& url, const OnlineOptions& options, const QStringList& jsRuntimes);
    static OnlineResult parse(const QByteArray& json, const QByteArray& errors, int exitCode, const OnlineOptions& options);
    // Is this address a page to ask yt-dlp about (true), or a media file or stream to open as it is?
    static bool isPage(const QString& url);
    // "web:<page>#<title>": how a web video is kept in the playlist.
    static QString reference(const QString& page, const QString& title);
    static bool isReference(const QString& entry) { return entry.startsWith(QLatin1String("web:")); }
    static QString referencePage(const QString& entry);
    static QString referenceTitle(const QString& entry);
    // An automatic-caption file as YouTube writes it (each line repeated while the next one rolls in,
    // words timed one by one) as plain WebVTT lines.
    static QByteArray tidyCaptions(const QByteArray& vtt);
    // One file out of a zip archive held in memory (how Deno is published); empty when it is not there.
    static QByteArray unzipOne(const QByteArray& zip, const QString& name);
    // Does a server's content type name media (a file, a stream, a manifest)?
    static bool isMediaType(const QString& contentType);
    // The date in a yt-dlp version ("2026.08.19" → days since then, as of `today`; -1 when it is no date).
    static int versionAgeDays(const QString& version, const class QDate& today);

    QJsonObject report() const;

private:
    QString m_configured;
    QPointer<QProcess> m_process;
    quint64 m_serial = 0;
    bool m_installing = false;
    void download(const QString& url, std::function<void(qint64, qint64)> progress, std::function<void(const QByteArray&, const QString&)> done);
    QString m_noRuntimeOption;           // the yt-dlp (its path and date) that does not know --js-runtimes (older than late 2025)
    QNetworkAccessManager* m_net = nullptr;
    QNetworkAccessManager* network();
    QString m_lastCommand;
    mutable qint64 m_lookedAt = 0;
    mutable QString m_seenProgram, m_seenRuntime;
    int m_runs = 0;
};
