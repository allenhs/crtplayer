#include "CutDialog.h"
#include "ControlBar.h"   // formatTime

#include <QDesktopServices>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QString t3(qint64 ns)
{
    // m:ss.mmm (or h:mm:ss.mmm): cut points need the milliseconds
    const qint64 ms = ns / 1000000;
    return formatTime(ms) + QString::asprintf(".%03lld", ms % 1000);
}
} // namespace

CutDialog::CutDialog(const Host& host, QWidget* parent) : QDialog(parent), m_host(host)
{
    setWindowTitle(tr("Cut without re-encoding"));
    setModal(false);
    auto* v = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Saves the section from A to B as a new file. The video and sound are copied as they "
                                "are: nothing is re-encoded, and the original file is never changed."));
    intro->setWordWrap(true);
    intro->setObjectName("paramValue");
    v->addWidget(intro);
    m_file = new QLabel;
    m_file->setWordWrap(true);
    v->addWidget(m_file);

    auto* g = new QGridLayout();
    m_range = new QLabel;
    m_range->setStyleSheet("font-weight: 600;");
    g->addWidget(m_range, 0, 0, 1, 4);
    auto* aNow = new QPushButton(tr("A = now"));
    auto* bNow = new QPushButton(tr("B = now"));
    auto* goA = new QPushButton(tr("Go to A"));
    auto* goB = new QPushButton(tr("Go to B"));
    aNow->setToolTip(tr("The start of the section: where the player is now"));
    bNow->setToolTip(tr("The end of the section: where the player is now"));
    g->addWidget(aNow, 1, 0);
    g->addWidget(bNow, 1, 1);
    g->addWidget(goA, 1, 2);
    g->addWidget(goB, 1, 3);
    v->addLayout(g);

    m_keyframe = new QLabel;
    m_keyframe->setWordWrap(true);
    v->addWidget(m_keyframe);
    auto* tip = new QLabel(tr("Tip: Shift+← and Shift+→ jump between keyframes, so A can sit exactly on one."));
    tip->setObjectName("paramValue");
    tip->setWordWrap(true);
    v->addWidget(tip);
    m_output = new QLabel;
    m_output->setWordWrap(true);
    m_output->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(m_output);
    m_progress = new QProgressBar;
    m_progress->setRange(0, 1000);
    m_progress->hide();
    v->addWidget(m_progress);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(m_status);

    auto* buttons = new QHBoxLayout();
    m_openFolder = new QPushButton(tr("Open folder"));
    m_play = new QPushButton(tr("Play the cut"));
    m_openFolder->hide();
    m_play->hide();
    m_cancel = new QPushButton(tr("Close"));
    m_save = new QPushButton(tr("Save cut"));
    m_save->setDefault(true);
    buttons->addWidget(m_openFolder);
    buttons->addWidget(m_play);
    buttons->addStretch(1);
    buttons->addWidget(m_cancel);
    buttons->addWidget(m_save);
    v->addLayout(buttons);
    setMinimumWidth(460);

    connect(aNow, &QPushButton::clicked, this, &CutDialog::setA);
    connect(bNow, &QPushButton::clicked, this, &CutDialog::setB);
    connect(goA, &QPushButton::clicked, this, [this] { if (m_host.loopA() >= 0) m_host.seek(m_host.loopA()); });
    connect(goB, &QPushButton::clicked, this, [this] { if (m_host.loopB() >= 0) m_host.seek(m_host.loopB()); });
    connect(m_save, &QPushButton::clicked, this, [this] { save(); });
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        if (m_cutter.isRunning()) m_cutter.cancel();
        else close();
    });
    connect(m_openFolder, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_last.output).absolutePath()));
    });
    connect(m_play, &QPushButton::clicked, this, [this] { if (!m_last.output.isEmpty()) m_host.open(m_last.output); });
    connect(&m_probe, &LosslessCutter::keyframeFound, this, [this](qint64 req, qint64 k) {
        if (req != m_host.loopA()) { refresh(); return; }   // A moved meanwhile
        if (k < 0) m_keyframe->setText(tr("The cut starts at the keyframe at or before A."));
        else if (std::llabs(k - req) < 2000000) m_keyframe->setText(tr("A is on a keyframe: the cut starts exactly there."));
        else m_keyframe->setText(tr("A copied video can only start on a keyframe: the cut starts at %1, %2 s before A.")
                                     .arg(t3(k), QLocale().toString((req - k) / 1e9, 'f', 2)));
    });
    connect(&m_cutter, &LosslessCutter::progress, this, [this](double f) { m_progress->setValue(int(f * 1000)); });
    connect(&m_cutter, &LosslessCutter::finished, this, [this](const LosslessCutter::Result& r) {
        m_last = r;
        showResult(r);
        emit finished(r);
    });
    refresh();
}

