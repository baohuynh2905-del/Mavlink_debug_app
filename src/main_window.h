#pragma once

#include "captured_frame.h"
#include "mavlink_dict.h"
#include "profiles.h"

#include <QMainWindow>

class InspectorWidget;
class PayloadLink;
class QLabel;
class QLineEdit;
class QComboBox;
class QPushButton;
class QSpinBox;
class StatsWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    PayloadLink *link() const { return m_link; }

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void onConnectClicked();
    void onStateChanged();
    void onFrames(const std::vector<CapturedFrame> &frames);
    void refreshStats(bool tick = false);  // tick: the 1 Hz timer, which also samples the rates
    void saveSettings() const;
    void reloadProfiles(const QString &select = {});
    void saveProfile();
    void deleteProfile();

    MavlinkDict m_dict;
    PayloadLink *m_link;
    InspectorWidget *m_inspector;
    StatsWidget *m_statsPage;

    QList<ConnectionProfile> m_profiles;
    QComboBox *m_profileBox;
    QPushButton *m_saveProfile, *m_deleteProfile;
    QLineEdit *m_ip;
    QSpinBox *m_port;
    QSpinBox *m_timeout;
    QPushButton *m_connectBtn;
    QLabel *m_stateBadge;
    QLabel *m_stateDetail;
    QLabel *m_statsLabel;
    QLabel *m_heartbeatLabel;
    QLabel *m_captureLabel;

    TrafficStats m_stats;
    quint64 m_prevFrames[2] = {0, 0};
    quint64 m_rate[2] = {0, 0};  // frames per second, sampled by the 1 Hz tick
};
