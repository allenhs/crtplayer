#include "JellyfinPanel.h"
#include "Icons.h"
#include "Theme.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QScrollBar>
#include <QTimer>
#include <QComboBox>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kCardW = 132, kCardH = 150;   // uniform grid for posters (2:3) and thumbnails (16:9)
constexpr int kRole = Qt::UserRole + 10;     // index into m_items
QString ticksToTime(qint64 t)
{
    const qint64 s = t / 10'000'000;
    return s >= 3600 ? QString::asprintf("%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60)
                     : QString::asprintf("%lld:%02lld", s / 60, s % 60);
}
} // namespace

// Items shown in the list are kept in a property on the list, parallel to its rows.
static QVector<JfItem>& itemsOf(QListWidget* list)
{
    static QHash<QListWidget*, QVector<JfItem>> store;
    return store[list];
}

JellyfinPanel::JellyfinPanel(JellyfinClient* client, QWidget* parent) : QWidget(parent), m_client(client)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    m_pages = new QStackedWidget(this);
    v->addWidget(m_pages);

    // --- sign in
    auto* signInPage = new QWidget;
    auto* sv = new QVBoxLayout(signInPage);
    sv->setContentsMargins(14, 14, 14, 14);
    auto* intro = new QLabel(tr("Sign in to your Jellyfin server to browse and stream its videos. "
                                "Videos play in their original format and are decoded by this computer; the "
                                "server converts only what this computer can't play, or what is over the quality limit."));
    intro->setWordWrap(true);
    intro->setObjectName("paramValue");
    sv->addWidget(intro);
    auto* form = new QFormLayout();
    m_server = new QLineEdit;
    m_server->setPlaceholderText("http://192.168.1.20:8096");
    m_user = new QLineEdit;
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Server"), m_server);
    form->addRow(tr("User"), m_user);
    form->addRow(tr("Password"), m_password);
    sv->addLayout(form);
    m_signIn = new QPushButton(tr("Sign in"));
    m_signIn->setDefault(true);
    sv->addWidget(m_signIn);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sv->addWidget(m_status);
    auto* note = new QLabel(tr("Only an access token is stored (never your password), readable only by you. "
                               "Signing out revokes it on the server."));
    note->setWordWrap(true);
    note->setObjectName("paramValue");
    sv->addWidget(note);
    sv->addStretch(1);
    m_pages->addWidget(signInPage);

    // --- browser
    auto* browser = new QWidget;
    auto* bv = new QVBoxLayout(browser);
    bv->setContentsMargins(8, 8, 8, 8);
    auto* bar = new QHBoxLayout();
    m_back = new QToolButton;
    m_back->setIcon(Icons::get("back"));
    m_back->setToolTip(tr("Back"));
    m_back->setAutoRaise(true);
    m_home = new QToolButton;
    m_home->setIcon(Icons::get("home"));
    m_home->setToolTip(tr("Home: continue watching and libraries"));
    m_home->setAutoRaise(true);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search…"));
    m_search->setClearButtonEnabled(true);
    bar->addWidget(m_back);
    bar->addWidget(m_home);
    bar->addWidget(m_search, 1);
    bv->addLayout(bar);
    m_title = new QLabel;
    m_title->setStyleSheet("font-weight: 600; padding: 4px 2px;");
    bv->addWidget(m_title);
    m_list = new QListWidget;
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(kCardW, kCardH));
    m_list->setGridSize(QSize(kCardW + 14, kCardH + 46));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWordWrap(true);
    m_list->setUniformItemSizes(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setSpacing(4);
    bv->addWidget(m_list, 1);
    auto* qrow = new QHBoxLayout();
    auto* ql = new QLabel(tr("Quality"));
    m_quality = new QComboBox;
    m_quality->addItem(tr("Original file"), 0);
    for (int mbps : {40, 20, 10, 8, 4, 2, 1}) m_quality->addItem(tr("Up to %1 Mbit/s (convert larger)").arg(mbps), mbps);
    m_quality->setToolTip(tr("The original file plays as it is when this computer can decode it. With a limit, the server "
                             "converts videos above it: for a slow connection or a server far away."));
    qrow->addWidget(ql);
    qrow->addWidget(m_quality, 1);
    bv->addLayout(qrow);
    auto* foot = new QHBoxLayout();
    m_who = new QLabel;
    m_who->setObjectName("paramValue");
    m_signOut = new QPushButton(tr("Sign out"));
    foot->addWidget(m_who, 1);
    foot->addWidget(m_signOut);
    bv->addLayout(foot);
    m_pages->addWidget(browser);

    connect(m_signIn, &QPushButton::clicked, this, [this] {
        m_status->setText(tr("Connecting…"));
        m_signIn->setEnabled(false);
        m_client->signIn(m_server->text(), m_user->text(), m_password->text());
    });
    connect(m_password, &QLineEdit::returnPressed, m_signIn, &QPushButton::click);
    connect(m_server, &QLineEdit::editingFinished, this, [this] {
        if (!m_server->text().trimmed().isEmpty() && !m_client->isSignedIn()) m_client->probeServer(m_server->text());
    });
    connect(m_client, &JellyfinClient::serverProbed, this, [this](bool ok, const QString& name, const QString& ver, const QString& err) {
        if (m_client->isSignedIn()) return;
        m_status->setText(ok ? tr("Found “%1” (Jellyfin %2)").arg(name, ver) : err);
    });
    connect(m_client, &JellyfinClient::signInFailed, this, [this](const QString& msg) {
        m_signIn->setEnabled(true);
        m_status->setText(msg);
    });
    connect(m_client, &JellyfinClient::signedIn, this, [this] {
        m_signIn->setEnabled(true);
        m_password->clear();   // never kept
        m_status->clear();     // the home screen loads via sessionChanged -> refreshSessionUi
    });
    connect(m_client, &JellyfinClient::sessionChanged, this, &JellyfinPanel::refreshSessionUi);
    connect(m_signOut, &QPushButton::clicked, m_client, &JellyfinClient::signOut);
    connect(m_home, &QToolButton::clicked, this, &JellyfinPanel::showHome);
    connect(m_back, &QToolButton::clicked, this, &JellyfinPanel::goBack);
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        const QString t = m_search->text().trimmed();
        if (t.isEmpty()) { showHome(); return; }
        m_title->setText(tr("Searching “%1”…").arg(t));
        m_client->search(t);
    });
    connect(m_client, &JellyfinClient::searchResults, this, [this](const QString& term, const QVector<JfItem>& items) {
        m_stack = {{QStringLiteral("search:") + term, tr("Results for “%1”").arg(term)}};
        m_pendingParent.clear();
        showItems(m_stack.last().title, items);
    });
    connect(m_client, &JellyfinClient::homeLoaded, this, [this](const QVector<JfItem>& resume, const QVector<JfItem>& libs) {
        m_stack = {{QString(), tr("Home")}};
        m_pendingParent.clear();
        showItems(tr("Home"), libs, resume);
    });
    connect(m_client, &JellyfinClient::itemsLoaded, this,
            [this](const QString& parentId, const QVector<JfItem>& items, int startIndex, int total) {
        if (parentId != m_pendingParent) return;   // a newer navigation happened
        if (startIndex == 0) {
            showItems(m_stack.isEmpty() ? QString() : m_stack.last().title, items);
        } else {
            if (!m_loadingMore || startIndex != m_loaded) return;   // stale page
            appendItems(items);
        }
        m_loadingMore = false;
        m_loaded = startIndex + items.size();
        m_total = std::max(total, m_loaded);
        updateTitle();
        emit listingChanged();
        // A list too short to scroll yet: the next page. (After the view has laid the items
        // out; before that its scroll range still reads empty.)
        QTimer::singleShot(250, this, [this] { maybeLoadMore(); });
    });
    // Near the end of the list: the next page.
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { maybeLoadMore(); });
    connect(m_list->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] { maybeLoadMore(); });
    connect(m_quality, &QComboBox::currentIndexChanged, this, [this] { emit maxBitrateChanged(maxBitrateMbps()); });
    connect(m_client, &JellyfinClient::requestFailed, this, [this](const QString& msg) { m_title->setText(msg); });
    connect(m_client, &JellyfinClient::imageReady, this, [this](const QString& id, const QPixmap& pm) {
        const auto& items = itemsOf(m_list);
        for (int i = 0; i < m_list->count(); ++i) {
            const int idx = m_list->item(i)->data(kRole).toInt();
            if (idx >= 0 && idx < items.size() && items[idx].id == id) m_list->item(i)->setIcon(QIcon(card(pm, items[idx])));
        }
    });
    connect(m_list, &QListWidget::itemActivated, this, &JellyfinPanel::activate);
    connect(m_list, &QListWidget::customContextMenuRequested, this, &JellyfinPanel::contextMenu);
    refreshSessionUi();
}

