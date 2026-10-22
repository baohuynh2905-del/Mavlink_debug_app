#pragma once

#include "flow_stats.h"

#include <QHash>
#include <QWidget>

class MavlinkDict;
class QLabel;
class QStandardItem;
class QStandardItemModel;
class QTableView;

class StatsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StatsWidget(const MavlinkDict *dict, QWidget *parent = nullptr);
    void addFrames(const std::vector<CapturedFrame> &frames);
    const FlowStats &flows() const { return m_flows; }

private:
    void refresh();  // 1 Hz
    void reset();
    QString componentName(quint8 compid) const;

    const MavlinkDict *m_dict;
    FlowStats m_flows;
    TrafficStats m_totals;
    qint64 m_lastSampleNs = -1;

    QLabel *m_totalsLabel;
    QStandardItemModel *m_msgModel, *m_srcModel;
    QTableView *m_msgView, *m_srcView;
    int m_sizedMsgRows = -1, m_sizedSrcRows = -1;
    std::map<FlowKey, QList<QStandardItem *>> m_msgRows;  // rows are updated in place, so sorting survives
    std::map<quint16, QList<QStandardItem *>> m_srcRows;
};
