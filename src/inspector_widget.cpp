#include "inspector_widget.h"

#include "frame_filter.h"
#include "frame_model.h"
#include "frame_text.h"
#include "frame_writer.h"
#include "mavlink_dict.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTableView>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

static const char *kFileFilters = "MAVLink telemetry log (*.tlog);;CSV (*.csv);;JSON Lines (*.jsonl)";

static QString withSuffix(QString path, const QString &selectedFilter)
{
    if (!QFileInfo(path).suffix().isEmpty()) return path;
    if (selectedFilter.contains(QLatin1String("*.csv"))) return path + QStringLiteral(".csv");
    if (selectedFilter.contains(QLatin1String("*.jsonl"))) return path + QStringLiteral(".jsonl");
    return path + QStringLiteral(".tlog");
}

InspectorWidget::InspectorWidget(const MavlinkDict *dict, QWidget *parent)
    : QWidget(parent), m_dict(dict), m_model(new FrameModel(100000, this)), m_filter(new FrameFilter(m_model, this))
{
    // --- Filter bar ---
    m_dir = new QComboBox;
    m_dir->addItems({QStringLiteral("RX + TX"), QStringLiteral("RX"), QStringLiteral("TX")});
    auto byteEdit = [](const QString &tip) {
        auto *e = new QLineEdit;
        e->setValidator(new QIntValidator(0, 255, e));
        e->setPlaceholderText(QStringLiteral("any"));
        e->setMaximumWidth(48);
        e->setToolTip(tip);
        return e;
    };
    m_sys = byteEdit(QStringLiteral("System ID (0-255), empty = any"));
    m_comp = byteEdit(QStringLiteral("Component ID (0-255), empty = any"));
    m_message = new QLineEdit;
    m_message->setObjectName(QStringLiteral("messageFilter"));
    m_message->setPlaceholderText(QStringLiteral("name or ID"));
    m_message->setToolTip(QStringLiteral("A number matches the message ID exactly; text matches part of the name"));
    m_message->setMaximumWidth(170);
    m_text = new QLineEdit;
    m_text->setPlaceholderText(QStringLiteral("search decoded values"));
    m_text->setToolTip(QStringLiteral("Case-insensitive search in the decoded field text, e.g. MAV_CMD_ or yaw"));
    m_status = new QComboBox;
    m_status->addItems({QStringLiteral("Any status"), QStringLiteral("Valid only"),
                        QStringLiteral("Problems (CRC, truncated, junk, send errors)"), QStringLiteral("Unknown ID")});

    auto *filters = new QHBoxLayout;
    filters->addWidget(m_dir);
    filters->addWidget(new QLabel(QStringLiteral("Sys")));
    filters->addWidget(m_sys);
    filters->addWidget(new QLabel(QStringLiteral("Comp")));
    filters->addWidget(m_comp);
    filters->addWidget(new QLabel(QStringLiteral("Message")));
    filters->addWidget(m_message);
    filters->addWidget(m_text, 1);
    filters->addWidget(m_status);

    // --- Actions ---
    m_pauseBtn = new QPushButton(QStringLiteral("Pause display"));
    m_pauseBtn->setObjectName(QStringLiteral("pauseButton"));
    m_pauseBtn->setCheckable(true);
    m_pauseBtn->setToolTip(QStringLiteral("Freezes the table only. Capture, statistics and recording continue."));
    m_autoScroll = new QCheckBox(QStringLiteral("Auto-scroll"));
    m_autoScroll->setChecked(true);
    auto *clearBtn = new QPushButton(QStringLiteral("Clear table"));
    clearBtn->setToolTip(QStringLiteral("Clears the displayed history only; capture continues."));
    auto *exportBtn = new QPushButton(QStringLiteral("Export visible…"));
    exportBtn->setToolTip(QStringLiteral("Writes the rows that pass the current filter (tlog, CSV or JSON Lines)."));
    m_recordBtn = new QPushButton(QStringLiteral("Record…"));
    m_recordBtn->setCheckable(true);
    m_recordBtn->setToolTip(QStringLiteral("Continuously writes every captured frame to a file, unfiltered."));
    m_statusLabel = new QLabel;
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *actions = new QHBoxLayout;
    actions->addWidget(m_pauseBtn);
    actions->addWidget(m_autoScroll);
    actions->addWidget(clearBtn);
    actions->addWidget(exportBtn);
    actions->addWidget(m_recordBtn);
    actions->addWidget(m_statusLabel, 1);

    // --- Table + details ---
    m_table = new QTableView;
    m_table->setObjectName(QStringLiteral("frameTable"));
    m_table->setModel(m_filter);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 4);
    m_table->horizontalHeader()->setStretchLastSection(true);
    // Fixed initial widths: resizeColumnsToContents() would scan up to 100k rows.
    const char *samples[FrameModel::Summary] = {"0000000", "00:00:00.000", "TX", "255.255.255.255:65535", "Ver",
                                                "255", "Comp", "255", "Msg ID", "GIMBAL_DEVICE_ATTITUDE_STATUS",
                                                "Len", "255/255", "UNKNOWN_ID"};
    for (int i = 0; i < FrameModel::Summary; ++i)
        m_table->setColumnWidth(i, m_table->fontMetrics().horizontalAdvance(QString::fromLatin1(samples[i])) + 16);

    m_fields = new QTreeWidget;
    m_fields->setColumnCount(4);
    m_fields->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value"), QStringLiteral("Type"), QStringLiteral("Units")});
    m_fields->setAlternatingRowColors(true);
    m_fields->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_fields->setColumnWidth(0, 200);
    m_fields->setColumnWidth(1, 330);
    m_hex = new QPlainTextEdit;
    m_hex->setReadOnly(true);
    m_hex->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_hex->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_hex->setPlaceholderText(QStringLiteral("Select a row to see its raw bytes"));
    m_hex->setMinimumWidth(QFontMetrics(m_hex->font()).horizontalAdvance(QString(76, QLatin1Char('0'))) + 24);

    auto *details = new QSplitter(Qt::Horizontal);
    details->addWidget(m_fields);
    details->addWidget(m_hex);
    details->setStretchFactor(0, 3);
    details->setStretchFactor(1, 2);
    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(m_table);
    split->addWidget(details);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(filters);
    layout->addLayout(actions);
    layout->addWidget(split, 1);

    // Restore the last filters (validated by the widgets themselves)
    QSettings s;
    m_dir->setCurrentIndex(qBound(0, s.value(QStringLiteral("inspector/dir")).toInt(), 2));
    m_status->setCurrentIndex(qBound(0, s.value(QStringLiteral("inspector/status")).toInt(), 3));
    m_message->setText(s.value(QStringLiteral("inspector/message")).toString());
    m_text->setText(s.value(QStringLiteral("inspector/text")).toString());
    m_autoScroll->setChecked(s.value(QStringLiteral("inspector/autoScroll"), true).toBool());

    m_filterDelay = new QTimer(this);
    m_filterDelay->setSingleShot(true);
    m_filterDelay->setInterval(250);  // re-filtering 100k rows on every keystroke would stall typing
    connect(m_filterDelay, &QTimer::timeout, this, &InspectorWidget::applyFilter);
    for (QLineEdit *e : {m_sys, m_comp, m_message, m_text})
        connect(e, &QLineEdit::textChanged, m_filterDelay, qOverload<>(&QTimer::start));
    connect(m_dir, &QComboBox::currentIndexChanged, this, &InspectorWidget::applyFilter);
    connect(m_status, &QComboBox::currentIndexChanged, this, &InspectorWidget::applyFilter);

    connect(m_pauseBtn, &QPushButton::toggled, this, &InspectorWidget::setPaused);
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        m_model->clear();
        m_held.clear();
        showDetails();
        refreshStatus();
    });
    connect(exportBtn, &QPushButton::clicked, this, &InspectorWidget::exportVisible);
    connect(m_recordBtn, &QPushButton::clicked, this, &InspectorWidget::onRecordClicked);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &InspectorWidget::showDetails);

    auto *statusTick = new QTimer(this);
    connect(statusTick, &QTimer::timeout, this, &InspectorWidget::refreshStatus);
    statusTick->start(1000);

    applyFilter();
}

