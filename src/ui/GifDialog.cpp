#include "GifDialog.h"
#include "ControlBar.h"   // formatTime

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

GifDialog::GifDialog(const Host& host, const Settings& st, QWidget* parent)
    : QDialog(parent), m_host(host), m_rec(host.recorder)
{
    setWindowTitle(tr("Save GIF clip"));
    setModal(false);
    auto* v = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Plays the A–B section once (or the next 5 seconds when A and B aren't set) and saves it "
                                "as an animated GIF, just as it looks on screen."));
    intro->setWordWrap(true);
    intro->setObjectName("paramValue");
    v->addWidget(intro);
    m_range = new QLabel;
    m_range->setStyleSheet("font-weight: 600;");
    m_range->setWordWrap(true);
    v->addWidget(m_range);
    auto* form = new QFormLayout();
    m_width = new QComboBox;
    for (int w : {320, 480, 640, 800, 1024}) m_width->addItem(tr("%1 pixels wide").arg(w), w);
    m_fps = new QComboBox;
    for (int f : {10, 15, 20, 25, 30}) m_fps->addItem(tr("%1 frames a second").arg(f), f);
    m_look = new QComboBox;
    m_look->addItem(tr("With the CRT look"), true);
    m_look->addItem(tr("The original picture"), false);
    form->addRow(tr("Size"), m_width);
    form->addRow(tr("Smoothness"), m_fps);
    form->addRow(tr("Picture"), m_look);
    v->addLayout(form);
    auto* note = new QLabel(tr("Bigger and smoother means a larger file: 480 pixels at 15 frames a second is a good start. "
                               "At most %1 seconds.").arg(GifRecorder::kMaxLengthNs / 1000000000));
    note->setWordWrap(true);
    note->setObjectName("paramValue");
    v->addWidget(note);
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
    m_openFolder->hide();
    m_close = new QPushButton(tr("Close"));
    m_record = new QPushButton(tr("Record GIF"));
    m_record->setDefault(true);
    buttons->addWidget(m_openFolder);
    buttons->addStretch(1);
    buttons->addWidget(m_close);
    buttons->addWidget(m_record);
    v->addLayout(buttons);
    setMinimumWidth(420);

    auto select = [](QComboBox* c, const QVariant& d) { const int i = c->findData(d); if (i >= 0) c->setCurrentIndex(i); };
    select(m_width, st.width);
    select(m_fps, st.fps);
    select(m_look, st.look);
    auto save = [this] { if (m_host.saveSettings) m_host.saveSettings(settings()); };
    connect(m_width, &QComboBox::currentIndexChanged, this, save);
    connect(m_fps, &QComboBox::currentIndexChanged, this, save);
    connect(m_look, &QComboBox::currentIndexChanged, this, save);
    connect(m_record, &QPushButton::clicked, this, [this] { record(); });
    connect(m_close, &QPushButton::clicked, this, [this] { if (m_rec.isRecording()) m_rec.cancel(); else close(); });
    connect(m_openFolder, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_last.path).absolutePath()));
    });
    connect(&m_rec, &GifRecorder::progress, this, [this](double done, double total) {
        m_progress->setValue(int(1000 * done / std::max(0.001, total)));
        m_status->setText(tr("Recording… %1 of %2 s").arg(QLocale().toString(done, 'f', 1), QLocale().toString(total, 'f', 1)));
    });
    connect(&m_rec, &GifRecorder::encoding, this, [this](int left) {
        m_progress->setRange(0, 0);
        m_status->setText(tr("Finishing the GIF (%1 frames)…").arg(left));
    });
    connect(&m_rec, &GifRecorder::finished, this, [this](const GifRecorder::Result& r) {
        m_last = r;
        if (m_host.recording) m_host.recording(false);
        m_progress->hide();
        m_progress->setRange(0, 1000);
        m_record->setEnabled(true);
        m_close->setText(tr("Close"));
        if (r.ok) {
            QString t = tr("<b>Saved</b> %1 — %2 frames, %3 × %4, %5 s, %6.")
                            .arg(QFileInfo(r.path).fileName().toHtmlEscaped()).arg(r.frames).arg(r.width).arg(r.height)
                            .arg(QLocale().toString(r.seconds, 'f', 1), QLocale().formattedDataSize(r.bytes));
            if (r.truncated) t += "<br>" + tr("The section was longer than %1 s: its start was saved.").arg(GifRecorder::kMaxLengthNs / 1000000000);
            const double got = r.seconds > 0 ? r.frames / r.seconds : 0;
            if (got > 0 && got < 0.8 * settings().fps)
                t += "<br>" + tr("This computer managed %1 frames a second, not %2: the GIF still plays at the right speed.")
                                  .arg(QLocale().toString(got, 'f', 0)).arg(settings().fps);
            m_status->setText(t);
            m_openFolder->show();
        } else {
            m_status->setText(tr("<b>Not saved:</b> %1").arg(r.error.toHtmlEscaped()));
        }
        emit finished(r);
    });
    refresh();
}

GifDialog::Settings GifDialog::settings() const
{
    return {m_width->currentData().toInt(), m_fps->currentData().toInt(), m_look->currentData().toBool()};
}

void GifDialog::section(qint64* a, qint64* b) const
{
    const qint64 la = m_host.loopA(), lb = m_host.loopB();
    if (la >= 0 && lb > la) { *a = la; *b = lb; return; }
    *a = m_host.position();
    *b = *a + 5'000'000'000LL;
    const qint64 d = m_host.duration();
    if (d > 0) *b = std::min(*b, d);
}

void GifDialog::refresh()
{
    if (m_rec.isBusy()) return;
    qint64 a = 0, b = 0;
    section(&a, &b);
    const bool ab = m_host.loopA() >= 0 && m_host.loopB() > m_host.loopA();
    QString t = ab ? tr("A–B: %1 to %2 (%3 s)") : tr("From here: %1 to %2 (%3 s; set A and B with R for another section)");
    t = t.arg(formatTime(a / 1000000), formatTime(b / 1000000), QLocale().toString((b - a) / 1e9, 'f', 1));
    if (m_host.deskMode && m_host.deskMode()) t += "\n" + tr("Desk mode: the whole scene is recorded.");
    m_range->setText(t);
    m_look->setEnabled(!(m_host.deskMode && m_host.deskMode()));
}

bool GifDialog::record(const QString& pathIn)
{
    if (m_rec.isBusy()) return false;
    qint64 a = 0, b = 0;
    section(&a, &b);
    if (b <= a) { m_status->setText(tr("Nothing to record here.")); return false; }
    const QString dir = m_host.folder();
    QDir().mkpath(dir);
    const QString path = pathIn.isEmpty() ? GifRecorder::pathFor(dir, m_host.title(), a, b) : pathIn;
    const Settings s = settings();
    if (m_host.setLook) m_host.setLook(s.look);
    if (m_host.recording) m_host.recording(true);
    m_openFolder->hide();
    m_record->setEnabled(false);
    m_close->setText(tr("Stop"));
    m_progress->setValue(0);
    m_progress->show();
    m_status->setText(tr("Getting ready…"));
    if (!m_rec.start(a, b, s.fps, s.width, path)) {
        if (m_host.recording) m_host.recording(false);
        m_record->setEnabled(true);
        m_progress->hide();
        return false;
    }
    return true;
}
