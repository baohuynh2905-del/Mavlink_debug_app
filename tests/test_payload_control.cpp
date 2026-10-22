// Stage 4: gimbal flag rules, command/ACK correlation, telemetry, and real SDK commands on the wire.
#include "command_tracker.h"
#include "payload_link.h"
#include "payload_profile.h"
#include "telemetry.h"

#include <QtTest>

#include <cmath>
#include <functional>

#include "fake_payload.h"

static quint64 g_index = 0;

static CapturedFrame frameOf(const mavlink_message_t &m, int dir, qint64 ms, int sendErrno = 0)
{
    uint8_t b[MAVLINK_MAX_PACKET_LEN];
    const uint16_t n = mavlink_msg_to_send_buffer(b, &m);
    CapturedFrame c;
    c.index = g_index++;
    c.dir = dir;
    c.monoNs = ms * 1000000;
    c.sendErrno = sendErrno;
    c.frame = parseDatagram(b, n).at(0);
    return c;
}

static mavlink_message_t commandLong(quint16 cmd, uint8_t targetComp, float p1 = 0)
{
    mavlink_message_t m;
    mavlink_msg_command_long_pack_chan(1, 193, MAVLINK_COMM_0, &m, 1, targetComp, cmd, 1, p1, 0, 0, 0, 0, 0, 0);
    return m;
}

static mavlink_message_t commandAck(uint8_t from, quint16 cmd, uint8_t result, uint8_t progress = 0)
{
    mavlink_message_t m;
    mavlink_msg_command_ack_pack_chan(1, from, MAVLINK_COMM_0, &m, cmd, result, progress, 0, 1, 193);
    return m;
}

static mavlink_message_t paramExt(bool ack, const char *id, uint8_t from, uint8_t result = PARAM_ACK_ACCEPTED)
{
    mavlink_message_t m;
    char pid[16] = {0};
    strncpy(pid, id, 16);
    char value[128] = {1};
    if (ack) mavlink_msg_param_ext_ack_pack_chan(1, from, MAVLINK_COMM_0, &m, pid, value, MAV_PARAM_EXT_TYPE_UINT32, result);
    else mavlink_msg_param_ext_set_pack_chan(1, from, MAVLINK_COMM_0, &m, 1, 101, pid, value, MAV_PARAM_EXT_TYPE_UINT32);
    return m;
}

static mavlink_message_t gimbalStatus(quint16 flags)
{
    mavlink_message_t m;
    float q[4];
    mavlink_euler_to_quaternion(0, float(-30 * M_PI / 180), float(10 * M_PI / 180), q);
    mavlink_msg_gimbal_device_attitude_status_pack_chan(1, MAV_COMP_ID_GIMBAL, MAVLINK_COMM_0, &m, 0, 0, 0, flags, q,
                                                        0, 0, 0, 0, NAN, NAN, 0);
    return m;
}

#define QTRY_STATE(link, s, ms) QTRY_COMPARE_WITH_TIMEOUT(linkStateText((link).state()), linkStateText(s), ms)

class TestPayloadControl : public QObject
{
    Q_OBJECT

    // Waits (processing events) until the fake payload receives a frame matching pred.
    static bool waitTx(FakePayload &fake, const std::function<bool(const MavFrame &)> &pred, MavFrame *out = nullptr, int ms = 3000)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            for (MavFrame &f : fake.received()) {
                if (pred(f)) {
                    if (out) *out = f;
                    return true;
                }
            }
            QTest::qWait(10);
        }
        return false;
    }

    static const TrackedCommand *find(const PayloadLink &link, const QString &actionPrefix)
    {
        for (const TrackedCommand &t : link.tracker().commands())
            if (t.action.startsWith(actionPrefix)) return &t;
        return nullptr;
    }

