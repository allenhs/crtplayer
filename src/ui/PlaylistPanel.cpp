#include "PlaylistPanel.h"
#include "Icons.h"

#include <QFileInfo>
#include <QUrl>
#include <QHBoxLayout>
#include <QListWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <algorithm>

PlaylistPanel::PlaylistPanel(QWidget* parent) : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(8, 8, 8, 8);
    m_list = new QListWidget(this);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    v->addWidget(m_list, 1);
    auto* h = new QHBoxLayout();
    auto mk = [this](const char* icon, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setIcon(Icons::get(icon));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    auto* add = mk("add", tr("Add files…"));
    auto* rem = mk("remove", tr("Remove selected (Delete)"));
    auto* clr = mk("clear", tr("Clear playlist"));
    m_clear = clr;
    m_shuffle = mk("shuffle", tr("Shuffle: play in a random order (Ctrl+H)"));
    m_shuffle->setCheckable(true);
    m_repeat = mk("repeat", QString());
    m_repeat->setCheckable(true);
    auto* save = mk("save", tr("Save the playlist as a file (.m3u8)…"));
    h->addWidget(add);
    h->addWidget(rem);
    h->addWidget(clr);
    h->addStretch(1);
    h->addWidget(m_shuffle);
    h->addWidget(m_repeat);
    h->addWidget(save);
    v->addLayout(h);
    setRepeatMode(0);
    connect(m_shuffle, &QToolButton::toggled, this, &PlaylistPanel::shuffleChanged);
    connect(m_repeat, &QToolButton::clicked, this, [this] { setRepeatMode((m_repeatMode + 1) % 3); emit repeatModeChanged(m_repeatMode); });
    connect(save, &QToolButton::clicked, this, &PlaylistPanel::saveRequested);

    connect(add, &QToolButton::clicked, this, &PlaylistPanel::addRequested);
    connect(rem, &QToolButton::clicked, this, [this] {
        const auto sel = m_list->selectedItems();
        for (auto* it : sel) {
            const int row = m_list->row(it);
            if (row == m_current) m_current = -1;
            else if (row < m_current) --m_current;
            delete it;
        }
        emit changed();
    });
    connect(clr, &QToolButton::clicked, this, [this] { m_list->clear(); m_current = -1; emit changed(); });
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) { emit activated(m_list->row(it)); });
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        for (int i = 0; i < m_list->count(); ++i)
            if (m_list->item(i)->data(Qt::UserRole + 1).toBool()) m_current = i;
    });
}

QStringList PlaylistPanel::items() const
{
    QStringList out;
    for (int i = 0; i < m_list->count(); ++i) out << m_list->item(i)->data(Qt::UserRole).toString();
    return out;
}

void PlaylistPanel::setItems(const QStringList& paths)
{
    m_list->clear();
    m_current = -1;
    addItems(paths);
}

// What an entry is called in the list, and its tooltip.
static void entryText(const QString& p, QString* label, QString* tip)
{
    *label = QFileInfo(p).fileName().isEmpty() ? p : QFileInfo(p).fileName();
    *tip = p;
    if (p.startsWith(QLatin1String("jellyfin:"))) {   // "jellyfin:///<id>#<title>"
        *label = QStringLiteral("Jellyfin · ") + QUrl::fromPercentEncoding(p.section('#', 1).toUtf8());
        *tip = *label;
    } else if (p.startsWith(QLatin1String("web:"))) {   // "web:<page>#<title>" (2.17)
        QString page = p.mid(4).section('#', 0, 0);
        page.replace(QStringLiteral("%23"), QStringLiteral("#"));
        const QString title = p.contains('#') ? QUrl::fromPercentEncoding(p.section('#', 1).toUtf8()) : QString();
        const QUrl u(page);
        *label = QStringLiteral("Web · ") + (title.isEmpty() ? u.host() + u.path() : title);
        *tip = page;
    } else if ((p.startsWith(QLatin1String("http://")) || p.startsWith(QLatin1String("https://"))) && QFileInfo(QUrl(p).path()).suffix().isEmpty()) {
        const QUrl u(p);   // (an address that is not a file's: a page, until yt-dlp has said what is on it)
        *label = QStringLiteral("Web · ") + u.host() + u.path();
    }
}

int PlaylistPanel::addItems(const QStringList& paths)
{
    const int first = m_list->count();
    for (const QString& p : paths) {
        QString label, tip;
        entryText(p, &label, &tip);
        auto* it = new QListWidgetItem(label);
        it->setData(Qt::UserRole, p);
        it->setToolTip(tip);
        m_list->addItem(it);
    }
    return first;
}

// One entry gives way to others in its place (a web page that turned out to be a playlist; a page that now
// has its title). The entry that is playing stays the one that is playing.
void PlaylistPanel::replaceAt(int i, const QStringList& paths)
{
    if (i < 0 || i >= m_list->count() || paths.isEmpty()) return;
    delete m_list->takeItem(i);
    int at = i;
    for (const QString& p : paths) {
        QString label, tip;
        entryText(p, &label, &tip);
        auto* it = new QListWidgetItem(label);
        it->setData(Qt::UserRole, p);
        it->setToolTip(tip);
        m_list->insertItem(at++, it);
    }
    if (m_current > i) m_current += int(paths.size()) - 1;
    restyle();   // (not "changed": the list is what it was, with one entry now known by its name)
}

void PlaylistPanel::setShuffle(bool on)
{
    QSignalBlocker b(m_shuffle);
    m_shuffle->setChecked(on);
}

void PlaylistPanel::setRepeatMode(int mode)
{
    m_repeatMode = std::clamp(mode, 0, 2);
    m_repeat->setChecked(m_repeatMode != 0);
    m_repeat->setIcon(Icons::get(m_repeatMode == 2 ? "repeat1" : "repeat"));
    m_repeat->setToolTip(m_repeatMode == 0 ? tr("Repeat: off (Ctrl+R)") : m_repeatMode == 1 ? tr("Repeat: the whole playlist (Ctrl+R)")
                                                                                             : tr("Repeat: this video (Ctrl+R)"));
}

void PlaylistPanel::click(const QString& button)
{
    if (button == "shuffle") m_shuffle->click();
    else if (button == "repeat") m_repeat->click();
    else if (button == "clear") m_clear->click();
}

int PlaylistPanel::count() const { return m_list->count(); }
QString PlaylistPanel::labelAt(int i) const { return (i >= 0 && i < m_list->count()) ? m_list->item(i)->text() : QString(); }
QString PlaylistPanel::at(int i) const { return (i >= 0 && i < m_list->count()) ? m_list->item(i)->data(Qt::UserRole).toString() : QString(); }

void PlaylistPanel::setCurrentIndex(int i)
{
    m_current = i;
    restyle();
}

void PlaylistPanel::restyle()
{
    for (int i = 0; i < m_list->count(); ++i) {
        auto* it = m_list->item(i);
        QFont f = it->font();
        f.setBold(i == m_current);
        it->setFont(f);
        it->setData(Qt::UserRole + 1, i == m_current);
        it->setForeground(i == m_current ? QColor(0xf2, 0xa3, 0x3a) : QColor(0xe8, 0xe4, 0xda));
    }
}
