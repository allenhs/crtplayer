#pragma once
#include "edit/LosslessCutter.h"
#include <QDialog>
#include <functional>

class QLabel;
class QPushButton;
class QProgressBar;

// "Cut A–B without re-encoding": sets the section (A and B from the player, or "now"),
// shows where the cut really starts (the keyframe at or before A) and what it will write,
// then saves it as a new file with LosslessCutter. The original is never changed.
class CutDialog : public QDialog {
    Q_OBJECT
public:
    struct Host {   // what the dialog needs from the player window
        std::function<QString()> sourceFile;          // local file playing ("" if none)
        std::function<qint64()> position;             // ns
        std::function<qint64()> duration;             // ns
        std::function<qint64()> loopA, loopB;         // ns, -1 unset
        std::function<void(qint64, qint64)> setLoop;  // ns
        std::function<void(qint64)> seek;             // ns
        std::function<void(const QString&)> open;     // play a file (the new cut)
    };
    CutDialog(const Host& host, QWidget* parent = nullptr);

    void refresh();                                   // A/B or the file changed
    bool save(const QString& outPath = QString());    // -> finished
    bool isBusy() const { return m_cutter.isRunning(); }
    LosslessCutter::Result lastResult() const { return m_last; }

signals:
    void finished(const LosslessCutter::Result& r);

private:
    void setA();
    void setB();
    void showResult(const LosslessCutter::Result& r);

    Host m_host;
    LosslessCutter m_cutter, m_probe;
    QLabel* m_file;
    QLabel* m_range;
    QLabel* m_keyframe;
    QLabel* m_output;
    QLabel* m_status;
    QProgressBar* m_progress;
    QPushButton* m_save;
    QPushButton* m_cancel;
    QPushButton* m_openFolder;
    QPushButton* m_play;
    LosslessCutter::Result m_last;
    qint64 m_probedA = -2;
};
