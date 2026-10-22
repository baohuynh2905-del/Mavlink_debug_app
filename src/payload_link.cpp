#include "payload_link.h"

#include "frame_text.h"

// The only translation unit that includes the SDK header (it defines globals and many macros).
#include "payloadSdkInterface.h"

#include <QThread>

#include <cmath>

#include <chrono>
#include <memory>

PayloadLink::PayloadLink(QObject *parent) : QObject(parent)
{
    m_pollTimer.setInterval(100);
    connect(&m_pollTimer, &QTimer::timeout, this, &PayloadLink::poll);
    m_pollTimer.start();
}

PayloadLink::~PayloadLink()
{
    if (m_worker) {
        m_worker->wait();
        delete m_worker;  // its queued finished() handler is dropped along with this object
    }
    delete m_sdk;  // patched destructor: sdkQuit() joins all SDK threads, so no callback outlives us
}

qint64 PayloadLink::monoNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

template <class Work, class Done> void PayloadLink::runInWorker(Work work, Done done)
{
    auto ok = std::make_shared<bool>(false);
    m_worker = QThread::create([work, ok] { *ok = work(); });
    connect(m_worker, &QThread::finished, this, [this, done, ok] {
        m_worker->deleteLater();
        m_worker = nullptr;
        done(*ok);
    });
    m_worker->start();
}

void PayloadLink::connectTo(const QString &ip, int port)
{
    if (m_state != LinkState::Disconnected && m_state != LinkState::Failed) return;
    if (const QString err = validateEndpoint(ip, port); !err.isEmpty()) {
        setState(LinkState::Failed, err);
        return;
    }

    m_ip = ip.trimmed().toLatin1();
    m_target = QStringLiteral("%1:%2").arg(QString::fromLatin1(m_ip)).arg(port);
    m_obs = {};
    m_heartbeats.clear();
    m_openSinceMs = -1;
    m_telemetry.clear();
    m_autoInfoSent = false;

    T_ConnInfo ci{};
    ci.type = CONTROL_UDP;
    ci.device.udp.ip = m_ip.data();
    ci.device.udp.port = port;
    m_sdk = new PayloadSdkInterface(ci);
    m_sdk->regTransportObserver([this](int dir, const uint8_t *d, int len, const sockaddr_in *peer, int err) {
        onTransport(dir, d, len, peer, err);
    });

    setState(LinkState::Connecting);
    PayloadSdkInterface *sdk = m_sdk;
    runInWorker([sdk] { return sdk->sdkInitConnection(); },  // blocks ~1 s inside the SDK
                [this](bool ok) {
                    if (ok) {
                        m_openSinceMs = monoNs() / 1000000;
                        setState(LinkState::SocketOpen);
                        poll();
                    } else {
                        delete m_sdk;  // no SDK threads were started, so this is immediate
                        m_sdk = nullptr;
                        setState(LinkState::Failed, QStringLiteral("SDK could not open the UDP socket"));
                    }
                });
}

void PayloadLink::disconnectLink()
{
    if (!isOpen(m_state)) return;
    setState(LinkState::Disconnecting);
    PayloadSdkInterface *sdk = m_sdk;
    m_sdk = nullptr;
    runInWorker([sdk] { delete sdk; return true; },
                [this](bool) {
                    poll();  // frames observed right before the threads stopped
                    m_openSinceMs = -1;
                    setState(LinkState::Disconnected);
                });
}

// SDK I/O threads: copy and queue only. Never blocks on the GUI.
void PayloadLink::onTransport(int dir, const uint8_t *data, int len, const sockaddr_in *peer, int sysErrno)
{
    QByteArray bytes(reinterpret_cast<const char *>(data), len);
    const qint64 wall = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pending.size() >= kMaxPendingDatagrams) {
        ++m_dropped;
        return;
    }
    m_pending.push_back({m_nextSeq++, monoNs(), wall, dir, ntohl(peer->sin_addr.s_addr),
                         ntohs(peer->sin_port), sysErrno, std::move(bytes)});
}

