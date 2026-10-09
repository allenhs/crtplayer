#include "DeskWindow.h"
#include "app/WinWindow.h"
#include "render/DeskView.h"
#include "ui/ControlBar.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDateTime>
#include <QJsonArray>
#include <QEvent>
#include <QGuiApplication>
#include <QResizeEvent>
#include <QScreen>

// Borderless everywhere. On Windows without the flag for it: the window is only ever shown full
// screen, where it has no frame anyway, and it needs the pixel of border that Qt gives only
// to a window without that flag (WinWindow.h).
static Qt::WindowFlags deskWindowFlags()
{
    return WinWindow::borderInFullScreen() ? Qt::WindowFlags(Qt::Window) : Qt::Window | Qt::FramelessWindowHint;
}

DeskWindow::DeskWindow(Player* player, VideoWidget* flat, QWidget* parent)
    : QWidget(parent, deskWindowFlags())
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setWindowTitle(tr("CRT Player — desk"));
    m_view = new DeskView(player, flat, this);
    m_bar = new ControlBar(m_view);
    m_bar->hide();
    m_bar->installEventFilter(this);
    m_hide.setSingleShot(true);
    m_hide.setInterval(2500);
    connect(&m_hide, &QTimer::timeout, this, &DeskWindow::autoHide);
    connect(m_view, &DeskView::silhouetteChanged, this, [this] { placeBar(); updateMask(); });
    connect(m_view, &DeskView::mouseActivity, this, [this] { ++m_wokenByView; m_why = QStringLiteral("view"); activity(); });
    connect(m_view, &DeskView::phaseChanged, this, [this](DeskView::Phase p) {
        m_bar->setFullscreenIcon(p == DeskView::Phase::Full || p == DeskView::Phase::FlyingIn);
        if (p == DeskView::Phase::Full) m_view->setCursor(Qt::BlankCursor);
        placeBar();
        updateMask();
        ++m_wokenByPhase;
        m_why = QStringLiteral("phase %1").arg(int(p));
        activity();
    });
    WinWindow::keepComposed(this);   // (Windows: see there)
}

void DeskWindow::openOn(QScreen* screen)
{
    if (!screen) screen = QGuiApplication::primaryScreen();
    // (Windows, with its pixel of border: a geometry given to a window that is already full screen
    // is taken for its inside, which would then be the whole screen after all. So a window opened
    // before goes back to an ordinary one first, while it is still hidden.)
    if (WinWindow::borderInFullScreen() && isFullScreen() && !isVisible()) setWindowState(windowState() & ~Qt::WindowFullScreen);
    setScreen(screen);
    setGeometry(screen->geometry());
    // Fullscreen from the start: the fly-in then only moves the camera inside this
    // window, and never has to resize or reposition it (Wayland does not allow that).
    showFullScreen();
    raise();
    activateWindow();
    m_view->setFocus();
    ++m_opened;
    m_why = QStringLiteral("open");
    activity();
}

void DeskWindow::setKeepOnTop(bool on)
{
    m_onTop = on;
    if (WinWindow::setTopmost(this, on)) return;   // (Windows, once the window is made)
    const bool vis = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    if (vis) showFullScreen();
}

void DeskWindow::closeEvent(QCloseEvent* e)
{
    e->ignore();       // leaving desk mode just hides this window
    emit closed();
}

bool DeskWindow::controlsVisible() const { return m_bar->isVisible(); }

