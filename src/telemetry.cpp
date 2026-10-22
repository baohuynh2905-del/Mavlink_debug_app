#include "telemetry.h"

#include <cmath>

namespace GimbalFlags {

quint16 apply(quint16 f, Change c)
{
    const quint16 retract = GIMBAL_DEVICE_FLAGS_RETRACT, neutral = GIMBAL_DEVICE_FLAGS_NEUTRAL,
                  yawLock = GIMBAL_DEVICE_FLAGS_YAW_LOCK;
    switch (c) {
    case Change::Lock: return quint16((f | yawLock) & ~(Mapping | neutral | retract));
    case Change::Follow: return quint16(f & ~(yawLock | Mapping | neutral | retract));
    case Change::Mapping: return quint16((f | Mapping) & ~(neutral | retract));
    case Change::MotorsOff: return quint16((f | retract) & ~(Mapping | neutral));
    case Change::MotorsOn: return quint16(f & ~(retract | Mapping | neutral));
    case Change::HomeSet: return quint16((f | neutral) & ~(retract | Mapping));
    case Change::HomeClear: return quint16(f & ~neutral);
    }
    return f;
}

QString modeName(quint16 f)
{
    if (f & GIMBAL_DEVICE_FLAGS_YAW_LOCK) return QStringLiteral("LOCK");
    if (f & GIMBAL_DEVICE_FLAGS_RETRACT) return QStringLiteral("OFF (motors off)");
    if (f & GIMBAL_DEVICE_FLAGS_NEUTRAL) return QStringLiteral("RESET (going home)");
    if (f & Mapping) return QStringLiteral("MAPPING");
    return QStringLiteral("FOLLOW");
}

}  // namespace GimbalFlags

bool gimbalAttitude(const MavFrame &f, GimbalAttitude *out)
{
    if (f.status != MavFrame::Ok || f.msgid != MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS) return false;
    mavlink_gimbal_device_attitude_status_t s;
    mavlink_msg_gimbal_device_attitude_status_decode(&f.msg, &s);
    float roll, pitch, yaw;
    mavlink_quaternion_to_euler(s.q, &roll, &pitch, &yaw);
    const double k = 180.0 / M_PI;
    out->pitchDeg = pitch * k;
    out->rollDeg = roll * k;
    out->yawDeg = yaw * k;
    out->rateX = s.angular_velocity_x * k;
    out->rateY = s.angular_velocity_y * k;
    out->rateZ = s.angular_velocity_z * k;
    out->flags = s.flags;
    out->failureFlags = s.failure_flags;
    return true;
}

std::optional<double> fieldNumber(const MavFrame &f, const char *name, int index)
{
    for (const MavField &x : decodeFields(f)) {
        if (x.name != name) continue;
        if (x.isReal && size_t(index) < x.reals.size()) return x.reals[size_t(index)];
        if (!x.isReal && !x.isText && size_t(index) < x.ints.size()) return double(x.ints[size_t(index)]);
        return std::nullopt;
    }
    return std::nullopt;
}

QString fieldText(const MavFrame &f, const char *name)
{
    for (const MavField &x : decodeFields(f))
        if (x.name == name && x.isText) return QString::fromStdString(x.value.substr(1, x.value.size() - 2));
    return {};
}

void TelemetryStore::add(const CapturedFrame &c)
{
    if (c.dir != CapturedFrame::Rx || c.frame.status != MavFrame::Ok) return;  // never trust unvalidated bytes
    m_last[{c.frame.msgid, c.frame.compid}] = c;
    if (c.frame.msgid == MAVLINK_MSG_ID_DEBUG && c.frame.compid == kPayloadCore) {
        mavlink_debug_t d;
        mavlink_msg_debug_decode(&c.frame.msg, &d);
        m_params[d.ind] = {d.value, c.monoNs};
    }
}

const CapturedFrame *TelemetryStore::latest(quint32 msgid) const
{
    const CapturedFrame *best = nullptr;
    for (auto it = m_last.lower_bound({msgid, 0}); it != m_last.end() && it->first.first == msgid; ++it)
        if (!best || it->second.monoNs > best->monoNs) best = &it->second;
    return best;
}

const CapturedFrame *TelemetryStore::latest(quint32 msgid, quint8 compid) const
{
    auto it = m_last.find({msgid, compid});
    return it == m_last.end() ? nullptr : &it->second;
}
