#pragma once
// Latest decoded telemetry, taken only from received frames that passed CRC validation.

#include "captured_frame.h"

#include <QMap>
#include <QString>

#include <map>
#include <optional>

// Gremsy gimbal modes live in GIMBAL_DEVICE_SET_ATTITUDE/_STATUS flags (SDK examples/gimbal_set_mode.cpp).
namespace GimbalFlags {
constexpr quint16 Mapping = 0x4000;  // Gremsy-specific bit 14; not defined in the MAVLink XML
enum class Change { Lock, Follow, Mapping, MotorsOff, MotorsOn, HomeSet, HomeClear };
quint16 apply(quint16 current, Change c);
QString modeName(quint16 flags);  // same priority order as the SDK's attitude handler
}

struct GimbalAttitude {
    double pitchDeg = 0, rollDeg = 0, yawDeg = 0;
    double rateX = 0, rateY = 0, rateZ = 0;  // deg/s, NaN = unknown
    quint16 flags = 0;
    quint32 failureFlags = 0;
};
bool gimbalAttitude(const MavFrame &f, GimbalAttitude *out);  // GIMBAL_DEVICE_ATTITUDE_STATUS only

std::optional<double> fieldNumber(const MavFrame &f, const char *name, int index = 0);
QString fieldText(const MavFrame &f, const char *name);  // char[] fields, without quotes

class TelemetryStore
{
public:
    struct Value {
        double value;
        qint64 monoNs;
    };
    static constexpr quint8 kPayloadCore = MAV_COMP_ID_USER2;  // source of the SDK's DEBUG parameter stream

    void add(const CapturedFrame &c);
    void clear() { m_last.clear(); m_params.clear(); m_cameraParams.clear(); }

    const CapturedFrame *latest(quint32 msgid) const;                // newest from any component
    const CapturedFrame *latest(quint32 msgid, quint8 compid) const;
    const QMap<int, Value> &payloadParams() const { return m_params; }  // DEBUG.ind -> value
    const QMap<QString, Value> &cameraParams() const { return m_cameraParams; }  // PARAM_EXT_VALUE param_id -> uint32

private:
    std::map<std::pair<quint32, quint8>, CapturedFrame> m_last;
    QMap<int, Value> m_params;
    QMap<QString, Value> m_cameraParams;
};
