#include "GifRecorder.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <cmath>
#include <thread>

GifRecorder::GifRecorder(const Host& host, QObject* parent) : QObject(parent), m_host(host)
{
    qRegisterMetaType<GifRecorder::Result>();
    connect(&m_timer, &QTimer::timeout, this, &GifRecorder::tick);
    m_timer.setInterval(2);
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

bool GifRecorder::start(qint64 startNs, qint64 endNs, int fps, const QSize& box, const QString& path)
{
    if (m_state != State::Idle) return false;
    m_r = Result();
    m_r.path = path;
    m_start = std::max<qint64>(0, startNs);
    m_end = endNs;
    if (m_end - m_start > kMaxLengthNs) { m_end = m_start + kMaxLengthNs; m_r.truncated = true; }
    if (m_end <= m_start) { finish(false, tr("The end must come after the start.")); return false; }
    m_fps = std::clamp(fps, 5, 60);
    m_box = QSize(std::clamp(box.width(), 120, 7680), std::max(0, box.height()));
    m_r.startNs = m_start;
    m_r.endNs = m_end;
    m_held = QImage();
    m_heldPts = m_lastPts = -1;
    m_writtenCs = 0;
    m_nextTarget = double(m_start);
    m_enc.reset(new GifEncoder);
    m_wasPlaying = m_host.isPlaying();
    m_clock0 = m_host.effectClock();
    m_total.start();
    m_serial = m_host.frameSerial();
    m_state = State::Seeking;
    m_wait.start();
    m_host.seekPaused(m_start);
    m_timer.start();
    return true;
}

void GifRecorder::cancel()
{
    if (m_state == State::Idle || m_state == State::Encoding) return;   // (encoding finishes on its own)
    m_timer.stop();
    if (m_enc) m_enc->abort();
    m_host.restore(m_wasPlaying);
    finish(false, tr("Cancelled"));
}

void GifRecorder::fail(const QString& error)
{
    m_timer.stop();
    if (m_enc) m_enc->abort();
    m_host.restore(m_wasPlaying);
    finish(false, error);
}

void GifRecorder::tick()
{
    switch (m_state) {
    case State::Seeking: {
        // Until the seek has landed on a frame at the start.
        const qint64 pts = m_host.framePts();
        const bool landed = !m_host.isSeeking() && m_host.frameSerial() != m_serial && pts >= m_start - 100'000'000 &&
                            pts <= m_start + 1'500'000'000;
        if (landed && m_wait.elapsed() > 150) { m_state = State::Frame; break; }
        if (m_wait.elapsed() > 15000) fail(tr("The video didn't get to the start of the section."));
        break;
    }
    case State::Frame:
        // The encoder works in the background; don't pile up frames faster than it writes them.
        if (m_enc->pendingFrames() > 8) break;
        takeFrame();
        break;
    case State::Stepping:
        if (m_host.frameSerial() != m_serial && !m_host.isSeeking()) {
            const qint64 pts = m_host.framePts();
            if (pts <= m_lastPts) {   // not a later frame: the end of the video
                stopRecording();
                break;
            }
            m_state = State::Frame;
        } else if (m_wait.elapsed() > 4000) {
            // No further frame: the end of the video (or a stream that can't be stepped).
            if (m_r.frames == 0 && m_held.isNull()) fail(tr("This video can't be stepped through frame by frame."));
            else stopRecording();
        }
        break;
    default:
        break;
    }
}

void GifRecorder::takeFrame()
{
    const qint64 pts = m_host.framePts();
    m_lastPts = pts;
    if (pts >= m_end) { stopRecording(); return; }
    // One GIF frame every 1/fps of video time: this frame is taken if it is the one on
    // screen at the next due time (or the first).
    if (m_held.isNull() || double(pts) >= m_nextTarget - 1e6) {
        if (m_r.width == 0) {
            const QSize size = m_host.pictureSize(m_box);
            if (size.isEmpty()) { fail(tr("There is no picture to record.")); return; }
            m_r.width = size.width();
            m_r.height = size.height();
            if (!m_enc->open(m_r.path, m_r.width, m_r.height)) { fail(tr("Could not create %1").arg(m_r.path)); return; }
        }
        const QImage img = m_host.render(QSize(m_r.width, m_r.height), m_clock0 + (pts - m_start) / 1e9);
        if (img.isNull()) { fail(tr("The picture could not be rendered at this size.")); return; }
        if (!m_held.isNull()) {
            // The held frame showed from its own time until this one's.
            const qint64 untilCs = std::llround((pts - m_start) / 1e7);
            m_enc->addFrame(m_held, int(std::max<qint64>(2, untilCs - m_writtenCs)));
            m_writtenCs = std::max(untilCs, m_writtenCs + 2);
            ++m_r.frames;
        }
        m_held = img;
        m_heldPts = pts;
        const double period = 1e9 / m_fps;
        while (m_nextTarget <= double(pts) + 1e6) m_nextTarget += period;
        emit progress((pts - m_start) / 1e9, (m_end - m_start) / 1e9, m_r.frames, QFileInfo(m_r.path).size());
    }
    m_serial = m_host.frameSerial();
    m_state = State::Stepping;
    m_wait.restart();
    m_host.step();
}

void GifRecorder::stopRecording()
{
    m_timer.stop();
    if (m_held.isNull()) { fail(tr("No frames were recorded.")); return; }
    // The last frame shows until the end of the section (or of the video).
    const qint64 endPts = std::min(m_end, std::max(m_lastPts, m_heldPts + qint64(1e9 / m_fps)));
    const qint64 untilCs = std::llround((endPts - m_start) / 1e7);
    m_enc->addFrame(m_held, int(std::clamp<qint64>(untilCs - m_writtenCs, 2, 200)));
    m_writtenCs = std::max(untilCs, m_writtenCs + 2);
    ++m_r.frames;
    m_r.seconds = m_writtenCs / 100.0;
    m_r.fps = m_r.seconds > 0 ? m_r.frames / m_r.seconds : 0;
    m_held = QImage();
    m_state = State::Encoding;
    m_host.restore(m_wasPlaying);
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
    m_r.tookSeconds = m_total.isValid() ? m_total.elapsed() / 1000.0 : 0;
    m_enc.reset();
    emit finished(m_r);
}
