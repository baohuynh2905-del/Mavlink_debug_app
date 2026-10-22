#include "frame_filter.h"

#include "frame_model.h"

bool FrameCriteria::matches(const CapturedFrame &c) const
{
    const MavFrame &f = c.frame;
    if (dir >= 0 && c.dir != dir) return false;
    if ((sysid >= 0 || compid >= 0 || !message.isEmpty()) && !f.headerComplete) return false;
    if (sysid >= 0 && f.sysid != sysid) return false;
    if (compid >= 0 && f.compid != compid) return false;

    switch (status) {
    case AnyStatus: break;
    case ValidOnly: if (f.status != MavFrame::Ok || c.sendErrno) return false; break;
    case ProblemsOnly:
        if (f.status == MavFrame::Ok && !c.sendErrno) return false;
        if (f.status == MavFrame::UnknownId) return false;  // has its own filter
        break;
    case UnknownOnly: if (f.status != MavFrame::UnknownId) return false; break;
    }

    if (!message.isEmpty()) {
        bool numeric = false;
        const uint id = message.toUInt(&numeric);
        if (numeric) {
            if (f.msgid != id) return false;
        } else {
            const char *n = messageName(f.msgid);
            if (!n || !QString::fromLatin1(n).contains(message, Qt::CaseInsensitive)) return false;
        }
    }
    if (!text.isEmpty() && !c.summary.contains(text, Qt::CaseInsensitive)) return false;
    return true;
}

FrameFilter::FrameFilter(FrameModel *source, QObject *parent) : QSortFilterProxyModel(parent), m_source(source)
{
    setSourceModel(source);
    setDynamicSortFilter(true);
}

void FrameFilter::setCriteria(const FrameCriteria &c)
{
    m_c = c;
    invalidateFilter();
}

const CapturedFrame &FrameFilter::frameAt(int proxyRow) const
{
    return m_source->frameAt(mapToSource(index(proxyRow, 0)).row());
}

bool FrameFilter::filterAcceptsRow(int row, const QModelIndex &) const
{
    return m_c.matches(m_source->frameAt(row));
}
