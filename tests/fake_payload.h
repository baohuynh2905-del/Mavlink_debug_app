#pragma once
// Loopback stand-in for a payload: a plain UDP socket the SDK connects to. Test-only.

#include "mavlink_frame.h"

#include <QByteArray>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

class FakePayload
{
public:
    FakePayload()
    {
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t l = sizeof a;
        bind(fd, (sockaddr *)&a, sizeof a);
        getsockname(fd, (sockaddr *)&a, &l);
        port = ntohs(a.sin_port);
        timeval tv{3, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }
    ~FakePayload() { close(fd); }

    // Waits for the app's first datagram (its heartbeat) to learn the app's ephemeral endpoint.
    bool learnAppEndpoint()
    {
        uint8_t b[512];
        socklen_t l = sizeof app;
        return recvfrom(fd, b, sizeof b, 0, (sockaddr *)&app, &l) > 0;
    }
    void send(const QByteArray &d) { sendto(fd, d.constData(), size_t(d.size()), 0, (sockaddr *)&app, sizeof app); }
    void send(const mavlink_message_t &m)
    {
        uint8_t b[MAVLINK_MAX_PACKET_LEN];
        send(QByteArray((const char *)b, mavlink_msg_to_send_buffer(b, &m)));
    }
    void sendHeartbeat(uint8_t comp, uint8_t type)
    {
        mavlink_message_t m;
        mavlink_msg_heartbeat_pack_chan(1, comp, MAVLINK_COMM_0, &m, type, MAV_AUTOPILOT_INVALID, 0, 0, MAV_STATE_ACTIVE);
        send(m);
    }

    // Every valid frame received since the last call, without blocking.
    std::vector<MavFrame> received()
    {
        std::vector<MavFrame> out;
        uint8_t b[2048];
        ssize_t n;
        while ((n = recv(fd, b, sizeof b, MSG_DONTWAIT)) > 0)
            for (MavFrame &f : parseDatagram(b, size_t(n)))
                if (f.status == MavFrame::Ok) out.push_back(std::move(f));
        return out;
    }

    int fd;
    int port;
    sockaddr_in app{};
};
