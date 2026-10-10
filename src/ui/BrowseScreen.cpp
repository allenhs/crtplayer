#include "BrowseScreen.h"
#include "Theme.h"

#include <QApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSettings>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kCols = 4, kRows = 3, kPerPage = kCols * kRows;

const QColor kAmber(0xf2, 0xa3, 0x3a);
const QColor kText(0xee, 0xea, 0xe0);
const QColor kMuted(0x8f, 0x95, 0xa1);

QString duration(double s)
{
    if (s <= 0) return QString();
    const qint64 t = qint64(std::llround(s));
    return t >= 3600 ? QString::asprintf("%lld:%02lld:%02lld", t / 3600, (t / 60) % 60, t % 60) : QString::asprintf("%lld:%02lld", t / 60, t % 60);
}

// "1 hour ago", "3 hours ago" (the English plural, without a translation file)
QString count(qint64 n, const char* one, const char* many)
{
    return n == 1 ? QObject::tr(one) : QObject::tr(many).arg(n);
}

QString age(qint64 when)
{
    if (when <= 0) return QString();
    const qint64 d = QDateTime::currentSecsSinceEpoch() - when;
    if (d < 3600) return count(std::max<qint64>(1, d / 60), "1 minute ago", "%1 minutes ago");
    if (d < 86400) return count(d / 3600, "1 hour ago", "%1 hours ago");
    if (d < 86400 * 30) return count(d / 86400, "1 day ago", "%1 days ago");
    if (d < 86400 * 365) return count(d / (86400 * 30), "1 month ago", "%1 months ago");
    return count(d / (86400 * 365), "1 year ago", "%1 years ago");
}

QString views(qint64 v)
{
    if (v < 0) return QString();
    if (v >= 1000000000) return QObject::tr("%1B views").arg(QString::number(v / 1e9, 'f', 1));
    if (v >= 1000000) return QObject::tr("%1M views").arg(QString::number(v / 1e6, 'f', v >= 10000000 ? 0 : 1));
    if (v >= 1000) return QObject::tr("%1K views").arg(QString::number(v / 1e3, 'f', v >= 10000 ? 0 : 1));
    return count(v, "1 view", "%1 views");
}

QRectF scaled(const QRectF& r, double f)
{
    const QPointF c = r.center();
    return QRectF(c.x() - r.width() * f / 2, c.y() - r.height() * f / 2, r.width() * f, r.height() * f);
}

QRectF lerp(const QRectF& a, const QRectF& b, double t)
{
    return QRectF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t, a.width() + (b.width() - a.width()) * t, a.height() + (b.height() - a.height()) * t);
}

// A colour of its own for something without a picture yet (a channel), from its name.
QColor tint(const QString& key)
{
    const uint h = qHash(key);
    return QColor::fromHsv(int(h % 360), 90, 110);
}

void icon(QPainter& p, const QString& name, const QRectF& r, const QColor& c)
{
    p.save();
    p.translate(r.topLeft());
    p.scale(r.width() / 24.0, r.height() / 24.0);
    QPen pen(c, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    if (name == "search") {
        p.drawEllipse(QPointF(10.5, 10.5), 6, 6);
        p.drawLine(QPointF(15, 15), QPointF(20.5, 20.5));
    } else if (name == "tv") {
        p.drawRoundedRect(QRectF(3, 6, 18, 13), 3, 3);
        p.drawLine(QPointF(8.5, 2.5), QPointF(12, 6));
        p.drawLine(QPointF(15.5, 2.5), QPointF(12, 6));
    } else if (name == "plus") {
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
    } else if (name == "import") {
        p.drawLine(QPointF(12, 3.5), QPointF(12, 14));
        p.drawPolyline(QPolygonF({{7.5, 9.5}, {12, 14}, {16.5, 9.5}}));
        p.drawPolyline(QPolygonF({{4, 14}, {4, 20}, {20, 20}, {20, 14}}));
    } else if (name == "play") {
        p.setBrush(c);
        p.drawPolygon(QPolygonF({{7.5, 5}, {19, 12}, {7.5, 19}}));
    } else if (name == "bookmark") {
        p.setBrush(c);
        p.drawPolygon(QPolygonF({{6, 3}, {18, 3}, {18, 21}, {12, 16}, {6, 21}}));
    } else if (name == "chevron-left") {
        p.drawPolyline(QPolygonF({{15, 5}, {8, 12}, {15, 19}}));
    } else if (name == "chevron-right") {
        p.drawPolyline(QPolygonF({{9, 5}, {16, 12}, {9, 19}}));
    } else if (name == "check") {
        p.drawPolyline(QPolygonF({{5, 12.5}, {10, 17.5}, {19, 7}}));
    }
    p.restore();
}

QPixmap scanlines()
{
    static QPixmap pm = [] {
        QPixmap px(4, 3);
        px.fill(Qt::transparent);
        QPainter q(&px);
        q.fillRect(QRect(0, 1, 4, 1), QColor(0, 0, 0, 46));
        return px;
    }();
    return pm;
}
} // namespace

BrowseScreen::BrowseScreen(WebBrowse* data, QWidget* parent) : QWidget(parent), m_data(data)
{
    setObjectName("browseScreen");
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    hide();

    m_input = new QLineEdit(this);
    m_input->setObjectName("browseInput");
    m_input->hide();
    m_input->installEventFilter(this);
    connect(m_input, &QLineEdit::returnPressed, this, &BrowseScreen::finishInput);

    for (QVariantAnimation* a : {&m_lift, &m_slide, &m_detailAnim, &m_fade}) {
        a->setEasingCurve(QEasingCurve::OutCubic);
        connect(a, &QVariantAnimation::valueChanged, this, [this] { update(); });
    }
    m_lift.setDuration(160);
    m_slide.setDuration(260);
    m_detailAnim.setDuration(240);
    m_fade.setDuration(220);
    m_lift.setStartValue(0.0);
    m_lift.setEndValue(1.0);
    connect(&m_detailAnim, &QVariantAnimation::finished, this, [this] {
        if (!m_detailOpen) update();
    });

    m_noteTimer.setSingleShot(true);
    m_noteTimer.setInterval(5000);
    connect(&m_noteTimer, &QTimer::timeout, this, [this] { if (!m_ytUpdating) { m_ytMessage.clear(); update(); } });
    m_clock.setInterval(10000);
    connect(&m_clock, &QTimer::timeout, this, [this] { update(); });
    m_shimmer.setInterval(40);
    connect(&m_shimmer, &QTimer::timeout, this, [this] {
        const bool busy = view().loading;
        if (!busy) m_shimmer.stop();
        update();
    });
    m_shimmerClock.start();

    const QSettings s;
    const QString last = s.value(QStringLiteral("browse/section")).toString();
    if (last == "channels") m_section = Section::Channels;
    else if (last == "later") m_section = Section::Later;
    else if (last == "history") m_section = Section::History;
    else if (last == "search") m_section = Section::Search;
}

QFont BrowseScreen::font(double px, int weight) const
{
    QFont f = QWidget::font();
    f.setPixelSize(std::max(9, int(std::lround(px))));
    f.setWeight(QFont::Weight(weight));
    return f;
}

// ---- opening and closing

void BrowseScreen::openScreen()
{
    if (m_open) return;
    m_open = true;
    m_detailOpen = false;
    show();
    raise();
    relayout();
    setFocus();
    m_fade.stop();
    m_fade.setStartValue(0.0);
    m_fade.setEndValue(1.0);
    m_fade.start();
    m_clock.start();
    enterSection(m_section == Section::Channel ? Section::Channels : m_section);
}

void BrowseScreen::closeScreen()
{
    if (!m_open) return;
    m_open = false;
    m_clock.stop();
    m_shimmer.stop();
    m_input->hide();
    hide();
}

void BrowseScreen::setYtDlpState(const QString& installed, const QString& newest)
{
    m_ytInstalled = installed;
    m_ytNewest = newest;
    update();
}

void BrowseScreen::ytDlpUpdated(bool ok, const QString& message)
{
    m_ytUpdating = false;
    say(message);
    if (ok) load(m_section, true);
    update();
}

// ---- sections

QString BrowseScreen::sectionTitle() const
{
    switch (m_section) {
    case Section::New: return tr("New from your channels");
    case Section::Channels: return tr("Your channels");
    case Section::Later: return tr("Watch later");
    case Section::History: return tr("History");
    case Section::Search: return tr("Search");
    case Section::Channel: return m_channel.title.isEmpty() ? tr("Channel") : m_channel.title;
    }
    return QString();
}

QString BrowseScreen::actionLabel() const
{
    switch (m_section) {
    case Section::New: return tr("Refresh");
    case Section::Channels: return tr("Import from Google Takeout");
    case Section::History: return m_views[int(Section::History)].tiles.isEmpty() ? QString() : tr("Clear history");
    case Section::Channel: return m_data->follows(m_channel.id) ? tr("Following ✓") : tr("Follow");
    case Section::Search: return tr("New search");
    case Section::Later: return QString();
    }
    return QString();
}

void BrowseScreen::runAction()
{
    switch (m_section) {
    case Section::New: load(Section::New, true); break;
    case Section::Channels: importTakeout(); break;
    case Section::History: m_data->clearHistory(); rebuildLocal(Section::History); break;
    case Section::Channel:
        if (m_data->follows(m_channel.id)) m_data->unfollow(m_channel.id);
        else m_data->follow(m_channel);
        break;
    case Section::Search: startInput(false); break;
    case Section::Later: break;
    }
    update();
}

bool BrowseScreen::chooseSection(const QString& name)
{
    static const QHash<QString, Section> names = {{"new", Section::New}, {"channels", Section::Channels}, {"later", Section::Later},
                                                 {"history", Section::History}, {"search", Section::Search}};
    if (!names.contains(name)) return false;
    enterSection(names.value(name));
    return true;
}

void BrowseScreen::enterSection(Section s, bool refresh)
{
    if (s != Section::Channel && m_section == Section::New && s != Section::New) {
        QSettings().setValue(QStringLiteral("browse/feedSeenAt"), QDateTime::currentSecsSinceEpoch());
    }
    m_section = s;
    if (s != Section::Channel) {
        static const char* keys[] = {"new", "channels", "later", "history", "search", ""};
        QSettings().setValue(QStringLiteral("browse/section"), QString::fromLatin1(keys[int(s)]));
        m_tab = int(s);
    }
    m_prevSelected = -1;
    m_inputChannel = false;
    if (s == Section::Search) {
        m_input->setPlaceholderText(tr("Search YouTube, and press Enter"));
        m_input->setText(m_data->lastSearch());
        m_input->show();
    } else {
        m_input->hide();
    }
    load(s, refresh);
    if (s == Section::Search && view().tiles.isEmpty() && !view().loading) startInput(false);
    else if (m_focus == Focus::Input || m_focus == Focus::Detail) setFocusArea(Focus::Grid);
    relayout();
    update();
}

