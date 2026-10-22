// Must precede the first mavlink include in this TU: enables the per-field metadata tables.
#define MAVLINK_USE_MESSAGE_INFO
#include "mavlink_frame.h"

#include <cstdio>
#include <cstring>

// Own framer instead of mavlink_frame_char_buffer(): we need byte offsets for the raw view,
// must keep non-frame bytes as diagnostics, must tell "unknown id" from "bad CRC", and that
// helper reports signed bad-CRC frames as OK. CRC/CRC_EXTRA/field tables are still the SDK's.

static const size_t kV1Header = 1 + MAVLINK_CORE_HEADER_MAVLINK1_LEN;  // STX + 5
static const size_t kV2Header = 1 + MAVLINK_CORE_HEADER_LEN;           // STX + 9

static const mavlink_field_info_t *findField(const mavlink_message_info_t *info, const char *name)
{
    for (unsigned i = 0; i < info->num_fields; ++i)
        if (strcmp(info->fields[i].name, name) == 0)
            return &info->fields[i];
    return nullptr;
}

static void buildFrame(MavFrame &f, const uint8_t *p, size_t hdr)
{
    const uint8_t *payload = p + hdr;
    const uint8_t *crc = payload + f.len;
    f.crcWire = uint16_t(crc[0] | (crc[1] << 8));

    const mavlink_msg_entry_t *e = mavlink_get_msg_entry(f.msgid);
    if (!e) {
        f.status = MavFrame::UnknownId;
        return;
    }
    uint16_t c = crc_calculate(p + 1, uint16_t(hdr - 1 + f.len));
    crc_accumulate(e->crc_extra, &c);
    f.crcCalc = c;
    if (c != f.crcWire) {
        f.status = MavFrame::BadCrc;
        return;
    }

    f.status = MavFrame::Ok;
    mavlink_message_t &m = f.msg;  // zero-initialised, so a v2-truncated payload is zero-extended
    m.magic = p[0];
    m.len = f.len;
    m.incompat_flags = f.incompatFlags;
    m.compat_flags = f.compatFlags;
    m.seq = f.seq;
    m.sysid = f.sysid;
    m.compid = f.compid;
    m.msgid = f.msgid;
    m.checksum = f.crcWire;
    memcpy(_MAV_PAYLOAD_NON_CONST(&m), payload, f.len);
    if (f.signaturePresent)
        memcpy(m.signature, crc + 2, MAVLINK_SIGNATURE_BLOCK_LEN);

    if (const mavlink_message_info_t *info = mavlink_get_message_info(&m)) {
        const uint8_t *pl = (const uint8_t *)_MAV_PAYLOAD(&m);
        if (const mavlink_field_info_t *ts = findField(info, "target_system"))
            f.targetSystem = pl[ts->wire_offset];
        if (const mavlink_field_info_t *tc = findField(info, "target_component"))
            f.targetComponent = pl[tc->wire_offset];
    }
}

