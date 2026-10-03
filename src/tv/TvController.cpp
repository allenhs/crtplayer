#include "TvController.h"
#include "jellyfin/JellyfinClient.h"

#include <QCollator>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <cmath>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

namespace {
const QStringList kVideoExt = {"mp4", "mkv", "webm", "avi", "mov", "m4v", "mpg", "mpeg", "ts", "m2ts", "mts", "wmv",
                               "flv", "ogv", "ogm", "3gp", "vob", "divx", "asf", "rm", "rmvb"};

QStringList videoFilesUnder(const QString& folder)
{
    QStringList out;
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) return out;
    QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    while (it.hasNext()) {
        const QString f = it.next();
        if (kVideoExt.contains(QFileInfo(f).suffix().toLower()) && !QFileInfo(f).fileName().startsWith('.')) out << f;
        if (out.size() >= 20000) break;
    }
    QCollator c;
    c.setNumericMode(true);            // "Episode 2" before "Episode 10"
    c.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(out.begin(), out.end(), [&c](const QString& a, const QString& b) { return c.compare(a, b) < 0; });
    return out;
}

QFont tvFont(int px, bool mono = false)
{
    QFont f;
    f.setFamilies(mono ? QStringList{"DejaVu Sans Mono", "Noto Sans Mono", "monospace"} : QStringList{"DejaVu Sans", "Noto Sans", "sans-serif"});
    f.setBold(true);
    f.setPixelSize(px);
    return f;
}

void outlined(QPainter& p, const QPointF& baseline, const QFont& f, const QString& text, const QColor& fill, double outline)
{
    QPainterPath path;
    path.addText(baseline, f, text);
    p.setPen(QPen(QColor(0, 0, 0, 235), outline, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.fillPath(path, fill);
}

QString clock(qint64 ms)
{
    return QLocale().toString(QDateTime::fromMSecsSinceEpoch(ms).time(), QStringLiteral("h:mm AP"));
}
} // namespace

QString TvController::tidyTitle(const QString& fileName)
{
    QString t = QFileInfo(fileName).completeBaseName();
    t.replace(QRegularExpression("[._]+"), " ");
    return t.simplified();
}

TvController::TvController(const Host& host, const QString& storePath, QObject* parent)
    : QObject(parent), m_host(host), m_storePath(storePath)
{
    m_worker = std::thread([this] { workerLoop(); });
    m_tick.setInterval(100);
    connect(&m_tick, &QTimer::timeout, this, &TvController::tick);
    m_digitTimer.setSingleShot(true);
    m_digitTimer.setInterval(1600);
    connect(&m_digitTimer, &QTimer::timeout, this, [this] {
        const int n = m_digits.toInt();
        m_digits.clear();
        if (n > 0) tune(n);
        else updateOverlay();
    });
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { save(); });
    m_anim.start();
    load();
}

TvController::~TvController()
{
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_quit = true;
        m_queue.clear();
    }
    m_wake.notify_all();
    if (m_worker.joinable()) m_worker.join();
    if (m_saveTimer.isActive()) save();
}

// ---- channels ---------------------------------------------------------------------------

TvChannel* TvController::find(int number)
{
    for (TvChannel& c : m_channels) if (c.number == number) return &c;
    return nullptr;
}

const TvChannel* TvController::channel(int number) const
{
    for (const TvChannel& c : m_channels) if (c.number == number) return &c;
    return nullptr;
}

int TvController::freeNumber() const
{
    int n = 2;   // cable starts at 2
    while (channel(n)) ++n;
    return n;
}

bool TvController::allReady() const
{
    for (const TvChannel& c : m_channels) if (!c.ready()) return false;
    return true;
}

static void sortChannels(QVector<TvChannel>& v)
{
    std::sort(v.begin(), v.end(), [](const TvChannel& a, const TvChannel& b) { return a.number < b.number; });
}

int TvController::addFolderChannel(const QString& folder, int number, const QString& name)
{
    if (!QFileInfo(folder).isDir()) return 0;
    TvChannel ch;
    ch.number = number > 0 && !channel(number) ? number : freeNumber();
    ch.name = name.isEmpty() ? QDir(folder).dirName() : name;
    ch.folders << QDir(folder).absolutePath();
    ch.seed = m_testSeeds ? quint32(ch.number) * 7919u : QRandomGenerator::global()->generate();
    const int n = ch.number;
    m_channels.push_back(ch);
    sortChannels(m_channels);
    scanLocal(*find(n));
    m_saveTimer.start();
    emit channelsChanged();
    return n;
}