void JellyfinPanel::refreshSessionUi()
{
    if (m_client->isSignedIn()) {
        m_who->setText(tr("%1 on %2").arg(m_client->userName(), m_client->serverName().isEmpty() ? m_client->serverUrl() : m_client->serverName()));
        if (m_pages->currentIndex() != 1) { m_pages->setCurrentIndex(1); showHome(); }
    } else {
        m_pages->setCurrentIndex(0);
        if (m_server->text().isEmpty()) m_server->setText(m_client->serverUrl());
        if (m_user->text().isEmpty()) m_user->setText(m_client->userName());
        m_list->clear();
    }
}

void JellyfinPanel::showHome()
{
    if (!m_client->isSignedIn()) return;
    m_title->setText(tr("Loading…"));
    m_search->clear();
    m_client->loadHome();
}

QPixmap JellyfinPanel::card(const QPixmap& img, const JfItem& it)
{
    // Uniform card: the image fitted inside, plus a resume bar when partly watched.
    QPixmap pm(kCardW, kCardH);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, kCardW, kCardH), 6, 6);
    p.fillPath(clip, Theme::raised());
    p.setClipPath(clip);
    if (!img.isNull()) {
        const QPixmap s = img.scaled(kCardW, kCardH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawPixmap((kCardW - s.width()) / 2, (kCardH - s.height()) / 2, s);
    }
    if (it.positionTicks > 0 && it.runTimeTicks > 0 && !it.played) {
        const double f = std::clamp(double(it.positionTicks) / it.runTimeTicks, 0.0, 1.0);
        p.fillRect(QRectF(0, kCardH - 5, kCardW, 5), QColor(0, 0, 0, 160));
        p.fillRect(QRectF(0, kCardH - 5, kCardW * f, 5), Theme::accent());
    }
    if (it.played) {
        p.setBrush(Theme::accent());
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(kCardW - 24, 6, 18, 18));
        p.setPen(QPen(QColor(0x1a, 0x14, 0x0a), 2.2));
        p.drawPolyline(QPolygonF({{double(kCardW) - 19.5, 15.0}, {double(kCardW) - 16.0, 18.5}, {double(kCardW) - 10.5, 11.0}}));
    }
    return pm;
}

