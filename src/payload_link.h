#pragma once
// Single owner of the PayloadSDK connection, and the bridge from the SDK's I/O threads
// (transport observer) to the GUI thread.

#include "captured_frame.h"
#include "command_tracker.h"
#include "link_health.h"
#include "telemetry.h"

#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QRectF>
#include <QTimer>

#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

class MavlinkDict;
class PayloadSdkInterface;
class QThread;
struct sockaddr_in;

// The operator screen's live values. Each is set only when it was received within the timeout.
struct OperatorStatus {
    std::optional<GimbalAttitude> attitude;     // GIMBAL_DEVICE_ATTITUDE_STATUS
    std::optional<double> eoZoom;               // EO_ZOOM parameter (x)
    std::optional<bool> recording;              // CAMERA_CAPTURE_STATUS.video_status
    double recordingS = 0;
    std::optional<int> trackMode, trackStatus;  // TRK_STATUS = (mode << 8) | status (SDK tracking example)
    std::optional<QRectF> trackBox;             // TRK_POS_X/Y (top-left corner) and _W/_H, in tracker pixels
};

QString trackingStateText(int mode, int status);  // e.g. "ACTIVE / TRACKED"

class PayloadLink : public QObject
{
    Q_OBJECT
public:
    explicit PayloadLink(QObject *parent = nullptr);
    ~PayloadLink() override;  // blocks until any connect/disconnect finishes and SDK threads are joined

    // Asynchronous; progress is reported through stateChanged().
    void connectTo(const QString &ip, int port);
    void disconnectLink();

    LinkState state() const { return m_state; }
    QString lastError() const { return m_error; }
    QString target() const { return m_target; }
    qint64 openSinceMs() const { return m_openSinceMs; }  // monotonic ms, -1 when not open
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    void setDictionary(const MavlinkDict *d) { m_dict = d; }  // for the decoded summaries
    int timeoutMs() const { return m_timeoutMs; }
    quint64 droppedDatagrams() const { return m_dropped; }  // capture queue overflow, never silent

    // Last HEARTBEAT time (monotonic ms) per (sysid << 8 | compid) since connecting.
    const QMap<quint16, qint64> &heartbeatSources() const { return m_heartbeats; }

    static qint64 monoNs();

    static constexpr size_t kMaxPendingDatagrams = 20000;

    // --- Payload commands -------------------------------------------------------------------
    // Each goes through the SDK API and is registered with tracker() under the given UI label.
    // They return false (reason in commandError()) when the link is not open or input is invalid.
    bool gimbalMode(GimbalFlags::Change change, const QString &label);  // needs observed gimbal flags
    bool gimbalHome();                                                // NEUTRAL set, cleared 100 ms later
    // Steers pitch and yaw to the angles with rate commands (no angle command works on VIO 3.1.8.21), at most
    // maxDps, from the measured attitude; angleFinished() reports the end. Any other gimbal command cancels it.
    bool gimbalAngle(double pitchDeg, double yawDeg, double maxDps = 30);
    bool gimbalRate(double pitchDps, double rollDps, double yawDps, bool track = true);
    // The steering law, public for tests: rates (deg/s) for the remaining errors (deg); 0 within kAngleTolerance.
    static void steerRates(double errPitch, double errYaw, double maxDps, double *pitchDps, double *yawDps);
    static double angleDiff(double target, double measured);  // shortest way round, -180..180
    // Gain 1/s: the VIO's attitude stream pauses up to 0.8 s, and blind for that long the gimbal covers at most 80% of
    // the error, so it cannot overshoot.
    static constexpr double kAngleTolerance = 0.5, kSteerGain = 1.0;
    bool cameraParam(const QString &label, const char *paramId, quint32 value);  // then reads the value back
    bool requestCameraSettings();  // PARAM_EXT_REQUEST_LIST: every setting arrives as PARAM_EXT_VALUE
    void readMissingCameraSetting(const char *paramId);  // call freely: re-asks at most every 2 s, untracked
    bool cameraZoom(int zoomType, float value, const QString &label);   // MAV_CMD_SET_CAMERA_ZOOM
    bool cameraFocus(int focusType, float value, const QString &label); // MAV_CMD_SET_CAMERA_FOCUS
    bool cameraMode(int cameraMode);                                    // CAMERA_MODE_IMAGE / _VIDEO
    bool captureImage();
    bool recordVideo(bool start);
    bool requestPayloadInfo(bool automatic = false);  // info messages + the SDK DEBUG parameter streams

