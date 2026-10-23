#include "payload_pages.h"

#include "frame_text.h"
#include "mavlink_dict.h"
#include "payload_link.h"
#include "payload_profile.h"
#include "ui_util.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <cmath>

// ---------------------------------------------------------------------------------------------

PayloadPage::PayloadPage(PayloadLink *link, const MavlinkDict *dict, QWidget *parent)
    : QWidget(parent), m_link(link), m_dict(dict), m_error(new QLabel)
{
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

void PayloadPage::setPage(QWidget *controls, QWidget *status, bool statusFills)
{
    m_controls = controls;
    m_controls->setEnabled(isOpen(m_link->state()));
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(8, 4, 8, 8);
    l->addWidget(controls);
    l->addWidget(m_error);
    l->addWidget(status, statusFills ? 1 : 0);
    if (!statusFills) l->addStretch();
}

void PayloadPage::report(bool ok)
{
    m_error->setText(ok ? QString() : colored(m_link->commandError().toHtmlEscaped(), Theme::Bad));
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

GimbalPage::GimbalPage(PayloadLink *link, const MavlinkDict *dict, const PayloadProfile &profile, QWidget *parent)
    : PayloadPage(link, dict, parent), m_profile(profile)
{
    // Mode: the mode bits in GIMBAL_DEVICE_SET_ATTITUDE flags, Gremsy's route since payload software v307.54 (SDK
    // gimbal_set_mode example). A VIO on 3.1.8.21 rejects the older PARAM_EXT_SET GB_MODE.
    using C = GimbalFlags::Change;
    const struct { const char *label; C change; } modeList[] = {
        {"OFF…", C::MotorsOff}, {"LOCK", C::Lock}, {"FOLLOW", C::Follow}, {"MAPPING", C::Mapping}, {"RESET", C::HomeSet}};
    auto *modes = new QGridLayout;
    int i = 0;
    for (const auto &m : modeList) {
        const QString name = QString::fromUtf8(m.label).remove(QChar(0x2026));
        const bool off = m.change == C::MotorsOff;
        QPushButton *b = button(QString::fromUtf8(m.label),
                                QStringLiteral("GIMBAL_DEVICE_SET_ATTITUDE flags. It has no ACK: the mode the gimbal reports is lit. "
                                               "LOCK and FOLLOW also switch the motors back on after OFF."),
                                off ? "danger" : nullptr);
        connect(b, &QPushButton::clicked, this, [this, name, off, change = m.change] {
            if (off && QMessageBox::question(this, QStringLiteral("Gimbal OFF"),
                                             QStringLiteral("Switch the gimbal to OFF? The camera will not be stabilised "
                                                            "and may drop to its end stop.")) != QMessageBox::Yes)
                return;
            report(change == C::HomeSet ? m_link->gimbalHome() : m_link->gimbalMode(change, QStringLiteral("Gimbal mode ") + name));
        });
        modes->addWidget(b, i / 3, i % 3);
        m_modeButtons.emplace_back(name, b);
        ++i;
    }
    if (m_profile.gimbalControlId) {
        m_control = new QComboBox;
        for (const ParamOption &o : m_profile.gimbalControls) m_control->addItem(QString::fromLatin1(o.label), o.value);
        m_control->setCurrentIndex(-1);
        m_control->setPlaceholderText(QStringLiteral("not read yet"));
        m_control->setToolTip(QStringLiteral("PARAM_EXT_SET %1, stored on the payload. Who may control the gimbal: with Autopilot "
                                             "the payload drops this app's gimbal commands; an autopilot may need it back.")
                                  .arg(QLatin1String(m_profile.gimbalControlId)));
        connect(m_control, &QComboBox::activated, this, [this](int idx) {
            report(m_link->cameraParam(QStringLiteral("Gimbal control source"), m_profile.gimbalControlId, m_control->itemData(idx).toUInt()));
        });
        m_controlHint = new QLabel;
        m_controlHint->setWordWrap(true);
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(QStringLiteral("Control source")));
        row->addWidget(m_control, 1);
        modes->addLayout(row, 2, 0, 1, 3);
        modes->addWidget(m_controlHint, 3, 0, 1, 3);
    }

    // Rate: hold to move, re-sent every 100 ms like Gremsy's ui_demo, zero on release
    m_speed = new QSlider(Qt::Horizontal);
    m_speed->setRange(1, 90);
    m_speed->setValue(20);
    m_speed->setPageStep(5);
    m_speed->setToolTip(QStringLiteral("Speed of the arrow pad, also while a button is held. Compare with the measured rate "
                                       "in the telemetry below."));
    auto *speedValue = new QLabel;
    speedValue->setObjectName(QStringLiteral("readout"));
    speedValue->setFont(monoFont(11));
    speedValue->setMinimumWidth(speedValue->fontMetrics().horizontalAdvance(QStringLiteral("90 °/s ")));
    speedValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto showSpeed = [this, speedValue](int v) {
        speedValue->setText(QStringLiteral("%1 °/s").arg(v));
        // A held arrow keeps its direction and takes the new speed with the next 100 ms repeat
        if (m_ratePitch) m_ratePitch = std::copysign(v, m_ratePitch);
        if (m_rateYaw) m_rateYaw = std::copysign(v, m_rateYaw);
    };
    connect(m_speed, &QSlider::valueChanged, this, showSpeed);
    showSpeed(m_speed->value());
    m_rateTimer = new QTimer(this);
    m_rateTimer->setInterval(100);
    connect(m_rateTimer, &QTimer::timeout, this, [this] {
        if (!m_held || !m_held->isDown()) {  // safety net if a release event was missed
            stopRate();
            return;
        }
        m_link->gimbalRate(m_ratePitch, 0, m_rateYaw, false);
    });
    auto *pad = new QGridLayout;
    pad->setSpacing(4);
    pad->addWidget(holdButton(QStringLiteral("▲"), QStringLiteral("Pitch up"), 1, 0), 0, 1);
    pad->addWidget(holdButton(QStringLiteral("◀"), QStringLiteral("Yaw left"), 0, -1), 1, 0);
    QPushButton *stop = button(QStringLiteral("■"), QStringLiteral("Stop: sends zero rate"), "pad");
    connect(stop, &QPushButton::clicked, this, [this] { stopRate(true); });
    pad->addWidget(stop, 1, 1);
    pad->addWidget(holdButton(QStringLiteral("▶"), QStringLiteral("Yaw right"), 0, 1), 1, 2);
    pad->addWidget(holdButton(QStringLiteral("▼"), QStringLiteral("Pitch down"), -1, 0), 2, 1);
    auto *padRow = new QHBoxLayout;
    padRow->addStretch();
    padRow->addLayout(pad);
    padRow->addStretch();
    auto *speedRow = new QHBoxLayout;
    speedRow->addWidget(new QLabel(QStringLiteral("Speed")));
    speedRow->addWidget(m_speed, 1);
    speedRow->addWidget(speedValue);
    auto *rate = new QVBoxLayout;
    rate->addLayout(padRow);
    rate->addLayout(speedRow);

    // Angle
    auto spin = [](double limit, const QString &suffix) {
        auto *s = new QDoubleSpinBox;
        s->setRange(-limit, limit);
        s->setDecimals(1);
        s->setSuffix(suffix);
        return s;
    };
    m_pitch = spin(PayloadLink::kPitchLimit, QStringLiteral("°"));
    m_yaw = spin(PayloadLink::kYawLimit, QStringLiteral("°"));
    QPushButton *move = button(QStringLiteral("Move to angle"),
                               QStringLiteral("Turns the gimbal to the angles with rate commands, at most the Speed above, "
                                              "reading its measured attitude, and stops within %1°. (A VIO on software 3.1.8.21 "
                                              "accepts no angle command.) Yaw is in the frame the gimbal reports. Roll is held "
                                              "level by the gimbal.").arg(PayloadLink::kAngleTolerance), "primary");
    connect(move, &QPushButton::clicked, this, [this] {
        const bool ok = m_link->gimbalAngle(m_pitch->value(), m_yaw->value(), m_speed->value());
        report(ok);
        if (ok) {
            m_haveCommand = true;
            m_cmdPitch = m_pitch->value();
            m_cmdYaw = m_yaw->value();
            m_commanded->setText(QStringLiteral("pitch %1, yaw %2: moving").arg(deg(m_cmdPitch), deg(m_cmdYaw)));
        }
    });
    connect(link, &PayloadLink::angleFinished, this, [this](bool reached, const QString &detail) {
        report(reached);
        m_commanded->setText(QStringLiteral("pitch %1, yaw %2: %3").arg(deg(m_cmdPitch), deg(m_cmdYaw), detail.toHtmlEscaped()));
    });
    auto *angle = new QFormLayout;
    angle->addRow(QStringLiteral("Pitch (±90°)"), m_pitch);
    angle->addRow(QStringLiteral("Yaw (±180°)"), m_yaw);
    angle->addRow(move);

    auto *controls = new QWidget;
    auto *cl = new QVBoxLayout(controls);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(group(QStringLiteral("MODE"), modes));
    cl->addWidget(group(QStringLiteral("MOVE (HOLD)"), rate));
    cl->addWidget(group(QStringLiteral("POINT TO ANGLE"), angle));

    // Measured
    auto *tiles = new QHBoxLayout;
    const char *names[3] = {"PITCH", "ROLL", "YAW"};
    for (int k = 0; k < 3; ++k) {
        auto *caption = new QLabel(QString::fromLatin1(names[k]));
        caption->setObjectName(QStringLiteral("readoutCaption"));
        caption->setAlignment(Qt::AlignCenter);
        m_readout[k] = new QLabel;
        m_readout[k]->setObjectName(QStringLiteral("readout"));
        m_readout[k]->setFont(monoFont(14));
        m_readout[k]->setAlignment(Qt::AlignCenter);
        auto *col = new QVBoxLayout;
        col->setSpacing(2);
        col->addWidget(caption);
        col->addWidget(m_readout[k]);
        tiles->addLayout(col);
    }
    m_commanded = new QLabel(colored(QStringLiteral("no angle command yet"), Theme::Dim));
    m_attitude = valueLabel();
    m_rates = valueLabel();
    m_mode = valueLabel();
    m_flags = valueLabel();
    m_failures = valueLabel();
    m_delta = valueLabel();
    for (QLabel *l : {m_commanded, m_attitude, m_rates, m_mode, m_flags, m_failures, m_delta}) l->setWordWrap(true);
    auto *meas = new QFormLayout;
    meas->setRowWrapPolicy(QFormLayout::WrapLongRows);  // long flag names go under their label in the narrow column
    meas->addRow(tiles);
    meas->addRow(QStringLiteral("Source"), m_attitude);
    meas->addRow(QStringLiteral("Commanded"), m_commanded);
    meas->addRow(QStringLiteral("Measured − cmd"), m_delta);
    meas->addRow(QStringLiteral("Rate"), m_rates);
    meas->addRow(QStringLiteral("Mode (flags / GB_MODE)"), m_mode);
    meas->addRow(QStringLiteral("Flags"), m_flags);
    meas->addRow(QStringLiteral("Failure flags"), m_failures);
    QGroupBox *status = group(QStringLiteral("GIMBAL TELEMETRY"), meas);
    status->setToolTip(QStringLiteral("Measured values come from GIMBAL_DEVICE_ATTITUDE_STATUS. GIMBAL_DEVICE_SET_ATTITUDE has "
                                      "no acknowledgement, so this telemetry is the only confirmation. Yaw is in the frame "
                                      "given by the flags (vehicle or earth)."));
    setPage(controls, status);
}

QPushButton *GimbalPage::holdButton(const QString &text, const QString &tip, double pitchSign, double yawSign)
{
    QPushButton *b = button(text, tip + QStringLiteral(" (hold to move; release = zero rate)"), "pad");
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
    const int to = m_link->timeoutMs();
    const qint64 n = now();
    const auto &params = m_link->telemetry().payloadParams();
    QString gbMode = QStringLiteral("not received");
    for (auto it = params.cbegin(); it != params.cend(); ++it) {
        if (PayloadLink::payloadParamName(it.key()) != QLatin1String("GB_MODE")) continue;
        gbMode = QString::number(it->value);
        for (const ParamOption &o : m_profile.gimbalModes)
            if (o.value == unsigned(it->value)) gbMode = QLatin1String(o.label);
    }

    if (m_control) {
        m_link->readMissingCameraSetting(m_profile.gimbalControlId);
        const auto &cam = m_link->telemetry().cameraParams();
        const auto it = cam.constFind(QString::fromLatin1(m_profile.gimbalControlId));
        const int idx = it == cam.cend() ? -1 : m_control->findData(unsigned(it->value));
        if (!m_control->view()->isVisible() && m_control->currentIndex() != idx) m_control->setCurrentIndex(idx);  // never while open
        if (it == cam.cend())
            m_controlHint->setText(colored(QStringLiteral("Control source not read yet (read on connect)"), Theme::Dim));
        else if (unsigned(it->value) != m_profile.gimbalControlOnboard)
            m_controlHint->setText(colored(QStringLiteral("The payload ignores this app's gimbal commands (mode, move). Switch the "
                                                          "control source to %1 to control the gimbal from here.")
                                               .arg(m_control->itemText(m_control->findData(m_profile.gimbalControlOnboard))).toHtmlEscaped(),
                                           Theme::Bad));
        else
            m_controlHint->clear();
    }

    const CapturedFrame *c = m_link->telemetry().latest(MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS);
    GimbalAttitude a;
    const bool fresh = c && gimbalAttitude(c->frame, &a) && n - c->monoNs <= qint64(to) * 1000000;
    for (const auto &[name, b] : m_modeButtons) setStyleFlag(b, "active", fresh && GimbalFlags::modeName(a.flags).startsWith(name));
    if (!c || !gimbalAttitude(c->frame, &a)) {
        for (QLabel *l : {m_attitude, m_rates, m_mode, m_flags, m_failures, m_delta}) l->setText(agedValue({}, -1, 0, to));
        for (QLabel *l : m_readout) l->setText(colored(QStringLiteral("—"), Theme::Dim));
        return;
    }
    const qint64 t = c->monoNs;
    const bool stale = n - t > qint64(to) * 1000000;
    const double v[3] = {a.pitchDeg, a.rollDeg, a.yawDeg};
    for (int k = 0; k < 3; ++k) m_readout[k]->setText(stale ? colored(deg(v[k]), Theme::Dim) : deg(v[k]));
    m_attitude->setText(agedValue(QStringLiteral("%1/%2").arg(c->frame.sysid).arg(c->frame.compid), t, n, to));
    m_rates->setText(agedValue(QStringLiteral("x %1/s, y %2/s, z %3/s").arg(deg(a.rateX), deg(a.rateY), deg(a.rateZ)), t, n, to));
    m_mode->setText(agedValue(QStringLiteral("%1 / %2").arg(GimbalFlags::modeName(a.flags), gbMode), t, n, to));
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
    m_failures->setText(a.failureFlags && !stale ? colored(fail.toHtmlEscaped(), Theme::Bad) : agedValue(fail, t, n, to));
    m_delta->setText(m_haveCommand ? agedValue(QStringLiteral("pitch %1, yaw %2").arg(deg(a.pitchDeg - m_cmdPitch), deg(a.yawDeg - m_cmdYaw)), t, n, to)
                                   : colored(QStringLiteral("no angle command yet"), Theme::Dim));
}

// --- Camera ----------------------------------------------------------------------------------

CameraPage::CameraPage(PayloadLink *link, const MavlinkDict *dict, const PayloadProfile &profile, QWidget *parent)
    : PayloadPage(link, dict, parent), m_profile(profile)
{
    QPushButton *shot = button(QStringLiteral("◉  PHOTO"), QStringLiteral("MAV_CMD_IMAGE_START_CAPTURE, single image (saved on the payload's SD card)"), "primary");
    m_recBtn = button(QStringLiteral("●  REC"), QStringLiteral("MAV_CMD_VIDEO_START_CAPTURE (records on the payload's SD card). "
                                                             "Lit while CAMERA_CAPTURE_STATUS reports recording."), "danger");
    QPushButton *recStop = button(QStringLiteral("■  STOP"), QStringLiteral("MAV_CMD_VIDEO_STOP_CAPTURE"));
    QPushButton *photoMode = button(QStringLiteral("Photo mode"), QStringLiteral("MAV_CMD_SET_CAMERA_MODE, CAMERA_MODE_IMAGE"));
    QPushButton *videoMode = button(QStringLiteral("Video mode"), QStringLiteral("MAV_CMD_SET_CAMERA_MODE, CAMERA_MODE_VIDEO"));
    for (QPushButton *b : {shot, m_recBtn, recStop}) b->setMinimumHeight(38);
    connect(photoMode, &QPushButton::clicked, this, [this] { report(m_link->cameraMode(CAMERA_MODE_IMAGE)); });
    connect(videoMode, &QPushButton::clicked, this, [this] { report(m_link->cameraMode(CAMERA_MODE_VIDEO)); });
    connect(shot, &QPushButton::clicked, this, [this] { report(m_link->captureImage()); });
    connect(m_recBtn, &QPushButton::clicked, this, [this] { report(m_link->recordVideo(true)); });
    connect(recStop, &QPushButton::clicked, this, [this] { report(m_link->recordVideo(false)); });
    auto *capture = new QGridLayout;
    capture->addWidget(shot, 0, 0);
    capture->addWidget(m_recBtn, 0, 1);
    capture->addWidget(recStop, 0, 2);
    capture->addWidget(photoMode, 1, 0);
    capture->addWidget(videoMode, 1, 1);

    // Zoom and focus (MAVLink semantics: -1 wide/near, +1 tele/far, 0 stop)
    m_zoomReadout = new QLabel;
    m_zoomReadout->setObjectName(QStringLiteral("readout"));
    m_zoomReadout->setFont(monoFont(12));
    m_zoomReadout->setAlignment(Qt::AlignCenter);
    m_zoomReadout->setToolTip(QStringLiteral("EO_ZOOM, as the payload reports it in its DEBUG parameter stream"));
    auto *optics = new QGridLayout;
    optics->addWidget(holdButton(QStringLiteral("Zoom −"), QStringLiteral("ZOOM_TYPE_CONTINUOUS -1 (wide) while held, 0 on release"), true, -1), 0, 0);
    optics->addWidget(holdButton(QStringLiteral("Zoom +"), QStringLiteral("ZOOM_TYPE_CONTINUOUS +1 (tele) while held, 0 on release"), true, 1), 0, 1);
    optics->addWidget(m_zoomReadout, 0, 2);
    m_zoomRange = new QSlider(Qt::Horizontal);
    m_zoomRange->setRange(0, 100);
    QPushButton *zoomTo = button(QStringLiteral("Go 0%"), QStringLiteral("ZOOM_TYPE_RANGE: percentage of the full zoom range"));
    connect(m_zoomRange, &QSlider::valueChanged, zoomTo, [zoomTo](int v) { zoomTo->setText(QStringLiteral("Go %1%").arg(v)); });
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

    // Object tracking, as in the SDK's payload_do_object_tracking example
    auto *tracking = new QGridLayout;
    if (!m_profile.trackingModeId) {
        tracking->addWidget(new QLabel(QStringLiteral("Tracking not supported by %1").arg(QLatin1String(m_profile.name))), 0, 0);
    } else {
        m_armBtn = button(QStringLiteral("Select target"),
                          QStringLiteral("Turns object tracking on (PARAM_EXT_SET %1, the TRK_* parameter streams, MAV_CMD_USER_4 "
                                         "ACTIVE). Then click an object on the video: the gimbal turns to follow it.")
                              .arg(QLatin1String(m_profile.trackingModeId)), "primary");
        m_centreBtn = button(QStringLiteral("Track centre"), QStringLiteral("Track the object in the centre of the picture"));
        m_centreBtn->setEnabled(false);
        QPushButton *release = button(QStringLiteral("Stop tracking"), QStringLiteral("Stops tracking (MAV_CMD_USER_4, mode STOP). "
                                                                                      "The gimbal stays where it is."));
        connect(m_armBtn, &QPushButton::clicked, this, [this] {
            const bool ok = m_link->cameraParam(QStringLiteral("Tracking mode: object"), m_profile.trackingModeId, m_profile.trackingObjectMode) &&
                            m_link->trackingStreams() && m_link->trackingMode(PayloadLink::TrackActive);
            report(ok);
            if (ok) setTrackArmed(true);
        });
        connect(m_centreBtn, &QPushButton::clicked, this, [this] { trackAt(QPointF(0.5, 0.5)); });
        connect(release, &QPushButton::clicked, this, [this] {
            report(m_link->trackingMode(PayloadLink::TrackStop));
            setTrackArmed(false);
        });
        m_track = valueLabel();
        m_track->setWordWrap(true);
        tracking->addWidget(m_armBtn, 0, 0);
        tracking->addWidget(m_centreBtn, 0, 1);
        tracking->addWidget(release, 0, 2);
        tracking->addWidget(m_track, 1, 0, 1, 3);
    }

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
        cb->setMinimumContentsLength(8);
        cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);  // fits the side column
        settings->addRow(label, cb);
    };
    addParam(QStringLiteral("View source"), m_profile.viewSourceId, m_profile.viewSources);
    addParam(QStringLiteral("Record source"), m_profile.recordSourceId, m_profile.recordSources);
    addParam(QStringLiteral("IR palette"), m_profile.irPaletteId, m_profile.irPalettes);
    QPushButton *all = button(QStringLiteral("⚙  All camera settings…"),
                              QStringLiteral("Exposure, image, focus and zoom, thermal, OSD and rangefinder, with the values the "
                                             "camera reports"));
    all->setEnabled(!m_profile.cameraSettings.empty());
    connect(all, &QPushButton::clicked, this, [this] {
        if (!m_settingsDialog) {
            m_settingsDialog = new QDialog(this);
            m_settingsDialog->setWindowTitle(QStringLiteral("Camera settings · %1").arg(QLatin1String(m_profile.name)));
            auto *l = new QVBoxLayout(m_settingsDialog);
            l->setContentsMargins(0, 0, 0, 0);
            l->addWidget(new CameraSettingsPage(m_link, m_dict, m_profile));
            m_settingsDialog->resize(720, 520);
        }
        m_settingsDialog->show();
        m_settingsDialog->raise();
        m_settingsDialog->activateWindow();
    });
    settings->insertRow(0, all);

    auto *controls = new QWidget;
    auto *cl = new QVBoxLayout(controls);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(group(QStringLiteral("CAPTURE (%1)").arg(QLatin1String(m_profile.name)), capture));
    cl->addWidget(group(QStringLiteral("ZOOM / FOCUS"), optics));
    cl->addWidget(group(QStringLiteral("TRACKING"), tracking));
    cl->addWidget(group(QStringLiteral("SETTINGS"), settings));

    m_capture = valueLabel();
    m_video = valueLabel();
    m_storage = valueLabel();
    m_settings = valueLabel();
    m_reported = valueLabel();
    for (QLabel *l : {m_capture, m_video, m_storage, m_settings, m_reported}) l->setWordWrap(true);
    auto *st = new QFormLayout;
    st->setRowWrapPolicy(QFormLayout::WrapLongRows);
    st->addRow(QStringLiteral("Image capture"), m_capture);
    st->addRow(QStringLiteral("Video"), m_video);
    st->addRow(QStringLiteral("Storage"), m_storage);
    st->addRow(QStringLiteral("Settings"), m_settings);
    st->addRow(QStringLiteral("Payload reports"), m_reported);
    QGroupBox *status = group(QStringLiteral("CAMERA STATUS"), st);
    status->setToolTip(QStringLiteral("Values are read back from CAMERA_CAPTURE_STATUS, STORAGE_INFORMATION, CAMERA_SETTINGS "
                                      "and the payload's DEBUG parameter stream. They are sent on request "
                                      "(Analyze › Telemetry) or by the payload itself."));
    setPage(controls, status);
}

