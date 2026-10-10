#include "Gamepad.h"
#include "ui/BrowseScreen.h"
#include "app/DeskWindow.h"
#include "app/MainWindow.h"
#include "render/DeskView.h"

#include <QApplication>
#include <QKeyEvent>
#include <SDL2/SDL.h>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// SDL entry points resolved at runtime.
namespace {
struct Sdl {
    int (*Init)(Uint32);
    void (*Quit)();
    SDL_bool (*SetHint)(const char*, const char*);
    int (*PollEvent)(SDL_Event*);
    SDL_bool (*IsGameController)(int);
    SDL_GameController* (*GameControllerOpen)(int);
    void (*GameControllerClose)(SDL_GameController*);
    const char* (*GameControllerName)(SDL_GameController*);
    SDL_GameController* (*GameControllerFromInstanceID)(SDL_JoystickID);
    int (*NumJoysticks)();
    void (*GetVersion)(SDL_version*);
    int (*JoystickAttachVirtualEx)(const SDL_VirtualJoystickDesc*);
    SDL_Joystick* (*JoystickOpen)(int);
    int (*JoystickSetVirtualButton)(SDL_Joystick*, int, Uint8);
    int (*JoystickSetVirtualAxis)(SDL_Joystick*, int, Sint16);
    void (*GameControllerUpdate)();
    SDL_Joystick* (*GameControllerGetJoystick)(SDL_GameController*);
    SDL_JoystickID (*JoystickInstanceID)(SDL_Joystick*);
} sdl;

// The SDL library, loaded at runtime: dlopen on Linux, LoadLibrary on Windows (SDL2.dll ships
// beside the player there).
void* libOpen(const char* name)
{
#ifdef _WIN32
    return reinterpret_cast<void*>(LoadLibraryA(name));
#else
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}
void libClose(void* lib)
{
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(lib));
#else
    dlclose(lib);
#endif
}
template <typename T> bool resolve(void* lib, T& fn, const char* name)
{
#ifdef _WIN32
    fn = reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name)));
#else
    fn = reinterpret_cast<T>(dlsym(lib, name));
#endif
    return fn != nullptr;
}

int buttonByName(const QString& n)
{
    static const QHash<QString, int> map = {
        {"a", SDL_CONTROLLER_BUTTON_A}, {"b", SDL_CONTROLLER_BUTTON_B}, {"x", SDL_CONTROLLER_BUTTON_X},
        {"y", SDL_CONTROLLER_BUTTON_Y}, {"back", SDL_CONTROLLER_BUTTON_BACK}, {"start", SDL_CONTROLLER_BUTTON_START},
        {"lb", SDL_CONTROLLER_BUTTON_LEFTSHOULDER}, {"rb", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
        {"l3", SDL_CONTROLLER_BUTTON_LEFTSTICK}, {"r3", SDL_CONTROLLER_BUTTON_RIGHTSTICK},
        {"up", SDL_CONTROLLER_BUTTON_DPAD_UP}, {"down", SDL_CONTROLLER_BUTTON_DPAD_DOWN},
        {"left", SDL_CONTROLLER_BUTTON_DPAD_LEFT}, {"right", SDL_CONTROLLER_BUTTON_DPAD_RIGHT}, {"guide", SDL_CONTROLLER_BUTTON_GUIDE}};
    return map.value(n.toLower(), -1);
}
int axisByName(const QString& n)
{
    static const QHash<QString, int> map = {{"lx", SDL_CONTROLLER_AXIS_LEFTX}, {"ly", SDL_CONTROLLER_AXIS_LEFTY},
                                            {"rx", SDL_CONTROLLER_AXIS_RIGHTX}, {"ry", SDL_CONTROLLER_AXIS_RIGHTY},
                                            {"lt", SDL_CONTROLLER_AXIS_TRIGGERLEFT}, {"rt", SDL_CONTROLLER_AXIS_TRIGGERRIGHT}};
    return map.value(n.toLower(), -1);
}
} // namespace