void BrowseScreen::load(Section s, bool refresh)
{
    View& v = m_views[int(s)];
    switch (s) {
    case Section::Channels:
    case Section::Later:
    case Section::History:
        rebuildLocal(s);
        return;
    case Section::New: {
        if (v.tiles.isEmpty()) {
            const WebList kept = m_data->cachedFeed();
            if (!kept.items.isEmpty()) setList(s, kept);
        }
        v.loading = true;
        m_shimmer.start();
        m_data->feed(refresh, [this](const WebList& l) { setList(Section::New, l); });
        return;
    }
    case Section::Search:
        if (v.tiles.isEmpty() && !v.loading) {
            WebList kept = m_data->cachedSearch();
            kept.cached = false;
            if (!kept.items.isEmpty()) setList(s, kept);
            if (m_input) m_input->setText(m_data->lastSearch());
        }
        if (refresh && !m_data->lastSearch().isEmpty()) searchFor(m_data->lastSearch());
        return;
    case Section::Channel: {
        if (!refresh) {
            v = View();
            v.selected = 0;
        }
        v.loading = true;
        m_shimmer.start();
        const QString id = m_channel.id;
        m_data->channelVideos(id, [this, id](const WebList& l) {
            if (m_channel.id != id) return;   // (another channel was opened meanwhile)
            if (!l.channel.title.isEmpty()) m_channel.title = l.channel.title;
            if (!l.channel.avatar.isEmpty()) m_channel.avatar = l.channel.avatar;
            setList(Section::Channel, l);
        });
        return;
    }
    }
}

void BrowseScreen::setList(Section s, const WebList& l)
{
    View& v = m_views[int(s)];
    const QString keep = v.tiles.value(v.selected).item.url;
    v.loading = false;
    v.error = l.error;
    v.detail = l.detail;
    v.cached = l.cached;
    v.fetchedAt = l.fetchedAt;
    v.failedChannels = l.failedChannels;
    if (!l.error.isEmpty() && l.items.isEmpty() && !v.tiles.isEmpty()) { update(); return; }   // (what is there stays)
    v.tiles.clear();
    const qint64 seenAt = QSettings().value(QStringLiteral("browse/feedSeenAt")).toLongLong();
    for (const WebItem& i : l.items) {
        if (s == Section::New && i.shorts) continue;   // (upright videos for phones: not for a TV)
        Tile t;
        t.item = i;
        if (s == Section::New && seenAt > 0 && i.published > seenAt) t.fresh = 1;
        v.tiles.append(t);
    }
    v.selected = 0;
    for (int i = 0; i < v.tiles.size(); ++i)
        if (!keep.isEmpty() && v.tiles[i].item.url == keep) { v.selected = i; break; }
    v.page = v.selected / kPerPage;
    if (s == Section::New) rebuildLocal(Section::Channels);   // (the channels' counts of new videos)
    update();
}

void BrowseScreen::rebuildLocal(Section s)
{
    View& v = m_views[int(s)];
    const int sel = v.selected;
    v.tiles.clear();
    v.loading = false;
    v.error.clear();
    if (s == Section::Channels) {
        // The newest video of each channel (from the feed): its picture behind the channel's, and how many are new.
        QHash<QString, WebItem> newest;
        QHash<QString, int> fresh;
        QHash<QString, qint64> opened;
        for (const WebChannel& c : m_data->channels()) opened.insert(c.id, c.openedAt);
        for (const Tile& t : m_views[int(Section::New)].tiles) {
            const WebItem& i = t.item;
            if (!newest.contains(i.channelId) || newest.value(i.channelId).published < i.published) newest.insert(i.channelId, i);
            if (i.published > opened.value(i.channelId)) fresh[i.channelId]++;
        }
        for (const WebChannel& c : m_data->channels()) {
            Tile t;
            t.kind = TileKind::Channel;
            t.channel = c;
            t.item = newest.value(c.id);
            t.fresh = fresh.value(c.id);
            v.tiles.append(t);
        }
        Tile add;
        add.kind = TileKind::AddChannel;
        v.tiles.append(add);
        Tile imp;
        imp.kind = TileKind::Import;
        v.tiles.append(imp);
    } else {
        const QList<WebItem> items = s == Section::Later ? m_data->watchLater() : m_data->history();
        for (const WebItem& i : items) {
            Tile t;
            t.item = i;
            v.tiles.append(t);
        }
    }
    v.selected = std::clamp(sel, 0, std::max(0, int(v.tiles.size()) - 1));
    v.page = std::min(v.page, std::max(0, pageCount(v) - 1));
    if (v.selected / kPerPage != v.page) v.selected = std::min(int(v.tiles.size()) - 1, v.page * kPerPage);
    v.selected = std::max(0, v.selected);
    update();
}

int BrowseScreen::pageCount(const View& v) const { return std::max(1, int((v.tiles.size() + kPerPage - 1) / kPerPage)); }
int BrowseScreen::pages() const { return pageCount(view()); }

void BrowseScreen::openChannel(const WebChannel& c)
{
    if (c.id.isEmpty()) return;
    if (m_section != Section::Channel) m_beforeChannel = m_section;
    m_channel = c;
    for (const WebChannel& f : m_data->channels())
        if (f.id == c.id) { if (m_channel.title.isEmpty()) m_channel.title = f.title; if (m_channel.avatar.isEmpty()) m_channel.avatar = f.avatar; }
    m_data->markOpened(c.id);
    m_detailOpen = false;
    setFocusArea(Focus::Grid);
    enterSection(Section::Channel);
}

void BrowseScreen::startInput(bool channelLink)
{
    m_inputChannel = channelLink;
    m_input->setPlaceholderText(channelLink ? tr("Paste a channel's link (youtube.com/@name) and press Enter")
                                            : tr("Search YouTube, and press Enter"));
    if (channelLink) m_input->clear();
    else m_input->setText(m_data->lastSearch());
    m_input->selectAll();
    relayout();
    m_input->show();
    setFocusArea(Focus::Input);
}

void BrowseScreen::finishInput()
{
    const QString text = m_input->text().trimmed();
    if (text.isEmpty()) return;
    if (m_inputChannel) {
        m_inputChannel = false;
        m_input->hide();   // (a channel's page now)
        // The channel's page tells its id, name and picture: then it is followed.
        WebChannel c;
        c.id = text;
        m_beforeChannel = Section::Channels;
        m_channel = c;
        m_channel.title = tr("Looking up the channel…");
        m_section = Section::Channel;
        View& v = m_views[int(Section::Channel)];
        v = View();
        v.loading = true;
        m_shimmer.start();
        setFocusArea(Focus::Grid);
        m_data->channelVideos(text, [this, text](const WebList& l) {
            if (m_channel.id != text) return;
            if (l.error.isEmpty() && !l.channel.id.isEmpty()) {
                m_channel = l.channel;
                m_data->follow(l.channel);
                m_data->markOpened(l.channel.id);
            } else {
                m_channel.title = tr("Channel not found");
            }
            setList(Section::Channel, l);
            relayout();
        });
        relayout();
        update();
        return;
    }
    searchFor(text);
}

void BrowseScreen::searchFor(const QString& text)
{
    if (m_section != Section::Search) { m_section = Section::Search; m_tab = int(Section::Search); }
    m_input->setText(text);
    View& v = m_views[int(Section::Search)];
    v.tiles.clear();
    v.page = v.selected = 0;
    v.loading = true;
    v.error.clear();
    m_shimmer.start();
    setFocusArea(Focus::Grid);
    m_data->search(text, [this](const WebList& l) {
        View& sv = m_views[int(Section::Search)];
        sv.tiles.clear();
        setList(Section::Search, l);
    });
    relayout();
    update();
}

void BrowseScreen::importTakeout()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Google Takeout: subscriptions.csv"), QString(), tr("Subscriptions (*.csv);;All files (*)"));
    if (!path.isEmpty()) importFrom(path);
}

int BrowseScreen::importFrom(const QString& path)
{
    QString error;
    const int added = m_data->importTakeout(path, &error);
    say(added < 0 ? error : count(added, "1 channel added", "%1 channels added"));
    rebuildLocal(Section::Channels);
    if (added > 0) m_views[int(Section::New)].tiles.clear();
    update();
    return added;
}

bool BrowseScreen::typeText(const QString& text)
{
    if (!m_input->isVisible()) return false;
    setFocusArea(Focus::Input);
    m_input->setText(text);
    finishInput();
    return true;
}

// ---- a video's own page

QList<QString> BrowseScreen::detailButtons() const
{
    QList<QString> b;
    const qint64 at = m_resume ? m_resume(m_detail.item.url) : 0;
    if (at > 0) {
        b << tr("Resume from %1").arg(duration(at / 1000.0)) << tr("Start over");
    } else {
        b << tr("Play");
    }
    b << (m_data->isWatchLater(m_detail.item.url) ? tr("Remove from Watch later") : tr("Watch later"));
    if (!m_detail.item.channelId.isEmpty()) {
        b << tr("Go to the channel");
        b << (m_data->follows(m_detail.item.channelId) ? tr("Unfollow the channel") : tr("Follow the channel"));
    }
    return b;
}

void BrowseScreen::openDetail(const Tile& t)
{
    m_detail = t;
    m_detailOpen = true;
    m_detailButton = 0;
    const int i = view().selected % kPerPage;
    m_detailFrom = i < m_l.cells.size() ? m_l.cells[i] : QRectF(width() / 2.0, height() / 2.0, 1, 1);
    m_detailAnim.stop();
    m_detailAnim.setStartValue(0.0);
    m_detailAnim.setEndValue(1.0);
    m_detailAnim.start();
    setFocusArea(Focus::Detail);
    relayout();
    want(t.item.thumb);
}

