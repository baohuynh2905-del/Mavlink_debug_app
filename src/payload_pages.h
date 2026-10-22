#pragma once
// Payload control and status pages. Commands go through PayloadLink (SDK API, tracked);
// everything displayed as measured comes from validated RX frames (PayloadLink::telemetry()).

#include <QWidget>

class MavlinkDict;
class PayloadLink;
struct PayloadProfile;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QTableWidget;
class QTimer;

// Base: disables the controls while the link is not open and shows command errors.
class PayloadPage : public QWidget
{
    Q_OBJECT
public:
    PayloadPage(PayloadLink *link, const MavlinkDict *dict, QWidget *parent);

protected:
    void report(bool ok);          // shows PayloadLink::commandError() when a command was refused
    virtual void refresh() = 0;    // 4 Hz
    virtual void linkChanged() {}  // after controls are enabled/disabled
    QWidget *controls() const { return m_controls; }
    void setPage(QWidget *controls, QWidget *status);
    qint64 now() const;

    PayloadLink *m_link;
    const MavlinkDict *m_dict;

private:
    QWidget *m_controls = nullptr;
    QLabel *m_error;
};

class GimbalPage : public PayloadPage
{
    Q_OBJECT
public:
    GimbalPage(PayloadLink *link, const MavlinkDict *dict, QWidget *parent = nullptr);

protected:
    void refresh() override;
    void linkChanged() override;

private:
    QPushButton *holdButton(const QString &text, double pitchSign, double yawSign);
    void stopRate(bool force = false);  // force: send zero even if not moving (Stop button)

    QDoubleSpinBox *m_pitch, *m_roll, *m_yaw, *m_speed;
    QLabel *m_commanded, *m_attitude, *m_rates, *m_mode, *m_flags, *m_failures, *m_delta;
    QTimer *m_rateTimer;
    double m_ratePitch = 0, m_rateYaw = 0;
    QPushButton *m_held = nullptr;
    bool m_haveCommand = false;
    double m_cmdPitch = 0, m_cmdYaw = 0;
};

class CameraPage : public PayloadPage
{
    Q_OBJECT
public:
    CameraPage(PayloadLink *link, const MavlinkDict *dict, const PayloadProfile &profile, QWidget *parent = nullptr);

protected:
    void refresh() override;

private:
    QPushButton *holdButton(const QString &text, const QString &tip, bool zoom, int direction);

    const PayloadProfile &m_profile;
    QSlider *m_zoomRange;
    QLabel *m_capture, *m_video, *m_storage, *m_settings, *m_reported;
};

class TelemetryPage : public PayloadPage
{
    Q_OBJECT
public:
    TelemetryPage(PayloadLink *link, const MavlinkDict *dict, QWidget *parent = nullptr);

protected:
    void refresh() override;

private:
    QLabel *m_component, *m_camera, *m_appVersion, *m_distance;
    QTableWidget *m_params;
};

class CommandsPage : public QWidget
{
    Q_OBJECT
public:
    CommandsPage(PayloadLink *link, const MavlinkDict *dict, QWidget *parent = nullptr);

signals:
    void showFrame(quint64 frameIndex);  // double-click on a TX/ACK cell

private:
    void refresh();

    PayloadLink *m_link;
    const MavlinkDict *m_dict;
    QTableWidget *m_table;
    quint64 m_seenChanges = 0;
};
