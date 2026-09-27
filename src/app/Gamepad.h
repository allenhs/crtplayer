#pragma once
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

class MainWindow;

// Game controllers via SDL2, loaded at runtime (dlopen): no build or run dependency; if
// SDL2 is missing, controller support is simply off. SDL's game-controller layer maps
// Xbox, PlayStation, Switch and Steam Deck (including Steam Input's virtual pad) alike.
class Gamepad : public QObject {
    Q_OBJECT
public:
    explicit Gamepad(MainWindow* w);
    ~Gamepad() override;
    bool available() const { return m_ok; }
    QString status() const;
    int controllerCount() const { return m_open.size(); }
    // Act on input even when the app is not active (used in gamescope and by tests).
    void setAlwaysActive(bool on) { m_alwaysActive = on; }
    // Test hooks: a virtual controller that goes through SDL's normal event path.
    bool attachVirtual();
    bool setVirtualButton(const QString& name, bool down);
    bool setVirtualAxis(const QString& name, double value);
    QString lastAction() const { return m_lastAction; }
private:
    void poll();
    void onButton(int button, bool down);
    void act(const QString& action, double value = 0);
    bool uiHasFocus() const;
    void sendKey(int qtKey);
    bool m_ok = false, m_alwaysActive = false;
    void* m_lib = nullptr;
    QHash<int, void*> m_open;   // instance id -> SDL_GameController*
    int m_virtualIndex = -1;
    void* m_virtualJoy = nullptr;
    MainWindow* m_w;
    QTimer m_timer;
    int m_heldButton = -1;
    QElapsedTimer m_heldFor, m_lastRepeat, m_stickClock;
    double m_axis[6] = {0, 0, 0, 0, 0, 0};
    bool m_triggerDown[2] = {false, false};
    QString m_lastAction;
    bool m_helpShown = false;
};
