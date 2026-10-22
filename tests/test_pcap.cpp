// Network capture: packet decoding and .pcap files written with libpcap itself (no root needed).
#include "pcap_capture.h"

#include <QTemporaryDir>
#include <QtTest>

#include <pcap/pcap.h>

using Bytes = std::vector<uint8_t>;

static Bytes heartbeat(uint8_t sys, uint8_t comp)
{
    mavlink_message_t m;
    mavlink_msg_heartbeat_pack_chan(sys, comp, MAVLINK_COMM_0, &m, MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_ARDUPILOTMEGA, 0, 0, MAV_STATE_ACTIVE);
    Bytes b(MAVLINK_MAX_PACKET_LEN);
    b.resize(mavlink_msg_to_send_buffer(b.data(), &m));
    return b;
}

// IPv4 + UDP (or another protocol) around a payload
static Bytes ipv4(quint32 src, quint32 dst, quint16 sport, quint16 dport, const Bytes &payload, uint8_t proto = 17, quint16 frag = 0)
{
    Bytes b(28);
    const size_t total = 28 + payload.size();
    b[0] = 0x45;
    b[2] = uint8_t(total >> 8); b[3] = uint8_t(total);
    b[6] = uint8_t(frag >> 8); b[7] = uint8_t(frag);
    b[8] = 64; b[9] = proto;
    for (int i = 0; i < 4; ++i) { b[12 + i] = uint8_t(src >> (24 - 8 * i)); b[16 + i] = uint8_t(dst >> (24 - 8 * i)); }
    b[20] = uint8_t(sport >> 8); b[21] = uint8_t(sport); b[22] = uint8_t(dport >> 8); b[23] = uint8_t(dport);
    const size_t ulen = 8 + payload.size();
    b[24] = uint8_t(ulen >> 8); b[25] = uint8_t(ulen);
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}

static Bytes ethernet(const Bytes &ip, bool vlan = false)
{
    Bytes b(12, 0x11);
    if (vlan) b.insert(b.end(), {0x81, 0x00, 0x00, 0x05});
    b.insert(b.end(), {0x08, 0x00});
    b.insert(b.end(), ip.begin(), ip.end());
    return b;
}

static const quint32 kAutopilot = 0xC0A8900A, kGcs = 0xC0A8900B;  // 192.168.144.10 / .11

class TestPcap : public QObject
{
    Q_OBJECT
private slots:
    void decodesPackets()
    {
        const Bytes hb = heartbeat(1, 1);
        UdpPacket u;
        const Bytes e = ethernet(ipv4(kAutopilot, kGcs, 14550, 14555, hb));
        QVERIFY(udpPayload(DLT_EN10MB, e.data(), e.size(), &u));
        QCOMPARE(u.srcIp, kAutopilot);
        QCOMPARE(u.dstPort, quint16(14555));
        QCOMPARE(u.len, hb.size());
        QVERIFY(std::equal(hb.begin(), hb.end(), u.payload));

        const Bytes v = ethernet(ipv4(kAutopilot, kGcs, 1, 2, hb), true);
        QVERIFY(udpPayload(DLT_EN10MB, v.data(), v.size(), &u));
        const Bytes raw = ipv4(kAutopilot, kGcs, 1, 2, hb);
        QVERIFY(udpPayload(DLT_RAW, raw.data(), raw.size(), &u));
        Bytes sll2(20, 0);
        sll2[0] = 0x08;  // protocol type 0x0800 at offset 0
        sll2.insert(sll2.end(), raw.begin(), raw.end());
        QVERIFY(udpPayload(DLT_LINUX_SLL2, sll2.data(), sll2.size(), &u));

        const Bytes tcp = ethernet(ipv4(kAutopilot, kGcs, 1, 2, hb, 6));
        QVERIFY(!udpPayload(DLT_EN10MB, tcp.data(), tcp.size(), &u));
        const Bytes frag = ethernet(ipv4(kAutopilot, kGcs, 1, 2, hb, 17, 0x2000));  // more fragments
        QVERIFY(!udpPayload(DLT_EN10MB, frag.data(), frag.size(), &u));
        QVERIFY(!udpPayload(DLT_EN10MB, e.data(), e.size() - 5, &u));  // truncated capture
    }

    void readsPcapFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("drone.pcap"));
        pcap_t *dead = pcap_open_dead(DLT_EN10MB, 65535);
        pcap_dumper_t *d = pcap_dump_open(dead, path.toLocal8Bit().constData());
        QVERIFY(d);
        Bytes two = heartbeat(1, 1);
        const Bytes gimbal = heartbeat(1, 154);
        two.insert(two.end(), gimbal.begin(), gimbal.end());
        const std::vector<Bytes> packets{
            ethernet(ipv4(kAutopilot, kGcs, 14550, 14555, heartbeat(1, 1))),
            ethernet(ipv4(kGcs, kAutopilot, 14555, 14550, two), true),
            ethernet(ipv4(kAutopilot, kGcs, 1, 2, two, 6)),  // TCP: skipped
        };
        long sec = 1761100000;
        for (const Bytes &p : packets) {
            pcap_pkthdr h{};
            h.ts.tv_sec = sec++;
            h.ts.tv_usec = 250000;
            h.caplen = h.len = bpf_u_int32(p.size());
            pcap_dump(reinterpret_cast<u_char *>(d), &h, p.data());
        }
        pcap_dump_close(d);
        pcap_close(dead);

        PcapCapture cap;
        std::vector<CapturedFrame> got;
        connect(&cap, &PcapCapture::framesCaptured, this, [&](const std::vector<CapturedFrame> &f) { got.insert(got.end(), f.begin(), f.end()); });
        QString err;
        // Plain "udp" would drop the VLAN-tagged packet in the kernel filter, hence the app's default
        QVERIFY2(cap.openFile(path, QStringLiteral("udp or (vlan and udp)"), &err), qPrintable(err));
        QTRY_COMPARE_WITH_TIMEOUT(cap.running(), false, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(int(got.size()), 3, 2000);
        QCOMPARE(cap.packets(), 2ull);  // TCP removed by the BPF filter before it reaches us
        QCOMPARE(got[0].dir, int(CapturedFrame::Net));
        QCOMPARE(got[0].peerIp, kAutopilot);
        QCOMPARE(got[0].dstPort, quint16(14555));
        QCOMPARE(got[0].wallUs, 1761100000LL * 1000000 + 250000);
        QCOMPARE(got[0].frame.status, MavFrame::Ok);
        QCOMPARE(int(got[2].frame.compid), 154);
        QCOMPARE(got[1].datagram, got[2].datagram);  // two frames, one datagram
        QVERIFY(got[0].index < got[1].index);

        // Without a filter the TCP packet reaches the decoder and is counted as skipped
        got.clear();
        QVERIFY(cap.openFile(path, QString(), &err));
        QTRY_COMPARE_WITH_TIMEOUT(cap.running(), false, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(int(got.size()), 3, 2000);
        QCOMPARE(cap.skipped(), 1ull);

        QVERIFY(!cap.openFile(path, QStringLiteral("this is not bpf"), &err));
        QVERIFY(err.contains(QStringLiteral("Filter")));
        QVERIFY(!cap.openFile(dir.filePath(QStringLiteral("missing.pcap")), {}, &err));
    }

    void liveErrorsAreReported()
    {
        PcapCapture cap;
        QString err;
        QVERIFY(!cap.startLive(QStringLiteral("nosuchif0"), QStringLiteral("udp"), &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!cap.running());
    }
};

QTEST_GUILESS_MAIN(TestPcap)
#include "test_pcap.moc"
