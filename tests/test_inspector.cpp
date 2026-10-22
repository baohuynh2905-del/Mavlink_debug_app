// Stage 3: dictionary, summaries, filters, statistics, exporters, and the inspector widget.
#include "flow_stats.h"
#include "frame_filter.h"
#include "frame_model.h"
#include "frame_text.h"
#include "frame_writer.h"
#include "inspector_widget.h"
#include "mavlink_dict.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtEndian>
#include <QtTest>

#include <cmath>

static quint64 g_index = 0;

static CapturedFrame captured(const QByteArray &bytes, int dir = CapturedFrame::Rx, const MavlinkDict *dict = nullptr)
{
    auto frames = parseDatagram(reinterpret_cast<const uint8_t *>(bytes.constData()), size_t(bytes.size()));
    CapturedFrame c;
    c.index = g_index++;
    c.dir = dir;
    c.wallUs = 1761103945123456 + qint64(c.index);
    c.monoNs = qint64(c.index) * 1000000;
    c.peerIp = 0xC0A83701;  // 192.168.55.1
    c.peerPort = 14566;
    c.frame = frames.at(0);
    c.summary = summarize(c.frame, dict);
    return c;
}

static QByteArray wire(const mavlink_message_t &m)
{
    uint8_t b[MAVLINK_MAX_PACKET_LEN];
    return QByteArray(reinterpret_cast<const char *>(b), mavlink_msg_to_send_buffer(b, &m));
}

static QByteArray heartbeat(uint8_t comp, uint8_t type, uint8_t seqChanStep = 0)
{
    mavlink_message_t m;
    for (uint8_t i = 0; i < seqChanStep; ++i) mavlink_get_channel_status(MAVLINK_COMM_3)->current_tx_seq++;
    mavlink_msg_heartbeat_pack_chan(1, comp, MAVLINK_COMM_3, &m, type, MAV_AUTOPILOT_INVALID, 0, 0, MAV_STATE_ACTIVE);
    return wire(m);
}

static QByteArray gimbalStatus()
{
    mavlink_message_t m;
    float q[4] = {1, 0, 0, 0};
    mavlink_msg_gimbal_device_attitude_status_pack_chan(1, MAV_COMP_ID_GIMBAL, MAVLINK_COMM_0, &m, 0, 0, 1234,
        GIMBAL_DEVICE_FLAGS_YAW_LOCK | GIMBAL_DEVICE_FLAGS_RETRACT, q, NAN, 0.1f, 0, 0, 0, 0, 0);
    return wire(m);
}

static QByteArray paramExtSet()
{
    mavlink_message_t m;
    char id[16] = "C_V_REC";   // pack copies the full 16 bytes
    char value[128] = {1, 0};  // Gremsy sends uint32 bytes, not text
    mavlink_msg_param_ext_set_pack_chan(1, 193, MAVLINK_COMM_0, &m, 1, 100, id, value, MAV_PARAM_EXT_TYPE_UINT32);
    return wire(m);
}

