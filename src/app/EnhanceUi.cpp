// Enhance (2.13), as part of MainWindow: sharper upscaling and frame generation, for a
// graphics card. The work itself is in render/Enhance.cpp.
#include "MainWindow.h"

#include "playback/Player.h"
#include "render/VideoWidget.h"
#include "ui/PlaybackPanel.h"

#include <cmath>

void MainWindow::setEnhanceUpscale(bool on)
{
    m_settings.enhanceUpscale = on;
    m_playbackPanel->setEnhance(m_settings.enhanceUpscale, m_settings.enhanceSharpness, m_settings.smoothMotion);
    applyEnhance();
    updateVideoPath();
    showOsd(on ? tr("Sharper upscaling on") : tr("Sharper upscaling off"));
}

void MainWindow::setEnhanceSharpness(double v)
{
    m_settings.enhanceSharpness = std::clamp(v, 0.0, 1.0);
    applyEnhance();
}

void MainWindow::setSmoothMotion(bool on)
{
    m_settings.smoothMotion = on;
    m_playbackPanel->setEnhance(m_settings.enhanceUpscale, m_settings.enhanceSharpness, m_settings.smoothMotion);
    applyEnhance();
    updateVideoPath();
    showOsd(on ? tr("Smooth motion on") : tr("Smooth motion off"));
}

void MainWindow::applyEnhance()
{
    const bool available = m_video->enhanceAvailable();
    const bool motion = m_settings.smoothMotion && !m_deskActive && !m_gifRecording;
    m_video->setEnhance(m_settings.enhanceUpscale, m_settings.enhanceSharpness, motion);
    // Frame generation shows each frame when the next has arrived: the picture runs one frame
    // behind the video's clock, and the sound is held back by that much.
    int latency = 0;
    if (motion && m_video->smoothMotionUseful() && m_player->hasMedia()) {
        const double fps = m_player->frameRate();
        latency = int(std::lround(1000.0 / (fps > 1.0 ? fps : 30.0)));
    }
    m_player->setPictureLatencyMs(latency);
    m_playbackPanel->setEnhanceStatus(available, available ? QString()
                                                           : tr("Needs a graphics card: here OpenGL runs in software (no graphics acceleration)."));
}

QString MainWindow::enhanceDescription() const
{
    if (!m_video->enhanceAvailable()) return {};
    QStringList parts;
    const QJsonObject e = m_video->enhanceReport();
    if (m_settings.enhanceUpscale) {
        const bool active = m_video->bypass() && e["upscaledWidth"].toInt() > 0;
        parts << (active ? tr("sharper upscaling to %1×%2").arg(e["upscaledWidth"].toInt()).arg(e["upscaledHeight"].toInt())
                         : tr("sharper upscaling (with effects off, when the picture is enlarged)"));
    }
    if (m_settings.smoothMotion) {
        const double fps = m_player->frameRate();
        const QString rate = fps > 0 ? QString::number(fps, 'f', fps == std::floor(fps) ? 0 : 2) : QStringLiteral("?");
        parts << (m_deskActive ? tr("smooth motion (not in desk mode)")
                  : m_video->smoothMotionUseful() ? tr("smooth motion: frames generated between the video's %1 a second").arg(rate)
                  : tr("smooth motion (nothing to add: the video has %1 frames a second, the screen shows %2)").arg(rate).arg(e["screenHz"].toDouble(), 0, 'f', 0));
    }
    return parts.join(QStringLiteral("; "));
}