void BrowseScreen::closeDetail()
{
    if (!m_detailOpen) return;
    m_detailOpen = false;
    m_detailAnim.stop();
    m_detailAnim.setStartValue(1.0);
    m_detailAnim.setEndValue(0.0);
    m_detailAnim.start();
    setFocusArea(Focus::Grid);
    if (m_section == Section::Later || m_section == Section::History) rebuildLocal(m_section);
}

void BrowseScreen::pressDetailButton(int i)
{
    const QList<QString> b = detailButtons();
    if (i < 0 || i >= b.size()) return;
    const qint64 at = m_resume ? m_resume(m_detail.item.url) : 0;
    int k = i;
    if (at <= 0 && k >= 1) ++k;   // (no "Start over")
    switch (k) {
    case 0: emit playRequested(m_detail.item, false); break;
    case 1: emit playRequested(m_detail.item, true); break;
    case 2: m_data->setWatchLater(m_detail.item, !m_data->isWatchLater(m_detail.item.url)); break;
    case 3: {
        WebChannel c;
        c.id = m_detail.item.channelId;
        c.title = m_detail.item.channel;
        openChannel(c);
        break;
    }
    case 4: {
        if (m_data->follows(m_detail.item.channelId)) m_data->unfollow(m_detail.item.channelId);
        else {
            WebChannel c;
            c.id = m_detail.item.channelId;
            c.title = m_detail.item.channel;
            m_data->follow(c);
        }
        break;
    }
    }
    relayout();
    update();
}

void BrowseScreen::activate()
{
    const View& v = view();
    if (v.tiles.isEmpty()) {
        if (!v.error.isEmpty()) load(m_section, true);
        else if (m_section == Section::Search) startInput(false);
        return;
    }
    const Tile& t = v.tiles.value(v.selected);
    switch (t.kind) {
    case TileKind::Video: openDetail(t); break;
    case TileKind::Channel: openChannel(t.channel); break;
    case TileKind::AddChannel: startInput(true); break;
    case TileKind::Import: importTakeout(); break;
    case TileKind::Empty: break;
    }
}

// ---- moving about

void BrowseScreen::setFocusArea(Focus f)
{
    m_focus = f;
    if (f == Focus::Input) m_input->setFocus();
    else setFocus();
    update();
}

void BrowseScreen::select(int index, bool animate)
{
    View& v = view();
    if (v.tiles.isEmpty()) return;
    index = std::clamp(index, 0, int(v.tiles.size()) - 1);
    if (index == v.selected && v.page == index / kPerPage) return;
    m_prevSelected = v.selected;
    v.selected = index;
    const int page = index / kPerPage;
    if (page != v.page) goPage(page, page > v.page ? 1 : -1);
    if (animate) {
        m_lift.stop();
        m_lift.start();
    }
    update();
}

void BrowseScreen::goPage(int page, int direction)
{
    View& v = view();
    page = std::clamp(page, 0, pageCount(v) - 1);
    if (page == v.page) return;
    v.page = page;
    if (v.selected / kPerPage != page) v.selected = std::min(int(v.tiles.size()) - 1, page * kPerPage);
    m_slideDir = direction;
    m_slide.stop();
    m_slide.setStartValue(1.0);
    m_slide.setEndValue(0.0);
    m_slide.start();
    m_prevSelected = -1;
    update();
}

void BrowseScreen::moveGrid(int dx, int dy)
{
    View& v = view();
    const int n = int(v.tiles.size());
    if (n == 0) {
        if (dy < 0) setFocusArea(Focus::Tabs);
        else if (dy > 0) setFocusArea(Focus::Tray);
        return;
    }
    const int inPage = v.selected % kPerPage, row = inPage / kCols, col = inPage % kCols;
    if (dx != 0) {
        if (dx < 0 && col == 0) {
            if (v.page > 0) select(std::min(n - 1, (v.page - 1) * kPerPage + row * kCols + kCols - 1));
            return;
        }
        if (dx > 0 && (col == kCols - 1 || v.selected == n - 1)) {
            if (v.page + 1 < pageCount(v)) select(std::min(n - 1, (v.page + 1) * kPerPage + row * kCols));
            return;
        }
        select(v.selected + dx);
        return;
    }
    if (dy < 0) {
        if (row == 0) { setFocusArea(m_section == Section::Search && m_input->isVisible() ? Focus::Input : Focus::Tabs); return; }
        select(v.selected - kCols);
        return;
    }
    if (dy > 0) {
        const int lastOnPage = std::min(n, (v.page + 1) * kPerPage) - 1;
        const int below = v.selected + kCols;
        if (below <= lastOnPage) { select(below); return; }
        // (nothing right below: the last tile, if it is on a lower row; otherwise the tray)
        if (row < (lastOnPage % kPerPage) / kCols) { select(lastOnPage); return; }
        m_trayButton = col < 2 ? 0 : 1;
        setFocusArea(Focus::Tray);
    }
}

QWidget* BrowseScreen::keyTarget() { return m_focus == Focus::Input ? static_cast<QWidget*>(m_input) : this; }

bool BrowseScreen::pressKey(const QString& name)
{
    static const QHash<QString, int> keys = {{"Left", Qt::Key_Left}, {"Right", Qt::Key_Right}, {"Up", Qt::Key_Up}, {"Down", Qt::Key_Down},
                                            {"Return", Qt::Key_Return}, {"Escape", Qt::Key_Escape}, {"Backspace", Qt::Key_Backspace},
                                            {"PageUp", Qt::Key_PageUp}, {"PageDown", Qt::Key_PageDown}, {"Tab", Qt::Key_Tab},
                                            {"W", Qt::Key_W}, {"F", Qt::Key_F}, {"R", Qt::Key_R}, {"U", Qt::Key_U}, {"Slash", Qt::Key_Slash},
                                            {"BracketLeft", Qt::Key_BracketLeft}, {"BracketRight", Qt::Key_BracketRight}, {"Home", Qt::Key_Home}};
    if (!keys.contains(name)) return false;
    QKeyEvent press(QEvent::KeyPress, keys.value(name), Qt::NoModifier);
    QWidget* target = m_focus == Focus::Input ? static_cast<QWidget*>(m_input) : this;
    QCoreApplication::sendEvent(target, &press);
    return true;
}

// The window's own keys (S for a screenshot, L for the playlist, ...) are not theirs while the menu is up: the
// menu takes every key without Ctrl or Alt (Ctrl+B still closes it, Ctrl+L still opens a link).
bool BrowseScreen::event(QEvent* e)
{
    if (e->type() == QEvent::ShortcutOverride) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (!(k->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) && k->key() != Qt::Key_F11) {
            e->accept();
            return true;
        }
    }
    return QWidget::event(e);
}

bool BrowseScreen::eventFilter(QObject* o, QEvent* e)
{
    if (o == m_input && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Down || k->key() == Qt::Key_Escape) {
            if (k->key() == Qt::Key_Escape && m_inputChannel) { m_inputChannel = false; if (m_section != Section::Search) m_input->hide(); }
            if (k->key() == Qt::Key_Escape && view().tiles.isEmpty() && m_section == Section::Search) { setFocusArea(Focus::Tabs); return true; }
            setFocusArea(Focus::Grid);
            return true;
        }
        if (k->key() == Qt::Key_Up) { setFocusArea(Focus::Tabs); return true; }
    }
    if (o == m_input && e->type() == QEvent::FocusIn) { m_focus = Focus::Input; update(); }
    return QWidget::eventFilter(o, e);
}

void BrowseScreen::keyPressEvent(QKeyEvent* e)
{
    const int k = e->key();
    // Anywhere: the sections, searching, back.
    if (k == Qt::Key_BracketLeft || k == Qt::Key_BracketRight || k == Qt::Key_Tab || k == Qt::Key_Backtab) {
        const int dir = (k == Qt::Key_BracketLeft || k == Qt::Key_Backtab) ? -1 : 1;
        if (m_detailOpen) closeDetail();
        const int cur = m_section == Section::Channel ? int(Section::Channels) : int(m_section);
        enterSection(Section((cur + dir + 5) % 5));
        return;
    }
    if (k == Qt::Key_Slash || (k == Qt::Key_F && e->modifiers() & Qt::ControlModifier)) {
        if (m_detailOpen) closeDetail();
        if (m_section != Section::Search) enterSection(Section::Search);
        startInput(false);
        return;
    }
    if (k == Qt::Key_U && !m_ytUpdating) { m_ytUpdating = true; say(tr("Fetching yt-dlp…")); emit updateYtDlpRequested(); update(); return; }

    if (m_focus == Focus::Detail) {
        const int n = int(detailButtons().size());
        if (k == Qt::Key_Escape || k == Qt::Key_Backspace) closeDetail();
        else if (k == Qt::Key_Up || k == Qt::Key_Left) m_detailButton = (m_detailButton + n - 1) % n;
        else if (k == Qt::Key_Down || k == Qt::Key_Right) m_detailButton = (m_detailButton + 1) % n;
        else if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space) pressDetailButton(m_detailButton);
        else if (k == Qt::Key_W) { m_data->setWatchLater(m_detail.item, !m_data->isWatchLater(m_detail.item.url)); }
        update();
        return;
    }
    if (k == Qt::Key_Escape || k == Qt::Key_Backspace) {
        if (m_section == Section::Channel) { enterSection(m_beforeChannel == Section::Channel ? Section::Channels : m_beforeChannel); return; }
        if (m_focus != Focus::Grid) { setFocusArea(Focus::Grid); return; }
        emit closeRequested();
        return;
    }
    if (k == Qt::Key_R) { load(m_section, true); update(); return; }

    if (m_focus == Focus::Tabs) {
        const bool chip = !actionLabel().isEmpty();
        const int last = chip ? 5 : 4;
        if (k == Qt::Key_Left || k == Qt::Key_Right) {
            m_tab = std::clamp(m_tab + (k == Qt::Key_Left ? -1 : 1), 0, last);
            if (m_tab < 5 && Section(m_tab) != m_section) enterSection(Section(m_tab));
            if (m_tab == 5) update();
            setFocusArea(Focus::Tabs);
        } else if (k == Qt::Key_Down) {
            setFocusArea(m_section == Section::Search && m_input->isVisible() ? Focus::Input : Focus::Grid);
        } else if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space) {
            if (m_tab == 5) runAction();
            else setFocusArea(Focus::Grid);
        }
        update();
        return;
    }
    if (m_focus == Focus::Tray) {
        if (k == Qt::Key_Left) m_trayButton = 0;
        else if (k == Qt::Key_Right) m_trayButton = 1;
        else if (k == Qt::Key_Up) setFocusArea(Focus::Grid);
        else if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space) {
            if (m_trayButton == 0) { if (m_section != Section::Search) enterSection(Section::Search); startInput(false); }
            else emit closeRequested();
        }
        update();
        return;
    }
    // the grid
    switch (k) {
    case Qt::Key_Left: moveGrid(-1, 0); break;
    case Qt::Key_Right: moveGrid(1, 0); break;
    case Qt::Key_Up: moveGrid(0, -1); break;
    case Qt::Key_Down: moveGrid(0, 1); break;
    case Qt::Key_PageUp: if (view().page > 0) select(std::max(0, (view().page - 1) * kPerPage)); break;
    case Qt::Key_PageDown: if (view().page + 1 < pages()) select((view().page + 1) * kPerPage); break;
    case Qt::Key_Home: select(0); break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space: activate(); break;
    case Qt::Key_W: {
        const Tile t = view().tiles.value(view().selected);
        if (t.kind == TileKind::Video && t.item.isValid()) {
            const bool on = !m_data->isWatchLater(t.item.url);
            m_data->setWatchLater(t.item, on);
            say(on ? tr("Put on Watch later: %1").arg(t.item.title) : tr("Taken off Watch later: %1").arg(t.item.title));
            if (m_section == Section::Later) rebuildLocal(Section::Later);
        }
        break;
    }
    case Qt::Key_F: {
        const Tile t = view().tiles.value(view().selected);
        WebChannel c = t.kind == TileKind::Channel ? t.channel : WebChannel{t.item.channelId, t.item.channel, QString(), 0, 0};
        if (!c.id.isEmpty()) {
            if (m_data->follows(c.id)) { m_data->unfollow(c.id); say(tr("No longer following %1").arg(c.title)); }
            else { m_data->follow(c); say(tr("Following %1").arg(c.title)); }
            if (m_section == Section::Channels) rebuildLocal(Section::Channels);
        }
        break;
    }
    default: QWidget::keyPressEvent(e); return;
    }
    update();
}

