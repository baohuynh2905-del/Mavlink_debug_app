#include "flow_stats.h"

void FlowStats::add(const CapturedFrame &c)
{
    const MavFrame &f = c.frame;
    if (f.status != MavFrame::Ok && f.status != MavFrame::UnknownId) return;

    FlowCounter &fc = flows[{c.dir, f.sysid, f.compid, f.msgid}];
    ++fc.count;
    fc.bytes += f.raw.size();
    fc.lastNs = c.monoNs;

    if (c.dir != CapturedFrame::Rx) return;  // the SDK's own TX uses several seq counters
    SourceSeq &s = rxSources[quint16(f.sysid << 8 | f.compid)];
    ++s.frames;
    if (s.lastSeq >= 0) {
        const int jump = (f.seq - ((s.lastSeq + 1) & 0xFF)) & 0xFF;
        if (jump > 0 && jump < 128) {
            ++s.gaps;
            s.skipped += quint64(jump);
        } else if (jump >= 128) {
            ++s.backwards;
        }
    }
    s.lastSeq = f.seq;
}

void FlowStats::sampleRates(double seconds)
{
    for (auto &kv : flows) {
        FlowCounter &fc = kv.second;
        fc.rateHz = seconds > 0 ? double(fc.count - fc.sampledCount) / seconds : 0;
        fc.sampledCount = fc.count;
    }
}
