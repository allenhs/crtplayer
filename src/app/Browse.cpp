// The browser of web videos (2.18): the window's side of it (ui/BrowseScreen.h, online/WebBrowse.h).
#include "MainWindow.h"
#include "online/OnlineVideo.h"
#include "online/WebBrowse.h"
#include "playback/Player.h"
#include "render/VideoWidget.h"
#include "settings/ResumeStore.h"
#include "ui/BrowseScreen.h"

#include <QJsonArray>

void MainWindow::setupBrowse()
{
    m_webBrowse = new WebBrowse(m_online, this);
    m_browse = new BrowseScreen(m_webBrowse, this);
    m_browse->setResumeLookup([this](const QString& url) { return m_resume ? m_resume->position(QStringLiteral("web:") + url) : 0; });
    connect(m_browse, &BrowseScreen::closeRequested, this, [this] { showBrowse(false); });
    connect(m_browse, &BrowseScreen::updateYtDlpRequested, this, [this] { fetchYtDlp(true); });
    connect(m_browse, &BrowseScreen::playRequested, this, [this](const WebItem& item, bool fromStart) {
        if (fromStart && m_resume) m_resume->forget(QStringLiteral("web:") + item.url);
        m_browsePicked = item.toJson();
        m_browseResume = false;   // (another video now)
        showBrowse(false);
        openFiles({OnlineResolver::reference(item.url, item.title)}, true);
    });
}

bool MainWindow::browseOpen() const { return m_browse && m_browse->isOpen(); }

void MainWindow::showBrowse(bool on)
{
    if (!m_browse || on == browseOpen()) return;
    if (on) {
        if (m_deskActive) toggleDeskMode();   // (the menu is the window's: back from the desk first)
        // The video stops while the menu is up, and plays on when it goes (unless another is chosen).
        m_browseResume = m_player->state() == Player::State::Playing;
        if (m_browseResume) m_player->pause();
        rememberPosition();   // (its tile and its page show how far it was watched)
        m_browse->setNowPlaying(m_player->currentUri().isEmpty() ? QString() : m_mediaTitle);
        m_browse->setGeometry(m_video->geometry());
        m_browse->openScreen();
        m_webBrowse->checkYtDlp(false, [this](const QString& installed, const QString& newest) {
            if (m_browse) m_browse->setYtDlpState(installed, newest);
        });
    } else {
        m_browse->closeScreen();
        m_video->setFocus();
        if (m_browseResume) m_player->play();
        m_browseResume = false;
    }
}

void MainWindow::browseYtDlpDone(bool ok, const QString& message)
{
    if (!m_browse) return;
    m_browse->ytDlpUpdated(ok, message);
    m_webBrowse->checkYtDlp(false, [this](const QString& installed, const QString& newest) {
        if (m_browse) m_browse->setYtDlpState(installed, newest);
    });
}

QJsonObject MainWindow::browseReport() const
{
    if (!m_browse) return {};
    QJsonObject o = m_browse->report();
    o["data"] = m_webBrowse->report();
    QJsonArray later, history;
    for (const WebItem& i : m_webBrowse->watchLater()) later.append(i.title);
    for (const WebItem& i : m_webBrowse->history()) history.append(QJsonObject{{"title", i.title}, {"url", i.url}, {"channel", i.channel},
                                                                                 {"channelId", i.channelId}, {"thumb", i.thumb}});
    o["laterTitles"] = later;
    o["historyItems"] = history;
    return o;
}
