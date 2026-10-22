#include "payload_pages.h"

#include "frame_text.h"
#include "mavlink_dict.h"
#include "payload_link.h"
#include "payload_profile.h"
#include "ui_util.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QSlider>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------------------------

PayloadPage::PayloadPage(PayloadLink *link, const MavlinkDict *dict, QWidget *parent)
    : QWidget(parent), m_link(link), m_dict(dict), m_error(new QLabel)
{
    m_error->setStyleSheet(QStringLiteral("color:#c01c28"));
    m_error->setWordWrap(true);
    auto *t = new QTimer(this);
    connect(t, &QTimer::timeout, this, [this] { refresh(); });
    t->start(250);
    connect(link, &PayloadLink::stateChanged, this, [this] {
        if (m_controls) m_controls->setEnabled(isOpen(m_link->state()));
        if (!isOpen(m_link->state())) m_error->clear();
        linkChanged();
    });
}

void PayloadPage::setPage(QWidget *controls, QWidget *status)
{
    m_controls = controls;
    m_controls->setEnabled(isOpen(m_link->state()));
    auto *hint = new QLabel(QStringLiteral("Controls are enabled while connected. Every action is listed on the "
                                           "<b>Commands</b> tab with its frame and acknowledgement."));
    hint->setWordWrap(true);
    auto *l = new QHBoxLayout(this);
    auto *left = new QVBoxLayout;
    left->addWidget(controls);
    left->addWidget(m_error);
    left->addWidget(hint);
    left->addStretch();
    l->addLayout(left, 3);
    l->addWidget(status, 2);
}

void PayloadPage::report(bool ok)
{
    m_error->setText(ok ? QString() : m_link->commandError());
}

qint64 PayloadPage::now() const { return PayloadLink::monoNs(); }

static QString deg(double v) { return std::isnan(v) ? QStringLiteral("NaN") : QStringLiteral("%1°").arg(v, 0, 'f', 1); }

static QGroupBox *group(const QString &title, QLayout *layout)
{
    auto *g = new QGroupBox(title);
    g->setLayout(layout);
    return g;
}

// --- Gimbal ----------------------------------------------------------------------------------