InspectorWidget::~InspectorWidget()
{
    QSettings s;
    s.setValue(QStringLiteral("inspector/dir"), m_dir->currentIndex());
    s.setValue(QStringLiteral("inspector/status"), m_status->currentIndex());
    s.setValue(QStringLiteral("inspector/message"), m_message->text());
    s.setValue(QStringLiteral("inspector/text"), m_text->text());
    s.setValue(QStringLiteral("inspector/autoScroll"), m_autoScroll->isChecked());
    delete m_recorder;  // flushes and closes the recording
}

void InspectorWidget::addFrames(const std::vector<CapturedFrame> &frames)
{
    if (m_recorder) m_recorder->write(frames);
    if (m_paused) {
        m_held.insert(m_held.end(), frames.begin(), frames.end());
        if (m_held.size() > m_model->capacity()) {  // these would have been evicted from the table anyway
            const size_t extra = m_held.size() - m_model->capacity();
            m_held.erase(m_held.begin(), m_held.begin() + long(extra));
            m_heldEvicted += extra;
        }
        return;
    }
    m_model->append(frames);
    if (m_autoScroll->isChecked()) m_table->scrollToBottom();
}

bool InspectorWidget::selectFrame(quint64 frameIndex)
{
    m_pauseBtn->setChecked(false);  // resumes via toggled(): the frame may still be held
    for (int r = m_filter->rowCount() - 1; r >= 0; --r) {
        const quint64 idx = m_filter->frameAt(r).index;
        if (idx == frameIndex) {
            m_autoScroll->setChecked(false);  // keep the selected frame in view
            m_table->setCurrentIndex(m_filter->index(r, 0));
            m_table->scrollTo(m_filter->index(r, 0), QAbstractItemView::PositionAtCenter);
            return true;
        }
        if (idx < frameIndex) break;  // rows are in observation order
    }
    return false;
}

