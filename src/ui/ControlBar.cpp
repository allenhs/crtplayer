#include "Theme.h"
#include <QPainter>
#include "ControlBar.h"
#include "Icons.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

QString formatTime(qint64 ms, bool forceHours)
{
    if (ms < 0) ms = 0;
    const qint64 s = ms / 1000;
    const int h = int(s / 3600), m = int((s / 60) % 60), sec = int(s % 60);
    if (h > 0 || forceHours) return QString::asprintf("%d:%02d:%02d", h, m, sec);
    return QString::asprintf("%d:%02d", m, sec);
}

SeekSlider::SeekSlider(QWidget* parent) : QSlider(Qt::Horizontal, parent)
{
    setObjectName("seekSlider");
    setMouseTracking(true);
    setRange(0, 0);
    setFocusPolicy(Qt::NoFocus);
}

qint64 SeekSlider::valueAt(int x) const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
    const int span = groove.width() - handle.width();
    const int pos = x - groove.x() - handle.width() / 2;
    return QStyle::sliderValueFromPosition(minimum(), maximum(), pos, std::max(1, span));
}

void SeekSlider::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || maximum() <= 0) return;
    m_scrubbing = true;
    emit scrubStarted();
    setValue(int(valueAt(int(e->position().x()))));
    emit scrubbedTo(value());
    e->accept();
}

void SeekSlider::mouseMoveEvent(QMouseEvent* e)
{
    if (maximum() > 0) {
        const qint64 v = valueAt(int(e->position().x()));
        if (m_richHover) emit hovered(v, int(e->position().x()));
        else QToolTip::showText(e->globalPosition().toPoint() - QPoint(0, 42), formatTime(v, maximum() >= 3600000), this);
        if (m_scrubbing) {
            setValue(int(v));
            emit scrubbedTo(v);
        }
    }
    e->accept();
}

void SeekSlider::leaveEvent(QEvent* e)
{
    emit hoverLeft();
    QSlider::leaveEvent(e);
}

void SeekSlider::setMarks(const QVector<qint64>& chaptersMs, qint64 loopA, qint64 loopB)
{
    m_chapters = chaptersMs;
    m_loopA = loopA;
    m_loopB = loopB;
    update();
}

int SeekSlider::xForValue(qint64 v) const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
    const int span = std::max(1, groove.width() - handle.width());
    return groove.x() + handle.width() / 2 + QStyle::sliderPositionFromValue(minimum(), std::max(1, maximum()), int(v), span);
}

void SeekSlider::paintEvent(QPaintEvent* e)
{
    QSlider::paintEvent(e);
    if (maximum() <= 0 || (m_chapters.isEmpty() && m_loopA < 0)) return;
    QPainter p(this);
    const int cy = height() / 2;
    if (m_loopA >= 0) {   // A-B loop: shaded range with end markers
        const int xa = xForValue(m_loopA);
        const int xb = m_loopB >= 0 ? xForValue(m_loopB) : xa;
        QColor acc = Theme::accent();
        acc.setAlpha(90);
        p.fillRect(QRect(xa, cy - 5, std::max(2, xb - xa), 10), acc);
        p.fillRect(QRect(xa - 1, cy - 8, 2, 16), Theme::accent());
        if (m_loopB >= 0) p.fillRect(QRect(xb - 1, cy - 8, 2, 16), Theme::accent());
    }
    for (qint64 c : m_chapters) {   // chapter ticks
        if (c <= 0) continue;
        p.fillRect(QRect(xForValue(c) - 1, cy - 4, 2, 8), QColor(232, 228, 218, 170));
    }
}

void SeekSlider::mouseReleaseEvent(QMouseEvent* e)
{
    if (!m_scrubbing) return;
    m_scrubbing = false;
    setValue(int(valueAt(int(e->position().x()))));
    emit scrubFinished(value());
    e->accept();
}