GimbalPage::GimbalPage(PayloadLink *link, const MavlinkDict *dict, QWidget *parent) : PayloadPage(link, dict, parent)
{
    // Mode (flags in GIMBAL_DEVICE_SET_ATTITUDE, as the SDK example gimbal_set_mode.cpp does)
    auto *modes = new QGridLayout;
    auto addMode = [&](const QString &text, GimbalFlags::Change c, int row, int col, const QString &tip) {
        QPushButton *b = button(text, tip);
        connect(b, &QPushButton::clicked, this, [this, text, c] { report(m_link->gimbalMode(c, QStringLiteral("Gimbal mode: ") + text)); });
        modes->addWidget(b, row, col);
    };
    addMode(QStringLiteral("Lock"), GimbalFlags::Change::Lock, 0, 0, QStringLiteral("Sets YAW_LOCK: the camera holds its heading"));
    addMode(QStringLiteral("Follow"), GimbalFlags::Change::Follow, 0, 1, QStringLiteral("Clears YAW_LOCK: the camera follows the vehicle"));
    addMode(QStringLiteral("Mapping"), GimbalFlags::Change::Mapping, 0, 2, QStringLiteral("Sets Gremsy bit 0x4000 (mapping mode)"));
    QPushButton *home = button(QStringLiteral("Home"), QStringLiteral("Sets NEUTRAL, then clears it 100 ms later"));
    connect(home, &QPushButton::clicked, this, [this] { report(m_link->gimbalHome()); });
    modes->addWidget(home, 1, 0);
    addMode(QStringLiteral("Motors on"), GimbalFlags::Change::MotorsOn, 1, 1, QStringLiteral("Clears RETRACT"));
    QPushButton *off = button(QStringLiteral("Motors off…"), QStringLiteral("Sets RETRACT: motors stop, the camera is not stabilised"));
    connect(off, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("Motors off"),
                                  QStringLiteral("Switch the gimbal motors off? The camera will not be stabilised "
                                                 "and may drop to its end stop.")) == QMessageBox::Yes)
            report(m_link->gimbalMode(GimbalFlags::Change::MotorsOff, QStringLiteral("Gimbal motors off")));
    });
    modes->addWidget(off, 1, 2);
    auto *modeNote = new QLabel(QStringLiteral("Mode changes start from the flags last reported by the gimbal, so "
                                               "they are refused until a GIMBAL_DEVICE_ATTITUDE_STATUS arrives."));
    modeNote->setWordWrap(true);
    modes->addWidget(modeNote, 2, 0, 1, 3);

    // Angle
    auto spin = [](double limit, const QString &suffix) {
        auto *s = new QDoubleSpinBox;
        s->setRange(-limit, limit);
        s->setDecimals(1);
        s->setSuffix(suffix);
        return s;
    };
    m_pitch = spin(PayloadLink::kPitchLimit, QStringLiteral("°"));
    m_roll = spin(PayloadLink::kRollLimit, QStringLiteral("°"));
    m_yaw = spin(PayloadLink::kYawLimit, QStringLiteral("°"));
    QPushButton *move = button(QStringLiteral("Move to angle"));
    connect(move, &QPushButton::clicked, this, [this] {
        const bool ok = m_link->gimbalAngle(m_pitch->value(), m_roll->value(), m_yaw->value());
        report(ok);
        if (ok) {
            m_haveCommand = true;
            m_cmdPitch = m_pitch->value();
            m_cmdYaw = m_yaw->value();
            m_commanded->setText(QStringLiteral("pitch %1, roll %2, yaw %3")
                                     .arg(deg(m_pitch->value()), deg(m_roll->value()), deg(m_yaw->value())));
        }
    });
    auto *angle = new QFormLayout;
    angle->addRow(QStringLiteral("Pitch (±90°)"), m_pitch);
    angle->addRow(QStringLiteral("Roll (±180°)"), m_roll);
    angle->addRow(QStringLiteral("Yaw (±180°)"), m_yaw);
    angle->addRow(move);
    auto *limits = new QLabel(QStringLiteral("Limits are the ones PayloadSdkInterface::setGimbalSpeed() enforces."));
    limits->setWordWrap(true);
    angle->addRow(limits);

    // Rate: hold to move, re-sent every 100 ms like Gremsy's ui_demo, zero on release
    m_speed = new QDoubleSpinBox;
    m_speed->setRange(1, 90);
    m_speed->setValue(20);
    m_speed->setSuffix(QStringLiteral(" °/s"));
    m_speed->setToolTip(QStringLiteral("The SDK documents no rate limit; the gimbal may clamp the value."));
    m_rateTimer = new QTimer(this);
    m_rateTimer->setInterval(100);
    connect(m_rateTimer, &QTimer::timeout, this, [this] {
        if (!m_held || !m_held->isDown()) {  // safety net if a release event was missed
            stopRate();
            return;
        }
        m_link->gimbalRate(m_ratePitch, 0, m_rateYaw, false);
    });
    auto *rate = new QGridLayout;
    rate->addWidget(holdButton(QStringLiteral("▲ Pitch up"), 1, 0), 0, 1);
    rate->addWidget(holdButton(QStringLiteral("◀ Yaw left"), 0, -1), 1, 0);
    QPushButton *stop = button(QStringLiteral("Stop"), QStringLiteral("Sends zero rate"));
    connect(stop, &QPushButton::clicked, this, [this] { stopRate(true); });
    rate->addWidget(stop, 1, 1);
    rate->addWidget(holdButton(QStringLiteral("Yaw right ▶"), 0, 1), 1, 2);
    rate->addWidget(holdButton(QStringLiteral("▼ Pitch down"), -1, 0), 2, 1);
    rate->addWidget(new QLabel(QStringLiteral("Speed")), 3, 0);
    rate->addWidget(m_speed, 3, 1, 1, 2);

    auto *controls = new QWidget;
    auto *cl = new QVBoxLayout(controls);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(group(QStringLiteral("Mode"), modes));
    cl->addWidget(group(QStringLiteral("Move to angle"), angle));
    cl->addWidget(group(QStringLiteral("Move by rate (hold)"), rate));

    // Measured
    m_commanded = new QLabel(QStringLiteral("<span style='color:#888'>no angle command yet</span>"));
    m_attitude = valueLabel();
    m_rates = valueLabel();
    m_mode = valueLabel();
    m_flags = valueLabel();
    m_failures = valueLabel();
    m_delta = valueLabel();
    m_flags->setWordWrap(true);
    m_failures->setWordWrap(true);
    auto *meas = new QFormLayout;
    meas->addRow(QStringLiteral("Last commanded"), m_commanded);
    meas->addRow(QStringLiteral("Measured attitude"), m_attitude);
    meas->addRow(QStringLiteral("Measured − commanded"), m_delta);
    meas->addRow(QStringLiteral("Angular rate"), m_rates);
    meas->addRow(QStringLiteral("Mode (from flags)"), m_mode);
    meas->addRow(QStringLiteral("Flags"), m_flags);
    meas->addRow(QStringLiteral("Failure flags"), m_failures);
    auto *note = new QLabel(QStringLiteral("Measured values come from GIMBAL_DEVICE_ATTITUDE_STATUS. GIMBAL_DEVICE_SET_ATTITUDE has "
                                           "no acknowledgement, so this telemetry is the only confirmation. Yaw is in the frame "
                                           "given by the flags (vehicle or earth)."));
    note->setWordWrap(true);
    meas->addRow(note);
    setPage(controls, group(QStringLiteral("Gimbal telemetry"), meas));
}

