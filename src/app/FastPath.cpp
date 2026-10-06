// Playback without a graphics card (2.12), as part of MainWindow.
//
// With a software OpenGL renderer (no GPU, or a virtual machine without 3D acceleration)
// every pixel the shaders touch is computed on the CPU by a general-purpose rasteriser, and
// the colour conversion alone, at the video's own size, took far longer than decoding it.
// With effects off, the player therefore has the video pipeline deliver frames already
// converted to RGB and scaled to the size they are shown at. That work runs in the
// pipeline's threads on all CPU cores with SIMD code; drawing is then a plain copy.
#include "MainWindow.h"

#include "playback/Player.h"
#include "render/VideoWidget.h"
#include "ui/PlaybackPanel.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>

bool MainWindow::fastPathWanted() const
{
    if (m_fastHeld || m_settings.videoPath == 2) return false;
    // (Enhance works on the frames as decoded: upscaling wants the video's own pixels.)
    if ((m_settings.enhanceUpscale || m_settings.smoothMotion) && m_video->enhanceAvailable()) return false;
    if (m_settings.videoPath == 0 && !m_video->softwareRenderer()) return false;
    // (A GIF is made from frames as decoded, stepped one by one.)
    return !m_gifRecording && m_video->uprightSource();
}

// Where to seek to have the frame on screen delivered again: just inside that frame. (The
// player's position while paused can lie a frame to either side of the one shown.)
qint64 MainWindow::shownFrameTime() const
{
    const qint64 t = m_player->lastFrameStreamTime();
    return t >= 0 ? t + 1000000 : m_player->position();
}

void MainWindow::updateVideoPath()
{
    if (!m_player->hasMedia()) return;
    const Player::Output was = m_player->output();
    Player::Output now = Player::Output::AsDecoded;
    QSize size;
    bool plain = false;
    if (fastPathWanted() && m_deskActive) {
        // Desk mode draws the set's screen from the frame at the video's own size: converted on the
        // CPU, never scaled (the same frames as the flat view with a look, so flying in lands on it).
        now = Player::Output::Rgb;
    } else if (fastPathWanted()) {
        plain = m_video->bypass() && !m_video->compare();
        // Effects off: exactly the size shown, so that drawing is a straight copy. With a look: the
        // video's own size (the look is drawn from the same pixels as ever), unless the video is
        // much larger than its picture on screen (4K in an HD window), where the detail could not show.
        size = plain ? m_video->fastTargetSize() : m_video->lookTargetSize();
        now = size.isEmpty() ? Player::Output::Rgb : Player::Output::RgbScaled;
    }
    // The look's size: half when the window is large and there is no graphics card (a quarter of the pixels to compute).
    double lookScale = 1.0;
    // (Only the plain window surface can show a smaller drawing enlarged.)
    if (m_video->softwareRenderer() && m_settings.videoPath != 2 && m_video->surfaceMode() == GlSurfaceWidget::Mode::Raster) {
        const QSize full(int(m_video->width() * m_video->devicePixelRatioF()), int(m_video->height() * m_video->devicePixelRatioF()));
        if (m_settings.lookDetail == 2 || (m_settings.lookDetail == 0 && full.height() > 800)) lookScale = 0.5;
    }
    m_video->setLookScale(lookScale);
    m_video->setPlainOnly(plain && now != Player::Output::AsDecoded);
    const QSize wasSize = m_player->fastOutput();
    m_player->setOutput(now, size);
    // Paused, the frame on screen stays as it was delivered. When it is not what is now asked for
    // (a frame scaled for a smaller window, after going fullscreen; a scaled frame, entering desk
    // mode or going back to frames as decoded), it is fetched again. Once per change.
    if ((was != now || wasSize != m_player->fastOutput()) && m_player->state() == Player::State::Paused &&
        !m_player->isSeeking() && m_video->hasFrame()) {
        int nw = 0, nh = 0, pn = 1, pd = 1;
        const bool known = m_player->nativeFormat(&nw, &nh, &pn, &pd);
        const QSize have = m_video->frameSize();
        bool stale = false;
        if (now == Player::Output::AsDecoded) stale = m_video->frameIsScaled();
        else if (now == Player::Output::Rgb) stale = known && have != QSize(nw, nh);
        else stale = have != m_player->fastOutput();
        // (The frame as decoded is usually still at hand; a seek for it would lose a subtitle line
        // already on screen, which the file does not send again.)
        if (stale && !m_video->showNativeFrame()) m_player->seek(shownFrameTime(), Player::SeekMode::Accurate);
    }
    if (was != now) updateInfoOverlay();
    applyEnhance();   // (what is available, and the sound's delay, follow the video)
}

