#include "NvEnhancer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#ifdef Q_OS_LINUX
#include "../../tools/nvfx/NvShm.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace {
const int kFrameTimeoutMs = 1500;   // far beyond what a frame takes (milliseconds): the helper is stuck
// A running mean that follows changes within a second or so.
void mean(double& m, double v) { m = m <= 0 ? v : m + (v - m) * 0.1; }

double field(const QByteArray& rest, const char* name)
{
    const QByteArray key = QByteArray(name) + '=';
    for (const QByteArray& part : rest.split(' '))
        if (part.startsWith(key)) return part.mid(key.size()).toDouble();
    return 0;
}
QByteArray fieldText(const QByteArray& rest, const char* name)
{
    const QByteArray key = QByteArray(name) + '=';
    for (const QByteArray& part : rest.split(' '))
        if (part.startsWith(key)) return part.mid(key.size());
    return {};
}
} // namespace

QString NvEnhancer::findHelper()
{
    const QString name = QStringLiteral("crtplayer-nvfx");
    const bool named = qEnvironmentVariableIsSet("CRTPLAYER_NVFX_HELPER");   // (that one or none)
    QStringList tries;
    if (named) tries << qEnvironmentVariable("CRTPLAYER_NVFX_HELPER");
    else {
        tries << QCoreApplication::applicationDirPath() + '/' + name;
        if (qEnvironmentVariableIsSet("APPDIR")) tries << qEnvironmentVariable("APPDIR") + QStringLiteral("/usr/bin/") + name;
    }
    for (const QString& t : tries) {
        const QFileInfo fi(t);
        if (fi.isFile() && fi.isExecutable()) return fi.absoluteFilePath();
    }
    return named ? QString() : QStandardPaths::findExecutable(name);
}

NvEnhancer::Install NvEnhancer::findInstall()
{
    Install in;
    if (qEnvironmentVariableIsSet("CRTPLAYER_NVFX_OFF")) return in;
    in.helper = findHelper();
    if (in.helper.isEmpty()) return in;
    QProcess p;
    p.start(in.helper, {QStringLiteral("--where")});
    if (!p.waitForFinished(3000)) { p.kill(); p.waitForFinished(500); return in; }
    for (const QByteArray& line : p.readAllStandardOutput().split('\n')) {
        if (line.startsWith("sdk=")) in.sdk = QString::fromLocal8Bit(line.mid(4)).trimmed();
        else if (line.startsWith("features=")) in.features = QString::fromLatin1(line.mid(9)).trimmed().split(',', Qt::SkipEmptyParts);
    }
    return in;
}

NvEnhancer::NvEnhancer() {}
NvEnhancer::~NvEnhancer() { shutdown(); }

void NvEnhancer::setInstall(const Install& in)
{
    if (m_state != Off) stop(false, QString());
    m_install = in;
    m_state = Off;
    forgive();
}

void NvEnhancer::forgive()
{
    m_failCount = 0;
    m_failedCfg = Config();
    m_restarts = 0;
    if (m_state == Fatal || m_state == Dead) { m_state = Off; m_error.clear(); }
}

QString NvEnhancer::stateName() const
{
    switch (m_state) {
    case Off: return QStringLiteral("off");
    case Starting: return QStringLiteral("starting");
    case Idle: return QStringLiteral("idle");
    case Opening: return QStringLiteral("opening");
    case Ready: return QStringLiteral("ready");
    case Dead: return QStringLiteral("stopped");
    case Fatal: return QStringLiteral("failed");
    }
    return {};
}

QJsonObject NvEnhancer::report() const
{
    return QJsonObject{{"helper", m_install.helper}, {"sdk", m_install.sdk}, {"sdkVersion", m_sdkVersion},
                       {"superRes", m_install.superRes()}, {"frameGen", m_install.frameGen()}, {"usable", m_install.usable()},
                       {"state", stateName()}, {"error", m_error}, {"lastError", m_lastError},
                       {"srcWidth", m_cfg.src.width()}, {"srcHeight", m_cfg.src.height()}, {"outWidth", m_cfg.out.width()}, {"outHeight", m_cfg.out.height()},
                       {"quality", m_cfg.quality}, {"mode", m_cfg.mode}, {"onCard", m_card},
                       {"frames", double(m_frames)}, {"pictures", double(m_pictures)}, {"generated", double(m_generated)},
                       {"opens", double(m_opens)}, {"failures", double(m_failures)}, {"restarts", m_restarts},
                       {"frameMs", m_frameMs}, {"pictureMs", m_getMs}, {"waitMs", m_waitMs}, {"loadMs", m_loadMs}};
}

