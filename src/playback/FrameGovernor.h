#pragma once
#include <gst/gst.h>

#include <QJsonObject>

#include <atomic>
#include <cstdint>
#include <mutex>

// When a computer cannot decode every picture of a video in time (4K without a graphics card), some
// have to be left out. GStreamer does that after the fact: a picture that arrives late is thrown away,
// decoded or not, and the work spent on it is lost (measured, 4K HEVC 10-bit on 2 cores: every second
// decoded picture). The governor leaves pictures out *before* they are decoded, and only those that no
// other picture is built from, so nothing else is damaged: just as many as it takes for the rest to
// arrive in time. With a computer that keeps up it does nothing at all.
//
// H.264 and H.265 only (their streams say which pictures nothing is built from), while playing only
// (a paused or stepped picture is always the exact one).
class FrameGovernor {
public:
    // Which kind of stream a decoder is fed.
    enum class Codec { None, H264, H265 };

    // The decoder of the video now opening: its input is watched from here on.
    void attach(GstElement* decoder);
    // A new video: back to "nothing left out".
    void reset();
    void setPlaying(bool playing) { m_playing.store(playing); }
    // (CRTPLAYER_GOVERNOR_OFF, and the tests' comparison.)
    void setEnabled(bool on) { m_enabled.store(on); }
    // From the video sink, once for every picture it shows: how early the picture was there, in
    // nanoseconds (negative: late), and how long a picture lasts.
    void arrived(gint64 earlyNs, gint64 pictureNs);
    QJsonObject report() const;

    // Is this picture (one access unit as the parser hands it on) one that no other picture is built
    // from? `lengthSize`: bytes of the length before each unit (0: start codes). For H.265 also its
    // temporal layer (-1 if not found). `whole`: the picture's full size, when only its beginning is at
    // hand (0: `size` is all of it). Exposed for the tests.
    static bool unreferenced(Codec codec, const uint8_t* data, size_t size, int lengthSize, int* temporalId = nullptr, size_t whole = 0);

private:
    static GstPadProbeReturn onBuffer(GstPad*, GstPadProbeInfo* info, gpointer self);
    static GstPadProbeReturn onEvent(GstPad*, GstPadProbeInfo* info, gpointer self);
    bool leaveOut(GstBuffer* buf);

    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_enabled{true};
    mutable std::mutex m_lock;
    Codec m_codec = Codec::None;
    int m_lengthSize = 4;
    int m_topLayer = 0;          // H.265: the highest temporal layer seen
    double m_share = 0;          // of the unreferenced pictures, the share left out (0..1)
    double m_credit = 0;
    int m_settle = 0;            // pictures after a jump whose timing says nothing
    double m_inHand = 0.4;       // how early pictures arrive, as a share of a picture's time, averaged over the last ten or so
    quint64 m_pictures = 0, m_unreferenced = 0, m_leftOut = 0;
    double m_shareMax = 0;
};