Gamepad::Gamepad(MainWindow* w) : QObject(w), m_w(w)
{
#ifdef _WIN32
    for (const char* name : {"SDL2.dll"}) if ((m_lib = libOpen(name))) break;
#else
    for (const char* name : {"libSDL2-2.0.so.0", "libSDL2.so"}) if ((m_lib = libOpen(name))) break;
#endif
    if (!m_lib) return;
    bool ok = resolve(m_lib, sdl.Init, "SDL_Init") && resolve(m_lib, sdl.Quit, "SDL_Quit") &&
              resolve(m_lib, sdl.SetHint, "SDL_SetHint") && resolve(m_lib, sdl.PollEvent, "SDL_PollEvent") &&
              resolve(m_lib, sdl.IsGameController, "SDL_IsGameController") &&
              resolve(m_lib, sdl.GameControllerOpen, "SDL_GameControllerOpen") &&
              resolve(m_lib, sdl.GameControllerClose, "SDL_GameControllerClose") &&
              resolve(m_lib, sdl.GameControllerName, "SDL_GameControllerName") &&
              resolve(m_lib, sdl.GameControllerFromInstanceID, "SDL_GameControllerFromInstanceID") &&
              resolve(m_lib, sdl.NumJoysticks, "SDL_NumJoysticks") && resolve(m_lib, sdl.GetVersion, "SDL_GetVersion") &&
              resolve(m_lib, sdl.GameControllerUpdate, "SDL_GameControllerUpdate") &&
              resolve(m_lib, sdl.GameControllerGetJoystick, "SDL_GameControllerGetJoystick") &&
              resolve(m_lib, sdl.JoystickInstanceID, "SDL_JoystickInstanceID");
    // Optional (SDL >= 2.0.14 / 2.24): only the test hooks need them.
    resolve(m_lib, sdl.JoystickAttachVirtualEx, "SDL_JoystickAttachVirtualEx");
    resolve(m_lib, sdl.JoystickOpen, "SDL_JoystickOpen");
    resolve(m_lib, sdl.JoystickSetVirtualButton, "SDL_JoystickSetVirtualButton");
    resolve(m_lib, sdl.JoystickSetVirtualAxis, "SDL_JoystickSetVirtualAxis");
    if (!ok) { libClose(m_lib); m_lib = nullptr; return; }
    // No SDL window exists: without this hint SDL would treat every event as "background".
    sdl.SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    sdl.SetHint("SDL_NO_SIGNAL_HANDLERS", "1");
    if (sdl.Init(SDL_INIT_GAMECONTROLLER) != 0) { libClose(m_lib); m_lib = nullptr; return; }
    m_ok = true;
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &Gamepad::poll);
    m_timer.start();
}

Gamepad::~Gamepad()
{
    if (!m_ok) return;
    for (void* c : m_open) sdl.GameControllerClose(static_cast<SDL_GameController*>(c));
    sdl.Quit();
}

QString Gamepad::status() const
{
    if (!m_ok) return QStringLiteral("off (SDL2 not found)");
    SDL_version v;
    sdl.GetVersion(&v);
    QStringList names;
    for (void* c : m_open) names << QString::fromUtf8(sdl.GameControllerName(static_cast<SDL_GameController*>(c)));
    return QStringLiteral("SDL %1.%2.%3, %4 controller(s)%5").arg(v.major).arg(v.minor).arg(v.patch).arg(m_open.size())
        .arg(names.isEmpty() ? QString() : QStringLiteral(": ") + names.join(", "));
}

bool Gamepad::attachVirtual()
{
    if (!m_ok || !sdl.JoystickAttachVirtualEx || !sdl.JoystickOpen) return false;
    SDL_VirtualJoystickDesc desc;
    std::memset(&desc, 0, sizeof desc);
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.name = "CRT Player test controller";
    m_virtualIndex = sdl.JoystickAttachVirtualEx(&desc);
    if (m_virtualIndex < 0) return false;
    m_virtualJoy = sdl.JoystickOpen(m_virtualIndex);
    return m_virtualJoy != nullptr;
}

bool Gamepad::setVirtualButton(const QString& name, bool down)
{
    const int b = buttonByName(name);
    if (!m_virtualJoy || b < 0 || !sdl.JoystickSetVirtualButton) return false;
    return sdl.JoystickSetVirtualButton(static_cast<SDL_Joystick*>(m_virtualJoy), b, down ? 1 : 0) == 0;
}