void NvEnhancer::releaseShm()
{
    if (m_shm) munmap(m_shm, m_shmSize);
    m_shm = nullptr;
    m_shmSize = 0;
    if (!m_shmName.isEmpty()) shm_unlink(m_shmName.constData());
    m_shmName.clear();
}

bool NvEnhancer::start()
{
    if (!m_install.usable()) { m_state = Fatal; m_error = QStringLiteral("not installed"); return false; }
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0) { m_state = Fatal; m_error = QString::fromLocal8Bit(std::strerror(errno)); return false; }
    // What the helper's libraries print goes to a file (the last runs only).
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cache);
    const QByteArray log = QFile::encodeName(cache + QStringLiteral("/nvfx.log"));
    struct stat st;
    const bool fresh = stat(log.constData(), &st) != 0 || st.st_size > 512 * 1024;

    const QByteArray helper = QFile::encodeName(m_install.helper), sdk = QFile::encodeName(m_install.sdk);
    std::vector<QByteArray> envStore;
    const QByteArray appDir = qgetenv("APPDIR");
    for (char** e = environ; e && *e; ++e) {
        QByteArray v(*e);
        if (v.startsWith("CRTPLAYER_NVFX_STARTED=")) continue;
        if (v.startsWith("LD_LIBRARY_PATH=") && !appDir.isEmpty()) {
            // (an AppImage's own libraries are not for the SDK)
            QList<QByteArray> keep;
            for (const QByteArray& part : v.mid(16).split(':'))
                if (!part.isEmpty() && !part.startsWith(appDir)) keep << part;
            if (keep.isEmpty()) continue;
            v = "LD_LIBRARY_PATH=" + keep.join(':');
        }
        envStore.push_back(v);
    }
    std::vector<char*> envp;
    for (QByteArray& v : envStore) envp.push_back(v.data());
    envp.push_back(nullptr);
    QByteArray a0 = helper, a1 = "--serve", a2 = "--sdk", a3 = sdk;
    char* argv[] = {a0.data(), a1.data(), a2.data(), a3.data(), nullptr};

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, sv[1], 0);
    posix_spawn_file_actions_adddup2(&fa, sv[1], 1);
    posix_spawn_file_actions_addopen(&fa, 2, log.constData(), O_WRONLY | O_CREAT | (fresh ? O_TRUNC : O_APPEND), 0644);
    pid_t pid = -1;
    const int rc = posix_spawn(&pid, helper.constData(), &fa, nullptr, argv, envp.data());
    posix_spawn_file_actions_destroy(&fa);
    ::close(sv[1]);
    if (rc != 0) {
        ::close(sv[0]);
        m_state = Fatal;
        m_error = QStringLiteral("the helper could not be started: ") + QString::fromLocal8Bit(std::strerror(rc));
        return false;
    }
    m_pid = pid;
    m_fd = sv[0];
    m_inbuf.clear();
    m_pending.clear();
    m_results.clear();
    m_slotTicket[0] = m_slotTicket[1] = 0;
    m_slotTaken[0] = m_slotTaken[1] = false;
    m_frameAck = 0;
    m_state = Starting;
    m_error.clear();
    return sendLine(0, "hello");
}

// The helper is let go. kill: at once (it is stuck or the connection is broken).
void NvEnhancer::stop(bool kill, const QString& why)
{
    if (m_fd >= 0) {
        if (!kill) { const char q[] = "0 quit\n"; (void)::send(m_fd, q, sizeof q - 1, MSG_NOSIGNAL | MSG_DONTWAIT); }
        ::close(m_fd);
    }
    m_fd = -1;
    if (m_pid > 0) {
        if (kill) ::kill(pid_t(m_pid), SIGKILL);
        int status = 0;
        // (it leaves when the connection closes; a helper that does not is killed)
        for (int i = 0; i < 40 && waitpid(pid_t(m_pid), &status, WNOHANG) == 0; ++i) {
            if (i == 20) ::kill(pid_t(m_pid), SIGKILL);
            usleep(5000);
        }
    }
    m_pid = -1;
    releaseShm();
    m_pending.clear();
    m_results.clear();
    m_slotTicket[0] = m_slotTicket[1] = 0;
    m_slotTaken[0] = m_slotTaken[1] = false;
    m_frameAck = 0;
    m_cfg = Config();
    m_lastEpoch = m_prevEpoch = 0;
    if (!why.isEmpty()) {
        m_error = m_lastError = why;
        ++m_failures;
        ++m_restarts;
        m_state = m_restarts >= 4 ? Fatal : Dead;
        m_deadSince.start();
        qWarning("NVIDIA helper: %s", qPrintable(why));
    } else m_state = Off;
}

