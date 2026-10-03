#include "GifRecorder.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <thread>

GifRecorder::GifRecorder(const Host& host, QObject* parent) : QObject(parent), m_host(host)
{
    qRegisterMetaType<GifRecorder::Result>();
    connect(&m_timer, &QTimer::timeout, this, &GifRecorder::tick);
    m_timer.setTimerType(Qt::PreciseTimer);
}

GifRecorder::~GifRecorder()
{
    m_timer.stop();
    if (m_enc) m_enc->abort();
}

QString GifRecorder::pathFor(const QString& dir, const QString& titleIn, qint64 startNs, qint64 endNs)
{
    QString title = titleIn.isEmpty() ? QStringLiteral("clip") : titleIn;
    title.replace(QRegularExpression("[/\\\\:*?\"<>|]"), "_");
    auto stamp = [](qint64 ns) {
        const qint64 ms = ns / 1000000;
        return QString::asprintf("%02lld-%02lld-%02lld.%01lld", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, (ms % 1000) / 100);
    };
    const QString stem = QStringLiteral("%1_%2_to_%3").arg(title, stamp(startNs), stamp(endNs));
    QString path = QDir(dir).filePath(stem + ".gif");
    for (int n = 2; QFileInfo::exists(path); ++n) path = QDir(dir).filePath(QStringLiteral("%1 (%2).gif").arg(stem).arg(n));
    return path;
}

bool GifRecorder::start(qint64 startNs, qint64 endNs, int fps, int width, const QString& path)
{
    if (m_state != State::Idle) return false;
    m_r = Result();
    m_r.path = path;
    m_start = std::max<qint64>(0, startNs);
    m_end = endNs;
    if (m_end - m_start > kMaxLengthNs) { m_end = m_start + kMaxLengthNs; m_r.truncated = true; }
    if (m_end <= m_start) { finish(false, tr("The end must come after the start.")); return false; }
    m_fps = std::clamp(fps, 5, 30);
    m_width = std::clamp(width, 120, 3840);
    m_r.startNs = m_start;
    m_r.endNs = m_end;
    m_lastPos = m_heldPos = -1;
    m_held = QImage();
    m_enc.reset(new GifEncoder);
    m_state = State::Waiting;
    m_clock.start();
    m_host.seekAndPlay(m_start);
    m_timer.start(1000 / m_fps);
    return true;
}

void GifRecorder::cancel()
{
    if (m_state == State::Idle || m_state == State::Encoding) return;   // (encoding finishes on its own)
    m_timer.stop();
    if (m_enc) m_enc->abort();
    finish(false, tr("Cancelled"));
}

void GifRecorder::tick()
{
    const qint64 pos = m_host.position();
    if (m_state == State::Waiting) {
        // Until the seek has landed and the video is moving from the start.
        if (m_clock.elapsed() > 15000) { m_timer.stop(); m_enc->abort(); finish(false, tr("The video didn't start playing.")); return; }
        if (!m_host.isPlaying() || pos < m_start - 60000000 || pos > m_start + 1500000000) return;
        m_state = State::Recording;
        m_clock.restart();
    }
    if (pos >= m_end || (m_lastPos >= 0 && pos < m_lastPos - 500000000) || !m_host.isPlaying() ||
        m_clock.elapsed() > (m_end - m_start) / 1000000 * 3 + 10000) {
        stopRecording();
        return;
    }
    if (pos == m_lastPos) return;   // no new picture yet
    m_lastPos = pos;
    QImage img = m_host.grab();
    if (img.isNull()) return;
    if (!m_enc || m_r.width == 0) {
        m_r.width = m_width & ~1;
        m_r.height = std::max(2, int(std::lround(double(img.height()) * m_r.width / std::max(1, img.width())))) & ~1;
        if (!m_enc->open(m_r.path, m_r.width, m_r.height)) {
            m_timer.stop();
            finish(false, tr("Could not create %1").arg(m_r.path));
            return;
        }
    }
    if (!m_held.isNull()) {
        const int cs = int(std::lround((pos - m_heldPos) / 1e7));
        m_enc->addFrame(m_held, std::max(2, cs));
        ++m_r.frames;
    }
    m_held = img;
    m_heldPos = pos;
    emit progress((pos - m_start) / 1e9, (m_end - m_start) / 1e9);
}

void GifRecorder::stopRecording()
{
    m_timer.stop();
    if (m_held.isNull()) { if (m_enc) m_enc->abort(); finish(false, tr("No frames were recorded.")); return; }
    // The last frame shows until the end of the section.
    const int cs = int(std::lround((std::max(m_end, m_heldPos) - m_heldPos) / 1e7));
    m_enc->addFrame(m_held, std::clamp(cs, 2, 100 / m_fps + 2));
    ++m_r.frames;
    m_r.seconds = (std::min(m_end, std::max(m_heldPos, m_lastPos)) - m_start) / 1e9;
    m_held = QImage();
    m_state = State::Encoding;
    emit encoding(m_enc->pendingFrames());
    // Waiting for the encoder happens off the GUI thread.
    GifEncoder* enc = m_enc.get();
    std::thread([this, enc] {
        const bool ok = enc->close();
        QMetaObject::invokeMethod(this, [this, ok] {
            m_r.bytes = QFileInfo(m_r.path).size();
            finish(ok, ok ? QString() : tr("Could not write %1").arg(m_r.path));
        }, Qt::QueuedConnection);
    }).detach();
}

void GifRecorder::finish(bool ok, const QString& error)
{
    m_state = State::Idle;
    m_r.ok = ok;
    m_r.error = error;
    m_enc.reset();
    emit finished(m_r);
}
