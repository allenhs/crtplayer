#pragma once
#include <QSlider>
#include <QVector>
#include <QWidget>

class QLabel;
class QToolButton;
class QComboBox;

// Seek slider that jumps to the clicked position and reports scrubbing.
class SeekSlider : public QSlider {
    Q_OBJECT
public:
    explicit SeekSlider(QWidget* parent = nullptr);
    bool isScrubbing() const { return m_scrubbing; }
    // Chapter ticks and the A-B loop range (ms; < 0 = unset) drawn on the groove.
    void setMarks(const QVector<qint64>& chaptersMs, qint64 loopA, qint64 loopB);
    // With a rich hover, hover positions are reported (for the preview popup) instead of
    // showing the plain time tooltip.
    void setRichHover(bool on) { m_richHover = on; }
    int xForValue(qint64 v) const;
signals:
    void hovered(qint64 ms, int x);
    void hoverLeft();
    void scrubStarted();
    void scrubbedTo(qint64 ms);
    void scrubFinished(qint64 ms);
protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void leaveEvent(QEvent* e) override;
private:
    qint64 valueAt(int x) const;
    bool m_scrubbing = false;
    bool m_richHover = false;
    QVector<qint64> m_chapters;
    qint64 m_loopA = -1, m_loopB = -1;
};

class ControlBar : public QWidget {
    Q_OBJECT
public:
    explicit ControlBar(QWidget* parent = nullptr);

    SeekSlider* seek() const { return m_seek; }
    QSlider* volumeSlider() const { return m_volume; }
    QComboBox* presetCombo() const { return m_preset; }

    QToolButton* openButton;
    QToolButton* prevButton;
    QToolButton* playButton;
    QToolButton* nextButton;
    QToolButton* stepBackButton;
    QToolButton* stepFwdButton;
    QToolButton* muteButton;
    QToolButton* crtButton;
    QToolButton* compareButton;
    QToolButton* aspectButton;
    QToolButton* audioButton;
    QToolButton* subtitleButton;
    QToolButton* screenshotButton;
    QToolButton* playlistButton;
    QToolButton* jellyfinButton;
    QToolButton* browseButton;
    QToolButton* settingsButton;
    QToolButton* fullscreenButton;

    void setPlaying(bool playing);
    void setMutedIcon(bool muted);
    void setFullscreenIcon(bool fs);
    void setTimes(qint64 posMs, qint64 durMs);
    void setDuration(qint64 durMs);
    bool isInteracting() const;

signals:
    void volumeChanged(double v01);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    SeekSlider* m_seek;
    QLabel* m_time;
    class QHBoxLayout* m_row = nullptr;
    void adaptToWidth(int w);
    QSlider* m_volume;
    QComboBox* m_preset;
    qint64 m_dur = 0;
};

QString formatTime(qint64 ms, bool forceHours = false);