void NvEnhancer::shutdown()
{
    if (m_state == Off && m_fd < 0) return;
    const State was = m_state;
    const QString err = m_error;
    stop(false, QString());
    if (was == Fatal) { m_state = Fatal; m_error = err; }
}

bool NvEnhancer::sendLine(int what, const QByteArray& text, int* idOut)
{
    if (m_fd < 0) return false;
    const int id = m_nextId++;
    if (m_nextId > 1000000000) m_nextId = 1;
    const QByteArray line = QByteArray::number(id) + ' ' + text + '\n';
    qsizetype done = 0;
    while (done < line.size()) {
        const ssize_t n = ::send(m_fd, line.constData() + done, size_t(line.size() - done), MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            stop(true, QStringLiteral("the helper has stopped"));
            return false;
        }
        done += n;
    }
    m_pending.push_back({id, what});
    if (idOut) *idOut = id;
    return true;
}

void NvEnhancer::pipelineFailed(const QString& why)
{
    // The effects are closed; the helper stays for another try (later, or with other settings).
    m_failedCfg = m_cfg.valid() ? m_cfg : m_opening;
    m_failedSince.start();
    ++m_failCount;
    ++m_failures;
    m_error = m_lastError = why;
    qWarning("NVIDIA effects: %s", qPrintable(why));
    m_cfg = Config();
    m_lastEpoch = m_prevEpoch = 0;
    if (m_state == Ready) { m_state = Idle; sendLine(4, "close"); }
    else if (m_state == Opening) m_state = Idle;
}

void NvEnhancer::handleLine(const QByteArray& line)
{
    const int sp = line.indexOf(' ');
    if (sp <= 0 || m_pending.empty() || line.left(sp).toInt() != m_pending.front().id) {
        stop(true, QStringLiteral("the helper answered out of turn: ") + QString::fromLocal8Bit(line.left(120)));
        return;
    }
    const Pending p = m_pending.front();
    m_pending.pop_front();
    QByteArray rest = line.mid(sp + 1);
    const bool ok = rest == "ok" || rest.startsWith("ok ");
    rest = rest.mid(ok ? 3 : 4);   // ("ok " / "err ")
    switch (p.what) {
    case 0:   // hello
        if (ok && int(field(rest, "proto")) == kNvProtocol) {
            m_sdkVersion = QString::fromLatin1(fieldText(rest, "sdk"));
            if (m_state == Starting) m_state = Idle;
        } else {
            const QString why = ok ? QStringLiteral("the helper is of another version than the player") : QString::fromLocal8Bit(rest);
            stop(false, QString());
            m_state = Fatal;
            m_error = m_lastError = why;
            ++m_failures;
            qWarning("NVIDIA helper: %s", qPrintable(why));
        }
        break;
    case 1:   // open
        if (!m_shmName.isEmpty()) { shm_unlink(m_shmName.constData()); m_shmName.clear(); }   // (both sides have it now, or it is not needed)
        if (m_state != Opening) break;
        if (ok) {
            m_cfg = m_opening;
            m_card = int(field(rest, "card")) != 0;
            m_loadMs = m_openTimer.elapsed();
            m_lastEpoch = m_prevEpoch = 0;
            m_frameMs = m_getMs = m_waitMs = 0;
            ++m_generation;
            ++m_opens;
            m_state = Ready;
            m_error.clear();
        } else pipelineFailed(QString::fromLocal8Bit(rest));
        break;
    case 2:   // frame
        if (m_frameAck == p.id) m_frameAck = 0;
        if (ok) mean(m_frameMs, field(rest, "ms"));
        else if (m_state == Ready) pipelineFailed(QString::fromLocal8Bit(rest));
        break;
    case 3: { // get
        auto it = m_results.find(p.id);
        if (it == m_results.end()) break;   // (abandoned)
        it->ok = ok;
        it->delivered = true;
        if (ok) {
            const QByteArray k = fieldText(rest, "kind");
            it->kind = k.isEmpty() ? '?' : k.at(0);
            mean(m_getMs, field(rest, "ms"));
            ++m_pictures;
            if (it->kind == 'g') ++m_generated;
        } else if (m_state == Ready) pipelineFailed(QString::fromLocal8Bit(rest));
        break;
    }
    default: break;
    }
}

