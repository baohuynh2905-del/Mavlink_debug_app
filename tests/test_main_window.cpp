// Offscreen GUI test. Set SCREENSHOT_DIR to also save a PNG of the window.
#include "inspector_widget.h"
#include "main_window.h"
#include "payload_pages.h"
#include "payload_link.h"
#include "video_page.h"
#include "theme.h"
#include "video_player.h"

#include <QApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QtTest>

#include <cmath>

#include "fake_payload.h"

class TestMainWindow : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);  // QSettings go to a test location
        QCoreApplication::setOrganizationName(QStringLiteral("GremsyTest"));
        applyTheme(*qApp);  // screenshots show the real look
    }

    // Saves a screenshot of each view and Analyze tab into dir, prefixed.
    static void shootViews(MainWindow &w, const QString &dir, const QString &prefix)
    {
        auto save = [&](const QString &name) {
            QTest::qWait(1100);  // 1 Hz refreshes
            w.grab().save(dir + QLatin1Char('/') + prefix + name + QStringLiteral(".png"));
        };
        w.findChild<QPushButton *>(QStringLiteral("navOperate"))->click();
        save(QStringLiteral("operate"));
        w.findChild<InspectorWidget *>()->setCollapsed(true);
        save(QStringLiteral("operate_monitor_collapsed"));
        w.findChild<InspectorWidget *>()->setCollapsed(false);
        auto *settings = w.findChild<QPushButton *>(QStringLiteral("settingsButton"));
        settings->setChecked(true);
        save(QStringLiteral("operate_settings"));
        settings->setChecked(false);
        w.findChild<QPushButton *>(QStringLiteral("navAnalyze"))->click();
        auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("analyzeTabs"));
        for (int i = 0; i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i);
            save(QStringLiteral("analyze_") + tabs->tabText(i).toLower());
        }
        w.findChild<QPushButton *>(QStringLiteral("navOperate"))->click();
    }

    void invalidIpIsRejectedWithoutConnecting()
    {
        MainWindow w;
        w.show();
        auto *ip = w.findChild<QLineEdit *>(QStringLiteral("ipEdit"));
        auto *connectBtn = w.findChild<QPushButton *>(QStringLiteral("connectButton"));
        QVERIFY(ip && connectBtn);
        QVERIFY(connectBtn->isEnabled());
        QCOMPARE(connectBtn->text(), QStringLiteral("Connect"));

        ip->setText(QStringLiteral("192.168.1"));
        connectBtn->click();
        QCOMPARE(w.link()->state(), LinkState::Failed);
        QVERIFY(ip->isEnabled());  // user can correct the input
        QCOMPARE(connectBtn->text(), QStringLiteral("Connect"));

    }

    void connectedWindowShowsLiveTraffic()
    {
        FakePayload fake;
        MainWindow w;
        w.show();
        auto *ip = w.findChild<QLineEdit *>(QStringLiteral("ipEdit"));
        auto *port = w.findChild<QSpinBox *>(QStringLiteral("portSpin"));
        auto *connectBtn = w.findChild<QPushButton *>(QStringLiteral("connectButton"));
        auto *table = w.findChild<QTableView *>(QStringLiteral("frameTable"));
        QVERIFY(ip && port && connectBtn && table);

        ip->setText(QStringLiteral("127.0.0.1"));
        port->setValue(fake.port);
        connectBtn->click();
        QVERIFY(!connectBtn->isEnabled());  // no double connect while the SDK initialises
        QTRY_COMPARE_WITH_TIMEOUT(connectBtn->text(), QStringLiteral("Disconnect"), 5000);
        QVERIFY(!ip->isEnabled() && !port->isEnabled());

        QVERIFY(fake.learnAppEndpoint());
        fake.sendHeartbeat(MAV_COMP_ID_USER2, MAV_TYPE_GENERIC);
        fake.sendHeartbeat(MAV_COMP_ID_CAMERA, MAV_TYPE_CAMERA);
        fake.sendHeartbeat(MAV_COMP_ID_GIMBAL, MAV_TYPE_GIMBAL);
        fake.send(QByteArray("\x01\x02junk", 6));
        QTRY_COMPARE_WITH_TIMEOUT(w.link()->state(), LinkState::Heartbeat, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount() >= 5, 3000);  // >= 1 TX heartbeat + 4 RX
        QTest::qWait(1100);  // let the 1 Hz panel refresh run

        // Select the gimbal's heartbeat so the details pane is filled
        for (int r = 0; r < table->model()->rowCount(); ++r) {
            if (table->model()->index(r, 6).data().toInt() == MAV_COMP_ID_GIMBAL) {
                table->setCurrentIndex(table->model()->index(r, 0));
                break;
            }
        }
        // Simulated payload telemetry so the control pages have something to show
        mavlink_message_t m;
        float q[4];
        mavlink_euler_to_quaternion(0, float(-30 * M_PI / 180), float(12 * M_PI / 180), q);
        mavlink_msg_gimbal_device_attitude_status_pack_chan(1, MAV_COMP_ID_GIMBAL, MAVLINK_COMM_0, &m, 0, 0, 0,
            GIMBAL_DEVICE_FLAGS_YAW_LOCK, q, 0, 0, 0, 0, NAN, NAN, 0);
        fake.send(m);
        for (auto [ind, v] : {std::pair<int, float>{0, 3}, {2, 152.3f}, {19, 1}, {20, 0}, {22, 2}}) {
            mavlink_msg_debug_pack_chan(1, MAV_COMP_ID_USER2, MAVLINK_COMM_0, &m, 0, uint8_t(ind), v);
            fake.send(m);
        }
        mavlink_msg_camera_capture_status_pack_chan(1, MAV_COMP_ID_CAMERA, MAVLINK_COMM_0, &m, 0, 0, 1, 0, 42000, 51200, 17, 0);
        fake.send(m);
        mavlink_msg_storage_information_pack_chan(1, MAV_COMP_ID_CAMERA, MAVLINK_COMM_0, &m, 0, 1, 1, STORAGE_STATUS_READY,
            61440, 10240, 51200, 0, 0, STORAGE_TYPE_SD, nullptr, 0);
        fake.send(m);
        QTRY_VERIFY_WITH_TIMEOUT(w.link()->telemetry().latest(MAVLINK_MSG_ID_STORAGE_INFORMATION), 3000);
        QVERIFY(w.link()->gimbalAngle(-30, 0, 10));

        // Picking a point on the video sends nothing until the tracker is armed: a bare target would move the gimbal
        auto *camera = w.findChild<CameraPage *>();
        QVERIFY(camera && !camera->trackArmed());
        const size_t before = w.link()->tracker().commands().size();
        camera->trackAt(QPointF(0.5, 0.5));
        QCOMPARE(w.link()->tracker().commands().size(), before);

        if (const QByteArray dir = qgetenv("SCREENSHOT_DIR"); !dir.isEmpty()) {
            w.resize(1440, 900);
            shootViews(w, QString::fromLocal8Bit(dir), {});
        }

        connectBtn->click();
        QTRY_COMPARE_WITH_TIMEOUT(w.link()->state(), LinkState::Disconnected, 3000);
        QVERIFY(ip->isEnabled());
    }

    // Read-only: connects to a real payload and screenshots each tab. Runs only when HW_IP is set.
    void hardwareScreenshots()
    {
        const QByteArray ip = qgetenv("HW_IP"), dir = qgetenv("SCREENSHOT_DIR");
        if (ip.isEmpty() || dir.isEmpty()) QSKIP("set HW_IP and SCREENSHOT_DIR to run against a payload");
        MainWindow w;
        w.show();
        w.findChild<QLineEdit *>(QStringLiteral("ipEdit"))->setText(QString::fromLatin1(ip));
        w.findChild<QSpinBox *>(QStringLiteral("portSpin"))->setValue(14566);
        w.findChild<QPushButton *>(QStringLiteral("connectButton"))->click();
        QTRY_COMPARE_WITH_TIMEOUT(w.link()->state(), LinkState::Heartbeat, 8000);
        auto *video = w.findChild<VideoPage *>();
        w.findChild<QPushButton *>(QStringLiteral("videoStartStop"))->click();
        QTRY_COMPARE_WITH_TIMEOUT(video->player()->state(), VideoPlayer::State::Playing, 15000);
        QTest::qWait(4000);  // info replies and parameter streams
        shootViews(w, QString::fromLocal8Bit(dir), QStringLiteral("hw_"));
        w.findChild<QPushButton *>(QStringLiteral("connectButton"))->click();
        QTRY_COMPARE_WITH_TIMEOUT(w.link()->state(), LinkState::Disconnected, 3000);
    }
};

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