int TvController::addJellyfinChannel(const QString& itemId, const QString& itemName, int number)
{
    if (itemId.isEmpty()) return 0;
    TvChannel ch;
    ch.number = number > 0 && !channel(number) ? number : freeNumber();
    ch.name = itemName;
    ch.jellyfin.push_back({itemId, itemName, {}});
    ch.seed = m_testSeeds ? quint32(ch.number) * 7919u : QRandomGenerator::global()->generate();
    const int n = ch.number;
    m_channels.push_back(ch);
    sortChannels(m_channels);
    fetchJellyfin(n, 0);
    m_saveTimer.start();
    emit channelsChanged();
    return n;
}

void TvController::removeChannel(int number)
{
    for (int i = 0; i < m_channels.size(); ++i)
        if (m_channels[i].number == number) { m_channels.removeAt(i); break; }
    m_saveTimer.start();
    emit channelsChanged();
    if (m_on && m_current == number) {
        if (m_channels.isEmpty()) setOn(false);
        else tune(m_channels.first().number);
    }
}

void TvController::renumber(int number, int newNumber)
{
    TvChannel* ch = find(number);
    if (!ch || newNumber < 1 || newNumber > 999 || channel(newNumber)) return;
    ch->number = newNumber;
    if (m_current == number) m_current = newNumber;
    sortChannels(m_channels);
    m_guideSlotsAt = -1;
    m_saveTimer.start();
    emit channelsChanged();
    updateOverlay();
}

void TvController::setName(int number, const QString& name)
{
    if (TvChannel* ch = find(number)) { ch->name = name; m_saveTimer.start(); emit channelsChanged(); updateOverlay(); }
}

void TvController::setShuffle(int number, bool shuffle)
{
    TvChannel* ch = find(number);
    if (!ch || ch->shuffle == shuffle) return;
    ch->shuffle = shuffle;
    rebuildLineup(*ch);
    m_saveTimer.start();
    if (m_on && m_current == number) tune(number);   // the schedule changed under us
}

void TvController::setBumperFolder(int number, const QString& folder)
{
    TvChannel* ch = find(number);
    if (!ch) return;
    ch->bumperFolder = folder.isEmpty() ? QString() : QDir(folder).absolutePath();
    scanLocal(*ch);
    m_saveTimer.start();
    emit channelsChanged();
}

void TvController::rescan(int number)
{
    TvChannel* ch = find(number);
    if (!ch) return;
    scanLocal(*ch);
    for (int i = 0; i < ch->jellyfin.size(); ++i) fetchJellyfin(number, i);
    emit channelsChanged();
}

// ---- scanning ---------------------------------------------------------------------------

void TvController::scanLocal(TvChannel& ch)
{
    ch.fileList.clear();
    for (const QString& f : ch.folders) ch.fileList += videoFilesUnder(f);
    ch.bumperList = videoFilesUnder(ch.bumperFolder);
    ch.files = ch.fileList.size();
    ch.pending = 0;
    ch.problem.clear();
    for (const QString& f : ch.folders) if (!QFileInfo(f).isDir()) ch.problem = tr("Folder not found: %1").arg(f);
    QStringList toProbe;
    for (const QString& path : ch.fileList + ch.bumperList) {
        const QFileInfo fi(path);
        const auto it = m_cache.constFind(path);
        if (it != m_cache.constEnd() && it->mtime == fi.lastModified().toSecsSinceEpoch() && it->size == fi.size()) continue;
        ++ch.pending;
        if (!m_waiting.contains(path)) toProbe << path;
        if (!m_waiting[path].contains(ch.number)) m_waiting[path] << ch.number;
    }
    if (!toProbe.isEmpty()) {
        {
            std::lock_guard<std::mutex> l(m_mutex);
            for (const QString& p : toProbe) m_queue.push_back(p);
        }
        m_wake.notify_one();
    }
    if (ch.pending == 0) rebuildLineup(ch);
}

void TvController::workerLoop()
{
    GstDiscoverer* disc = nullptr;
    for (;;) {
        QString path;
        {
            std::unique_lock<std::mutex> l(m_mutex);
            m_wake.wait(l, [this] { return m_quit || !m_queue.empty(); });
            if (m_quit) break;
            path = m_queue.front();
            m_queue.pop_front();
        }
        if (!disc) disc = gst_discoverer_new(8 * GST_SECOND, nullptr);
        qint64 ms = 0;
        if (disc) {
            gchar* uri = gst_filename_to_uri(QFile::encodeName(path).constData(), nullptr);
            GstDiscovererInfo* info = uri ? gst_discoverer_discover_uri(disc, uri, nullptr) : nullptr;
            g_free(uri);
            if (info) {
                GList* video = gst_discoverer_info_get_video_streams(info);
                if (video) ms = qint64(gst_discoverer_info_get_duration(info) / GST_MSECOND);   // (files without a picture are left out)
                gst_discoverer_stream_info_list_free(video);
                g_object_unref(info);
            }
        }
        QMetaObject::invokeMethod(this, [this, path, ms] { probed(path, ms); }, Qt::QueuedConnection);
    }
    if (disc) g_object_unref(disc);
}