private slots:
    void gimbalFlagRules()
    {
        using C = GimbalFlags::Change;
        const quint16 R = GIMBAL_DEVICE_FLAGS_RETRACT, N = GIMBAL_DEVICE_FLAGS_NEUTRAL, Y = GIMBAL_DEVICE_FLAGS_YAW_LOCK,
                      M = GimbalFlags::Mapping, P = GIMBAL_DEVICE_FLAGS_PITCH_LOCK;
        QCOMPARE(GimbalFlags::apply(R | N | P, C::Lock), quint16(Y | P));  // unrelated bits are kept
        QCOMPARE(GimbalFlags::apply(M | Y, C::Follow), quint16(0));
        QCOMPARE(GimbalFlags::apply(R | Y, C::Mapping), quint16(M | Y));
        QCOMPARE(GimbalFlags::apply(M | Y, C::MotorsOff), quint16(R | Y));
        QCOMPARE(GimbalFlags::apply(R | Y, C::MotorsOn), quint16(Y));
        QCOMPARE(GimbalFlags::apply(M | R | Y, C::HomeSet), quint16(N | Y));
        QCOMPARE(GimbalFlags::apply(N | Y, C::HomeClear), quint16(Y));
        QCOMPARE(GimbalFlags::modeName(Y | R), QStringLiteral("LOCK"));  // same priority as the SDK handler
        QCOMPARE(GimbalFlags::modeName(R), QStringLiteral("OFF (motors off)"));
        QCOMPARE(GimbalFlags::modeName(M), QStringLiteral("MAPPING"));
        QCOMPARE(GimbalFlags::modeName(0), QStringLiteral("FOLLOW"));
    }

    void trackerCommandAck()
    {
        CommandTracker t;
        t.expect(QStringLiteral("zoom"), MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_SET_CAMERA_ZOOM, {}, 0);
        QCOMPARE(t.commands().back().state, TrackedCommand::Queued);

        QVERIFY(!t.onFrame(frameOf(commandLong(MAV_CMD_SET_CAMERA_FOCUS, 100), CapturedFrame::Tx, 5)));  // other command
        QVERIFY(t.onFrame(frameOf(commandLong(MAV_CMD_SET_CAMERA_ZOOM, 100), CapturedFrame::Tx, 10)));
        const TrackedCommand &c = t.commands().back();
        QCOMPARE(c.state, TrackedCommand::Sent);
        QCOMPARE(c.targetComponent, 100);
        QCOMPARE(c.sentMs, 10);

        QVERIFY(!t.onFrame(frameOf(commandAck(154, MAV_CMD_SET_CAMERA_ZOOM, MAV_RESULT_ACCEPTED), CapturedFrame::Rx, 20)));  // wrong component
        QVERIFY(!t.onFrame(frameOf(commandAck(100, MAV_CMD_SET_CAMERA_FOCUS, MAV_RESULT_ACCEPTED), CapturedFrame::Rx, 20)));  // wrong command
        QVERIFY(t.onFrame(frameOf(commandAck(100, MAV_CMD_SET_CAMERA_ZOOM, MAV_RESULT_IN_PROGRESS, 40), CapturedFrame::Rx, 30)));
        QCOMPARE(c.state, TrackedCommand::InProgress);
        QCOMPARE(c.progress, 40);
        QVERIFY(t.onFrame(frameOf(commandAck(100, MAV_CMD_SET_CAMERA_ZOOM, MAV_RESULT_ACCEPTED), CapturedFrame::Rx, 50)));
        QCOMPARE(c.state, TrackedCommand::Accepted);
        QCOMPARE(c.ackFrom, 100);

        t.expect(QStringLiteral("record"), MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_VIDEO_START_CAPTURE, {}, 100);
        t.onFrame(frameOf(commandLong(MAV_CMD_VIDEO_START_CAPTURE, 100), CapturedFrame::Tx, 110));
        t.onFrame(frameOf(commandAck(100, MAV_CMD_VIDEO_START_CAPTURE, MAV_RESULT_DENIED), CapturedFrame::Rx, 120));
        QCOMPARE(t.commands().back().state, TrackedCommand::Rejected);
        QCOMPARE(t.commands().back().result, int(MAV_RESULT_DENIED));

        // REQUEST_MESSAGE answered by the message itself, with no COMMAND_ACK (real VIO behaviour)
        t.expect(QStringLiteral("info"), MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_REQUEST_MESSAGE, {}, 200, {},
                 MAVLINK_MSG_ID_COMPONENT_INFORMATION_BASIC);
        t.onFrame(frameOf(commandLong(MAV_CMD_REQUEST_MESSAGE, 26, MAVLINK_MSG_ID_COMPONENT_INFORMATION_BASIC), CapturedFrame::Tx, 210));
        mavlink_message_t info;
        char e[32] = {0};  // pack copies the full array length
        mavlink_msg_component_information_basic_pack_chan(1, 154, MAVLINK_COMM_0, &info, 0, 0, 0, e, e, e, e, e);
        QVERIFY(!t.onFrame(frameOf(info, CapturedFrame::Rx, 212)));  // from another component
        mavlink_msg_component_information_basic_pack_chan(1, 26, MAVLINK_COMM_0, &info, 0, 0, 0, e, e, e, e, e);
        QVERIFY(t.onFrame(frameOf(info, CapturedFrame::Rx, 214)));
        QCOMPARE(t.commands().back().state, TrackedCommand::Answered);
        QVERIFY(!t.tick(214 + 10 * CommandTracker::kAckTimeoutMs));  // answered: never times out
        t.onFrame(frameOf(commandAck(26, MAV_CMD_REQUEST_MESSAGE, MAV_RESULT_ACCEPTED), CapturedFrame::Rx, 300));
        QCOMPARE(t.commands().back().state, TrackedCommand::Accepted);  // a late ACK still counts
    }

    void trackerParamAckAndTimeouts()
    {
        CommandTracker t;
        t.expect(QStringLiteral("view"), MAVLINK_MSG_ID_PARAM_EXT_SET, 0, QStringLiteral("C_SOURCE"), 0);
        t.onFrame(frameOf(paramExt(false, "C_SOURCE", 193), CapturedFrame::Tx, 10));
        QCOMPARE(t.commands().back().state, TrackedCommand::Sent);
        QVERIFY(!t.onFrame(frameOf(paramExt(true, "C_V_REC", 101), CapturedFrame::Rx, 20)));  // other parameter
        QVERIFY(t.onFrame(frameOf(paramExt(true, "C_SOURCE", 101, PARAM_ACK_VALUE_UNSUPPORTED), CapturedFrame::Rx, 30)));
        QCOMPARE(t.commands().back().state, TrackedCommand::Rejected);
        QCOMPARE(t.commands().back().result, int(PARAM_ACK_VALUE_UNSUPPORTED));

        // GIMBAL_DEVICE_SET_ATTITUDE has no ACK; the content check picks the right frame
        t.expect(QStringLiteral("mode"), MAVLINK_MSG_ID_GIMBAL_DEVICE_SET_ATTITUDE, 0, {}, 100,
                 [](const MavFrame &f) { return mavlink_msg_gimbal_device_set_attitude_get_flags(&f.msg) == 0x10; });
        mavlink_message_t m;
        float q[4] = {NAN, NAN, NAN, NAN};
        mavlink_msg_gimbal_device_set_attitude_pack_chan(1, 193, MAVLINK_COMM_0, &m, 1, 154, 0x00, q, 0, 0, 0);
        QVERIFY(!t.onFrame(frameOf(m, CapturedFrame::Tx, 105)));  // untracked rate repeat with other flags
        mavlink_msg_gimbal_device_set_attitude_pack_chan(1, 193, MAVLINK_COMM_0, &m, 1, 154, 0x10, q, 0, 0, 0);
        QVERIFY(t.onFrame(frameOf(m, CapturedFrame::Tx, 110)));
        QCOMPARE(t.commands().back().state, TrackedCommand::NoAckDefined);

        // Timeouts: never on the wire, and on the wire without an ACK
        t.expect(QStringLiteral("lost in queue"), MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_IMAGE_START_CAPTURE, {}, 1000);
        t.expect(QStringLiteral("unanswered"), MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_SET_CAMERA_MODE, {}, 1000);
        t.onFrame(frameOf(commandLong(MAV_CMD_SET_CAMERA_MODE, 100), CapturedFrame::Tx, 1010));
        QVERIFY(!t.tick(1000 + CommandTracker::kSendTimeoutMs));
        QVERIFY(t.tick(1011 + CommandTracker::kAckTimeoutMs));
        QCOMPARE(t.commands()[t.commands().size() - 2].state, TrackedCommand::NotSent);
        QCOMPARE(t.commands().back().state, TrackedCommand::AckTimeout);

        // A failed sendto() is reported as such
        t.expect(QStringLiteral("send fails"), MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_IMAGE_STOP_CAPTURE, {}, 2000);
        t.onFrame(frameOf(commandLong(MAV_CMD_IMAGE_STOP_CAPTURE, 100), CapturedFrame::Tx, 2001, ENETUNREACH));
        QCOMPARE(t.commands().back().state, TrackedCommand::SendFailed);

        for (int i = 0; i < 600; ++i) t.expect(QStringLiteral("x"), 0, 0, {}, 3000);
        QCOMPARE(t.commands().size(), CommandTracker::kMaxHistory);
    }

    void telemetryStore()
    {
        TelemetryStore s;
        s.add(frameOf(gimbalStatus(0x10), CapturedFrame::Tx, 1));  // TX is never telemetry
        QVERIFY(!s.latest(MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS));
        s.add(frameOf(gimbalStatus(0x10), CapturedFrame::Rx, 2));
        const CapturedFrame *c = s.latest(MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS);
        QVERIFY(c);
        GimbalAttitude a;
        QVERIFY(gimbalAttitude(c->frame, &a));
        QVERIFY(std::abs(a.pitchDeg - (-30)) < 0.01 && std::abs(a.yawDeg - 10) < 0.01);
        QCOMPARE(a.flags, quint16(0x10));
        QVERIFY(std::isnan(a.rateX) == false && std::abs(a.rateX) < 1e-9);

        mavlink_message_t m;
        mavlink_msg_debug_pack_chan(1, TelemetryStore::kPayloadCore, MAVLINK_COMM_0, &m, 0, 2, 123.5f);
        s.add(frameOf(m, CapturedFrame::Rx, 3));
        mavlink_msg_debug_pack_chan(1, MAV_COMP_ID_GIMBAL, MAVLINK_COMM_0, &m, 0, 5, 9.f);  // not the payload core
        s.add(frameOf(m, CapturedFrame::Rx, 4));
        QCOMPARE(s.payloadParams().size(), 1);
        QCOMPARE(s.payloadParams().value(2).value, 123.5);
        QCOMPARE(PayloadLink::payloadParamName(2), QStringLiteral("LRF_RANGE"));
    }

    void vioProfileComesFromSdkHeader()
    {
        const PayloadProfile &p = vioProfile();
        QCOMPARE(QString::fromLatin1(p.viewSourceId), QStringLiteral("C_SOURCE"));
        QCOMPARE(QString::fromLatin1(p.recordSourceId), QStringLiteral("C_V_REC"));
        QCOMPARE(QString::fromLatin1(p.irPaletteId), QStringLiteral("C_T_PALETTE"));
        QCOMPARE(p.viewSources.at(1).value, 1u);  // EO only, per Mavlink_Command_List and vio_sdk.h
        QCOMPARE(p.recordSources.back().value, 5u);
        QCOMPARE(p.irPalettes.size(), size_t(10));
    }

    void commandsOnTheWire()
    {
        PayloadLink link;
        QVERIFY(!link.captureImage());
        QCOMPARE(link.commandError(), QStringLiteral("Not connected"));

        FakePayload fake;
        link.connectTo(QStringLiteral("127.0.0.1"), fake.port);
        QTRY_STATE(link, LinkState::SocketOpen, 5000);
        QVERIFY(fake.learnAppEndpoint());

        // Payload core heartbeat -> one automatic info request (no movement or camera action)
        fake.sendHeartbeat(TelemetryStore::kPayloadCore, MAV_TYPE_GENERIC);
        MavFrame f;
        QVERIFY(waitTx(fake, [](const MavFrame &x) {
            return x.msgid == MAVLINK_MSG_ID_COMMAND_LONG && mavlink_msg_command_long_get_command(&x.msg) == MAV_CMD_REQUEST_MESSAGE &&
                   int(mavlink_msg_command_long_get_param1(&x.msg)) == MAVLINK_MSG_ID_COMPONENT_INFORMATION_BASIC;
        }, &f));
        QCOMPARE(int(f.targetComponent), int(TelemetryStore::kPayloadCore));
        fake.send(commandAck(TelemetryStore::kPayloadCore, MAV_CMD_REQUEST_MESSAGE, MAV_RESULT_ACCEPTED));
        QTRY_VERIFY_WITH_TIMEOUT(find(link, QStringLiteral("Auto: Request component info")) &&
                                 find(link, QStringLiteral("Auto: Request component info"))->state == TrackedCommand::Accepted, 3000);

        // Camera parameter: PARAM_EXT_SET carries the uint32 bytes, as setPayloadCameraParam() encodes them
        QVERIFY(link.cameraParam(QStringLiteral("View source"), vioProfile().viewSourceId, 1));
        QVERIFY(waitTx(fake, [](const MavFrame &x) { return x.msgid == MAVLINK_MSG_ID_PARAM_EXT_SET; }, &f));
        char id[17] = {0}, value[129] = {0};
        mavlink_msg_param_ext_set_get_param_id(&f.msg, id);
        mavlink_msg_param_ext_set_get_param_value(&f.msg, value);
        QCOMPARE(QString::fromLatin1(id), QStringLiteral("C_SOURCE"));
        QCOMPARE(int(value[0]), 1);
        QCOMPARE(int(mavlink_msg_param_ext_set_get_param_type(&f.msg)), int(MAV_PARAM_EXT_TYPE_UINT32));
        fake.send(paramExt(true, "C_SOURCE", f.targetComponent > 0 ? uint8_t(f.targetComponent) : 101));
        QTRY_VERIFY_WITH_TIMEOUT(find(link, QStringLiteral("View source"))->state == TrackedCommand::Accepted, 3000);

        // Continuous zoom, rejected by the camera
        QVERIFY(link.cameraZoom(ZOOM_TYPE_CONTINUOUS, 1, QStringLiteral("Zoom in")));
        QVERIFY(waitTx(fake, [](const MavFrame &x) {
            return x.msgid == MAVLINK_MSG_ID_COMMAND_LONG && mavlink_msg_command_long_get_command(&x.msg) == MAV_CMD_SET_CAMERA_ZOOM;
        }, &f));
        QCOMPARE(mavlink_msg_command_long_get_param1(&f.msg), float(ZOOM_TYPE_CONTINUOUS));
        QCOMPARE(mavlink_msg_command_long_get_param2(&f.msg), 1.f);
        fake.send(commandAck(uint8_t(f.targetComponent), MAV_CMD_SET_CAMERA_ZOOM, MAV_RESULT_DENIED));
        QTRY_VERIFY_WITH_TIMEOUT(find(link, QStringLiteral("Zoom in"))->state == TrackedCommand::Rejected, 3000);

        // Gimbal: mode is refused until the gimbal has reported its flags
        QVERIFY(!link.gimbalMode(GimbalFlags::Change::Follow, QStringLiteral("Follow")));
        QVERIFY(link.commandError().contains(QStringLiteral("GIMBAL_DEVICE_ATTITUDE_STATUS")));
        fake.send(gimbalStatus(GIMBAL_DEVICE_FLAGS_YAW_LOCK | GIMBAL_DEVICE_FLAGS_PITCH_LOCK));
        QTRY_VERIFY_WITH_TIMEOUT(link.telemetry().latest(MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS), 3000);
        QVERIFY(link.gimbalMode(GimbalFlags::Change::Follow, QStringLiteral("Follow")));
        QVERIFY(waitTx(fake, [](const MavFrame &x) { return x.msgid == MAVLINK_MSG_ID_GIMBAL_DEVICE_SET_ATTITUDE; }, &f));
        QCOMPARE(mavlink_msg_gimbal_device_set_attitude_get_flags(&f.msg), quint16(GIMBAL_DEVICE_FLAGS_PITCH_LOCK));
        QTRY_VERIFY_WITH_TIMEOUT(find(link, QStringLiteral("Follow"))->state == TrackedCommand::NoAckDefined, 3000);

        // Angle: SDK limits checked before sending; the quaternion encodes the requested angles
        QVERIFY(!link.gimbalAngle(95, 0, 0));
        QVERIFY(link.gimbalAngle(-30, 0, 10));
        QVERIFY(waitTx(fake, [](const MavFrame &x) { return x.msgid == MAVLINK_MSG_ID_GIMBAL_DEVICE_SET_ATTITUDE; }, &f));
        float q[4], r, p, y;
        mavlink_msg_gimbal_device_set_attitude_get_q(&f.msg, q);
        mavlink_quaternion_to_euler(q, &r, &p, &y);
        QVERIFY(std::abs(p * 180 / M_PI - (-30)) < 0.01 && std::abs(y * 180 / M_PI - 10) < 0.01);

        // No answer: reported as an ACK timeout, not as a failure
        QVERIFY(link.recordVideo(true));
        QTRY_VERIFY_WITH_TIMEOUT(find(link, QStringLiteral("Start recording"))->state == TrackedCommand::AckTimeout, 6000);

        link.disconnectLink();
        QTRY_STATE(link, LinkState::Disconnected, 3000);
        QVERIFY(!link.gimbalAngle(0, 0, 0));  // refused once disconnected
    }
};

QTEST_GUILESS_MAIN(TestPayloadControl)
#include "test_payload_control.moc"
