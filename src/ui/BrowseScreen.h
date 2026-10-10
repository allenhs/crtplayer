#pragma once
// The browser of web videos (2.18): a full-window screen of channels and videos, laid out like a game
// console's channel menu (twelve tiles to a page, a tray with the clock below), in the player's own look.
// Made to be used from the couch: arrow keys or a game controller do everything, the mouse works too.
//
//   New            the newest videos of the channels followed
//   Channels       the channels followed (and adding one: a link, or Google Takeout's list)
//   Watch later    videos put aside
//   History        what was watched
//   Search         YouTube's search
//
// A video opens a page of its own (a large picture, what it is, Play / Resume, Watch later, its channel,
// Follow), as a channel on a console's menu opens its preview before it starts.
#include "online/WebBrowse.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QPixmap>
#include <QPointer>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

#include <functional>

class QLineEdit;

class BrowseScreen : public QWidget {
    Q_OBJECT
public:
    explicit BrowseScreen(WebBrowse* data, QWidget* parent);

    void openScreen();
    void closeScreen();
    bool isOpen() const { return m_open; }
    // How far a video was watched (ms, 0: not begun), for the bar along its tile.
    void setResumeLookup(std::function<qint64(const QString& url)> f) { m_resume = std::move(f); }
    // A video is open in the player: the tray offers to go back to it.
    void setNowPlaying(const QString& title) { m_nowPlaying = title; update(); }
    void setYtDlpState(const QString& installed, const QString& newest);
    // The fetch of a new yt-dlp has ended (the list that failed is asked for again).
    void ytDlpUpdated(bool ok, const QString& message);

    QJsonObject report() const;
    // For the tests: the same as pressing a key (by name: "Left", "Return", "Escape", "W", ...).
    bool pressKey(const QString& name);
    bool chooseSection(const QString& name);
    QWidget* keyTarget();
    void resetPaintStats() { m_paints = 0; m_paintMsTotal = m_paintMsMax = 0; }                        // where a key goes (the line being typed into, or the menu)
    void searchFor(const QString& text);
    bool typeText(const QString& text);          // into the line shown (a search, a channel's link), and Enter
    int importFrom(const QString& path);         // Google Takeout's subscriptions.csv: how many were new (-1: none found)

signals:
    void playRequested(const WebItem& item, bool fromStart);
    void closeRequested();
    void updateYtDlpRequested();

protected:
    bool event(QEvent* e) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void keyPressEvent(QKeyEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    enum class Section { New, Channels, Later, History, Search, Channel };
    enum class Focus { Tabs, Grid, Tray, Detail, Input };
    enum class TileKind { Video, Channel, AddChannel, Import, Empty };
    struct Tile {
        TileKind kind = TileKind::Video;
        WebItem item;
        WebChannel channel;
        int fresh = 0;            // new videos of a channel; a video not seen before ("NEW")
    };
    struct View {                 // what a section shows
        QList<Tile> tiles;
        int page = 0, selected = 0;
        bool loading = false;
        QString error, detail;    // the last fetch failed (the tiles are then what was kept, if anything)
        bool cached = false;
        QDateTime fetchedAt;
        int failedChannels = 0;
    };

    // ---- what is shown
    void enterSection(Section s, bool refresh = false);
    void load(Section s, bool refresh);
    void setList(Section s, const WebList& l);
    void rebuildLocal(Section s);   // channels, watch later, history: from what is kept
    View& view() { return m_views[int(m_section)]; }
    const View& view() const { return m_views[int(m_section)]; }
    int pages() const;
    int pageCount(const View& v) const;
    QString sectionTitle() const;
    QString actionLabel() const;    // the chip beside the title: Refresh, Follow, Import, Clear (empty: none)
    void runAction();
    void openDetail(const Tile& t);
    void closeDetail();
    void activate();
    void openChannel(const WebChannel& c);
    void leaveChannel();
    QString backLabel() const;
    void startInput(bool channelLink);
    void finishInput();
    void importTakeout();
    QList<QString> detailButtons() const;
    void pressDetailButton(int i);

    // ---- moving about
    void moveGrid(int dx, int dy);
    void select(int index, bool animate = true);
    void goPage(int page, int direction);
    void setFocusArea(Focus f);

    // ---- layout (all in pixels of this widget)
    struct Layout {
        double s = 1;
        QRectF back;   // a channel's page: back to where it was opened from
        QRectF header, title, tabsRow, grid, tray, clock, trayLeft, trayRight, arrowLeft, arrowRight, dots, banner, action, input;
        QList<QRectF> tabs;
        QList<QRectF> cells;   // the twelve places of a page
        QRectF detailTv, detailText;
        QList<QRectF> detailButtons;
    };
    Layout m_l;
    void relayout();

    // ---- painting
    void paintBackground(QPainter& p);
    void paintHeader(QPainter& p);
    void paintGrid(QPainter& p);
    void paintTile(QPainter& p, const Tile& t, const QRectF& r, double lift, bool focused, int index);
    void paintScreenGlass(QPainter& p, const QPainterPath& screen, const QRectF& r);
    void paintTray(QPainter& p);
    void paintTrayBand(QPainter& p);
    void paintTrayFront(QPainter& p, double hump);
    QPixmap m_trayPicture;
    void paintDetail(QPainter& p);
    void paintMessage(QPainter& p);
    QPixmap thumb(const QString& url, const QSize& size);
    double tileMargin() const;
    QString tileKey(const Tile& t, const QSizeF& cell, bool lifted) const;
    const QPixmap& tilePicture(const Tile& t, const QSizeF& cell, bool lifted);
    void warmTiles();
    QHash<QString, QPixmap> m_tiles;
    bool m_warmQueued = false;
    QPixmap avatar(const QString& url, int size);
    void want(const QString& url);
    QFont font(double px, int weight = 400) const;
    void say(const QString& text);   // a line under the tabs, for a few seconds

    WebBrowse* m_data;
    bool m_open = false;
    Section m_section = Section::New, m_beforeChannel = Section::Channels;
    Focus m_focus = Focus::Grid;
    int m_tab = 0;                 // (Focus::Tabs) which of the tabs, or the action chip after them
    int m_trayButton = 0;
    View m_views[6];
    WebChannel m_channel;          // the channel page shown
    Tile m_detail;                 // the video whose page is open
    int m_detailButton = 0;
    bool m_detailOpen = false;
    QRectF m_detailFrom;           // the tile it grew out of
    QString m_nowPlaying, m_ytInstalled, m_ytNewest, m_ytMessage;
    bool m_ytUpdating = false;
    QLineEdit* m_input = nullptr;
    bool m_inputChannel = false;   // the line asks for a channel's link (otherwise: a search)
    std::function<qint64(const QString&)> m_resume;

    QHash<QString, QImage> m_images;
    QHash<QString, QPixmap> m_scaled;
    QHash<QString, bool> m_asked;
    QPixmap m_background;
    QHash<QString, qint64> m_seenFeed;   // (the New section: what was there before, so that the rest is new)

    // animations
    QVariantAnimation m_lift, m_slide, m_detailAnim, m_fade;
    int m_prevSelected = -1;
    int m_slideDir = 0;
    QTimer m_clock, m_shimmer, m_noteTimer;
    QElapsedTimer m_shimmerClock;
    int m_hover = -1;
    int m_wheel = 0;
    int m_paints = 0;
    double m_paintMsTotal = 0, m_paintMsMax = 0;
};
