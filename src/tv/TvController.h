#pragma once
#include "tv/TvSchedule.h"

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QSizeF>
#include <QStringList>
#include <QTimer>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

class JellyfinClient;

// One cable channel: where its programmes come from, and (once scanned) its line-up.
struct TvChannel {
    int number = 2;
    QString name;
    QStringList folders;                       // folders on this computer, with their subfolders
    struct Jf { QString id, name; QVector<TvProgram> items; };
    QVector<Jf> jellyfin;                      // Jellyfin libraries, series or folders
    QString bumperFolder;                      // optional: short clips played between programmes
    bool shuffle = true;
    quint32 seed = 0;
    // from scanning
    TvLineup lineup;
    int pending = 0;                           // files whose length is still being read
    int files = 0;
    QStringList fileList, bumperList;          // the video files found, in order
    bool fetching = false;                     // Jellyfin items being listed
    QString problem;
    bool ready() const { return pending == 0 && !fetching; }
};

// Cable TV mode: channels made from folders and Jellyfin libraries that "broadcast" around
// the clock (TvSchedule), channel flipping, and the on-screen graphics: channel number,
// now/next banners and a scrolling programme guide, drawn into the picture.
//
// It decides what to play and draws the overlay; the window does the playing (Host).
class TvController : public QObject {
    Q_OBJECT
public:
    struct Host {
        // Play a source from offsetNs. burst: with static, as when changing channel.
        std::function<void(const QString& source, qint64 offsetNs, const QString& title, bool burst)> play;
        std::function<void()> snow;                         // nothing to show: static
        std::function<void(const QImage&)> setOverlay;      // null: none
        std::function<double()> pictureAspect;              // of the picture on screen
        std::function<qint64()> positionMs;                 // of the player
        std::function<int()> framesShown;                   // of what play() asked for: frames decoded so far (0 while it opens)
        JellyfinClient* jellyfin = nullptr;
    };
    explicit TvController(const Host& host, const QString& storePath, QObject* parent = nullptr);
    ~TvController() override;

    // Channels
    const QVector<TvChannel>& channels() const { return m_channels; }
    const TvChannel* channel(int number) const;
    int addFolderChannel(const QString& folder, int number = 0, const QString& name = QString());
    int addJellyfinChannel(const QString& itemId, const QString& itemName, int number = 0);
    void removeChannel(int number);
    void renumber(int number, int newNumber);
    void setName(int number, const QString& name);
    void setShuffle(int number, bool shuffle);
    void setBumperFolder(int number, const QString& folder);
    void rescan(int number);
    bool allReady() const;

    // Watching
    bool isOn() const { return m_on; }
    void setOn(bool on);
    int currentChannel() const { return m_current; }
    void tune(int number, bool burst = true);
    void channelStep(int dir);            // +1 up, -1 down (wraps)
    void digit(int d);                    // channel number entry
    bool guideVisible() const { return m_guide; }
    void setGuide(bool on);
    void programEnded();                  // the player reached the end
    void programFailed(const QString& why);
    TvSlot currentSlot() const { return m_slot; }

    qint64 nowMs() const;
    void setClock(qint64 wallMs);         // tests: the wall clock reads this now (and runs on)
    void setTestSeeds(bool on) { m_testSeeds = on; }   // tests: new channels get a fixed place in their rounds
    QJsonObject report() const;
    QImage overlayImage() const { return m_overlay; }
    static QString tidyTitle(const QString& fileName);

signals:
    void channelsChanged();               // list, names, scanning progress
    void stateChanged();                  // on/off, channel, programme

private:
    struct CacheEntry { qint64 mtime = 0, size = 0, ms = 0; };
    TvChannel* find(int number);
    int freeNumber() const;
    void load();
    void save() const;
    void scanLocal(TvChannel& ch);
    void fetchJellyfin(int number, int index);
    void rebuildLineup(TvChannel& ch);
    void probed(const QString& path, qint64 ms);
    void workerLoop();
    void playSlot(const TvSlot& slot, bool burst, bool whole = false);
    void tick();
    void noteStarted();                   // the programme asked for is on screen and running
    void updateOverlay();
    void paintGuide(class QPainter& p, const QSize& size, qint64 now);

    Host m_host;
    QString m_storePath;
    QVector<TvChannel> m_channels;
    QHash<QString, CacheEntry> m_cache;                // file -> length
    QHash<QString, QVector<int>> m_waiting;            // file being probed -> channels waiting for it
    // length-reading worker
    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<QString> m_queue;
    bool m_quit = false;

    bool m_on = false;
    int m_current = 0;
    TvSlot m_slot;
    bool m_guide = false;
    QString m_digits;
    QTimer m_tick, m_digitTimer, m_saveTimer;
    QElapsedTimer m_anim;
    qint64 m_clockOffset = 0;
    qint64 m_badgeUntil = 0, m_bannerUntil = 0;        // wall clock ms
    QString m_message;                                 // "NO PROGRAMMES", "NO SIGNAL"...
    QImage m_overlay;
    QString m_overlayKey;                              // what the overlay shows (redrawn when it changes)
    int m_tunes = 0, m_programsPlayed = 0, m_bumpersPlayed = 0;
    qint64 m_requestedAt = 0;                          // when the player was last asked to play (real time, ms)
    double m_openLagMs = 300;                          // how long files take to open here (running average)
    qint64 m_requestStartMs = 0;                       // where in the file it was asked to start
    TvSlot m_next;                                     // what follows m_slot
    bool m_failed = false;
    bool m_testSeeds = false;
    bool m_tunedAwaitingPicture = false;               // just tuned: the badge runs from when the picture arrives
    qint64 m_retryAt = 0;
    QHash<int, QVector<TvSlot>> m_guideSlots;          // per channel, for the guide's window
    qint64 m_guideSlotsAt = -1;
};