void CutDialog::setA()
{
    const qint64 b = m_host.loopB();
    const qint64 p = m_host.position();
    m_host.setLoop(p, b > p ? b : -1);
    refresh();
}

void CutDialog::setB()
{
    const qint64 a = m_host.loopA();
    const qint64 p = m_host.position();
    if (a < 0 || p <= a) { m_status->setText(tr("Set A first, before B.")); return; }
    m_host.setLoop(a, p);
    refresh();
}

void CutDialog::refresh()
{
    const QString src = m_host.sourceFile();
    const qint64 a = m_host.loopA(), b = m_host.loopB();
    if (m_cutter.isRunning()) return;
    if (src.isEmpty()) {
        m_file->setText(tr("<b>Only files on this computer can be cut</b> (not Jellyfin or network streams)."));
        m_save->setEnabled(false);
        m_range->clear();
        m_keyframe->clear();
        m_output->clear();
        return;
    }
    m_file->setText(tr("From: %1").arg(QFileInfo(src).fileName().toHtmlEscaped()));
    if (a < 0 || b <= a) {
        m_range->setText(a < 0 ? tr("Set A and B (the R key, or the buttons below).")
                               : tr("A = %1 — now set B.").arg(t3(a)));
        m_keyframe->clear();
        m_output->clear();
        m_save->setEnabled(false);
        return;
    }
    m_range->setText(tr("A = %1   B = %2   (%3 s)").arg(t3(a), t3(b), QLocale().toString((b - a) / 1e9, 'f', 2)));
    QString ext, name;
    LosslessCutter::muxerFor(src, &ext, &name);
    const QString out = LosslessCutter::outputPathFor(src, a, b, ext);
    const bool same = QFileInfo(src).suffix().toLower() == ext;
    m_output->setText(tr("New file: %1\n%2").arg(QFileInfo(out).fileName(),
        same ? tr("%1, like the original. Saved in %2").arg(name, QFileInfo(out).absolutePath())
             : tr("%1 (GStreamer can't write this file's own type here). Saved in %2").arg(name, QFileInfo(out).absolutePath())));
    m_save->setEnabled(true);
    if (m_probedA != a && !m_probe.isRunning()) {
        m_probedA = a;
        m_keyframe->setText(tr("Finding the keyframe…"));
        m_probe.findKeyframe(src, a);
    }
}

bool CutDialog::save(const QString& outPath)
{
    const QString src = m_host.sourceFile();
    if (src.isEmpty() || m_cutter.isRunning()) return false;
    const qint64 a = m_host.loopA(), b = m_host.loopB();
    if (a < 0 || b <= a) { m_status->setText(tr("Set A and B first.")); return false; }
    m_status->setText(tr("Copying…"));
    m_progress->setValue(0);
    m_progress->show();
    m_save->setEnabled(false);
    m_openFolder->hide();
    m_play->hide();
    m_cancel->setText(tr("Stop"));
    return m_cutter.start(src, a, b, outPath);
}

void CutDialog::showResult(const LosslessCutter::Result& r)
{
    m_progress->hide();
    m_cancel->setText(tr("Close"));
    m_save->setEnabled(true);
    if (!r.ok) {
        m_status->setText(r.cancelled ? tr("Stopped. Nothing was saved.") : tr("<b>Not saved:</b> %1").arg(r.error.toHtmlEscaped()));
        return;
    }
    const QLocale loc;
    QString t = tr("<b>Saved</b> %1 — %2 s, %3.").arg(QFileInfo(r.output).fileName().toHtmlEscaped(),
                                                        loc.toString(r.durationNs / 1e9, 'f', 1), loc.formattedDataSize(r.bytes));
    t += "<br>" + tr("From %1 (the keyframe) to %2.").arg(t3(r.startNs), t3(r.stopNs));
    t += "<br>" + tr("Copied: %1.").arg(r.kept.join(", ").toHtmlEscaped());
    if (!r.dropped.isEmpty())
        t += "<br>" + tr("Left out (a %1 file can't hold them): %2.").arg(r.container, r.dropped.join(", ").toHtmlEscaped());
    m_status->setText(t);
    m_openFolder->show();
    m_play->show();
    refresh();   // the next name, should it be saved again
}
