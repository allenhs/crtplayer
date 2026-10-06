// Videos from web sites (2.17), as part of MainWindow: an address that is a page is put to yt-dlp
// (online/OnlineVideo.h), and what it names is played: one stream, the picture and the sound as two, or a
// manifest. The playlist keeps the page ("web:<page>#<title>"), never the streams' own addresses,
// which stop working after a few hours.
#include "MainWindow.h"

#include "online/OnlineVideo.h"
#include "playback/Player.h"
#include "render/DeskView.h"
#include "render/VideoWidget.h"
#include "app/DeskWindow.h"
#include "settings/ResumeStore.h"
#include "ui/ControlBar.h"
#include "ui/PlaybackPanel.h"
#include "ui/PlaylistPanel.h"

#include <QApplication>
#include <QClipboard>
#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QUrl>
#include <gst/tag/tag.h>

namespace {
// An address as it is shown: without "https://", and not longer than a line.
QString shownAddress(const QString& page)
{
    const QUrl u(page);
    QString s = u.host() + u.path() + (u.hasQuery() ? QLatin1Char('?') + u.query() : QString());
    if (s.startsWith(QLatin1String("www."))) s.remove(0, 4);
    return s.size() > 70 ? s.left(67) + QStringLiteral("…") : s;
}
} // namespace

QStringList MainWindow::addressesIn(const QString& text)
{
    QStringList out;
    for (const QString& word : text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
        if (!word.startsWith(QLatin1String("http://")) && !word.startsWith(QLatin1String("https://"))) continue;
        const QUrl u(word, QUrl::StrictMode);
        if (!u.isValid() || u.host().isEmpty()) continue;
        if (!out.contains(word)) out << word;
        if (out.size() >= 200) break;
    }
    return out;
}

void MainWindow::openLink(const QString& address, bool playNow)
{
    QString text = address.trimmed();
    // "youtube.com/watch?v=…", typed without the scheme
    if (!text.contains(QStringLiteral("://")) && !text.contains(QLatin1Char(' ')) && text.contains(QLatin1Char('.')) && text.contains(QLatin1Char('/')) &&
        !QFileInfo::exists(text))
        text.prepend(QStringLiteral("https://"));
    const QStringList found = addressesIn(text);
    if (found.isEmpty()) { showOsd(tr("That is not a web address"), 2500); return; }
    openFiles(found, playNow);
}

void MainWindow::promptOpenLink()
{
    // What is on the clipboard is usually what is meant.
    const QStringList there = addressesIn(QApplication::clipboard()->text());
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Open link"),
                                               tr("The address of a page with a video on it (YouTube and many other sites),\nor of a video file or stream:"),
                                               QLineEdit::Normal, there.value(0), &ok);
    if (ok && !text.trimmed().isEmpty()) openLink(text, true);
}

bool MainWindow::pasteLink()
{
    const QStringList found = addressesIn(QApplication::clipboard()->text());
    if (found.isEmpty()) { showOsd(tr("There is no web address on the clipboard"), 2500); return false; }
    openFiles(found, true);
    return true;
}

void MainWindow::setHint(const QString& text)
{
    m_emptyHint->setText(text.isEmpty() ? tr("Drop video files here, press Ctrl+O to open a file\nor Ctrl+L to open a link") : text);
    if (!text.isEmpty()) {
        m_emptyHint->show();
        layoutOverlays();
    }
}

QString MainWindow::resumeKey() const
{
    if (!m_currentLocal.isEmpty()) return m_currentLocal;
    if (!m_webPage.isEmpty() && !m_webLive) return QStringLiteral("web:") + m_webPage;
    return {};
}

OnlineOptions MainWindow::onlineOptions() const
{
    OnlineOptions o;
    QStringList containers;
    Player::localFormats(&containers, &o.videoCodecs, &o.audioCodecs);
    const bool software = m_video->softwareRenderer();
    int h = m_settings.onlineMaxHeight;
    if (h <= 0) {
        // As large as this screen shows: the smallest of the usual sizes that fills it (or all but fills it).
        const QScreen* s = screen() ? screen() : QGuiApplication::primaryScreen();
        const double px = s ? s->size().height() * s->devicePixelRatio() : 1080.0;
        h = 2160;
        for (int usual : {360, 480, 720, 1080, 1440, 2160})
            if (usual >= px * 0.9) { h = usual; break; }
        // Without a graphics card every picture is decoded and drawn by the processor: 1080p is plenty for it.
        if (software) h = std::min(h, 1080);
    }
    o.maxHeight = h;
    o.preferH264 = software;   // the cheapest to decode on the processor
    return o;
}