void InspectorWidget::applyFilter()
{
    FrameCriteria c;
    c.dir = m_dir->currentIndex() - 1;
    c.sysid = m_sys->text().isEmpty() ? -1 : m_sys->text().toInt();
    c.compid = m_comp->text().isEmpty() ? -1 : m_comp->text().toInt();
    c.message = m_message->text().trimmed();
    c.text = m_text->text().trimmed();
    c.status = FrameCriteria::Status(m_status->currentIndex());
    m_filter->setCriteria(c);
    refreshStatus();
}

void InspectorWidget::setPaused(bool paused)
{
    m_paused = paused;
    m_pauseBtn->setText(paused ? QStringLiteral("Resume display") : QStringLiteral("Pause display"));
    if (!paused) {
        m_model->append(m_held);
        m_held.clear();
        if (m_autoScroll->isChecked()) m_table->scrollToBottom();
    }
    refreshStatus();
}

static QTreeWidgetItem *addRow(QTreeWidgetItem *parent, const QString &name, const QString &value,
                               const QString &type = {}, const QString &units = {}, const QString &tip = {})
{
    auto *it = new QTreeWidgetItem(parent, {name, value, type, units});
    if (!tip.isEmpty()) for (int i = 0; i < 4; ++i) it->setToolTip(i, tip);
    return it;
}

// Marks a value equal to the field's documented "invalid" sentinel (e.g. NaN = unknown).
static bool isInvalidSentinel(const MavField &f, const QString &invalid)
{
    if (invalid.isEmpty() || f.isText) return false;
    if (invalid.compare(QLatin1String("NaN"), Qt::CaseInsensitive) == 0)
        return std::any_of(f.reals.begin(), f.reals.end(), [](double d) { return std::isnan(d); });
    bool ok = false;
    const qint64 v = invalid.toLongLong(&ok, 0);
    return ok && f.ints.size() == 1 && f.ints[0] == v;
}