void PayloadLink::poll()
{
    std::vector<Datagram> batch;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        batch.swap(m_pending);
    }

    std::vector<CapturedFrame> out;
    for (const Datagram &d : batch) {
        const qint64 ms = d.monoNs / 1000000;
        if (d.dir == CapturedFrame::Rx) m_obs.lastRx = ms;
        for (MavFrame &f : parseDatagram(reinterpret_cast<const uint8_t *>(d.bytes.constData()), size_t(d.bytes.size()))) {
            if (d.dir == CapturedFrame::Rx && f.status == MavFrame::Ok) {
                m_obs.lastValidRx = ms;
                if (f.msgid == MAVLINK_MSG_ID_HEARTBEAT) {
                    m_obs.lastHeartbeat = ms;
                    m_heartbeats[quint16(f.sysid << 8 | f.compid)] = ms;
                }
            }
            CapturedFrame c;
            c.index = CapturedFrame::nextIndex();
            c.datagram = d.seq;
            c.monoNs = d.monoNs;
            c.wallUs = d.wallUs;
            c.dir = d.dir;
            c.peerIp = d.peerIp;
            c.peerPort = d.peerPort;
            c.sendErrno = d.sysErrno;
            c.frame = std::move(f);
            c.summary = summarize(c.frame, m_dict);
            out.push_back(std::move(c));
        }
    }
    bool commands = false;
    for (const CapturedFrame &c : out) {
        m_telemetry.add(c);
        commands |= m_tracker.onFrame(c);
    }
    commands |= m_tracker.tick(monoNs() / 1000000);
    if (!out.empty()) emit framesCaptured(out);
    if (commands) emit commandsChanged();

    // Once per connection, as Gremsy's ui_demo does: ask for info and start the parameter streams.
    if (!m_autoInfoSent && isOpen(m_state) && m_telemetry.latest(MAVLINK_MSG_ID_HEARTBEAT, TelemetryStore::kPayloadCore)) {
        m_autoInfoSent = true;
        requestPayloadInfo(true);
    }

    if (isOpen(m_state)) {
        const LinkState s = evaluateLink(m_obs, monoNs() / 1000000, m_timeoutMs);
        if (s != m_state) setState(s);
    }
}

void PayloadLink::setState(LinkState s, const QString &error)
{
    m_state = s;
    m_error = error;
    emit stateChanged();
}

// ---------------------------------------------------------------------------------------------
// Payload commands. SDK calls only enqueue (mutex-protected), so they are safe from the GUI thread.

static qint64 nowMs() { return PayloadLink::monoNs() / 1000000; }

bool PayloadLink::ready()
{
    if (m_sdk && isOpen(m_state)) return true;
    m_commandError = QStringLiteral("Not connected");
    return false;
}

bool PayloadLink::gimbalMode(GimbalFlags::Change change, const QString &label)
{
    if (!ready()) return false;
    const CapturedFrame *status = m_telemetry.latest(MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS);
    if (!status) {
        // The SDK example starts from the gimbal's reported flags; without them the other bits are unknown.
        m_commandError = QStringLiteral("No GIMBAL_DEVICE_ATTITUDE_STATUS received yet: current gimbal flags unknown");
        return false;
    }
    GimbalAttitude a;
    gimbalAttitude(status->frame, &a);
    const quint16 flags = GimbalFlags::apply(a.flags, change);
    m_tracker.expect(QStringLiteral("%1 (flags 0x%2)").arg(label).arg(flags, 4, 16, QLatin1Char('0')),
                     MAVLINK_MSG_ID_GIMBAL_DEVICE_SET_ATTITUDE, 0, {}, nowMs(), [flags](const MavFrame &f) {
                         return mavlink_msg_gimbal_device_set_attitude_get_flags(&f.msg) == flags;
                     });
    m_sdk->setGimbalMode(flags);
    emit commandsChanged();
    return true;
}

bool PayloadLink::gimbalHome()
{
    if (!gimbalMode(GimbalFlags::Change::HomeSet, QStringLiteral("Gimbal home: set NEUTRAL"))) return false;
    QTimer::singleShot(100, this, [this] {  // the SDK example clears the bit again after 100 ms
        if (!gimbalMode(GimbalFlags::Change::HomeClear, QStringLiteral("Gimbal home: clear NEUTRAL")))
            qWarning("Gimbal home: could not clear NEUTRAL: %s", qPrintable(m_commandError));
    });
    return true;
}

