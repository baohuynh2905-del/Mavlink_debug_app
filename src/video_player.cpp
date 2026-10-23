#include "video_player.h"

#include <QDateTime>
#include <QUrl>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>

static void ensureGstInit()
{
    static bool done = false;
    if (!done) {
        gst_init(nullptr, nullptr);
        done = true;
    }
}

VideoPlayer::VideoPlayer(QObject *parent) : QObject(parent)
{
    ensureGstInit();
    m_poll.setInterval(33);
    connect(&m_poll, &QTimer::timeout, this, &VideoPlayer::pollBus);
}

VideoPlayer::~VideoPlayer()
{
    blockSignals(true);  // the owners are being destroyed: no stateChanged() to them from here
    stop();
}

QString VideoPlayer::validateUrl(const QString &url)
{
    const QUrl u(url.trimmed(), QUrl::StrictMode);
    if (!u.isValid() || (u.scheme() != QLatin1String("rtsp") && u.scheme() != QLatin1String("rtsps")) || u.host().isEmpty())
        return QStringLiteral("\"%1\" is not an rtsp:// URL with a host").arg(url);
    return {};
}

bool VideoPlayer::start(const QString &url, Transport transport, int latencyMs)
{
    stop();
    if (const QString err = validateUrl(url); !err.isEmpty()) {
        setState(State::Error, err);
        return false;
    }
    GError *perr = nullptr;
    // Fixed description; the user's URL is set as a property below, never parsed as pipeline syntax.
    m_pipeline = gst_parse_launch("rtspsrc name=src ! decodebin ! videoconvert ! video/x-raw,format=RGBx "
                                  "! appsink name=sink max-buffers=2 drop=true sync=false",
                                  &perr);
    if (!m_pipeline) {
        setState(State::Error, QStringLiteral("GStreamer: %1").arg(QString::fromUtf8(perr ? perr->message : "pipeline error")));
        if (perr) g_error_free(perr);
        return false;
    }
    if (perr) g_error_free(perr);  // non-fatal warning (e.g. a missing optional element)

    GstElement *src = gst_bin_get_by_name(GST_BIN(m_pipeline), "src");
    const QByteArray u = url.trimmed().toUtf8();
    g_object_set(src, "location", u.constData(), "latency", guint(latencyMs), nullptr);
    gst_util_set_object_arg(G_OBJECT(src), "protocols",
                            transport == Transport::Tcp ? "tcp" : transport == Transport::Udp ? "udp" : "udp+tcp");
    gst_object_unref(src);

    GstElement *sink = gst_bin_get_by_name(GST_BIN(m_pipeline), "sink");
    GstAppSinkCallbacks cb{};
    cb.new_sample = [](GstAppSink *s, gpointer self) { return GstFlowReturn(onSample(s, self)); };
    gst_app_sink_set_callbacks(GST_APP_SINK(sink), &cb, this, nullptr);
    gst_object_unref(sink);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_image = QImage();
        m_imageNumber = 0;
    }
    m_frames = 0;
    m_shownNumber = 0;
    m_fps = 0;
    m_fpsFrames = 0;
    m_fpsStartMs = QDateTime::currentMSecsSinceEpoch();

    setState(State::Connecting);
    if (gst_element_set_state(m_pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        stop();
        setState(State::Error, QStringLiteral("GStreamer could not start the pipeline"));
        return false;
    }
    m_poll.start();
    return true;
}

void VideoPlayer::stop()
{
    m_poll.stop();
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);  // joins the streaming threads: no callback after this
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        if (m_state != State::Error) setState(State::Stopped);
    }
}

int VideoPlayer::onSample(GstAppSink *sink, void *selfPtr)
{
    auto *self = static_cast<VideoPlayer *>(selfPtr);
    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_OK;
    GstVideoInfo info;
    GstBuffer *buf = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (gst_video_info_from_caps(&info, gst_sample_get_caps(sample)) && gst_buffer_map(buf, &map, GST_MAP_READ)) {
        const QImage view(map.data, GST_VIDEO_INFO_WIDTH(&info), GST_VIDEO_INFO_HEIGHT(&info),
                          GST_VIDEO_INFO_PLANE_STRIDE(&info, 0), QImage::Format_RGBX8888);
        QImage copy = view.copy();  // the buffer is unmapped below
        gst_buffer_unmap(buf, &map);
        std::lock_guard<std::mutex> lock(self->m_mutex);
        self->m_image = std::move(copy);
        ++self->m_imageNumber;
        ++self->m_frames;
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

QImage VideoPlayer::latestFrame(quint64 *frameNumber) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (frameNumber) *frameNumber = m_imageNumber;
    return m_image;
}

void VideoPlayer::pollBus()
{
    if (!m_pipeline) return;
    GstBus *bus = gst_element_get_bus(m_pipeline);
    while (GstMessage *msg = gst_bus_pop_filtered(bus, GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
        QString err;
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
            GError *e = nullptr;
            gchar *dbg = nullptr;
            gst_message_parse_error(msg, &e, &dbg);
            err = QStringLiteral("%1 (%2)").arg(QString::fromUtf8(e ? e->message : "error"), QString::fromUtf8(GST_OBJECT_NAME(msg->src)));
            if (e) g_error_free(e);
            g_free(dbg);
        } else {
            err = QStringLiteral("Stream ended (EOS)");
        }
        gst_message_unref(msg);
        gst_object_unref(bus);
        stop();
        setState(State::Error, err);
        return;
    }
    gst_object_unref(bus);

    quint64 n = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        n = m_imageNumber;
    }
    if (n != m_shownNumber) {
        if (m_state != State::Playing) setState(State::Playing);
        m_shownNumber = n;
        emit frameReady();
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_fpsStartMs >= 1000) {
        const quint64 f = m_frames;
        m_fps = double(f - m_fpsFrames) * 1000.0 / double(now - m_fpsStartMs);
        m_fpsFrames = f;
        m_fpsStartMs = now;
    }
}

void VideoPlayer::setState(State s, const QString &error)
{
    m_state = s;
    m_error = error;
    emit stateChanged();
}
