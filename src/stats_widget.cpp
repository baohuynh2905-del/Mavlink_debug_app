#include "stats_widget.h"

#include "mavlink_dict.h"
#include "payload_link.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

static QTableView *makeView(QStandardItemModel *m)
{
    auto *v = new QTableView;
    v->setModel(m);
    v->setSortingEnabled(true);
    v->setAlternatingRowColors(true);
    v->setSelectionBehavior(QAbstractItemView::SelectRows);
    v->verticalHeader()->hide();
    v->verticalHeader()->setDefaultSectionSize(v->fontMetrics().height() + 4);
    v->horizontalHeader()->setStretchLastSection(true);
    return v;
}

static QList<QStandardItem *> newRow(int columns, QList<int> textColumns)
{
    QList<QStandardItem *> row;
    for (int i = 0; i < columns; ++i) {
        auto *it = new QStandardItem;
        it->setEditable(false);
        if (!textColumns.contains(i)) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row << it;
    }
    return row;
}

StatsWidget::StatsWidget(const MavlinkDict *dict, QWidget *parent) : QWidget(parent), m_dict(dict)
{
    m_totalsLabel = new QLabel;
    m_totalsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *resetBtn = new QPushButton(QStringLiteral("Reset statistics"));

    m_msgModel = new QStandardItemModel(0, 10, this);
    m_msgModel->setHorizontalHeaderLabels({QStringLiteral("Dir"), QStringLiteral("Sys"), QStringLiteral("Comp"),
                                           QStringLiteral("Component"), QStringLiteral("Msg ID"), QStringLiteral("Message"),
                                           QStringLiteral("Count"), QStringLiteral("Rate (Hz)"), QStringLiteral("Bytes"),
                                           QStringLiteral("Last seen (s ago)")});
    m_srcModel = new QStandardItemModel(0, 8, this);
    m_srcModel->setHorizontalHeaderLabels({QStringLiteral("Sys"), QStringLiteral("Comp"), QStringLiteral("Component"),
                                           QStringLiteral("RX frames"), QStringLiteral("Last seq"), QStringLiteral("Seq gaps"),
                                           QStringLiteral("Seq skipped (max lost)"), QStringLiteral("Seq backwards")});
    m_msgView = makeView(m_msgModel);
    m_msgView->sortByColumn(6, Qt::DescendingOrder);
    m_srcView = makeView(m_srcModel);

    auto *note = new QLabel(QStringLiteral(
        "Sequence numbers are checked on received frames only, per sysid/compid. A forward gap means frames "
        "<i>may</i> have been lost (UDP drop, or the sender skipped numbers). A backwards jump means a restart, "
        "reordering, or a sender using several counters. The SDK itself sends that way, so TX is not checked."));
    note->setWordWrap(true);

    auto *top = new QHBoxLayout;
    top->addWidget(m_totalsLabel, 1);
    top->addWidget(resetBtn);

    auto *srcBox = new QWidget;
    auto *srcLayout = new QVBoxLayout(srcBox);
    srcLayout->setContentsMargins(0, 0, 0, 0);
    srcLayout->addWidget(new QLabel(QStringLiteral("<b>RX sources</b>")));
    srcLayout->addWidget(m_srcView);
    srcLayout->addWidget(note);

    auto *msgBox = new QWidget;
    auto *msgLayout = new QVBoxLayout(msgBox);
    msgLayout->setContentsMargins(0, 0, 0, 0);
    msgLayout->addWidget(new QLabel(QStringLiteral("<b>Messages per direction and component</b>")));
    msgLayout->addWidget(m_msgView);

    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(msgBox);
    split->addWidget(srcBox);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(split, 1);

    connect(resetBtn, &QPushButton::clicked, this, &StatsWidget::reset);
    auto *tick = new QTimer(this);
    connect(tick, &QTimer::timeout, this, &StatsWidget::refresh);
    tick->start(1000);
    refresh();
}

void StatsWidget::addFrames(const std::vector<CapturedFrame> &frames)
{
    for (const CapturedFrame &c : frames) {
        m_flows.add(c);
        m_totals.add(c);
    }
}

