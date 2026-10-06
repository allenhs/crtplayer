#include "ShrinkFilter.h"

#include "ShrinkKernel.h"

#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

#include <cstring>
#include <thread>
#include <vector>

namespace {

struct Format { const char* in; const char* out; int depth; };
// Planar YUV with 10 or 12 bits a sample (little-endian), as software decoders hand out HEVC, AV1 and VP9
// "10-bit" video. Not 8-bit pictures: GStreamer's scaler is as fast on those (measured: no gain), and they
// are scaled and converted exactly as before. (P010 comes from hardware decoders, which a machine without
// a graphics card does not have.)
const Format kFormats[] = {
    {"I420_10LE", "I420", 10}, {"I422_10LE", "Y42B", 10}, {"Y444_10LE", "Y444", 10},
    {"I420_12LE", "I420", 12}, {"I422_12LE", "Y42B", 12}, {"Y444_12LE", "Y444", 12},
};
const int kMaxFactor = 8;

const Format* findFormat(const char* name)
{
    if (!name) return nullptr;
    for (const Format& f : kFormats)
        if (!std::strcmp(f.in, name)) return &f;
    return nullptr;
}

int ceilDiv(int a, int k) { return (a + k - 1) / k; }

int factorFor(ShrinkMode mode, int tw, int th, int w, int h)
{
    if (mode != ShrinkMode::Fit || tw <= 0 || th <= 0) return 1;
    return std::clamp(std::min(w / tw, h / th), 1, kMaxFactor);
}

} // namespace

struct CrtShrink {
    GstVideoFilter parent;
    // (all under the object lock)
    ShrinkMode mode;
    int targetW, targetH;
    int factor;        // in use; 0: frames pass untouched
    int depth;
    int outW, outH;
    guint64 frames;
    double msSum;
};
struct CrtShrinkClass { GstVideoFilterClass parent_class; };

G_DEFINE_TYPE(CrtShrink, crt_shrink, GST_TYPE_VIDEO_FILTER)

// The shrunk form of one fixed description of the input, or null when such frames pass untouched.
static GstStructure* shrunkStructure(const GstStructure* s, ShrinkMode mode, int tw, int th)
{
    if (mode == ShrinkMode::Off) return nullptr;
    int w = 0, h = 0;
    const Format* f = findFormat(gst_structure_get_string(s, "format"));   // (null for a list of formats)
    if (!f || !gst_structure_get_int(s, "width", &w) || !gst_structure_get_int(s, "height", &h)) return nullptr;
    // Averaging lines would mix the two fields of an interlaced frame.
    const char* il = gst_structure_get_string(s, "interlace-mode");
    if (il && std::strcmp(il, "progressive") != 0) return nullptr;
    const int k = factorFor(mode, tw, th, w, h);
    GstStructure* out = gst_structure_copy(s);
    gst_structure_set(out, "format", G_TYPE_STRING, f->out, "width", G_TYPE_INT, ceilDiv(w, k), "height", G_TYPE_INT,
                      ceilDiv(h, k), nullptr);
    return out;
}

static GstCaps* crt_shrink_transform_caps(GstBaseTransform* trans, GstPadDirection direction, GstCaps* caps, GstCaps* filter)
{
    CrtShrink* self = reinterpret_cast<CrtShrink*>(trans);
    GST_OBJECT_LOCK(self);
    const ShrinkMode mode = self->mode;
    const int tw = self->targetW, th = self->targetH;
    GST_OBJECT_UNLOCK(self);
    GstCaps* out = gst_caps_new_empty();
    for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
        const GstStructure* s = gst_caps_get_structure(caps, i);
        GstCapsFeatures* feat = gst_caps_features_copy(gst_caps_get_features(caps, i));
        // What comes out for this input: the shrunk picture where one is made, the same frame otherwise.
        // (Asked the other way round, what may come in for this output, the answer is "the same": the
        // exact answer depends on the input's size, and is given when the input is known.)
        const bool ordinary = gst_caps_features_is_equal(feat, GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY);
        GstStructure* shrunk = direction == GST_PAD_SINK && ordinary ? shrunkStructure(s, mode, tw, th) : nullptr;
        gst_caps_append_structure_full(out, shrunk ? shrunk : gst_structure_copy(s), feat);
    }
    if (filter) {
        GstCaps* both = gst_caps_intersect_full(filter, out, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(out);
        out = both;
    }
    return out;
}

static gboolean crt_shrink_set_info(GstVideoFilter* filter, GstCaps* incaps, GstVideoInfo* in, GstCaps* outcaps, GstVideoInfo* out)
{
    CrtShrink* self = reinterpret_cast<CrtShrink*>(filter);
    int k = 0, depth = 8;
    if (!gst_caps_is_equal(incaps, outcaps)) {
        const Format* f = findFormat(gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(in)));
        if (!f || std::strcmp(f->out, gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(out))) != 0) return FALSE;
        for (int c = 1; c <= kMaxFactor && !k; ++c)
            if (ceilDiv(GST_VIDEO_INFO_WIDTH(in), c) == GST_VIDEO_INFO_WIDTH(out) && ceilDiv(GST_VIDEO_INFO_HEIGHT(in), c) == GST_VIDEO_INFO_HEIGHT(out))
                k = c;
        if (!k) return FALSE;
        depth = f->depth;
    }
    GST_OBJECT_LOCK(self);
    self->factor = k;
    self->depth = depth;
    self->outW = GST_VIDEO_INFO_WIDTH(out);
    self->outH = GST_VIDEO_INFO_HEIGHT(out);
    GST_OBJECT_UNLOCK(self);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(filter), k == 0);
    return TRUE;
}