bool PayloadLink::gimbalAngle(double pitch, double roll, double yaw)
{
    if (!ready()) return false;
    if (std::abs(pitch) > kPitchLimit || std::abs(roll) > kRollLimit || std::abs(yaw) > kYawLimit) {
        m_commandError = QStringLiteral("Angle outside SDK limits (pitch ±90°, roll ±180°, yaw ±180°)");
        return false;
    }
    m_tracker.expect(QStringLiteral("Gimbal angle pitch %1° roll %2° yaw %3°").arg(pitch).arg(roll).arg(yaw),
                     MAVLINK_MSG_ID_GIMBAL_DEVICE_SET_ATTITUDE, 0, {}, nowMs(), [](const MavFrame &f) {
                         float q[4];
                         mavlink_msg_gimbal_device_set_attitude_get_q(&f.msg, q);
                         return !std::isnan(q[0]);
                     });
    m_sdk->setGimbalSpeed(float(pitch), float(roll), float(yaw), INPUT_ANGLE);
    emit commandsChanged();
    return true;
}

bool PayloadLink::gimbalRate(double pitch, double roll, double yaw, bool track)
{
    if (!ready()) return false;
    if (track) {
        const float p = float(pitch * M_PI / 180), y = float(yaw * M_PI / 180);
        m_tracker.expect(QStringLiteral("Gimbal rate pitch %1°/s roll %2°/s yaw %3°/s").arg(pitch).arg(roll).arg(yaw),
                         MAVLINK_MSG_ID_GIMBAL_DEVICE_SET_ATTITUDE, 0, {}, nowMs(), [p, y](const MavFrame &f) {
                             float q[4];
                             mavlink_msg_gimbal_device_set_attitude_get_q(&f.msg, q);
                             return std::isnan(q[0]) &&
                                    std::abs(mavlink_msg_gimbal_device_set_attitude_get_angular_velocity_y(&f.msg) - p) < 1e-4f &&
                                    std::abs(mavlink_msg_gimbal_device_set_attitude_get_angular_velocity_z(&f.msg) - y) < 1e-4f;
                         });
        emit commandsChanged();
    }
    m_sdk->setGimbalSpeed(float(pitch), float(roll), float(yaw), INPUT_SPEED);
    return true;
}

bool PayloadLink::cameraParam(const QString &label, const char *paramId, quint32 value)
{
    if (!ready()) return false;
    m_tracker.expect(QStringLiteral("%1 (%2 = %3)").arg(label, QLatin1String(paramId)).arg(value),
                     MAVLINK_MSG_ID_PARAM_EXT_SET, 0, QString::fromLatin1(paramId), nowMs());
    char id[17] = {0};
    strncpy(id, paramId, 16);
    m_sdk->setPayloadCameraParam(id, value, PARAM_TYPE_UINT32);
    emit commandsChanged();
    return true;
}

bool PayloadLink::cameraZoom(int zoomType, float value, const QString &label)
{
    if (!ready()) return false;
    m_tracker.expect(label, MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_SET_CAMERA_ZOOM, {}, nowMs());
    m_sdk->setCameraZoom(float(zoomType), value);
    emit commandsChanged();
    return true;
}

bool PayloadLink::cameraFocus(int focusType, float value, const QString &label)
{
    if (!ready()) return false;
    m_tracker.expect(label, MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_SET_CAMERA_FOCUS, {}, nowMs());
    m_sdk->setCameraFocus(float(focusType), value);
    emit commandsChanged();
    return true;
}

bool PayloadLink::cameraMode(int mode)
{
    if (!ready()) return false;
    m_tracker.expect(mode == CAMERA_MODE_VIDEO ? QStringLiteral("Camera mode: video") : QStringLiteral("Camera mode: photo"),
                     MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_SET_CAMERA_MODE, {}, nowMs());
    m_sdk->setPayloadCameraMode(CAMERA_MODE(mode));
    emit commandsChanged();
    return true;
}

bool PayloadLink::captureImage()
{
    if (!ready()) return false;
    m_tracker.expect(QStringLiteral("Capture image"), MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_IMAGE_START_CAPTURE, {}, nowMs());
    m_sdk->setPayloadCameraCaptureImage(0);  // interval 0 = single shot
    emit commandsChanged();
    return true;
}

