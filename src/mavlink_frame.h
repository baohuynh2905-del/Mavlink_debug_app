#pragma once
// MAVLink frame splitting/validation and generic field decoding.
// Qt-free so it can be unit tested without a GUI or hardware.

#include <cstdint>
#include <string>
#include <vector>

#include <ardupilotmega/mavlink.h>

struct MavFrame {
    enum Status {
        Ok,         // CRC verified against the dialect's CRC_EXTRA
        BadCrc,     // known message id, CRC mismatch
        UnknownId,  // message id not in the dialect: CRC cannot be verified, payload undecoded
        Truncated,  // header found but the datagram ended before the frame did
        Garbage     // bytes that are not part of any frame
    };

    Status status = Garbage;
    std::vector<uint8_t> raw;  // exact bytes as seen on the wire

    // Header fields: valid for Ok/BadCrc/UnknownId, and for Truncated when the header was complete.
    bool headerComplete = false;
    int version = 0;           // 1 or 2
    uint8_t len = 0, incompatFlags = 0, compatFlags = 0, seq = 0, sysid = 0, compid = 0;
    uint32_t msgid = 0;
    bool signaturePresent = false;  // flag only: signatures are NOT verified (no key)
    uint16_t crcWire = 0, crcCalc = 0;
    int targetSystem = -1, targetComponent = -1;  // -1 = message has no such field

    mavlink_message_t msg{};   // Ok only: payload zero-extended to the dialect length
};

// Split one UDP datagram into frames. Each datagram is parsed independently, so a
// broken datagram can never corrupt the next one.
std::vector<MavFrame> parseDatagram(const uint8_t *data, size_t len);

struct MavField {
    std::string name;
    std::string type;   // e.g. "uint16_t", "float[4]"
    std::string value;  // display text
    bool extension;     // MAVLink 2 extension field (absent on the wire reads as 0)
    bool isText = false;        // char[]: value holds the string
    bool isReal = false;        // float/double: see reals, otherwise see ints
    std::vector<int64_t> ints;  // shortcut: uint64 above INT64_MAX wraps; no MAVLink enum/bitmask goes that high
    std::vector<double> reals;
};

// Decoded fields in XML order. Empty unless frame.status == Ok.
std::vector<MavField> decodeFields(const MavFrame &frame);

// Dialect message name, or nullptr when the id is unknown.
const char *messageName(uint32_t msgid);

const char *statusName(MavFrame::Status s);