void CameraPage::setTrackArmed(bool armed)
{
    if (armed == m_trackArmed) return;
    m_trackArmed = armed;
    if (m_centreBtn) m_centreBtn->setEnabled(armed);
    if (m_armBtn) setStyleFlag(m_armBtn, "active", armed);
    emit trackArmedChanged(armed);
}

void CameraPage::linkChanged()
{
    if (!isOpen(m_link->state())) setTrackArmed(false);
}

void CameraPage::trackAt(QPointF pos)
{
    if (!m_trackArmed) return;  // a position sent without an active tracker moves the gimbal (SDK: "EagleEyes")
    report(m_link->trackingTarget(pos.x() * PayloadLink::kTrackerWidth, pos.y() * PayloadLink::kTrackerHeight));
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

    const OperatorStatus o = m_link->operatorStatus();
    m_zoomReadout->setText(o.eoZoom ? QStringLiteral("%1×").arg(*o.eoZoom, 0, 'f', 1) : colored(QStringLiteral("—"), Theme::Dim));
    const bool rec = o.recording.value_or(false);
    setStyleFlag(m_recBtn, "active", rec);
    m_recBtn->setText(rec ? QStringLiteral("●  REC %1").arg(clockText(o.recordingS)) : QStringLiteral("●  REC"));
    if (m_track) {
        QString t = o.trackStatus ? colored(trackingStateText(*o.trackMode, *o.trackStatus).toHtmlEscaped(),
                                            *o.trackStatus == PayloadLink::TrackTracked ? Theme::Ok
                                            : *o.trackStatus == PayloadLink::TrackLost ? Theme::Bad : Theme::Text)
                                  : colored(QStringLiteral("no TRK_STATUS received (the streams start on Select target)"), Theme::Dim);
        if (o.trackBox)
            t += colored(QStringLiteral(" · target %1, %2 (%3×%4)").arg(o.trackBox->center().x(), 0, 'f', 0).arg(o.trackBox->center().y(), 0, 'f', 0)
                             .arg(o.trackBox->width(), 0, 'f', 0).arg(o.trackBox->height(), 0, 'f', 0), Theme::Dim);
        if (m_trackArmed) t += QStringLiteral("<br>") + colored(QStringLiteral("Click an object on the video to track it"), Theme::Accent);
        m_track->setText(t);
    }
}

