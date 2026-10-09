#pragma once
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

class MainWindow;
class QWidget;

// Scripted driver used for verification (--automation script.txt --automation-log out.json).
// It exercises the same public MainWindow/Player API that the UI uses. See docs/AUTOMATION.md.
class Automation : public QObject {
    Q_OBJECT
public:
    Automation(MainWindow* w, const QString& scriptPath, const QString& logPath, QObject* parent = nullptr);
    ~Automation() override;
    bool start();
private:
    void next();
    void log(const QString& cmd, const QJsonObject& data = {});
    void finish(int code);
    MainWindow* m_w;
    QString m_script, m_logPath;
    QStringList m_lines;
    int m_pc = 0;
    qint64 m_stepStart = 0;
    QJsonArray m_log;
    QElapsedTimer m_clock;
    QTimer m_loopProbe;
    qint64 m_lastProbe = 0;
    double m_maxStallMs = 0;
    int m_failures = 0;
    QPointer<QWidget> m_backdrop;   // "backdrop": a plain window behind the player
};
