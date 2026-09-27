#include "PlaylistPanel.h"
#include "Icons.h"

#include <QFileInfo>
#include <QUrl>
#include <QHBoxLayout>
#include <QListWidget>
#include <QToolButton>
#include <QVBoxLayout>

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
    h->addWidget(add);
    h->addWidget(rem);
    h->addWidget(clr);
    h->addStretch(1);
    v->addLayout(h);

    connect(add, &QToolButton::clicked, this, &PlaylistPanel::addRequested);
    connect(rem, &QToolButton::clicked, this, [this] {
        const auto sel = m_list->selectedItems();
        for (auto* it : sel) {
            const int row = m_list->row(it);
            if (row == m_current) m_current = -1;
            else if (row < m_current) --m_current;
            delete it;
        }
    });
    connect(clr, &QToolButton::clicked, this, [this] { m_list->clear(); m_current = -1; });
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

int PlaylistPanel::addItems(const QStringList& paths)
{
    const int first = m_list->count();
    for (const QString& p : paths) {
        QString label = QFileInfo(p).fileName().isEmpty() ? p : QFileInfo(p).fileName();
        QString tip = p;
        if (p.startsWith(QLatin1String("jellyfin:"))) {   // "jellyfin:///<id>#<title>"
            label = QStringLiteral("Jellyfin · ") + QUrl::fromPercentEncoding(p.section('#', 1).toUtf8());
            tip = label;
        }
        auto* it = new QListWidgetItem(label);
        it->setData(Qt::UserRole, p);
        it->setToolTip(tip);
        m_list->addItem(it);
    }
    return first;
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
