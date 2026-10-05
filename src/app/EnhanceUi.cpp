// Enhance (2.13), as part of MainWindow: sharper upscaling and frame generation, for a
// graphics card. The work itself is in render/Enhance.cpp.
#include "MainWindow.h"

#include "playback/Player.h"
#include "render/VideoWidget.h"
#include "ui/PlaybackPanel.h"

#include <QJsonObject>
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
    m_video->setNvidia(m_settings.enhanceNvidia, m_settings.nvidiaQuality, m_settings.nvidiaMotion);
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
    refreshNvidiaStatus();
}

void MainWindow::setEnhanceNvidia(bool on, int quality, int mode)
{
    const bool toggled = on != m_settings.enhanceNvidia;
    m_settings.enhanceNvidia = on;
    m_settings.nvidiaQuality = std::clamp(quality, 1, 4);
    m_settings.nvidiaMotion = std::clamp(mode, 0, 2);
    m_playbackPanel->setNvidia(m_settings.enhanceNvidia, m_settings.nvidiaQuality, m_settings.nvidiaMotion);
    applyEnhance();
    if (toggled) showOsd(on ? tr("NVIDIA AI on") : tr("NVIDIA AI off"));
}

void MainWindow::refreshNvidiaStatus()
{
    // How many pictures a second are being drawn (with smooth motion: as many as the screen shows, if all is well).
    const double draws = m_video->enhanceReport()["draws"].toDouble();
    if (m_nvRateTimer.isValid() && m_nvRateTimer.elapsed() >= 500) {
        if (m_nvDrawsSeen >= 0) m_nvDrawRate = (draws - m_nvDrawsSeen) * 1000.0 / m_nvRateTimer.elapsed();
        m_nvDrawsSeen = draws;
        m_nvRateTimer.start();
    } else if (!m_nvRateTimer.isValid()) { m_nvDrawsSeen = draws; m_nvRateTimer.start(); }
    m_playbackPanel->setNvidiaStatus(m_video->nvidia().install().usable(), nvidiaStatus());
}

// One line on what NVIDIA's methods are doing (or why they are not).
QString MainWindow::nvidiaStatus() const
{
    const NvEnhancer& nv = m_video->nvidia();
    const NvEnhancer::Install& in = nv.install();
    if (in.helper.isEmpty()) return tr("NVIDIA AI: not part of this build (its helper program, crtplayer-nvfx, is not beside the player).");
    if (in.sdk.isEmpty()) return tr("NVIDIA AI: not installed. It is for NVIDIA RTX graphics cards; the README says how to add it.");
    if (!in.usable()) return tr("NVIDIA AI: the SDK is installed, but neither Video Super Resolution nor Video Frame Generation is in it (the README says how to add them).");
    QStringList have;
    if (in.superRes()) have << tr("upscaling");
    if (in.frameGen()) have << tr("smooth motion");
    const QString haveText = have.join(tr(" and "));
    if (!m_video->enhanceAvailable()) return tr("NVIDIA AI: installed (%1).").arg(haveText);
    if (!m_settings.enhanceNvidia) return tr("NVIDIA AI: installed (%1), not in use.").arg(haveText);
    const QJsonObject e = m_video->enhanceReport()["nvidia"].toObject();
    const QString state = e["state"].toString(), error = e["error"].toString();
    if (state == QLatin1String("starting") || state == QLatin1String("opening")) return tr("NVIDIA AI: starting…");
    if (!error.isEmpty() && state != QLatin1String("ready"))
        return tr("NVIDIA AI could not be used: %1. The built-in methods are used instead.").arg(error);
    if (state == QLatin1String("ready") && e["kind"].toInt() != 0) {
        static const char* const quality[] = {"", QT_TR_NOOP("low"), QT_TR_NOOP("medium"), QT_TR_NOOP("high"), QT_TR_NOOP("ultra")};
        static const char* const mode[] = {QT_TR_NOOP("fast"), QT_TR_NOOP("balanced"), QT_TR_NOOP("best")};
        QStringList parts;
        if (e["quality"].toInt() > 0)
            parts << tr("upscaling %1×%2 to %3×%4 (%5)").arg(e["srcWidth"].toInt()).arg(e["srcHeight"].toInt()).arg(e["outWidth"].toInt()).arg(e["outHeight"].toInt())
                         .arg(tr(quality[std::clamp(e["quality"].toInt(), 1, 4)]));
        if (e["mode"].toInt() >= 0) parts << tr("smooth motion (%1)").arg(tr(mode[std::clamp(e["mode"].toInt(), 0, 2)]));
        // (what each picture costs: the helper's work on it, and putting it on screen; and each of the video's frames: reading it back, and the helper's work)
        const double picture = e["pictureMs"].toDouble() + e["uploadMs"].toDouble(), frame = e["frameMs"].toDouble() + e["readMs"].toDouble();
        QString text = tr("NVIDIA AI at work: %1. %2 ms a picture, %3 ms a frame of the video.").arg(parts.join(tr(", "))).arg(picture, 0, 'f', 1).arg(frame, 0, 'f', 1);
        const QJsonObject all = m_video->enhanceReport();
        if (all["running"].toBool() && m_nvDrawRate > 1)
            text += QLatin1Char(' ') + tr("%1 pictures a second on a %2 Hz screen.").arg(m_nvDrawRate, 0, 'f', 0).arg(all["screenHz"].toDouble(), 0, 'f', 0);
        return text;
    }
    return tr("NVIDIA AI: ready (%1). It comes in when upscaling or smooth motion has something to do.").arg(haveText);
}

QString MainWindow::enhanceDescription() const
{
    if (!m_video->enhanceAvailable()) return {};
    QStringList parts;
    const QJsonObject e = m_video->enhanceReport();
    if (m_settings.enhanceUpscale) {
        const bool active = m_video->bypass() && e["upscaledWidth"].toInt() > 0;
        parts << (active && m_video->nvidiaUpscaling() ? tr("NVIDIA AI upscaling to %1×%2").arg(e["upscaledWidth"].toInt()).arg(e["upscaledHeight"].toInt())
                  : active ? tr("sharper upscaling to %1×%2").arg(e["upscaledWidth"].toInt()).arg(e["upscaledHeight"].toInt())
                         : tr("sharper upscaling (with effects off, when the picture is enlarged)"));
    }
    if (m_settings.smoothMotion) {
        const double fps = m_player->frameRate();
        const QString rate = fps > 0 ? QString::number(fps, 'f', fps == std::floor(fps) ? 0 : 2) : QStringLiteral("?");
        parts << (m_deskActive ? tr("smooth motion (not in desk mode)")
                  : m_video->smoothMotionUseful() && m_video->nvidiaMotion() ? tr("smooth motion: frames generated by NVIDIA AI between the video's %1 a second").arg(rate)
                  : m_video->smoothMotionUseful() ? tr("smooth motion: frames generated between the video's %1 a second").arg(rate)
                  : tr("smooth motion (nothing to add: the video has %1 frames a second, the screen shows %2)").arg(rate).arg(e["screenHz"].toDouble(), 0, 'f', 0));
    }
    return parts.join(QStringLiteral("; "));
}