QIcon JellyfinPanel::placeholderIcon(const JfItem& it) const
{
    QPixmap base(kCardW, kCardH);
    base.fill(Qt::transparent);
    {
        QPainter p(&base);
        const QIcon ic = Icons::get(it.isFolder ? "open" : "crt");
        ic.paint(&p, QRect(kCardW / 2 - 24, kCardH / 2 - 24, 48, 48), Qt::AlignCenter, QIcon::Disabled);
    }
    return QIcon(card(base, it));
}

void JellyfinPanel::showItems(const QString& title, const QVector<JfItem>& items, const QVector<JfItem>& resume)
{
    m_list->clear();
    itemsOf(m_list).clear();
    m_listTitle = title;
    m_loaded = m_total = items.size();   // a paged folder sets these after
    m_loadingMore = false;
    appendItems(resume, QStringLiteral("▶ "));
    appendItems(items);
    m_title->setText(items.isEmpty() && resume.isEmpty() ? tr("%1 — nothing here").arg(title) : title);
    m_back->setEnabled(m_stack.size() > 1);
    emit listingChanged();
}

void JellyfinPanel::appendItems(const QVector<JfItem>& items, const QString& prefix)
{
    auto& store = itemsOf(m_list);
    auto add = [&](const JfItem& it, const QString& prefix) {
        store.push_back(it);
        QString label = it.displayName();
        if (it.type == QLatin1String("Episode") && !it.seriesName.isEmpty() && !prefix.isEmpty()) label = it.seriesName + '\n' + label;
        auto* li = new QListWidgetItem(placeholderIcon(it), prefix + label);
        li->setData(kRole, store.size() - 1);
        QString tip = it.displayName();
        if (it.positionTicks > 0 && !it.played) tip += tr("\nResume at %1").arg(ticksToTime(it.positionTicks));
        if (it.runTimeTicks > 0) tip += tr("\nLength %1").arg(ticksToTime(it.runTimeTicks));
        if (!it.overview.isEmpty()) tip += "\n\n" + it.overview.left(400);
        li->setToolTip(tip);
        m_list->addItem(li);
        m_client->fetchImage(it, 300);
    };
    for (const JfItem& it : items) add(it, prefix);
}

void JellyfinPanel::updateTitle()
{
    if (m_loaded == 0) { m_title->setText(tr("%1 — nothing here").arg(m_listTitle)); return; }
    if (m_total <= JellyfinClient::kPageSize && m_loaded >= m_total) { m_title->setText(m_listTitle); return; }
    const QLocale loc;
    m_title->setText(m_loaded < m_total ? tr("%1 — %2 of %3").arg(m_listTitle, loc.toString(m_loaded), loc.toString(m_total))
                                        : tr("%1 — %2").arg(m_listTitle, loc.toString(m_total)));
}