// ---- the mouse

void BrowseScreen::mouseMoveEvent(QMouseEvent* e)
{
    if (m_detailOpen) {
        for (int i = 0; i < m_l.detailButtons.size(); ++i)
            if (m_l.detailButtons[i].contains(e->position())) { if (m_detailButton != i) { m_detailButton = i; update(); } }
        return;
    }
    const View& v = view();
    for (int i = 0; i < m_l.cells.size(); ++i) {
        const int index = v.page * kPerPage + i;
        if (index < v.tiles.size() && m_l.cells[i].contains(e->position())) {
            if (m_focus != Focus::Grid) setFocusArea(Focus::Grid);
            select(index);
            return;
        }
    }
}

void BrowseScreen::mousePressEvent(QMouseEvent* e)
{
    const QPointF at = e->position();
    if (e->button() == Qt::BackButton || e->button() == Qt::RightButton) {
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        keyPressEvent(&esc);
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    if (m_detailOpen) {
        for (int i = 0; i < m_l.detailButtons.size(); ++i)
            if (m_l.detailButtons[i].contains(at)) { m_detailButton = i; pressDetailButton(i); return; }
        if (!m_l.detailTv.contains(at) && !m_l.detailText.contains(at)) closeDetail();
        else if (m_l.detailTv.contains(at)) pressDetailButton(0);
        return;
    }
    for (int i = 0; i < m_l.tabs.size(); ++i)
        if (m_l.tabs[i].contains(at)) { m_tab = i; enterSection(Section(i)); return; }
    if (m_l.action.contains(at) && !actionLabel().isEmpty()) { runAction(); return; }
    if (m_l.trayLeft.contains(at)) { if (m_section != Section::Search) enterSection(Section::Search); startInput(false); return; }
    if (m_l.trayRight.contains(at)) { emit closeRequested(); return; }
    if (m_l.arrowLeft.contains(at) && view().page > 0) { select((view().page - 1) * kPerPage); return; }
    if (m_l.arrowRight.contains(at) && view().page + 1 < pages()) { select((view().page + 1) * kPerPage); return; }
    const View& v = view();
    for (int i = 0; i < m_l.cells.size(); ++i) {
        const int index = v.page * kPerPage + i;
        if (index < v.tiles.size() && m_l.cells[i].contains(at)) {
            select(index);
            setFocusArea(Focus::Grid);
            activate();
            return;
        }
    }
    if (v.tiles.isEmpty() && m_l.grid.contains(at)) activate();
}

void BrowseScreen::wheelEvent(QWheelEvent* e)
{
    if (m_detailOpen) return;
    const int d = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
    if (d < 0 && view().page + 1 < pages()) select((view().page + 1) * kPerPage);
    else if (d > 0 && view().page > 0) select((view().page - 1) * kPerPage);
}

// ---- layout

void BrowseScreen::resizeEvent(QResizeEvent*)
{
    m_background = QPixmap();
    m_scaled.clear();
    relayout();
}

void BrowseScreen::relayout()
{
    Layout& l = m_l;
    const double w = width(), h = height();
    const double s = std::max(0.45, std::min(w / 1600.0, h / 900.0));
    l.s = s;
    const double mx = std::max(24.0 * s, (w - 1440 * s) / 2);
    l.header = QRectF(0, 0, w, 140 * s);
    l.title = QRectF(mx, 22 * s, w - 2 * mx, 46 * s);
    l.tabsRow = QRectF(mx, 80 * s, w - 2 * mx, 36 * s);
    // the tabs
    l.tabs.clear();
    const QFont tf = font(15 * s, 600);
    const QFontMetricsF fm(tf);
    const QStringList names = {tr("New"), tr("Channels"), tr("Watch later"), tr("History"), tr("Search")};
    double x = mx;
    for (const QString& n : names) {
        const double tw = fm.horizontalAdvance(n) + 34 * s;
        l.tabs.append(QRectF(x, l.tabsRow.top(), tw, l.tabsRow.height()));
        x += tw + 10 * s;
    }
    const QString act = actionLabel();
    const double aw = act.isEmpty() ? 0 : QFontMetricsF(font(14 * s, 600)).horizontalAdvance(act) + 34 * s;
    l.action = act.isEmpty() ? QRectF() : QRectF(w - mx - aw, l.tabsRow.top(), aw, l.tabsRow.height());
    // the line to type into (a search; a channel's link): right of the title
    const double iw = std::min(620 * s, w - 2 * mx - 360 * s);
    l.input = QRectF(w - mx - iw, 20 * s, iw, 48 * s);
    if (m_input) {
        m_input->setGeometry(l.input.toAlignedRect());
        QFont f = font(19 * s, 500);
        m_input->setFont(f);
        m_input->setStyleSheet(QStringLiteral("#browseInput { background: rgba(255,255,255,0.07); border: 2px solid %1; border-radius: %2px; padding: 0 %3px; color: #eeeae0; }"
                                              "#browseInput:focus { border-color: #f2a33a; background: rgba(242,163,58,0.08); }")
                                   .arg(m_focus == Focus::Input ? QStringLiteral("#f2a33a") : QStringLiteral("rgba(255,255,255,0.14)"))
                                   .arg(int(24 * s)).arg(int(18 * s)));
    }
    l.banner = QRectF(mx, 124 * s, w - 2 * mx, 20 * s);
    // the tray
    const double trayH = 118 * s;
    l.tray = QRectF(0, h - trayH, w, trayH);
    l.clock = QRectF(w / 2 - 220 * s, l.tray.top() + 12 * s, 440 * s, trayH - 16 * s);
    const double bd = 70 * s;
    l.trayLeft = QRectF(mx + 30 * s, l.tray.center().y() - bd / 2 - 6 * s, bd, bd);
    l.trayRight = QRectF(w - mx - 30 * s - bd, l.trayLeft.top(), bd, bd);
    // the grid
    const double top = 150 * s, bottom = l.tray.top() - 46 * s;
    const double gx = 28 * s, gy = 26 * s;
    const double tw = std::min((1440 * s - 3 * gx) / 4, ((bottom - top) - 2 * gy) / 3 * 16.0 / 9.0);
    const double th = tw * 9 / 16;
    const double gw = 4 * tw + 3 * gx, gh = 3 * th + 2 * gy;
    const double x0 = (w - gw) / 2, y0 = top + ((bottom - top) - gh) / 2;
    l.grid = QRectF(x0, y0, gw, gh);
    l.cells.clear();
    for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kCols; ++c) l.cells.append(QRectF(x0 + c * (tw + gx), y0 + r * (th + gy), tw, th));
    const double ad = 54 * s;
    const double ax = std::max(8 * s, x0 - ad - 26 * s);
    l.arrowLeft = QRectF(ax, l.grid.center().y() - ad / 2, ad, ad);
    l.arrowRight = QRectF(std::min(w - ad - 8 * s, x0 + gw + 26 * s), l.arrowLeft.top(), ad, ad);
    l.dots = QRectF(x0, y0 + gh + 14 * s, gw, 16 * s);
    // a video's page
    const double tvW = std::min(860 * s, w * 0.55), tvH = tvW * 9 / 16;
    const double ty = top + std::max(0.0, ((bottom + 30 * s - top) - tvH) / 2);
    l.detailTv = QRectF(mx + 10 * s, ty, tvW, tvH);
    l.detailText = QRectF(l.detailTv.right() + 48 * s, ty, w - mx - (l.detailTv.right() + 48 * s), tvH);
    l.detailButtons.clear();
    const int nb = m_detailOpen ? int(detailButtons().size()) : 0;
    const double bh = 50 * s, bgap = 11 * s;
    double by = l.detailText.bottom() - nb * bh - (nb - 1) * bgap;
    for (int i = 0; i < nb; ++i) {
        l.detailButtons.append(QRectF(l.detailText.left(), by, std::min(l.detailText.width(), 380 * s), bh));
        by += bh + bgap;
    }
}

// ---- pictures

void BrowseScreen::want(const QString& url)
{
    if (url.isEmpty() || m_asked.contains(url)) return;
    m_asked.insert(url, true);
    QPointer<BrowseScreen> self(this);
    m_data->thumbnail(url, [self, url](const QImage& img) {
        if (!self) return;
        self->m_images.insert(url, img);
        self->update();
    });
}

