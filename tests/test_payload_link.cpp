// PayloadLink against a loopback fake payload: real SDK, real sockets, no hardware.
#include "payload_link.h"

#include <QElapsedTimer>
#include <QtTest>

#include "fake_payload.h"

#define QTRY_STATE(link, s, ms) QTRY_COMPARE_WITH_TIMEOUT(linkStateText((link).state()), linkStateText(s), ms)

class TestPayloadLink : public QObject
{
    Q_OBJECT
private slots:
    void invalidEndpointNeverConnects()
    {
        PayloadLink link;
        link.connectTo(QStringLiteral("192.168.55"), 14566);
        QCOMPARE(link.state(), LinkState::Failed);
        QVERIFY(link.lastError().contains(QStringLiteral("IPv4")));
    }

    void reportsOnlyObservedLevels()
    {
        FakePayload fake;
        PayloadLink link;
        link.setTimeoutMs(1000);

        std::vector<CapturedFrame> seen;
        bool allOnGuiThread = true;
        connect(&link, &PayloadLink::framesCaptured, this, [&](const std::vector<CapturedFrame> &f) {
            allOnGuiThread &= QThread::currentThread() == qApp->thread();
            seen.insert(seen.end(), f.begin(), f.end());
        });

        link.connectTo(QStringLiteral("127.0.0.1"), fake.port);
        QCOMPARE(link.state(), LinkState::Connecting);
        QTRY_STATE(link, LinkState::SocketOpen, 5000);
        QVERIFY(fake.learnAppEndpoint());

        fake.send(QByteArray("not mavlink"));
        QTRY_STATE(link, LinkState::BytesOnly, 2000);

        mavlink_message_t m;
        mavlink_msg_system_time_pack_chan(1, MAV_COMP_ID_GIMBAL, MAVLINK_COMM_0, &m, 0, 1234);
        fake.send(m);
        QTRY_STATE(link, LinkState::Mavlink, 2000);

        mavlink_msg_heartbeat_pack_chan(1, MAV_COMP_ID_GIMBAL, MAVLINK_COMM_0, &m, MAV_TYPE_GIMBAL, MAV_AUTOPILOT_INVALID, 0, 0, MAV_STATE_ACTIVE);
        fake.send(m);
        QTRY_STATE(link, LinkState::Heartbeat, 2000);
        QVERIFY(link.heartbeatSources().contains(quint16(1 << 8 | MAV_COMP_ID_GIMBAL)));

        // The payload goes silent while the SDK keeps sending: the link must turn stale.
        QTRY_STATE(link, LinkState::Stale, 4000);

        QVERIFY(allOnGuiThread);
        bool txHeartbeat = false, rxGarbage = false, rxHeartbeat = false, ordered = true;
        for (size_t i = 0; i < seen.size(); ++i) {
            const CapturedFrame &c = seen[i];
            if (i) ordered &= c.index > seen[i - 1].index;
            QCOMPARE(int(c.peerPort), fake.port);  // TX destination and RX source are the payload
            if (c.dir == CapturedFrame::Tx && c.frame.status == MavFrame::Ok && c.frame.msgid == MAVLINK_MSG_ID_HEARTBEAT)
                txHeartbeat = c.sendErrno == 0;
            if (c.dir == CapturedFrame::Rx && c.frame.status == MavFrame::Garbage) rxGarbage = true;
            if (c.dir == CapturedFrame::Rx && c.frame.status == MavFrame::Ok && c.frame.msgid == MAVLINK_MSG_ID_HEARTBEAT)
                rxHeartbeat = c.frame.compid == MAV_COMP_ID_GIMBAL;
        }
        QVERIFY(ordered);
        QVERIFY(txHeartbeat);
        QVERIFY(rxGarbage);
        QVERIFY(rxHeartbeat);

        QElapsedTimer t;
        t.start();
        link.disconnectLink();
        QCOMPARE(link.state(), LinkState::Disconnecting);
        QTRY_STATE(link, LinkState::Disconnected, 3000);
        QVERIFY(t.elapsed() < 2000);
        QCOMPARE(link.openSinceMs(), qint64(-1));

        // Reconnect on the same object
        link.connectTo(QStringLiteral("127.0.0.1"), fake.port);
        QTRY_STATE(link, LinkState::SocketOpen, 5000);
        QVERIFY(link.heartbeatSources().isEmpty());  // nothing carried over from the previous session
    }

    void destroyWhileOpen()
    {
        FakePayload fake;
        auto *link = new PayloadLink;
        link->connectTo(QStringLiteral("127.0.0.1"), fake.port);
        QTRY_STATE(*link, LinkState::SocketOpen, 5000);
        QElapsedTimer t;
        t.start();
        delete link;
        QVERIFY(t.elapsed() < 2000);
    }

    void destroyWhileConnecting()
    {
        FakePayload fake;
        auto *link = new PayloadLink;
        link->connectTo(QStringLiteral("127.0.0.1"), fake.port);
        QCOMPARE(link->state(), LinkState::Connecting);
        delete link;  // waits for the SDK init worker, then shuts the SDK down
    }
};

QTEST_GUILESS_MAIN(TestPayloadLink)
#include "test_payload_link.moc"