// --- Camera settings -------------------------------------------------------------------------

CameraSettingsPage::CameraSettingsPage(PayloadLink *link, const MavlinkDict *dict, const PayloadProfile &profile, QWidget *parent)
    : PayloadPage(link, dict, parent)
{
    QPushButton *read = button(QStringLiteral("Read from camera"), QStringLiteral("PARAM_EXT_REQUEST_LIST: the camera answers with every setting"));
    connect(read, &QPushButton::clicked, this, [this] { report(m_link->requestCameraSettings()); });
    auto *top = new QHBoxLayout;
    auto *hint = new QLabel(QStringLiteral("Each control shows what the camera reports. A change is sent at once (PARAM_EXT_SET) "
                                           "and read back; its ACK is in Analyze › Commands."));
    hint->setWordWrap(true);
    top->addWidget(hint, 1);
    top->addWidget(read);

    auto *tabs = new QTabWidget;
    QMap<QString, QGridLayout *> grids;
    for (const CameraSetting &cs : profile.cameraSettings) {
        const QString group = QString::fromLatin1(cs.group);
        QGridLayout *g = grids.value(group);
        if (!g) {
            auto *w = new QWidget;
            auto *outer = new QVBoxLayout(w);
            g = new QGridLayout;
            g->setColumnStretch(1, 1);
            g->setColumnMinimumWidth(2, 170);
            outer->addLayout(g);
            outer->addStretch();
            tabs->addTab(w, group);
            grids.insert(group, g);
        }
        Row row{&cs, nullptr, nullptr, nullptr, valueLabel()};
        const QString label = QString::fromLatin1(cs.label), id = QString::fromLatin1(cs.id);
        QWidget *control;
        if (!cs.options.empty()) {
            row.combo = new QComboBox;
            for (const ParamOption &o : cs.options) row.combo->addItem(QString::fromLatin1(o.label), o.value);
            row.combo->setCurrentIndex(-1);
            row.combo->setPlaceholderText(QStringLiteral("not read yet"));
            connect(row.combo, &QComboBox::activated, this, [this, combo = row.combo, label, id = cs.id](int i) {
                report(m_link->cameraParam(label, id, combo->itemData(i).toUInt()));
            });
            control = row.combo;
        } else {
            row.spin = new QSpinBox;
            row.spin->setRange(int(cs.min), int(cs.max));
            row.spin->setSingleStep(int(cs.step));
            QPushButton *set = row.set = button(QStringLiteral("Set"), QStringLiteral("PARAM_EXT_SET %1").arg(id));
            set->setEnabled(false);
            connect(set, &QPushButton::clicked, this, [this, spin = row.spin, label, id = cs.id] {
                report(m_link->cameraParam(label, id, quint32(spin->value())));
            });
            control = new QWidget;
            auto *h = new QHBoxLayout(control);
            h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(row.spin, 1);
            h->addWidget(set);
        }
        control->setToolTip(QStringLiteral("PARAM_EXT_SET %1").arg(id));
        const int r = g->rowCount();
        g->addWidget(new QLabel(label), r, 0);
        g->addWidget(control, r, 1);
        g->addWidget(row.reported, r, 2);
        m_rows.push_back(row);
    }

    auto *controls = new QWidget;
    auto *cl = new QVBoxLayout(controls);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addLayout(top);
    cl->addWidget(tabs);
    auto *note = new QLabel(QStringLiteral("IDs and values come from the SDK's %1 header. Settings persist on the camera.")
                                .arg(QString::fromLatin1(profile.name).toLower() + QStringLiteral("_sdk.h")));
    note->setWordWrap(true);
    setPage(controls, note);
}

