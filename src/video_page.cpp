#include "video_page.h"

#include "video_player.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

void VideoView::setImage(const QImage &img)
{
    m_image = img;
    update();
}

void VideoView::clear(const QString &message)
{
    m_image = QImage();
    m_message = message;
    update();
}

void VideoView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (m_image.isNull()) {
        p.setPen(QColor(0xbb, 0xbb, 0xbb));
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_message);
        return;
    }
    const QSize s = m_image.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect r(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(r, m_image);
}

VideoPage::VideoPage(QWidget *parent) : QWidget(parent), m_player(new VideoPlayer(this)), m_view(new VideoView)
{
    m_url = new QLineEdit;
    m_url->setObjectName(QStringLiteral("rtspUrl"));
    m_url->setToolTip(QStringLiteral("Built from the payload IP (Gremsy format rtsp://IP:8554/payload) until you edit it"));
    m_transport = new QComboBox;
    m_transport->addItem(QStringLiteral("TCP"), int(VideoPlayer::Transport::Tcp));
    m_transport->addItem(QStringLiteral("UDP"), int(VideoPlayer::Transport::Udp));
    m_transport->addItem(QStringLiteral("UDP, then TCP"), int(VideoPlayer::Transport::Auto));
    m_transport->setToolTip(QStringLiteral("RTP transport. Under WSL2 NAT, UDP from the VIO does not get through; TCP does."));
    m_latency = new QSpinBox;
    m_latency->setRange(0, 2000);
    m_latency->setSuffix(QStringLiteral(" ms"));
    m_latency->setToolTip(QStringLiteral("rtspsrc jitter buffer"));
    m_startStop = new QPushButton(QStringLiteral("Start video"));
    m_startStop->setObjectName(QStringLiteral("videoStartStop"));
    m_status = new QLabel;
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setWordWrap(true);

    QSettings s;
    m_transport->setCurrentIndex(qBound(0, s.value(QStringLiteral("video/transport"), 0).toInt(), 2));
    m_latency->setValue(qBound(0, s.value(QStringLiteral("video/latencyMs"), 100).toInt(), 2000));
    const QString savedUrl = s.value(QStringLiteral("video/url")).toString();
    if (!savedUrl.isEmpty() && VideoPlayer::validateUrl(savedUrl).isEmpty()) {
        m_url->setText(savedUrl);
        m_urlEdited = s.value(QStringLiteral("video/urlEdited"), false).toBool();
    }

    auto *bar = new QHBoxLayout;
    bar->addWidget(new QLabel(QStringLiteral("RTSP URL")));
    bar->addWidget(m_url, 1);
    bar->addWidget(m_transport);
    bar->addWidget(new QLabel(QStringLiteral("Latency")));
    bar->addWidget(m_latency);
    bar->addWidget(m_startStop);
    auto *l = new QVBoxLayout(this);
    l->addLayout(bar);
    l->addWidget(m_status);
    l->addWidget(m_view, 1);

    connect(m_url, &QLineEdit::textEdited, this, [this] { m_urlEdited = true; });
    connect(m_url, &QLineEdit::returnPressed, this, &VideoPage::onStartStop);
    connect(m_startStop, &QPushButton::clicked, this, &VideoPage::onStartStop);
    connect(m_player, &VideoPlayer::stateChanged, this, &VideoPage::onState);
    connect(m_player, &VideoPlayer::frameReady, this, [this] { m_view->setImage(m_player->latestFrame()); });
    auto *t = new QTimer(this);
    connect(t, &QTimer::timeout, this, &VideoPage::refreshStatus);
    t->start(1000);
    onState();
}

VideoPage::~VideoPage()
{
    QSettings s;
    s.setValue(QStringLiteral("video/url"), m_url->text().trimmed());
    s.setValue(QStringLiteral("video/urlEdited"), m_urlEdited);
    s.setValue(QStringLiteral("video/transport"), m_transport->currentIndex());
    s.setValue(QStringLiteral("video/latencyMs"), m_latency->value());
}

void VideoPage::setPayloadIp(const QString &ip)
{
    if (!m_urlEdited) m_url->setText(VideoPlayer::defaultUrl(ip.trimmed()));
}

void VideoPage::onStartStop()
{
    const VideoPlayer::State st = m_player->state();
    if (st == VideoPlayer::State::Connecting || st == VideoPlayer::State::Playing) {
        m_player->stop();
        return;
    }
    m_player->start(m_url->text(), VideoPlayer::Transport(m_transport->currentData().toInt()), m_latency->value());
}

void VideoPage::onState()
{
    const VideoPlayer::State st = m_player->state();
    const bool running = st == VideoPlayer::State::Connecting || st == VideoPlayer::State::Playing;
    m_startStop->setText(running ? QStringLiteral("Stop video") : QStringLiteral("Start video"));
    m_url->setEnabled(!running);
    m_transport->setEnabled(!running);
    m_latency->setEnabled(!running);
    if (st == VideoPlayer::State::Connecting) m_view->clear(QStringLiteral("Connecting to %1 …").arg(m_url->text()));
    if (st == VideoPlayer::State::Stopped) m_view->clear(QStringLiteral("Video stopped"));
    if (st == VideoPlayer::State::Error) m_view->clear(QStringLiteral("No video: %1").arg(m_player->error()));
    refreshStatus();
}

void VideoPage::refreshStatus()
{
    switch (m_player->state()) {
    case VideoPlayer::State::Stopped:
        m_status->setText(QStringLiteral("Stopped. Video is independent of the MAVLink connection."));
        break;
    case VideoPlayer::State::Connecting:
        m_status->setText(QStringLiteral("Connecting (RTSP setup, waiting for the first decoded frame)…"));
        break;
    case VideoPlayer::State::Playing: {
        const QImage img = m_player->latestFrame();
        m_status->setText(QStringLiteral("Playing %1×%2, %3 fps decoded, %4 frames")
                              .arg(img.width()).arg(img.height()).arg(m_player->fps(), 0, 'f', 1).arg(m_player->frames()));
        break;
    }
    case VideoPlayer::State::Error:
        m_status->setText(QStringLiteral("<span style='color:#c01c28'>%1</span>").arg(m_player->error().toHtmlEscaped()));
        break;
    }
}