QPixmap BrowseScreen::thumb(const QString& url, const QSize& size)
{
    if (url.isEmpty()) return QPixmap();
    const QString key = url + QLatin1Char('|') + QString::number(size.width()) + QLatin1Char('x') + QString::number(size.height());
    auto it = m_scaled.constFind(key);
    if (it != m_scaled.constEnd()) return *it;
    const QImage img = m_images.value(url);
    if (img.isNull()) { want(url); return QPixmap(); }
    // Filled, not fitted (cut at the sides or above and below), as a TV fills its screen.
    const QImage fill = img.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QImage cut = fill.copy((fill.width() - size.width()) / 2, (fill.height() - size.height()) / 2, size.width(), size.height());
    const QPixmap pm = QPixmap::fromImage(cut);
    if (m_scaled.size() > 400) m_scaled.clear();
    m_scaled.insert(key, pm);
    return pm;
}

QPixmap BrowseScreen::avatar(const QString& url, int size)
{
    if (url.isEmpty()) return QPixmap();
    const QString key = QStringLiteral("avatar|") + url + QLatin1Char('|') + QString::number(size);
    auto it = m_scaled.constFind(key);
    if (it != m_scaled.constEnd()) return *it;
    const QImage img = m_images.value(url);
    if (img.isNull()) { want(url); return QPixmap(); }
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath circle;
    circle.addEllipse(QRectF(0, 0, size, size));
    p.setClipPath(circle);
    p.drawImage(QRectF(0, 0, size, size), img.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    p.end();
    m_scaled.insert(key, pm);
    return pm;
}

// ---- painting

void BrowseScreen::paintBackground(QPainter& p)
{
    if (m_background.size() != size()) {
        m_background = QPixmap(size());
        QPainter b(&m_background);
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, QColor(0x1c, 0x1f, 0x27));
        g.setColorAt(0.55, QColor(0x14, 0x16, 0x1c));
        g.setColorAt(1, QColor(0x0c, 0x0d, 0x11));
        b.fillRect(rect(), g);
        // a warm light from above, as off a lamp over the TV
        QRadialGradient glow(QPointF(width() * 0.5, -height() * 0.15), width() * 0.65);
        glow.setColorAt(0, QColor(242, 163, 58, 30));
        glow.setColorAt(1, QColor(242, 163, 58, 0));
        b.fillRect(rect(), glow);
        // the faintest lines, as on a tube
        for (int y = 0; y < height(); y += 3) b.fillRect(QRect(0, y, width(), 1), QColor(0, 0, 0, 16));
        // darker toward the corners
        QRadialGradient vig(rect().center(), std::hypot(width(), height()) * 0.6);
        vig.setColorAt(0.6, QColor(0, 0, 0, 0));
        vig.setColorAt(1, QColor(0, 0, 0, 120));
        b.fillRect(rect(), vig);
    }
    p.drawPixmap(0, 0, m_background);
}

void BrowseScreen::say(const QString& text)
{
    m_ytMessage = text;
    m_noteTimer.start();
    update();
}

void BrowseScreen::paintHeader(QPainter& p)
{
    const double s = m_l.s;
    // the title (a channel's: with its picture)
    double tx = m_l.title.left();
    if (m_section == Section::Channel) {
        const int ad = int(44 * s);
        const QPixmap av = avatar(m_channel.avatar, ad);
        const QRectF ar(tx, m_l.title.center().y() - ad / 2.0, ad, ad);
        if (!av.isNull()) p.drawPixmap(ar.topLeft(), av);
        else {
            p.setPen(Qt::NoPen);
            p.setBrush(tint(m_channel.id));
            p.drawEllipse(ar);
        }
        tx += ad + 14 * s;
    }
    p.setFont(font(30 * s, 600));
    p.setPen(kText);
    const double titleRight = m_input->isVisible() ? m_l.input.left() - 20 * s : m_l.title.right();
    p.drawText(QRectF(tx, m_l.title.top(), titleRight - tx, m_l.title.height()), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetricsF(font(30 * s, 600)).elidedText(sectionTitle(), Qt::ElideRight, titleRight - tx));

    // the tabs
    const int current = m_section == Section::Channel ? int(Section::Channels) : int(m_section);
    const QStringList names = {tr("New"), tr("Channels"), tr("Watch later"), tr("History"), tr("Search")};
    p.setFont(font(15 * s, 600));
    for (int i = 0; i < m_l.tabs.size(); ++i) {
        const QRectF r = m_l.tabs[i];
        const bool on = i == current, focused = m_focus == Focus::Tabs && m_tab == i;
        p.setPen(focused ? QPen(kAmber, 2 * s) : QPen(on ? QColor(255, 255, 255, 40) : QColor(255, 255, 255, 18), 1));
        p.setBrush(on ? QColor(242, 163, 58, 46) : QColor(255, 255, 255, 10));
        p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
        p.setPen(on ? QColor(0xff, 0xd9, 0xa1) : kMuted);
        p.drawText(r, Qt::AlignCenter, names[i]);
        if (i == int(Section::New)) {   // (how many are new)
            int fresh = 0;
            for (const Tile& t : m_views[int(Section::New)].tiles) fresh += t.fresh;
            if (fresh > 0) {
                const QString n = QString::number(fresh);
                const QRectF b(r.right() - 10 * s, r.top() - 7 * s, std::max(20 * s, QFontMetricsF(font(11 * s, 700)).horizontalAdvance(n) + 10 * s), 18 * s);
                p.setPen(Qt::NoPen);
                p.setBrush(kAmber);
                p.drawRoundedRect(b, 9 * s, 9 * s);
                p.setFont(font(11 * s, 700));
                p.setPen(QColor(0x1a, 0x14, 0x0a));
                p.drawText(b, Qt::AlignCenter, n);
                p.setFont(font(15 * s, 600));
            }
        }
    }
    // the action beside them
    const QString act = actionLabel();
    if (!act.isEmpty()) {
        const QRectF r = m_l.action;
        const bool focused = m_focus == Focus::Tabs && m_tab == 5;
        p.setPen(focused ? QPen(kAmber, 2 * s) : QPen(QColor(255, 255, 255, 30), 1));
        p.setBrush(QColor(255, 255, 255, focused ? 22 : 8));
        p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
        p.setFont(font(14 * s, 600));
        p.setPen(focused ? kText : kMuted);
        p.drawText(r, Qt::AlignCenter, act);
    }
    // a line about the list: kept from before, partly read, or what was just done
    const View& v = view();
    QString note;
    QColor noteColor = kMuted;
    if (!v.error.isEmpty() && !v.tiles.isEmpty()) {
        note = tr("YouTube did not answer (%1). Shown: the list from %2. R tries again%3.")
                   .arg(v.error, QLocale().toString(v.fetchedAt.time(), QLocale::ShortFormat),
                        m_ytNewest.isEmpty() || m_ytNewest == m_ytInstalled ? QString() : tr(", U fetches the newer yt-dlp"));
        noteColor = kAmber;
    } else if (v.failedChannels > 0) {
        note = count(v.failedChannels, "1 channel could not be read just now.", "%1 channels could not be read just now.");
    } else if (!m_ytMessage.isEmpty()) {
        note = m_ytMessage;
    } else if (v.loading && !v.tiles.isEmpty()) {
        note = tr("Looking for anything newer…");
    }
    if (!note.isEmpty()) {
        p.setFont(font(13 * s, 500));
        p.setPen(noteColor);
        p.drawText(m_l.banner, Qt::AlignLeft | Qt::AlignVCenter, QFontMetricsF(font(13 * s, 500)).elidedText(note, Qt::ElideRight, m_l.banner.width()));
    }
}

void BrowseScreen::paintScreenGlass(QPainter& p, const QPainterPath& screen, const QRectF& r)
{
    p.save();
    p.setClipPath(screen, Qt::IntersectClip);
    p.fillRect(r, QBrush(scanlines()));
    // the curved glass catching the light, top left
    QLinearGradient g(r.topLeft(), QPointF(r.left() + r.width() * 0.45, r.top() + r.height() * 0.6));
    g.setColorAt(0, QColor(255, 255, 255, 46));
    g.setColorAt(0.5, QColor(255, 255, 255, 10));
    g.setColorAt(0.51, QColor(255, 255, 255, 0));
    p.fillRect(r, g);
    // and darker at the edges, as a tube is
    QRadialGradient edge(r.center(), std::hypot(r.width(), r.height()) * 0.58);
    edge.setColorAt(0.7, QColor(0, 0, 0, 0));
    edge.setColorAt(1, QColor(0, 0, 0, 110));
    p.fillRect(r, edge);
    p.restore();
}