void InspectorWidget::showDetails()
{
    m_fields->clear();
    m_hex->clear();
    const QModelIndex cur = m_table->currentIndex();
    if (!cur.isValid()) return;
    const CapturedFrame &c = m_filter->frameAt(cur.row());
    const MavFrame &f = c.frame;

    auto *validation = new QTreeWidgetItem(m_fields, {QStringLiteral("Validation"), QString::fromLatin1(statusName(f.status))});
    for (const QString &line : validationText(c).split(QLatin1Char('\n')))
        addRow(validation, {}, line);

    if (f.status == MavFrame::Ok) {
        const char *name = messageName(f.msgid);
        auto *fields = new QTreeWidgetItem(m_fields, {QStringLiteral("Fields: %1").arg(QString::fromLatin1(name ? name : "?"))});
        if (m_dict) fields->setToolTip(0, m_dict->messageDescription(f.msgid));
        for (const MavField &x : decodeFields(f)) {
            const MavlinkDict::Field *meta = m_dict ? m_dict->field(f.msgid, QString::fromStdString(x.name)) : nullptr;
            QString value = labelledValue(x, f.msgid, m_dict);
            if (meta && isInvalidSentinel(x, meta->invalid)) value += QStringLiteral("  (invalid/unknown marker)");
            QString type = QString::fromStdString(x.type);
            if (x.extension) type += QStringLiteral(" (extension)");
            addRow(fields, QString::fromStdString(x.name), value, type, meta ? meta->units : QString(),
                   meta ? meta->description : QString());
        }
    } else if (f.headerComplete) {
        new QTreeWidgetItem(m_fields, {QStringLiteral("Fields"), QStringLiteral("not decoded (see Validation)")});
    }
    if (f.headerComplete) {
        auto *hdr = new QTreeWidgetItem(m_fields, {QStringLiteral("Header")});
        addRow(hdr, QStringLiteral("Version"), f.version == 2 ? QStringLiteral("MAVLink 2 (STX 0xFD)") : QStringLiteral("MAVLink 1 (STX 0xFE)"));
        if (f.version == 2) {
            addRow(hdr, QStringLiteral("incompat_flags"), QStringLiteral("0x%1%2").arg(f.incompatFlags, 2, 16, QLatin1Char('0'))
                       .arg(f.signaturePresent ? QStringLiteral(" (MAVLINK_IFLAG_SIGNED)") : QString()), QStringLiteral("uint8_t"));
            addRow(hdr, QStringLiteral("compat_flags"), QStringLiteral("0x%1").arg(f.compatFlags, 2, 16, QLatin1Char('0')), QStringLiteral("uint8_t"));
        }
        addRow(hdr, QStringLiteral("seq"), QString::number(f.seq), QStringLiteral("uint8_t"));
        addRow(hdr, QStringLiteral("sysid"), QString::number(f.sysid), QStringLiteral("uint8_t"));
        const QString compName = m_dict ? m_dict->enumLabel(QStringLiteral("MAV_COMPONENT"), f.compid) : QString();
        addRow(hdr, QStringLiteral("compid"), compName.isEmpty() ? QString::number(f.compid) : QStringLiteral("%1 (%2)").arg(compName).arg(f.compid),
               QStringLiteral("uint8_t"));
        const char *name = messageName(f.msgid);
        addRow(hdr, QStringLiteral("msgid"), QStringLiteral("%1 (%2)").arg(name ? QString::fromLatin1(name) : QStringLiteral("unknown")).arg(f.msgid),
               f.version == 2 ? QStringLiteral("uint24_t") : QStringLiteral("uint8_t"));
        addRow(hdr, QStringLiteral("len"), QString::number(f.len), QStringLiteral("uint8_t"), QStringLiteral("bytes"),
               QStringLiteral("Payload length on the wire (MAVLink 2 trims trailing zero bytes)"));
        addRow(hdr, QStringLiteral("checksum"), QStringLiteral("0x%1").arg(f.crcWire, 4, 16, QLatin1Char('0')), QStringLiteral("uint16_t"));
        if (f.targetSystem >= 0)
            addRow(hdr, QStringLiteral("target"), f.targetComponent >= 0 ? QStringLiteral("%1/%2").arg(f.targetSystem).arg(f.targetComponent)
                                                                          : QString::number(f.targetSystem), {}, {},
                   QStringLiteral("target_system / target_component fields of the payload"));
    }

    auto *frame = new QTreeWidgetItem(m_fields, {QStringLiteral("Frame #%1").arg(c.index)});
    addRow(frame, QStringLiteral("Direction"), c.dir == CapturedFrame::Tx ? QStringLiteral("TX (sent by this app)") : QStringLiteral("RX (received)"));
    addRow(frame, c.dir == CapturedFrame::Tx ? QStringLiteral("Destination") : QStringLiteral("Source"), endpointText(c.peerIp, c.peerPort));
    addRow(frame, QStringLiteral("Time (UTC)"), isoTime(c.wallUs));
    addRow(frame, QStringLiteral("UDP datagram"), QStringLiteral("#%1").arg(c.datagram), {}, {},
           QStringLiteral("Frames that arrived in the same datagram share this number"));
    addRow(frame, QStringLiteral("Bytes"), QString::number(f.raw.size()));

    m_fields->expandAll();
    frame->setExpanded(false);  // rarely needed; keeps the decoded fields above the fold
    m_hex->setPlainText(hexDump(f));
}