void TvController::probed(const QString& path, qint64 ms)
{
    const QFileInfo fi(path);
    m_cache.insert(path, {fi.lastModified().toSecsSinceEpoch(), fi.size(), ms});
    const QVector<int> waiting = m_waiting.take(path);
    for (int number : waiting) {
        TvChannel* ch = find(number);
        if (!ch || ch->pending <= 0) continue;
        if (--ch->pending == 0) rebuildLineup(*ch);
    }
    m_saveTimer.start();
    emit channelsChanged();
}

void TvController::fetchJellyfin(int number, int index)
{
    TvChannel* ch = find(number);
    if (!ch || index >= ch->jellyfin.size()) return;
    if (!m_host.jellyfin || !m_host.jellyfin->isSignedIn()) {
        if (ch->jellyfin[index].items.isEmpty()) ch->problem = tr("Sign in to Jellyfin to load this channel.");
        return;
    }
    ch->fetching = true;
    const QString id = ch->jellyfin[index].id;
    m_host.jellyfin->loadAllVideos(id, [this, number, id](const QVector<JfItem>& items, const QString& error) {
        TvChannel* c = find(number);
        if (!c) return;
        c->fetching = false;
        for (TvChannel::Jf& j : c->jellyfin) {
            if (j.id != id) continue;
            if (!error.isEmpty() && items.isEmpty()) { c->problem = error; break; }
            c->problem.clear();
            j.items.clear();
            for (const JfItem& it : items) {
                if (!it.isVideo() || it.runTimeTicks < 10 * 10000000LL / 10) continue;   // (under a second: not a programme)
                const QString title = it.seriesName.isEmpty() ? it.displayName() : it.seriesName + QStringLiteral(" · ") + it.displayName();
                j.items.push_back({QStringLiteral("jellyfin:///%1#%2").arg(it.id, QString::fromUtf8(QUrl::toPercentEncoding(title))),
                                   title, it.runTimeTicks / 10000});
            }
        }
        rebuildLineup(*c);
        m_saveTimer.start();
    });
}

void TvController::rebuildLineup(TvChannel& ch)
{
    TvLineup l;
    l.shuffle = ch.shuffle;
    l.seed = ch.seed;
    for (const QString& f : ch.fileList) {
        const qint64 ms = m_cache.value(f).ms;
        if (ms >= 1000) l.programs.push_back({f, tidyTitle(f), ms});
    }
    for (const TvChannel::Jf& j : ch.jellyfin) l.programs += j.items;
    for (const QString& f : ch.bumperList) {
        const qint64 ms = m_cache.value(f).ms;
        if (ms >= 500) l.bumpers.push_back({f, tidyTitle(f), ms});
    }
    ch.lineup = l;
    m_guideSlotsAt = -1;
    emit channelsChanged();
    // The channel being watched had nothing on until now.
    if (m_on && m_current == ch.number && !m_slot.valid && ch.ready()) tune(ch.number);
}

// ---- storage ----------------------------------------------------------------------------

void TvController::load()
{
    QFile f(m_storePath);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.contains("openLagMs")) m_openLagMs = std::clamp(o.value("openLagMs").toDouble(300), 50.0, 4000.0);
    const QJsonObject cache = o.value("lengths").toObject();
    for (auto it = cache.begin(); it != cache.end(); ++it) {
        const QJsonArray a = it.value().toArray();
        m_cache.insert(it.key(), {qint64(a.at(0).toDouble()), qint64(a.at(1).toDouble()), qint64(a.at(2).toDouble())});
    }
    for (const QJsonValue& v : o.value("channels").toArray()) {
        const QJsonObject c = v.toObject();
        TvChannel ch;
        ch.number = c.value("number").toInt();
        ch.name = c.value("name").toString();
        for (const QJsonValue& x : c.value("folders").toArray()) ch.folders << x.toString();
        ch.bumperFolder = c.value("bumpers").toString();
        ch.shuffle = c.value("shuffle").toBool(true);
        ch.seed = quint32(c.value("seed").toDouble());
        for (const QJsonValue& x : c.value("jellyfin").toArray()) {
            const QJsonObject j = x.toObject();
            TvChannel::Jf jf{j.value("id").toString(), j.value("name").toString(), {}};
            for (const QJsonValue& y : j.value("items").toArray()) {
                const QJsonArray a = y.toArray();
                const QString title = a.at(1).toString();
                jf.items.push_back({QStringLiteral("jellyfin:///%1#%2").arg(a.at(0).toString(), QString::fromUtf8(QUrl::toPercentEncoding(title))),
                                    title, qint64(a.at(2).toDouble())});
            }
            ch.jellyfin.push_back(jf);
        }
        if (ch.number > 0 && !channel(ch.number)) m_channels.push_back(ch);
    }
    sortChannels(m_channels);
    m_current = o.value("last").toInt();
    for (TvChannel& ch : m_channels) scanLocal(ch);   // (lengths come from the cache; only new files are read)
}