QPushButton *GimbalPage::holdButton(const QString &text, double pitchSign, double yawSign)
{
    QPushButton *b = button(text, QStringLiteral("Hold to move; released = zero rate"));
    connect(b, &QPushButton::pressed, this, [this, b, pitchSign, yawSign] {
        m_held = b;
        m_ratePitch = pitchSign * m_speed->value();
        m_rateYaw = yawSign * m_speed->value();
        report(m_link->gimbalRate(m_ratePitch, 0, m_rateYaw, true));
        m_rateTimer->start();
    });
    connect(b, &QPushButton::released, this, [this] { stopRate(); });
    return b;
}

void GimbalPage::stopRate(bool force)
{
    const bool wasMoving = m_rateTimer->isActive() || m_ratePitch != 0 || m_rateYaw != 0;
    m_rateTimer->stop();
    m_held = nullptr;
    m_ratePitch = m_rateYaw = 0;
    if (isOpen(m_link->state()) && (wasMoving || force)) report(m_link->gimbalRate(0, 0, 0, true));
}

void GimbalPage::linkChanged()
{
    if (!isOpen(m_link->state())) {
        m_rateTimer->stop();
        m_held = nullptr;
        m_ratePitch = m_rateYaw = 0;
    }
}

void GimbalPage::refresh()
{
    const CapturedFrame *c = m_link->telemetry().latest(MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS);
    const int to = m_link->timeoutMs();
    GimbalAttitude a;
    if (!c || !gimbalAttitude(c->frame, &a)) {
        for (QLabel *l : {m_attitude, m_rates, m_mode, m_flags, m_failures, m_delta}) l->setText(agedValue({}, -1, 0, to));
        return;
    }
    const qint64 t = c->monoNs, n = now();
    m_attitude->setText(agedValue(QStringLiteral("pitch %1, roll %2, yaw %3  (from %4/%5)")
                                      .arg(deg(a.pitchDeg), deg(a.rollDeg), deg(a.yawDeg)).arg(c->frame.sysid).arg(c->frame.compid),
                                  t, n, to));
    m_rates->setText(agedValue(QStringLiteral("x %1/s, y %2/s, z %3/s").arg(deg(a.rateX), deg(a.rateY), deg(a.rateZ)), t, n, to));
    m_mode->setText(agedValue(GimbalFlags::modeName(a.flags), t, n, to));
    QString flags;
    if (const MavlinkDict::Field *f = m_dict ? m_dict->field(MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS, QStringLiteral("flags")) : nullptr)
        flags = m_dict->label(*f, a.flags & ~GimbalFlags::Mapping);
    if (a.flags & GimbalFlags::Mapping) flags += QStringLiteral(" | Gremsy MAPPING (0x4000)");
    m_flags->setText(agedValue(QStringLiteral("0x%1  %2").arg(a.flags, 4, 16, QLatin1Char('0')).arg(flags), t, n, to));
    QString fail = QStringLiteral("none");
    if (a.failureFlags) {
        fail = QStringLiteral("0x%1").arg(a.failureFlags, 0, 16);
        if (const MavlinkDict::Field *f = m_dict ? m_dict->field(MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS, QStringLiteral("failure_flags")) : nullptr)
            fail += QStringLiteral("  ") + m_dict->label(*f, a.failureFlags);
    }
    m_failures->setText(agedValue(fail, t, n, to));
    m_delta->setText(m_haveCommand ? agedValue(QStringLiteral("pitch %1, yaw %2").arg(deg(a.pitchDeg - m_cmdPitch), deg(a.yawDeg - m_cmdYaw)), t, n, to)
                                   : QStringLiteral("<span style='color:#888'>no angle command yet</span>"));
}

