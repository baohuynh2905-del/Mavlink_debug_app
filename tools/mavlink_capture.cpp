// Headless capture: connect through PayloadLink (the same SDK connection the GUI uses), record
// every frame for a while, and print what was observed. Sends only what the GUI sends on connect:
// the SDK heartbeat/stream requests and the one-off info requests. No movement or camera commands.
//
//   mavlink_capture --ip 192.168.55.1 --port 14566 --seconds 30 --out capture   (writes capture.tlog + capture.jsonl)

#include "flow_stats.h"
#include "frame_model.h"
#include "frame_writer.h"
#include "mavlink_dict.h"
#include "payload_link.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTimer>

#include <cstdio>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser p;
    p.addHelpOption();
    p.addOption({QStringLiteral("ip"), QStringLiteral("Payload IPv4"), QStringLiteral("ip"), QStringLiteral("192.168.55.1")});
    p.addOption({QStringLiteral("port"), QStringLiteral("Payload UDP port"), QStringLiteral("port"), QStringLiteral("14566")});
    p.addOption({QStringLiteral("seconds"), QStringLiteral("Capture duration"), QStringLiteral("s"), QStringLiteral("30")});
    p.addOption({QStringLiteral("out"), QStringLiteral("Output base name (.tlog and .jsonl)"), QStringLiteral("path"), QStringLiteral("capture")});
    p.process(app);

    MavlinkDict dict;
    QString err;
    const bool haveDict = dict.load(MavlinkDict::defaultDir(), QStringLiteral("ardupilotmega.xml"), &err);
    if (!haveDict) fprintf(stderr, "warning: %s\n", qPrintable(err));

    PayloadLink link;
    link.setDictionary(haveDict ? &dict : nullptr);
    FlowStats flows;
    TrafficStats totals;
    FrameWriter tlog, jsonl;
    const QString base = p.value(QStringLiteral("out"));
    if (!tlog.start(base + QStringLiteral(".tlog"), FrameWriter::Tlog, &err) ||
        !jsonl.start(base + QStringLiteral(".jsonl"), FrameWriter::JsonLines, &err)) {
        fprintf(stderr, "cannot write %s: %s\n", qPrintable(base), qPrintable(err));
        return 2;
    }

    const qint64 t0 = PayloadLink::monoNs();
    auto stamp = [t0] { return double(PayloadLink::monoNs() - t0) / 1e9; };
    QObject::connect(&link, &PayloadLink::stateChanged, [&] {
        printf("[%6.2f s] %s%s\n", stamp(), qPrintable(linkStateText(link.state())),
               link.lastError().isEmpty() ? "" : qPrintable(QStringLiteral(": ") + link.lastError()));
        fflush(stdout);
        if (link.state() == LinkState::Disconnected || link.state() == LinkState::Failed) app.quit();
    });
    QObject::connect(&link, &PayloadLink::framesCaptured, [&](const std::vector<CapturedFrame> &frames) {
        for (const CapturedFrame &c : frames) {
            flows.add(c);
            totals.add(c);
        }
        tlog.write(frames);
        jsonl.write(frames);
    });

    const int seconds = p.value(QStringLiteral("seconds")).toInt();
    flows.sampleRates(0);
    QTimer::singleShot(seconds * 1000, &link, [&] {
        flows.sampleRates(double(PayloadLink::monoNs() - t0) / 1e9);  // average rate over the whole capture
        link.disconnectLink();
        if (!isOpen(link.state())) app.quit();
    });
    link.connectTo(p.value(QStringLiteral("ip")), p.value(QStringLiteral("port")).toInt());
    app.exec();

    auto comp = [&](int c) {
        const QString n = haveDict ? dict.enumLabel(QStringLiteral("MAV_COMPONENT"), c) : QString();
        return n.isEmpty() ? QString::number(c) : QStringLiteral("%1 %2").arg(c).arg(n);
    };
    printf("\n== Totals: RX %llu frames / %llu B, TX %llu frames / %llu B; bad CRC %llu, unknown ID %llu, "
           "truncated %llu, junk %llu, send errors %llu, capture drops %llu\n",
           totals.frames[0], totals.bytes[0], totals.frames[1], totals.bytes[1], totals.badCrc, totals.unknownId,
           totals.truncated, totals.garbage, totals.sendErrors, (unsigned long long)link.droppedDatagrams());
    printf("\n== Messages (dir sys/comp msgid name: count, average Hz)\n");
    for (const auto &[k, fc] : flows.flows) {
        const char *n = messageName(k.msgid);
        printf("  %s %3d/%-40s %6u %-34s %6llu  %7.2f Hz\n", qPrintable(dirText(k.dir)), k.sysid, qPrintable(comp(k.compid)), k.msgid,
               n ? n : "(unknown)", fc.count, fc.rateHz);
    }
    printf("\n== RX sequence per source\n");
    for (const auto &[key, s] : flows.rxSources)
        printf("  %3d/%-40s %6llu frames  jumps %llu  missing %llu  reordered %llu  other backwards %llu\n", key >> 8,
               qPrintable(comp(key & 0xFF)), s.frames, s.jumps, s.missing, s.reordered, s.backwards);
    printf("\n== Commands sent by the app\n");
    for (const TrackedCommand &t : link.tracker().commands()) {
        QString result;
        if (t.result >= 0) {
            const QString e = t.msgid == MAVLINK_MSG_ID_PARAM_EXT_SET ? QStringLiteral("PARAM_ACK") : QStringLiteral("MAV_RESULT");
            result = QStringLiteral(" -> %1 from comp %2 after %3 ms")
                         .arg(haveDict ? dict.enumLabel(e, t.result) : QString::number(t.result)).arg(t.ackFrom).arg(t.ackMs - t.sentMs);
        }
        printf("  %-45s %-24s%s\n", qPrintable(t.action), qPrintable(CommandTracker::stateText(t.state)), qPrintable(result));
    }
    tlog.stop();
    jsonl.stop();
    printf("\nWrote %s.tlog and %s.jsonl\n", qPrintable(base), qPrintable(base));
    return 0;
}
