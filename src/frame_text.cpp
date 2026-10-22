#include "frame_text.h"

#include "frame_model.h"
#include "mavlink_dict.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstring>

static size_t headerLen(const MavFrame &f) { return f.version == 1 ? 6 : 10; }

QString labelledValue(const MavField &f, quint32 msgid, const MavlinkDict *dict)
{
    const QString v = QString::fromStdString(f.value);
    if (!dict || f.isReal || f.isText || f.ints.size() != 1) return v;
    const MavlinkDict::Field *meta = dict->field(msgid, QString::fromStdString(f.name));
    if (!meta) return v;
    const QString label = dict->label(*meta, f.ints[0]);
    if (label.isEmpty()) return v;
    return meta->bitmask ? QStringLiteral("%1 (0x%2)").arg(label).arg(quint64(f.ints[0]), 0, 16)
                         : QStringLiteral("%1 (%2)").arg(label, v);
}

QString summarize(const MavFrame &f, const MavlinkDict *dict)
{
    if (f.status != MavFrame::Ok) return {};
    QStringList parts;
    for (const MavField &x : decodeFields(f))
        parts << QString::fromStdString(x.name) + QLatin1Char('=') + labelledValue(x, f.msgid, dict);
    return parts.join(QLatin1Char(' '));
}

QString validationText(const CapturedFrame &c)
{
    const MavFrame &f = c.frame;
    QStringList out;
    switch (f.status) {
    case MavFrame::Ok:
        out << QStringLiteral("CRC OK (0x%1)").arg(f.crcWire, 4, 16, QLatin1Char('0'));
        break;
    case MavFrame::BadCrc:
        out << QStringLiteral("CRC MISMATCH: wire 0x%1, computed 0x%2. Payload not decoded.")
                   .arg(f.crcWire, 4, 16, QLatin1Char('0')).arg(f.crcCalc, 4, 16, QLatin1Char('0'));
        break;
    case MavFrame::UnknownId:
        out << QStringLiteral("Message ID %1 is not in the ardupilotmega dialect: CRC cannot be verified "
                              "(no CRC_EXTRA), payload not decoded.").arg(f.msgid);
        break;
    case MavFrame::Truncated:
        if (f.headerComplete) {
            const size_t want = headerLen(f) + f.len + 2 + (f.signaturePresent ? 13 : 0);
            out << QStringLiteral("TRUNCATED: datagram ended after %1 of %2 frame bytes.").arg(f.raw.size()).arg(want);
        } else {
            out << QStringLiteral("TRUNCATED: datagram ended inside a v%1 header (%2 bytes).").arg(f.version).arg(f.raw.size());
        }
        break;
    case MavFrame::Garbage:
        out << QStringLiteral("%1 byte(s) outside any MAVLink frame (no STX, or a header with unsupported "
                              "incompatibility flags).").arg(f.raw.size());
        break;
    }
    if (f.signaturePresent)
        out << QStringLiteral("Signature present (13 bytes), NOT verified: no signing key is configured.");
    if (c.sendErrno)
        out << QStringLiteral("TX: sendto() failed: %1. The frame did not leave this PC.").arg(QString::fromLocal8Bit(strerror(c.sendErrno)));
    return out.join(QLatin1Char('\n'));
}

QString hexString(const std::vector<uint8_t> &raw)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(raw.data()), int(raw.size())).toHex());
}

QString hexDump(const MavFrame &f)
{
    QString s;
    const auto &r = f.raw;
    for (size_t off = 0; off < r.size(); off += 16) {
        QString hex, ascii;
        for (size_t i = off; i < off + 16; ++i) {
            if (i < r.size()) {
                hex += QStringLiteral("%1 ").arg(r[i], 2, 16, QLatin1Char('0'));
                ascii += (r[i] >= 0x20 && r[i] < 0x7f) ? QChar(r[i]) : QLatin1Char('.');
            } else {
                hex += QStringLiteral("   ");
            }
            if (i == off + 7) hex += QLatin1Char(' ');
        }
        s += QStringLiteral("%1  %2 |%3|\n").arg(off, 4, 16, QLatin1Char('0')).arg(hex, ascii);
    }
    if (f.headerComplete) {
        const size_t h = headerLen(f), p = h + f.len, avail = r.size();
        auto range = [avail](const char *what, size_t a, size_t b) {
            if (a >= avail) return QStringLiteral("%1: missing\n").arg(QLatin1String(what));
            return QStringLiteral("%1: 0x%2-0x%3\n").arg(QLatin1String(what)).arg(a, 0, 16).arg(std::min(b, avail) - 1, 0, 16);
        };
        s += QLatin1Char('\n') + range("header ", 0, h);
        if (f.len) s += range("payload", h, p);
        s += range("crc    ", p, p + 2);
        if (f.signaturePresent) s += range("signature", p + 2, p + 15);
    }
    return s;
}