// The frame as decoded is needed (an original-frame screenshot): the fast path is held off
// until releaseNativeFrame(), and a full-size frame is waited for. False if none came.
bool MainWindow::ensureNativeFrame()
{
    ++m_fastHold;
    // Usually the frame as decoded is still at hand, and takes the place of the one on screen.
    if (!m_video->frameIsScaled() || m_video->showNativeFrame()) return true;
    m_fastHeld = true;   // otherwise it is asked for from the pipeline
    updateVideoPath();
    if (!m_video->frameIsScaled()) return true;
    if (!m_player->isPlaying() && !m_player->isSeeking()) m_player->seek(shownFrameTime(), Player::SeekMode::Accurate);
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 4000 && m_video->frameIsScaled()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
        m_video->pullFrame();
    }
    return !m_video->frameIsScaled();
}

void MainWindow::releaseNativeFrame()
{
    m_fastHold = std::max(0, m_fastHold - 1);
    if (m_fastHold == 0) m_fastHeld = false;
}

void MainWindow::setLookDetail(int mode)
{
    m_settings.lookDetail = std::clamp(mode, 0, 2);
    m_playbackPanel->setLookDetail(m_settings.lookDetail);
    updateVideoPath();
}

void MainWindow::setVideoPath(int mode)
{
    m_settings.videoPath = std::clamp(mode, 0, 2);
    m_playbackPanel->setVideoPath(m_settings.videoPath);
    updateVideoPath();
}

QString MainWindow::videoPathDescription() const
{
    const QSize fast = m_player->fastOutput();
    QString s = m_video->softwareRenderer() ? tr("no graphics acceleration (software OpenGL)") : tr("graphics card");
    if (!fast.isEmpty()) s += tr("; converted and scaled to %1×%2 on %3 CPU threads").arg(fast.width()).arg(fast.height()).arg(QThread::idealThreadCount());
    else if (m_player->output() == Player::Output::Rgb) s += tr("; converted on %1 CPU threads").arg(QThread::idealThreadCount());
    return s;
}

QJsonObject MainWindow::videoPathReport() const
{
    const QSize fast = m_player->fastOutput(), frame = m_video->frameSize();
    const Player::Output o = m_player->output();
    return QJsonObject{{"software", m_video->softwareRenderer()}, {"setting", m_settings.videoPath}, {"fast", o != Player::Output::AsDecoded},
                       {"output", o == Player::Output::AsDecoded ? "as decoded" : o == Player::Output::Rgb ? "rgb" : "rgb scaled"},
                       {"surface", m_video->surfaceMode() == GlSurfaceWidget::Mode::Raster ? "raster" : "gl widget"},
                       {"lookScale", m_video->lookScale()},
                       {"fastSize", QJsonArray{fast.width(), fast.height()}}, {"frameSize", QJsonArray{frame.width(), frame.height()}},
                       {"frameDirect", m_video->frameDirect()}, {"frameScaled", m_video->frameIsScaled()},
                       {"sinkForeignMemory", m_player->sinkForeignMemoryEntries()}, {"decoderThreads", m_player->decoderThreads()}, {"cpuThreads", QThread::idealThreadCount()},
                       {"shrink", m_player->shrinkReport()}, {"governor", m_player->governorReport()}};
}
