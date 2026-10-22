#include "video_page.h"

#include "theme.h"
#include "ui_util.h"
#include "video_player.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
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

void VideoView::setHud(std::vector<HudText> items, const QRectF &box, const QColor &boxColor)
{
    m_hud = std::move(items);
    m_box = box;
    m_boxColor = boxColor;
    update();
}

void VideoView::setPickMode(bool on)
{
    m_pick = on;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
}

QRect VideoView::pictureRect() const
{
    if (m_image.isNull()) return rect();
    const QSize s = m_image.size().scaled(size(), Qt::KeepAspectRatio);
    return QRect(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
}

void VideoView::mousePressEvent(QMouseEvent *e)
{
    const QRect r = pictureRect();
    if (!m_pick || m_image.isNull() || e->button() != Qt::LeftButton || !r.contains(e->position().toPoint())) {
        QWidget::mousePressEvent(e);
        return;
    }
    emit picked(QPointF((e->position().x() - r.x()) / r.width(), (e->position().y() - r.y()) / r.height()));
}

void VideoView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x05, 0x08, 0x0e));
    const QRect r = pictureRect();
    if (m_image.isNull()) {
        p.setPen(QColor(Theme::Dim));
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_message);
    } else {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(r, m_image);
        // Centre reticle
        QColor accent(Theme::Accent);
        accent.setAlpha(200);
        p.setPen(QPen(accent, 1.5));
        const QPoint c = r.center();
        for (const QPoint d : {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)})
            p.drawLine(c + d * 6, c + d * 18);
    }
    if (!m_box.isEmpty()) {
        p.setPen(QPen(m_boxColor, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(r.x() + m_box.x() * r.width(), r.y() + m_box.y() * r.height(),
                          m_box.width() * r.width(), m_box.height() * r.height()));
    }

    // Overlay lines, stacked from each corner of the picture on a dark backing for legibility
    p.setFont(monoFont(9));
    const QFontMetrics fm(p.font());
    const int pad = 6, lineH = fm.height() + 6;
    for (const Qt::Alignment corner : {Qt::AlignTop | Qt::AlignLeft, Qt::AlignTop | Qt::AlignRight,
                                       Qt::AlignBottom | Qt::AlignLeft, Qt::AlignBottom | Qt::AlignRight}) {
        int line = 0;
        for (const HudText &t : m_hud) {
            if (t.corner != corner) continue;
            const int w = fm.horizontalAdvance(t.text) + 12;
            const int x = corner & Qt::AlignLeft ? r.left() + pad : r.right() - pad - w;
            const int y = corner & Qt::AlignTop ? r.top() + pad + line * (lineH + 2) : r.bottom() - pad - (line + 1) * (lineH + 2);
            const QRect box(x, y, w, lineH);
            p.fillRect(box, QColor(5, 9, 16, 175));
            p.setPen(t.color);
            p.drawText(box, Qt::AlignCenter, t.text);
            ++line;
        }
    }
}

VideoPage::VideoPage(QWidget *parent)
    : QWidget(parent), m_player(new VideoPlayer(this)), m_view(new VideoView), m_settings(new QWidget(this))
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
    m_startStop = button(QStringLiteral("Start video"), {}, "primary");
    m_startStop->setObjectName(QStringLiteral("videoStartStop"));
    m_status = new QLabel;
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);  // long errors must not widen the panel

    QSettings s;
    m_transport->setCurrentIndex(qBound(0, s.value(QStringLiteral("video/transport"), 0).toInt(), 2));
    m_latency->setValue(qBound(0, s.value(QStringLiteral("video/latencyMs"), 100).toInt(), 2000));
    const QString savedUrl = s.value(QStringLiteral("video/url")).toString();
    if (!savedUrl.isEmpty() && VideoPlayer::validateUrl(savedUrl).isEmpty()) {
        m_url->setText(savedUrl);
        m_urlEdited = s.value(QStringLiteral("video/urlEdited"), false).toBool();
    }

    auto *bar = new QHBoxLayout(m_settings);
    bar->setContentsMargins(0, 0, 0, 0);
    bar->addWidget(new QLabel(QStringLiteral("RTSP URL")));
    bar->addWidget(m_url, 1);
    bar->addWidget(m_transport);
    bar->addWidget(new QLabel(QStringLiteral("Latency")));
    bar->addWidget(m_latency);

    auto *title = new QLabel(QStringLiteral("LIVE VIDEO"));
    title->setObjectName(QStringLiteral("panelTitle"));
    title->setStyleSheet(QStringLiteral("border: none"));  // the whole title row carries the rule
    auto *head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 8, 0);
    head->addWidget(title);
    head->addWidget(m_status, 1);
    head->addWidget(m_startStop);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    l->addLayout(head);
    l->addWidget(m_settings);
    l->addWidget(m_view, 1);

    // Saved on every change: the settings panel may be moved into another widget and destroyed before this page
    auto save = [this] {
        QSettings s;
        s.setValue(QStringLiteral("video/url"), m_url->text().trimmed());
        s.setValue(QStringLiteral("video/urlEdited"), m_urlEdited);
        s.setValue(QStringLiteral("video/transport"), m_transport->currentIndex());
        s.setValue(QStringLiteral("video/latencyMs"), m_latency->value());
    };
    connect(m_url, &QLineEdit::textChanged, this, save);
    connect(m_url, &QLineEdit::textEdited, this, [this, save] {  // emitted after textChanged
        m_urlEdited = true;
        save();
    });
    connect(m_transport, &QComboBox::currentIndexChanged, this, save);
    connect(m_latency, &QSpinBox::valueChanged, this, save);
    connect(m_url, &QLineEdit::returnPressed, this, &VideoPage::onStartStop);
    connect(m_startStop, &QPushButton::clicked, this, &VideoPage::onStartStop);
    connect(m_player, &VideoPlayer::stateChanged, this, &VideoPage::onState);
    connect(m_player, &VideoPlayer::frameReady, this, [this] { m_view->setImage(m_player->latestFrame()); });
    auto *t = new QTimer(this);
    connect(t, &QTimer::timeout, this, &VideoPage::refreshStatus);
    t->start(1000);
    onState();
}

VideoPage::~VideoPage() = default;

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
        m_status->setText(colored(QStringLiteral("Stopped (independent of the MAVLink link)"), Theme::Dim));
        break;
    case VideoPlayer::State::Connecting:
        m_status->setText(colored(QStringLiteral("Connecting: RTSP setup, waiting for the first decoded frame…"), Theme::Warn));
        break;
    case VideoPlayer::State::Playing: {
        const QImage img = m_player->latestFrame();
        m_status->setText(colored(QStringLiteral("%1×%2 · %3 fps decoded · %4 frames")
                                      .arg(img.width()).arg(img.height()).arg(m_player->fps(), 0, 'f', 1).arg(m_player->frames()),
                                  Theme::Dim));
        break;
    }
    case VideoPlayer::State::Error:
        m_status->setText(colored(m_player->error().toHtmlEscaped(), Theme::Bad));
        break;
    }
    m_status->setToolTip(m_status->text());
}