QString isoTime(qint64 wallUs)
{
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(wallUs / 1000, Qt::UTC);
    return t.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz")) +
           QStringLiteral("%1Z").arg(wallUs % 1000, 3, 10, QLatin1Char('0'));
}

static QString csvField(const QString &s)
{
    if (!s.contains(QLatin1Char(',')) && !s.contains(QLatin1Char('"')) && !s.contains(QLatin1Char('\n')))
        return s;
    QString q = s;
    q.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + q + QLatin1Char('"');
}

QString csvHeader()
{
    return QStringLiteral("index,datagram,time_utc,mono_ns,dir,peer,version,sysid,compid,seq,msgid,name,len,"
                          "target_system,target_component,status,signed,send_errno,summary,raw_hex");
}

QString csvRow(const CapturedFrame &c)
{
    const MavFrame &f = c.frame;
    const bool h = f.headerComplete;
    auto num = [h](qint64 v) { return h ? QString::number(v) : QString(); };
    const char *name = h ? messageName(f.msgid) : nullptr;
    QStringList cols{
        QString::number(c.index), QString::number(c.datagram), isoTime(c.wallUs), QString::number(c.monoNs),
        c.dir == CapturedFrame::Tx ? QStringLiteral("TX") : QStringLiteral("RX"), endpointText(c.peerIp, c.peerPort),
        num(f.version), num(f.sysid), num(f.compid), num(f.seq), num(f.msgid),
        name ? QString::fromLatin1(name) : QString(), num(f.len),
        f.targetSystem >= 0 ? QString::number(f.targetSystem) : QString(),
        f.targetComponent >= 0 ? QString::number(f.targetComponent) : QString(),
        QString::fromLatin1(statusName(f.status)), f.signaturePresent ? QStringLiteral("1") : QStringLiteral("0"),
        QString::number(c.sendErrno), csvField(c.summary), hexString(f.raw)};
    return cols.join(QLatin1Char(','));
}

static QJsonValue real(double d)
{
    if (std::isnan(d)) return QStringLiteral("nan");  // JSON has no NaN/Inf; keep them distinguishable
    if (std::isinf(d)) return d > 0 ? QStringLiteral("inf") : QStringLiteral("-inf");
    return d;
}

QByteArray jsonLine(const CapturedFrame &c)
{
    const MavFrame &f = c.frame;
    QJsonObject o{{QStringLiteral("index"), qint64(c.index)},
                  {QStringLiteral("datagram"), qint64(c.datagram)},
                  {QStringLiteral("time_utc"), isoTime(c.wallUs)},
                  {QStringLiteral("mono_ns"), c.monoNs},
                  {QStringLiteral("dir"), c.dir == CapturedFrame::Tx ? QStringLiteral("TX") : QStringLiteral("RX")},
                  {QStringLiteral("peer"), endpointText(c.peerIp, c.peerPort)},
                  {QStringLiteral("status"), QString::fromLatin1(statusName(f.status))},
                  {QStringLiteral("signed"), f.signaturePresent},
                  {QStringLiteral("send_errno"), c.sendErrno},
                  {QStringLiteral("raw_hex"), hexString(f.raw)}};
    if (f.headerComplete) {
        o.insert(QStringLiteral("version"), f.version);
        o.insert(QStringLiteral("sysid"), f.sysid);
        o.insert(QStringLiteral("compid"), f.compid);
        o.insert(QStringLiteral("seq"), f.seq);
        o.insert(QStringLiteral("msgid"), qint64(f.msgid));
        o.insert(QStringLiteral("len"), f.len);
        if (const char *n = messageName(f.msgid)) o.insert(QStringLiteral("name"), QString::fromLatin1(n));
        if (f.targetSystem >= 0) o.insert(QStringLiteral("target_system"), f.targetSystem);
        if (f.targetComponent >= 0) o.insert(QStringLiteral("target_component"), f.targetComponent);
    }
    if (f.status == MavFrame::Ok) {
        QJsonObject fields;
        for (const MavField &x : decodeFields(f)) {
            QJsonValue v;
            if (x.isText) {
                v = QString::fromStdString(x.value.substr(1, x.value.size() - 2));  // drop display quotes
            } else {
                QJsonArray a;
                if (x.isReal) for (double d : x.reals) a.append(real(d));
                else for (int64_t i : x.ints) a.append(qint64(i));
                v = x.type.find('[') == std::string::npos ? a.first() : QJsonValue(a);
            }
            fields.insert(QString::fromStdString(x.name), v);
        }
        o.insert(QStringLiteral("fields"), fields);
    }
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