static GstFlowReturn crt_shrink_transform_frame(GstVideoFilter* filter, GstVideoFrame* in, GstVideoFrame* out)
{
    CrtShrink* self = reinterpret_cast<CrtShrink*>(filter);
    GST_OBJECT_LOCK(self);
    const int k = self->factor, depth = self->depth;
    GST_OBJECT_UNLOCK(self);
    if (k <= 0) return GST_FLOW_NOT_NEGOTIATED;
    const gint64 t0 = g_get_monotonic_time();
    const int planes = int(GST_VIDEO_FRAME_N_PLANES(in));
    // In bands of lines, on as many cores as there are (a small picture is not worth the threads).
    int bands = std::clamp(int(g_get_num_processors()), 1, 8);
    if (gint64(GST_VIDEO_FRAME_WIDTH(in)) * GST_VIDEO_FRAME_HEIGHT(in) < 1000000) bands = 1;
    auto work = [&](int band) {
        for (int p = 0; p < planes; ++p) {
            const int sw = GST_VIDEO_FRAME_COMP_WIDTH(in, p), sh = GST_VIDEO_FRAME_COMP_HEIGHT(in, p);
            const int dw = GST_VIDEO_FRAME_COMP_WIDTH(out, p), dh = GST_VIDEO_FRAME_COMP_HEIGHT(out, p);
            const uint8_t* src = static_cast<const uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(in, p));
            uint8_t* dst = static_cast<uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(out, p));
            const int ss = GST_VIDEO_FRAME_PLANE_STRIDE(in, p), ds = GST_VIDEO_FRAME_PLANE_STRIDE(out, p);
            const int r0 = int(gint64(dh) * band / bands), r1 = int(gint64(dh) * (band + 1) / bands);
            if (depth > 8) Shrink::plane<uint16_t>(src, ss, sw, sh, depth, dst, ds, dw, dh, k, r0, r1);
            else Shrink::plane<uint8_t>(src, ss, sw, sh, depth, dst, ds, dw, dh, k, r0, r1);
        }
    };
    std::vector<std::thread> threads;
    threads.reserve(size_t(bands - 1));
    for (int b = 1; b < bands; ++b) threads.emplace_back(work, b);
    work(0);
    for (std::thread& t : threads) t.join();
    GST_OBJECT_LOCK(self);
    ++self->frames;
    self->msSum += double(g_get_monotonic_time() - t0) / 1000.0;
    GST_OBJECT_UNLOCK(self);
    return GST_FLOW_OK;
}

static void crt_shrink_class_init(CrtShrinkClass* klass)
{
    GstElementClass* element = GST_ELEMENT_CLASS(klass);
    gst_element_class_set_static_metadata(element, "CRT Player shrink", "Filter/Converter/Video/Scaler",
                                          "Shrinks a picture by a whole factor and to 8 bits a sample in one pass", "CRT Player");
    GstCaps* caps = gst_caps_from_string(GST_VIDEO_CAPS_MAKE(GST_VIDEO_FORMATS_ALL));
    gst_element_class_add_pad_template(element, gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_element_class_add_pad_template(element, gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);
    GST_BASE_TRANSFORM_CLASS(klass)->transform_caps = crt_shrink_transform_caps;
    GST_VIDEO_FILTER_CLASS(klass)->set_info = crt_shrink_set_info;
    GST_VIDEO_FILTER_CLASS(klass)->transform_frame = crt_shrink_transform_frame;
}

static void crt_shrink_init(CrtShrink* self)
{
    self->mode = ShrinkMode::Off;
    self->targetW = self->targetH = 0;
    self->factor = 0;
    self->depth = 8;
    self->outW = self->outH = 0;
    self->frames = 0;
    self->msSum = 0;
}

GstElement* crt_shrink_new()
{
    return GST_ELEMENT(g_object_new(crt_shrink_get_type(), nullptr));
}

void crt_shrink_set(GstElement* shrink, ShrinkMode mode, int width, int height)
{
    if (!shrink) return;
    CrtShrink* self = reinterpret_cast<CrtShrink*>(shrink);
    GST_OBJECT_LOCK(self);
    const bool changed = self->mode != mode || self->targetW != width || self->targetH != height;
    self->mode = mode;
    self->targetW = width;
    self->targetH = height;
    GST_OBJECT_UNLOCK(self);
    if (changed) gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(shrink));   // (decided anew with the next frame)
}

ShrinkState crt_shrink_state(GstElement* shrink)
{
    ShrinkState st{0, 0, 0, 0, 0.0};
    if (!shrink) return st;
    CrtShrink* self = reinterpret_cast<CrtShrink*>(shrink);
    GST_OBJECT_LOCK(self);
    st.frames = self->frames;
    st.factor = self->factor;
    st.outWidth = self->outW;
    st.outHeight = self->outH;
    st.msPerFrame = self->frames ? self->msSum / double(self->frames) : 0.0;
    GST_OBJECT_UNLOCK(self);
    return st;
}
