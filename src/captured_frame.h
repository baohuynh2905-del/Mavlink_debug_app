#pragma once

#include "mavlink_frame.h"

#include <QString>

#include <atomic>

// One frame (or diagnostic byte run) as observed on the SDK's real transport.
struct CapturedFrame {
    enum Dir { Rx = 0, Tx = 1, Net = 2 };  // Rx/Tx = this app's link (SDK observer's dir); Net = network capture

    static quint64 nextIndex()  // one observation order across the app link and network capture
    {
        static std::atomic<quint64> n{0};
        return n++;
    }

    quint64 index = 0;     // observation order across RX and TX
    quint64 datagram = 0;  // frames from the same UDP datagram share this
    qint64 monoNs = 0;     // steady clock: ordering and intervals
    qint64 wallUs = 0;     // system clock (UTC epoch, µs): display and export
    int dir = Rx;
    quint32 peerIp = 0;    // host byte order; RX = source, TX = destination, Net = source
    quint16 peerPort = 0;
    quint32 dstIp = 0;     // Net only: destination
    quint16 dstPort = 0;
    int sendErrno = 0;     // TX only: errno of a failed sendto(), 0 = handed to the OS
    MavFrame frame;
    QString summary;       // decoded "field=value ..." with enum names; filled on the GUI thread
};

// Running totals for the connection panel and the inspector statistics.
struct TrafficStats {
    quint64 frames[3] = {0, 0, 0};  // valid-header frames per Dir
    quint64 bytes[3] = {0, 0, 0};   // all observed bytes per Dir
    quint64 badCrc = 0, unknownId = 0, truncated = 0, garbage = 0, sendErrors = 0;
    qint64 lastNs[3] = {-1, -1, -1};

    void add(const CapturedFrame &c)
    {
        bytes[c.dir] += c.frame.raw.size();
        lastNs[c.dir] = c.monoNs;
        switch (c.frame.status) {
        case MavFrame::Ok: ++frames[c.dir]; break;
        case MavFrame::BadCrc: ++frames[c.dir]; ++badCrc; break;
        case MavFrame::UnknownId: ++frames[c.dir]; ++unknownId; break;
        case MavFrame::Truncated: ++truncated; break;
        case MavFrame::Garbage: ++garbage; break;
        }
        if (c.sendErrno) ++sendErrors;
    }
};
