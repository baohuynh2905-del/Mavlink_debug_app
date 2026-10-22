#pragma once
// Pure link logic (no SDK, no sockets) so it can be unit tested.

#include <QList>
#include <QMap>
#include <QString>

enum class LinkState {
    Disconnected,
    Connecting,
    Disconnecting,
    Failed,
    // SDK socket open; the levels below report only what was actually observed:
    SocketOpen,  // sending heartbeats, nothing received yet
    BytesOnly,   // datagrams received, none contained a valid MAVLink frame
    Mavlink,     // valid frames received, but no recent HEARTBEAT
    Heartbeat,   // a HEARTBEAT was received within the timeout
    Stale        // traffic was seen before, but nothing within the timeout
};

// Monotonic timestamps in ms of the last observed RX events; -1 = never.
struct LinkObservations {
    qint64 lastRx = -1;
    qint64 lastValidRx = -1;
    qint64 lastHeartbeat = -1;
};

// State of an open link, judged only from observed RX traffic.
LinkState evaluateLink(const LinkObservations &o, qint64 nowMs, qint64 timeoutMs);

bool isOpen(LinkState s);
QString linkStateText(LinkState s);

// Operator status lamps: Off = not running, Ok = confirmed by received data, Warn = partial or waiting,
// Bad = lost or failed.
enum class Lamp { Off, Ok, Warn, Bad };
Lamp linkLamp(LinkState s);

// How many of compids (any sysid) sent a HEARTBEAT within the timeout. heartbeats: last HEARTBEAT (ms)
// per (sysid << 8 | compid), as PayloadLink::heartbeatSources() keeps them.
int freshComponents(const QMap<quint16, qint64> &heartbeats, const QList<quint8> &compids, qint64 nowMs, qint64 timeoutMs);
Lamp payloadLamp(bool linkOpen, int fresh, int expected);

// Empty when valid, otherwise a user-facing reason. The SDK only supports IPv4 (inet_addr).
QString validateEndpoint(const QString &ip, int port);
