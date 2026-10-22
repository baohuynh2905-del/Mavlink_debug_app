#pragma once
// Single owner of the PayloadSDK connection, and the bridge from the SDK's I/O threads
// (transport observer) to the GUI thread.

#include "captured_frame.h"
#include "link_health.h"

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QTimer>

#include <atomic>
#include <mutex>
#include <vector>

class MavlinkDict;
class PayloadSdkInterface;
class QThread;
struct sockaddr_in;

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

signals:
    void stateChanged();
    void framesCaptured(const std::vector<CapturedFrame> &frames);  // GUI thread, in observation order

private:
    void onTransport(int dir, const uint8_t *data, int len, const sockaddr_in *peer, int sysErrno);
    void poll();
    void setState(LinkState s, const QString &error = {});
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
    quint64 m_nextFrameIndex = 0;
    QTimer m_pollTimer;
};