// --- Camera ----------------------------------------------------------------------------------

CameraPage::CameraPage(PayloadLink *link, const MavlinkDict *dict, const PayloadProfile &profile, QWidget *parent)
    : PayloadPage(link, dict, parent), m_profile(profile)
{
    auto *capture = new QGridLayout;
    QPushButton *photoMode = button(QStringLiteral("Photo mode"), QStringLiteral("MAV_CMD_SET_CAMERA_MODE, CAMERA_MODE_IMAGE"));
    QPushButton *videoMode = button(QStringLiteral("Video mode"), QStringLiteral("MAV_CMD_SET_CAMERA_MODE, CAMERA_MODE_VIDEO"));
    QPushButton *shot = button(QStringLiteral("Capture photo"), QStringLiteral("MAV_CMD_IMAGE_START_CAPTURE, single image"));
    QPushButton *recStart = button(QStringLiteral("Start recording"), QStringLiteral("MAV_CMD_VIDEO_START_CAPTURE"));
    QPushButton *recStop = button(QStringLiteral("Stop recording"), QStringLiteral("MAV_CMD_VIDEO_STOP_CAPTURE"));
    connect(photoMode, &QPushButton::clicked, this, [this] { report(m_link->cameraMode(CAMERA_MODE_IMAGE)); });
    connect(videoMode, &QPushButton::clicked, this, [this] { report(m_link->cameraMode(CAMERA_MODE_VIDEO)); });
    connect(shot, &QPushButton::clicked, this, [this] { report(m_link->captureImage()); });
    connect(recStart, &QPushButton::clicked, this, [this] { report(m_link->recordVideo(true)); });
    connect(recStop, &QPushButton::clicked, this, [this] { report(m_link->recordVideo(false)); });
    capture->addWidget(photoMode, 0, 0);
    capture->addWidget(videoMode, 0, 1);
    capture->addWidget(shot, 1, 0);
    capture->addWidget(recStart, 1, 1);
    capture->addWidget(recStop, 1, 2);

    // Settings: sent only on a user choice (activated), never when the combo is refreshed
    auto *settings = new QFormLayout;
    auto addParam = [&](const QString &label, const char *id, const std::vector<ParamOption> &opts) {
        auto *cb = new QComboBox;
        if (!id) {
            cb->addItem(QStringLiteral("not supported by %1").arg(QLatin1String(m_profile.name)));
            cb->setEnabled(false);
        } else {
            cb->addItem(QStringLiteral("— choose to send —"));
            for (const ParamOption &o : opts) cb->addItem(QString::fromLatin1(o.label), o.value);
            cb->setToolTip(QStringLiteral("PARAM_EXT_SET %1").arg(QLatin1String(id)));
            connect(cb, &QComboBox::activated, this, [this, cb, label, id](int i) {
                if (i <= 0) return;
                report(m_link->cameraParam(label, id, cb->itemData(i).toUInt()));
            });
        }
        settings->addRow(label, cb);
    };
    addParam(QStringLiteral("View source"), m_profile.viewSourceId, m_profile.viewSources);
    addParam(QStringLiteral("Record source"), m_profile.recordSourceId, m_profile.recordSources);
    addParam(QStringLiteral("IR palette"), m_profile.irPaletteId, m_profile.irPalettes);

    // Zoom and focus (MAVLink semantics: -1 wide/near, +1 tele/far, 0 stop)
    auto *optics = new QGridLayout;
    optics->addWidget(holdButton(QStringLiteral("Zoom out (wide)"), QStringLiteral("ZOOM_TYPE_CONTINUOUS -1 while held, 0 on release"), true, -1), 0, 0);
    optics->addWidget(holdButton(QStringLiteral("Zoom in (tele)"), QStringLiteral("ZOOM_TYPE_CONTINUOUS +1 while held, 0 on release"), true, 1), 0, 1);
    m_zoomRange = new QSlider(Qt::Horizontal);
    m_zoomRange->setRange(0, 100);
    QPushButton *zoomTo = button(QStringLiteral("Zoom to 0%"), QStringLiteral("ZOOM_TYPE_RANGE: percentage of the full zoom range"));
    connect(m_zoomRange, &QSlider::valueChanged, zoomTo, [zoomTo](int v) { zoomTo->setText(QStringLiteral("Zoom to %1%").arg(v)); });
    connect(zoomTo, &QPushButton::clicked, this, [this] {
        report(m_link->cameraZoom(ZOOM_TYPE_RANGE, float(m_zoomRange->value()), QStringLiteral("Zoom to %1%").arg(m_zoomRange->value())));
    });
    optics->addWidget(m_zoomRange, 1, 0, 1, 2);
    optics->addWidget(zoomTo, 1, 2);
    optics->addWidget(holdButton(QStringLiteral("Focus near"), QStringLiteral("FOCUS_TYPE_CONTINUOUS -1 while held (the SDK's enum calls -1 FOCUS_OUT)"), false, -1), 2, 0);
    optics->addWidget(holdButton(QStringLiteral("Focus far"), QStringLiteral("FOCUS_TYPE_CONTINUOUS +1 while held (the SDK's enum calls +1 FOCUS_IN)"), false, 1), 2, 1);
    QPushButton *af = button(QStringLiteral("Auto focus"), QStringLiteral("FOCUS_TYPE_AUTO"));
    connect(af, &QPushButton::clicked, this, [this] { report(m_link->cameraFocus(FOCUS_TYPE_AUTO, 0, QStringLiteral("Auto focus"))); });
    optics->addWidget(af, 2, 2);
    for (int c = 0; c < 3; ++c) optics->setColumnStretch(c, 1);

    auto *controls = new QWidget;
    auto *cl = new QVBoxLayout(controls);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(group(QStringLiteral("Capture (%1)").arg(QLatin1String(m_profile.name)), capture));
    cl->addWidget(group(QStringLiteral("Settings"), settings));
    cl->addWidget(group(QStringLiteral("Zoom and focus"), optics));

    m_capture = valueLabel();
    m_video = valueLabel();
    m_storage = valueLabel();
    m_settings = valueLabel();
    m_reported = valueLabel();
    m_reported->setWordWrap(true);
    auto *st = new QFormLayout;
    st->addRow(QStringLiteral("Image capture"), m_capture);
    st->addRow(QStringLiteral("Video"), m_video);
    st->addRow(QStringLiteral("Storage"), m_storage);
    st->addRow(QStringLiteral("Camera settings"), m_settings);
    st->addRow(QStringLiteral("Payload reports"), m_reported);
    auto *note = new QLabel(QStringLiteral("Values are read back from CAMERA_CAPTURE_STATUS, STORAGE_INFORMATION, CAMERA_SETTINGS "
                                           "and the payload's DEBUG parameter stream. They are sent on request "
                                           "(Telemetry tab) or by the payload itself."));
    note->setWordWrap(true);
    st->addRow(note);
    setPage(controls, group(QStringLiteral("Camera status"), st));
}

