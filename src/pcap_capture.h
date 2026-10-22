#pragma once
// Network capture (libpcap) of MAVLink traffic that is not this app's own link, e.g. payload <->
// autopilot, live from an interface (needs root or CAP_NET_RAW) or from a .pcap file.
// Frames are marked CapturedFrame::Net and never feed the link state, telemetry or command tracking.

#include "captured_frame.h"

#include <QObject>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <mutex>
#include <thread>

struct UdpPacket {
    quint32 srcIp = 0, dstIp = 0;  // host order
    quint16 srcPort = 0, dstPort = 0;
    const uint8_t *payload = nullptr;
    size_t len = 0;
};

// IPv4/UDP payload of one captured packet. Supports Ethernet (with 802.1Q), Linux "any" (SLL, SLL2)
// and raw IP link types. Fragments, non-UDP and truncated packets return false.
bool udpPayload(int linkType, const uint8_t *pkt, size_t caplen, UdpPacket *out);

class PcapCapture : public QObject
{
    Q_OBJECT
public:
    explicit PcapCapture(QObject *parent = nullptr);
    ~PcapCapture() override;

    static QStringList interfaces(QString *error = nullptr);
    bool startLive(const QString &iface, const QString &bpfFilter, QString *error);
    bool openFile(const QString &path, const QString &bpfFilter, QString *error);
    void stop();

    bool running() const { return m_thread.joinable() && !m_done; }
    QString source() const { return m_source; }
    QString lastError() const;  // set by the capture thread (e.g. interface went down)
    quint64 packets() const { return m_packets; }      // UDP packets passed to the parser
    quint64 skipped() const { return m_skipped; }      // non-IPv4/UDP, fragments, truncated
    quint64 dropped() const { return m_dropped; }      // queue overflow

signals:
    void framesCaptured(const std::vector<CapturedFrame> &frames);  // GUI thread
    void stateChanged();

private:
    bool begin(void *pcap, const QString &source, bool live, const QString &filter, QString *error);
    void run(void *pcap, bool live);
    void poll();

    struct Packet {
        qint64 wallUs, monoNs;
        UdpPacket udp;
        QByteArray data;
    };

    std::thread m_thread;
    std::atomic<bool> m_stop{false}, m_done{true};
    std::atomic<quint64> m_packets{0}, m_skipped{0}, m_dropped{0};
    mutable std::mutex m_mutex;
    std::vector<Packet> m_pending;
    QString m_threadError;
    QString m_source;
    quint64 m_datagram = 0;
    QTimer m_poll;
};