void TvController::save() const
{
    QJsonObject o;
    QJsonArray chans;
    QSet<QString> used;
    for (const TvChannel& ch : m_channels) {
        QJsonArray jf;
        for (const TvChannel::Jf& j : ch.jellyfin) {
            QJsonArray items;
            for (const TvProgram& p : j.items)
                items.append(QJsonArray{QUrl(p.source).path().section('/', -1), p.title, double(p.durationMs)});
            jf.append(QJsonObject{{"id", j.id}, {"name", j.name}, {"items", items}});
        }
        chans.append(QJsonObject{{"number", ch.number}, {"name", ch.name}, {"folders", QJsonArray::fromStringList(ch.folders)},
                                 {"bumpers", ch.bumperFolder}, {"shuffle", ch.shuffle}, {"seed", double(ch.seed)}, {"jellyfin", jf}});
        for (const QString& f : ch.fileList + ch.bumperList) used.insert(f);
    }
    QJsonObject cache;   // only the files channels still use
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
        if (used.contains(it.key())) cache.insert(it.key(), QJsonArray{double(it->mtime), double(it->size), double(it->ms)});
    o.insert("version", 1);
    o.insert("channels", chans);
    o.insert("lengths", cache);
    o.insert("last", m_current);
    o.insert("openLagMs", m_openLagMs);   // how long videos take to open on this computer: known from the first tune next time
    QDir().mkpath(QFileInfo(m_storePath).absolutePath());
    QSaveFile f(m_storePath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

// ---- watching ---------------------------------------------------------------------------

qint64 TvController::nowMs() const { return QDateTime::currentMSecsSinceEpoch() + m_clockOffset; }

void TvController::setClock(qint64 wallMs)
{
    m_clockOffset = wallMs - QDateTime::currentMSecsSinceEpoch();
    m_guideSlotsAt = -1;
}

void TvController::setOn(bool on)
{
    if (on == m_on) return;
    if (on && m_channels.isEmpty()) { emit stateChanged(); return; }
    m_on = on;
    if (!on) {
        m_tick.stop();
        m_digitTimer.stop();
        m_digits.clear();
        m_guide = false;
        m_message.clear();
        m_failed = false;
        m_slot = TvSlot();
        m_overlay = QImage();
        m_overlayKey.clear();
        m_host.setOverlay(QImage());
        m_saveTimer.start();
        emit stateChanged();
        return;
    }
    m_tick.start();
    tune(channel(m_current) ? m_current : m_channels.first().number);
}

void TvController::tune(int number, bool burst)
{
    if (!m_on) return;
    m_digitTimer.stop();
    m_digits.clear();
    const qint64 now = nowMs();
    TvChannel* ch = find(number);
    m_badgeUntil = now + 4000;
    m_tunedAwaitingPicture = ch != nullptr;
    if (!ch) {   // no such channel: say so and stay where we are
        m_message = tr("CH %1 NOT IN USE").arg(number, 2, 10, QChar('0'));
        QTimer::singleShot(2500, this, [this, msg = m_message] { if (m_message == msg) { m_message.clear(); updateOverlay(); } });
        updateOverlay();
        return;
    }
    m_current = number;
    ++m_tunes;
    m_failed = false;
    m_saveTimer.start();
    const TvSlot slot = TvSchedule::at(ch->lineup, now);
    if (!slot.valid) {
        m_slot = m_next = TvSlot();
        m_message = ch->ready() ? (ch->problem.isEmpty() ? tr("NO PROGRAMMES") : tr("NO SIGNAL")) : tr("SCANNING…");
        m_host.snow();
        updateOverlay();
        emit stateChanged();
        return;
    }
    m_message.clear();
    playSlot(slot, burst);
}

// A programme that follows on up to this late still starts from its beginning.
static const qint64 kLateTolerated = 3000;

void TvController::playSlot(const TvSlot& slotIn, bool burst, bool whole)
{
    const TvChannel* ch = channel(m_current);
    if (!ch) return;
    TvSlot slot = slotIn;
    const qint64 now = nowMs();
    // Too close to its end to be worth opening: the next one, from its start.
    if (!whole && slot.endMs - now < 1500) {
        const TvSlot next = TvSchedule::at(ch->lineup, slot.endMs);
        if (next.valid) { slot = next; slot.offsetMs = 0; }
    }
    // Where in the file to start. By the time it is open the broadcast has moved on, so aim
    // that much ahead - except that a beginning is not clipped for a small delay: a bumper
    // always plays whole, a programme that has barely begun (or follows a little late) starts
    // from the top, and the time is made up later.
    qint64 startMs = 0;
    if (!(whole && slot.bumper)) {
        startMs = slot.offsetMs + qint64(m_openLagMs);
        if (slot.offsetMs < 1000 || (whole && startMs < kLateTolerated)) startMs = 0;
        startMs = std::clamp<qint64>(startMs, 0, std::max<qint64>(0, slot.program.durationMs - 1000));
    }
    slot.offsetMs = startMs;
    m_requestedAt = QDateTime::currentMSecsSinceEpoch();
    m_requestStartMs = startMs;
    m_slot = slot;
    m_next = TvSchedule::at(ch->lineup, slot.endMs);
    m_bannerUntil = slot.bumper ? 0 : now + 5500;   // (an ident is not announced)
    ++(slot.bumper ? m_bumpersPlayed : m_programsPlayed);
    m_host.play(slot.program.source, startMs * 1000000, slot.program.title, burst);
    updateOverlay();
    emit stateChanged();
}

void TvController::channelStep(int dir)
{
    if (!m_on || m_channels.isEmpty()) return;
    int idx = 0;
    for (int i = 0; i < m_channels.size(); ++i) if (m_channels[i].number == m_current) idx = i;
    idx = (idx + (dir > 0 ? 1 : -1) + m_channels.size()) % m_channels.size();
    tune(m_channels[idx].number);
}

void TvController::digit(int d)
{
    if (!m_on || d < 0 || d > 9) return;
    m_digits += QString::number(d);
    if (m_digits.size() >= 3) { m_digitTimer.stop(); const int n = m_digits.toInt(); m_digits.clear(); tune(n); return; }
    m_digitTimer.start();
    updateOverlay();
}

void TvController::setGuide(bool on)
{
    if (!m_on) on = false;
    if (m_guide == on) return;
    m_guide = on;
    m_anim.restart();
    m_guideSlotsAt = -1;
    updateOverlay();
    emit stateChanged();
}

void TvController::programEnded()
{
    if (!m_on) return;
    const TvChannel* ch = channel(m_current);
    if (!ch) return;
    const qint64 now = nowMs();
    // What follows in the schedule, straight on. The player runs a little behind the clock
    // (files take a moment to open): a bumper still plays whole, and the next programme
    // makes the time up by starting that much in. Only when it is far out (after a long
    // pause, say) is the channel joined again where the broadcast is.
    if (m_slot.valid) {
        TvSlot next = TvSchedule::at(ch->lineup, m_slot.endMs);
        qint64 late = now - next.startMs;
        if (next.valid && next.bumper && late > next.endMs - next.startMs - 800) {   // its time is already up
            next = TvSchedule::at(ch->lineup, next.endMs);
            late = now - next.startMs;
        }
        if (next.valid && late > -4000 && late < 8000) {
            next.offsetMs = next.bumper ? 0 : std::max<qint64>(0, late);
            playSlot(next, false, true);
            return;
        }
    }
    const TvSlot slot = TvSchedule::at(ch->lineup, now);
    if (!slot.valid) { tune(m_current, false); return; }
    playSlot(slot, false);
}

void TvController::noteStarted()
{
    if (m_tunedAwaitingPicture) {
        // The channel number and the now/next banner stay up for a few seconds of picture,
        // however long the channel took to come in (a slow disk, a Jellyfin stream).
        m_tunedAwaitingPicture = false;
        const qint64 now = nowMs();
        m_badgeUntil = std::max(m_badgeUntil, now + 3000);
        if (!m_slot.bumper) m_bannerUntil = std::max(m_bannerUntil, now + 4500);
        updateOverlay();
    }
    if (m_requestedAt <= 0) return;
    const double took = std::clamp<double>(QDateTime::currentMSecsSinceEpoch() - m_requestedAt, 0, 5000);
    m_requestedAt = 0;
    m_openLagMs = 0.6 * m_openLagMs + 0.4 * took;
}

void TvController::programFailed(const QString& why)
{
    if (!m_on) return;
    Q_UNUSED(why);
    m_failed = true;
    m_message = tr("NO SIGNAL");
    // Try again when the next programme starts (or soon, if that is far off).
    m_retryAt = m_slot.valid ? std::min(m_slot.endMs, nowMs() + 60000) : nowMs() + 15000;
    m_host.snow();
    updateOverlay();
    emit stateChanged();
}

void TvController::tick()
{
    if (!m_on) return;
    // On screen and running: a few frames in, at (or past) the place asked for.
    if (m_requestedAt > 0 && m_host.framesShown && m_host.framesShown() >= 3 && m_host.positionMs() >= m_requestStartMs - 300) noteStarted();
    if (m_failed && nowMs() >= m_retryAt) { m_failed = false; m_message.clear(); tune(m_current, false); return; }
    updateOverlay();
}

// ---- the overlay --------------------------------------------------------------------------

void TvController::updateOverlay()
{
    if (!m_on) return;
    const qint64 now = nowMs();
    const TvChannel* ch = channel(m_current);
    // What to show.
    QString badge, badgeName;
    if (!m_digits.isEmpty()) badge = QStringLiteral("CH %1").arg(m_digits + QString(std::max<qsizetype>(0, 2 - m_digits.size()), QChar('-')));
    else if (now < m_badgeUntil || !m_message.isEmpty()) {
        badge = QStringLiteral("CH %1").arg(m_current, 2, 10, QChar('0'));
        badgeName = ch ? ch->name.toUpper() : QString();
    }
    QString nowLine, nextLine;
    const qint64 left = m_slot.valid ? m_slot.endMs - now : 0;
    if (m_slot.valid && m_message.isEmpty() && !m_guide) {
        if (now < m_bannerUntil) {
            nowLine = m_slot.program.title;
            if (m_next.valid) nextLine = tr("NEXT  %1  %2").arg(clock(m_slot.endMs), m_next.program.title);
        } else if (left > 0 && left <= 12000 && m_next.valid && !m_slot.bumper) {
            nextLine = tr("NEXT   %1").arg(m_next.program.title);
        }
    }
    const double aspect = std::clamp(m_host.pictureAspect ? m_host.pictureAspect() : 4.0 / 3.0, 0.5, 3.0);
    // The shorter side is 480: lettering keeps its size on wide and on upright pictures.
    const QSize size = aspect >= 1.0 ? QSize(int(std::lround(480 * aspect)), 480) : QSize(480, int(std::lround(480 / aspect)));
    int rowsFit = 0;
    QString key = badge + '|' + badgeName + '|' + m_message + '|' + nowLine + '|' + nextLine + '|' + QString::number(size.width());
    if (m_guide) {
        rowsFit = int((size.height() - int(size.height() * 0.42) - 52) / 34);
        const bool scrolls = m_channels.size() > rowsFit;
        key += QStringLiteral("|guide|%1|%2").arg(now / 1000).arg(scrolls ? m_anim.elapsed() / 66 : 0);
    }
    if (key == m_overlayKey) return;
    m_overlayKey = key;
    if (badge.isEmpty() && m_message.isEmpty() && nowLine.isEmpty() && nextLine.isEmpty() && !m_guide) {
        m_overlay = QImage();
        m_host.setOverlay(m_overlay);
        return;
    }

    QImage img(size, QImage::Format_RGBA8888);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const int W = size.width(), H = size.height();
    if (m_guide) paintGuide(p, size, now);
    if (!badge.isEmpty()) {   // the set's own channel display: chunky green, top right
        const QFont f = tvFont(46, true);
        const QFontMetrics fm(f);
        outlined(p, QPointF(W - fm.horizontalAdvance(badge) - 28, 64), f, badge, QColor(70, 255, 96), 8);
        if (!badgeName.isEmpty()) {
            const QFont f2 = tvFont(22, true);
            const QString n = QFontMetrics(f2).elidedText(badgeName, Qt::ElideRight, W / 2);
            outlined(p, QPointF(W - QFontMetrics(f2).horizontalAdvance(n) - 28, 94), f2, n, QColor(70, 255, 96), 6);
        }
    }
    if (!m_message.isEmpty()) {
        const QFont f = tvFont(34, true);
        outlined(p, QPointF((W - QFontMetrics(f).horizontalAdvance(m_message)) / 2.0, H / 2.0 + 12), f, m_message, QColor(245, 245, 245), 8);
    }
    if (!nowLine.isEmpty() || !nextLine.isEmpty()) {   // the station's lower-third
        const bool two = !nowLine.isEmpty() && !nextLine.isEmpty();
        const int bh = two ? 76 : 44;
        const QRectF band(W * 0.05, H * 0.90 - bh, W * 0.90, bh);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(12, 18, 86, 225));
        p.drawRect(band);
        p.setBrush(QColor(255, 214, 64));
        p.drawRect(QRectF(band.left(), band.top(), band.width(), 4));
        const int textW = int(band.width()) - 28;
        if (!nowLine.isEmpty()) {
            const QFont f = tvFont(25);
            p.setFont(f);
            p.setPen(QColor(255, 255, 255));
            p.drawText(QPointF(band.left() + 14, band.top() + 36), QFontMetrics(f).elidedText(nowLine, Qt::ElideRight, textW));
        }
        if (!nextLine.isEmpty()) {
            const QFont f = tvFont(20);
            p.setFont(f);
            p.setPen(QColor(255, 226, 120));
            p.drawText(QPointF(band.left() + 14, band.bottom() - 13), QFontMetrics(f).elidedText(nextLine, Qt::ElideRight, textW));
        }
    }
    p.end();
    m_overlay = img;
    m_host.setOverlay(img);
}

// The programme guide: a blue grid over the lower part of the picture, channels down the
// side, the next hour and a half across, scrolling when there are more channels than rows.
void TvController::paintGuide(QPainter& p, const QSize& size, qint64 now)
{
    const int W = size.width(), H = size.height();
    const int top = int(H * 0.42);
    const QRect area(0, top, W, H - top);
    p.setPen(Qt::NoPen);
    p.fillRect(area, QColor(14, 22, 110));
    // title bar
    p.fillRect(QRect(0, top, W, 28), QColor(8, 12, 70));
    p.setFont(tvFont(18));
    p.setPen(QColor(255, 214, 64));
    p.drawText(QRect(12, top, W / 2, 28), Qt::AlignVCenter | Qt::AlignLeft, tr("CRT CABLE GUIDE"));
    p.setPen(Qt::white);
    p.drawText(QRect(W / 2, top, W / 2 - 12, 28), Qt::AlignVCenter | Qt::AlignRight, clock(now));
    // time header: three half-hours from the last half-hour mark
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(now);
    const qint64 t0 = now - (qint64(dt.time().minute() % 30) * 60000 + dt.time().second() * 1000 + dt.time().msec());
    const qint64 span = 90 * 60000;
    const int chW = int(W * 0.24), gridX = chW, gridW = W - chW;
    const int headY = top + 28, headH = 24, rowsY = headY + headH, rowH = 34;
    p.fillRect(QRect(0, headY, W, headH), QColor(30, 44, 150));
    p.setFont(tvFont(15));
    for (int i = 0; i < 3; ++i) {
        const int x = gridX + gridW * i / 3;
        p.setPen(QColor(255, 255, 255));
        p.drawText(QRect(x + 6, headY, gridW / 3 - 8, headH), Qt::AlignVCenter | Qt::AlignLeft, clock(t0 + i * 30 * 60000));
        p.setPen(QColor(8, 12, 70));
        p.drawLine(x, headY, x, headY + headH);
    }
    // schedules (worked out once a second)
    if (m_guideSlotsAt != now / 1000) {
        m_guideSlots.clear();
        for (const TvChannel& c : m_channels) m_guideSlots.insert(c.number, TvSchedule::between(c.lineup, t0, t0 + span, 600));
        m_guideSlotsAt = now / 1000;
    }
    const QRect rowsArea(0, rowsY, W, H - rowsY);
    p.save();
    p.setClipRect(rowsArea);
    const int n = m_channels.size();
    const int fit = rowsArea.height() / rowH;
    const bool scrolls = n > fit;
    const int cycle = n * rowH + rowH;   // a blank row between the last channel and the first again
    const int scroll = scrolls ? int((m_anim.elapsed() * 16 / 1000) % cycle) : 0;
    auto drawRow = [&](const TvChannel& c, int y) {
        if (y + rowH < rowsArea.top() || y > rowsArea.bottom()) return;
        const bool current = c.number == m_current;
        p.fillRect(QRect(0, y, chW - 1, rowH - 1), current ? QColor(255, 214, 64) : QColor(8, 12, 70));
        p.setFont(tvFont(17));
        p.setPen(current ? QColor(8, 12, 70) : QColor(255, 214, 64));
        p.drawText(QRect(8, y, 34, rowH - 1), Qt::AlignVCenter | Qt::AlignLeft, QString::number(c.number));
        p.setFont(tvFont(14));
        p.setPen(current ? QColor(8, 12, 70) : QColor(255, 255, 255));
        p.drawText(QRect(44, y, chW - 50, rowH - 1), Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(p.font()).elidedText(c.name.toUpper(), Qt::ElideRight, chW - 52));
        const QVector<TvSlot> shown = m_guideSlots.value(c.number);
        if (shown.isEmpty()) {
            p.setFont(tvFont(14));
            p.setPen(QColor(170, 180, 230));
            p.drawText(QRect(gridX + 8, y, gridW - 12, rowH - 1), Qt::AlignVCenter | Qt::AlignLeft,
                       c.ready() ? tr("No programmes") : tr("Scanning…"));
            return;
        }
        for (const TvSlot& s : shown) {
            const double x0 = gridX + gridW * double(std::max(s.startMs, t0) - t0) / span;
            const double x1 = gridX + gridW * double(std::min(s.endMs, t0 + span) - t0) / span;
            if (x1 - x0 < 1.5) continue;
            const QRectF cell(x0, y, x1 - x0 - 1, rowH - 1);
            const bool onNow = s.startMs <= now && now < s.endMs;
            p.fillRect(cell, s.bumper ? QColor(22, 32, 120) : onNow ? (current ? QColor(70, 104, 236) : QColor(48, 72, 200)) : QColor(34, 50, 166));
            if (cell.width() < 30 || s.bumper) continue;
            QString t = s.program.title;
            if (s.startMs < t0) t = QStringLiteral("◀ ") + t;
            p.setFont(tvFont(14));
            p.setPen(onNow ? QColor(255, 255, 255) : QColor(214, 222, 255));
            p.drawText(cell.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       QFontMetrics(p.font()).elidedText(t, Qt::ElideRight, int(cell.width()) - 10));
        }
        // Short programmes make cells too narrow to read: what is on now is then written
        // out from its cell to the right, over the ones that follow.
        for (const TvSlot& s : shown) {
            if (!(s.startMs <= now && now < s.endMs) || s.bumper) continue;
            const double x0 = gridX + gridW * double(std::max(s.startMs, t0) - t0) / span;
            const double x1 = gridX + gridW * double(std::min(s.endMs, t0 + span) - t0) / span;
            if (x1 - x0 >= 110) break;
            p.setFont(tvFont(14));
            const QFontMetrics fm(p.font());
            const double w = std::min<double>(fm.horizontalAdvance(s.program.title) + 16, gridX + gridW - x0 - 2);
            if (w < 40) break;
            const QRectF label(x0, y, w, rowH - 1);
            p.fillRect(label, current ? QColor(70, 104, 236) : QColor(48, 72, 200));
            p.setPen(Qt::white);
            p.drawText(label.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(s.program.title, Qt::ElideRight, int(w) - 10));
            break;
        }
    };
    for (int pass = 0; pass < (scrolls ? 2 : 1); ++pass)
        for (int i = 0; i < n; ++i) drawRow(m_channels[i], rowsY + i * rowH - scroll + pass * cycle);
    // the "now" line
    const double nx = gridX + gridW * double(now - t0) / span;
    p.setPen(QPen(QColor(255, 80, 80, 220), 2));
    p.drawLine(QPointF(nx, rowsY), QPointF(nx, H));
    p.restore();
}

QJsonObject TvController::report() const
{
    const qint64 now = nowMs();
    QJsonArray chans;
    for (const TvChannel& c : m_channels) {
        const TvSlot s = TvSchedule::at(c.lineup, now);
        chans.append(QJsonObject{{"number", c.number}, {"name", c.name}, {"programs", c.lineup.programs.size()},
                                 {"bumpers", c.lineup.bumpers.size()}, {"files", c.files}, {"ready", c.ready()},
                                 {"shuffle", c.shuffle}, {"cycleMs", double(TvSchedule::cycleLengthMs(c.lineup))},
                                 {"jellyfin", !c.jellyfin.isEmpty()}, {"problem", c.problem},
                                 {"onNow", s.valid ? s.program.title : QString()}, {"onNowOffsetMs", double(s.offsetMs)}});
    }
    const TvChannel* ch = channel(m_current);
    const TvSlot live = ch ? TvSchedule::at(ch->lineup, now) : TvSlot();
    return QJsonObject{{"on", m_on}, {"channel", m_current}, {"channelName", ch ? ch->name : QString()},
                       {"program", m_slot.valid ? m_slot.program.title : QString()},
                       {"source", m_slot.valid ? m_slot.program.source.section('#', 0, 0) : QString()},
                       {"bumper", m_slot.bumper}, {"slotStartMs", double(m_slot.startMs)}, {"slotEndMs", double(m_slot.endMs)},
                       {"tunedOffsetMs", double(m_slot.offsetMs)},
                       {"scheduleOffsetMs", m_slot.valid ? double(now - m_slot.startMs) : -1.0},
                       {"liveProgram", live.valid ? live.program.title : QString()},
                       {"next", m_next.valid ? m_next.program.title : QString()},
                       {"nowMs", double(now)}, {"guide", m_guide}, {"programsPlayed", m_programsPlayed}, {"bumpersPlayed", m_bumpersPlayed},
                       {"liveBumper", live.valid && live.bumper}, {"liveOffsetMs", live.valid ? double(live.offsetMs) : -1.0},
                       {"liveRemainingMs", live.valid ? double(live.endMs - now) : -1.0}, {"message", m_message}, {"tunes", m_tunes},
                       {"openLagMs", m_openLagMs}, {"overlay", m_overlayKey}, {"hasOverlay", !m_overlay.isNull()}, {"allReady", allReady()}, {"channels", chans}};
}