void BrowseScreen::paintTile(QPainter& p, const Tile& t, const QRectF& cell, double lift, bool focused, int index)
{
    const double s = m_l.s;
    const QRectF r = scaled(cell, 1.0 + 0.075 * lift);
    const double radius = 20 * s * (1.0 + 0.075 * lift), bezel = 6 * s;
    QPainterPath outer;
    outer.addRoundedRect(r, radius, radius);
    // the shadow it casts, deeper when it is lifted
    p.setPen(Qt::NoPen);
    for (int i = 4; i >= 1; --i) {
        const double grow = i * (3 + 3 * lift) * s;
        p.setBrush(QColor(0, 0, 0, int(22 + 8 * lift)));
        p.drawRoundedRect(r.adjusted(-grow * 0.4, -grow * 0.1 + (6 + 6 * lift) * s, grow * 0.4, grow * 0.5 + (6 + 6 * lift) * s), radius + grow * 0.4, radius + grow * 0.4);
    }
    if (t.kind == TileKind::Empty) {
        p.setBrush(QColor(255, 255, 255, 6));
        p.setPen(QPen(QColor(255, 255, 255, 14), 1.2 * s));
        p.drawRoundedRect(r, radius, radius);
        return;
    }
    // the glow of the one chosen
    if (focused && lift > 0.01) {
        for (int i = 6; i >= 1; --i) {
            p.setPen(QPen(QColor(242, 163, 58, int(lift * 120 / (i + 1))), i * 3 * s));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, radius, radius);
        }
    }
    // the bezel
    QLinearGradient bz(r.topLeft(), r.bottomLeft());
    bz.setColorAt(0, QColor(0x3a, 0x3f, 0x4b));
    bz.setColorAt(1, QColor(0x1c, 0x1f, 0x26));
    p.setPen(Qt::NoPen);
    p.setBrush(bz);
    p.drawPath(outer);
    p.setPen(QPen(QColor(255, 255, 255, 26), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    // the screen
    const QRectF sr = r.adjusted(bezel, bezel, -bezel, -bezel);
    QPainterPath screen;
    screen.addRoundedRect(sr, radius - bezel * 0.8, radius - bezel * 0.8);
    p.save();
    p.setClipPath(screen);
    p.fillRect(sr, QColor(0x0b, 0x0c, 0x0f));
    const QSize px = sr.size().toSize();
    if (t.kind == TileKind::Video) {
        const QPixmap pm = thumb(t.item.thumb, px);
        if (!pm.isNull()) p.drawPixmap(sr, pm, QRectF(0, 0, pm.width(), pm.height()));
        else {
            QLinearGradient g(sr.topLeft(), sr.bottomRight());
            g.setColorAt(0, tint(t.item.channel).darker(160));
            g.setColorAt(1, QColor(0x10, 0x11, 0x15));
            p.fillRect(sr, g);
        }
        // what it is, over the bottom of the picture
        QLinearGradient cap(sr.left(), sr.top() + sr.height() * 0.38, sr.left(), sr.bottom());
        cap.setColorAt(0, QColor(0, 0, 0, 0));
        cap.setColorAt(0.45, QColor(0, 0, 0, 150));
        cap.setColorAt(1, QColor(0, 0, 0, 225));
        p.fillRect(sr, cap);
        const double pad = 12 * s;
        const QFont tf = font(15.5 * s, 600), mf = font(12.5 * s, 500);
        const QFontMetricsF tfm(tf), mfm(mf);
        QStringList meta;
        if (!t.item.channel.isEmpty() && m_section != Section::Channel) meta << t.item.channel;
        if (t.item.live) meta << tr("LIVE");
        else if (t.item.seconds > 0) meta << duration(t.item.seconds);
        if (t.item.published > 0) meta << age(t.item.published);
        else if (t.item.views >= 0) meta << views(t.item.views);
        const QString metaText = mfm.elidedText(meta.join(QStringLiteral(" · ")), Qt::ElideRight, sr.width() - 2 * pad);
        const QRectF mr(sr.left() + pad, sr.bottom() - pad - mfm.height(), sr.width() - 2 * pad, mfm.height());
        p.setFont(mf);
        p.setPen(QColor(0xc8, 0xc4, 0xbb));
        p.drawText(mr, Qt::AlignLeft | Qt::AlignVCenter, metaText);
        // the title: two lines at most
        const double lineH = tfm.height();
        QString title = t.item.title, first, second;
        const double tw = sr.width() - 2 * pad;
        if (tfm.horizontalAdvance(title) <= tw) first = title;
        else {
            int cut = title.size();
            while (cut > 0 && tfm.horizontalAdvance(title.left(cut)) > tw) cut = title.lastIndexOf(QLatin1Char(' '), cut - 1);
            if (cut <= 0) cut = int(title.size() / 2);
            first = title.left(cut);
            second = tfm.elidedText(title.mid(cut).trimmed(), Qt::ElideRight, tw);
            first = tfm.elidedText(first, Qt::ElideRight, tw);
        }
        p.setFont(tf);
        p.setPen(kText);
        double ty = mr.top() - 4 * s - lineH * (second.isEmpty() ? 1 : 2);
        p.drawText(QRectF(sr.left() + pad, ty, tw, lineH), Qt::AlignLeft | Qt::AlignVCenter, first);
        if (!second.isEmpty()) p.drawText(QRectF(sr.left() + pad, ty + lineH, tw, lineH), Qt::AlignLeft | Qt::AlignVCenter, second);
        // how far it was watched
        const qint64 at = m_resume ? m_resume(t.item.url) : 0;
        if (at > 0 && t.item.seconds > 0) {
            const double f = std::clamp(at / 1000.0 / t.item.seconds, 0.02, 1.0);
            p.fillRect(QRectF(sr.left(), sr.bottom() - 4 * s, sr.width(), 4 * s), QColor(255, 255, 255, 50));
            p.fillRect(QRectF(sr.left(), sr.bottom() - 4 * s, sr.width() * f, 4 * s), kAmber);
        }
    } else if (t.kind == TileKind::Channel) {
        // its newest video, dim and soft, behind its picture and name
        const QPixmap pm = thumb(t.item.thumb, QSize(std::max(8, px.width() / 10), std::max(5, px.height() / 10)));
        if (!pm.isNull()) {
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawPixmap(sr, pm, QRectF(0, 0, pm.width(), pm.height()));
            p.fillRect(sr, QColor(10, 11, 14, 130));
        } else {
            QLinearGradient g(sr.topLeft(), sr.bottomRight());
            g.setColorAt(0, tint(t.channel.id).darker(150));
            g.setColorAt(1, QColor(0x12, 0x13, 0x17));
            p.fillRect(sr, g);
        }
        const int ad = int(sr.height() * 0.46);
        const QRectF ar(sr.center().x() - ad / 2.0, sr.top() + sr.height() * 0.13, ad, ad);
        const QPixmap av = avatar(t.channel.avatar, ad);
        p.setPen(QPen(QColor(255, 255, 255, 60), 2 * s));
        if (!av.isNull()) { p.drawPixmap(ar.topLeft(), av); p.setBrush(Qt::NoBrush); p.drawEllipse(ar); }
        else {
            p.setBrush(tint(t.channel.id));
            p.drawEllipse(ar);
            p.setFont(font(ad * 0.42, 700));
            p.setPen(QColor(255, 255, 255, 220));
            p.drawText(ar, Qt::AlignCenter, t.channel.title.left(1).toUpper());
        }
        p.setFont(font(16 * s, 600));
        p.setPen(kText);
        const QRectF nr(sr.left() + 10 * s, ar.bottom() + 8 * s, sr.width() - 20 * s, sr.bottom() - ar.bottom() - 14 * s);
        p.drawText(nr, Qt::AlignHCenter | Qt::AlignVCenter, QFontMetricsF(font(16 * s, 600)).elidedText(t.channel.title, Qt::ElideRight, nr.width()));
    } else {   // adding channels
        QLinearGradient g(sr.topLeft(), sr.bottomLeft());
        g.setColorAt(0, QColor(0x22, 0x25, 0x2d));
        g.setColorAt(1, QColor(0x16, 0x18, 0x1d));
        p.fillRect(sr, g);
        const double id = sr.height() * 0.3;
        icon(p, t.kind == TileKind::AddChannel ? "plus" : "import", QRectF(sr.center().x() - id / 2, sr.top() + sr.height() * 0.2, id, id), focused ? kAmber : kMuted);
        p.setFont(font(15 * s, 600));
        p.setPen(focused ? kText : kMuted);
        const QString label = t.kind == TileKind::AddChannel ? tr("Add a channel\n(its link)") : tr("Import your subscriptions\n(Google Takeout)");
        p.drawText(QRectF(sr.left(), sr.top() + sr.height() * 0.56, sr.width(), sr.height() * 0.4), Qt::AlignHCenter | Qt::AlignTop, label);
    }
    p.restore();
    paintScreenGlass(p, screen, sr);
    // marks: new, put aside
    double bx = sr.left() + 10 * s;
    auto pill = [&](const QString& text, const QColor& bg, const QColor& fg) {
        const QFont bf = font(11.5 * s, 700);
        const double bw = QFontMetricsF(bf).horizontalAdvance(text) + 14 * s;
        const QRectF b(bx, sr.top() + 10 * s, bw, 20 * s);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(b, 10 * s, 10 * s);
        p.setFont(bf);
        p.setPen(fg);
        p.drawText(b, Qt::AlignCenter, text);
        bx += bw + 6 * s;
    };
    if (t.kind == TileKind::Video && t.fresh) pill(tr("NEW"), kAmber, QColor(0x1a, 0x14, 0x0a));
    if (t.kind == TileKind::Channel && t.fresh > 0) pill(tr("%1 new").arg(t.fresh), kAmber, QColor(0x1a, 0x14, 0x0a));
    if (t.kind == TileKind::Video && m_section != Section::Later && m_data->isWatchLater(t.item.url))
        icon(p, "bookmark", QRectF(sr.right() - 30 * s, sr.top() - 1 * s, 20 * s, 26 * s), kAmber);
    // the frame of the one chosen
    if (focused) {
        p.setPen(QPen(QColor(242, 163, 58, int(140 + 115 * lift)), 2.4 * s));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), radius, radius);
    }
    Q_UNUSED(index);
}

void BrowseScreen::paintMessage(QPainter& p)
{
    const double s = m_l.s;
    const View& v = view();
    QString head, body, hint;
    if (v.loading) return;
    if (!v.error.isEmpty()) {
        head = m_section == Section::Search ? tr("The search did not come back")
             : m_section == Section::Channel ? tr("The channel's page could not be read")
             : m_section == Section::New ? tr("The channels could not be read")
             : tr("This could not be read");
        body = tr("yt-dlp says: %1").arg(v.error);
        hint = tr("Enter tries again.");
        if (!m_ytNewest.isEmpty() && !m_ytInstalled.isEmpty() && m_ytNewest > m_ytInstalled)
            hint += QLatin1Char(' ') + tr("A newer yt-dlp is out (%1, this one is %2): U fetches it.").arg(m_ytNewest, m_ytInstalled);
        else if (m_ytInstalled.isEmpty())
            hint += QLatin1Char(' ') + tr("U fetches yt-dlp.");
        else
            hint += QLatin1Char(' ') + tr("U fetches yt-dlp again (YouTube changes, and yt-dlp follows within days).");
    } else if (v.tiles.isEmpty()) {
        switch (m_section) {
        case Section::New:
            head = tr("Follow a few channels, and their new videos appear here");
            body = tr("A video's page has \"Follow the channel\" (or press F on a video). Under Channels you can paste a channel's link, "
                      "or bring in your YouTube subscriptions from Google Takeout. Nothing is signed in to: the list stays on this computer.");
            hint = tr("Channels is next: press ] (on a controller, the right shoulder button).");
            break;
        case Section::Later:
            head = tr("Nothing put aside yet");
            body = tr("Press W on a video, or choose \"Watch later\" on its page.");
            break;
        case Section::History:
            head = tr("Nothing watched yet");
            body = tr("The videos you play from here or from a link appear here.");
            break;
        case Section::Search:
            head = tr("What would you like to watch?");
            body = tr("Type it above, and press Enter.");
            break;
        case Section::Channel:
            head = tr("No videos on this channel");
            break;
        case Section::Channels: break;
        }
    } else {
        return;
    }
    const QRectF card = QRectF(m_l.grid.center().x() - 380 * s, m_l.grid.center().y() - 120 * s, 760 * s, 240 * s);
    p.setPen(QPen(QColor(255, 255, 255, 24), 1));
    p.setBrush(QColor(20, 22, 28, 220));
    p.drawRoundedRect(card, 22 * s, 22 * s);
    const QRectF in = card.adjusted(34 * s, 28 * s, -34 * s, -24 * s);
    p.setFont(font(21 * s, 600));
    p.setPen(v.error.isEmpty() ? kText : kAmber);
    QRectF used;
    p.drawText(in, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, head, &used);
    p.setFont(font(14.5 * s, 400));
    p.setPen(kMuted);
    QRectF bodyR(in.left(), used.bottom() + 12 * s, in.width(), in.bottom() - used.bottom() - 40 * s);
    p.drawText(bodyR, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, body);
    if (!hint.isEmpty()) {
        p.setFont(font(13.5 * s, 600));
        p.setPen(QColor(0xcf, 0xca, 0xbf));
        p.drawText(QRectF(in.left(), in.bottom() - 24 * s, in.width(), 24 * s), Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap, hint);
    }
}

