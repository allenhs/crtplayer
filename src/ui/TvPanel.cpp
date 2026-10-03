#include "TvPanel.h"
#include "tv/TvController.h"
#include "ui/ControlBar.h"   // formatTime

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>

TvPanel::TvPanel(QWidget* parent) : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(10, 10, 10, 10);
    auto* intro = new QLabel(tr("Cable TV: each channel plays a folder's videos around the clock. Tune in and it's already "
                                "partway through something; flip channels with Page Up / Page Down or the number keys; "
                                "W shows the guide."));
    intro->setWordWrap(true);
    intro->setObjectName("paramValue");
    v->addWidget(intro);
    m_power = new QPushButton(tr("Turn TV on  (Ctrl+T)"));
    m_power->setCheckable(true);
    v->addWidget(m_power);
    m_now = new QLabel;
    m_now->setWordWrap(true);
    v->addWidget(m_now);

    m_list = new QTreeWidget;
    m_list->setColumnCount(3);
    m_list->setHeaderLabels({tr("Ch"), tr("Name"), tr("Programmes")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_list->setToolTip(tr("Double-click a channel to watch it"));
    v->addWidget(m_list, 1);

    auto* row = new QHBoxLayout();
    auto* add = new QPushButton(tr("Add folder…"));
    add->setToolTip(tr("One channel from a folder (and everything in its subfolders)"));
    auto* addEach = new QPushButton(tr("Add each subfolder…"));
    addEach->setToolTip(tr("One channel for every folder inside the one you pick: e.g. a Shows folder becomes a channel per show"));
    auto* remove = new QPushButton(tr("Remove"));
    row->addWidget(add);
    row->addWidget(addEach);
    row->addWidget(remove);
    v->addLayout(row);

    m_edit = new QWidget;
    auto* form = new QFormLayout(m_edit);
    form->setContentsMargins(0, 4, 0, 0);
    m_name = new QLineEdit;
    m_number = new QSpinBox;
    m_number->setRange(1, 999);
    m_shuffle = new QCheckBox(tr("Shuffled (a new order every time round)"));
    m_shuffle->setToolTip(tr("Off: the videos run in name order, like a series from its first episode to its last, then again."));
    auto* brow = new QHBoxLayout();
    m_bumpers = new QLabel;
    m_bumpers->setObjectName("paramValue");
    auto* pickB = new QPushButton(tr("Choose…"));
    auto* clearB = new QPushButton(tr("None"));
    brow->addWidget(m_bumpers, 1);
    brow->addWidget(pickB);
    brow->addWidget(clearB);
    auto* rescan = new QPushButton(tr("Look for new videos"));
    m_details = new QLabel;
    m_details->setWordWrap(true);
    m_details->setObjectName("paramValue");
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Number"), m_number);
    form->addRow(tr("Order"), m_shuffle);
    form->addRow(tr("Bumpers"), brow);
    form->addRow(QString(), rescan);
    form->addRow(m_details);
    v->addWidget(m_edit);
    auto* jf = new QLabel(tr("Jellyfin: in the Jellyfin panel, right-click a library, a series or a folder and choose "
                             "<i>Add as TV channel</i>. Bumpers are short clips (idents, adverts) played between programmes."));
    jf->setWordWrap(true);
    jf->setObjectName("paramValue");
    v->addWidget(jf);

    auto startDir = [] { const QString d = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation); return d.isEmpty() ? QDir::homePath() : d; };
    connect(m_power, &QPushButton::clicked, this, [this](bool on) { emit tvModeRequested(on); refresh(); });
    connect(add, &QPushButton::clicked, this, [this, startDir] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("A folder of videos for a channel"), startDir());
        if (!d.isEmpty() && m_tv) m_tv->addFolderChannel(d);
    });
    connect(addEach, &QPushButton::clicked, this, [this, startDir] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("A folder whose subfolders each become a channel"), startDir());
        if (d.isEmpty() || !m_tv) return;
        for (const QFileInfo& sub : QDir(d).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) m_tv->addFolderChannel(sub.absoluteFilePath());
    });
    connect(remove, &QPushButton::clicked, this, [this] { if (m_tv && selected() > 0) m_tv->removeChannel(selected()); });
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, &TvPanel::showSelected);
    connect(m_list, &QTreeWidget::itemActivated, this, [this] {
        if (!m_tv || selected() <= 0) return;
        const int n = selected();
        if (!m_tv->isOn()) emit tvModeRequested(true);
        if (m_tv->isOn()) m_tv->tune(n);
    });
    connect(m_name, &QLineEdit::editingFinished, this, [this] { if (!m_updating && m_tv && selected() > 0) m_tv->setName(selected(), m_name->text().trimmed()); });
    connect(m_number, &QSpinBox::editingFinished, this, [this] {
        if (m_updating || !m_tv || selected() <= 0 || m_number->value() == selected()) return;
        const int to = m_number->value();
        m_tv->renumber(selected(), to);
        for (int i = 0; i < m_list->topLevelItemCount(); ++i)
            if (m_list->topLevelItem(i)->data(0, Qt::UserRole).toInt() == to) m_list->setCurrentItem(m_list->topLevelItem(i));
    });
    connect(m_shuffle, &QCheckBox::toggled, this, [this](bool on) { if (!m_updating && m_tv && selected() > 0) m_tv->setShuffle(selected(), on); });
    connect(pickB, &QPushButton::clicked, this, [this, startDir] {
        if (!m_tv || selected() <= 0) return;
        const QString d = QFileDialog::getExistingDirectory(this, tr("A folder of short clips to play between programmes"), startDir());
        if (!d.isEmpty()) m_tv->setBumperFolder(selected(), d);
    });
    connect(clearB, &QPushButton::clicked, this, [this] { if (m_tv && selected() > 0) m_tv->setBumperFolder(selected(), QString()); });
    connect(rescan, &QPushButton::clicked, this, [this] { if (m_tv && selected() > 0) m_tv->rescan(selected()); });
}

