#include "link_health.h"

#include <arpa/inet.h>

LinkState evaluateLink(const LinkObservations &o, qint64 nowMs, qint64 timeoutMs)
{
    if (o.lastRx < 0) return LinkState::SocketOpen;
    if (nowMs - o.lastRx > timeoutMs) return LinkState::Stale;
    if (o.lastHeartbeat >= 0 && nowMs - o.lastHeartbeat <= timeoutMs) return LinkState::Heartbeat;
    if (o.lastValidRx >= 0 && nowMs - o.lastValidRx <= timeoutMs) return LinkState::Mavlink;
    return LinkState::BytesOnly;
}

bool isOpen(LinkState s)
{
    return s == LinkState::SocketOpen || s == LinkState::BytesOnly || s == LinkState::Mavlink ||
           s == LinkState::Heartbeat || s == LinkState::Stale;
}

QString linkStateText(LinkState s)
{
    switch (s) {
    case LinkState::Disconnected: return QStringLiteral("Disconnected");
    case LinkState::Connecting: return QStringLiteral("Connecting");
    case LinkState::Disconnecting: return QStringLiteral("Disconnecting");
    case LinkState::Failed: return QStringLiteral("Failed");
    case LinkState::SocketOpen: return QStringLiteral("Socket open, nothing received");
    case LinkState::BytesOnly: return QStringLiteral("Receiving data, no valid MAVLink");
    case LinkState::Mavlink: return QStringLiteral("MAVLink traffic, no heartbeat");
    case LinkState::Heartbeat: return QStringLiteral("Heartbeat received");
    case LinkState::Stale: return QStringLiteral("Stale: no data within timeout");
    }
    return {};
}

Lamp linkLamp(LinkState s)
{
    switch (s) {
    case LinkState::Heartbeat: return Lamp::Ok;
    case LinkState::Connecting:
    case LinkState::Disconnecting:
    case LinkState::SocketOpen:
    case LinkState::BytesOnly:
    case LinkState::Mavlink: return Lamp::Warn;
    case LinkState::Stale:
    case LinkState::Failed: return Lamp::Bad;
    case LinkState::Disconnected: return Lamp::Off;
    }
    return Lamp::Off;
}

int freshComponents(const QMap<quint16, qint64> &heartbeats, const QList<quint8> &compids, qint64 nowMs, qint64 timeoutMs)
{
    int n = 0;
    for (quint8 comp : compids) {
        for (auto it = heartbeats.cbegin(); it != heartbeats.cend(); ++it) {
            if ((it.key() & 0xFF) == comp && nowMs - it.value() <= timeoutMs) {
                ++n;
                break;
            }
        }
    }
    return n;
}

Lamp payloadLamp(bool linkOpen, int fresh, int expected)
{
    if (!linkOpen) return Lamp::Off;
    if (fresh >= expected) return Lamp::Ok;
    return fresh > 0 ? Lamp::Warn : Lamp::Bad;
}

QString validateEndpoint(const QString &ip, int port)
{
    in_addr a{};
    // inet_pton is stricter than the SDK's inet_addr, so anything accepted here parses identically there
    if (inet_pton(AF_INET, ip.trimmed().toLatin1().constData(), &a) != 1)
        return QStringLiteral("\"%1\" is not an IPv4 address (a.b.c.d)").arg(ip);
    if (a.s_addr == htonl(INADDR_ANY) || a.s_addr == htonl(INADDR_BROADCAST))
        return QStringLiteral("%1 cannot be used as a payload address").arg(ip);
    if (port < 1 || port > 65535)
        return QStringLiteral("Port %1 is outside 1-65535").arg(port);
    return {};
}