void CameraSettingsPage::showEvent(QShowEvent *e)
{
    PayloadPage::showEvent(e);
    if (isOpen(m_link->state())) report(m_link->requestCameraSettings());
}

void CameraSettingsPage::refresh()
{
    if (!isVisible()) return;
    const auto &params = m_link->telemetry().cameraParams();
    const qint64 n = now();
    const int once = std::numeric_limits<int>::max();  // read on request: show the age, never "stale"
    for (const Row &r : m_rows) {
        m_link->readMissingCameraSetting(r.setting->id);
        const auto it = params.constFind(QString::fromLatin1(r.setting->id));
        if (r.set) r.set->setEnabled(it != params.cend());
        if (it == params.cend()) {
            r.reported->setText(agedValue({}, -1, n, once));
            continue;
        }
        const unsigned v = unsigned(it->value);
        QString text = QString::number(v);
        if (r.combo) {
            const int i = r.combo->findData(v);
            if (i >= 0) text = r.combo->itemText(i);
            else r.combo->setPlaceholderText(QStringLiteral("camera reports %1 (not in the SDK header)").arg(v));
            if (!r.combo->view()->isVisible() && r.combo->currentIndex() != i) r.combo->setCurrentIndex(i);  // never while open
        } else if (!r.spin->hasFocus() && r.spin->value() != int(v)) {
            r.spin->setValue(int(v));
        }
        r.reported->setText(agedValue(text, it->monoNs, n, once));
    }
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
    m_distance = valueLabel();
    auto *dev = new QFormLayout;
    dev->addRow(req);
    dev->addRow(QStringLiteral("Component (model / software / serial)"), m_component);
    dev->addRow(QStringLiteral("Camera (vendor / model / firmware)"), m_camera);
    dev->addRow(QStringLiteral("Rangefinder"), m_distance);
    auto *controls = new QWidget;
    auto *cl = new QVBoxLayout(controls);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(group(QStringLiteral("DEVICE"), dev));

    m_params = new QTableWidget(0, 4);
    m_params->setHorizontalHeaderLabels({QStringLiteral("Index"), QStringLiteral("Name"), QStringLiteral("Value"), QStringLiteral("Age (s)")});
    m_params->verticalHeader()->hide();
    m_params->horizontalHeader()->setStretchLastSection(true);
    m_params->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_params->verticalHeader()->setDefaultSectionSize(m_params->fontMetrics().height() + 6);
    auto *pl = new QVBoxLayout;
    pl->addWidget(new QLabel(QStringLiteral("DEBUG messages from the payload core (compid %1): ind = SDK parameter index.")
                                 .arg(TelemetryStore::kPayloadCore)));
    pl->addWidget(m_params);
    setPage(controls, group(QStringLiteral("PAYLOAD PARAMETER STREAM"), pl), true);
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
    const int once = std::numeric_limits<int>::max();  // one-off replies: show their age, never "stale"

    if (const CapturedFrame *c = tel.latest(MAVLINK_MSG_ID_COMPONENT_INFORMATION_BASIC))
        m_component->setText(agedValue(QStringLiteral("%1 / %2 / %3").arg(bytesText(c->frame, "model_name"),
                                                                         bytesText(c->frame, "software_version"),
                                                                         bytesText(c->frame, "serial_number")), c->monoNs, n, once));
    else
        m_component->setText(agedValue({}, -1, n, to));
    if (const CapturedFrame *c = tel.latest(MAVLINK_MSG_ID_CAMERA_INFORMATION)) {
        const quint32 fw = quint32(fieldNumber(c->frame, "firmware_version").value_or(0));
        // MAVLink defines (dev << 24) | (patch << 16) | (minor << 8) | major. A VIO running software 3.1.4.18
        // reports 0x03010412, i.e. the reverse, so both readings are shown instead of guessing.
        m_camera->setText(agedValue(QStringLiteral("%1 / %2 / 0x%3 (MAVLink order %4.%5.%6.%7, reversed %7.%6.%5.%4)")
                                        .arg(bytesText(c->frame, "vendor_name"), bytesText(c->frame, "model_name"))
                                        .arg(fw, 8, 16, QLatin1Char('0'))
                                        .arg(fw & 0xFF).arg((fw >> 8) & 0xFF).arg((fw >> 16) & 0xFF).arg(fw >> 24), c->monoNs, n, once));
    } else {
        m_camera->setText(agedValue({}, -1, n, to));
    }
    const auto &params = tel.payloadParams();
    if (const CapturedFrame *c = tel.latest(MAVLINK_MSG_ID_DISTANCE_SENSOR)) {
        m_distance->setText(agedValue(QStringLiteral("%1 m (DISTANCE_SENSOR)").arg(fieldNumber(c->frame, "current_distance").value_or(NAN) / 100.0, 0, 'f', 2),
                                      c->monoNs, n, to));
    } else {
        // The VIO streams its rangefinder as the LRF_RANGE parameter instead of DISTANCE_SENSOR
        m_distance->setText(agedValue({}, -1, n, to));
        for (auto it = params.cbegin(); it != params.cend(); ++it)
            if (PayloadLink::payloadParamName(it.key()) == QLatin1String("LRF_RANGE"))
                m_distance->setText(agedValue(QStringLiteral("%1 (LRF_RANGE parameter; unit not documented)").arg(it->value, 0, 'g', 6),
                                              it->monoNs, n, to));
    }

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
            item->setForeground(age * 1000 > to ? QColor(Theme::Dim) : palette().text().color());
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
        if (t.state == TrackedCommand::Accepted) color = QColor(Theme::Ok);
        else if (t.state == TrackedCommand::Rejected || t.state == TrackedCommand::SendFailed || t.state == TrackedCommand::NotSent) color = QColor(Theme::Bad);
        else if (t.state == TrackedCommand::AckTimeout) color = QColor(Theme::Warn);
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
