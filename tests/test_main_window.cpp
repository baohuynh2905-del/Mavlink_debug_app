// Offscreen GUI test. Set SCREENSHOT_DIR to also save a PNG of the window.
#include "main_window.h"
#include "payload_link.h"

#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QtTest>

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
        if (const QByteArray dir = qgetenv("SCREENSHOT_DIR"); !dir.isEmpty()) {
            w.grab().save(QString::fromLocal8Bit(dir) + QStringLiteral("/main_window.png"));
            auto *tabs = w.findChild<QTabWidget *>();
            tabs->setCurrentIndex(1);
            QTest::qWait(1100);  // statistics refresh at 1 Hz
            w.grab().save(QString::fromLocal8Bit(dir) + QStringLiteral("/statistics.png"));
        }

        connectBtn->click();
        QTRY_COMPARE_WITH_TIMEOUT(w.link()->state(), LinkState::Disconnected, 3000);
        QVERIFY(ip->isEnabled());
    }
};

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