std::vector<MavFrame> parseDatagram(const uint8_t *d, size_t n)
{
    std::vector<MavFrame> out;
    const size_t none = size_t(-1);
    size_t garbageStart = none;

    auto flushGarbage = [&](size_t end) {
        if (garbageStart == none) return;
        MavFrame g;
        g.status = MavFrame::Garbage;
        g.raw.assign(d + garbageStart, d + end);
        out.push_back(std::move(g));
        garbageStart = none;
    };

    size_t i = 0;
    while (i < n) {
        const uint8_t stx = d[i];
        const bool v2 = stx == MAVLINK_STX;
        if (!v2 && stx != MAVLINK_STX_MAVLINK1) {
            if (garbageStart == none) garbageStart = i;
            ++i;
            continue;
        }
        const size_t hdr = v2 ? kV2Header : kV1Header;
        // A v2 header with unknown incompat flags cannot be framed (same rule as the SDK parser).
        if (v2 && n - i >= 3 && (d[i + 2] & ~MAVLINK_IFLAG_MASK)) {
            if (garbageStart == none) garbageStart = i;
            ++i;
            continue;
        }

        MavFrame f;
        f.version = v2 ? 2 : 1;
        if (n - i < hdr) {
            flushGarbage(i);
            f.status = MavFrame::Truncated;
            f.raw.assign(d + i, d + n);
            out.push_back(std::move(f));
            return out;
        }

        const uint8_t *p = d + i;
        f.headerComplete = true;
        f.len = p[1];
        if (v2) {
            f.incompatFlags = p[2];
            f.compatFlags = p[3];
            f.seq = p[4];
            f.sysid = p[5];
            f.compid = p[6];
            f.msgid = uint32_t(p[7]) | (uint32_t(p[8]) << 8) | (uint32_t(p[9]) << 16);
            f.signaturePresent = f.incompatFlags & MAVLINK_IFLAG_SIGNED;
        } else {
            f.seq = p[2];
            f.sysid = p[3];
            f.compid = p[4];
            f.msgid = p[5];
        }
        const size_t total = hdr + f.len + MAVLINK_NUM_CHECKSUM_BYTES +
                             (f.signaturePresent ? MAVLINK_SIGNATURE_BLOCK_LEN : 0);
        flushGarbage(i);
        if (n - i < total) {
            f.status = MavFrame::Truncated;
            f.raw.assign(p, d + n);
            out.push_back(std::move(f));
            return out;
        }
        f.raw.assign(p, p + total);
        buildFrame(f, p, hdr);
        out.push_back(std::move(f));
        i += total;  // a bad-CRC frame is consumed whole, as the SDK's own parser does
    }
    flushGarbage(n);
    return out;
}

static const char *typeName(mavlink_message_type_t t)
{
    switch (t) {
    case MAVLINK_TYPE_CHAR: return "char";
    case MAVLINK_TYPE_UINT8_T: return "uint8_t";
    case MAVLINK_TYPE_INT8_T: return "int8_t";
    case MAVLINK_TYPE_UINT16_T: return "uint16_t";
    case MAVLINK_TYPE_INT16_T: return "int16_t";
    case MAVLINK_TYPE_UINT32_T: return "uint32_t";
    case MAVLINK_TYPE_INT32_T: return "int32_t";
    case MAVLINK_TYPE_UINT64_T: return "uint64_t";
    case MAVLINK_TYPE_INT64_T: return "int64_t";
    case MAVLINK_TYPE_FLOAT: return "float";
    case MAVLINK_TYPE_DOUBLE: return "double";
    }
    return "?";
}

static size_t typeSize(mavlink_message_type_t t)
{
    switch (t) {
    case MAVLINK_TYPE_UINT16_T: case MAVLINK_TYPE_INT16_T: return 2;
    case MAVLINK_TYPE_UINT32_T: case MAVLINK_TYPE_INT32_T: case MAVLINK_TYPE_FLOAT: return 4;
    case MAVLINK_TYPE_UINT64_T: case MAVLINK_TYPE_INT64_T: case MAVLINK_TYPE_DOUBLE: return 8;
    default: return 1;
    }
}

template <typename T> static T rd(const uint8_t *p) { T v; memcpy(&v, p, sizeof v); return v; }  // wire is little-endian, as are x86/arm hosts

static std::string scalar(mavlink_message_type_t t, const uint8_t *p)
{
    char b[48];
    switch (t) {
    case MAVLINK_TYPE_CHAR:
    case MAVLINK_TYPE_UINT8_T: return std::to_string(p[0]);
    case MAVLINK_TYPE_INT8_T: return std::to_string(rd<int8_t>(p));
    case MAVLINK_TYPE_UINT16_T: return std::to_string(rd<uint16_t>(p));
    case MAVLINK_TYPE_INT16_T: return std::to_string(rd<int16_t>(p));
    case MAVLINK_TYPE_UINT32_T: return std::to_string(rd<uint32_t>(p));
    case MAVLINK_TYPE_INT32_T: return std::to_string(rd<int32_t>(p));
    case MAVLINK_TYPE_UINT64_T: return std::to_string(rd<uint64_t>(p));
    case MAVLINK_TYPE_INT64_T: return std::to_string(rd<int64_t>(p));
    case MAVLINK_TYPE_FLOAT: snprintf(b, sizeof b, "%.7g", rd<float>(p)); return b;
    case MAVLINK_TYPE_DOUBLE: snprintf(b, sizeof b, "%.15g", rd<double>(p)); return b;
    }
    return "?";
}