// A page (or a "web:" entry of the playlist): yt-dlp is asked what it holds. An address that is not known
// to be a page is first put to its server: a media file or stream is opened as it is, as before 2.17.
void MainWindow::playWeb(const QString& entry, int index)
{
    const QString page = OnlineResolver::referencePage(entry);
    m_webAsking = page;
    m_mediaTitle = OnlineResolver::referenceTitle(entry);
    if (index >= 0) m_playlist->setCurrentIndex(index);
    m_lastError.clear();
    m_onlineAsked.clear();
    m_player->close();
    m_video->clearFrame();
    if (m_desk) m_desk->view()->clearFrame();
    updateTitle();
    const int request = m_webRequest;
    setHint(tr("Looking for the video…\n%1").arg(shownAddress(page)));
    m_webIsPage = true;
    if (OnlineResolver::isReference(entry)) { askYtDlp(entry, index, request); return; }
    m_online->probe(page, [this, entry, page, index, request](OnlineResolver::Kind kind) {
        if (request != m_webRequest) return;
        m_webIsPage = kind == OnlineResolver::Kind::Page;
        // A media file, or a server that does not say and no yt-dlp to ask: opened as it is.
        if (kind == OnlineResolver::Kind::Media || (kind == OnlineResolver::Kind::Unknown && m_online->program().isEmpty())) {
            m_webAsking.clear();
            setHint(QString());
            const qint64 start = std::max<qint64>(m_webStartNs, 0);
            openResolved(index, page, start, QFileInfo(QUrl(page).path()).fileName().isEmpty() ? shownAddress(page) : QFileInfo(QUrl(page).path()).fileName(), QString());
            return;
        }
        askYtDlp(entry, index, request);
    });
}

void MainWindow::askYtDlp(const QString& entry, int index, int request)
{
    if (m_online->program().isEmpty()) { offerYtDlp(entry, index); return; }
    ++m_webResolves;
    if (m_ytVersion.isEmpty()) updateOnlineStatus();   // (asks for its version, for what is said when it fails)
    setHint(tr("yt-dlp is finding the video…\n%1").arg(shownAddress(OnlineResolver::referencePage(entry))));
    m_online->resolve(OnlineResolver::referencePage(entry), onlineOptions(),
                      [this, entry, index, request](const OnlineResult& r) { webResolved(r, entry, index, request); });
}