QPushButton *CameraPage::holdButton(const QString &text, const QString &tip, bool zoom, int direction)
{
    QPushButton *b = button(text, tip);
    auto send = [this, zoom, text](int value) {
        const QString label = value ? text : text + QStringLiteral(": stop");
        report(zoom ? m_link->cameraZoom(ZOOM_TYPE_CONTINUOUS, float(value), label)
                    : m_link->cameraFocus(FOCUS_TYPE_CONTINUOUS, float(value), label));
    };
    connect(b, &QPushButton::pressed, this, [send, direction] { send(direction); });
    connect(b, &QPushButton::released, this, [send] { send(0); });
    return b;
}

void CameraPage::refresh()
{
    const TelemetryStore &tel = m_link->telemetry();
    const int to = m_link->timeoutMs();
    const qint64 n = now();
    auto enumText = [this](const char *e, double v) {
        const QString l = m_dict ? m_dict->enumLabel(QLatin1String(e), qint64(v)) : QString();
        return l.isEmpty() ? QString::number(v) : QStringLiteral("%1 (%2)").arg(l).arg(v);
    };

    if (const CapturedFrame *c = tel.latest(MAVLINK_MSG_ID_CAMERA_CAPTURE_STATUS)) {
        static const char *img[] = {"idle", "capturing", "interval idle", "interval capturing"};
        const int is = int(fieldNumber(c->frame, "image_status").value_or(-1));
        m_capture->setText(agedValue(QStringLiteral("%1, %2 images").arg(is >= 0 && is < 4 ? img[is] : "unknown")
                                         .arg(fieldNumber(c->frame, "image_count").value_or(0)), c->monoNs, n, to));
        const int vs = int(fieldNumber(c->frame, "video_status").value_or(-1));
        m_video->setText(agedValue(QStringLiteral("%1, %2 s").arg(vs == 1 ? QStringLiteral("RECORDING") : vs == 0 ? QStringLiteral("idle") : QStringLiteral("unknown"))
                                       .arg(fieldNumber(c->frame, "recording_time_ms").value_or(0) / 1000.0, 0, 'f', 1), c->monoNs, n, to));
    } else {
        m_capture->setText(agedValue({}, -1, n, to));
        m_video->setText(agedValue({}, -1, n, to));
    }
    if (const CapturedFrame *c = tel.latest(MAVLINK_MSG_ID_STORAGE_INFORMATION)) {
        m_storage->setText(agedValue(QStringLiteral("%1 of %2 MiB free, %3")
                                         .arg(fieldNumber(c->frame, "available_capacity").value_or(NAN), 0, 'f', 0)
                                         .arg(fieldNumber(c->frame, "total_capacity").value_or(NAN), 0, 'f', 0)
                                         .arg(enumText("STORAGE_STATUS", fieldNumber(c->frame, "status").value_or(-1))), c->monoNs, n, to));
    } else {
        m_storage->setText(agedValue({}, -1, n, to));
    }
    if (const CapturedFrame *c = tel.latest(MAVLINK_MSG_ID_CAMERA_SETTINGS)) {
        m_settings->setText(agedValue(QStringLiteral("%1, zoom %2%, focus %3%")
                                          .arg(enumText("CAMERA_MODE", fieldNumber(c->frame, "mode_id").value_or(-1)))
                                          .arg(fieldNumber(c->frame, "zoomLevel").value_or(NAN), 0, 'f', 1)
                                          .arg(fieldNumber(c->frame, "focusLevel").value_or(NAN), 0, 'f', 1), c->monoNs, n, to));
    } else {
        m_settings->setText(agedValue({}, -1, n, to));
    }

    // DEBUG stream values, mapped back to this profile's option labels where possible
    auto option = [](const std::vector<ParamOption> &opts, double v) {
        for (const ParamOption &o : opts) if (o.value == unsigned(v)) return QString::fromLatin1(o.label);
        return QString::number(v);
    };
    QStringList parts;
    qint64 newest = -1;
    const auto &params = tel.payloadParams();
    for (auto it = params.cbegin(); it != params.cend(); ++it) {
        const QString name = PayloadLink::payloadParamName(it.key());
        QString v;
        if (name == QLatin1String("VIEW_MODE")) v = option(m_profile.viewSources, it->value);
        else if (name == QLatin1String("REC_SRC")) v = option(m_profile.recordSources, it->value);
        else if (name == QLatin1String("PALETTE_ID")) v = option(m_profile.irPalettes, it->value);
        else if (name == QLatin1String("EO_ZOOM") || name == QLatin1String("IR_ZOOM")) v = QStringLiteral("%1x").arg(it->value, 0, 'g', 4);
        else continue;
        parts << QStringLiteral("%1: %2").arg(name, v);
        newest = std::max(newest, it->monoNs);
    }
    m_reported->setText(agedValue(parts.join(QStringLiteral(", ")), parts.isEmpty() ? -1 : newest, n, to));
}

