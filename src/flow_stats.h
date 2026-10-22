#pragma once
// Per-flow message statistics and RX sequence tracking. Pure, so it is unit tested.

#include "captured_frame.h"

#include <map>
#include <tuple>
#include <vector>

struct FlowKey {
    int dir;
    quint8 sysid, compid;
    quint32 msgid;
    bool operator<(const FlowKey &o) const
    {
        return std::tie(dir, sysid, compid, msgid) < std::tie(o.dir, o.sysid, o.compid, o.msgid);
    }
};

struct FlowCounter {
    quint64 count = 0, bytes = 0;
    double rateHz = 0;
    qint64 lastNs = -1;
    quint64 sampledCount = 0;
};

// MAVLink seq is one counter per sending channel, shared by all its message types. A forward jump
// MAY be loss. A late frame that fills an earlier jump is reordering (seen on the VIO: two sender
// threads share one counter). Any other backwards jump is a restart or a sender with several counters.
struct SourceSeq {
    quint64 frames = 0;
    int lastSeq = -1;
    quint64 jumps = 0;      // forward jumps
    quint64 missing = 0;    // numbers jumped over and never seen later: upper bound on lost frames
    quint64 reordered = 0;  // late frames that filled an earlier jump
    quint64 backwards = 0;  // other backwards jumps
    std::vector<quint8> pending;  // recently jumped-over numbers, newest last (bounded)
};

class FlowStats
{
public:
    void add(const CapturedFrame &c);
    void sampleRates(double seconds);  // call periodically; rate = new frames / seconds
    void clear() { flows.clear(); rxSources.clear(); }

    std::map<FlowKey, FlowCounter> flows;   // OK and UNKNOWN_ID frames (BAD_CRC headers are not trusted)
    std::map<quint16, SourceSeq> rxSources; // key sysid << 8 | compid
};
