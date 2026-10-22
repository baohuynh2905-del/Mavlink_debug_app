#include "pcap_capture.h"

#include "payload_link.h"

#include <pcap/pcap.h>

#include <chrono>

static quint16 be16(const uint8_t *p) { return quint16(p[0] << 8 | p[1]); }
static quint32 be32(const uint8_t *p) { return quint32(p[0]) << 24 | quint32(p[1]) << 16 | quint32(p[2]) << 8 | p[3]; }

bool udpPayload(int linkType, const uint8_t *p, size_t n, UdpPacket *out)
{
    size_t off = 0;
    quint16 etherType = 0x0800;
    switch (linkType) {
    case DLT_EN10MB:
        if (n < 14) return false;
        etherType = be16(p + 12);
        off = 14;
        while (etherType == 0x8100 || etherType == 0x88A8) {  // VLAN tags
            if (n < off + 4) return false;
            etherType = be16(p + off + 2);
            off += 4;
        }
        break;
    case DLT_LINUX_SLL:
        if (n < 16) return false;
        etherType = be16(p + 14);
        off = 16;
        break;
    case DLT_LINUX_SLL2:
        if (n < 20) return false;
        etherType = be16(p);
        off = 20;
        break;
    case DLT_RAW:
#ifdef DLT_IPV4
    case DLT_IPV4:
#endif
        break;
    default:
        return false;
    }
    if (etherType != 0x0800 || n < off + 20) return false;
    const uint8_t *ip = p + off;
    const size_t ihl = size_t(ip[0] & 0x0F) * 4;
    if ((ip[0] >> 4) != 4 || ihl < 20 || ip[9] != 17) return false;  // IPv4, UDP
    if (be16(ip + 6) & 0x3FFF) return false;                           // fragment (MF set or offset > 0)
    const size_t ipLen = be16(ip + 2);
    if (ipLen < ihl + 8 || n < off + ihl + 8) return false;
    const uint8_t *udp = ip + ihl;
    const size_t udpLen = be16(udp + 4);
    if (udpLen < 8 || n < off + ihl + udpLen || ihl + udpLen > ipLen) return false;  // truncated capture
    out->srcIp = be32(ip + 12);
    out->dstIp = be32(ip + 16);
    out->srcPort = be16(udp);
    out->dstPort = be16(udp + 2);
    out->payload = udp + 8;
    out->len = udpLen - 8;
    return true;
}

PcapCapture::PcapCapture(QObject *parent) : QObject(parent)
{
    m_poll.setInterval(100);
    connect(&m_poll, &QTimer::timeout, this, &PcapCapture::poll);
}

PcapCapture::~PcapCapture()
{
    blockSignals(true);  // receivers may already be half destroyed (we are often their child)
    stop();
}

QStringList PcapCapture::interfaces(QString *error)
{
    QStringList out;
    char err[PCAP_ERRBUF_SIZE] = {0};
    pcap_if_t *all = nullptr;
    if (pcap_findalldevs(&all, err) != 0) {
        if (error) *error = QString::fromLocal8Bit(err);
        return out;
    }
    for (pcap_if_t *d = all; d; d = d->next) out << QString::fromLocal8Bit(d->name);
    pcap_freealldevs(all);
    return out;
}

bool PcapCapture::startLive(const QString &iface, const QString &filter, QString *error)
{
    stop();
    char err[PCAP_ERRBUF_SIZE] = {0};
    pcap_t *p = pcap_create(iface.toLocal8Bit().constData(), err);
    if (!p) {
        *error = QString::fromLocal8Bit(err);
        return false;
    }
    pcap_set_snaplen(p, 65535);
    pcap_set_promisc(p, 1);  // other devices' unicast traffic only arrives if the network delivers it here
    pcap_set_timeout(p, 100);
    if (const int rc = pcap_activate(p); rc < 0) {
        *error = QStringLiteral("%1: %2%3").arg(iface, QString::fromLocal8Bit(pcap_geterr(p)),
                                               rc == PCAP_ERROR_PERM_DENIED ? QStringLiteral(" (needs root or CAP_NET_RAW, see README)") : QString());
        pcap_close(p);
        return false;
    }
    return begin(p, iface, true, filter, error);
}