void InspectorWidget::exportVisible()
{
    QString selected;
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export visible rows"), m_lastExport,
                                                QString::fromLatin1(kFileFilters), &selected);
    if (path.isEmpty()) return;
    path = withSuffix(path, selected);
    m_lastExport = path;

    std::vector<CapturedFrame> rows;
    rows.reserve(size_t(m_filter->rowCount()));
    for (int r = 0; r < m_filter->rowCount(); ++r) rows.push_back(m_filter->frameAt(r));

    auto *w = new FrameWriter(this);
    QString err;
    if (!w->start(path, FrameWriter::formatForPath(path), &err)) {
        delete w;
        QMessageBox::warning(this, QStringLiteral("Export failed"), QStringLiteral("%1\n%2").arg(path, err));
        return;
    }
    const size_t n = rows.size();
    connect(w, &FrameWriter::failed, this, [this, path](const QString &e) {
        QMessageBox::warning(this, QStringLiteral("Export failed"), QStringLiteral("%1\n%2").arg(path, e));
    });
    connect(w, &FrameWriter::finished, this, [this, w, path, n] {
        m_note = QStringLiteral("Exported %1 of %2 rows to %3%4").arg(w->written()).arg(n).arg(path.toHtmlEscaped())
                     .arg(w->skipped() ? QStringLiteral(" (%1 without a complete header skipped)").arg(w->skipped()) : QString());
        w->deleteLater();
        refreshStatus();
    });
    w->write(std::move(rows));
    w->stop();
    m_note = QStringLiteral("Exporting %1 rows…").arg(n);
    refreshStatus();
}

bool InspectorWidget::startRecording(const QString &path, QString *error)
{
    auto *w = new FrameWriter(this);
    if (!w->start(path, FrameWriter::formatForPath(path), error)) {
        delete w;
        return false;
    }
    connect(w, &FrameWriter::failed, this, [this, path](const QString &e) {
        stopRecording();
        QMessageBox::warning(this, QStringLiteral("Recording stopped"), QStringLiteral("Write error on %1:\n%2").arg(path, e));
    });
    m_recorder = w;
    m_recordBtn->setChecked(true);
    m_recordBtn->setText(QStringLiteral("Stop recording"));
    refreshStatus();
    return true;
}

void InspectorWidget::stopRecording()
{
    if (!m_recorder) return;
    FrameWriter *w = m_recorder;
    m_recorder = nullptr;
    connect(w, &FrameWriter::finished, w, &QObject::deleteLater);
    w->stop();
    m_recordBtn->setChecked(false);
    m_recordBtn->setText(QStringLiteral("Record…"));
    refreshStatus();
}

void InspectorWidget::onRecordClicked(bool checked)
{
    if (!checked) {
        stopRecording();
        return;
    }
    m_recordBtn->setChecked(false);  // only checked once a file is actually open
    QString selected;
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Record all captured frames"), m_lastExport,
                                                QString::fromLatin1(kFileFilters), &selected);
    if (path.isEmpty()) return;
    path = withSuffix(path, selected);
    m_lastExport = path;
    QString err;
    if (!startRecording(path, &err))
        QMessageBox::warning(this, QStringLiteral("Cannot record"), QStringLiteral("%1\n%2").arg(path, err));
}

void InspectorWidget::refreshStatus()
{
    QStringList parts;
    parts << QStringLiteral("Showing %1 of %2 rows").arg(m_filter->rowCount()).arg(m_model->rowCount());
    if (m_model->evicted() || m_heldEvicted)
        parts << QStringLiteral("%1 oldest evicted (limit %2)").arg(m_model->evicted() + m_heldEvicted).arg(m_model->capacity());
    if (m_paused) parts << QStringLiteral("<b>DISPLAY PAUSED</b>, %1 new frames held, capture continues").arg(m_held.size());
    if (m_recorder) {
        parts << QStringLiteral("<span style='color:#c01c28'>&#9679;</span> REC %1: %2 frames, %3 KiB%4")
                     .arg(QFileInfo(m_recorder->path()).fileName().toHtmlEscaped()).arg(m_recorder->written())
                     .arg(m_recorder->bytes() / 1024)
                     .arg(m_recorder->dropped() ? QStringLiteral(", <b>%1 dropped (disk too slow)</b>").arg(m_recorder->dropped()) : QString());
    }
    if (!m_note.isEmpty()) parts << m_note;
    m_statusLabel->setText(parts.join(QStringLiteral("  |  ")));
}
