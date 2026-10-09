#pragma once
#include <QElapsedTimer>
#include <QJsonObject>
#include <QTimer>
#include <QWidget>

class Player;
class VideoWidget;
class DeskView;
class ControlBar;
class QScreen;

// A borderless, transparent window covering one screen. Only the TV (and its control
// strip, when shown) accepts input: the window's input mask follows the set's
// silhouette, so clicks anywhere else fall through to the desktop. When the set flies
// into fullscreen the mask is dropped and the window becomes an ordinary opaque
// fullscreen player.
class DeskWindow : public QWidget {
    Q_OBJECT
public:
    DeskWindow(Player* player, VideoWidget* flat, QWidget* parent = nullptr);
    DeskView* view() const { return m_view; }
    ControlBar* bar() const { return m_bar; }
    void openOn(QScreen* screen);
    void setKeepOnTop(bool on);
    bool keepOnTop() const { return m_onTop; }
    bool controlsVisible() const;
    QJsonObject controlsReport() const;
    QRegion currentMask() const { return m_mask; }

signals:
    void closed();   // closed by the window manager (e.g. Alt+F4)

protected:
    void resizeEvent(QResizeEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void placeBar();
    void updateMask();
    void activity();
    void autoHide();

    DeskView* m_view;
    ControlBar* m_bar;
    QTimer m_hide;
    int m_opened = 0;
    QString m_why;
    QStringList m_events;
    QElapsedTimer m_lastActivity;
    int m_wokenByView = 0, m_wokenByBar = 0, m_wokenByPhase = 0, m_keptByUse = 0;   // (what has kept the strip up, for the checks)
    bool m_onTop = false;
    QRegion m_mask;
};