void JellyfinPanel::maybeLoadMore()
{
    const QScrollBar* sb = m_list->verticalScrollBar();
    // Within about two rows of the end (or no scroll bar at all yet).
    if (sb->maximum() - sb->value() <= 2 * m_list->gridSize().height()) loadMore();
}

bool JellyfinPanel::loadMore()
{
    if (m_loadingMore || m_loaded >= m_total || m_pendingParent.isEmpty() || m_stack.isEmpty() ||
        m_stack.last().parentId != m_pendingParent)
        return false;
    m_loadingMore = true;
    m_title->setText(tr("%1 — loading more…").arg(m_listTitle));
    m_client->loadChildren(m_pendingParent, m_loaded);
    return true;
}

void JellyfinPanel::setMaxBitrateMbps(int mbps, bool notify)
{
    const int i = m_quality->findData(mbps);
    QSignalBlocker b(m_quality);
    m_quality->setCurrentIndex(i >= 0 ? i : 0);
    if (notify) emit maxBitrateChanged(maxBitrateMbps());
}

void JellyfinPanel::scrollToEnd()
{
    m_list->scrollToBottom();
    maybeLoadMore();
}

bool JellyfinPanel::playConvertedByName(const QString& name)
{
    const auto& store = itemsOf(m_list);
    for (const JfItem& it : store)
        if (it.isVideo() && (it.name == name || it.displayName() == name)) { emit convertedPlayRequested(it); return true; }
    return false;
}

int JellyfinPanel::maxBitrateMbps() const { return m_quality->currentData().toInt(); }

void JellyfinPanel::navigateInto(const JfItem& folder)
{
    m_stack.push_back({folder.id, folder.displayName()});
    m_pendingParent = folder.id;
    m_title->setText(tr("Loading %1…").arg(folder.displayName()));
    m_client->loadChildren(folder.id);
}

void JellyfinPanel::goBack()
{
    if (m_stack.size() <= 1) { showHome(); return; }
    m_stack.pop_back();
    const Level l = m_stack.last();
    if (l.parentId.isEmpty()) { showHome(); return; }
    if (l.parentId.startsWith(QStringLiteral("search:"))) { m_client->search(l.parentId.mid(7)); return; }
    m_pendingParent = l.parentId;
    m_client->loadChildren(l.parentId);
}

void JellyfinPanel::activate(QListWidgetItem* li)
{
    const auto& store = itemsOf(m_list);
    const int idx = li->data(kRole).toInt();
    if (idx < 0 || idx >= store.size()) return;
    const JfItem it = store[idx];
    if (it.isVideo()) emit playRequested(it, false);
    else if (it.isFolder) navigateInto(it);
}

void JellyfinPanel::contextMenu(const QPoint& pos)
{
    QListWidgetItem* li = m_list->itemAt(pos);
    if (!li) return;
    const auto& store = itemsOf(m_list);
    const int idx = li->data(kRole).toInt();
    if (idx < 0 || idx >= store.size()) return;
    const JfItem it = store[idx];
    QMenu menu;
    if (it.isVideo()) {
        if (it.positionTicks > 0 && !it.played)
            menu.addAction(tr("Resume at %1").arg(ticksToTime(it.positionTicks)), this, [this, it] { emit playRequested(it, false); });
        menu.addAction(tr("Play from the beginning"), this, [this, it] { emit playRequested(it, true); });
        menu.addAction(tr("Add to playlist"), this, [this, it] { emit enqueueRequested(it); });
        menu.addSeparator();
        menu.addAction(tr("Play converted by the server"), this, [this, it] { emit convertedPlayRequested(it); });
    } else {
        menu.addAction(tr("Open"), this, [this, it] { navigateInto(it); });
    }
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

bool JellyfinPanel::openByName(const QString& name)
{
    const auto& store = itemsOf(m_list);
    for (int i = 0; i < m_list->count(); ++i) {
        const int idx = m_list->item(i)->data(kRole).toInt();
        if (idx >= 0 && idx < store.size() && (store[idx].name == name || store[idx].displayName() == name)) {
            activate(m_list->item(i));
            return true;
        }
    }
    return false;
}

QString JellyfinPanel::currentTitle() const { return m_title->text(); }
int JellyfinPanel::itemCount() const { return m_list->count(); }
