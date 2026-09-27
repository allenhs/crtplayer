#include <functional>
#include <QElapsedTimer>
#include <QFileInfo>
#include <memory>
#include "Automation.h"
#include "MainWindow.h"
#include "playback/Player.h"
#include "render/VideoWidget.h"
#include "render/DeskView.h"
#include "app/DeskWindow.h"
#include "jellyfin/JellyfinClient.h"
#include "playback/Thumbnailer.h"
#include "app/Gamepad.h"
#include "ui/JellyfinPanel.h"

#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QTextStream>

Automation::Automation(MainWindow* w, const QString& script, const QString& logPath, QObject* parent)
    : QObject(parent), m_w(w), m_script(script), m_logPath(logPath)
{
    // Measures GUI-thread responsiveness: gaps between 10 ms ticks = event-loop stalls.
    m_loopProbe.setInterval(10);
    connect(&m_loopProbe, &QTimer::timeout, this, [this] {
        const qint64 now = m_clock.elapsed();
        if (m_lastProbe) m_maxStallMs = std::max(m_maxStallMs, double(now - m_lastProbe - 10));
        m_lastProbe = now;
    });
}

bool Automation::start()
{
    QFile f(m_script);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        const QString l = ts.readLine().trimmed();
        if (!l.isEmpty() && !l.startsWith('#')) m_lines << l;
    }
    m_clock.start();
    m_loopProbe.start();
    QTimer::singleShot(300, this, &Automation::next);
    return true;
}

void Automation::log(const QString& cmd, const QJsonObject& data)
{
    QJsonObject o = data;
    o["t"] = double(m_clock.elapsed());
    o["cmd"] = cmd;
    m_log.append(o);
    QFile f(m_logPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(m_log).toJson());
    QTextStream(stdout) << "[auto] " << cmd << Qt::endl;   // flushed: a crash still shows the last step
    fflush(stdout);
}

void Automation::finish(int code)
{
    log("finished", {{"failures", m_failures}});
    QTimer::singleShot(100, qApp, [code] { qApp->exit(code); });
}