void StatsWidget::reset()
{
    m_flows.clear();
    m_totals = {};
    m_msgRows.clear();
    m_srcRows.clear();
    m_msgModel->removeRows(0, m_msgModel->rowCount());
    m_srcModel->removeRows(0, m_srcModel->rowCount());
    m_lastSampleNs = -1;
    refresh();
}

QString StatsWidget::componentName(quint8 compid) const
{
    return m_dict ? m_dict->enumLabel(QStringLiteral("MAV_COMPONENT"), compid) : QString();
}

void StatsWidget::refresh()
{
    const qint64 now = PayloadLink::monoNs();
    if (m_lastSampleNs >= 0) m_flows.sampleRates(double(now - m_lastSampleNs) / 1e9);
    m_lastSampleNs = now;

    auto ago = [now](qint64 ns) { return ns < 0 ? QVariant() : QVariant(double((now - ns) / 100000000) / 10.0); };

    for (const auto &[key, fc] : m_flows.flows) {
        auto it = m_msgRows.find(key);
        if (it == m_msgRows.end()) {
            QList<QStandardItem *> row = newRow(10, {0, 3, 5});
            row[0]->setText(key.dir == CapturedFrame::Tx ? QStringLiteral("TX") : QStringLiteral("RX"));
            row[1]->setData(key.sysid, Qt::DisplayRole);
            row[2]->setData(key.compid, Qt::DisplayRole);
            row[3]->setText(componentName(key.compid));
            row[4]->setData(key.msgid, Qt::DisplayRole);
            const char *n = messageName(key.msgid);
            row[5]->setText(n ? QString::fromLatin1(n) : QStringLiteral("(unknown)"));
            m_msgModel->appendRow(row);
            it = m_msgRows.emplace(key, row).first;
        }
        it->second[6]->setData(fc.count, Qt::DisplayRole);
        it->second[7]->setData(qRound(fc.rateHz * 10) / 10.0, Qt::DisplayRole);
        it->second[8]->setData(fc.bytes, Qt::DisplayRole);
        it->second[9]->setData(ago(fc.lastNs), Qt::DisplayRole);
    }

    for (const auto &[key, s] : m_flows.rxSources) {
        auto it = m_srcRows.find(key);
        if (it == m_srcRows.end()) {
            QList<QStandardItem *> row = newRow(8, {2});
            row[0]->setData(key >> 8, Qt::DisplayRole);
            row[1]->setData(key & 0xFF, Qt::DisplayRole);
            row[2]->setText(componentName(quint8(key & 0xFF)));
            m_srcModel->appendRow(row);
            it = m_srcRows.emplace(key, row).first;
        }
        it->second[3]->setData(s.frames, Qt::DisplayRole);
        it->second[4]->setData(s.lastSeq, Qt::DisplayRole);
        it->second[5]->setData(s.gaps, Qt::DisplayRole);
        it->second[6]->setData(s.skipped, Qt::DisplayRole);
        it->second[7]->setData(s.backwards, Qt::DisplayRole);
    }

    if (m_msgModel->rowCount() != m_sizedMsgRows) {  // size columns only when rows appear, not every tick
        m_msgView->resizeColumnsToContents();
        m_sizedMsgRows = m_msgModel->rowCount();
    }
    if (m_srcModel->rowCount() != m_sizedSrcRows) {
        m_srcView->resizeColumnsToContents();
        m_sizedSrcRows = m_srcModel->rowCount();
    }

    const TrafficStats &t = m_totals;
    m_totalsLabel->setText(
        QStringLiteral("RX %1 frames / %2 bytes  |  TX %3 frames / %4 bytes  |  bad CRC %5  |  unknown ID %6  |  "
                       "truncated %7  |  junk runs %8  |  send errors %9")
            .arg(t.frames[0]).arg(t.bytes[0]).arg(t.frames[1]).arg(t.bytes[1]).arg(t.badCrc).arg(t.unknownId)
            .arg(t.truncated).arg(t.garbage).arg(t.sendErrors));
}