static QToolButton* makeButton(QWidget* parent, const QString& icon, const QString& tip, bool checkable = false)
{
    auto* b = new QToolButton(parent);
    b->setIcon(Icons::get(icon));
    b->setIconSize(QSize(22, 22));
    b->setToolTip(tip);
    b->setCheckable(checkable);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

ControlBar::ControlBar(QWidget* parent) : QWidget(parent)
{
    setObjectName("controlBar");
    setAttribute(Qt::WA_StyledBackground, true);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(14, 8, 14, 8);
    v->setSpacing(2);

    m_seek = new SeekSlider(this);
    v->addWidget(m_seek);

    auto* h = new QHBoxLayout();
    h->setSpacing(2);
    m_row = h;
    v->addLayout(h);

    openButton = makeButton(this, "open", tr("Open files… (Ctrl+O)"));
    prevButton = makeButton(this, "prev", tr("Previous in playlist (Page Up)"));
    playButton = makeButton(this, "play", tr("Play / pause (Space)"));
    playButton->setIconSize(QSize(28, 28));
    nextButton = makeButton(this, "next", tr("Next in playlist (Page Down)"));
    stepBackButton = makeButton(this, "stepback", tr("Previous frame (,)"));
    stepFwdButton = makeButton(this, "stepfwd", tr("Next frame (.)"));
    m_time = new QLabel("0:00 / 0:00", this);
    m_time->setObjectName("timeLabel");
    QFont tf = m_time->font();
    tf.setStyleHint(QFont::Monospace);
    tf.setFamilies({"DejaVu Sans Mono", "Noto Sans Mono", "monospace"});
    m_time->setFont(tf);
    m_time->setFixedWidth(QFontMetrics(tf).horizontalAdvance("0:00:00 / 0:00:00") + 8);

    m_preset = new QComboBox(this);
    m_preset->setToolTip(tr("CRT preset ([ and ] cycle presets)"));
    m_preset->setMinimumWidth(190);
    m_preset->setFocusPolicy(Qt::NoFocus);
    crtButton = makeButton(this, "crt", tr("CRT effects on/off (B)"), true);
    compareButton = makeButton(this, "compare", tr("Before/after comparison (C) — drag the divider"), true);
    aspectButton = makeButton(this, "aspect", tr("Scaling: Original / Fit / Fill / Crop (Z)"));
    aspectButton->setPopupMode(QToolButton::InstantPopup);
    audioButton = makeButton(this, "audio", tr("Audio track (A cycles)"));
    audioButton->setPopupMode(QToolButton::InstantPopup);
    subtitleButton = makeButton(this, "subtitles", tr("Subtitles (J cycles, V toggles)"));
    subtitleButton->setPopupMode(QToolButton::InstantPopup);
    screenshotButton = makeButton(this, "camera", tr("Screenshot (S saves the default type)"));
    screenshotButton->setPopupMode(QToolButton::InstantPopup);
    muteButton = makeButton(this, "volume", tr("Mute (M)"));
    m_volume = new QSlider(Qt::Horizontal, this);
    m_volume->setRange(0, 100);
    m_volume->setFixedWidth(96);
    m_volume->setFocusPolicy(Qt::NoFocus);
    m_volume->setToolTip(tr("Volume (Up/Down)"));
    playlistButton = makeButton(this, "playlist", tr("Playlist (L)"), true);
    browseButton = makeButton(this, "browse", tr("Browse YouTube: your channels, search, watch later (Ctrl+B)"));
    jellyfinButton = makeButton(this, "server", tr("Jellyfin library (Ctrl+J)"), true);
    settingsButton = makeButton(this, "tune", tr("CRT and picture settings (E)"), true);
    fullscreenButton = makeButton(this, "fullscreen", tr("Fullscreen (F)"));

    h->addWidget(openButton);
    h->addSpacing(6);
    h->addWidget(prevButton);
    h->addWidget(playButton);
    h->addWidget(nextButton);
    h->addWidget(stepBackButton);
    h->addWidget(stepFwdButton);
    h->addSpacing(8);
    h->addWidget(m_time);
    h->addStretch(1);
    h->addWidget(m_preset);
    h->addWidget(crtButton);
    h->addWidget(compareButton);
    h->addSpacing(8);
    h->addWidget(aspectButton);
    h->addWidget(audioButton);
    h->addWidget(subtitleButton);
    h->addWidget(screenshotButton);
    h->addSpacing(8);
    h->addWidget(muteButton);
    h->addWidget(m_volume);
    h->addSpacing(8);
    h->addWidget(playlistButton);
    h->addWidget(browseButton);
    h->addWidget(jellyfinButton);
    h->addWidget(settingsButton);
    h->addWidget(fullscreenButton);

    connect(m_volume, &QSlider::valueChanged, this, [this](int v) { emit volumeChanged(v / 100.0); });
}

void ControlBar::setPlaying(bool playing) { playButton->setIcon(Icons::get(playing ? "pause" : "play")); }
void ControlBar::setMutedIcon(bool m) { muteButton->setIcon(Icons::get(m ? "mute" : "volume")); }
void ControlBar::setFullscreenIcon(bool fs)
{
    fullscreenButton->setIcon(Icons::get(fs ? "unfullscreen" : "fullscreen"));
    fullscreenButton->setToolTip(fs ? tr("Leave fullscreen (F / Esc)") : tr("Fullscreen (F)"));
}

void ControlBar::setDuration(qint64 durMs)
{
    m_dur = std::max<qint64>(0, durMs);
    m_seek->setRange(0, int(std::min<qint64>(m_dur, INT_MAX)));
}

void ControlBar::setTimes(qint64 posMs, qint64 durMs)
{
    if (durMs != m_dur) setDuration(durMs);
    if (!m_seek->isScrubbing()) {
        QSignalBlocker b(m_seek);
        m_seek->setValue(int(std::min<qint64>(posMs, INT_MAX)));
    }
    const bool hours = m_dur >= 3600000;
    const qint64 shown = m_seek->isScrubbing() ? m_seek->value() : posMs;
    m_time->setText(formatTime(shown, hours) + " / " + (m_dur > 0 ? formatTime(m_dur, hours) : QStringLiteral("--:--")));
}

bool ControlBar::isInteracting() const
{
    if (m_seek->isScrubbing() || underMouse()) return true;
    for (auto* b : findChildren<QToolButton*>())
        if (b->isDown() || (b->menu() && b->menu()->isVisible())) return true;
    return m_preset->view()->isVisible();
}

void ControlBar::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    adaptToWidth(e->size().width());
}

void ControlBar::adaptToWidth(int w)
{
    // Drop lower-priority controls as the bar narrows, measuring the real row width,
    // so nothing ever overlaps. Every hidden control also has a keyboard shortcut and
    // a place in the side panels.
    const QList<QList<QWidget*>> dropOrder = {
        {m_preset}, {stepBackButton, stepFwdButton}, {m_volume}, {prevButton, nextButton},
        {compareButton}, {jellyfinButton}, {browseButton}, {playlistButton}, {aspectButton}, {openButton}};
    for (const auto& group : dropOrder)
        for (QWidget* x : group) x->setVisible(true);
    const QMargins m = layout()->contentsMargins();
    const int avail = w - m.left() - m.right();
    for (const auto& group : dropOrder) {
        m_row->invalidate();
        if (m_row->minimumSize().width() <= avail) break;
        for (QWidget* x : group) x->setVisible(false);
    }
}
