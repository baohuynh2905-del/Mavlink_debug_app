#pragma once
// Payload control and status pages. Commands go through PayloadLink (SDK API, tracked);
// everything displayed as measured comes from validated RX frames (PayloadLink::telemetry()).

#include <QPointF>
#include <QWidget>

#include <utility>
#include <vector>

class MavlinkDict;
class PayloadLink;
struct CameraSetting;
struct PayloadProfile;
class QComboBox;
class QDialog;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
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
    void setPage(QWidget *controls, QWidget *status, bool statusFills = false);  // stacked vertically
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
    GimbalPage(PayloadLink *link, const MavlinkDict *dict, const PayloadProfile &profile, QWidget *parent = nullptr);

protected:
    void refresh() override;
    void linkChanged() override;

private:
    QPushButton *holdButton(const QString &text, const QString &tip, double pitchSign, double yawSign);
    void stopRate(bool force = false);  // force: send zero even if not moving (Stop button)

    const PayloadProfile &m_profile;
    QDoubleSpinBox *m_pitch, *m_yaw;
    QSlider *m_speed;  // °/s for the hold-to-move pad
    QLabel *m_readout[3];  // pitch, roll, yaw
    QLabel *m_commanded, *m_attitude, *m_rates, *m_mode, *m_flags, *m_failures, *m_delta;
    std::vector<std::pair<QString, QPushButton *>> m_modeButtons;  // GimbalFlags::modeName() prefix, lit when reported
    QComboBox *m_control = nullptr;  // the payload's gimbal control source
    QLabel *m_controlHint = nullptr;
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

    // "Armed" = Select target was pressed: the tracker is active and a click on the video picks the target.
    bool trackArmed() const { return m_trackArmed; }
    void trackAt(QPointF pos);  // 0..1 picture coordinates; only while armed

signals:
    void trackArmedChanged(bool armed);

protected:
    void refresh() override;
    void linkChanged() override;

private:
    QPushButton *holdButton(const QString &text, const QString &tip, bool zoom, int direction);
    void setTrackArmed(bool armed);

    const PayloadProfile &m_profile;
    QSlider *m_zoomRange;
    QPushButton *m_recBtn, *m_armBtn = nullptr, *m_centreBtn = nullptr;
    QDialog *m_settingsDialog = nullptr;
    QLabel *m_zoomReadout, *m_track = nullptr;
    QLabel *m_capture, *m_video, *m_storage, *m_settings, *m_reported;
    bool m_trackArmed = false;
};

// Every camera setting in the profile, showing what the camera reports (PARAM_EXT_VALUE). Opened as a dialog
// from the camera panel; reads all settings each time it is shown.
class CameraSettingsPage : public PayloadPage
{
    Q_OBJECT
public:
    CameraSettingsPage(PayloadLink *link, const MavlinkDict *dict, const PayloadProfile &profile, QWidget *parent = nullptr);

protected:
    void refresh() override;
    void showEvent(QShowEvent *e) override;

private:
    struct Row {
        const CameraSetting *setting;
        QComboBox *combo;  // list settings
        QSpinBox *spin;    // number settings
        QPushButton *set;  // enabled once the camera has reported the value, so an unread 0 is never sent
        QLabel *reported;
    };
    std::vector<Row> m_rows;
};

class TelemetryPage : public PayloadPage
{
    Q_OBJECT
public:
    TelemetryPage(PayloadLink *link, const MavlinkDict *dict, QWidget *parent = nullptr);

protected:
    void refresh() override;

private:
    QLabel *m_component, *m_camera, *m_distance;
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
