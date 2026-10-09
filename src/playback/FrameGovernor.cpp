#include "FrameGovernor.h"

#include <algorithm>
#include <cstring>

namespace {
const size_t kLook = 4096;   // bytes of a picture looked at: its first slice comes after a few short units
}

bool FrameGovernor::unreferenced(Codec codec, const uint8_t* d, size_t n, int lengthSize, int* temporalId, size_t whole)
{
    if (temporalId) *temporalId = -1;
    if (codec == Codec::None || !d) return false;
    if (whole < n) whole = n;
    size_t pos = 0;
    for (int guard = 0; guard < 64; ++guard) {
        const uint8_t* p = nullptr;
        if (lengthSize > 0) {
            if (pos + size_t(lengthSize) >= n) return false;
            size_t len = 0;
            for (int i = 0; i < lengthSize; ++i) len = (len << 8) | d[pos + size_t(i)];
            if (len == 0 || pos + size_t(lengthSize) + len > whole) return false;   // (not what it was taken for)
            p = d + pos + size_t(lengthSize);
            pos += size_t(lengthSize) + len;   // (the next unit; only this one's first bytes are read)
        } else {
            size_t i = pos;
            for (; i + 3 < n; ++i)
                if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) break;
            if (i + 3 >= n) return false;
            p = d + i + 3;
            pos = i + 3;
        }
        if (p + 2 > d + n) return false;
        if (codec == Codec::H264) {
            const int type = p[0] & 0x1f, ref = (p[0] >> 5) & 3;
            // The first slice decides (1: a slice of an ordinary picture, 5: of a picture everything starts from).
            if (type >= 1 && type <= 5) return type != 5 && ref == 0;
        } else {
            const int type = (p[0] >> 1) & 0x3f;
            if (type <= 31) {   // a slice
                if (temporalId) *temporalId = (p[1] & 7) - 1;
                // The even types below 16 are the "sub-layer non-reference" pictures (TRAIL_N, TSA_N, STSA_N,
                // RADL_N, RASL_N): nothing in their own temporal layer is built from them.
                return type <= 14 && (type % 2) == 0;
            }
        }
        if (pos >= n) return false;
    }
    return false;
}

void FrameGovernor::attach(GstElement* decoder)
{
    GstPad* pad = decoder ? gst_element_get_static_pad(decoder, "sink") : nullptr;
    if (!pad) return;
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, &FrameGovernor::onBuffer, this, nullptr);
    gst_pad_add_probe(pad, GstPadProbeType(GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_EVENT_FLUSH), &FrameGovernor::onEvent,
                      this, nullptr);
    gst_object_unref(pad);
}

void FrameGovernor::reset()
{
    std::lock_guard<std::mutex> lock(m_lock);
    m_codec = Codec::None;
    m_lengthSize = 4;
    m_topLayer = 0;
    m_share = m_credit = m_shareMax = 0;
    m_settle = 15;
    m_inHand = 0.4;
    m_pictures = m_unreferenced = m_leftOut = 0;
    m_winFed = m_winLeft = m_winArrived = 0;
    m_lostToDecoder = 0;
}

GstPadProbeReturn FrameGovernor::onEvent(GstPad*, GstPadProbeInfo* info, gpointer self)
{
    auto* g = static_cast<FrameGovernor*>(self);
    GstEvent* ev = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(ev) == GST_EVENT_CAPS) {
        GstCaps* caps = nullptr;
        gst_event_parse_caps(ev, &caps);
        const GstStructure* s = caps ? gst_caps_get_structure(caps, 0) : nullptr;
        Codec codec = Codec::None;
        int lengthSize = 0;
        if (s) {
            const char* name = gst_structure_get_name(s);
            if (!std::strcmp(name, "video/x-h264")) codec = Codec::H264;
            else if (!std::strcmp(name, "video/x-h265")) codec = Codec::H265;
            const char* sf = gst_structure_get_string(s, "stream-format");
            const GValue* cd = gst_structure_get_value(s, "codec_data");
            GstBuffer* data = cd && GST_VALUE_HOLDS_BUFFER(cd) ? gst_value_get_buffer(cd) : nullptr;
            // Units with their length before them (avc, hvc1, ...) unless the stream says start codes.
            const bool startCodes = sf ? !std::strcmp(sf, "byte-stream") : data == nullptr;
            if (codec != Codec::None && !startCodes) {
                lengthSize = 4;
                const gsize at = codec == Codec::H264 ? 4 : 21;   // where the configuration record keeps the length's size
                guint8 b = 0;
                if (data && gst_buffer_get_size(data) > at && gst_buffer_extract(data, at, &b, 1) == 1) lengthSize = (b & 3) + 1;
            }
        }
        std::lock_guard<std::mutex> lock(g->m_lock);
        g->m_codec = codec;
        g->m_lengthSize = lengthSize;
    } else if (GST_EVENT_TYPE(ev) == GST_EVENT_FLUSH_STOP) {
        std::lock_guard<std::mutex> lock(g->m_lock);
        g->m_credit = 0;
        g->m_settle = 12;
        g->m_winFed = g->m_winLeft = g->m_winArrived = 0;
        g->m_inHand = 0.4;   // (the first pictures after a jump are shown as they come)
    }
    return GST_PAD_PROBE_OK;
}