bool Gamepad::setVirtualAxis(const QString& name, double value)
{
    const int a = axisByName(name);
    if (!m_virtualJoy || a < 0 || !sdl.JoystickSetVirtualAxis) return false;
    return sdl.JoystickSetVirtualAxis(static_cast<SDL_Joystick*>(m_virtualJoy), a, Sint16(std::clamp(value, -1.0, 1.0) * 32767)) == 0;
}

bool Gamepad::uiHasFocus() const
{
    // A menu, dialog or panel with keyboard focus: the pad navigates it like arrow keys.
    if (QApplication::activePopupWidget() || QApplication::activeModalWidget()) return true;
    if (m_w->browseOpen()) return true;   // (the browser of web videos covers the window: it is all UI)
    QWidget* f = QApplication::focusWidget();
    return f && f->window() == m_w && !m_w->isVideoFocus(f);
}

void Gamepad::sendKey(int qtKey)
{
    QWidget* target = QApplication::activePopupWidget();
    if (!target) target = QApplication::activeModalWidget();
    if (!target && m_w->browseOpen()) target = m_w->browseScreen()->keyTarget();
    if (!target) target = QApplication::focusWidget();
    if (!target) return;
    QKeyEvent press(QEvent::KeyPress, qtKey, Qt::NoModifier), release(QEvent::KeyRelease, qtKey, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &press);
    QCoreApplication::sendEvent(target, &release);
}

void Gamepad::act(const QString& action, double value)
{
    m_lastAction = action;
    if (action.startsWith(QLatin1String("key:"))) { sendKey(action.mid(4).toInt()); return; }
    m_w->remoteAction(action, value);
}

void Gamepad::onButton(int b, bool down)
{
    if (!down) { if (b == m_heldButton) m_heldButton = -1; return; }
    const bool ui = uiHasFocus();
    const bool dpad = b == SDL_CONTROLLER_BUTTON_DPAD_UP || b == SDL_CONTROLLER_BUTTON_DPAD_DOWN ||
                      b == SDL_CONTROLLER_BUTTON_DPAD_LEFT || b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    if (dpad && b != m_heldButton) { m_heldButton = b; m_heldFor.restart(); m_lastRepeat.restart(); }
    if (ui) {
        switch (b) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: act(QStringLiteral("key:%1").arg(Qt::Key_Up)); return;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: act(QStringLiteral("key:%1").arg(Qt::Key_Down)); return;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: act(QStringLiteral("key:%1").arg(Qt::Key_Left)); return;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: act(QStringLiteral("key:%1").arg(Qt::Key_Right)); return;
        case SDL_CONTROLLER_BUTTON_A: act(QStringLiteral("key:%1").arg(Qt::Key_Return)); return;
        case SDL_CONTROLLER_BUTTON_B: act(QStringLiteral("key:%1").arg(Qt::Key_Escape)); return;
        default: break;
        }
        if (m_w->browseOpen()) {   // (the browser of web videos: put aside, search, the sections)
            switch (b) {
            case SDL_CONTROLLER_BUTTON_X: act(QStringLiteral("key:%1").arg(Qt::Key_W)); return;
            case SDL_CONTROLLER_BUTTON_Y: act(QStringLiteral("key:%1").arg(Qt::Key_Slash)); return;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: act(QStringLiteral("key:%1").arg(Qt::Key_BracketLeft)); return;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: act(QStringLiteral("key:%1").arg(Qt::Key_BracketRight)); return;
            case SDL_CONTROLLER_BUTTON_GUIDE: act("browse"); return;
            default: break;
            }
        }
    }
    if (b == SDL_CONTROLLER_BUTTON_GUIDE) { act("browse"); return; }
    switch (b) {
    case SDL_CONTROLLER_BUTTON_A: act("playpause"); break;
    case SDL_CONTROLLER_BUTTON_B: act("back"); break;
    case SDL_CONTROLLER_BUTTON_X: act("subs"); break;
    case SDL_CONTROLLER_BUTTON_Y: act("fly"); break;
    case SDL_CONTROLLER_BUTTON_BACK: act("desk"); break;
    case SDL_CONTROLLER_BUTTON_START: act("controls"); break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: act("preset-"); break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: act("preset+"); break;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: act("chapter-"); break;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: act("chapter+"); break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: act("seekrel", -10e9); break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: act("seekrel", 10e9); break;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: act("volup"); break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: act("voldown"); break;
    default: break;
    }
    if (b != SDL_CONTROLLER_BUTTON_START) m_w->showControlsBriefly();
}

