#pragma once

#include "captured_frame.h"

#include <QAbstractTableModel>

#include <deque>

// Bounded history of captured frames: the oldest rows are evicted beyond the capacity.
class FrameModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { Index, Time, Dir, Peer, Version, SysId, CompId, Seq, MsgId, Name, Len, Target, Status, Summary, ColumnCount };

    explicit FrameModel(size_t capacity = 100000, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

    void append(const std::vector<CapturedFrame> &frames);
    void clear();
    const CapturedFrame &frameAt(int row) const { return m_rows[size_t(row)]; }
    quint64 evicted() const { return m_evicted; }
    size_t capacity() const { return m_capacity; }

private:
    std::deque<CapturedFrame> m_rows;
    size_t m_capacity;
    quint64 m_evicted = 0;
};

QString endpointText(quint32 ip, quint16 port);