void MainWindow::webResolved(const OnlineResult& r, const QString& entry, int index, int request)
{
    if (request != m_webRequest) return;
    const QString asked = OnlineResolver::referencePage(entry);
    // Things worth knowing once (no JavaScript runtime for YouTube).
    QStringList fresh;
    for (const QString& n : r.notes)
        if (!m_webNotesShown.contains(n)) { m_webNotesShown << n; fresh << n; }
    if (!r.notes.isEmpty()) updateOnlineStatus();

    if (!r.ok) {
        if (r.detail == QLatin1String("no-program")) { offerYtDlp(entry, index); return; }
        if (!m_webIsPage) {
            // An address whose server did not say what it is (neither a page nor media): it may still be
            // a stream GStreamer knows. Tried as it is; if that fails too, both reasons are shown.
            m_webAsking.clear();
            m_webFallbackNote = r.error;
            setHint(QString());
            openResolved(index, asked, std::max<qint64>(m_webStartNs, 0), shownAddress(asked), QString());
            return;
        }
        QString text = r.error;
        if (!r.notes.isEmpty()) text += QStringLiteral("\n\n") + r.notes.join(QStringLiteral("\n"));
        const int age = OnlineResolver::versionAgeDays(m_ytVersion, QDate::currentDate());
        if (age > 60)
            text += QStringLiteral("\n\n") + tr("This yt-dlp is from %1. Video sites change every few weeks, and an older yt-dlp stops finding their "
                                                "videos: a newer one usually helps (Settings → Playback → Videos from web sites).").arg(m_ytVersion);
        webFailed(tr("yt-dlp could not find the video"), text, r.detail);
        return;
    }

    if (r.playlist) {
        if (m_webDepth >= 2) { webFailed(tr("This list only leads to more lists"), asked, r.detail); return; }
        QStringList refs;
        for (const OnlineEntry& e : r.entries) {
            refs << OnlineResolver::reference(e.url, e.title);
            if (refs.size() >= 1000) break;
        }
        if (index >= 0) {
            for (QStringList* l : {&m_shufflePlayed, &m_shuffleHistory}) l->removeAll(entry);
            m_playlist->replaceAt(index, refs);
        }
        showOsd(tr("%1 — %2 videos").arg(r.playlistTitle.isEmpty() ? shownAddress(asked) : r.playlistTitle).arg(refs.size()), 3000);
        ++m_webDepth;
        m_webInner = true;
        playSource(refs.first(), index);
        return;
    }

    const OnlineVideo& v = r.video;
    const QString page = v.page.isEmpty() ? asked : v.page;
    const QString ref = OnlineResolver::reference(page, v.title);
    if (index >= 0 && m_playlist->at(index) != ref) {
        for (QStringList* l : {&m_shufflePlayed, &m_shuffleHistory})
            for (QString& s : *l) if (s == entry) s = ref;
        m_playlist->replaceAt(index, {ref});
    }
    if (m_lastSource == entry) m_lastSource = ref;
    m_webAsking.clear();
    m_webResolvedAt.start();
    m_webPage = page;
    m_webSite = v.site;
    m_webWhat = v.what;
    m_webLive = v.live;
    m_webHeaders = v.headers;
    m_mediaTitle = v.title.isEmpty() ? shownAddress(page) : v.title;

    // Subtitle files on offer: the ones people wrote first, then the automatic ones.
    QStringList labels;
    for (int pass = 0; pass < 2; ++pass) {
        for (const OnlineSubtitle& s : v.subtitles) {
            if (s.automatic != (pass == 1)) continue;
            QString label = s.label;
            if (label.isEmpty() || label == s.language) {
                const gchar* name = gst_tag_get_language_name(s.language.section(QLatin1Char('-'), 0, 0).toUtf8().constData());
                label = name ? QString::fromUtf8(name) : s.language;
                if (name && s.language.contains(QLatin1Char('-'))) label += QStringLiteral(" (%1)").arg(s.language);
            }
            if (s.automatic) label = tr("%1 (automatic)").arg(label);
            if (labels.contains(label)) label += QStringLiteral(" (%1)").arg(s.language);
            if (labels.contains(label) || m_webSubs.contains(s.url)) continue;
            labels << label;
            m_extSubs.append({label, s.url});
            m_webSubs.insert(s.url, {s.ext, s.automatic});
        }
    }
    m_webChapters.clear();
    for (const OnlineChapter& c : v.chapters) m_webChapters.append({c.startNs, c.title});
    rebuildSubtitleMenu();

    qint64 start = 0;
    if (m_webStartNs >= 0) start = m_webStartNs;                                        // (asked for again, where it was)
    else if (!v.live) start = m_resume->position(QStringLiteral("web:") + page) * 1000000;   // where it was left
    m_webStartNs = start;
    setHint(QString());
    if (!v.streams.isEmpty()) {
        m_player->setHttpHeaders({});
        openResolved(index, QString(), start, m_mediaTitle, v.what, v.streams);
    } else {
        m_player->setHttpHeaders(v.headers);
        openResolved(index, v.manifest, start, m_mediaTitle, v.live ? tr("live") : v.what);
    }
    if (!fresh.isEmpty()) {
        m_lastWarning = fresh.join(QLatin1Char(' '));
        QTimer::singleShot(2500, this, [this, fresh] { showOsd(fresh.first(), 9000); });
    }
}