void BrowseScreen::paintGrid(QPainter& p)
{
    const double s = m_l.s;
    const View& v = view();
    const double slide = m_slide.state() == QAbstractAnimation::Running ? m_slide.currentValue().toDouble() : 0.0;
    const double shift = slide * m_slideDir * m_l.grid.width() * 0.35;
    const double lift = m_lift.state() == QAbstractAnimation::Running ? m_lift.currentValue().toDouble() : 1.0;
    p.save();
    if (slide > 0) p.setOpacity(1.0 - slide * 0.85);
    p.translate(shift, 0);
    const int first = v.page * kPerPage;
    const int selInPage = v.selected - first;
    // the chosen one last, over its neighbours
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < kPerPage; ++i) {
            const int index = first + i;
            const bool chosen = index == v.selected;
            if ((pass == 1) != chosen) continue;
            Tile t;
            if (index < v.tiles.size()) t = v.tiles[index];
            else if (v.loading && v.tiles.isEmpty()) t.kind = TileKind::Video;   // (a place being filled)
            else t.kind = TileKind::Empty;
            const bool gridFocus = m_focus == Focus::Grid && !m_detailOpen;
            double l = 0;
            if (chosen && gridFocus) l = lift;
            else if (index == m_prevSelected && gridFocus) l = 1.0 - lift;
            if (v.loading && v.tiles.isEmpty() && index >= v.tiles.size()) {
                // waiting: the places shimmer
                const QRectF r = m_l.cells[i];
                QPainterPath path;
                path.addRoundedRect(r, 20 * s, 20 * s);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0x22, 0x25, 0x2c));
                p.drawPath(path);
                const double ph = std::fmod(m_shimmerClock.elapsed() / 1400.0 + i * 0.07, 1.0);
                QLinearGradient g(r.left() + (ph * 2 - 0.5) * r.width(), 0, r.left() + (ph * 2) * r.width(), 0);
                g.setColorAt(0, QColor(255, 255, 255, 0));
                g.setColorAt(0.5, QColor(255, 255, 255, 18));
                g.setColorAt(1, QColor(255, 255, 255, 0));
                p.setBrush(g);
                p.drawPath(path);
                continue;
            }
            paintTile(p, t, m_l.cells[i], l, chosen && gridFocus, index);
        }
    Q_UNUSED(selInPage);
    p.restore();
    paintMessage(p);
    // more pages: arrows and dots
    const int n = pages();
    if (n > 1) {
        for (int side = 0; side < 2; ++side) {
            const bool can = side == 0 ? v.page > 0 : v.page + 1 < n;
            if (!can) continue;
            const QRectF r = side == 0 ? m_l.arrowLeft : m_l.arrowRight;
            p.setPen(QPen(QColor(255, 255, 255, 40), 1.5 * s));
            p.setBrush(QColor(30, 33, 40, 230));
            p.drawEllipse(r);
            icon(p, side == 0 ? "chevron-left" : "chevron-right", r.adjusted(r.width() * 0.22, r.height() * 0.22, -r.width() * 0.22, -r.height() * 0.22), kText);
        }
        const double d = 9 * s, gap = 9 * s;
        const int shown = std::min(n, 24);
        double x = m_l.dots.center().x() - (shown * d + (shown - 1) * gap) / 2;
        for (int i = 0; i < shown; ++i) {
            p.setPen(Qt::NoPen);
            p.setBrush(i == v.page ? kAmber : QColor(255, 255, 255, 50));
            p.drawEllipse(QRectF(x, m_l.dots.center().y() - d / 2, d, d));
            x += d + gap;
        }
    }
}

