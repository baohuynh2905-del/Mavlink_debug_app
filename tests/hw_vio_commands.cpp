// Hardware check of the VIO control path (not part of ctest: it moves a real gimbal).
// Every action goes through PayloadLink exactly as the GUI sends it, and every effect is confirmed
// from the payload's own telemetry. Small motions only, each undone afterwards; no photo/recording.
// tracking moves the gimbal to follow a target; settings writes camera settings (only values read back from
// the camera) and changes three of them, restoring each.
//
//   hw_vio_commands --ip 192.168.55.1 [--steps camera,angle,rate,mode-flags,mode-param,tracking,settings] [--record run.jsonl]

#include "frame_writer.h"
#include "payload_link.h"
#include "payload_profile.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>

static PayloadLink *g_link;
static qint64 g_t0;

static void log(const QString &s)
{
    printf("[%6.2f s] %s\n", double(PayloadLink::monoNs() - g_t0) / 1e9, qPrintable(s));
    fflush(stdout);
}

static bool waitUntil(const std::function<bool()> &pred, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!pred()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

static void pause(int ms) { waitUntil([] { return false; }, ms); }

static bool attitude(GimbalAttitude *a, qint64 *ageMs = nullptr)
{
    const CapturedFrame *c = g_link->telemetry().latest(MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS);
    if (!c || !gimbalAttitude(c->frame, a)) return false;
    if (ageMs) *ageMs = (PayloadLink::monoNs() - c->monoNs) / 1000000;
    return true;
}

static std::optional<double> param(const char *name)
{
    const auto &p = g_link->telemetry().payloadParams();
    for (auto it = p.cbegin(); it != p.cend(); ++it)
        if (PayloadLink::payloadParamName(it.key()) == QLatin1String(name)) return it->value;
    return std::nullopt;
}

// Waits for the newest tracked command to settle and prints its outcome.
static const TrackedCommand *settle(const QString &what)
{
    const quint64 id = g_link->tracker().commands().back().id;
    const TrackedCommand *t = nullptr;
    waitUntil([&] {
        for (const TrackedCommand &x : g_link->tracker().commands())
            if (x.id == id) t = &x;
        return t && t->state != TrackedCommand::Queued && t->state != TrackedCommand::Sent && t->state != TrackedCommand::InProgress;
    }, 4500);
    QString r = t ? CommandTracker::stateText(t->state) : QStringLiteral("?");
    if (t && t->result >= 0) r += QStringLiteral(" (result %1 from comp %2, %3 ms after send)").arg(t->result).arg(t->ackFrom).arg(t->ackMs - t->sentMs);
    if (t && t->sentMs >= 0) r += QStringLiteral(", target %1/%2, TX frame #%3").arg(t->targetSystem).arg(t->targetComponent).arg(t->txFrame);
    log(QStringLiteral("  %1 -> %2").arg(what, r));
    return t;
}

static bool safe(QString *why)
{
    GimbalAttitude a;
    qint64 age = 0;
    if (g_link->state() != LinkState::Heartbeat) *why = QStringLiteral("link state %1").arg(linkStateText(g_link->state()));
    else if (!attitude(&a, &age) || age > 1000) *why = QStringLiteral("gimbal telemetry missing or stale");
    else if (a.failureFlags) *why = QStringLiteral("gimbal failure flags 0x%1").arg(a.failureFlags, 0, 16);
    else return true;
    return false;
}

// Yaw lands ~2.7° off an angle command on a VIO (3.1.4.18), so yaw gets a wider tolerance.
static bool reachPitchYaw(double pitch, double yaw, int ms, double tol = 1.0, double yawTol = 4.0)
{
    QElapsedTimer t;
    t.start();
    const bool ok = waitUntil([&] {
        GimbalAttitude a;
        return attitude(&a) && std::abs(a.pitchDeg - pitch) < tol && std::abs(a.yawDeg - yaw) < yawTol;
    }, ms);
    GimbalAttitude a;
    attitude(&a);
    log(QStringLiteral("  measured pitch %1°, yaw %2° -> %3 (%4 ms)").arg(a.pitchDeg, 0, 'f', 2).arg(a.yawDeg, 0, 'f', 2)
            .arg(ok ? QStringLiteral("REACHED") : QStringLiteral("NOT reached within tolerance")).arg(t.elapsed()));
    return ok;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser p;
    p.addOption({QStringLiteral("ip"), QStringLiteral("Payload IPv4"), QStringLiteral("ip")});
    p.addOption({QStringLiteral("port"), QStringLiteral("UDP port"), QStringLiteral("port"), QStringLiteral("14566")});
    p.addOption({QStringLiteral("steps"), QStringLiteral("Comma-separated steps"), QStringLiteral("list"),
                 QStringLiteral("camera,angle,rate,mode-flags")});
    p.addOption({QStringLiteral("record"), QStringLiteral("Also write every frame to this .jsonl/.tlog/.csv"), QStringLiteral("path")});
    p.process(app);
    if (!p.isSet(QStringLiteral("ip"))) {
        fprintf(stderr, "--ip is required (this program moves a real gimbal)\n");
        return 2;
    }

    PayloadLink link;
    g_link = &link;
    g_t0 = PayloadLink::monoNs();
    int failures = 0;
    auto check = [&](bool ok, const QString &what) {
        if (!ok) ++failures;
        log(QStringLiteral("  %1: %2").arg(ok ? QStringLiteral("OK") : QStringLiteral("FAIL"), what));
    };

    const QStringList steps = p.value(QStringLiteral("steps")).split(QLatin1Char(','));
    FrameWriter rec;
    if (p.isSet(QStringLiteral("record"))) {
        QString err;
        const QString path = p.value(QStringLiteral("record"));
        if (!rec.start(path, FrameWriter::formatForPath(path), &err)) {
            fprintf(stderr, "cannot record: %s\n", qPrintable(err));
            return 2;
        }
        QObject::connect(&link, &PayloadLink::framesCaptured, [&](const std::vector<CapturedFrame> &f) { rec.write(f); });
    }
    link.connectTo(p.value(QStringLiteral("ip")), p.value(QStringLiteral("port")).toInt());
    if (!waitUntil([&] {
            GimbalAttitude a;
            return link.state() == LinkState::Heartbeat && attitude(&a) && param("VIEW_MODE") && param("EO_ZOOM") &&
                   link.telemetry().latest(MAVLINK_MSG_ID_HEARTBEAT, MAV_COMP_ID_CAMERA);
        }, 8000)) {
        log(QStringLiteral("ABORT: payload not ready (heartbeats, gimbal status, parameter stream)"));
        return 1;
    }
    QString why;
    if (!safe(&why)) {
        log(QStringLiteral("ABORT: ") + why);
        return 1;
    }

    // ---------------------------------------------------------------- camera (no motion)
    if (steps.contains(QLatin1String("camera"))) {
    log(QStringLiteral("CAMERA"));
    const int view = int(*param("VIEW_MODE"));
    link.cameraParam(QStringLiteral("View source unchanged"), "C_SOURCE", quint32(view));
    const TrackedCommand *t = settle(QStringLiteral("PARAM_EXT_SET C_SOURCE = %1 (current value)").arg(view));
    check(t && t->state == TrackedCommand::Accepted, QStringLiteral("view source acknowledged"));
    pause(1500);
    check(param("VIEW_MODE") && int(*param("VIEW_MODE")) == view, QStringLiteral("VIEW_MODE still %1").arg(view));

    const double zoom0 = *param("EO_ZOOM");
    log(QStringLiteral("  EO_ZOOM before: %1").arg(zoom0));
    link.cameraZoom(ZOOM_TYPE_CONTINUOUS, 1, QStringLiteral("Zoom in"));
    settle(QStringLiteral("SET_CAMERA_ZOOM continuous +1"));
    pause(1000);
    link.cameraZoom(ZOOM_TYPE_CONTINUOUS, 0, QStringLiteral("Zoom stop"));
    settle(QStringLiteral("SET_CAMERA_ZOOM continuous 0"));
    pause(2200);  // EO_ZOOM streams at 1 Hz
    const double zoom1 = *param("EO_ZOOM");
    check(zoom1 > zoom0, QStringLiteral("EO_ZOOM after zoom in: %1 (was %2)").arg(zoom1).arg(zoom0));
    link.cameraZoom(ZOOM_TYPE_CONTINUOUS, -1, QStringLiteral("Zoom out"));
    settle(QStringLiteral("SET_CAMERA_ZOOM continuous -1"));
    pause(2500);
    link.cameraZoom(ZOOM_TYPE_CONTINUOUS, 0, QStringLiteral("Zoom stop"));
    settle(QStringLiteral("SET_CAMERA_ZOOM continuous 0"));
    if (!waitUntil([&] { return std::abs(*param("EO_ZOOM") - zoom0) < 0.05; }, 3000)) {
        link.cameraZoom(ZOOM_TYPE_RANGE, 0, QStringLiteral("Zoom to 0%"));
        settle(QStringLiteral("SET_CAMERA_ZOOM range 0% (restore)"));
        waitUntil([&] { return std::abs(*param("EO_ZOOM") - zoom0) < 0.05; }, 4000);
    }
    check(std::abs(*param("EO_ZOOM") - zoom0) < 0.05, QStringLiteral("EO_ZOOM restored: %1").arg(*param("EO_ZOOM")));
    link.cameraFocus(FOCUS_TYPE_AUTO, 0, QStringLiteral("Auto focus"));
    settle(QStringLiteral("SET_CAMERA_FOCUS auto"));
    }

    // ---------------------------------------------------------------- gimbal (small motions, undone)
    log(QStringLiteral("GIMBAL"));
    GimbalAttitude a0;
    attitude(&a0);
    log(QStringLiteral("  start: pitch %1°, roll %2°, yaw %3°, flags 0x%4 (%5)").arg(a0.pitchDeg, 0, 'f', 2).arg(a0.rollDeg, 0, 'f', 2)
            .arg(a0.yawDeg, 0, 'f', 2).arg(a0.flags, 4, 16, QLatin1Char('0')).arg(GimbalFlags::modeName(a0.flags)));
    const double yaw0 = a0.yawDeg, pitch0 = a0.pitchDeg;

    if (steps.contains(QLatin1String("angle"))) {
    if (!safe(&why)) { log(QStringLiteral("ABORT: ") + why); return 1; }
    link.gimbalAngle(-10, 0, yaw0);
    settle(QStringLiteral("GIMBAL_DEVICE_SET_ATTITUDE pitch -10°"));
    check(reachPitchYaw(-10, yaw0, 4000), QStringLiteral("pitch -10° confirmed by telemetry"));
    link.gimbalAngle(pitch0, 0, yaw0);
    settle(QStringLiteral("GIMBAL_DEVICE_SET_ATTITUDE back to start"));
    check(reachPitchYaw(pitch0, yaw0, 4000), QStringLiteral("back to start pitch"));
    }

    if (steps.contains(QLatin1String("rate"))) {
    if (!safe(&why)) { log(QStringLiteral("ABORT: ") + why); return 1; }
    GimbalAttitude a0r;
    attitude(&a0r);
    double peakRate = 0;  // reported angular_velocity_z, deg/s
    const auto peakConn = QObject::connect(&link, &PayloadLink::framesCaptured, [&](const std::vector<CapturedFrame> &f) {
        GimbalAttitude a;
        for (const CapturedFrame &c : f)
            if (c.dir == CapturedFrame::Rx && gimbalAttitude(c.frame, &a) && std::abs(a.rateZ) > std::abs(peakRate)) peakRate = a.rateZ;
    });
    link.setGimbalRateScale(vioProfile().gimbalRateScale);  // as GimbalPage does
    link.gimbalRate(0, 0, 10, true);
    settle(QStringLiteral("rate yaw +10°/s (then re-sent every 100 ms for 1 s)"));
    for (int i = 0; i < 9; ++i) {
        pause(100);
        if (!safe(&why)) break;
        link.gimbalRate(0, 0, 10, false);
    }
    pause(100);
    link.gimbalRate(0, 0, 0, true);
    settle(QStringLiteral("rate zero"));
    pause(800);
    GimbalAttitude a1;
    attitude(&a1);
    const double moved = a1.yawDeg - a0r.yawDeg;
    check(moved > 7 && moved < 15, QStringLiteral("yaw moved by %1° for ~1 s at 10°/s").arg(moved, 0, 'f', 2));
    link.gimbalAngle(pitch0, 0, yaw0);
    settle(QStringLiteral("angle back to start yaw"));
    QObject::disconnect(peakConn);  // the lambda refers to peakRate, which ends with this block
    log(QStringLiteral("  peak reported angular_velocity_z: %1°/s").arg(peakRate, 0, 'f', 2));
    check(std::abs(peakRate) > 8 && std::abs(peakRate) < 12, QStringLiteral("peak rate near the requested 10°/s"));
    check(reachPitchYaw(pitch0, yaw0, 5000), QStringLiteral("back to start yaw"));
    }

    if (steps.contains(QLatin1String("mode-flags"))) {
    if (!safe(&why)) { log(QStringLiteral("ABORT: ") + why); return 1; }
    link.gimbalMode(GimbalFlags::Change::Follow, QStringLiteral("Follow"));
    settle(QStringLiteral("mode Follow"));
    check(waitUntil([&] { GimbalAttitude a; return attitude(&a) && !(a.flags & GIMBAL_DEVICE_FLAGS_YAW_LOCK); }, 3000),
          QStringLiteral("YAW_LOCK cleared in telemetry"));
    pause(1500);
    log(QStringLiteral("  GB_MODE parameter: %1").arg(param("GB_MODE").value_or(-1)));
    link.gimbalMode(GimbalFlags::Change::Lock, QStringLiteral("Lock"));
    settle(QStringLiteral("mode Lock"));
    check(waitUntil([&] { GimbalAttitude a; return attitude(&a) && (a.flags & GIMBAL_DEVICE_FLAGS_YAW_LOCK); }, 3000),
          QStringLiteral("YAW_LOCK set again in telemetry"));
    pause(1500);
    log(QStringLiteral("  GB_MODE parameter: %1").arg(param("GB_MODE").value_or(-1)));
    }

    // Older route, in Mavlink_Command_List v2.0.0 and vio_sdk.h: PARAM_EXT_SET GB_MODE to the camera component
    if (steps.contains(QLatin1String("mode-param"))) {
    if (!safe(&why)) { log(QStringLiteral("ABORT: ") + why); return 1; }
    const PayloadProfile &vio = vioProfile();
    auto modeValue = [&](const char *label) {
        for (const ParamOption &o : vio.gimbalModes) if (QLatin1String(o.label) == QLatin1String(label)) return o.value;
        return 0u;
    };
    log(QStringLiteral("  GB_MODE parameter before: %1").arg(param("GB_MODE").value_or(-1)));
    link.cameraParam(QStringLiteral("Gimbal mode FOLLOW"), vio.gimbalModeId, modeValue("FOLLOW"));
    settle(QStringLiteral("PARAM_EXT_SET GB_MODE = FOLLOW (%1)").arg(modeValue("FOLLOW")));
    check(waitUntil([&] { GimbalAttitude a; return attitude(&a) && !(a.flags & GIMBAL_DEVICE_FLAGS_YAW_LOCK); }, 3000),
          QStringLiteral("YAW_LOCK cleared in telemetry"));
    check(waitUntil([&] { return param("GB_MODE").value_or(-1) == modeValue("FOLLOW"); }, 3000),
          QStringLiteral("GB_MODE parameter reports FOLLOW"));
    { GimbalAttitude a; attitude(&a); log(QStringLiteral("  flags now 0x%1 (%2)").arg(a.flags, 4, 16, QLatin1Char('0')).arg(GimbalFlags::modeName(a.flags))); }
    pause(1000);
    link.cameraParam(QStringLiteral("Gimbal mode LOCK"), vio.gimbalModeId, modeValue("LOCK"));
    settle(QStringLiteral("PARAM_EXT_SET GB_MODE = LOCK (%1)").arg(modeValue("LOCK")));
    check(waitUntil([&] { GimbalAttitude a; return attitude(&a) && (a.flags & GIMBAL_DEVICE_FLAGS_YAW_LOCK); }, 3000),
          QStringLiteral("YAW_LOCK set again in telemetry"));
    check(waitUntil([&] { return param("GB_MODE").value_or(-1) == modeValue("LOCK"); }, 3000),
          QStringLiteral("GB_MODE parameter reports LOCK"));
    }

    if (steps.contains(QLatin1String("tracking"))) {
    if (!safe(&why)) { log(QStringLiteral("ABORT: ") + why); return 1; }
    log(QStringLiteral("TRACKING"));
    const PayloadProfile &vio = vioProfile();
    auto trk = [&] { return link.operatorStatus(); };
    link.cameraParam(QStringLiteral("Tracking mode: object"), vio.trackingModeId, vio.trackingObjectMode);
    check(settle(QStringLiteral("PARAM_EXT_SET TRACK_MODE = object"))->state == TrackedCommand::Accepted, QStringLiteral("TRACK_MODE acknowledged"));
    link.trackingStreams();
    link.trackingMode(PayloadLink::TrackActive);
    check(settle(QStringLiteral("tracker ACTIVE"))->state == TrackedCommand::Accepted, QStringLiteral("ACTIVE acknowledged"));

    // Centre pick: the gimbal barely moves, so the reported box shows the coordinate convention
    link.trackingTarget(960, 540);
    settle(QStringLiteral("target 960,540 (after rate 0 and ACTIVE)"));
    check(waitUntil([&] { return trk().trackStatus.value_or(-1) == PayloadLink::TrackTracked; }, 2500), QStringLiteral("TRACKED"));
    waitUntil([&] { return trk().trackBox.has_value(); }, 1000);
    const QRectF b = trk().trackBox.value_or(QRectF());
    log(QStringLiteral("  box x %1 y %2 w %3 h %4").arg(b.x()).arg(b.y()).arg(b.width()).arg(b.height()));
    check(std::abs(b.center().x() - 960) < 30 && std::abs(b.center().y() - 540) < 30, QStringLiteral("box centred on the pick (TRK_POS = top-left)"));
    link.trackingMode(PayloadLink::TrackStop);
    settle(QStringLiteral("tracker STOP"));
    check(waitUntil([&] { return trk().trackMode.value_or(-1) == PayloadLink::TrackStop; }, 2000), QStringLiteral("mode STOP reported"));

    // Off-centre pick after an angle command (the case that never followed): the gimbal must turn to it
    link.gimbalAngle(pitch0, 0, yaw0);
    settle(QStringLiteral("angle hold at the start attitude"));
    pause(1500);
    GimbalAttitude s0;
    attitude(&s0);
    link.trackingTarget(480, 270);
    settle(QStringLiteral("target 480,270"));
    // The box must first show the new target near the pick (not the previous, centred box), then reach the centre
    double turned = 0, bestDist = 1e9;
    bool seenAtPick = false;
    waitUntil([&] {
        GimbalAttitude a;
        if (attitude(&a)) turned = std::max(turned, std::hypot(a.pitchDeg - s0.pitchDeg, a.yawDeg - s0.yawDeg));
        if (const auto bx = trk().trackBox; bx && trk().trackStatus.value_or(0) == PayloadLink::TrackTracked) {
            seenAtPick |= std::hypot(bx->center().x() - 480, bx->center().y() - 270) < 200;
            if (seenAtPick) bestDist = std::min(bestDist, std::hypot(bx->center().x() - 960, bx->center().y() - 540));
        }
        return turned > 10 && bestDist < 60;
    }, 5000);
    check(seenAtPick, QStringLiteral("box reported at the picked target"));
    log(QStringLiteral("  status %1").arg(trackingStateText(trk().trackMode.value_or(-1), trk().trackStatus.value_or(-1))));
    check(turned > 3, QStringLiteral("gimbal followed the target: turned %1°").arg(turned, 0, 'f', 1));
    check(bestDist < 60, QStringLiteral("target brought to the picture centre: %1 px away").arg(bestDist, 0, 'f', 0));
    link.trackingMode(PayloadLink::TrackStop);
    settle(QStringLiteral("tracker STOP"));
    check(waitUntil([&] { return trk().trackMode.value_or(-1) == PayloadLink::TrackStop; }, 2000), QStringLiteral("mode STOP reported"));
    link.gimbalAngle(pitch0, 0, yaw0);
    settle(QStringLiteral("angle back to start"));
    check(reachPitchYaw(pitch0, yaw0, 5000), QStringLiteral("back to start"));
    }

    if (steps.contains(QLatin1String("settings-read"))) {  // read-only
    link.requestCameraSettings();
    pause(3000);
    const auto &cam = link.telemetry().cameraParams();
    QStringList all;
    for (auto it = cam.cbegin(); it != cam.cend(); ++it) all << QStringLiteral("%1=%2").arg(it.key()).arg(it->value);
    log(QStringLiteral("  %1 settings: %2").arg(cam.size()).arg(all.join(QLatin1Char(' '))));
    }

    // Settings: only values the camera reported are written back; three visible ones are changed and restored
    if (steps.contains(QLatin1String("settings"))) {
    log(QStringLiteral("SETTINGS"));
    const auto &cam = link.telemetry().cameraParams();
    link.requestCameraSettings();
    pause(3000);
    log(QStringLiteral("  camera listed %1 settings").arg(cam.size()));
    auto reported = [&](const char *id) -> std::optional<unsigned> {
        const auto it = cam.constFind(QString::fromLatin1(id));
        return it == cam.cend() ? std::nullopt : std::optional<unsigned>(unsigned(it->value));
    };
    for (const CameraSetting &cs : vioProfile().cameraSettings) {
        const auto v = reported(cs.id);
        bool known = v.has_value();
        if (known && !cs.options.empty())
            known = std::any_of(cs.options.begin(), cs.options.end(), [&](const ParamOption &o) { return o.value == *v; });
        else if (known)
            known = *v >= cs.min && *v <= cs.max;
        if (!known) {
            check(false, QStringLiteral("%1: reported %2, not a value the profile knows: not written").arg(QLatin1String(cs.id))
                             .arg(v ? QString::number(*v) : QStringLiteral("nothing")));
            continue;
        }
        link.cameraParam(QStringLiteral("Same value"), cs.id, *v);
        const TrackedCommand *t = settle(QStringLiteral("%1 = %2 (unchanged)").arg(QLatin1String(cs.id)).arg(*v));
        check(t && t->state == TrackedCommand::Accepted, QStringLiteral("%1 accepted").arg(QLatin1String(cs.id)));
    }
    for (const char *id : {"OSD_MODE", "C_T_PALETTE", "C_V_DEFOG"}) {
        const CameraSetting *cs = nullptr;
        for (const CameraSetting &c : vioProfile().cameraSettings) if (QLatin1String(c.id) == QLatin1String(id)) cs = &c;
        const auto v0 = reported(id);
        if (!cs || !v0) { check(false, QStringLiteral("%1 not read: not changed").arg(QLatin1String(id))); continue; }
        const unsigned v1 = cs->options.front().value == *v0 ? cs->options.back().value : cs->options.front().value;
        for (unsigned v : {v1, *v0}) {
            link.cameraParam(QStringLiteral("Change"), id, v);
            settle(QStringLiteral("%1 = %2").arg(QLatin1String(id)).arg(v));
            check(waitUntil([&] { return reported(id) == v; }, 3000), QStringLiteral("%1 reads back %2").arg(QLatin1String(id)).arg(v));
            pause(800);
        }
    }
    }

    GimbalAttitude a2;
    attitude(&a2);
    log(QStringLiteral("  end: pitch %1°, yaw %2°, flags 0x%3").arg(a2.pitchDeg, 0, 'f', 2).arg(a2.yawDeg, 0, 'f', 2).arg(a2.flags, 4, 16, QLatin1Char('0')));
    check(a2.flags == a0.flags, QStringLiteral("flags back to the start value"));

    link.disconnectLink();
    waitUntil([&] { return link.state() == LinkState::Disconnected; }, 3000);
    rec.stop();
    log(failures ? QStringLiteral("%1 check(s) FAILED").arg(failures) : QStringLiteral("all checks passed"));
    return failures ? 1 : 0;
}
