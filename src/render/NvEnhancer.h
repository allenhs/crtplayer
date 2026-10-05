#pragma once
// NVIDIA's AI video effects for Enhance: Video Super Resolution and Video Frame Generation,
// from the NVIDIA Video Effects SDK, which the user installs separately (the README says how).
//
// The player does not load the SDK. It starts a small helper program, crtplayer-nvfx, that
// does, and talks to it over a socket; the pictures lie in memory the two share (the
// protocol is in tools/nvfx/NvShm.h). Whatever goes wrong there (no SDK, a driver the SDK
// does not like, the helper dying) shows here as "not ready", and the built-in methods
// are used instead.
//
// Used from the thread that draws. Nothing here waits for long: starting the helper and
// opening the effects (a few hundred milliseconds) happen in the background, and ready()
// says when they are done.
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <deque>

class NvEnhancer {
public:
    // What was found installed (no SDK is loaded for this).
    struct Install {
        QString helper;          // crtplayer-nvfx; empty: not found
        QString sdk;             // the SDK's folder; empty: not found
        QStringList features;
        bool superRes() const { return features.contains(QStringLiteral("nvvfxvideosuperres")); }
        bool frameGen() const { return features.contains(QStringLiteral("nvvfxvideoframegeneration")); }
        bool usable() const { return !helper.isEmpty() && !sdk.isEmpty() && (superRes() || frameGen()); }
    };
    static QString findHelper();
    static Install findInstall();   // (runs the helper for a moment)

    struct Config {
        QSize src, out;      // the video's frames, and the pictures wanted
        int quality = 0;     // super resolution: 0 none, 1 low .. 4 ultra
        int mode = -1;       // frame generation: -1 none, 0 low, 1 medium, 2 high
        bool valid() const { return !src.isEmpty() && !out.isEmpty() && (quality > 0 || mode >= 0); }
        bool operator==(const Config& o) const { return src == o.src && out == o.out && quality == o.quality && mode == o.mode; }
        bool operator!=(const Config& o) const { return !(*this == o); }
    };

    NvEnhancer();
    ~NvEnhancer();
    NvEnhancer(const NvEnhancer&) = delete;
    NvEnhancer& operator=(const NvEnhancer&) = delete;

    void setInstall(const Install& in);
    const Install& install() const { return m_install; }
    // True when the effects for `want` are open. Otherwise this works towards it (without
    // waiting) and the caller uses something else for now.
    bool ready(const Config& want);
    bool busy() const { return m_state == Starting || m_state == Opening; }   // ask again soon
    bool poll();           // reads what the helper has answered; true when ready/failed has changed
    void forgive();        // earlier failures are forgotten (a new video, a changed setting)
    void shutdown();       // the helper is told to leave

    const Config& config() const { return m_cfg; }
    int generation() const { return m_generation; }   // changes whenever the effects are opened anew

    // The video's next frame: write it at beginFrame() (src.width x src.height RGBA, rows packed,
    // the top row first), then endFrame(). `epoch` names the frame; `cut`: it does not follow the one before.
    uchar* beginFrame();
    bool endFrame(quint64 epoch, bool cut);
    quint64 lastEpoch() const { return m_lastEpoch; }   // the frame sent last, and the one before it
    quint64 prevEpoch() const { return m_prevEpoch; }

    // Asks for the picture at t (0 the frame before .. 1 the newest); the ticket is redeemed
    // with wait(), at once or at a later draw. 0: could not be asked.
    int request(double t);
    // The picture (out.width x out.height RGBA), or null. Valid until the next request().
    const uchar* wait(int ticket, char* kind = nullptr);
    void abandon(int ticket);

    QString stateName() const;
    QString error() const { return m_error; }           // why it is not ready now
    QString lastError() const { return m_lastError; }   // the last thing that went wrong, whatever has happened since
    QJsonObject report() const;

private:
    enum State { Off, Starting, Idle, Opening, Ready, Dead, Fatal };
    struct Pending { int id; int what; };              // what: 0 hello, 1 open, 2 frame, 3 get, 4 close
    struct Result { bool ok = false; char kind = '?'; int slot = -1; bool delivered = false; };
    bool start();
    void stop(bool kill, const QString& why);
    bool sendLine(int what, const QByteArray& text, int* idOut = nullptr);
    bool readAvailable(int timeoutMs);                 // false: the helper is gone
    void handleLine(const QByteArray& line);
    bool waitFor(int id, int timeoutMs);
    bool openConfig(const Config& c);
    void releaseShm();
    void pipelineFailed(const QString& why);

    Install m_install;
    State m_state = Off;
    QString m_error, m_lastError;
    qint64 m_pid = -1;
    int m_fd = -1;
    QByteArray m_inbuf;
    int m_nextId = 1;
    std::deque<Pending> m_pending;
    QHash<int, Result> m_results;
    int m_slotTicket[2] = {0, 0};                      // the request whose picture each output slot holds (or will)
    bool m_slotTaken[2] = {false, false};              // ... and whether that picture has been handed out
    int m_frameAck = 0;                                // the id of a frame not yet acknowledged
    Config m_cfg, m_opening, m_want, m_failedCfg;
    QElapsedTimer m_wantSince, m_failedSince, m_deadSince, m_openTimer;
    int m_failCount = 0, m_restarts = 0, m_generation = 0;
    uchar* m_shm = nullptr;
    size_t m_shmSize = 0, m_inOff = 0, m_slotOff[2] = {0, 0};
    QByteArray m_shmName;
    quint64 m_lastEpoch = 0, m_prevEpoch = 0;
    bool m_card = true;
    QString m_sdkVersion;
    // what happened, for the report
    quint64 m_frames = 0, m_pictures = 0, m_generated = 0, m_opens = 0, m_failures = 0;
    double m_frameMs = 0, m_getMs = 0, m_waitMs = 0, m_loadMs = 0;
};
