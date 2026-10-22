#include "main_window.h"

#include "inspector_widget.h"
#include "payload_link.h"
#include "stats_widget.h"

#include <QCloseEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

static const char *kDefaultIp = "192.168.55.1";  // PayloadSDK v3 README default
static const int kDefaultPort = 14566;

static QString badgeColor(LinkState s)
{
    switch (s) {
    case LinkState::Heartbeat: return QStringLiteral("#2e7d32");
    case LinkState::Mavlink:
    case LinkState::BytesOnly: return QStringLiteral("#b26a00");
    case LinkState::Stale:
    case LinkState::Failed: return QStringLiteral("#c01c28");
    case LinkState::SocketOpen:
    case LinkState::Connecting:
    case LinkState::Disconnecting: return QStringLiteral("#1565c0");
    case LinkState::Disconnected: return QStringLiteral("#6b6b6b");
    }
    return {};
}

static QString bytesText(quint64 b)
{
    if (b < 10 * 1024) return QStringLiteral("%1 B").arg(b);
    if (b < 10 * 1024 * 1024) return QStringLiteral("%1 KiB").arg(b / 1024);
    return QStringLiteral("%1 MiB").arg(b / (1024 * 1024));
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_link(new PayloadLink(this))
{
    setWindowTitle(QStringLiteral("Gremsy MAVLink Debug"));

    // --- Connection panel ---
    m_ip = new QLineEdit;
    m_ip->setObjectName(QStringLiteral("ipEdit"));
    m_ip->setPlaceholderText(QStringLiteral("payload IPv4, e.g. %1").arg(kDefaultIp));
    m_ip->setMaximumWidth(160);
    m_port = new QSpinBox;
    m_port->setObjectName(QStringLiteral("portSpin"));
    m_port->setRange(1, 65535);
    m_port->setToolTip(QStringLiteral("Payload UDP port. The SDK sends from an ephemeral local port (client mode)."));
    m_timeout = new QSpinBox;
    m_timeout->setRange(1, 60);
    m_timeout->setSuffix(QStringLiteral(" s"));
    m_timeout->setToolTip(QStringLiteral("Link is reported stale when nothing is received for this long."));
    m_connectBtn = new QPushButton(QStringLiteral("Connect"));
    m_connectBtn->setObjectName(QStringLiteral("connectButton"));
    m_connectBtn->setDefault(true);

    QSettings s;
    const QString savedIp = s.value(QStringLiteral("conn/ip"), kDefaultIp).toString();
    const int savedPort = s.value(QStringLiteral("conn/port"), kDefaultPort).toInt();
    const bool savedOk = validateEndpoint(savedIp, savedPort).isEmpty();  // never trust stored values
    m_ip->setText(savedOk ? savedIp : QString::fromLatin1(kDefaultIp));
    m_port->setValue(savedOk ? savedPort : kDefaultPort);
    m_timeout->setValue(qBound(1, s.value(QStringLiteral("conn/timeoutS"), 3).toInt(), 60));

    auto *row1 = new QHBoxLayout;
    row1->addWidget(new QLabel(QStringLiteral("Payload IP")));
    row1->addWidget(m_ip);
    row1->addWidget(new QLabel(QStringLiteral("UDP port")));
    row1->addWidget(m_port);
    row1->addWidget(new QLabel(QStringLiteral("Timeout")));
    row1->addWidget(m_timeout);
    row1->addWidget(m_connectBtn);
    row1->addStretch();

    m_stateBadge = new QLabel;
    m_stateBadge->setMinimumWidth(260);
    m_stateDetail = new QLabel;
    m_stateDetail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *row2 = new QHBoxLayout;
    row2->addWidget(m_stateBadge);
    row2->addWidget(m_stateDetail, 1);

    m_statsLabel = new QLabel;
    m_statsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_heartbeatLabel = new QLabel;
    m_heartbeatLabel->setToolTip(QStringLiteral("sysid/compid of every component that sent a HEARTBEAT, with its age"));

    auto *connBox = new QGroupBox(QStringLiteral("Connection"));
    auto *connLayout = new QVBoxLayout(connBox);
    connLayout->addLayout(row1);
    connLayout->addLayout(row2);
    connLayout->addWidget(m_statsLabel);
    connLayout->addWidget(m_heartbeatLabel);

    // --- Pages ---
    QString dictError;
    if (!m_dict.load(MavlinkDict::defaultDir(), QStringLiteral("ardupilotmega.xml"), &dictError))
        m_dict = MavlinkDict();  // the inspector still works; units and enum names are just missing
    m_link->setDictionary(m_dict.isLoaded() ? &m_dict : nullptr);
    m_inspector = new InspectorWidget(m_dict.isLoaded() ? &m_dict : nullptr);
    m_statsPage = new StatsWidget(m_dict.isLoaded() ? &m_dict : nullptr);

    auto *tabs = new QTabWidget;
    tabs->addTab(m_inspector, QStringLiteral("MAVLink Inspector"));
    tabs->addTab(m_statsPage, QStringLiteral("Statistics"));

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(connBox);
    layout->addWidget(tabs, 1);
    setCentralWidget(central);

    m_captureLabel = new QLabel;
    statusBar()->addWidget(m_captureLabel);
    statusBar()->addPermanentWidget(new QLabel(
        m_dict.isLoaded() ? QStringLiteral("Definitions: %1 (%2 files)").arg(MavlinkDict::defaultDir()).arg(m_dict.files().size())
                          : QStringLiteral("<span style='color:#c01c28'>MAVLink XML not loaded: %1. Units and enum names unavailable.</span>")
                                .arg(dictError.toHtmlEscaped())));

    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_ip, &QLineEdit::returnPressed, this, &MainWindow::onConnectClicked);
    connect(m_timeout, &QSpinBox::valueChanged, this, [this](int v) { m_link->setTimeoutMs(v * 1000); });
    connect(m_link, &PayloadLink::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_link, &PayloadLink::framesCaptured, this, &MainWindow::onFrames);
    auto *tick = new QTimer(this);
    connect(tick, &QTimer::timeout, this, [this] { refreshStats(true); });
    tick->start(1000);

    m_link->setTimeoutMs(m_timeout->value() * 1000);
    resize(1200, 760);
    onStateChanged();
    refreshStats();
}

