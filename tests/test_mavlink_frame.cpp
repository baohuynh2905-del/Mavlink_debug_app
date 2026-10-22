// Hardware-free tests for the frame parser. Frames are built with the SDK's own pack functions.
#include "mavlink_frame.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

using Bytes = std::vector<uint8_t>;

static Bytes wire(const mavlink_message_t &m)
{
    Bytes b(MAVLINK_MAX_PACKET_LEN);
    b.resize(mavlink_msg_to_send_buffer(b.data(), &m));
    return b;
}

static Bytes heartbeatV2(uint8_t sys = 1, uint8_t comp = 154)
{
    mavlink_message_t m;
    mavlink_msg_heartbeat_pack_chan(sys, comp, MAVLINK_COMM_0, &m, MAV_TYPE_GIMBAL, MAV_AUTOPILOT_INVALID, 0, 0, MAV_STATE_ACTIVE);
    return wire(m);
}

static std::vector<MavFrame> parse(const Bytes &b) { return parseDatagram(b.data(), b.size()); }

static std::string field(const MavFrame &f, const char *name)
{
    for (auto &x : decodeFields(f)) if (x.name == name) return x.value;
    return "<missing>";
}

int main()
{
    {   // single v2 frame, decoded header and fields
        auto fr = parse(heartbeatV2(1, 154));
        CHECK(fr.size() == 1);
        CHECK(fr[0].status == MavFrame::Ok);
        CHECK(fr[0].version == 2 && fr[0].sysid == 1 && fr[0].compid == 154 && fr[0].msgid == 0);
        CHECK(std::string(messageName(fr[0].msgid)) == "HEARTBEAT");
        CHECK(field(fr[0], "type") == std::to_string(MAV_TYPE_GIMBAL));
        CHECK(fr[0].targetSystem == -1);
    }
    {   // extended msgid (284), arrays, NaN, target fields
        mavlink_message_t m;
        float q[4] = {1, 0, 0, 0};
        mavlink_msg_gimbal_device_set_attitude_pack_chan(1, 191, MAVLINK_COMM_0, &m, 1, 154, GIMBAL_DEVICE_FLAGS_YAW_LOCK, q, NAN, NAN, 0.5f);
        auto fr = parse(wire(m));
        CHECK(fr.size() == 1 && fr[0].status == MavFrame::Ok);
        CHECK(fr[0].msgid == 284 && fr[0].targetSystem == 1 && fr[0].targetComponent == 154);
        CHECK(field(fr[0], "q") == "[1, 0, 0, 0]");
        CHECK(field(fr[0], "angular_velocity_x") == "nan");
        CHECK(field(fr[0], "angular_velocity_z") == "0.5");
    }
    {   // garbage prefix + two frames + garbage suffix in one datagram
        Bytes d = {'x', 'y', 'z'};
        Bytes a = heartbeatV2(1, 100), b = heartbeatV2(1, 154);
        d.insert(d.end(), a.begin(), a.end());
        d.insert(d.end(), b.begin(), b.end());
        d.push_back(0x00);
        auto fr = parse(d);
        CHECK(fr.size() == 4);
        CHECK(fr[0].status == MavFrame::Garbage && fr[0].raw.size() == 3);
        CHECK(fr[1].status == MavFrame::Ok && fr[1].compid == 100 && fr[1].raw == a);
        CHECK(fr[2].status == MavFrame::Ok && fr[2].compid == 154 && fr[2].raw == b);
        CHECK(fr[3].status == MavFrame::Garbage && fr[3].raw.size() == 1);
    }
    {   // bad CRC is reported, consumed whole, and the next frame still parses
        Bytes a = heartbeatV2(), b = heartbeatV2(1, 100);
        a[12] ^= 0xFF;
        Bytes d = a;
        d.insert(d.end(), b.begin(), b.end());
        auto fr = parse(d);
        CHECK(fr.size() == 2);
        CHECK(fr[0].status == MavFrame::BadCrc && fr[0].crcWire != fr[0].crcCalc);
        CHECK(decodeFields(fr[0]).empty());
        CHECK(fr[1].status == MavFrame::Ok);
    }
    {   // unknown message id: header kept, payload undecoded
        Bytes d = {MAVLINK_STX, 2, 0, 0, 7, 1, 25, 0xDE, 0xBC, 0x0A, 0x11, 0x22, 0x33, 0x44};
        auto fr = parse(d);
        CHECK(fr.size() == 1 && fr[0].status == MavFrame::UnknownId);
        CHECK(fr[0].msgid == 0x0ABCDE && fr[0].seq == 7 && fr[0].compid == 25 && fr[0].raw == d);
        CHECK(messageName(0x0ABCDE) == nullptr);
        CHECK(decodeFields(fr[0]).empty());
    }
    {   // truncated frame and truncated header
        Bytes a = heartbeatV2();
        a.resize(a.size() - 3);
        auto fr = parse(a);
        CHECK(fr.size() == 1 && fr[0].status == MavFrame::Truncated && fr[0].headerComplete && fr[0].raw == a);
        Bytes h = {MAVLINK_STX, 9, 0};
        fr = parse(h);
        CHECK(fr.size() == 1 && fr[0].status == MavFrame::Truncated && !fr[0].headerComplete);
    }
    {   // MAVLink 1 frame
        mavlink_get_channel_status(MAVLINK_COMM_2)->flags |= MAVLINK_STATUS_FLAG_OUT_MAVLINK1;
        mavlink_message_t m;
        mavlink_msg_heartbeat_pack_chan(3, 1, MAVLINK_COMM_2, &m, MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_ARDUPILOTMEGA, 0, 0, MAV_STATE_STANDBY);
        Bytes b = wire(m);
        CHECK(b[0] == MAVLINK_STX_MAVLINK1);
        auto fr = parse(b);
        CHECK(fr.size() == 1 && fr[0].status == MavFrame::Ok && fr[0].version == 1 && fr[0].sysid == 3);
        CHECK(field(fr[0], "autopilot") == std::to_string(MAV_AUTOPILOT_ARDUPILOTMEGA));
    }
    {   // signed frame: signature flagged (not verified); a signed bad-CRC frame stays BAD_CRC
        Bytes b = heartbeatV2();
        b[2] |= MAVLINK_IFLAG_SIGNED;
        size_t crcAt = b.size() - 2;
        uint16_t c = crc_calculate(&b[1], uint16_t(crcAt - 1));
        crc_accumulate(mavlink_get_msg_entry(0)->crc_extra, &c);
        b[crcAt] = c & 0xFF;
        b[crcAt + 1] = c >> 8;
        b.insert(b.end(), MAVLINK_SIGNATURE_BLOCK_LEN, 0xAB);
        auto fr = parse(b);
        CHECK(fr.size() == 1 && fr[0].status == MavFrame::Ok && fr[0].signaturePresent && fr[0].raw.size() == b.size());
        b[crcAt] ^= 1;
        fr = parse(b);
        CHECK(fr.size() == 1 && fr[0].status == MavFrame::BadCrc);
    }
    {   // unknown incompat flag: not framable, reported as garbage
        Bytes b = heartbeatV2();
        b[2] = 0x80;
        auto fr = parse(b);
        CHECK(!fr.empty() && fr[0].status == MavFrame::Garbage);
    }
    {   // v2 trailing-zero truncation and extension fields
        mavlink_message_t m;
        mavlink_msg_command_long_pack_chan(1, 191, MAVLINK_COMM_0, &m, 1, 0, MAV_CMD_DO_DIGICAM_CONTROL, 0, 1, 0, 0, 0, 0, 0, 0);
        auto fr = parse(wire(m));
        CHECK(fr.size() == 1 && fr[0].status == MavFrame::Ok);
        CHECK(fr[0].len < MAVLINK_MSG_ID_COMMAND_LONG_LEN);
        CHECK(field(fr[0], "confirmation") == "0" && fr[0].targetComponent == 0);

        mavlink_msg_command_ack_pack_chan(1, 100, MAVLINK_COMM_0, &m, MAV_CMD_DO_DIGICAM_CONTROL, MAV_RESULT_ACCEPTED, 0, 0, 1, 191);
        fr = parse(wire(m));
        CHECK(fr.size() == 1 && fr[0].status == MavFrame::Ok && fr[0].targetComponent == 191);
        bool extFlagged = false;
        for (auto &f : decodeFields(fr[0])) if (f.name == "target_component") extFlagged = f.extension;
        CHECK(extFlagged);
    }
    {   // empty datagram
        CHECK(parseDatagram(nullptr, 0).empty());
    }

    printf(failures ? "%d check(s) failed\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