// --- Telemetry -------------------------------------------------------------------------------

TelemetryPage::TelemetryPage(PayloadLink *link, const MavlinkDict *dict, QWidget *parent) : PayloadPage(link, dict, parent)
{
    QPushButton *req = button(QStringLiteral("Request payload info and streams"),
                              QStringLiteral("COMPONENT_INFORMATION_BASIC, CAMERA_INFORMATION, CAMERA_SETTINGS, STORAGE_INFORMATION, "
                                             "CAMERA_CAPTURE_STATUS, and the DEBUG parameter streams at the ui_demo rates. "
                                             "Also sent once automatically when the payload core's heartbeat first arrives."));
    connect(req, &QPushButton::clicked, this, [this] { report(m_link->requestPayloadInfo()); });

    m_component = valueLabel();
    m_camera = valueLabel();
    m_appVersion = valueLabel();
    m_distance = valueLabel();
    auto *dev = new QFormLayout;
    dev->addRow(req);
    dev->addRow(QStringLiteral("Component (model / software / serial)"), m_component);
    dev->addRow(QStringLiteral("Camera (vendor / model / firmware)"), m_camera);
    dev->addRow(QStringLiteral("Payload app version"), m_appVersion);
    dev->addRow(QStringLiteral("Rangefinder (DISTANCE_SENSOR)"), m_distance);
    auto *controls = new QWidget;
    auto *cl = new QVBoxLayout(controls);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(group(QStringLiteral("Device"), dev));

    m_params = new QTableWidget(0, 4);
    m_params->setHorizontalHeaderLabels({QStringLiteral("Index"), QStringLiteral("Name"), QStringLiteral("Value"), QStringLiteral("Age (s)")});
    m_params->verticalHeader()->hide();
    m_params->horizontalHeader()->setStretchLastSection(true);
    m_params->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto *pl = new QVBoxLayout;
    pl->addWidget(new QLabel(QStringLiteral("DEBUG messages from the payload core (compid %1): ind = SDK parameter index.")
                                 .arg(TelemetryStore::kPayloadCore)));
    pl->addWidget(m_params);
    setPage(controls, group(QStringLiteral("Payload parameter stream"), pl));
    // The status side stays readable while disconnected; only the request button needs a link.
}