bool PcapCapture::openFile(const QString &path, const QString &filter, QString *error)
{
    stop();
    char err[PCAP_ERRBUF_SIZE] = {0};
    pcap_t *p = pcap_open_offline(path.toLocal8Bit().constData(), err);
    if (!p) {
        *error = QString::fromLocal8Bit(err);
        return false;
    }
    return begin(p, path, false, filter, error);
}

bool PcapCapture::begin(void *handle, const QString &source, bool live, const QString &filter, QString *error)
{
    auto *p = static_cast<pcap_t *>(handle);
    if (!filter.trimmed().isEmpty()) {
        bpf_program prog;
        if (pcap_compile(p, &prog, filter.toLatin1().constData(), 1, PCAP_NETMASK_UNKNOWN) != 0 || pcap_setfilter(p, &prog) != 0) {
            *error = QStringLiteral("Filter \"%1\": %2").arg(filter, QString::fromLocal8Bit(pcap_geterr(p)));
            pcap_close(p);
            return false;
        }
        pcap_freecode(&prog);
    }
    m_source = source;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_threadError.clear();
    }
    m_packets = m_skipped = m_dropped = 0;
    m_stop = false;
    m_done = false;
    m_thread = std::thread([this, p, live] { run(p, live); });
    m_poll.start();
    emit stateChanged();
    return true;
}

void PcapCapture::run(void *handle, bool live)
{
    auto *p = static_cast<pcap_t *>(handle);
    const int linkType = pcap_datalink(p);
    while (!m_stop) {
        pcap_pkthdr *h = nullptr;
        const u_char *data = nullptr;
        const int rc = pcap_next_ex(p, &h, &data);
        if (rc == 0) continue;  // live read timeout: check m_stop
        if (rc < 0) {
            if (rc != PCAP_ERROR_BREAK && live) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_threadError = QString::fromLocal8Bit(pcap_geterr(p));
            }
            break;  // end of file, or error
        }
        UdpPacket u;
        if (!udpPayload(linkType, data, h->caplen, &u)) {
            ++m_skipped;
            continue;
        }
        Packet pk{qint64(h->ts.tv_sec) * 1000000 + h->ts.tv_usec, PayloadLink::monoNs(), u,
                  QByteArray(reinterpret_cast<const char *>(u.payload), int(u.len))};
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pending.size() >= PayloadLink::kMaxPendingDatagrams) {
            if (live) {
                ++m_dropped;
                continue;
            }
            // Files are read faster than the GUI parses them: wait instead of dropping.
            m_mutex.unlock();
            while (!m_stop) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                std::lock_guard<std::mutex> l2(m_mutex);
                if (m_pending.size() < PayloadLink::kMaxPendingDatagrams / 2) break;
            }
            m_mutex.lock();
        }
        ++m_packets;
        m_pending.push_back(std::move(pk));
    }
    pcap_close(p);
    m_done = true;
}

void PcapCapture::stop()
{
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
    poll();  // deliver what was read
    m_poll.stop();
    emit stateChanged();
}

QString PcapCapture::lastError() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_threadError;
}

void PcapCapture::poll()
{
    std::vector<Packet> batch;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        batch.swap(m_pending);
    }
    std::vector<CapturedFrame> out;
    for (Packet &pk : batch) {
        const quint64 dg = m_datagram++;
        for (MavFrame &f : parseDatagram(reinterpret_cast<const uint8_t *>(pk.data.constData()), size_t(pk.data.size()))) {
            CapturedFrame c;
            c.index = CapturedFrame::nextIndex();
            c.datagram = dg;
            c.monoNs = pk.monoNs;
            c.wallUs = pk.wallUs;
            c.dir = CapturedFrame::Net;
            c.peerIp = pk.udp.srcIp;
            c.peerPort = pk.udp.srcPort;
            c.dstIp = pk.udp.dstIp;
            c.dstPort = pk.udp.dstPort;
            c.frame = std::move(f);
            out.push_back(std::move(c));
        }
    }
    if (!out.empty()) emit framesCaptured(out);
    if (m_thread.joinable() && m_done) {  // file finished or live error: reap the thread
        m_thread.join();
        m_poll.stop();
        emit stateChanged();
    }
}
