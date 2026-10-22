#pragma once
// Writes captured frames to disk on its own thread (export and continuous recording).
//   CSV        - one row per frame, see csvHeader()
//   JSON Lines - one object per frame, decoded fields typed
//   tlog       - MAVLink telemetry log: per frame an 8-byte big-endian UTC timestamp in µs followed
//                by the raw frame. Readable by pymavlink / MAVExplorer / Mission Planner. Contains only
//                frames with a complete header (no junk bytes) and has no direction column.

#include "captured_frame.h"

#include <QFile>
#include <QObject>
#include <QThread>

#include <atomic>
#include <vector>

class FrameWriter : public QObject
{
    Q_OBJECT
public:
    enum Format { Csv, JsonLines, Tlog };
    static Format formatForPath(const QString &path);  // by extension: .csv, .jsonl, otherwise .tlog
    static constexpr quint64 kMaxBacklog = 500000;     // frames queued but not yet written

    explicit FrameWriter(QObject *parent = nullptr);
    ~FrameWriter() override;  // stops and flushes

    bool start(const QString &path, Format format, QString *error);  // once per instance
    void write(std::vector<CapturedFrame> frames);  // non-blocking
    void stop();                                    // flush + close; emits finished()

    bool isActive() const { return m_active; }
    QString path() const { return m_path; }
    quint64 written() const { return m_written; }
    quint64 skipped() const { return m_skipped; }  // tlog: frames without a complete header
    quint64 dropped() const { return m_dropped; }  // backlog overflow
    qint64 bytes() const { return m_bytes; }

signals:
    void failed(const QString &error);  // file is closed afterwards
    void finished();

private:
    void writeBatch(const std::vector<CapturedFrame> &frames);  // writer thread

    QThread m_thread;
    QObject m_ctx;  // lives in m_thread; work is posted to it
    QFile m_file;   // after start(), touched only on the writer thread
    Format m_format = Csv;
    QString m_path;
    bool m_active = false;  // GUI thread
    std::atomic<bool> m_failed{false};
    std::atomic<quint64> m_written{0}, m_skipped{0}, m_dropped{0}, m_backlog{0};
    std::atomic<qint64> m_bytes{0};
};