void Automation::next()
{
    if (m_pc >= m_lines.size()) { finish(m_failures ? 1 : 0); return; }
    const QString line = m_lines.at(m_pc++);
    const QStringList a = line.split(' ', Qt::SkipEmptyParts);
    const QString cmd = a.value(0).toLower();
    const QString rest = line.section(' ', 1).trimmed();
    int delay = 50;
    Player* p = m_w->player();

    if (cmd == "open") {
        m_w->openFiles({rest}, true);
        log(line);
    } else if (cmd == "wait") {
        delay = a.value(1).toInt();
        log(line);
    } else if (cmd == "waitstate") {
        const QString want = a.value(1);
        const int timeout = a.value(2, "10000").toInt();
        const qint64 t0 = m_clock.elapsed();
        auto* poll = new QTimer(this);
        poll->setInterval(20);
        connect(poll, &QTimer::timeout, this, [=] {
            const QString st = m_w->stateReport().value("state").toString();
            const bool ok = st == want && (want != "playing" || m_w->stateReport().value("hasFrame").toBool());
            if (ok || st == "error" || m_clock.elapsed() - t0 > timeout) {
                poll->deleteLater();
                if (!ok) ++m_failures;
                log(line, {{"ok", ok}, {"state", st}, {"waitedMs", double(m_clock.elapsed() - t0)}});
                QTimer::singleShot(10, this, &Automation::next);
            }
        });
        poll->start();
        return;
    } else if (cmd == "play") { p->play(); log(line); }
    else if (cmd == "pause") { p->pause(); log(line); }
    else if (cmd == "seek") {
        p->seek(qint64(a.value(1).toDouble() * 1e9), a.value(2) == "fast" ? Player::SeekMode::Fast : Player::SeekMode::Accurate);
        log(line);
    } else if (cmd == "scrub") {
        // scrub <from s> <to s> <steps> <interval ms>: emulates dragging the seek bar.
        const double from = a.value(1).toDouble(), to = a.value(2).toDouble();
        const int steps = a.value(3, "20").toInt(), interval = a.value(4, "30").toInt();
        m_maxStallMs = 0;
        // Like the seek bar: holding it marks the view as being scrubbed.
        if (DeskWindow* d = m_w->deskWindow()) d->view()->setScrubbing(true);
        auto* t = new QTimer(this);
        auto i = std::make_shared<int>(0);
        t->setInterval(interval);
        connect(t, &QTimer::timeout, this, [=] {
            const double pos = from + (to - from) * (*i) / std::max(1, steps - 1);
            p->seek(qint64(pos * 1e9), (*i == steps - 1) ? Player::SeekMode::Accurate : Player::SeekMode::Fast);
            if (++(*i) >= steps) {
                t->deleteLater();
                if (DeskWindow* d = m_w->deskWindow()) d->view()->setScrubbing(false);   // let go
                log(line, {{"maxEventLoopStallMs", m_maxStallMs}});
                QTimer::singleShot(10, this, &Automation::next);
            }
        });
        t->start();
        return;
    } else if (cmd == "step") {
        p->stepFrame(a.value(1) != "back");
        log(line);
    } else if (cmd == "preset") {
        const bool ok = m_w->selectPreset(rest);
        if (!ok) ++m_failures;
        log(line, {{"ok", ok}});
    } else if (cmd == "bypass") { m_w->setBypass(a.value(1) == "on"); log(line); }
    else if (cmd == "compare") {
        m_w->setCompare(a.value(1) == "on");
        if (a.size() > 2) m_w->video()->setSplitFraction(a.value(2).toDouble());
        log(line);
    } else if (cmd == "fullscreen") { m_w->setFullscreen(a.value(1) == "on"); log(line); delay = 400; }
    else if (cmd == "mode") {
        const QString m = a.value(1);
        m_w->setScaleMode(m == "original" ? ScaleMode::Original : m == "fill" ? ScaleMode::Fill : m == "crop" ? ScaleMode::Crop : ScaleMode::Fit);
        log(line);
    } else if (cmd == "crop") {
        CropFractions c{a.value(1).toDouble(), a.value(2).toDouble(), a.value(3).toDouble(), a.value(4).toDouble()};
        m_w->setCrop(c);
        log(line);
    } else if (cmd == "param") {
        // param <key> <value> : sets one CRT parameter (float keys, maskType, scanLines, includeBars)
        CrtParams cp = m_w->video()->params();
        QJsonObject j = cp.toJson();
        const QString key = a.value(1);
        static const QStringList boolKeys = {"includeBars", "powerEffects", "channelStatic", "vcrOsd"};
        if (boolKeys.contains(key)) j[key] = a.value(2) == "on" || a.value(2) == "1" || a.value(2) == "true";
        else j[key] = a.value(2).toDouble();
        m_w->setParams(CrtParams::fromJson(j));
        log(line);
    } else if (cmd == "aspect") { m_w->setAspectOverride(a.value(1).toDouble()); log(line); }
    else if (cmd == "screenshot") {
        const QString path = m_w->takeScreenshot(a.value(1) != "original", a.value(2));
        if (path.isEmpty()) ++m_failures;
        log(line, {{"saved", path}});
    } else if (cmd == "grabwindow") {
        const bool ok = m_w->grab().save(a.value(1));
        log(line, {{"ok", ok}});
    } else if (cmd == "mousemove") {
        VideoWidget* v = m_w->video();
        const QPointF pt(v->width() / 2.0 + (m_pc % 7), v->height() / 2.0);
        QMouseEvent ev(QEvent::MouseMove, pt, v->mapToGlobal(pt), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(v, &ev);
        log(line);
    } else if (cmd == "audio") { p->setAudioTrack(a.value(1).toInt()); log(line); }
    else if (cmd == "sub") { p->setSubtitleTrack(a.value(1).toInt()); log(line); }
    else if (cmd == "hw") { p->setHardwareDecoding(a.value(1) == "on"); log(line); }
    else if (cmd == "moment") {
        // moment poweron|poweroff|static : start a set moment now (on the effect clock)
        const QString m = a.value(1);
        if (m == "static") m_w->video()->channelChange();
        else m_w->video()->startMoment(m == "poweroff" ? 2 : m == "none" ? 0 : 1);
        log(line);
    }
    else if (cmd == "osd") {
        // osd TEXT : show VCR on-screen text (if the preset enables it)
        m_w->video()->setOsdText(rest, 0);
        log(line);
    }
    else if (cmd == "gamepad") {
        // gamepad attach | tap BUTTON | press BUTTON | release BUTTON | axis NAME VALUE
        Gamepad* g = m_w->gamepad();
        bool ok = g && g->available();
        const QString op = a.value(1);
        if (ok && op == "attach") { ok = g->attachVirtual(); g->setAlwaysActive(true); }   // tests: focus-independent
        else if (ok && op == "press") ok = g->setVirtualButton(a.value(2), true);
        else if (ok && op == "release") ok = g->setVirtualButton(a.value(2), false);
        else if (ok && op == "tap") {
            ok = g->setVirtualButton(a.value(2), true);
            const QString b = a.value(2);
            QTimer::singleShot(60, g, [g, b] { g->setVirtualButton(b, false); });
        }
        else if (ok && op == "axis") ok = g->setVirtualAxis(a.value(2), a.value(3).toDouble());
        if (!ok) ++m_failures;
        log(line, {{"ok", ok}});
    }
    else if (cmd == "rate") {
        m_w->setSpeed(a.value(1).toDouble());
        log(line);
    }
    else if (cmd == "loop") {
        // loop A_MS B_MS | loop off
        if (a.value(1) == "off") m_w->setLoop(-1, -1);
        else m_w->setLoop(a.value(1).toLongLong() * 1000000, a.value(2).toLongLong() * 1000000);
        log(line);
    }
    else if (cmd == "chapter") {
        m_w->jumpChapter(a.value(1) == "prev" ? -1 : +1);
        log(line);
    }
    else if (cmd == "subfile") {
        // subfile PATH | off : load (or drop) an external subtitle file, reopening in place
        if (a.value(1) == "off") m_w->setExternalSubtitleFile(QString(), QString());
        else if (a.value(1) == "offer") { if (!m_w->loadSubtitleOffer(a.value(2).toInt())) ++m_failures; }
        else m_w->setExternalSubtitleFile(QUrl::fromLocalFile(rest).toString(QUrl::FullyEncoded), QFileInfo(rest).fileName());
        log(line);
    }
    else if (cmd == "sysreport") {
        QFile f(a.value(1));
        const bool ok = f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(m_w->systemReport().toUtf8()) > 0;
        if (!ok) ++m_failures;
        log(line, {{"ok", ok}});
    }
    else if (cmd == "thumb") {
        // thumb MS PATH : request a seek-bar preview and save it when it arrives
        const QString path = a.value(2);
        const QString uri = m_w->player()->currentUri();
        auto conn = std::make_shared<QMetaObject::Connection>();
        auto done = std::make_shared<bool>(false);
        auto finish = [=](bool ok, QSize size) {
            if (*done) return;
            *done = true;
            QObject::disconnect(*conn);
            if (!ok) ++m_failures;
            log(line, {{"ok", ok}, {"width", size.width()}, {"height", size.height()}});
            QTimer::singleShot(10, this, &Automation::next);
        };
        *conn = connect(m_w->thumbnailer(), &Thumbnailer::ready, this, [=](const QString& u, qint64, const QImage& img) {
            if (u != uri) return;
            finish(img.save(path), img.size());
        });
        m_w->thumbnailer()->request(uri, a.value(1).toLongLong() * 1000000);
        QTimer::singleShot(8000, this, [=] { finish(false, {}); });
        return;
    }
    else if (cmd == "rendertime") {
        // rendertime SECONDS|off : pin the effect clock (deterministic captures of animated effects)
        m_w->video()->setClockOverride(a.value(1) == "off" ? -1.0 : a.value(1).toDouble());
        log(line);
    }
    else if (cmd == "resetsync") { m_w->video()->resetSyncStats(); m_maxStallMs = 0; log(line); }
    else if (cmd == "report") {
        QJsonObject r = m_w->stateReport();
        r["label"] = rest;
        r["maxEventLoopStallMs"] = m_maxStallMs;
        log("report", r);
    } else if (cmd == "expect") {
        // expect <key> <value> : compares a stateReport() field (string compare, or numeric with 1% tolerance)
        const QJsonValue v = m_w->stateReport().value(a.value(1));
        const QString want = line.section(' ', 2).trimmed();
        bool ok;
        if (v.isDouble()) ok = std::abs(v.toDouble() - want.toDouble()) <= std::max(0.01 * std::abs(want.toDouble()), 1e-3);
        else if (v.isBool()) ok = (v.toBool() ? "true" : "false") == want;
        else ok = v.toVariant().toString() == want;
        if (!ok) ++m_failures;
        log(line, {{"ok", ok}, {"actual", v}});
    } else if (cmd == "desk") {
        if (a.value(1) == "on") m_w->enterDeskMode(); else m_w->leaveDeskMode();
        log(line);
        delay = 600;
    } else if (cmd == "deskpose") {
        // deskpose YAW PITCH [HEIGHT CX CY]
        if (DeskWindow* d = m_w->deskWindow()) {
            DeskView::DeskPose p = d->view()->deskPose();
            p.yaw = a.value(1).toDouble(); p.pitch = a.value(2).toDouble();
            if (a.size() > 5) { p.height = a.value(3).toDouble(); p.cx = a.value(4).toDouble(); p.cy = a.value(5).toDouble(); }
            d->view()->setDeskPose(p);
        }
        log(line);
    } else if (cmd == "deskfly") {
        if (DeskWindow* d = m_w->deskWindow()) { if (a.value(1) == "in") d->view()->flyIn(); else d->view()->flyOut(); }
        log(line);
    } else if (cmd == "deskcabinet") {
        // deskcabinet crt|flat-crt|flat-panel
        const QString c = a.value(1);
        static const QStringList names = {"crt", "flat-crt", "flat-panel", "wood-console", "pvm", "beige-monitor", "arcade"};
        if (DeskWindow* d = m_w->deskWindow()) d->view()->setCabinet(std::max<int>(0, int(names.indexOf(c))));
        log(line);
    } else if (cmd == "theaterlook") {
        // theaterlook 0..3 (35mm print, worn 16mm, drive-in, keep the current look)
        m_w->setTheaterLookSetting(a.value(1).toInt());
        log(line);
    } else if (cmd == "cg") {
        // cg palette 0..2 | floor checker|grid | stand pedestal|plinth|floating | objects|banding|reveal|orbit on|off | revealsnap
        if (DeskWindow* d = m_w->deskWindow()) {
            DeskView::Scene s = d->view()->scene();
            const QString k = a.value(1), v = a.value(2);
            const bool on = v == "on";
            if (k == "palette") s.cgPalette = v.toInt();
            else if (k == "floor") s.cgFloor = v == "grid" ? 1 : 0;
            else if (k == "stand") s.cgStand = v == "plinth" ? 1 : v == "floating" ? 2 : 0;
            else if (k == "objects") s.cgObjects = on;
            else if (k == "banding") s.cgBanding = on;
            else if (k == "reveal") s.cgReveal = on;
            else if (k == "orbit") s.cgOrbit = on;
            else if (k == "set") s.cgObjectSet = v.toInt();
            else if (k == "background") s.cgBackground = on;
            else if (k == "models") { s.modelsFolder = v == "off" ? QString() : rest.section(' ', 1); s.models = true; }   // cg models PATH
            else if (k == "finish") s.modelFinish = v.toInt();
            if (k == "revealsnap") d->view()->finishReveal();
            else m_w->setDeskScene(s);
        }
        log(line);
    } else if (cmd == "arcadeart") {
        // arcadeart 0..3 : the arcade cabinet's art (space, sunset, neon, 70s woodgrain)
        if (DeskWindow* d = m_w->deskWindow()) d->view()->setArcadeArt(a.value(1).toInt());
        log(line);
    } else if (cmd == "marqueetitle") {
        // marqueetitle RAW : how a file name is tidied for the marquee (tests)
        log(line, {{"input", rest}, {"result", DeskView::marqueeTitle(rest)}});
    } else if (cmd == "marqueeimage") {
        // marqueeimage PATH : save the marquee's title artwork (tests)
        if (DeskWindow* d = m_w->deskWindow()) d->view()->marqueeImage().save(a.value(1));
        log(line);
    } else if (cmd == "settings") {
        // settings TAB : show the settings panel on that tab (by its title)
        m_w->showSettingsTab(a.value(1));
        log(line);
    } else if (cmd == "looksound") {
        // looksound on|off | looksound volume 0..2 | looksound strength 0..1
        if (a.value(1) == "volume") m_w->setSoundLevels(a.value(2).toDouble(), m_w->settings().effectStrength);
        else if (a.value(1) == "strength") m_w->setSoundLevels(m_w->settings().noiseVolume, a.value(2).toDouble());
        else m_w->setLookSound(a.value(1) == "on");
        log(line);
    } else if (cmd == "modelswait") {
        // modelswait TIMEOUT_MS : until the models folder has finished loading
        DeskWindow* d = m_w->deskWindow();
        const int timeout = a.value(1).toInt();
        auto waited = std::make_shared<QElapsedTimer>();
        waited->start();
        auto poll = std::make_shared<std::function<void()>>();
        *poll = [this, d, timeout, waited, poll, line] {
            const bool done = !d || !d->view()->modelLibrary()->loading();
            if (done || waited->elapsed() > timeout) {
                if (!done) ++m_failures;
                log(line, {{"ok", done}, {"ms", double(waited->elapsed())}});
                QTimer::singleShot(10, this, &Automation::next);
            } else QTimer::singleShot(50, this, *poll);
        };
        QTimer::singleShot(50, this, *poll);
        return;
    } else if (cmd == "theatermarch") {
        // theatermarch ref|normal : near-exact reference rendering of the seats (tests)
        if (DeskWindow* d = m_w->deskWindow()) d->view()->setMarchReference(a.value(1) == "ref");
        log(line);
    } else if (cmd == "theatersnap") {
        // finish the curtains / masking / beam moves at once (timing-independent checks)
        if (DeskWindow* d = m_w->deskWindow()) d->view()->finishTheaterMoves();
        log(line);
    } else if (cmd == "sceneplace") {
        // sceneplace TVHEIGHT PICHEIGHT SPACING SIZE (wall scene placement)
        if (DeskWindow* d = m_w->deskWindow()) {
            DeskView::Scene s = d->view()->scene();
            s.tvHeight = a.value(1).toDouble(); s.picHeight = a.value(2).toDouble();
            s.picSpacing = a.value(3).toDouble(); s.picSize = a.value(4).toDouble();
            m_w->setDeskScene(s);
        }
        log(line);
    } else if (cmd == "scenewall" || cmd == "sceneframes" || cmd == "sceneframestyle" || cmd == "scenepicture") {
        // scenewall 0..5 ; sceneframes 0..4 (layout) ; sceneframestyle black|wood|gold ; scenepicture SLOT PATH|clear
        if (DeskWindow* d = m_w->deskWindow()) {
            DeskView::Scene s = d->view()->scene();
            if (cmd == "scenewall") s.wallStyle = a.value(1).toInt();
            else if (cmd == "sceneframes") s.frameLayout = a.value(1).toInt();
            else if (cmd == "sceneframestyle") s.frameStyle = a.value(1) == "wood" ? 1 : a.value(1) == "gold" ? 2 : 0;
            else {
                const int slot = a.value(1).toInt();
                while (s.framePaths.size() < 4) s.framePaths.append(QString());
                s.framePaths[slot] = a.value(2) == "clear" ? QString() : rest.section(' ', 1);
            }
            m_w->setDeskScene(s);
        }
        log(line);
    } else if (cmd == "scenepreviews") {
        // scenepreviews DIR : save the settings window's scene previews
        if (DeskWindow* d = m_w->deskWindow())
            for (int i = 0; i < 5; ++i) d->view()->scenePreview(i, QSize(384, 216)).save(QStringLiteral("%1/preview_%2.png").arg(a.value(1)).arg(i));
        log(line);
    } else if (cmd == "scenedialog") {
        m_w->openSceneDialog();
        log(line);
    } else if (cmd == "deskbackdrop") {
        // deskbackdrop room|desktop (1.9 name for the desk scene)
        if (DeskWindow* d = m_w->deskWindow()) d->view()->setRoom(a.value(1) == "room");
        log(line);
    } else if (cmd == "scene" || cmd == "scenemood" || cmd == "scenefog" || cmd == "scenewood" || cmd == "scenequality") {
        // scene desktop|desk ; scenemood evening|night|dark ; scenefog off|light|thick [STRENGTH]
        // scenewood walnut|oak|cherry ; scenequality low|medium|high
        if (DeskWindow* d = m_w->deskWindow()) {
            DeskView::Scene s = d->view()->scene();
            const QString v = a.value(1);
            if (cmd == "scene") s.scene = v == "desk" ? 1 : v == "wall" ? 2 : v == "theater" ? 3 : v == "cg" ? 4 : 0;
            else if (cmd == "scenemood") s.mood = v == "night" ? 1 : v == "dark" ? 2 : 0;
            else if (cmd == "scenefog") { s.fog = v == "light" ? 1 : v == "thick" ? 2 : 0; if (a.size() > 2) s.fogStrength = a.value(2).toDouble(); }
            else if (cmd == "scenewood") s.wood = v == "oak" ? 1 : v == "cherry" ? 2 : 0;
            else s.quality = v == "low" ? 0 : v == "high" ? 2 : 1;
            m_w->setDeskScene(s);
        }
        log(line);
    } else if (cmd == "deskshape") {
        if (DeskWindow* d = m_w->deskWindow()) d->view()->setClassicShape(a.value(1) == "classic");
        log(line);
    } else if (cmd == "deskwait") {
        // deskwait PHASE TIMEOUT_MS: wait for desk/flying-in/full/flying-out
        const QString want = a.value(1);
        const int timeout = a.value(2, "5000").toInt();
        const qint64 t0 = m_clock.elapsed();
        auto* poll = new QTimer(this);
        poll->setInterval(10);
        connect(poll, &QTimer::timeout, this, [=] {
            DeskWindow* d = m_w->deskWindow();
            const QString ph = d ? d->view()->phaseName() : QString();
            if (ph == want || m_clock.elapsed() - t0 > timeout) {
                poll->deleteLater();
                if (ph != want) ++m_failures;
                log(line, {{"ok", ph == want}, {"phase", ph}, {"waitedMs", double(m_clock.elapsed() - t0)}});
                QTimer::singleShot(10, this, &Automation::next);
            }
        });
        poll->start();
        return;
    } else if (cmd == "deskgrab") {
        // Framebuffer of the desk view, including alpha (transparent around the set).
        bool ok = false;
        if (DeskWindow* d = m_w->deskWindow()) ok = d->view()->grabFramebuffer().save(a.value(1));
        log(line, {{"ok", ok}});
    } else if (cmd == "deskmouse") {
        // deskmouse X Y : synthetic move over the desk view (wakes the control strip)
        if (DeskWindow* d = m_w->deskWindow()) {
            const QPointF pt(a.value(1).toDouble(), a.value(2).toDouble());
            QMouseEvent ev(QEvent::MouseMove, pt, d->view()->mapToGlobal(pt), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(d->view(), &ev);
        }
        log(line);
    } else if (cmd == "jfsignin") {
        // jfsignin URL USER PASSWORD
        m_w->showJellyfin(true);
        m_w->jellyfin()->signIn(a.value(1), a.value(2), a.value(3));
        log("jfsignin " + a.value(1) + " " + a.value(2) + " ***");   // never log the password
    } else if (cmd == "jfsignout") {
        m_w->jellyfin()->signOut();
        log(line);
    } else if (cmd == "jfhome") {
        m_w->jellyfinPanel()->showHome();
        log(line);
    } else if (cmd == "jfopen") {
        // jfopen NAME: open a folder or play a video shown in the current listing
        const bool ok = m_w->jellyfinPanel()->openByName(rest);
        if (!ok) ++m_failures;
        log(line, {{"ok", ok}});
    } else if (cmd == "jfwait") {
        // jfwait signedin|signedout|listing TIMEOUT_MS
        const QString want = a.value(1);
        const int timeout = a.value(2, "8000").toInt();
        const qint64 t0 = m_clock.elapsed();
        auto* poll = new QTimer(this);
        poll->setInterval(20);
        connect(poll, &QTimer::timeout, this, [=] {
            JellyfinClient* jf = m_w->jellyfin();
            JellyfinPanel* jp = m_w->jellyfinPanel();
            bool ok = false;
            if (want == "signedin") ok = jf->isSignedIn();
            else if (want == "signedout") ok = !jf->isSignedIn();
            else if (want == "listing") ok = jp->itemCount() > 0 && !jp->currentTitle().startsWith("Loading");
            if (ok || m_clock.elapsed() - t0 > timeout) {
                poll->deleteLater();
                if (!ok) ++m_failures;
                log(line, {{"ok", ok}, {"listing", jp->currentTitle()}, {"count", jp->itemCount()}, {"waitedMs", double(m_clock.elapsed() - t0)}});
                QTimer::singleShot(10, this, &Automation::next);
            }
        });
        poll->start();
        return;
    } else if (cmd == "quit") {
        finish(m_failures ? 1 : 0);
        return;
    } else {
        ++m_failures;
        log(line, {{"error", "unknown command"}});
    }
    QTimer::singleShot(delay, this, &Automation::next);
}