class TestInspector : public QObject
{
    Q_OBJECT
    MavlinkDict dict;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QString err;
        QVERIFY2(dict.load(MavlinkDict::defaultDir(), QStringLiteral("ardupilotmega.xml"), &err), qPrintable(err));
    }

    void dictionary()
    {
        QVERIFY(dict.files().contains(QStringLiteral("common.xml")));
        QVERIFY(dict.files().contains(QStringLiteral("minimal.xml")));  // nested include
        const MavlinkDict::Field *f = dict.field(MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS, QStringLiteral("angular_velocity_x"));
        QVERIFY(f);
        QCOMPARE(f->units, QStringLiteral("rad/s"));
        QCOMPARE(f->invalid, QStringLiteral("NaN"));
        const MavlinkDict::Field *type = dict.field(MAVLINK_MSG_ID_HEARTBEAT, QStringLiteral("type"));
        QVERIFY(type);
        QCOMPARE(dict.label(*type, MAV_TYPE_GIMBAL), QStringLiteral("MAV_TYPE_GIMBAL"));
        const MavlinkDict::Field *flags = dict.field(MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS, QStringLiteral("flags"));
        QVERIFY(flags && flags->bitmask);
        QCOMPARE(dict.label(*flags, GIMBAL_DEVICE_FLAGS_RETRACT | GIMBAL_DEVICE_FLAGS_YAW_LOCK),
                 QStringLiteral("GIMBAL_DEVICE_FLAGS_RETRACT | GIMBAL_DEVICE_FLAGS_YAW_LOCK"));
        QVERIFY(dict.label(*flags, 1 << 20).contains(QStringLiteral("0x100000")));  // unnamed bits are shown
        QCOMPARE(dict.enumLabel(QStringLiteral("MAV_COMPONENT"), MAV_COMP_ID_GIMBAL), QStringLiteral("MAV_COMP_ID_GIMBAL"));

        MavlinkDict missing;
        QString err;
        QVERIFY(!missing.load(QStringLiteral("/nonexistent"), QStringLiteral("ardupilotmega.xml"), &err));
        QVERIFY(!err.isEmpty());
    }

    void summaries()
    {
        CapturedFrame hb = captured(heartbeat(MAV_COMP_ID_GIMBAL, MAV_TYPE_GIMBAL), CapturedFrame::Rx, &dict);
        QVERIFY2(hb.summary.contains(QStringLiteral("type=MAV_TYPE_GIMBAL (26)")), qPrintable(hb.summary));
        CapturedFrame gs = captured(gimbalStatus(), CapturedFrame::Rx, &dict);
        QVERIFY2(gs.summary.contains(QStringLiteral("flags=GIMBAL_DEVICE_FLAGS_RETRACT | GIMBAL_DEVICE_FLAGS_YAW_LOCK (0x11)")),
                 qPrintable(gs.summary));
        CapturedFrame ps = captured(paramExtSet(), CapturedFrame::Tx, &dict);
        QVERIFY2(ps.summary.contains(QStringLiteral("param_value=\"\\x01\"")), qPrintable(ps.summary));
        QVERIFY(validationText(ps).startsWith(QStringLiteral("CRC OK")));
        QVERIFY(hexDump(ps.frame).contains(QStringLiteral("payload: 0xa-")));
    }

    void criteria()
    {
        CapturedFrame hb = captured(heartbeat(MAV_COMP_ID_GIMBAL, MAV_TYPE_GIMBAL), CapturedFrame::Rx, &dict);
        CapturedFrame junk = captured(QByteArray("xyz"));
        FrameCriteria c;
        QVERIFY(c.matches(hb) && c.matches(junk));
        c.dir = CapturedFrame::Tx;
        QVERIFY(!c.matches(hb));
        c = {};
        c.compid = MAV_COMP_ID_GIMBAL;
        QVERIFY(c.matches(hb) && !c.matches(junk));  // junk has no header
        c.compid = 100;
        QVERIFY(!c.matches(hb));
        c = {};
        c.message = QStringLiteral("0");
        QVERIFY(c.matches(hb));
        c.message = QStringLiteral("heart");
        QVERIFY(c.matches(hb));
        c.message = QStringLiteral("COMMAND");
        QVERIFY(!c.matches(hb));
        c = {};
        c.text = QStringLiteral("mav_type_gimbal");
        QVERIFY(c.matches(hb) && !c.matches(junk));
        c = {};
        c.status = FrameCriteria::ProblemsOnly;
        QVERIFY(!c.matches(hb) && c.matches(junk));
        c.status = FrameCriteria::ValidOnly;
        QVERIFY(c.matches(hb) && !c.matches(junk));
    }

    void proxyFiltersLiveRows()
    {
        FrameModel model(100);
        FrameFilter filter(&model);
        FrameCriteria c;
        c.message = QStringLiteral("HEARTBEAT");
        filter.setCriteria(c);
        model.append({captured(heartbeat(1, MAV_TYPE_GIMBAL)), captured(QByteArray("junk")), captured(gimbalStatus())});
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(filter.rowCount(), 1);
        model.append({captured(heartbeat(100, MAV_TYPE_CAMERA))});
        QCOMPARE(filter.rowCount(), 2);
        QCOMPARE(int(filter.frameAt(1).frame.compid), 100);
    }

    void sequenceGaps()
    {
        FlowStats s;
        auto rx = [&](uint8_t comp, int seq) {
            CapturedFrame c = captured(heartbeat(comp, MAV_TYPE_GIMBAL));
            c.frame.seq = uint8_t(seq);  // header seq only; the stats never re-check the CRC
            s.add(c);
        };
        rx(154, 1); rx(154, 2); rx(154, 5);  // 3 and 4 jumped over
        const SourceSeq &gimbal = s.rxSources.at(quint16(1 << 8 | 154));
        QCOMPARE(gimbal.jumps, 1ull);
        QCOMPARE(gimbal.missing, 2ull);
        rx(154, 3);  // late: fills the jump (VIO pattern), not a restart
        QCOMPARE(gimbal.reordered, 1ull);
        QCOMPARE(gimbal.missing, 1ull);
        QCOMPARE(gimbal.backwards, 0ull);
        rx(154, 6);  // continues after 5: the late frame did not move the counter
        QCOMPARE(gimbal.jumps, 1ull);
        rx(154, 2);  // backwards to a number that was never skipped: restart / second counter
        QCOMPARE(gimbal.backwards, 1ull);
        QCOMPARE(gimbal.frames, 6ull);

        // The exact sequence captured from a real VIO payload core: 200, 202, 201, 203
        rx(26, 200); rx(26, 202); rx(26, 201); rx(26, 203);
        const SourceSeq &core = s.rxSources.at(quint16(1 << 8 | 26));
        QCOMPARE(core.missing, 0ull);
        QCOMPARE(core.reordered, 1ull);
        QCOMPARE(core.backwards, 0ull);

        rx(100, 254); rx(100, 255); rx(100, 0); rx(100, 1);  // wraparound is continuous
        const SourceSeq &camera = s.rxSources.at(quint16(1 << 8 | 100));
        QCOMPARE(camera.jumps, 0ull);
        QCOMPARE(camera.backwards, 0ull);
        rx(100, 3);
        QCOMPARE(camera.jumps, 1ull);
        QCOMPARE(camera.missing, 1ull);

        CapturedFrame tx = captured(heartbeat(193, MAV_TYPE_ONBOARD_CONTROLLER), CapturedFrame::Tx);
        s.add(tx);
        QVERIFY(!s.rxSources.count(quint16(1 << 8 | 193)));  // TX seq is never checked
        QCOMPARE(s.flows.at({CapturedFrame::Tx, 1, 193, 0}).count, 1ull);
        s.sampleRates(0.5);
        QCOMPARE(s.flows.at({CapturedFrame::Rx, 1, 154, 0}).rateHz, 12.0);  // 6 frames / 0.5 s
    }

    void csvAndJson()
    {
        CapturedFrame c = captured(gimbalStatus(), CapturedFrame::Rx, &dict);
        c.summary = QStringLiteral("a,\"b\"");
        const QString row = csvRow(c);
        QVERIFY(row.endsWith(QStringLiteral(",\"a,\"\"b\"\"\",") + hexString(c.frame.raw)));
        QCOMPARE(csvHeader().count(QLatin1Char(',')), 19);

        const QJsonObject o = QJsonDocument::fromJson(jsonLine(c)).object();
        QCOMPARE(o.value(QStringLiteral("dir")).toString(), QStringLiteral("RX"));
        QCOMPARE(o.value(QStringLiteral("peer")).toString(), QStringLiteral("192.168.55.1:14566"));
        QCOMPARE(o.value(QStringLiteral("time_utc")).toString(), isoTime(c.wallUs));
        QVERIFY(isoTime(1761103945123456).endsWith(QStringLiteral(".123456Z")));
        const QJsonObject f = o.value(QStringLiteral("fields")).toObject();
        QCOMPARE(f.value(QStringLiteral("flags")).toInt(), GIMBAL_DEVICE_FLAGS_YAW_LOCK | GIMBAL_DEVICE_FLAGS_RETRACT);
        QCOMPARE(f.value(QStringLiteral("angular_velocity_x")).toString(), QStringLiteral("nan"));
        QCOMPARE(f.value(QStringLiteral("q")).toArray().size(), 4);

        CapturedFrame p = captured(paramExtSet(), CapturedFrame::Tx);
        const QJsonObject pf = QJsonDocument::fromJson(jsonLine(p)).object().value(QStringLiteral("fields")).toObject();
        QCOMPARE(pf.value(QStringLiteral("param_id")).toString(), QStringLiteral("C_V_REC"));
    }

    void writerFormats()
    {
        QTemporaryDir dir;
        std::vector<CapturedFrame> frames{captured(heartbeat(154, MAV_TYPE_GIMBAL)), captured(QByteArray("junk")),
                                          captured(gimbalStatus(), CapturedFrame::Tx)};
        auto writeFile = [&](const QString &name) {
            const QString path = dir.filePath(name);
            FrameWriter w;
            QString err;
            if (!w.start(path, FrameWriter::formatForPath(path), &err)) qFatal("%s", qPrintable(err));
            QSignalSpy done(&w, &FrameWriter::finished);
            w.write(frames);
            w.stop();
            if (!done.wait(5000)) qFatal("writer did not finish");
            QFile f(path);
            f.open(QIODevice::ReadOnly);
            return std::make_pair(f.readAll(), w.skipped());
        };

        const auto [csv, csvSkipped] = writeFile(QStringLiteral("a.csv"));
        QCOMPARE(csv.count('\n'), 4);  // header + 3 rows
        QCOMPARE(csvSkipped, 0ull);

        const auto [jsonl, jsonSkipped] = writeFile(QStringLiteral("a.jsonl"));
        const QList<QByteArray> lines = jsonl.trimmed().split('\n');
        QCOMPARE(lines.size(), 3);
        for (const QByteArray &l : lines) QVERIFY(QJsonDocument::fromJson(l).isObject());

        // tlog: [8-byte big-endian µs][raw frame] per record, junk excluded
        const auto [tlog, tlogSkipped] = writeFile(QStringLiteral("a.tlog"));
        QCOMPARE(tlogSkipped, 1ull);
        int pos = 0;
        std::vector<CapturedFrame> expect{frames[0], frames[2]};
        for (const CapturedFrame &c : expect) {
            QVERIFY(pos + 8 <= tlog.size());
            QCOMPARE(qint64(qFromBigEndian<quint64>(tlog.constData() + pos)), c.wallUs);
            pos += 8;
            const QByteArray raw(reinterpret_cast<const char *>(c.frame.raw.data()), int(c.frame.raw.size()));
            QCOMPARE(tlog.mid(pos, raw.size()), raw);
            pos += raw.size();
        }
        QCOMPARE(pos, tlog.size());
        if (const QByteArray out = qgetenv("TLOG_OUT"); !out.isEmpty()) {  // for checking with an external reader
            QFile f(QString::fromLocal8Bit(out));
            QVERIFY(f.open(QIODevice::WriteOnly) && f.write(tlog) == tlog.size());
        }

        FrameWriter bad;
        QString err;
        QVERIFY(!bad.start(dir.filePath(QStringLiteral("no/such/dir/x.csv")), FrameWriter::Csv, &err));
        QVERIFY(!err.isEmpty());
    }

    void pauseKeepsCapturingAndRecording()
    {
        QTemporaryDir dir;
        InspectorWidget w(&dict);
        const QString path = dir.filePath(QStringLiteral("rec.jsonl"));
        QString err;
        QVERIFY2(w.startRecording(path, &err), qPrintable(err));

        w.addFrames({captured(heartbeat(154, MAV_TYPE_GIMBAL), CapturedFrame::Rx, &dict)});
        QCOMPARE(w.model()->rowCount(), 1);

        auto *pause = w.findChild<QPushButton *>(QStringLiteral("pauseButton"));
        QVERIFY(pause && pause->isCheckable());
        pause->setChecked(true);
        w.addFrames({captured(gimbalStatus(), CapturedFrame::Rx, &dict), captured(paramExtSet(), CapturedFrame::Tx, &dict)});
        QCOMPARE(w.model()->rowCount(), 1);  // display frozen
        pause->setChecked(false);
        QCOMPARE(w.model()->rowCount(), 3);  // held frames appended in order
        QCOMPARE(w.model()->frameAt(2).frame.msgid, quint32(MAVLINK_MSG_ID_PARAM_EXT_SET));

        w.stopRecording();
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) && f.readAll().count('\n') == 3;  // recording ignored the pause
        }(), 5000);

        // Details for the selected row
        auto *table = w.findChild<QTableView *>();
        table->setCurrentIndex(table->model()->index(1, 0));
        auto *tree = w.findChild<QTreeWidget *>();
        QVERIFY(!tree->findItems(QStringLiteral("Fields: GIMBAL_DEVICE_ATTITUDE_STATUS"), Qt::MatchExactly).isEmpty());
        const auto vel = tree->findItems(QStringLiteral("angular_velocity_x"), Qt::MatchExactly | Qt::MatchRecursive);
        QVERIFY(!vel.isEmpty());
        QVERIFY(vel[0]->text(1).contains(QStringLiteral("invalid/unknown")));
        QCOMPARE(vel[0]->text(3), QStringLiteral("rad/s"));
        QVERIFY(w.findChild<QPlainTextEdit *>()->toPlainText().startsWith(QStringLiteral("0000  fd")));
    }
};

QTEST_MAIN(TestInspector)
#include "test_inspector.moc"