void MainWindow::webFailed(const QString& title, const QString& text, const QString& details)
{
    m_webAsking.clear();
    ++m_webFailures;
    m_lastError = title + QStringLiteral(": ") + text;
    setHint(title + tr("\nDetails are in the error dialog. Drop another file to continue."));
    auto* box = new QMessageBox(QMessageBox::Warning, title, text, QMessageBox::Ok, this);
    box->setObjectName(QStringLiteral("webErrorDialog"));
    if (!details.isEmpty()) box->setDetailedText(details);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

// No yt-dlp on this computer: what it is, and the offer to fetch it. The video waits meanwhile.
void MainWindow::offerYtDlp(const QString& entry, int index)
{
    m_webAsking.clear();
    m_webPending = entry;
    m_webPendingIndex = index;
    m_onlineAsked = QStringLiteral("no-program");
    m_lastError = tr("yt-dlp is needed to play videos from web pages, and it is not on this computer");
    setHint(tr("To play videos from web pages, yt-dlp is needed.\nSettings → Playback → Videos from web sites"));
    if (m_ytDialog) { m_ytDialog->raise(); return; }
    const bool runtime = OnlineResolver::jsRuntime().isEmpty();
    auto* box = new QMessageBox(QMessageBox::Question, tr("yt-dlp is needed"),
                                tr("To play videos from web pages (YouTube and many other sites), CRT Player uses yt-dlp: a free program, kept up "
                                   "to date by its own project, that knows where each site keeps its videos. It is not on this computer yet."),
                                QMessageBox::NoButton, this);
    box->setInformativeText(tr("CRT Player can fetch the official yt-dlp program from its project (github.com/yt-dlp, about 40 MB)%1 into its own "
                               "folder. Nothing else on the computer is changed, and the video then starts.")
                                .arg(runtime ? tr(", and Deno (github.com/denoland, about 42 MB), which yt-dlp needs for YouTube,") : QString()));
    box->setObjectName(QStringLiteral("ytDlpDialog"));
    QPushButton* get = box->addButton(tr("Get yt-dlp"), QMessageBox::AcceptRole);
    get->setObjectName(QStringLiteral("ytDlpGet"));
    box->addButton(QMessageBox::Cancel);
    box->setAttribute(Qt::WA_DeleteOnClose);
    connect(box, &QMessageBox::buttonClicked, this, [this, box, get](QAbstractButton* b) {
        if (b == get) fetchYtDlp();
        else { m_webPending.clear(); m_webPendingIndex = -1; }
    });
    m_ytDialog = box;
    box->open();
}

void MainWindow::fetchYtDlp()
{
    if (m_online->installing()) return;
    const bool have = !m_online->program().isEmpty();
    const bool own = have && m_online->programIsOwnCopy();
    const bool runtime = OnlineResolver::jsRuntime().isEmpty();
    // A yt-dlp of the system's own is left to the system; only what is missing is fetched. Asked once
    // more (nothing is missing), the player gets a copy of its own, which it then uses and keeps current.
    const bool program = !have || own || !runtime;
    m_ytStatus = tr("Fetching…");
    updateOnlineStatus();
    m_online->install(program, runtime,
                      [this](const QString& what, qint64 got, qint64 total) {
                          m_ytStatus = total > 0 ? tr("Fetching %1… %2 of %3 MB").arg(what).arg(got / 1048576.0, 0, 'f', 1).arg(total / 1048576.0, 0, 'f', 1)
                                                 : tr("Fetching %1… %2 MB").arg(what).arg(got / 1048576.0, 0, 'f', 1);
                          updateOnlineStatus();
                      },
                      [this](bool ok, const QString& message, const QString& problem) {
                          m_ytStatus.clear();
                          m_ytVersion.clear();
                          m_onlineAsked = ok ? QStringLiteral("fetched") : QStringLiteral("fetch-failed");
                          ++(ok ? m_ytFetches : m_ytFetchFailures);
                          updateOnlineStatus();
                          if (!ok) {
                              m_lastWarning = tr("yt-dlp could not be fetched: %1").arg(message);
                              showOsd(m_lastWarning, 8000);
                              auto* box = new QMessageBox(QMessageBox::Warning, tr("yt-dlp could not be fetched"), message, QMessageBox::Ok, this);
                              box->setInformativeText(tr("yt-dlp can also be installed by hand (the README says how): the player uses the one it finds."));
                              box->setObjectName(QStringLiteral("ytDlpFailedDialog"));
                              box->setAttribute(Qt::WA_DeleteOnClose);
                              box->open();
                              return;
                          }
                          showOsd(problem.isEmpty() ? message : message + QStringLiteral(" · ") + problem, problem.isEmpty() ? 4000 : 9000);
                          if (!problem.isEmpty()) m_lastWarning = problem;
                          // The video that was waiting for it.
                          const QString pending = m_webPending;
                          const int index = m_webPendingIndex;
                          m_webPending.clear();
                          m_webPendingIndex = -1;
                          if (!pending.isEmpty() && (index < 0 || m_playlist->at(index) == pending) && m_player->currentUri().isEmpty()) playSource(pending, index);
                      });
}

void MainWindow::setOnlineMaxHeight(int height)
{
    m_settings.onlineMaxHeight = std::max(0, height);
    m_playbackPanel->setOnlineHeight(m_settings.onlineMaxHeight);
}

void MainWindow::setYtDlpPath(const QString& path)
{
    m_settings.ytDlpPath = path;
    m_online->setConfiguredProgram(path);
    m_ytVersion.clear();
    updateOnlineStatus();
}

// The line in the settings, and what its button offers.
void MainWindow::updateOnlineStatus(bool askVersion)
{
    if (m_online->installing()) {
        m_playbackPanel->setOnlineStatus(m_ytStatus, tr("Fetching…"), false);
        return;
    }
    const QString program = m_online->program();
    const QString runtime = OnlineResolver::jsRuntime();
    QString runtimeLine;
    if (runtime.isEmpty()) runtimeLine = tr("For YouTube, yt-dlp also needs a JavaScript runtime (Deno), and there is none on this computer.");
    else {
        QString name = runtime.section(QLatin1Char(':'), 0, 0);
        name[0] = name[0].toUpper();
        runtimeLine = tr("JavaScript for YouTube: %1.").arg(name);
    }
    if (program.isEmpty()) {
        m_playbackPanel->setOnlineStatus(tr("yt-dlp is not on this computer. The player can fetch the official program from its project (github.com/yt-dlp) "
                                            "and keep it in its own folder; nothing else is changed."), tr("Get yt-dlp"), true);
        return;
    }
    const bool own = m_online->programIsOwnCopy();
    auto show = [this, program, own, runtime, runtimeLine] {
        QString s = m_ytVersion.isEmpty() ? tr("yt-dlp") : tr("yt-dlp %1").arg(m_ytVersion);
        s += own ? tr(" (the player's own copy)") : QStringLiteral(" (%1)").arg(program);
        const int age = OnlineResolver::versionAgeDays(m_ytVersion, QDate::currentDate());
        if (age > 60) s += tr(", %1 days old: sites change every few weeks, and a newer one finds more").arg(age);
        s += QStringLiteral(". ") + runtimeLine;
        const QString button = own ? (runtime.isEmpty() ? tr("Update yt-dlp and get Deno") : tr("Update yt-dlp"))
                                   : runtime.isEmpty() ? tr("Get Deno (for YouTube)") : tr("Use the newest yt-dlp instead");
        m_playbackPanel->setOnlineStatus(s, button, true);
    };
    show();
    if (!askVersion || !m_ytVersion.isEmpty() || m_ytAsking) return;
    m_ytAsking = true;
    m_online->version([this](const QString& v) {
        m_ytAsking = false;
        m_ytVersion = v.isEmpty() ? QStringLiteral("?") : v;
        if (!m_online->installing()) updateOnlineStatus();
    });
}

QJsonObject MainWindow::onlineReport() const
{
    QJsonObject o = m_online->report();
    o["asking"] = m_webAsking;
    o["idle"] = m_webAsking.isEmpty() && m_player->state() != Player::State::Loading;
    o["resumeKey"] = resumeKey();
    o["failures"] = m_webFailures;
    o["fetches"] = m_ytFetches;
    o["fetchFailures"] = m_ytFetchFailures;
    o["wallClock"] = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    o["page"] = m_webPage;
    o["site"] = m_webSite;
    o["what"] = m_webWhat;
    o["live"] = m_webLive;
    o["title"] = m_mediaTitle;
    o["resolves"] = m_webResolves;
    o["askedAgain"] = m_webReasks;
    o["retried"] = m_webRetried;
    o["asked"] = m_onlineAsked;
    o["dialog"] = m_ytDialog ? QStringLiteral("yt-dlp") : QString();
    o["fallbackNote"] = m_webFallbackNote;
    o["version"] = m_ytVersion;
    o["maxHeightSetting"] = m_settings.onlineMaxHeight;
    const OnlineOptions opt = onlineOptions();
    o["maxHeight"] = opt.maxHeight;
    o["preferH264"] = opt.preferH264;
    o["hint"] = m_emptyHint->isVisible() ? m_emptyHint->text() : QString();
    o["status"] = m_playbackPanel->onlineStatus();
    o["button"] = m_playbackPanel->onlineButton();
    QJsonArray subs, automatic;
    for (const auto& s : m_extSubs) {
        subs.append(s.first);
        automatic.append(m_webSubs.value(s.second).second);
    }
    o["subtitles"] = subs;
    o["subtitlesAutomatic"] = automatic;
    o["subtitleLoaded"] = m_extSubLabel;
    QJsonArray chapters;
    for (const auto& c : m_webChapters) chapters.append(QJsonObject{{"ms", double(c.first / 1000000)}, {"title", c.second}});
    o["chapters"] = chapters;
    o["player"] = m_player->webReport();
    o["notes"] = QJsonArray::fromStringList(m_webNotesShown);
    QJsonArray entries, labels;
    for (int i = 0; i < m_playlist->count(); ++i) {
        entries.append(m_playlist->at(i));
        labels.append(m_playlist->labelAt(i));
    }
    o["playlist"] = entries;
    o["playlistLabels"] = labels;
    o["playlistIndex"] = m_playlist->currentIndex();
    QStringList open;   // (the dialogs that are up)
    for (QWidget* w : QApplication::topLevelWidgets())
        if (w->isVisible() && qobject_cast<QMessageBox*>(w)) open << w->objectName() + QStringLiteral(": ") + static_cast<QMessageBox*>(w)->text();
    o["dialogs"] = QJsonArray::fromStringList(open);
    return o;
}