void TvPanel::setController(TvController* tv)
{
    m_tv = tv;
    connect(tv, &TvController::channelsChanged, this, &TvPanel::refresh);
    refresh();
}

int TvPanel::selected() const
{
    const auto* it = m_list->currentItem();
    return it ? it->data(0, Qt::UserRole).toInt() : 0;
}

void TvPanel::refresh()
{
    if (!m_tv) return;
    m_updating = true;
    const int keep = selected();
    m_list->clear();
    for (const TvChannel& c : m_tv->channels()) {
        const qint64 cycle = TvSchedule::cycleLengthMs(c.lineup);
        QString progs = !c.ready() ? (c.fetching ? tr("listing…") : tr("reading %1 of %2…").arg(c.files - c.pending).arg(c.files))
                                   : tr("%1 (%2)").arg(c.lineup.programs.size()).arg(formatTime(cycle, true));
        auto* it = new QTreeWidgetItem({QString::number(c.number), c.name, progs});
        it->setData(0, Qt::UserRole, c.number);
        if (!c.problem.isEmpty()) it->setToolTip(1, c.problem);
        if (m_tv->isOn() && m_tv->currentChannel() == c.number) { QFont f = it->font(1); f.setBold(true); it->setFont(0, f); it->setFont(1, f); }
        m_list->addTopLevelItem(it);
        if (c.number == keep) m_list->setCurrentItem(it);
    }
    m_power->setChecked(m_tv->isOn());
    m_power->setText(m_tv->isOn() ? tr("Turn TV off  (Ctrl+T)") : tr("Turn TV on  (Ctrl+T)"));
    m_power->setEnabled(!m_tv->channels().isEmpty());
    const TvSlot s = m_tv->currentSlot();
    const TvChannel* cur = m_tv->channel(m_tv->currentChannel());
    m_now->setText(!m_tv->isOn() ? (m_tv->channels().isEmpty() ? tr("No channels yet. Add a folder of videos to make the first one.") : QString())
                                 : s.valid && cur ? tr("<b>Channel %1 · %2</b><br>%3").arg(cur->number).arg(cur->name.toHtmlEscaped(), s.program.title.toHtmlEscaped())
                                 : QString());
    m_updating = false;
    showSelected();
}

void TvPanel::showSelected()
{
    const TvChannel* c = m_tv ? m_tv->channel(selected()) : nullptr;
    m_edit->setEnabled(c != nullptr);
    if (!c) { m_details->clear(); return; }
    m_updating = true;
    if (!m_name->hasFocus()) m_name->setText(c->name);
    if (!m_number->hasFocus()) m_number->setValue(c->number);
    m_shuffle->setChecked(c->shuffle);
    m_bumpers->setText(c->bumperFolder.isEmpty() ? tr("None") : tr("%1 (%2 clips)").arg(QDir(c->bumperFolder).dirName()).arg(c->lineup.bumpers.size()));
    QStringList from = c->folders;
    for (const TvChannel::Jf& j : c->jellyfin) from << tr("Jellyfin: %1").arg(j.name);
    QString d = tr("From: %1").arg(from.join(QStringLiteral("; ")));
    if (!c->problem.isEmpty()) d += "\n" + c->problem;
    else if (c->ready() && c->lineup.programs.isEmpty()) d += "\n" + tr("No videos found there.");
    m_details->setText(d);
    m_updating = false;
}
