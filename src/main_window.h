#pragma once

#include "captured_frame.h"
#include "mavlink_dict.h"
#include "profiles.h"
#include "splash.h"

#include <QList>
#include <QMainWindow>

class CameraPage;
class InspectorWidget;
class PayloadLink;
class QButtonGroup;
class QLabel;
class QLineEdit;
class QComboBox;
class QPushButton;
class QSpinBox;
class QScrollArea;
class QSplitter;
struct PayloadProfile;
class QStackedWidget;
class StatsWidget;
class VideoPage;

// Header (status lamps, connect, settings drawer), the Operate workspace (gimbal | video | camera)
// and the Analyze view (commands, telemetry, statistics), with the MAVLink monitor below both.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    PayloadLink *link() const { return m_link; }
    std::vector<BootCheck> bootChecks() const;  // what the constructor loaded, for the boot splash

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void onConnectClicked();
    void onStateChanged();
    void onFrames(const std::vector<CapturedFrame> &frames);
    void refreshStats(bool tick = false);  // tick: the 1 Hz timer, which also samples the rates
    void refreshOperator();                // 4 Hz: lamps, warnings and the video overlay
    void setDrawerOpen(bool open);
    void saveSettings() const;
    void reloadProfiles(const QString &select = {});
    void saveProfile();
    void deleteProfile();
    void setModel(const PayloadProfile &p);  // rebuilds the gimbal and camera pages for this payload model

    MavlinkDict m_dict;
    PayloadLink *m_link;
    InspectorWidget *m_inspector;
    StatsWidget *m_statsPage;
    VideoPage *m_video;
    CameraPage *m_camera = nullptr;
    const PayloadProfile *m_profile = nullptr;
    QScrollArea *m_gimbalCol, *m_cameraCol;
    QComboBox *m_modelBox;
    QLabel *m_subtitle;

    QList<ConnectionProfile> m_profiles;
    QComboBox *m_profileBox;
    QPushButton *m_saveProfile, *m_deleteProfile;
    QLineEdit *m_ip;
    QSpinBox *m_port;
    QSpinBox *m_timeout;
    QPushButton *m_connectBtn, *m_settingsBtn;
    QLabel *m_payloadLamp, *m_linkLamp, *m_videoLamp, *m_rates, *m_warning;
    QLabel *m_stateBadge;
    QLabel *m_stateDetail;
    QLabel *m_statsLabel;
    QLabel *m_heartbeatLabel;
    QLabel *m_captureLabel;
    QWidget *m_drawer, *m_monitor;
    QSplitter *m_hsplit, *m_vsplit;
    QStackedWidget *m_views;
    QButtonGroup *m_nav;
    QList<int> m_expandedSizes;  // monitor splitter sizes before collapsing
    bool m_hideDrawerOnHeartbeat = false;

    TrafficStats m_stats;
    quint64 m_prevFrames[2] = {0, 0};
    quint64 m_rate[2] = {0, 0};  // frames per second, sampled by the 1 Hz tick
};