// For the checks: what has kept the strip of controls up.
QJsonObject DeskWindow::controlsReport() const
{
    const QPoint cursor = QCursor::pos();
    QWidget* popup = QApplication::activePopupWidget();
    return QJsonObject{{"visible", m_bar->isVisible()}, {"wokenByView", m_wokenByView}, {"wokenByBar", m_wokenByBar}, {"wokenByPhase", m_wokenByPhase},
                       {"keptByUse", m_keptByUse}, {"barUnderMouse", m_bar->underMouse()}, {"barInUse", m_bar->isInteracting()},
                       {"popup", popup ? QString::fromLatin1(popup->metaObject()->className()) + QLatin1Char(' ') + popup->objectName() : QString()},
                       {"cursor", QStringLiteral("%1,%2").arg(cursor.x()).arg(cursor.y())},
                       {"bar", QStringLiteral("%1,%2 %3x%4").arg(m_bar->mapToGlobal(QPoint(0, 0)).x()).arg(m_bar->mapToGlobal(QPoint(0, 0)).y()).arg(m_bar->width()).arg(m_bar->height())},
                       {"hideInMs", m_hide.isActive() ? m_hide.remainingTime() : -1}, {"opened", m_opened}, {"events", QJsonArray::fromStringList(m_events)}, {"now", double(QDateTime::currentMSecsSinceEpoch() % 1000000)},
                       {"sinceActivityMs", m_lastActivity.isValid() ? double(m_lastActivity.elapsed()) : -1.0}};
}

void DeskWindow::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    m_view->setGeometry(rect());
    placeBar();
    updateMask();
}

void DeskWindow::placeBar()
{
    const int bh = m_bar->sizeHint().height();
    const auto ph = m_view->phase();
    if (ph == DeskView::Phase::Desk) {
        // A strip just under the set, at least wide enough for the main controls.
        const QRectF tv = m_view->silhouetteLogical().boundingRect();
        const int w = std::clamp(int(tv.width()), 640, std::max(640, width() - 40));
        int x = int(tv.center().x() - w / 2.0);
        x = std::clamp(x, 20, std::max(20, width() - w - 20));
        int y = int(tv.bottom()) + 10;
        if (y + bh > height() - 10) y = int(tv.bottom()) - bh - 10;   // no room below: overlap the base
        m_bar->setGeometry(x, std::max(10, y), w, bh);
    } else {
        const int w = std::min(width() - 48, 1280);
        m_bar->setGeometry((width() - w) / 2, height() - bh - 24, w, bh);
    }
    m_bar->raise();
}

void DeskWindow::updateMask()
{
    // Whole-window input in fullscreen, and with the room backdrop (nothing to click through to).
    if (m_view->phase() != DeskView::Phase::Desk || m_view->room()) {
        if (!m_mask.isEmpty()) { clearMask(); m_mask = QRegion(); }
        return;
    }
    QRegion r(m_view->silhouetteLogical().toPolygon());
    if (m_bar->isVisible()) r += m_bar->geometry().adjusted(-4, -4, 4, 4);
    if (r.isEmpty()) r = QRegion(0, 0, 1, 1);
    if (r != m_mask) {
        m_mask = r;
        setMask(r);   // input region on Wayland; window shape on X11
    }
}

void DeskWindow::activity()
{
    m_lastActivity.start();
    m_events.append(QStringLiteral("%1 %2 %3").arg(QDateTime::currentMSecsSinceEpoch() % 1000000).arg(m_why.isEmpty() ? QStringLiteral("?") : m_why).arg(m_bar->isVisible() ? "shown" : "hidden"));
    if (m_events.size() > 12) m_events.removeFirst();
    m_why.clear();
    if (!m_bar->isVisible()) {
        m_bar->show();
        placeBar();
        updateMask();
    }
    if (m_view->phase() == DeskView::Phase::Full) m_view->unsetCursor();
    m_hide.start();
}

void DeskWindow::autoHide()
{
    m_events.append(QStringLiteral("%1 autohide").arg(QDateTime::currentMSecsSinceEpoch() % 1000000));
    if (m_events.size() > 12) m_events.removeFirst();
    const bool interacting = m_bar->isInteracting() || QApplication::activePopupWidget();
    if (interacting) { ++m_keptByUse; m_hide.start(); return; }
    m_bar->hide();
    if (m_view->phase() == DeskView::Phase::Full) m_view->setCursor(Qt::BlankCursor);
    updateMask();
}

bool DeskWindow::eventFilter(QObject* o, QEvent* e)
{
    if (o == m_bar && (e->type() == QEvent::Enter || e->type() == QEvent::MouseMove)) { ++m_wokenByBar; m_why = e->type() == QEvent::Enter ? QStringLiteral("bar enter") : QStringLiteral("bar move"); activity(); }
    return QWidget::eventFilter(o, e);
}