static QString bytesText(const MavFrame &f, const char *name)
{
    QString s;
    for (const MavField &x : decodeFields(f)) {
        if (x.name != name) continue;
        if (x.isText) return QString::fromStdString(x.value.substr(1, x.value.size() - 2));
        for (int64_t c : x.ints) {
            if (!c) break;
            s += QChar(char(c));
        }
    }
    return s;
}

void TelemetryPage::refresh()
{
    const TelemetryStore &tel = m_link->telemetry();
    const int to = m_link->timeoutMs();
    const qint64 n = now();

    if (const CapturedFrame *c = tel.latest(MAVLINK_MSG_ID_COMPONENT_INFORMATION_BASIC))
        m_component->setText(agedValue(QStringLiteral("%1 / %2 / %3").arg(bytesText(c->frame, "model_name"),
                                                                         bytesText(c->frame, "software_version"),
                                                                         bytesText(c->frame, "serial_number")), c->monoNs, n, to));
    else
        m_component->setText(agedValue({}, -1, n, to));
    if (const CapturedFrame *c = tel.latest(MAVLINK_MSG_ID_CAMERA_INFORMATION)) {
        const quint32 fw = quint32(fieldNumber(c->frame, "firmware_version").value_or(0));
        // MAVLink encoding: (dev << 24) | (patch << 16) | (minor << 8) | major
        m_camera->setText(agedValue(QStringLiteral("%1 / %2 / %3.%4.%5.%6").arg(bytesText(c->frame, "vendor_name"), bytesText(c->frame, "model_name"))
                                        .arg(fw & 0xFF).arg((fw >> 8) & 0xFF).arg((fw >> 16) & 0xFF).arg(fw >> 24), c->monoNs, n, to));
    } else {
        m_camera->setText(agedValue({}, -1, n, to));
    }
    if (const CapturedFrame *c = tel.latest(MAVLINK_MSG_ID_DISTANCE_SENSOR))
        m_distance->setText(agedValue(QStringLiteral("%1 m").arg(fieldNumber(c->frame, "current_distance").value_or(NAN) / 100.0, 0, 'f', 2),
                                      c->monoNs, n, to));
    else
        m_distance->setText(agedValue({}, -1, n, to));

    const auto &params = tel.payloadParams();
    QStringList ver;
    qint64 verNs = -1;
    for (const char *k : {"APP_VER_X", "APP_VER_Y", "APP_VER_Z"}) {
        for (auto it = params.cbegin(); it != params.cend(); ++it) {
            if (PayloadLink::payloadParamName(it.key()) == QLatin1String(k)) {
                ver << QString::number(it->value);
                verNs = std::max(verNs, it->monoNs);
            }
        }
    }
    m_appVersion->setText(agedValue(ver.join(QLatin1Char('.')), ver.size() == 3 ? verNs : -1, n, to));

    m_params->setRowCount(params.size());
    int row = 0;
    for (auto it = params.cbegin(); it != params.cend(); ++it, ++row) {
        const double age = double(n - it->monoNs) / 1e9;
        const QString cells[4] = {QString::number(it.key()), PayloadLink::payloadParamName(it.key()),
                                  QString::number(it->value, 'g', 7)  /* DEBUG.value is a float */, QString::number(age, 'f', 1)};
        for (int col = 0; col < 4; ++col) {
            QTableWidgetItem *item = m_params->item(row, col);
            if (!item) m_params->setItem(row, col, item = new QTableWidgetItem);
            item->setText(cells[col]);
            item->setForeground(age * 1000 > to ? QColor(0x88, 0x88, 0x88) : palette().text().color());
        }
    }
}

// --- Commands --------------------------------------------------------------------------------