// Reads what has arrived (waiting up to timeoutMs for something to). False: the helper is gone.
bool NvEnhancer::readAvailable(int timeoutMs)
{
    if (m_fd < 0) return false;
    if (timeoutMs > 0) {
        pollfd pfd{m_fd, POLLIN, 0};
        int r;
        do { r = ::poll(&pfd, 1, timeoutMs); } while (r < 0 && errno == EINTR);
        if (r == 0) return true;
    }
    char buf[4096];
    for (;;) {
        const ssize_t n = ::recv(m_fd, buf, sizeof buf, MSG_DONTWAIT);
        if (n > 0) { m_inbuf.append(buf, int(n)); if (n < ssize_t(sizeof buf)) break; continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        stop(true, m_state == Starting ? QStringLiteral("the helper stopped while starting (see nvfx.log)") : QStringLiteral("the helper has stopped (see nvfx.log)"));
        return false;
    }
    int nl;
    while (m_fd >= 0 && (nl = m_inbuf.indexOf('\n')) >= 0) {
        const QByteArray line = m_inbuf.left(nl);
        m_inbuf.remove(0, nl + 1);
        handleLine(line);
    }
    return m_fd >= 0;
}

bool NvEnhancer::poll()
{
    if (m_fd < 0) return false;
    const State before = m_state;
    readAvailable(0);
    if (m_state == Starting && m_openTimer.isValid() && m_openTimer.elapsed() > 20000) stop(true, QStringLiteral("the helper did not start"));
    if (m_state == Opening && m_openTimer.elapsed() > 30000) stop(true, QStringLiteral("the effects did not open"));
    return m_state != before;
}

// Until the answer to request `id` has been read. False: it will not come.
bool NvEnhancer::waitFor(int id, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    auto outstanding = [this, id] {
        for (const Pending& p : m_pending) if (p.id == id) return true;
        return false;
    };
    while (outstanding()) {
        const int left = timeoutMs - int(t.elapsed());
        if (left <= 0) { stop(true, QStringLiteral("the helper did not answer")); return false; }
        if (!readAvailable(left)) return false;
    }
    return true;
}

bool NvEnhancer::openConfig(const Config& c)
{
    releaseShm();
    const NvShmLayout l = nvShmLayout(c.src.width(), c.src.height(), c.out.width(), c.out.height());
    static int serial = 0;
    m_shmName = "/crtplayer-nvfx-" + QByteArray::number(qint64(getpid())) + '-' + QByteArray::number(++serial);
    shm_unlink(m_shmName.constData());
    const int fd = shm_open(m_shmName.constData(), O_CREAT | O_EXCL | O_RDWR, 0600);
    QString why;
    if (fd < 0) why = QStringLiteral("no shared memory: ") + QString::fromLocal8Bit(std::strerror(errno));
    else if (const int e = posix_fallocate(fd, 0, off_t(l.total))) why = QStringLiteral("no shared memory for the pictures: ") + QString::fromLocal8Bit(std::strerror(e));
    void* p = MAP_FAILED;
    if (why.isEmpty()) {
        p = mmap(nullptr, l.total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) why = QStringLiteral("the shared memory could not be mapped: ") + QString::fromLocal8Bit(std::strerror(errno));
    }
    if (fd >= 0) ::close(fd);
    m_opening = c;
    if (!why.isEmpty()) {
        shm_unlink(m_shmName.constData());
        m_shmName.clear();
        m_state = Opening;
        pipelineFailed(why);
        return false;
    }
    m_shm = static_cast<uchar*>(p);
    m_shmSize = l.total;
    m_inOff = l.input;
    m_slotOff[0] = l.slot[0];
    m_slotOff[1] = l.slot[1];
    m_cfg = Config();
    m_results.clear();
    m_slotTicket[0] = m_slotTicket[1] = 0;
    m_slotTaken[0] = m_slotTaken[1] = false;
    m_openTimer.start();
    const QByteArray cmd = "open " + m_shmName + ' ' + QByteArray::number(c.src.width()) + ' ' + QByteArray::number(c.src.height()) + ' ' +
                           QByteArray::number(c.out.width()) + ' ' + QByteArray::number(c.out.height()) + ' ' +
                           QByteArray::number(c.quality) + ' ' + QByteArray::number(c.mode);
    if (!sendLine(1, cmd)) return false;
    m_state = Opening;
    return true;
}

bool NvEnhancer::ready(const Config& want)
{
    if (!want.valid() || !m_install.usable() || m_state == Fatal) return false;
    if (m_state == Ready && m_cfg == want) return true;
    if (m_fd >= 0) poll();
    if (m_state == Ready && m_cfg == want) return true;
    if (want != m_want) { m_want = want; m_wantSince.start(); }
    if (m_state == Starting || m_state == Opening || m_state == Fatal) return false;
    if (m_state == Dead) {
        if (m_deadSince.elapsed() < 3000) return false;
        m_state = Off;
    }
    if (want == m_failedCfg && (m_failCount >= 3 || m_failedSince.elapsed() < 5000)) return false;
    if (m_state == Off) { m_openTimer.start(); start(); return false; }
    // (a window being resized asks for a new size at every draw: the effects are opened for the size it settles at)
    if (m_state == Ready && m_wantSince.elapsed() < 250) return false;
    openConfig(want);
    return false;
}

uchar* NvEnhancer::beginFrame()
{
    if (m_state != Ready || !m_shm) return nullptr;
    if (m_frameAck && !waitFor(m_frameAck, kFrameTimeoutMs)) return nullptr;   // (the helper may still be reading the frame before)
    return m_state == Ready && m_shm ? m_shm + m_inOff : nullptr;
}

bool NvEnhancer::endFrame(quint64 epoch, bool cut)
{
    if (m_state != Ready) return false;
    int id = 0;
    if (!sendLine(2, cut ? "frame 1" : "frame 0", &id)) return false;
    m_frameAck = id;
    m_prevEpoch = m_lastEpoch;
    m_lastEpoch = epoch;
    ++m_frames;
    return true;
}

int NvEnhancer::request(double t)
{
    if (m_state != Ready || !m_shm) return 0;
    // A slot is free when nothing was asked into it, or when its picture has been taken.
    int slot = -1;
    for (int s = 0; s < 2 && slot < 0; ++s) if (!m_slotTicket[s]) slot = s;
    for (int s = 0; s < 2 && slot < 0; ++s) if (m_slotTaken[s]) slot = s;
    if (slot < 0) return 0;
    int id = 0;
    if (!sendLine(3, "get " + QByteArray::number(slot) + ' ' + QByteArray::number(qBound(0.0, t, 1.0), 'f', 4), &id)) return 0;
    m_results.remove(m_slotTicket[slot]);
    m_slotTicket[slot] = id;
    m_slotTaken[slot] = false;
    Result r;
    r.slot = slot;
    m_results.insert(id, r);
    return id;
}

const uchar* NvEnhancer::wait(int ticket, char* kind)
{
    if (!ticket || !m_results.contains(ticket)) return nullptr;
    QElapsedTimer t;
    t.start();
    if (!m_results.value(ticket).delivered && !waitFor(ticket, kFrameTimeoutMs)) return nullptr;
    const auto it = m_results.constFind(ticket);
    if (it == m_results.constEnd() || !it->ok || m_state != Ready || !m_shm) return nullptr;
    mean(m_waitMs, t.nsecsElapsed() / 1e6);
    if (kind) *kind = it->kind;
    m_slotTaken[it->slot] = true;
    return m_shm + m_slotOff[it->slot];
}

void NvEnhancer::abandon(int ticket)
{
    const auto it = m_results.constFind(ticket);
    if (it == m_results.constEnd()) return;
    const int slot = it->slot;
    m_results.remove(ticket);
    if (slot >= 0 && m_slotTicket[slot] == ticket) { m_slotTicket[slot] = 0; m_slotTaken[slot] = false; }
}

#else   // ---- other systems: not there

QString NvEnhancer::findHelper() { return {}; }
NvEnhancer::Install NvEnhancer::findInstall() { return {}; }
NvEnhancer::NvEnhancer() {}
NvEnhancer::~NvEnhancer() {}
void NvEnhancer::setInstall(const Install& in) { m_install = in; }
bool NvEnhancer::ready(const Config&) { return false; }
bool NvEnhancer::poll() { return false; }
void NvEnhancer::forgive() {}
void NvEnhancer::shutdown() {}
uchar* NvEnhancer::beginFrame() { return nullptr; }
bool NvEnhancer::endFrame(quint64, bool) { return false; }
int NvEnhancer::request(double) { return 0; }
const uchar* NvEnhancer::wait(int, char*) { return nullptr; }
void NvEnhancer::abandon(int) {}
QString NvEnhancer::stateName() const { return QStringLiteral("off"); }
QJsonObject NvEnhancer::report() const { return QJsonObject{{"usable", false}, {"state", "off"}}; }
bool NvEnhancer::start() { return false; }
void NvEnhancer::stop(bool, const QString&) {}
bool NvEnhancer::sendLine(int, const QByteArray&, int*) { return false; }
bool NvEnhancer::readAvailable(int) { return false; }
void NvEnhancer::handleLine(const QByteArray&) {}
bool NvEnhancer::waitFor(int, int) { return false; }
bool NvEnhancer::openConfig(const Config&) { return false; }
void NvEnhancer::releaseShm() {}
void NvEnhancer::pipelineFailed(const QString&) {}

#endif
