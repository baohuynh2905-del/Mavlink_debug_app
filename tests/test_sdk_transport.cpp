// Integration test of the patched SDK transport over loopback: a local socket plays the payload.
// Verifies the observer sees real TX/RX traffic and that sdkQuit() cannot hang on a silent peer.
#include "payloadSdkInterface.h"
#include "mavlink_frame.h"

#include <atomic>
#include <chrono>
#include <thread>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

using Clock = std::chrono::steady_clock;

int main()
{
    int fake = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in fakeAddr{};
    fakeAddr.sin_family = AF_INET;
    fakeAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t al = sizeof fakeAddr;
    CHECK(bind(fake, (sockaddr *)&fakeAddr, sizeof fakeAddr) == 0);
    getsockname(fake, (sockaddr *)&fakeAddr, &al);
    timeval tv{3, 0};
    setsockopt(fake, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    const int fakePort = ntohs(fakeAddr.sin_port);

    for (int round = 0; round < 2; ++round) {  // second round = reconnect after disconnect
        std::atomic<int> tx{0}, rx{0}, rxPeerPort{0};
        T_ConnInfo ci{};
        ci.type = CONTROL_UDP;
        ci.device.udp.ip = (char *)"127.0.0.1";
        ci.device.udp.port = fakePort;

        auto *sdk = new PayloadSdkInterface(ci);
        sdk->regTransportObserver([&](int dir, const uint8_t *, int, const sockaddr_in *peer, int err) {
            if (dir == 1 && err == 0) ++tx;
            if (dir == 0) { ++rx; rxPeerPort = ntohs(peer->sin_port); }
        });
        CHECK(sdk->sdkInitConnection());

        // The SDK heartbeats about once per second; the fake payload must receive it.
        uint8_t buf[512];
        sockaddr_in app{};
        socklen_t appLen = sizeof app;
        int n = recvfrom(fake, buf, sizeof buf, 0, (sockaddr *)&app, &appLen);
        CHECK(n > 0);
        if (n > 0) {
            auto frames = parseDatagram(buf, size_t(n));
            CHECK(!frames.empty() && frames[0].status == MavFrame::Ok && frames[0].msgid == MAVLINK_MSG_ID_HEARTBEAT);
        }
        CHECK(tx > 0);

        // Reply as the gimbal; the observer must see it with the payload's endpoint.
        mavlink_message_t m;
        mavlink_msg_heartbeat_pack_chan(1, MAV_COMP_ID_GIMBAL, MAVLINK_COMM_0, &m, MAV_TYPE_GIMBAL, MAV_AUTOPILOT_INVALID, 0, 0, MAV_STATE_ACTIVE);
        uint8_t out[MAVLINK_MAX_PACKET_LEN];
        sendto(fake, out, mavlink_msg_to_send_buffer(out, &m), 0, (sockaddr *)&app, appLen);
        for (int i = 0; i < 100 && rx == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(rx > 0);
        CHECK(rxPeerPort == fakePort);

        // The peer is silent now, so the SDK read thread sits in recvfrom(): quit must still return.
        auto t0 = Clock::now();
        sdk->sdkQuit();
        sdk->sdkQuit();  // idempotent
        delete sdk;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
        printf("round %d: tx=%d rx=%d quit=%lld ms\n", round, tx.load(), rx.load(), (long long)ms);
        CHECK(ms < 2000);
    }
    close(fake);

    printf(failures ? "%d check(s) failed\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