    // Object tracking: MAV_CMD_USER_4 with param1 = 4, as the SDK's payload_do_object_tracking example sends it.
    enum TrackMode { TrackStop = 0, TrackActive = 1 };
    enum TrackStatus { TrackIdle = 0, TrackTracked = 1, TrackLost = 2 };
    // shortcut: tracker pixel space taken from the SDK defaults (centre 960,540) and example (x <= 1920, y <= 1080),
    // not from a spec; check on hardware and move to the payload profile if a model differs.
    static constexpr double kTrackerWidth = 1920, kTrackerHeight = 1080;
    bool trackingMode(int mode);
    // Centre and size in tracker pixels. Also sends gimbal rate 0 and mode ACTIVE first (see the definition).
    bool trackingTarget(double x, double y, double w = 128, double h = 128);
    bool trackingStreams();  // TRK_* DEBUG parameters every 100 ms, the example's rate
    QString commandError() const { return m_commandError; }

    const CommandTracker &tracker() const { return m_tracker; }
    const TelemetryStore &telemetry() const { return m_telemetry; }
    OperatorStatus operatorStatus() const { return operatorStatus(m_telemetry, monoNs(), m_timeoutMs); }
    static OperatorStatus operatorStatus(const TelemetryStore &t, qint64 nowNs, int timeoutMs);

    // Names of the SDK's DEBUG parameter stream indexes (payloadParams in payloadSdkInterface.h).
    static QString payloadParamName(int index);
    static QString sdkVersion();  // PayloadSDK's SDK_VERSION

    // Gimbal angle limits (the ones PayloadSdkInterface::setGimbalSpeed() enforces in INPUT_ANGLE mode)
    static constexpr double kPitchLimit = 90, kYawLimit = 180;

signals:
    void stateChanged();
    void framesCaptured(const std::vector<CapturedFrame> &frames);  // GUI thread, in observation order
    void commandsChanged();
    void angleFinished(bool reached, const QString &detail);  // gimbalAngle() ended; detail also in commandError()

private:
    void onTransport(int dir, const uint8_t *data, int len, const sockaddr_in *peer, int sysErrno);
    void poll();
    void setState(LinkState s, const QString &error = {});
    bool ready();
    bool setMotionFlags();
    void steerStep();                              // 100 ms, from poll()
    void endSteer(bool reached, const QString &detail);  // flags for the next rate/angle command; false (commandError) until the gimbal reported
    template <class Work, class Done> void runInWorker(Work work, Done done);

    struct Datagram {
        quint64 seq;
        qint64 monoNs, wallUs;
        int dir;
        quint32 peerIp;
        quint16 peerPort;
        int sysErrno;
        QByteArray bytes;
    };

    // Shared with the SDK I/O threads
    std::mutex m_mutex;
    std::vector<Datagram> m_pending;
    quint64 m_nextSeq = 0;
    std::atomic<quint64> m_dropped{0};

    // GUI thread only
    const MavlinkDict *m_dict = nullptr;
    PayloadSdkInterface *m_sdk = nullptr;
    QThread *m_worker = nullptr;
    QByteArray m_ip;  // the SDK keeps a raw char* to it for the whole connection
    QString m_target;
    LinkState m_state = LinkState::Disconnected;
    QString m_error;
    LinkObservations m_obs;
    QMap<quint16, qint64> m_heartbeats;
    qint64 m_openSinceMs = -1;
    int m_timeoutMs = 3000;
    QTimer m_pollTimer;
    CommandTracker m_tracker;
    TelemetryStore m_telemetry;
    QString m_commandError;
    bool m_autoInfoSent = false;
    QHash<QString, qint64> m_settingReads;  // param_id -> last PARAM_EXT_REQUEST_READ (ms)
    qint64 m_listMs = -1;                    // last PARAM_EXT_REQUEST_LIST (ms)
    qint64 m_cameraSeenMs = -1;              // first frame from the camera this connection
    struct Steer {
        double pitch, yaw, maxDps;
        qint64 deadlineMs;
    };
    std::optional<Steer> m_steer;
    quint16 m_modeFlags = 0;  // last mode commanded by gimbalMode()
    qint64 m_modeFlagsMs = -1;
};