void Gamepad::poll()
{
    const bool active = m_alwaysActive || QGuiApplication::applicationState() == Qt::ApplicationActive;
    SDL_Event e;
    while (sdl.PollEvent(&e)) {
        switch (e.type) {
        case SDL_CONTROLLERDEVICEADDED:
            if (SDL_GameController* c = sdl.GameControllerOpen(e.cdevice.which)) {
                // Keyed by the instance id, which later events report (the "added" event gives the index).
                m_open.insert(sdl.JoystickInstanceID(sdl.GameControllerGetJoystick(c)), c);
                if (!m_helpShown) {
                    m_helpShown = true;
                    m_w->showOsdMessage(tr("Controller: %1 — A play/pause · B back · D-pad seek/volume · LB/RB preset · View desk mode")
                                            .arg(QString::fromUtf8(sdl.GameControllerName(c))), 6000);
                }
            }
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (void* c = m_open.take(e.cdevice.which)) sdl.GameControllerClose(static_cast<SDL_GameController*>(c));
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            if (active) onButton(e.cbutton.button, e.type == SDL_CONTROLLERBUTTONDOWN);
            break;
        case SDL_CONTROLLERAXISMOTION: {
            const double v = e.caxis.value / 32767.0;
            if (e.caxis.axis < 6) m_axis[e.caxis.axis] = v;
            // Triggers act on the press (edge), for the previous / next playlist item.
            for (int t = 0; t < 2; ++t) {
                const int axis = t == 0 ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
                if (e.caxis.axis != axis) continue;
                if (!m_triggerDown[t] && v > 0.6) { m_triggerDown[t] = true; if (active) act(t == 0 ? "prev" : "next"); }
                else if (m_triggerDown[t] && v < 0.3) m_triggerDown[t] = false;
            }
            break;
        }
        default: break;
        }
    }
    if (!active) return;
    // Held D-pad repeats (seek / volume) after 400 ms, every 250 ms.
    if (m_heldButton >= 0 && m_heldFor.elapsed() > 400 && m_lastRepeat.elapsed() > 250 && !uiHasFocus()) {
        m_lastRepeat.restart();
        onButton(m_heldButton, true);   // same button: the hold timer keeps running
    }
    // Sticks turn and resize the set in desk mode.
    if (DeskWindow* d = m_w->deskWindow()) {
        const auto dz = [](double v) { return std::abs(v) < 0.18 ? 0.0 : v; };
        const double lx = dz(m_axis[SDL_CONTROLLER_AXIS_LEFTX]), ly = dz(m_axis[SDL_CONTROLLER_AXIS_LEFTY]);
        const double ry = dz(m_axis[SDL_CONTROLLER_AXIS_RIGHTY]);
        // Time-based: the same speed however often we get to poll (rendering can be heavy).
        const double dt = std::min(0.5, m_stickClock.isValid() ? m_stickClock.restart() / 1000.0 : 0.016);   // cap: no big jump after idling
        if (!m_stickClock.isValid()) m_stickClock.start();
        if (lx != 0 || ly != 0 || ry != 0) {
            DeskView::DeskPose p = d->view()->deskPose();
            p.yaw = std::clamp(p.yaw + lx * 90.0 * dt, -85.0, 85.0);        // 90 degrees/s at full tilt
            p.pitch = std::clamp(p.pitch - ly * 60.0 * dt, -35.0, 45.0);    // 60 degrees/s
            p.height = std::clamp(p.height * (1.0 - ry * 0.8 * dt), 0.15, 0.95);
            d->view()->setDeskPose(p);
            m_lastAction = QStringLiteral("stick");
        }
    }
}