CommandsPage::CommandsPage(PayloadLink *link, const MavlinkDict *dict, QWidget *parent)
    : QWidget(parent), m_link(link), m_dict(dict), m_table(new QTableWidget(0, 9))
{
    m_table->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("Action"), QStringLiteral("Message"), QStringLiteral("Target"),
                                        QStringLiteral("State"), QStringLiteral("Result"), QStringLiteral("Queue → wire / ACK (ms)"),
                                        QStringLiteral("TX frame"), QStringLiteral("ACK frame")});
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height() + 6);
    m_table->setColumnWidth(0, 50);
    m_table->setColumnWidth(1, 330);
    m_table->setColumnWidth(2, 360);
    m_table->setColumnWidth(4, 150);
    m_table->setColumnWidth(5, 220);
    m_table->setColumnWidth(6, 160);
    auto *note = new QLabel(QStringLiteral(
        "Each UI action is matched to its outgoing frame and to the payload's COMMAND_ACK (by MAV_CMD and answering "
        "component) or PARAM_EXT_ACK (by param_id). \"No ACK (timeout)\" does not mean the command failed. "
        "GIMBAL_DEVICE_SET_ATTITUDE defines no ACK: check the gimbal telemetry. Double-click a frame number to open it "
        "in the inspector."));
    note->setWordWrap(true);
    auto *l = new QVBoxLayout(this);
    l->addWidget(note);
    l->addWidget(m_table);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (col < 7) return;
        bool ok = false;
        const quint64 idx = m_table->item(row, col) ? m_table->item(row, col)->text().toULongLong(&ok) : 0;
        if (ok) emit showFrame(idx);
    });
    connect(link, &PayloadLink::commandsChanged, this, &CommandsPage::refresh);
}

void CommandsPage::refresh()
{
    const auto &cmds = m_link->tracker().commands();
    m_table->setRowCount(int(cmds.size()));
    int row = 0;
    for (const TrackedCommand &t : cmds) {
        QString msg = QString::fromLatin1(messageName(t.msgid) ? messageName(t.msgid) : "?");
        if (t.command) {
            const QString c = m_dict ? m_dict->enumLabel(QStringLiteral("MAV_CMD"), t.command) : QString();
            msg += QStringLiteral(" / ") + (c.isEmpty() ? QString::number(t.command) : c);
        }
        if (!t.paramId.isEmpty()) msg += QStringLiteral(" / ") + t.paramId;
        QString result;
        if (t.result >= 0) {
            const QString e = t.msgid == MAVLINK_MSG_ID_PARAM_EXT_SET ? QStringLiteral("PARAM_ACK") : QStringLiteral("MAV_RESULT");
            const QString l = m_dict ? m_dict->enumLabel(e, t.result) : QString();
            result = QStringLiteral("%1 from comp %2").arg(l.isEmpty() ? QString::number(t.result) : l).arg(t.ackFrom);
            if (t.progress > 0 && t.progress <= 100) result += QStringLiteral(", %1%").arg(t.progress);
        }
        QString timing;
        if (t.sentMs >= 0) timing = QString::number(t.sentMs - t.requestedMs);
        if (t.ackMs >= 0) timing += QStringLiteral(" / ") + QString::number(t.ackMs - t.sentMs);
        const QString cells[9] = {QString::number(t.id), t.action, msg,
                                  t.targetSystem >= 0 ? QStringLiteral("%1/%2").arg(t.targetSystem).arg(t.targetComponent) : QString(),
                                  CommandTracker::stateText(t.state), result, timing,
                                  t.sentMs >= 0 ? QString::number(t.txFrame) : QString(),
                                  t.ackMs >= 0 ? QString::number(t.ackFrame) : QString()};
        QColor color = palette().text().color();
        if (t.state == TrackedCommand::Accepted) color = QColor(0x2e, 0x7d, 0x32);
        else if (t.state == TrackedCommand::Rejected || t.state == TrackedCommand::SendFailed || t.state == TrackedCommand::NotSent) color = QColor(0xc0, 0x1c, 0x28);
        else if (t.state == TrackedCommand::AckTimeout) color = QColor(0xb2, 0x6a, 0x00);
        for (int col = 0; col < 9; ++col) {
            QTableWidgetItem *item = m_table->item(row, col);
            if (!item) m_table->setItem(row, col, item = new QTableWidgetItem);
            item->setText(cells[col]);
            if (col == 4) item->setForeground(color);
        }
        ++row;
    }
    if (m_link->tracker().changes() != m_seenChanges && cmds.size()) m_table->scrollToBottom();
    m_seenChanges = m_link->tracker().changes();
}
