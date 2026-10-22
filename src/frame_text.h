#pragma once
// Text renderings of captured frames, shared by the table, the details pane and the exporters.

#include "captured_frame.h"

#include <QByteArray>
#include <QString>

class MavlinkDict;

// "field=value ..." with enum/bitmask names when the dictionary knows them. Empty unless OK.
QString summarize(const MavFrame &f, const MavlinkDict *dict);

// Field value with its enum/bitmask label, e.g. "MAV_TYPE_GIMBAL (26)".
QString labelledValue(const MavField &f, quint32 msgid, const MavlinkDict *dict);

// Human-readable validation result (CRC, signature, truncation, send errors).
QString validationText(const CapturedFrame &c);

// Hex/ASCII dump plus the byte ranges of header, payload, CRC and signature.
QString hexDump(const MavFrame &f);
QString hexString(const std::vector<uint8_t> &raw);

QString isoTime(qint64 wallUs);  // UTC, microseconds

QString csvHeader();
QString csvRow(const CapturedFrame &c);
QByteArray jsonLine(const CapturedFrame &c);  // one JSON object, no trailing newline
