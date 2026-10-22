// Offscreen GUI test. Set SCREENSHOT_DIR to also save a PNG of the window.
#include "main_window.h"
#include "payload_link.h"

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
        fake.sendHeartbeat(MAV_COMP_ID_CAMERA2, MAV_TYPE_CAMERA);
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
        mavlink_msg_camera_capture_status_pack_chan(1, MAV_COMP_ID_CAMERA2, MAVLINK_COMM_0, &m, 0, 0, 1, 0, 42000, 51200, 17, 0);
        fake.send(m);
        mavlink_msg_storage_information_pack_chan(1, MAV_COMP_ID_CAMERA2, MAVLINK_COMM_0, &m, 0, 1, 1, STORAGE_STATUS_READY,
            61440, 10240, 51200, 0, 0, STORAGE_TYPE_SD, nullptr, 0);
        fake.send(m);
        QTRY_VERIFY_WITH_TIMEOUT(w.link()->telemetry().latest(MAVLINK_MSG_ID_STORAGE_INFORMATION), 3000);
        QVERIFY(w.link()->gimbalAngle(-30, 0, 10));

        if (const QByteArray dir = qgetenv("SCREENSHOT_DIR"); !dir.isEmpty()) {
            auto *tabs = w.findChild<QTabWidget *>();
            auto shoot = [&](const QString &tab, const QString &file) {
                for (int i = 0; i < tabs->count(); ++i)
                    if (tabs->tabText(i).startsWith(tab)) tabs->setCurrentIndex(i);
                QTest::qWait(1100);  // 1 Hz refreshes
                w.grab().save(QString::fromLocal8Bit(dir) + QLatin1Char('/') + file);
            };
            shoot(QStringLiteral("MAVLink"), QStringLiteral("main_window.png"));
            shoot(QStringLiteral("Statistics"), QStringLiteral("statistics.png"));
            shoot(QStringLiteral("Gimbal"), QStringLiteral("gimbal.png"));
            shoot(QStringLiteral("Camera"), QStringLiteral("camera.png"));
            shoot(QStringLiteral("Telemetry"), QStringLiteral("telemetry.png"));
            shoot(QStringLiteral("Commands"), QStringLiteral("commands.png"));
        }

        connectBtn->click();
        QTRY_COMPARE_WITH_TIMEOUT(w.link()->state(), LinkState::Disconnected, 3000);
        QVERIFY(ip->isEnabled());
    }
};

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
