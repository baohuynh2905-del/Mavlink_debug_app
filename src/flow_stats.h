#pragma once
// Per-flow message statistics and RX sequence tracking. Pure, so it is unit tested.

#include "captured_frame.h"

#include <map>
#include <tuple>

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

// MAVLink seq is one counter per sending channel, shared by all its message types. A jump forward
// MAY be loss; a jump backwards is a restart, reordering, or a sender with several counters.
struct SourceSeq {
    quint64 frames = 0;
    int lastSeq = -1;
    quint64 gaps = 0;     // forward jumps
    quint64 skipped = 0;  // sequence numbers jumped over (an upper bound on lost frames)
    quint64 backwards = 0;
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
