#include "Thumbnailer.h"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>

Thumbnailer::Thumbnailer(QObject* parent) : QObject(parent)
{
    m_thread = std::thread([this] { run(); });
}

Thumbnailer::~Thumbnailer()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void Thumbnailer::request(const QString& uri, qint64 ns)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_reqUri = uri;
        m_reqNs = ns;
        m_has = true;
    }
    m_cv.notify_one();
}

void Thumbnailer::run()
{
    for (;;) {
        QString uri;
        qint64 ns = 0;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stop || m_has; });
            if (m_stop) break;
            uri = m_reqUri;
            ns = m_reqNs;
            m_has = false;
        }
        const QImage img = grab(uri, ns);
        if (!img.isNull()) emit ready(uri, ns, img);
    }
    teardown();
}

void Thumbnailer::teardown()
{
    if (m_pipe) {
        gst_element_set_state(m_pipe, GST_STATE_NULL);
        gst_object_unref(m_pipe);
    }
    m_pipe = m_sink = nullptr;
    m_uri.clear();
}

QImage Thumbnailer::grab(const QString& uri, qint64 ns)
{
    if (uri != m_uri || !m_pipe) {
        teardown();
        // Video only (no audio, no subtitles); rotation tags applied; scaled to square pixels.
        GstElement* pipe = gst_element_factory_make("playbin", "thumbnailer");
        GError* err = nullptr;
        GstElement* bin = gst_parse_bin_from_description(
            "videoflip method=automatic ! videoconvert ! videoscale ! "
            "video/x-raw,format=RGBA,width=240,pixel-aspect-ratio=1/1 ! appsink name=thumbsink sync=false max-buffers=1 drop=true",
            TRUE, &err);
        if (!pipe || !bin) {
            if (err) g_error_free(err);
            if (pipe) gst_object_unref(pipe);
            if (bin) gst_object_unref(bin);
            return {};
        }
        g_object_set(pipe, "uri", uri.toUtf8().constData(), "video-sink", bin, "flags", 0x1 /* video only */, nullptr);
        m_sink = gst_bin_get_by_name(GST_BIN(bin), "thumbsink");
        gst_object_unref(m_sink);   // owned by the bin
        m_pipe = pipe;
        m_uri = uri;
        gst_element_set_state(m_pipe, GST_STATE_PAUSED);
        if (gst_element_get_state(m_pipe, nullptr, nullptr, 5 * GST_SECOND) == GST_STATE_CHANGE_FAILURE) { teardown(); return {}; }
    }
    // Fast keyframe seek: a preview does not need the exact frame.
    gst_element_seek_simple(m_pipe, GST_FORMAT_TIME, GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_SNAP_NEAREST), ns);
    GstSample* sample = gst_app_sink_try_pull_preroll(GST_APP_SINK(m_sink), 3 * GST_SECOND);
    if (!sample) return {};
    QImage out;
    GstVideoInfo info;
    if (gst_video_info_from_caps(&info, gst_sample_get_caps(sample))) {
        GstVideoFrame frame;
        if (gst_video_frame_map(&frame, &info, gst_sample_get_buffer(sample), GST_MAP_READ)) {
            out = QImage(static_cast<const uchar*>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0)), GST_VIDEO_INFO_WIDTH(&info),
                         GST_VIDEO_INFO_HEIGHT(&info), GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0), QImage::Format_RGBA8888).copy();
            gst_video_frame_unmap(&frame);
        }
    }
    gst_sample_unref(sample);
    return out;
}