template <typename T> static T numeric(mavlink_message_type_t t, const uint8_t *p)
{
    switch (t) {
    case MAVLINK_TYPE_CHAR:
    case MAVLINK_TYPE_UINT8_T: return T(p[0]);
    case MAVLINK_TYPE_INT8_T: return T(rd<int8_t>(p));
    case MAVLINK_TYPE_UINT16_T: return T(rd<uint16_t>(p));
    case MAVLINK_TYPE_INT16_T: return T(rd<int16_t>(p));
    case MAVLINK_TYPE_UINT32_T: return T(rd<uint32_t>(p));
    case MAVLINK_TYPE_INT32_T: return T(rd<int32_t>(p));
    case MAVLINK_TYPE_UINT64_T: return T(rd<uint64_t>(p));
    case MAVLINK_TYPE_INT64_T: return T(rd<int64_t>(p));
    case MAVLINK_TYPE_FLOAT: return T(rd<float>(p));
    case MAVLINK_TYPE_DOUBLE: return T(rd<double>(p));
    }
    return T(0);
}

// char[] up to the first NUL; non-printable bytes as \xNN (Gremsy PARAM_EXT values are binary)
static std::string charArray(const uint8_t *p, unsigned n)
{
    std::string s = "\"";
    for (unsigned i = 0; i < n && p[i]; ++i) {
        if (p[i] >= 0x20 && p[i] < 0x7f && p[i] != '"' && p[i] != '\\') {
            s += char(p[i]);
        } else {
            char b[8];
            snprintf(b, sizeof b, "\\x%02x", p[i]);
            s += b;
        }
    }
    return s + "\"";
}

std::vector<MavField> decodeFields(const MavFrame &frame)
{
    std::vector<MavField> out;
    if (frame.status != MavFrame::Ok) return out;
    const mavlink_message_info_t *info = mavlink_get_message_info(&frame.msg);
    const mavlink_msg_entry_t *e = mavlink_get_msg_entry(frame.msgid);
    if (!info || !e) return out;

    const uint8_t *pl = (const uint8_t *)_MAV_PAYLOAD(&frame.msg);
    for (unsigned i = 0; i < info->num_fields; ++i) {
        const mavlink_field_info_t &fi = info->fields[i];
        MavField f;
        f.name = fi.name;
        f.extension = fi.wire_offset >= e->min_msg_len;
        f.type = typeName(fi.type);
        const uint8_t *p = pl + fi.wire_offset;
        f.isReal = fi.type == MAVLINK_TYPE_FLOAT || fi.type == MAVLINK_TYPE_DOUBLE;
        const unsigned n = fi.array_length ? fi.array_length : 1;
        if (fi.array_length && fi.type == MAVLINK_TYPE_CHAR) {
            f.isText = true;
            f.value = charArray(p, fi.array_length);
        } else {
            for (unsigned k = 0; k < n; ++k) {
                const uint8_t *e = p + k * typeSize(fi.type);
                if (f.isReal) f.reals.push_back(numeric<double>(fi.type, e));
                else f.ints.push_back(numeric<int64_t>(fi.type, e));
                f.value += (k ? ", " : "") + scalar(fi.type, e);
            }
            if (fi.array_length) f.value = "[" + f.value + "]";
        }
        if (fi.array_length) f.type += "[" + std::to_string(fi.array_length) + "]";
        out.push_back(std::move(f));
    }
    return out;
}

const char *messageName(uint32_t msgid)
{
    const mavlink_message_info_t *info = mavlink_get_message_info_by_id(msgid);
    return info ? info->name : nullptr;
}

const char *statusName(MavFrame::Status s)
{
    switch (s) {
    case MavFrame::Ok: return "OK";
    case MavFrame::BadCrc: return "BAD_CRC";
    case MavFrame::UnknownId: return "UNKNOWN_ID";
    case MavFrame::Truncated: return "TRUNCATED";
    case MavFrame::Garbage: return "GARBAGE";
    }
    return "?";
}
