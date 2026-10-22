#pragma once
// RTSP video through GStreamer, independent of the MAVLink link:
//   rtspsrc ! decodebin ! videoconvert ! video/x-raw,format=RGBx ! appsink
// Frames are copied on GStreamer's streaming thread; the GUI only takes the latest image.

#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <mutex>

typedef struct _GstElement GstElement;
typedef struct _GstAppSink GstAppSink;

class VideoPlayer : public QObject
{
    Q_OBJECT
public:
    enum class State { Stopped, Connecting, Playing, Error };
    enum class Transport { Tcp, Udp, Auto };

    explicit VideoPlayer(QObject *parent = nullptr);
    ~VideoPlayer() override;  // stops the pipeline

    bool start(const QString &url, Transport transport, int latencyMs);  // false: reason in error()
    void stop();

    State state() const { return m_state; }
    QString error() const { return m_error; }
    QImage latestFrame(quint64 *frameNumber = nullptr) const;
    quint64 frames() const { return m_frames; }
    double fps() const { return m_fps; }

    static QString defaultUrl(const QString &ip) { return QStringLiteral("rtsp://%1:8554/payload").arg(ip); }  // Gremsy ui_demo format
    static QString validateUrl(const QString &url);  // empty when usable

signals:
    void stateChanged();
    void frameReady();  // GUI thread, at most once per poll

private:
    static int onSample(GstAppSink *sink, void *self);  // GstFlowReturn, streaming thread
    void pollBus();
    void setState(State s, const QString &error = {});

    GstElement *m_pipeline = nullptr;
    QTimer m_poll;
    State m_state = State::Stopped;
    QString m_error;

    mutable std::mutex m_mutex;  // m_image / m_imageNumber shared with the streaming thread
    QImage m_image;
    quint64 m_imageNumber = 0;
    std::atomic<quint64> m_frames{0};
    quint64 m_shownNumber = 0;
    quint64 m_fpsFrames = 0;
    qint64 m_fpsStartMs = 0;
    double m_fps = 0;
};
