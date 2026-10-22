#include "payload_link.h"

#include "frame_text.h"

// The only translation unit that includes the SDK header (it defines globals and many macros).
#include "payloadSdkInterface.h"

#include <QThread>

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
            c.index = m_nextFrameIndex++;
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
    if (!out.empty()) emit framesCaptured(out);

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
