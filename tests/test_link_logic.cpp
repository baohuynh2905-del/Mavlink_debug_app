#include "frame_model.h"
#include "link_health.h"
#include "profiles.h"

#include <QMap>
#include <QSettings>
#include <QTemporaryDir>

#include <QtTest>

class TestLinkLogic : public QObject
{
    Q_OBJECT
private slots:
    void endpointValidation_data()
    {
        QTest::addColumn<QString>("ip");
        QTest::addColumn<int>("port");
        QTest::addColumn<bool>("valid");
        QTest::newRow("default") << "192.168.55.1" << 14566 << true;
        QTest::newRow("spaces trimmed") << " 10.0.0.2 " << 14550 << true;
        QTest::newRow("loopback") << "127.0.0.1" << 1 << true;
        QTest::newRow("max port") << "10.0.0.2" << 65535 << true;
        QTest::newRow("three octets") << "192.168.55" << 14566 << false;
        QTest::newRow("octet > 255") << "256.1.1.1" << 14566 << false;
        QTest::newRow("leading zero") << "01.2.3.4" << 14566 << false;
        QTest::newRow("hostname") << "payload.local" << 14566 << false;
        QTest::newRow("ipv6") << "::1" << 14566 << false;
        QTest::newRow("empty") << "" << 14566 << false;
        QTest::newRow("any") << "0.0.0.0" << 14566 << false;
        QTest::newRow("broadcast") << "255.255.255.255" << 14566 << false;
        QTest::newRow("port 0") << "10.0.0.2" << 0 << false;
        QTest::newRow("port 65536") << "10.0.0.2" << 65536 << false;
    }
    void endpointValidation()
    {
        QFETCH(QString, ip);
        QFETCH(int, port);
        QFETCH(bool, valid);
        QCOMPARE(validateEndpoint(ip, port).isEmpty(), valid);
    }

    void linkEvaluation()
    {
        const qint64 t = 10000, to = 3000;
        LinkObservations o;
        QCOMPARE(evaluateLink(o, t, to), LinkState::SocketOpen);   // nothing received yet
        o.lastRx = t - 100;
        QCOMPARE(evaluateLink(o, t, to), LinkState::BytesOnly);    // bytes, but no valid frame
        o.lastValidRx = t - 100;
        QCOMPARE(evaluateLink(o, t, to), LinkState::Mavlink);      // valid frames, no heartbeat
        o.lastHeartbeat = t - 100;
        QCOMPARE(evaluateLink(o, t, to), LinkState::Heartbeat);
        o.lastHeartbeat = t - to - 1;
        QCOMPARE(evaluateLink(o, t, to), LinkState::Mavlink);      // heartbeat aged out, data still flowing
        o.lastRx = o.lastValidRx = t - to - 1;
        QCOMPARE(evaluateLink(o, t, to), LinkState::Stale);
        QVERIFY(isOpen(LinkState::Stale) && !isOpen(LinkState::Failed) && !isOpen(LinkState::Connecting));
    }

    void statusLamps()
    {
        QCOMPARE(linkLamp(LinkState::Heartbeat), Lamp::Ok);
        QCOMPARE(linkLamp(LinkState::Mavlink), Lamp::Warn);
        QCOMPARE(linkLamp(LinkState::Stale), Lamp::Bad);
        QCOMPARE(linkLamp(LinkState::Disconnected), Lamp::Off);

        const qint64 t = 10000, to = 3000;
        QMap<quint16, qint64> hb;
        hb[1 << 8 | 26] = t - 100;
        hb[1 << 8 | 154] = t - to - 1;  // aged out
        hb[2 << 8 | 100] = t;           // another sysid still counts
        hb[1 << 8 | 1] = t;             // not an expected component
        const QList<quint8> expected{26, 100, 154};
        QCOMPARE(freshComponents(hb, expected, t, to), 2);
        QCOMPARE(payloadLamp(true, 2, 3), Lamp::Warn);
        hb[1 << 8 | 154] = t;
        QCOMPARE(freshComponents(hb, expected, t, to), 3);
        QCOMPARE(payloadLamp(true, 3, 3), Lamp::Ok);
        QCOMPARE(payloadLamp(true, 0, 3), Lamp::Bad);
        QCOMPARE(payloadLamp(false, 3, 3), Lamp::Off);
    }

