#include "flow_stats.h"

#include <algorithm>

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
        const int expected = (s.lastSeq + 1) & 0xFF;
        const int jump = (f.seq - expected) & 0xFF;
        if (jump > 0 && jump < 128) {
            ++s.jumps;
            s.missing += quint64(jump);
            for (int i = 0; i < jump; ++i) s.pending.push_back(quint8((expected + i) & 0xFF));
            if (s.pending.size() > 64) s.pending.erase(s.pending.begin(), s.pending.end() - 64);
        } else if (jump >= 128) {
            auto late = std::find(s.pending.begin(), s.pending.end(), f.seq);
            if (late != s.pending.end()) {  // fills an earlier jump: not lost, and the counter did not restart
                s.pending.erase(late);
                --s.missing;
                ++s.reordered;
                return;
            }
            ++s.backwards;
            s.pending.clear();
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
