// Offscreen GUI test. Set SCREENSHOT_DIR to also save a PNG of the window.
#include "inspector_widget.h"
#include "main_window.h"
#include "payload_pages.h"
#include "payload_link.h"
#include "splash.h"
#include "video_page.h"
#include "theme.h"
#include "video_player.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QSlider>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QtTest>

#include <cmath>
#include <functional>

#include "fake_payload.h"

class TestMainWindow : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);  // QSettings go to a test location
        QCoreApplication::setOrganizationName(QStringLiteral("GremsyTest"));
        QSettings().clear();  // e.g. the model a previous run connected with
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

    void bootSplashEndsByItselfOrOnClick()
    {
        MainWindow w;
        const std::vector<BootCheck> checks = w.bootChecks();
        QCOMPARE(checks.size(), size_t(4));
        QVERIFY(checks[2].value.startsWith(QStringLiteral("VIO")));

        BootSplash s(checks);
        QSignalSpy done(&s, &BootSplash::finished);
        s.start();
        QTest::qWait(1500);
        QVERIFY(done.isEmpty() && s.isVisible());
        if (const QByteArray dir = qgetenv("SCREENSHOT_DIR"); !dir.isEmpty())
            s.grab().save(QString::fromLocal8Bit(dir) + QStringLiteral("/splash.png"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, BootSplash::kShowMs);
        QTRY_VERIFY_WITH_TIMEOUT(!s.isVisible(), 2 * BootSplash::kFadeMs);

        BootSplash skipped(checks);
        QSignalSpy skip(&skipped, &BootSplash::finished);
        skipped.start();
        QTest::mouseClick(&skipped, Qt::LeftButton);
        QCOMPARE(skip.size(), 1);  // at once
        QTest::mouseClick(&skipped, Qt::LeftButton);
        QCOMPARE(skip.size(), 1);  // only once
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
        QVERIFY(w.link()->gimbalAngle(-30, 12));  // already there: ends at once

        // Picking a point on the video sends nothing until the tracker is armed: a bare target would move the gimbal
        auto *camera = w.findChild<CameraPage *>();
        QVERIFY(camera && !camera->trackArmed());
        const size_t before = w.link()->tracker().commands().size();
        camera->trackAt(QPointF(0.5, 0.5));
        QCOMPARE(w.link()->tracker().commands().size(), before);

        auto buttonText = [&](const QString &text) {
            for (QPushButton *b : w.findChildren<QPushButton *>()) if (b->text() == text) return b;
            return static_cast<QPushButton *>(nullptr);
        };
        auto sent = [&](const std::function<bool(const MavFrame &)> &pred) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < 3000) {
                for (const MavFrame &f : fake.received()) if (pred(f)) return true;
                QTest::qWait(10);
            }
            return false;
        };
        auto user4 = [](const MavFrame &f, float p3, float p4) {
            return f.msgid == MAVLINK_MSG_ID_COMMAND_LONG && mavlink_msg_command_long_get_command(&f.msg) == MAV_CMD_USER_4 &&
                   mavlink_msg_command_long_get_param3(&f.msg) == p3 && mavlink_msg_command_long_get_param4(&f.msg) == p4;
        };
        fake.received();
        QVERIFY(buttonText(QStringLiteral("Select target")) && buttonText(QStringLiteral("Stop tracking")));
        buttonText(QStringLiteral("Select target"))->click();
        QVERIFY(camera->trackArmed());
        QVERIFY(sent([&](const MavFrame &f) { return user4(f, 0, 1); }));  // ACTIVE
        camera->trackAt(QPointF(0.25, 0.25));                                  // a click on the video
        QVERIFY(sent([&](const MavFrame &f) { return user4(f, 1, 480) && mavlink_msg_command_long_get_param5(&f.msg) == 270; }));
        buttonText(QStringLiteral("Stop tracking"))->click();
        QVERIFY(!camera->trackArmed());
        QVERIFY(sent([&](const MavFrame &f) { return user4(f, 0, 0); }));  // STOP

        // Speed slider: holding an arrow sends the slider's speed, scaled by the VIO profile
        auto *speed = w.findChild<QSlider *>();
        for (QSlider *sl : w.findChildren<QSlider *>()) if (sl->maximum() == 90) speed = sl;
        QVERIFY(speed);
        speed->setValue(30);
        QPushButton *up = buttonText(QStringLiteral("▲"));
        QVERIFY(up);
        QTest::mousePress(up, Qt::LeftButton);
        QVERIFY(sent([](const MavFrame &f) {
            return f.msgid == MAVLINK_MSG_ID_GIMBAL_DEVICE_SET_ATTITUDE &&
                   std::abs(mavlink_msg_gimbal_device_set_attitude_get_angular_velocity_y(&f.msg) - float(30 * M_PI / 180)) < 1e-4f;
        }));
        QTest::mouseRelease(up, Qt::LeftButton);
        QVERIFY(sent([](const MavFrame &f) {
            return f.msgid == MAVLINK_MSG_ID_GIMBAL_DEVICE_SET_ATTITUDE && mavlink_msg_gimbal_device_set_attitude_get_angular_velocity_y(&f.msg) == 0;
        }));

        // Mode buttons: the mode flags in GIMBAL_DEVICE_SET_ATTITUDE (the gimbal reported LOCK above), no angle
        QVERIFY(buttonText(QStringLiteral("LOCK"))->property("active").toBool());
        buttonText(QStringLiteral("FOLLOW"))->click();
        QVERIFY(sent([](const MavFrame &f) {
            if (f.msgid != MAVLINK_MSG_ID_GIMBAL_DEVICE_SET_ATTITUDE) return false;
            float q[4];
            mavlink_msg_gimbal_device_set_attitude_get_q(&f.msg, q);
            return std::isnan(q[0]) && mavlink_msg_gimbal_device_set_attitude_get_flags(&f.msg) == 0;
        }));

        // Control source: shows what the payload reports, warns unless Onboard computer, and switches it
        QComboBox *control = nullptr;
        for (QComboBox *c : w.findChildren<QComboBox *>())
            if (c->findText(QStringLiteral("Onboard computer")) >= 0) control = c;
        QVERIFY(control);
        char fid[16] = {0}, fval[128] = {0};  // fval[0] = 0: Autopilot
        strncpy(fid, "GB_FW_FLAG", sizeof fid);
        mavlink_msg_param_ext_value_pack_chan(1, MAV_COMP_ID_CAMERA, MAVLINK_COMM_0, &m, fid, fval, MAV_PARAM_EXT_TYPE_UINT32, 44, 52);
        fake.send(m);
        QTRY_COMPARE_WITH_TIMEOUT(control->currentText(), QStringLiteral("Autopilot"), 3000);
        QLabel *hint = nullptr;
        for (QLabel *l : w.findChildren<QLabel *>()) if (l->text().contains(QStringLiteral("ignores this app"))) hint = l;
        QVERIFY(hint);
        emit control->activated(control->findText(QStringLiteral("Onboard computer")));
        QVERIFY(sent([](const MavFrame &f) {
            if (f.msgid != MAVLINK_MSG_ID_PARAM_EXT_SET) return false;
            char pid[17] = {0}, v[129] = {0};
            mavlink_msg_param_ext_set_get_param_id(&f.msg, pid);
            mavlink_msg_param_ext_set_get_param_value(&f.msg, v);
            return QLatin1String(pid) == QLatin1String("GB_FW_FLAG") && v[0] == 1;
        }));
        fval[0] = 1;  // the payload's read-back
        mavlink_msg_param_ext_value_pack_chan(1, MAV_COMP_ID_CAMERA, MAVLINK_COMM_0, &m, fid, fval, MAV_PARAM_EXT_TYPE_UINT32, 44, 52);
        fake.send(m);
        QTRY_VERIFY_WITH_TIMEOUT(hint->text().isEmpty(), 3000);

        // Camera settings dialog: the controls follow what the camera reports
        char id[16] = {0}, value[128] = {0};
        strncpy(id, "C_V_WB", sizeof id);
        value[0] = 2;  // Outdoor
        mavlink_msg_param_ext_value_pack_chan(1, MAV_COMP_ID_CAMERA, MAVLINK_COMM_0, &m, id, value, MAV_PARAM_EXT_TYPE_UINT32, 44, 30);
        fake.send(m);
        QPushButton *all = nullptr;
        for (QPushButton *b : w.findChildren<QPushButton *>())
            if (b->text().contains(QStringLiteral("All camera settings"))) all = b;
        QVERIFY(all && all->isEnabled());
        all->click();
        auto *dialog = w.findChild<QDialog *>();
        QVERIFY(dialog && dialog->isVisible());
        QComboBox *wb = nullptr;
        for (QComboBox *c : dialog->findChildren<QComboBox *>())
            if (c->findText(QStringLiteral("ATW (auto tracing)")) >= 0) wb = c;
        QVERIFY(wb);
        QTRY_COMPARE_WITH_TIMEOUT(wb->currentText(), QStringLiteral("Outdoor"), 3000);
        bool listRequested = false;  // opening the dialog reads every setting
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const MavFrame &f : fake.received()) listRequested |= f.msgid == MAVLINK_MSG_ID_PARAM_EXT_REQUEST_LIST;
            return listRequested;
        }(), 3000);
        if (const QByteArray dir = qgetenv("SCREENSHOT_DIR"); !dir.isEmpty()) {
            QTest::qWait(300);
            dialog->grab().save(QString::fromLocal8Bit(dir) + QStringLiteral("/camera_settings.png"));
        }
        dialog->close();

        if (const QByteArray dir = qgetenv("SCREENSHOT_DIR"); !dir.isEmpty()) {
            w.resize(1440, 900);
            shootViews(w, QString::fromLocal8Bit(dir), {});
        }

        connectBtn->click();
        QTRY_COMPARE_WITH_TIMEOUT(w.link()->state(), LinkState::Disconnected, 3000);
        QVERIFY(ip->isEnabled());
    }

    // Choosing another model rebuilds the gimbal and camera pages from its profile; a different reported model warns
    void modelSwitchRebuildsPages()
    {
        FakePayload fake;
        MainWindow w;
        w.show();
        auto *model = w.findChild<QComboBox *>(QStringLiteral("modelBox"));
        QVERIFY(model && model->count() == 4 && model->currentText() == QStringLiteral("VIO"));
        w.findChild<QLineEdit *>(QStringLiteral("ipEdit"))->setText(QStringLiteral("127.0.0.1"));
        w.findChild<QSpinBox *>(QStringLiteral("portSpin"))->setValue(fake.port);
        w.findChild<QPushButton *>(QStringLiteral("connectButton"))->click();  // controls are enabled only while connected
        QTRY_VERIFY_WITH_TIMEOUT(isOpen(w.link()->state()), 5000);
        QVERIFY(fake.learnAppEndpoint());

        for (int i = 0; i < model->count(); ++i) {  // every model builds its pages and its settings dialog
            model->setCurrentIndex(i);
            emit model->activated(i);
            QCOMPARE(w.findChildren<CameraPage *>().size(), qsizetype(1));  // the previous pages are gone
            QCOMPARE(w.findChildren<GimbalPage *>().size(), qsizetype(1));
            auto *camera = w.findChild<CameraPage *>();
            QPushButton *all = nullptr;
            for (QPushButton *b : camera->findChildren<QPushButton *>())
                if (b->text().contains(QStringLiteral("All camera settings"))) all = b;
            QVERIFY(all && all->isEnabled());
            all->click();
            auto *dialog = camera->findChild<QDialog *>();
            QVERIFY(dialog);
            auto *tabs = dialog->findChild<QTabWidget *>();
            QVERIFY2(tabs && tabs->count() >= 2, qPrintable(model->currentText()));
            dialog->close();
        }

        model->setCurrentText(QStringLiteral("ZIO"));
        emit model->activated(model->currentIndex());
        bool noPalette = false;  // EO-only model
        for (QComboBox *c : w.findChild<CameraPage *>()->findChildren<QComboBox *>())
            noPalette |= c->currentText() == QStringLiteral("not supported by ZIO");
        QVERIFY(noPalette);
        QVERIFY(w.findChild<QLabel *>(QStringLiteral("appSubtitle"))->text().contains(QStringLiteral("ZIO")));
        mavlink_message_t m;
        mavlink_msg_component_information_basic_pack_chan(1, MAV_COMP_ID_USER2, MAVLINK_COMM_0, &m, 0, 0, 0, "Gremsy JSC", "Vio",
                                                          "3.1.8.21", "V42A", "1");
        fake.send(m);
        QTRY_VERIFY_WITH_TIMEOUT(w.findChild<QLabel *>(QStringLiteral("warning"))->text().contains(QStringLiteral("PAYLOAD REPORTS VIO, MODEL SET TO ZIO")), 3000);
        model->setCurrentText(QStringLiteral("VIO"));
        emit model->activated(model->currentIndex());
        QTRY_VERIFY_WITH_TIMEOUT(!w.findChild<QLabel *>(QStringLiteral("warning"))->text().contains(QStringLiteral("MODEL SET TO")), 3000);
        w.findChild<QPushButton *>(QStringLiteral("connectButton"))->click();
        QTRY_COMPARE_WITH_TIMEOUT(w.link()->state(), LinkState::Disconnected, 3000);
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

        // Camera settings dialog with the camera's values (opening it only reads)
        for (QPushButton *b : w.findChildren<QPushButton *>()) if (b->text().contains(QStringLiteral("All camera settings"))) b->click();
        auto *dialog = w.findChild<QDialog *>();
        QVERIFY(dialog);
        QTRY_VERIFY_WITH_TIMEOUT(w.link()->telemetry().cameraParams().size() >= 40, 5000);
        QTest::qWait(500);
        auto *tabs = dialog->findChild<QTabWidget *>();
        for (int i = 0; i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i);
            QTest::qWait(400);
            dialog->grab().save(QString::fromLocal8Bit(dir) + QStringLiteral("/hw_camera_settings_%1.png").arg(i));
        }
        dialog->close();

        // HW_TRACK=1 also tracks the picture centre (approved: the gimbal may move a little) to compare the HUD box
        // with the box the camera's own OSD draws
        if (qgetenv("HW_TRACK") == "1") {
            auto *camera = w.findChild<CameraPage *>();
            for (QPushButton *b : w.findChildren<QPushButton *>()) if (b->text() == QStringLiteral("Select target")) b->click();
            QVERIFY(camera->trackArmed());
            camera->trackAt(QPointF(0.5, 0.5));
            QTRY_VERIFY_WITH_TIMEOUT(w.link()->operatorStatus().trackStatus.value_or(-1) == PayloadLink::TrackTracked, 3000);
            QTest::qWait(1500);
            video->view()->grab().save(QString::fromLocal8Bit(dir) + QStringLiteral("/hw_tracking.png"));
            for (QPushButton *b : w.findChildren<QPushButton *>()) if (b->text() == QStringLiteral("Stop tracking")) b->click();
            QTRY_VERIFY_WITH_TIMEOUT(w.link()->operatorStatus().trackMode.value_or(-1) == PayloadLink::TrackStop, 3000);
        }
        w.findChild<QPushButton *>(QStringLiteral("connectButton"))->click();
        QTRY_COMPARE_WITH_TIMEOUT(w.link()->state(), LinkState::Disconnected, 3000);
    }
};

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