bool PayloadLink::recordVideo(bool start)
{
    if (!ready()) return false;
    m_tracker.expect(start ? QStringLiteral("Start recording") : QStringLiteral("Stop recording"), MAVLINK_MSG_ID_COMMAND_LONG,
                     start ? MAV_CMD_VIDEO_START_CAPTURE : MAV_CMD_VIDEO_STOP_CAPTURE, {}, nowMs());
    if (start) m_sdk->setPayloadCameraRecordVideoStart();
    else m_sdk->setPayloadCameraRecordVideoStop();
    emit commandsChanged();
    return true;
}

bool PayloadLink::requestPayloadInfo(bool automatic)
{
    if (!ready()) return false;
    const QString p = automatic ? QStringLiteral("Auto: ") : QString();
    const qint64 now = nowMs();
    auto track = [&](const char *what, quint16 cmd, quint32 response) {
        m_tracker.expect(p + QLatin1String(what), MAVLINK_MSG_ID_COMMAND_LONG, cmd, {}, now, {}, response);
    };
    track("Request component info", MAV_CMD_REQUEST_MESSAGE, MAVLINK_MSG_ID_COMPONENT_INFORMATION_BASIC);
    m_sdk->getPayloadComponentBasicInformation();
    track("Request camera info", MAV_CMD_REQUEST_CAMERA_INFORMATION, MAVLINK_MSG_ID_CAMERA_INFORMATION);
    m_sdk->getPayloadCameraInformation();
    track("Request camera settings", MAV_CMD_REQUEST_CAMERA_SETTINGS, MAVLINK_MSG_ID_CAMERA_SETTINGS);
    m_sdk->getPayloadCameraMode();
    track("Request storage info", MAV_CMD_REQUEST_STORAGE_INFORMATION, MAVLINK_MSG_ID_STORAGE_INFORMATION);
    m_sdk->getPayloadStorage();
    track("Request capture status", MAV_CMD_REQUEST_CAMERA_CAPTURE_STATUS, MAVLINK_MSG_ID_CAMERA_CAPTURE_STATUS);
    m_sdk->getPayloadCaptureStatus();

    // DEBUG parameter streams, with the intervals (ms) Gremsy's tests/ui_demo/main.cpp uses
    static const std::pair<int, int> rates[] = {
        {PARAM_EO_ZOOM_LEVEL, 1000}, {PARAM_IR_ZOOM_LEVEL, 1000}, {PARAM_LRF_RANGE, 100},
        {PARAM_LRF_OFSET_X, 100}, {PARAM_LRF_OFSET_Y, 100}, {PARAM_TARGET_COOR_LON, 1000},
        {PARAM_TARGET_COOR_LAT, 1000}, {PARAM_TARGET_COOR_ALT, 1000}, {PARAM_CAM_VIEW_MODE, 1000},
        {PARAM_CAM_REC_SOURCE, 1000}, {PARAM_CAM_IR_TYPE, 1000}, {PARAM_CAM_IR_PALETTE_ID, 1000},
        {PARAM_GIMBAL_MODE, 1000}, {PARAM_PAYLOAD_GPS_LON, 1000}, {PARAM_PAYLOAD_GPS_LAT, 1000},
        {PARAM_PAYLOAD_GPS_ALT, 1000}};
    for (const auto &r : rates) {
        m_tracker.expect(p + QStringLiteral("Stream %1 every %2 ms").arg(payloadParamName(r.first)).arg(r.second),
                         MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_SET_MESSAGE_INTERVAL, {}, now,
                         [idx = r.first, us = r.second * 1000](const MavFrame &f) {  // the SDK sends its own stream requests too
                             return int(mavlink_msg_command_long_get_param1(&f.msg)) == idx &&
                                    int(mavlink_msg_command_long_get_param2(&f.msg)) == us;
                         });
        m_sdk->setParamRate(r.first, uint16_t(r.second));
    }
    emit commandsChanged();
    return true;
}

QString PayloadLink::payloadParamName(int index)
{
    return index >= 0 && index < PARAM_COUNT ? QString::fromLatin1(payloadParams[index].id) : QStringLiteral("#%1").arg(index);
}