MainWindow::~MainWindow()
{
    m_link->setDictionary(nullptr);  // m_dict is destroyed before the child objects
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    saveSettings();
    e->accept();
}

void MainWindow::saveSettings() const
{
    QSettings s;
    if (validateEndpoint(m_ip->text(), m_port->value()).isEmpty()) {
        s.setValue(QStringLiteral("conn/ip"), m_ip->text().trimmed());
        s.setValue(QStringLiteral("conn/port"), m_port->value());
    }
    s.setValue(QStringLiteral("conn/timeoutS"), m_timeout->value());
}

void MainWindow::onConnectClicked()
{
    if (isOpen(m_link->state())) {
        m_link->disconnectLink();
        return;
    }
    m_stats = {};
    m_prevFrames[0] = m_prevFrames[1] = m_rate[0] = m_rate[1] = 0;
    m_link->connectTo(m_ip->text(), m_port->value());
    if (m_link->state() == LinkState::Connecting) saveSettings();
}

void MainWindow::onStateChanged()
{
    const LinkState st = m_link->state();
    const bool idle = st == LinkState::Disconnected || st == LinkState::Failed;
    m_ip->setEnabled(idle);
    m_port->setEnabled(idle);
    m_connectBtn->setEnabled(idle || isOpen(st));
    m_connectBtn->setText(isOpen(st) ? QStringLiteral("Disconnect") : QStringLiteral("Connect"));
    m_stateBadge->setText(QStringLiteral("<span style='color:%1;font-size:14pt'>&#9679;</span> <b>%2</b>")
                              .arg(badgeColor(st), linkStateText(st).toHtmlEscaped()));
    if (st == LinkState::Failed)
        m_stateDetail->setText(m_link->lastError());
    else if (st == LinkState::Disconnected)
        m_stateDetail->clear();
    else
        m_stateDetail->setText(QStringLiteral("Target %1, local port ephemeral (SDK UDP client mode)").arg(m_link->target()));
    refreshStats();
}

void MainWindow::onFrames(const std::vector<CapturedFrame> &frames)
{
    for (const CapturedFrame &c : frames) m_stats.add(c);
    m_inspector->addFrames(frames);
    m_statsPage->addFrames(frames);
}

void MainWindow::refreshStats(bool tick)
{
    const qint64 nowMs = PayloadLink::monoNs() / 1000000;
    QString open = QStringLiteral("-");
    if (m_link->openSinceMs() >= 0) {
        const qint64 s = (nowMs - m_link->openSinceMs()) / 1000;
        open = QStringLiteral("%1:%2:%3").arg(s / 3600, 2, 10, QLatin1Char('0'))
                   .arg(s / 60 % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    }
    if (tick) {
        m_rate[0] = m_stats.frames[0] - m_prevFrames[0];
        m_rate[1] = m_stats.frames[1] - m_prevFrames[1];
        m_prevFrames[0] = m_stats.frames[0];
        m_prevFrames[1] = m_stats.frames[1];
    }
    const quint64 rxRate = m_rate[0], txRate = m_rate[1];
    const QString lastRx = m_stats.lastNs[0] < 0 ? QStringLiteral("never")
        : QStringLiteral("%1 s ago").arg((nowMs - m_stats.lastNs[0] / 1000000) / 1000.0, 0, 'f', 1);

    m_statsLabel->setText(
        QStringLiteral("Open %1  |  RX %2 frames (%3/s), %4  |  TX %5 frames (%6/s), %7  |  last RX %8  |  "
                       "bad CRC %9, unknown ID %10, truncated %11, junk %12, send errors %13")
            .arg(open).arg(m_stats.frames[0]).arg(rxRate).arg(bytesText(m_stats.bytes[0]))
            .arg(m_stats.frames[1]).arg(txRate).arg(bytesText(m_stats.bytes[1])).arg(lastRx)
            .arg(m_stats.badCrc).arg(m_stats.unknownId).arg(m_stats.truncated).arg(m_stats.garbage)
            .arg(m_stats.sendErrors));

    QStringList hb;
    const auto &src = m_link->heartbeatSources();
    for (auto it = src.cbegin(); it != src.cend(); ++it)
        hb << QStringLiteral("%1/%2 (%3 s)").arg(it.key() >> 8).arg(it.key() & 0xFF)
                  .arg((nowMs - it.value()) / 1000.0, 0, 'f', 1);
    m_heartbeatLabel->setText(QStringLiteral("Heartbeats from: ") +
                              (hb.isEmpty() ? QStringLiteral("none") : hb.join(QStringLiteral(", "))));

    m_captureLabel->setText(m_link->droppedDatagrams()
        ? QStringLiteral("<span style='color:#c01c28'>Capture queue overflow: %1 datagrams not shown</span>").arg(m_link->droppedDatagrams())
        : QStringLiteral("Capture queue OK"));
}
