#pragma once

#include "captured_frame.h"

#include <QWidget>

class FrameFilter;
class FrameModel;
class FrameWriter;
class PcapCapture;
class MavlinkDict;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableView;
class QTimer;
class QTreeWidget;

class InspectorWidget : public QWidget
{
    Q_OBJECT
public:
    explicit InspectorWidget(const MavlinkDict *dict, QWidget *parent = nullptr);
    ~InspectorWidget() override;

    void addFrames(const std::vector<CapturedFrame> &frames);  // always recorded, displayed unless paused
    PcapCapture *capture() const { return m_capture; }

    // Collapsed: only the header strip (title, direction, filters, pause) stays visible.
    void setCollapsed(bool collapsed);
    bool isCollapsed() const;

signals:
    void networkFrames(const std::vector<CapturedFrame> &frames);  // from network capture, for the statistics
    void collapsedChanged(bool collapsed);

public:
    FrameModel *model() const { return m_model; }
    FrameFilter *filter() const { return m_filter; }
    bool startRecording(const QString &path, QString *error);  // also used by tests
    bool selectFrame(quint64 frameIndex);  // false if evicted or hidden by the current filter
    void stopRecording();

private:
    void applyFilter();
    void showDetails();
    void setPaused(bool paused);
    void exportVisible();
    void onRecordClicked(bool checked);
    void refreshStatus();
    void onCaptureClicked();
    void onOpenPcap();
    void onNetworkFrames(const std::vector<CapturedFrame> &frames);

    const MavlinkDict *m_dict;
    FrameModel *m_model;
    FrameFilter *m_filter;
    FrameWriter *m_recorder = nullptr;
    PcapCapture *m_capture;
    QComboBox *m_iface;
    QLineEdit *m_bpf;
    QPushButton *m_captureBtn;
    QLabel *m_captureStatus;

    QButtonGroup *m_dir;  // ids: 0 all, then CapturedFrame::Dir + 1
    QComboBox *m_status;
    QLineEdit *m_sys, *m_comp, *m_message, *m_text;
    QPushButton *m_pauseBtn, *m_recordBtn, *m_detailsBtn, *m_toolsBtn, *m_collapseBtn;
    QWidget *m_body, *m_tools, *m_details;
    QCheckBox *m_autoScroll;
    QTableView *m_table;
    QTreeWidget *m_fields;
    QPlainTextEdit *m_hex;
    QLabel *m_statusLabel;
    QTimer *m_filterDelay;

    bool m_paused = false;
    std::vector<CapturedFrame> m_held;  // frames received while the display is paused
    quint64 m_heldEvicted = 0;
    QString m_lastExport;
    QString m_note;  // result of the last export, kept in the status line
};