bool FrameGovernor::leaveOut(GstBuffer* buf)
{
    Codec codec;
    int lengthSize;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        codec = m_codec;
        lengthSize = m_lengthSize;
    }
    if (codec == Codec::None) return false;
    int layer = -1;
    bool unref = GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DROPPABLE);
    if (!unref || codec == Codec::H265) {
        uint8_t head[kLook];
        const gsize n = gst_buffer_extract(buf, 0, head, sizeof head);
        const bool u = unreferenced(codec, head, n, lengthSize, &layer, gst_buffer_get_size(buf));
        unref = unref || u;
    }
    std::lock_guard<std::mutex> lock(m_lock);
    ++m_pictures;
    if (m_settle == 0 && m_playing.load()) ++m_winFed;
    if (layer > m_topLayer) m_topLayer = layer;
    // H.265 with temporal layers: pictures of a higher layer may be built from this one.
    if (unref && codec == Codec::H265 && layer >= 0 && layer < m_topLayer) unref = false;
    if (!unref) return false;
    ++m_unreferenced;
    if (!m_enabled.load() || !m_playing.load() || m_share <= 0) return false;
    m_credit += m_share;
    if (m_credit < 1.0) return false;
    m_credit -= 1.0;
    ++m_leftOut;
    if (m_settle == 0) ++m_winLeft;
    return true;
}

GstPadProbeReturn FrameGovernor::onBuffer(GstPad*, GstPadProbeInfo* info, gpointer self)
{
    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    return buf && static_cast<FrameGovernor*>(self)->leaveOut(buf) ? GST_PAD_PROBE_DROP : GST_PAD_PROBE_OK;
}

// How early pictures arrive, as a share of a picture's time, averaged over the last ten or so: on a computer
// that keeps up it is 0.4 to 0.7 (measured), and single pictures are late now and then whatever is done
// (a key picture takes long to decode), so it is the average that counts. Below 0.2, more is left out;
// above 0.3, less again. (Acting on each late picture by itself was tried: it left pictures out of a video
// the computer could play in full.)
void FrameGovernor::arrived(gint64 earlyNs, gint64 pictureNs)
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_settle > 0) { --m_settle; return; }
    ++m_winArrived;
    if (pictureNs <= 0) pictureNs = 33333333;
    const double inHand = double(earlyNs) / double(pictureNs);
    m_inHand = m_inHand * 0.9 + std::clamp(inHand, -1.0, 1.0) * 0.1;
    if (m_inHand < 0.20) m_share = std::min(1.0, m_share + std::min(0.08, 0.015 + (0.20 - m_inHand) * 0.08));
    else if (m_inHand > 0.30) m_share = std::max(0.0, m_share - 0.006);
    m_shareMax = std::max(m_shareMax, m_share);
}

QJsonObject FrameGovernor::report() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return QJsonObject{{"codec", m_codec == Codec::H264 ? "h264" : m_codec == Codec::H265 ? "h265" : "none"},
                       {"enabled", m_enabled.load()}, {"pictures", double(m_pictures)}, {"unreferenced", double(m_unreferenced)},
                       {"leftOut", double(m_leftOut)}, {"share", m_share}, {"shareMax", m_shareMax}, {"topLayer", m_topLayer},
                       {"inHand", m_inHand}, {"lostToDecoder", double(m_lostToDecoder)}};
}