    void trafficStats()
    {
        auto make = [](int dir, MavFrame::Status st, size_t bytes, int err = 0) {
            CapturedFrame c;
            c.dir = dir;
            c.frame.status = st;
            c.frame.raw.assign(bytes, 0);
            c.sendErrno = err;
            return c;
        };
        TrafficStats s;
        s.add(make(CapturedFrame::Rx, MavFrame::Ok, 21));
        s.add(make(CapturedFrame::Rx, MavFrame::BadCrc, 21));
        s.add(make(CapturedFrame::Rx, MavFrame::UnknownId, 14));
        s.add(make(CapturedFrame::Rx, MavFrame::Garbage, 3));
        s.add(make(CapturedFrame::Rx, MavFrame::Truncated, 5));
        s.add(make(CapturedFrame::Tx, MavFrame::Ok, 21, ENETUNREACH));
        QCOMPARE(s.frames[CapturedFrame::Rx], 3ull);
        QCOMPARE(s.bytes[CapturedFrame::Rx], 64ull);
        QCOMPARE(s.frames[CapturedFrame::Tx], 1ull);
        QCOMPARE(s.badCrc, 1ull);
        QCOMPARE(s.unknownId, 1ull);
        QCOMPARE(s.garbage, 1ull);
        QCOMPARE(s.truncated, 1ull);
        QCOMPARE(s.sendErrors, 1ull);
    }

    void frameModelIsBounded()
    {
        FrameModel m(5);
        auto batch = [](quint64 first, int n) {
            std::vector<CapturedFrame> v(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) v[size_t(i)].index = first + quint64(i);
            return v;
        };
        m.append(batch(0, 3));
        QCOMPARE(m.rowCount(), 3);
        m.append(batch(3, 4));  // 7 total -> oldest 2 evicted
        QCOMPARE(m.rowCount(), 5);
        QCOMPARE(m.evicted(), 2ull);
        QCOMPARE(m.frameAt(0).index, 2ull);
        QCOMPARE(m.frameAt(4).index, 6ull);
        m.append(batch(7, 8));  // a batch larger than the capacity keeps only its newest rows
        QCOMPARE(m.rowCount(), 5);
        QCOMPARE(m.frameAt(0).index, 10ull);
        QCOMPARE(m.evicted(), 10ull);
        QCOMPARE(m.data(m.index(0, FrameModel::Dir), Qt::DisplayRole).toString(), QStringLiteral("RX"));
        m.clear();
        QCOMPARE(m.rowCount(), 0);
    }

    void connectionProfiles()
    {
        QList<ConnectionProfile> list;
        QVERIFY(upsertProfile(list, {QStringLiteral(" Bench VIO "), QStringLiteral("192.168.55.1"), 14566, 3}).isEmpty());
        QVERIFY(upsertProfile(list, {QStringLiteral("Drone"), QStringLiteral("10.0.0.2"), 14550, 5, QStringLiteral("MB1")}).isEmpty());
        QVERIFY(upsertProfile(list, {QStringLiteral("bench vio"), QStringLiteral("192.168.55.2"), 14566, 3}).isEmpty());  // replaces
        QCOMPARE(list.size(), 2);
        QCOMPARE(list[0].ip, QStringLiteral("192.168.55.2"));
        QCOMPARE(list[0].name, QStringLiteral("bench vio"));
        QVERIFY(!upsertProfile(list, {QStringLiteral("bad"), QStringLiteral("1.2.3"), 14566, 3}).isEmpty());
        QVERIFY(!upsertProfile(list, {QStringLiteral(""), QStringLiteral("1.2.3.4"), 14566, 3}).isEmpty());
        QVERIFY(!upsertProfile(list, {QStringLiteral("t"), QStringLiteral("1.2.3.4"), 14566, 0}).isEmpty());
        QCOMPARE(list.size(), 2);

        QTemporaryDir dir;
        QSettings s(dir.filePath(QStringLiteral("p.ini")), QSettings::IniFormat);
        saveProfiles(s, list);
        QCOMPARE(loadProfiles(s).size(), 2);
        QCOMPARE(loadProfiles(s)[1].port, 14550);
        QCOMPARE(loadProfiles(s)[1].model, QStringLiteral("MB1"));
        QCOMPARE(loadProfiles(s)[0].model, QStringLiteral("VIO"));  // the default

        // A tampered entry is dropped on load, the others survive
        s.beginWriteArray(QStringLiteral("profiles"), 3);
        s.setArrayIndex(2);
        s.setValue(QStringLiteral("name"), QStringLiteral("evil"));
        s.setValue(QStringLiteral("ip"), QStringLiteral("255.255.255.255"));
        s.setValue(QStringLiteral("port"), 1);
        s.endArray();
        QCOMPARE(loadProfiles(s).size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestLinkLogic)
#include "test_link_logic.moc"