void BrowseScreen::paintTray(QPainter& p)
{
    const double s = m_l.s;
    const QRectF t = m_l.tray;
    // a band across the bottom, its edge rising gently to hold the clock
    QPainterPath band;
    const double edge = t.top() + 18 * s, hump = t.top();
    band.moveTo(0, edge);
    band.lineTo(t.center().x() - 330 * s, edge);
    band.cubicTo(t.center().x() - 230 * s, edge, t.center().x() - 230 * s, hump, t.center().x() - 140 * s, hump);
    band.lineTo(t.center().x() + 140 * s, hump);
    band.cubicTo(t.center().x() + 230 * s, hump, t.center().x() + 230 * s, edge, t.center().x() + 330 * s, edge);
    band.lineTo(t.right(), edge);
    band.lineTo(t.right(), t.bottom());
    band.lineTo(0, t.bottom());
    band.closeSubpath();
    QLinearGradient g(0, hump, 0, t.bottom());
    g.setColorAt(0, QColor(0x1d, 0x20, 0x27));
    g.setColorAt(1, QColor(0x0e, 0x0f, 0x13));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawPath(band);
    p.setPen(QPen(QColor(255, 255, 255, 22), 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(band);
    // the clock
    const QDateTime now = QDateTime::currentDateTime();
    p.setFont(font(40 * s, 300));
    p.setPen(kText);
    const QRectF timeR(m_l.clock.left(), hump + 6 * s, m_l.clock.width(), 50 * s);
    QString timeFmt = QLocale().timeFormat(QLocale::ShortFormat);
    timeFmt.remove(QRegularExpression(QStringLiteral("[:.]ss?")));
    p.drawText(timeR, Qt::AlignCenter, QLocale().toString(now.time(), timeFmt));
    QString dateFmt = QLocale().dateFormat(QLocale::LongFormat);
    dateFmt.remove(QRegularExpression(QStringLiteral("[,./\\s-]*y+[,./\\s]*")));
    p.setFont(font(14 * s, 500));
    p.setPen(kMuted);
    p.drawText(QRectF(m_l.clock.left(), timeR.bottom(), m_l.clock.width(), 22 * s), Qt::AlignCenter, QLocale().toString(now.date(), dateFmt));
    if (!m_ytInstalled.isEmpty() || !m_ytNewest.isEmpty()) {
        const bool newer = !m_ytNewest.isEmpty() && !m_ytInstalled.isEmpty() && m_ytNewest > m_ytInstalled;
        p.setFont(font(11.5 * s, newer ? 600 : 400));
        p.setPen(newer ? kAmber : QColor(0x6c, 0x71, 0x7c));
        const QString line = m_ytUpdating ? tr("fetching yt-dlp…")
                             : m_ytInstalled.isEmpty() ? tr("no yt-dlp: U fetches it")
                             : newer ? tr("yt-dlp %1 · %2 is out: U updates").arg(m_ytInstalled, m_ytNewest)
                             : tr("yt-dlp %1").arg(m_ytInstalled);
        p.drawText(QRectF(m_l.clock.left(), timeR.bottom() + 20 * s, m_l.clock.width(), 18 * s), Qt::AlignCenter, line);
    }
    // the two round buttons
    for (int i = 0; i < 2; ++i) {
        const QRectF r = i == 0 ? m_l.trayLeft : m_l.trayRight;
        const bool focused = m_focus == Focus::Tray && m_trayButton == i;
        if (focused)
            for (int k = 5; k >= 1; --k) {
                p.setPen(QPen(QColor(242, 163, 58, 110 / (k + 1)), k * 3 * s));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(r);
            }
        QLinearGradient bg(r.topLeft(), r.bottomLeft());
        bg.setColorAt(0, QColor(0x3a, 0x3f, 0x4b));
        bg.setColorAt(1, QColor(0x1e, 0x21, 0x28));
        p.setPen(QPen(focused ? kAmber : QColor(255, 255, 255, 36), (focused ? 2.4 : 1.2) * s));
        p.setBrush(bg);
        p.drawEllipse(r);
        icon(p, i == 0 ? "search" : "tv", r.adjusted(r.width() * 0.27, r.height() * 0.27, -r.width() * 0.27, -r.height() * 0.27), focused ? kAmber : kText);
        p.setFont(font(12.5 * s, 600));
        p.setPen(focused ? kText : kMuted);
        const QString label = i == 0 ? tr("Search") : (m_nowPlaying.isEmpty() ? tr("Back to the player") : tr("Back to the video"));
        const QRectF lr(r.center().x() - 110 * s, r.bottom() + 4 * s, 220 * s, 18 * s);
        p.drawText(lr, Qt::AlignCenter, label);
        if (i == 1 && !m_nowPlaying.isEmpty()) {
            p.setFont(font(11 * s, 400));
            p.setPen(QColor(0x6c, 0x71, 0x7c));
            const QRectF np(r.left() - 260 * s, r.center().y() - 9 * s, 248 * s, 18 * s);
            p.drawText(np, Qt::AlignRight | Qt::AlignVCenter, QFontMetricsF(font(11 * s, 400)).elidedText(m_nowPlaying, Qt::ElideRight, np.width()));
        }
    }
}

void BrowseScreen::paintDetail(QPainter& p)
{
    const double a = m_detailAnim.state() == QAbstractAnimation::Running ? m_detailAnim.currentValue().toDouble() : (m_detailOpen ? 1.0 : 0.0);
    if (a <= 0.001) return;
    const double s = m_l.s;
    // the menu steps back
    {
        const QRectF behind(0, m_l.header.bottom() - 4 * s, width(), m_l.tray.top() - m_l.header.bottom() + 4 * s);
        p.save();
        p.setOpacity(a);
        p.drawPixmap(behind, m_background, behind);
        QLinearGradient dim(0, behind.top(), 0, behind.top() + 40 * s);
        dim.setColorAt(0, QColor(8, 9, 12, 0));
        dim.setColorAt(1, QColor(8, 9, 12, 90));
        p.fillRect(behind, dim);
        p.restore();
    }
    const WebItem& it = m_detail.item;
    // the big screen, grown out of the tile
    const QRectF tv = lerp(m_detailFrom, m_l.detailTv, a);
    const double radius = 26 * s, bezel = 10 * s * a + 6 * s * (1 - a);
    for (int i = 5; i >= 1; --i) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 30));
        p.drawRoundedRect(tv.adjusted(-i * 4 * s, -i * 2 * s + 12 * s, i * 4 * s, i * 5 * s + 12 * s), radius + i * 3 * s, radius + i * 3 * s);
    }
    QLinearGradient bz(tv.topLeft(), tv.bottomLeft());
    bz.setColorAt(0, QColor(0x40, 0x45, 0x52));
    bz.setColorAt(1, QColor(0x1a, 0x1c, 0x22));
    p.setBrush(bz);
    p.setPen(QPen(QColor(255, 255, 255, 30), 1));
    p.drawRoundedRect(tv, radius, radius);
    const QRectF sr = tv.adjusted(bezel, bezel, -bezel, -bezel);
    QPainterPath screen;
    screen.addRoundedRect(sr, radius - bezel * 0.7, radius - bezel * 0.7);
    p.save();
    p.setClipPath(screen);
    p.fillRect(sr, Qt::black);
    // (YouTube's large picture, for the large screen, where it has one; the tile's meanwhile, or instead)
    const QSize tvSize = m_l.detailTv.adjusted(10 * s, 10 * s, -10 * s, -10 * s).size().toSize();
    QPixmap pm;
    if (!it.id.isEmpty() && it.thumb.contains(QLatin1String("ytimg.com/"))) {
        const QString big = QStringLiteral("https://i.ytimg.com/vi/%1/hq720.jpg").arg(it.id);
        pm = thumb(big, tvSize);
    }
    if (pm.isNull()) pm = thumb(it.thumb, tvSize);
    if (!pm.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawPixmap(sr, pm, QRectF(0, 0, pm.width(), pm.height()));
    }
    // a play mark in the middle
    if (a > 0.5) {
        const double d = 84 * s;
        const QRectF c(sr.center().x() - d / 2, sr.center().y() - d / 2, d, d);
        p.setOpacity((a - 0.5) * 2);
        p.setPen(QPen(QColor(255, 255, 255, 90), 2 * s));
        p.setBrush(QColor(10, 11, 14, 150));
        p.drawEllipse(c);
        icon(p, "play", c.adjusted(d * 0.3, d * 0.27, -d * 0.24, -d * 0.27), kText);
        p.setOpacity(1);
    }
    const qint64 at = m_resume ? m_resume(it.url) : 0;
    if (at > 0 && it.seconds > 0) {
        const double f = std::clamp(at / 1000.0 / it.seconds, 0.02, 1.0);
        p.fillRect(QRectF(sr.left(), sr.bottom() - 6 * s, sr.width(), 6 * s), QColor(255, 255, 255, 50));
        p.fillRect(QRectF(sr.left(), sr.bottom() - 6 * s, sr.width() * f, 6 * s), kAmber);
    }
    p.restore();
    paintScreenGlass(p, screen, sr);
    if (a < 0.6) return;
    p.setOpacity((a - 0.6) / 0.4);
    // what it is
    const QRectF tr_ = m_l.detailText;
    p.setFont(font(26 * s, 650));
    p.setPen(kText);
    QRectF used;
    p.drawText(QRectF(tr_.left(), tr_.top(), tr_.width(), 108 * s), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, it.title, &used);
    double y = std::min(used.bottom(), tr_.top() + 108 * s) + 10 * s;
    p.setFont(font(17 * s, 600));
    p.setPen(kAmber);
    p.drawText(QRectF(tr_.left(), y, tr_.width(), 24 * s), Qt::AlignLeft | Qt::AlignVCenter,
               it.channel + (m_data->follows(it.channelId) ? QStringLiteral("  ✓") : QString()));
    y += 30 * s;
    QStringList meta;
    if (it.live) meta << tr("Live now");
    else if (it.seconds > 0) meta << duration(it.seconds);
    if (it.published > 0) meta << age(it.published);
    if (it.views >= 0) meta << views(it.views);
    p.setFont(font(14.5 * s, 500));
    p.setPen(kMuted);
    p.drawText(QRectF(tr_.left(), y, tr_.width(), 22 * s), Qt::AlignLeft | Qt::AlignVCenter, meta.join(QStringLiteral(" · ")));
    y += 34 * s;
    const double buttonsTop = m_l.detailButtons.isEmpty() ? tr_.bottom() : m_l.detailButtons.first().top();
    if (!it.description.isEmpty() && buttonsTop - y > 40 * s) {
        p.setFont(font(14 * s, 400));
        p.setPen(QColor(0xb4, 0xb0, 0xa7));
        const QRectF dr(tr_.left(), y, tr_.width(), buttonsTop - y - 18 * s);
        const QFontMetricsF fm(font(14 * s, 400));
        const int lines = std::max(1, int(dr.height() / fm.lineSpacing()));
        QString d = it.description.simplified();
        // (as many lines as there is room for)
        while (d.size() > 20 && fm.boundingRect(dr, Qt::TextWordWrap, d).height() > lines * fm.lineSpacing()) d = d.left(int(d.size() * 0.9)).trimmed() + QStringLiteral("…");
        p.drawText(dr, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, d);
    }
    // the buttons
    const QList<QString> labels = detailButtons();
    for (int i = 0; i < labels.size() && i < m_l.detailButtons.size(); ++i) {
        const QRectF r = m_l.detailButtons[i];
        const bool focused = i == m_detailButton;
        const bool primary = i == 0;
        if (focused)
            for (int k = 4; k >= 1; --k) {
                p.setPen(QPen(QColor(242, 163, 58, 90 / (k + 1)), k * 3 * s));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
            }
        p.setPen(QPen(focused ? kAmber : QColor(255, 255, 255, 34), (focused ? 2.2 : 1.2) * s));
        p.setBrush(primary ? QColor(242, 163, 58, focused ? 235 : 190) : QColor(255, 255, 255, focused ? 26 : 10));
        p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
        if (primary) icon(p, "play", QRectF(r.left() + 20 * s, r.center().y() - 10 * s, 20 * s, 20 * s), QColor(0x1a, 0x14, 0x0a));
        p.setFont(font(16 * s, 650));
        p.setPen(primary ? QColor(0x1a, 0x14, 0x0a) : (focused ? kText : QColor(0xcf, 0xca, 0xbf)));
        p.drawText(r.adjusted(primary ? 48 * s : 22 * s, 0, -16 * s, 0), Qt::AlignLeft | Qt::AlignVCenter, labels[i]);
    }
    p.setOpacity(1);
}

void BrowseScreen::paintEvent(QPaintEvent*)
{
    ++m_paints;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    paintBackground(p);
    const double fade = m_fade.state() == QAbstractAnimation::Running ? m_fade.currentValue().toDouble() : 1.0;
    if (fade < 1.0) {
        // opening: the menu comes forward
        p.translate(width() / 2.0, height() / 2.0);
        p.scale(1.04 - 0.04 * fade, 1.04 - 0.04 * fade);
        p.translate(-width() / 2.0, -height() / 2.0);
        p.setOpacity(fade);
    }
    paintHeader(p);
    paintGrid(p);
    paintDetail(p);
    paintTray(p);
}

QJsonObject BrowseScreen::report() const
{
    static const char* sections[] = {"new", "channels", "later", "history", "search", "channel"};
    static const char* focuses[] = {"tabs", "grid", "tray", "detail", "input"};
    const View& v = view();
    QJsonArray titles;
    for (int i = v.page * kPerPage; i < std::min(int(v.tiles.size()), (v.page + 1) * kPerPage); ++i) {
        const Tile& t = v.tiles[i];
        titles.append(t.kind == TileKind::Video ? t.item.title : t.kind == TileKind::Channel ? QStringLiteral("channel:") + t.channel.title
                      : t.kind == TileKind::AddChannel ? QStringLiteral("+add") : QStringLiteral("+import"));
    }
    const Tile sel = v.tiles.value(v.selected);
    QJsonArray buttons;
    if (m_detailOpen)
        for (const QString& b : detailButtons()) buttons.append(b);
    int fresh = 0;
    for (const Tile& t : v.tiles) fresh += t.fresh;
    QJsonArray cells;
    for (const QRectF& r : m_l.cells) cells.append(QJsonArray{r.x(), r.y(), r.width(), r.height()});
    return QJsonObject{{"open", m_open}, {"section", QString::fromLatin1(sections[int(m_section)])}, {"focus", QString::fromLatin1(focuses[int(m_focus)])},
                       {"tab", m_tab}, {"page", v.page}, {"pages", pages()}, {"selected", v.selected}, {"count", int(v.tiles.size())},
                       {"titles", titles}, {"selectedTitle", sel.kind == TileKind::Channel ? sel.channel.title : sel.item.title},
                       {"selectedUrl", sel.item.url}, {"loading", v.loading}, {"error", v.error}, {"cached", v.cached}, {"fresh", fresh},
                       {"failedChannels", v.failedChannels}, {"detail", m_detailOpen}, {"detailTitle", m_detailOpen ? m_detail.item.title : QString()},
                       {"detailButtons", buttons}, {"detailButton", m_detailButton}, {"channel", m_channel.id}, {"channelTitle", m_channel.title},
                       {"input", m_input->isVisible() ? m_input->text() : QString()}, {"inputChannel", m_inputChannel},
                       {"message", m_ytMessage}, {"ytInstalled", m_ytInstalled}, {"ytNewest", m_ytNewest}, {"paints", m_paints},
                       {"action", actionLabel()}, {"thumbsShown", int(m_scaled.size())}, {"updating", m_ytUpdating}, {"cells", cells},
                       {"scale", m_l.s}, {"origin", QStringLiteral("%1,%2").arg(pos().x()).arg(pos().y())}};
}
