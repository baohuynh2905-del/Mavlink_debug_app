#include "frame_model.h"

#include <QBrush>
#include <QColor>
#include <QDateTime>

#include <cstring>

QString dirText(int dir)
{
    return dir == CapturedFrame::Tx ? QStringLiteral("TX") : dir == CapturedFrame::Net ? QStringLiteral("NET") : QStringLiteral("RX");
}

QString peerText(const CapturedFrame &c)
{
    const QString src = endpointText(c.peerIp, c.peerPort);
    return c.dir == CapturedFrame::Net ? src + QStringLiteral(" → ") + endpointText(c.dstIp, c.dstPort) : src;
}

QString endpointText(quint32 ip, quint16 port)
{
    return QStringLiteral("%1.%2.%3.%4:%5")
        .arg(ip >> 24).arg((ip >> 16) & 0xFF).arg((ip >> 8) & 0xFF).arg(ip & 0xFF).arg(port);
}

FrameModel::FrameModel(size_t capacity, QObject *parent) : QAbstractTableModel(parent), m_capacity(capacity) {}

int FrameModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int FrameModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant FrameModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const char *names[ColumnCount] = {"#", "Time", "Dir", "Peer", "Ver", "Sys", "Comp", "Seq",
                                             "Msg ID", "Message", "Len", "Target", "Status", "Decoded"};
    return QString::fromLatin1(names[section]);
}

QVariant FrameModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return {};
    const CapturedFrame &c = m_rows[size_t(index.row())];
    const MavFrame &f = c.frame;
    const bool hdr = f.headerComplete;

    if (role == Qt::ForegroundRole && index.column() == Status) {
        if (f.status == MavFrame::Ok && !c.sendErrno) return {};
        return QBrush(f.status == MavFrame::UnknownId ? QColor(0xB3, 0x6B, 0x00) : QColor(0xC0, 0x1C, 0x28));
    }
    if (role == Qt::TextAlignmentRole) {
        if (index.column() == Dir) return int(Qt::AlignCenter);
        if (index.column() != Name && index.column() != Status && index.column() != Peer && index.column() != Summary)
            return int(Qt::AlignRight | Qt::AlignVCenter);
    }
    if (role == Qt::ToolTipRole && index.column() == Summary) return c.summary;  // column is often narrow
    if (role == Qt::ToolTipRole && index.column() == Status) return data(index, Qt::DisplayRole);
    if (role != Qt::DisplayRole) return {};

    switch (index.column()) {
    case Index: return c.index;
    case Time: return QDateTime::fromMSecsSinceEpoch(c.wallUs / 1000).toString(QStringLiteral("HH:mm:ss.zzz"));
    case Dir: return dirText(c.dir);
    case Peer: return peerText(c);
    case Version: return hdr ? QVariant(f.version) : QVariant();
    case SysId: return hdr ? QVariant(f.sysid) : QVariant();
    case CompId: return hdr ? QVariant(f.compid) : QVariant();
    case Seq: return hdr ? QVariant(f.seq) : QVariant();
    case MsgId: return hdr ? QVariant(f.msgid) : QVariant();
    case Name:
        if (!hdr) return QStringLiteral("(%1 bytes)").arg(f.raw.size());
        if (const char *n = messageName(f.msgid)) return QString::fromLatin1(n);
        return QStringLiteral("(unknown)");
    case Len: return hdr ? QVariant(f.len) : QVariant();
    case Target:
        if (f.targetSystem < 0) return {};
        return f.targetComponent < 0 ? QString::number(f.targetSystem)
                                     : QStringLiteral("%1/%2").arg(f.targetSystem).arg(f.targetComponent);
    case Status: {
        QString s = QString::fromLatin1(statusName(f.status));
        if (f.signaturePresent) s += QStringLiteral(", signed (unverified)");
        if (c.sendErrno) s += QStringLiteral(", SEND FAILED: ") + QString::fromLocal8Bit(strerror(c.sendErrno));
        return s;
    }
    case Summary: return c.summary;
    }
    return {};
}

void FrameModel::append(const std::vector<CapturedFrame> &frames)
{
    if (frames.empty()) return;
    const size_t keep = std::min(frames.size(), m_capacity);
    const size_t overflow = m_rows.size() + keep > m_capacity ? m_rows.size() + keep - m_capacity : 0;
    if (overflow) {
        beginRemoveRows({}, 0, int(overflow) - 1);
        m_rows.erase(m_rows.begin(), m_rows.begin() + long(overflow));
        endRemoveRows();
        m_evicted += overflow;
    }
    m_evicted += frames.size() - keep;
    const int first = int(m_rows.size());
    beginInsertRows({}, first, first + int(keep) - 1);
    m_rows.insert(m_rows.end(), frames.end() - long(keep), frames.end());
    endInsertRows();
}

void FrameModel::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
}
