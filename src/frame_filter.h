#pragma once

#include "captured_frame.h"

#include <QSortFilterProxyModel>

struct FrameCriteria {
    enum Status { AnyStatus, ValidOnly, ProblemsOnly, UnknownOnly };
    int dir = -1;      // -1 any, else CapturedFrame::Dir
    int sysid = -1;    // -1 any
    int compid = -1;   // -1 any
    QString message;   // numeric = exact msg id, otherwise case-insensitive part of the name
    QString text;      // case-insensitive part of the decoded summary
    Status status = AnyStatus;

    bool matches(const CapturedFrame &c) const;
};

class FrameModel;

// Filters the bounded frame table; rows stay in observation order (no sorting).
class FrameFilter : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit FrameFilter(FrameModel *source, QObject *parent = nullptr);
    void setCriteria(const FrameCriteria &c);
    const FrameCriteria &criteria() const { return m_c; }
    const CapturedFrame &frameAt(int proxyRow) const;

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
    FrameModel *m_source;
    FrameCriteria m_c;
};
