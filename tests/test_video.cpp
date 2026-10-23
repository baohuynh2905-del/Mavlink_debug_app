// VideoPlayer against a local RTSP server (gst-rtsp-server) streaming an H.264 test pattern.
// GLib/gio headers first: they use "signals" as an identifier, which Qt defines as a macro.
#include <gst/rtsp-server/rtsp-server.h>

#include "video_page.h"
#include "video_player.h"

#include <QLineEdit>
#include <QPushButton>
#include <QtTest>

class TestVideo : public QObject
{
    Q_OBJECT
    GstRTSPServer *m_server = nullptr;
    guint m_sourceId = 0;
    QString m_url;

private slots:
    void initTestCase()
    {
        gst_init(nullptr, nullptr);
        m_server = gst_rtsp_server_new();
        gst_rtsp_server_set_address(m_server, "127.0.0.1");
        gst_rtsp_server_set_service(m_server, "0");  // any free port
        GstRTSPMediaFactory *f = gst_rtsp_media_factory_new();
        // H.264 like the VIO (openh264enc from gst-plugins-bad; jpegenc output is rejected by rtpjpegpay here)
        gst_rtsp_media_factory_set_launch(f, "( videotestsrc is-live=true ! video/x-raw,width=320,height=240,framerate=30/1 "
                                             "! openh264enc ! rtph264pay name=pay0 pt=96 config-interval=1 )");
        gst_rtsp_media_factory_set_shared(f, TRUE);
        gst_rtsp_mount_points_add_factory(gst_rtsp_server_get_mount_points(m_server), "/payload", f);
        m_sourceId = gst_rtsp_server_attach(m_server, nullptr);  // default GLib context, which Qt's event loop runs
        QVERIFY(m_sourceId);
        m_url = QStringLiteral("rtsp://127.0.0.1:%1/payload").arg(gst_rtsp_server_get_bound_port(m_server));
    }

    void cleanupTestCase()
    {
        if (m_sourceId) g_source_remove(m_sourceId);
        g_object_unref(m_server);
    }

    void urlValidation()
    {
        QVERIFY(VideoPlayer::validateUrl(QStringLiteral("rtsp://192.168.55.1:8554/payload")).isEmpty());
        QVERIFY(!VideoPlayer::validateUrl(QStringLiteral("http://192.168.55.1/x")).isEmpty());
        QVERIFY(!VideoPlayer::validateUrl(QStringLiteral("rtsp:///nohost")).isEmpty());
        QVERIFY(!VideoPlayer::validateUrl(QStringLiteral("rtsp://h ! fakesink")).isEmpty());  // no pipeline injection
        QCOMPARE(VideoPlayer::defaultUrl(QStringLiteral("192.168.55.1")), QStringLiteral("rtsp://192.168.55.1:8554/payload"));
    }

    void playsAndStops()
    {
        VideoPlayer p;
        QVERIFY(p.start(m_url, VideoPlayer::Transport::Tcp, 50));
        QCOMPARE(p.state(), VideoPlayer::State::Connecting);
        QTRY_VERIFY_WITH_TIMEOUT(p.frames() >= 15, 10000);
        QCOMPARE(p.state(), VideoPlayer::State::Playing);
        const QImage img = p.latestFrame();
        QCOMPARE(img.size(), QSize(320, 240));
        QVERIFY(img.pixel(10, 10) != img.pixel(310, 10));  // test pattern, not a blank image
        p.stop();
        QCOMPARE(p.state(), VideoPlayer::State::Stopped);
        const quint64 n = p.frames();
        QTest::qWait(300);
        QCOMPARE(p.frames(), n);  // no callbacks after stop()

        QVERIFY(p.start(m_url, VideoPlayer::Transport::Udp, 50));  // UDP works on loopback
        QTRY_VERIFY_WITH_TIMEOUT(p.frames() >= 5, 10000);
    }

    void reportsErrors()
    {
        VideoPlayer p;
        QVERIFY(!p.start(QStringLiteral("not a url"), VideoPlayer::Transport::Tcp, 0));
        QCOMPARE(p.state(), VideoPlayer::State::Error);
        QVERIFY(p.start(QStringLiteral("rtsp://127.0.0.1:1/none"), VideoPlayer::Transport::Tcp, 0));  // nothing listens
        QTRY_COMPARE_WITH_TIMEOUT(p.state(), VideoPlayer::State::Error, 10000);
        QVERIFY(!p.error().isEmpty());
    }

    void pageFollowsPayloadIpUntilEdited()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("GremsyTest"));
        QSettings().remove(QStringLiteral("video"));
        VideoPage page;
        auto *url = page.findChild<QLineEdit *>(QStringLiteral("rtspUrl"));
        page.setPayloadIp(QStringLiteral("10.0.0.7"));
        QCOMPARE(url->text(), QStringLiteral("rtsp://10.0.0.7:8554/payload"));
        QTest::keyClicks(url, QStringLiteral("x"));  // a user edit
        page.setPayloadIp(QStringLiteral("10.0.0.8"));
        QVERIFY(url->text().endsWith(QLatin1Char('x')));

        url->setText(m_url);
        page.findChild<QPushButton *>(QStringLiteral("videoStartStop"))->click();
        QTRY_COMPARE_WITH_TIMEOUT(page.player()->state(), VideoPlayer::State::Playing, 10000);
        QVERIFY(!url->isEnabled());
        page.findChild<QPushButton *>(QStringLiteral("videoStartStop"))->click();
        QCOMPARE(page.player()->state(), VideoPlayer::State::Stopped);
    }

    // The main window moves the settings panel into its drawer, which is destroyed first: closing the app while
    // the video plays must not reach the deleted panel.
    void closingWhilePlayingWithPanelGone()
    {
        auto *drawer = new QWidget;
        auto *page = new VideoPage;
        page->settingsPanel()->setParent(drawer);
        drawer->findChild<QLineEdit *>(QStringLiteral("rtspUrl"))->setText(m_url);
        page->findChild<QPushButton *>(QStringLiteral("videoStartStop"))->click();
        QTRY_COMPARE_WITH_TIMEOUT(page->player()->state(), VideoPlayer::State::Playing, 10000);
        delete drawer;
        delete page;  // crashed in VideoPage::onState before the fix
    }
};

QTEST_MAIN(TestVideo)
#include "test_video.moc"
