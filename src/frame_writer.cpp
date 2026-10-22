#include "frame_writer.h"

#include "frame_text.h"

#include <QtEndian>

FrameWriter::Format FrameWriter::formatForPath(const QString &path)
{
    if (path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) return Csv;
    if (path.endsWith(QLatin1String(".jsonl"), Qt::CaseInsensitive)) return JsonLines;
    return Tlog;
}

FrameWriter::FrameWriter(QObject *parent) : QObject(parent)
{
    m_ctx.moveToThread(&m_thread);
    m_thread.start();
}

FrameWriter::~FrameWriter()
{
    stop();
    // Posted work runs in order: this returns once every queued batch is written and the file closed.
    QMetaObject::invokeMethod(&m_ctx, [] {}, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

bool FrameWriter::start(const QString &path, Format format, QString *error)
{
    if (m_active || m_file.isOpen()) return false;  // one file per writer instance
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = m_file.errorString();
        return false;
    }
    m_path = path;
    m_format = format;
    m_failed = false;
    m_written = m_skipped = m_dropped = m_backlog = 0;
    m_bytes = 0;
    if (format == Csv) {
        const QByteArray h = csvHeader().toUtf8() + '\n';
        m_bytes += m_file.write(h);
    }
    // From here on only the writer thread touches m_file; queued posts order the accesses.
    m_active = true;
    return true;
}

void FrameWriter::write(std::vector<CapturedFrame> frames)
{
    if (!m_active || frames.empty() || m_failed) return;
    if (m_backlog + frames.size() > kMaxBacklog) {
        m_dropped += frames.size();
        return;
    }
    m_backlog += frames.size();
    QMetaObject::invokeMethod(&m_ctx, [this, f = std::move(frames)] { writeBatch(f); }, Qt::QueuedConnection);
}

void FrameWriter::stop()
{
    if (!m_active) return;
    m_active = false;
    QMetaObject::invokeMethod(&m_ctx, [this] {
        m_file.close();
        emit finished();
    }, Qt::QueuedConnection);
}

void FrameWriter::writeBatch(const std::vector<CapturedFrame> &frames)
{
    m_backlog -= frames.size();
    if (m_failed || !m_file.isOpen()) return;

    QByteArray out;
    for (const CapturedFrame &c : frames) {
        switch (m_format) {
        case Csv: out += csvRow(c).toUtf8() + '\n'; break;
        case JsonLines: out += jsonLine(c) + '\n'; break;
        case Tlog: {
            if (!c.frame.headerComplete || c.frame.status == MavFrame::Truncated) {
                ++m_skipped;
                continue;
            }
            char ts[8];
            qToBigEndian<quint64>(quint64(c.wallUs), ts);
            out.append(ts, 8);
            out.append(reinterpret_cast<const char *>(c.frame.raw.data()), int(c.frame.raw.size()));
            break;
        }
        }
        ++m_written;
    }
    if (m_file.write(out) != out.size() || !m_file.flush()) {
        m_failed = true;
        const QString err = m_file.errorString();
        m_file.close();
        emit failed(err);  // queued to GUI-thread receivers
        return;
    }
    m_bytes += out.size();
}
