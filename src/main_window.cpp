#include "main_window.h"

#include "inspector_widget.h"
#include "payload_pages.h"
#include "payload_profile.h"
#include "payload_link.h"
#include "stats_widget.h"
#include "ui_util.h"
#include "video_page.h"
#include "video_player.h"

#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

static const char *kDefaultIp = "192.168.55.1";  // PayloadSDK v3 README default
static const int kDefaultPort = 14566;
// Components a VIO reports from (README "Hardware checks"): payload core, camera, gimbal
static const QList<quint8> kPayloadComponents{TelemetryStore::kPayloadCore, MAV_COMP_ID_CAMERA, MAV_COMP_ID_GIMBAL};

static const char *lampColor(Lamp l)
{
    switch (l) {
    case Lamp::Ok: return Theme::Ok;
    case Lamp::Warn: return Theme::Warn;
    case Lamp::Bad: return Theme::Bad;
    case Lamp::Off: break;
    }
    return Theme::Dim;
}

static QString lampText(const QString &name, Lamp l, const QString &detail)
{
    return colored(QStringLiteral("&#9679;"), lampColor(l)) + QStringLiteral(" <b>%1</b> ").arg(name) +
           colored(detail.toHtmlEscaped(), Theme::Dim);
}

static QString linkShort(LinkState s)
{
    switch (s) {
    case LinkState::Disconnected: return QStringLiteral("offline");
    case LinkState::Connecting: return QStringLiteral("connecting");
    case LinkState::Disconnecting: return QStringLiteral("closing");
    case LinkState::Failed: return QStringLiteral("failed");
    case LinkState::SocketOpen: return QStringLiteral("no reply");
    case LinkState::BytesOnly: return QStringLiteral("no MAVLink");
    case LinkState::Mavlink: return QStringLiteral("no heartbeat");
    case LinkState::Heartbeat: return QStringLiteral("heartbeat");
    case LinkState::Stale: return QStringLiteral("stale");
    }
    return {};
}

static QString bytesText(quint64 b)
{
    if (b < 10 * 1024) return QStringLiteral("%1 B").arg(b);
    if (b < 10 * 1024 * 1024) return QStringLiteral("%1 KiB").arg(b / 1024);
    return QStringLiteral("%1 MiB").arg(b / (1024 * 1024));
}

// A bordered panel with an optional title row.
static QFrame *panel(const QString &title, QWidget *content)
{
    auto *f = new QFrame;
    f->setObjectName(QStringLiteral("panel"));
    auto *l = new QVBoxLayout(f);
    l->setContentsMargins(1, 1, 1, 1);
    l->setSpacing(0);
    if (!title.isEmpty()) {
        auto *t = new QLabel(title);
        t->setObjectName(QStringLiteral("panelTitle"));
        l->addWidget(t);
    }
    l->addWidget(content, 1);
    return f;
}

// Vertical scrolling for a side column. It never scrolls sideways, so it is at least as wide as its content
// (asked on every layout pass: the stylesheet's padding is only known once the widgets are polished).
class Column : public QScrollArea
{
public:
    QSize minimumSizeHint() const override
    {
        return {widget()->minimumSizeHint().width() + verticalScrollBar()->sizeHint().width() + 2, 120};
    }
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == widget() && e->type() == QEvent::LayoutRequest) updateGeometry();  // new telemetry text can widen the content
        return QScrollArea::eventFilter(o, e);
    }
};

static QScrollArea *column(QWidget *w)
{
    auto *a = new Column;
    a->setWidget(w);
    a->setWidgetResizable(true);
    a->setFrameShape(QFrame::NoFrame);
    a->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    a->viewport()->setAutoFillBackground(false);
    w->setAutoFillBackground(false);  // setWidget() turned it on; the panel colour shows through
    return a;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_link(new PayloadLink(this))
{
    setWindowTitle(QStringLiteral("Payload Control Station"));

    // --- Connection fields (settings drawer) ---
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
    m_connectBtn = button(QStringLiteral("Connect"), {}, "primary");
    m_connectBtn->setObjectName(QStringLiteral("connectButton"));
    m_connectBtn->setDefault(true);
    m_connectBtn->setMinimumWidth(110);

    QSettings s;
    const QString savedIp = s.value(QStringLiteral("conn/ip"), kDefaultIp).toString();
    const int savedPort = s.value(QStringLiteral("conn/port"), kDefaultPort).toInt();
    const bool savedOk = validateEndpoint(savedIp, savedPort).isEmpty();  // never trust stored values
    m_ip->setText(savedOk ? savedIp : QString::fromLatin1(kDefaultIp));
    m_port->setValue(savedOk ? savedPort : kDefaultPort);
    m_timeout->setValue(qBound(1, s.value(QStringLiteral("conn/timeoutS"), 3).toInt(), 60));

    m_profileBox = new QComboBox;
    m_profileBox->setObjectName(QStringLiteral("profileBox"));
    m_profileBox->setMinimumContentsLength(14);
    m_profileBox->setToolTip(QStringLiteral("Saved connection profiles; choosing one fills IP, port and timeout"));
    m_saveProfile = new QPushButton(QStringLiteral("Save…"));
    m_saveProfile->setToolTip(QStringLiteral("Save the current IP, port and timeout as a named profile"));
    m_deleteProfile = new QPushButton(QStringLiteral("Delete"));
    {
        QSettings ps;
        m_profiles = loadProfiles(ps);
    }
    reloadProfiles(s.value(QStringLiteral("conn/profile")).toString());

    auto *row1 = new QHBoxLayout;
    auto *connTitle = new QLabel(QStringLiteral("CONNECTION"));
    connTitle->setObjectName(QStringLiteral("readoutCaption"));
    connTitle->setMinimumWidth(90);
    row1->addWidget(connTitle);
    row1->addWidget(new QLabel(QStringLiteral("Profile")));
    row1->addWidget(m_profileBox);
    row1->addWidget(m_saveProfile);
    row1->addWidget(m_deleteProfile);
    row1->addSpacing(12);
    row1->addWidget(new QLabel(QStringLiteral("Payload IP")));
    row1->addWidget(m_ip);
    row1->addWidget(new QLabel(QStringLiteral("UDP port")));
    row1->addWidget(m_port);
    row1->addWidget(new QLabel(QStringLiteral("Timeout")));
    row1->addWidget(m_timeout);
    row1->addStretch();

    m_stateBadge = new QLabel;
    m_stateDetail = new QLabel;
    m_stateDetail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *row2 = new QHBoxLayout;
    row2->addSpacing(90 + row1->spacing());
    row2->addWidget(m_stateBadge);
    row2->addWidget(m_stateDetail, 1);

    m_statsLabel = new QLabel;
    m_statsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_heartbeatLabel = new QLabel;
    m_heartbeatLabel->setToolTip(QStringLiteral("sysid/compid of every component that sent a HEARTBEAT, with its age"));
    for (QLabel *l : {m_statsLabel, m_heartbeatLabel}) l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    // --- Pages ---
    QString dictError;
    if (!m_dict.load(MavlinkDict::defaultDir(), QStringLiteral("ardupilotmega.xml"), &dictError))
        m_dict = MavlinkDict();  // the inspector still works; units and enum names are just missing
    m_link->setDictionary(m_dict.isLoaded() ? &m_dict : nullptr);
    m_inspector = new InspectorWidget(m_dict.isLoaded() ? &m_dict : nullptr);
    m_statsPage = new StatsWidget(m_dict.isLoaded() ? &m_dict : nullptr);
    connect(m_inspector, &InspectorWidget::networkFrames, m_statsPage, &StatsWidget::addFrames);

    const MavlinkDict *dict = m_dict.isLoaded() ? &m_dict : nullptr;
    auto *commands = new CommandsPage(m_link, dict);
    auto *gimbal = new GimbalPage(m_link, dict, vioProfile());
    m_camera = new CameraPage(m_link, dict, vioProfile());
    m_video = new VideoPage;
    connect(m_ip, &QLineEdit::textChanged, m_video, &VideoPage::setPayloadIp);
    m_video->setPayloadIp(m_ip->text());
    connect(m_camera, &CameraPage::trackArmedChanged, m_video->view(), &VideoView::setPickMode);
    connect(m_video->view(), &VideoView::picked, m_camera, &CameraPage::trackAt);

    auto *videoRow = new QHBoxLayout;
    auto *videoTitle = new QLabel(QStringLiteral("VIDEO"));
    videoTitle->setObjectName(QStringLiteral("readoutCaption"));
    videoTitle->setMinimumWidth(90);
    videoRow->addWidget(videoTitle);
    videoRow->addWidget(m_video->settingsPanel(), 1);  // moved out of the video panel to keep the picture large

    m_drawer = new QFrame;
    m_drawer->setObjectName(QStringLiteral("drawer"));
    auto *dl = new QVBoxLayout(m_drawer);
    dl->setContentsMargins(12, 8, 12, 8);
    dl->addLayout(row1);
    dl->addLayout(row2);
    dl->addWidget(m_statsLabel);
    dl->addWidget(m_heartbeatLabel);
    dl->addLayout(videoRow);

    // --- Header ---
    auto *title = new QLabel(QStringLiteral("PAYLOAD CONTROL STATION"));
    title->setObjectName(QStringLiteral("appTitle"));
    auto *subtitle = new QLabel(QStringLiteral("Gremsy %1 · PayloadSDK · MAVLink").arg(QLatin1String(vioProfile().name)));
    subtitle->setObjectName(QStringLiteral("appSubtitle"));
    auto *titles = new QVBoxLayout;
    titles->setSpacing(0);
    titles->addWidget(title);
    titles->addWidget(subtitle);

    m_nav = new QButtonGroup(this);
    auto *nav = new QHBoxLayout;
    nav->setSpacing(0);
    for (const auto &[id, text, name] : {std::tuple{0, "OPERATE", "navOperate"}, std::tuple{1, "ANALYZE", "navAnalyze"}}) {
        QPushButton *b = button(QLatin1String(text), id ? QStringLiteral("Command log, payload telemetry and traffic statistics")
                                                        : QStringLiteral("Video with gimbal and camera control"), "segment");
        b->setObjectName(QLatin1String(name));
        b->setCheckable(true);
        b->setMinimumWidth(96);
        m_nav->addButton(b, id);
        nav->addWidget(b);
    }

    m_payloadLamp = new QLabel;
    m_linkLamp = new QLabel;
    m_videoLamp = new QLabel;
    m_rates = new QLabel;
    m_rates->setFont(monoFont(9));
    m_warning = new QLabel;
    m_warning->setObjectName(QStringLiteral("warning"));
    m_settingsBtn = new QPushButton(QStringLiteral("SETTINGS"));
    m_settingsBtn->setObjectName(QStringLiteral("settingsButton"));
    m_settingsBtn->setCheckable(true);
    m_settingsBtn->setToolTip(QStringLiteral("Connection, profiles and video stream settings"));

    auto *header = new QFrame;
    header->setObjectName(QStringLiteral("header"));
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(14, 8, 14, 8);
    hl->setSpacing(14);
    hl->addLayout(titles);
    hl->addSpacing(10);
    hl->addLayout(nav);
    hl->addStretch(1);
    hl->addWidget(m_warning);
    hl->addWidget(m_payloadLamp);
    hl->addWidget(m_linkLamp);
    hl->addWidget(m_videoLamp);
    hl->addWidget(m_rates);
    hl->addWidget(m_settingsBtn);
    hl->addWidget(m_connectBtn);

    // --- Operate: gimbal | video | camera ---
    m_hsplit = new QSplitter(Qt::Horizontal);
    m_hsplit->setChildrenCollapsible(false);
    m_hsplit->addWidget(panel(QStringLiteral("GIMBAL CONTROL"), column(gimbal)));
    m_hsplit->addWidget(panel({}, m_video));
    m_hsplit->addWidget(panel(QStringLiteral("CAMERA / TELEMETRY"), column(m_camera)));
    m_hsplit->setStretchFactor(0, 0);
    m_hsplit->setStretchFactor(1, 1);
    m_hsplit->setStretchFactor(2, 0);
    m_hsplit->setSizes({320, 800, 340});

    // --- Analyze ---
    auto *tabs = new QTabWidget;
    tabs->setObjectName(QStringLiteral("analyzeTabs"));
    tabs->setDocumentMode(true);
    tabs->addTab(commands, QStringLiteral("Commands"));
    tabs->addTab(new TelemetryPage(m_link, dict), QStringLiteral("Telemetry"));
    tabs->addTab(m_statsPage, QStringLiteral("Statistics"));

    m_views = new QStackedWidget;
    m_views->addWidget(m_hsplit);
    m_views->addWidget(tabs);
    connect(m_nav, &QButtonGroup::idClicked, m_views, &QStackedWidget::setCurrentIndex);

    m_monitor = panel({}, m_inspector);
    m_vsplit = new QSplitter(Qt::Vertical);
    m_vsplit->setChildrenCollapsible(false);
    m_vsplit->addWidget(m_views);
    m_vsplit->addWidget(m_monitor);
    m_vsplit->setStretchFactor(0, 1);
    m_vsplit->setStretchFactor(1, 0);
    m_vsplit->setSizes({620, 240});
    connect(m_inspector, &InspectorWidget::collapsedChanged, this, [this](bool collapsed) {
        if (collapsed) {
            m_expandedSizes = m_vsplit->sizes();
            m_monitor->setMaximumHeight(m_inspector->maximumHeight() + 2);  // + the panel border
            m_vsplit->setSizes({m_vsplit->height(), 0});  // the views take the freed height
        } else {
            m_monitor->setMaximumHeight(QWIDGETSIZE_MAX);
            if (!m_expandedSizes.isEmpty()) m_vsplit->setSizes(m_expandedSizes);
        }
    });

    connect(commands, &CommandsPage::showFrame, this, [this](quint64 index) {
        if (!m_inspector->selectFrame(index))
            statusBar()->showMessage(QStringLiteral("Frame #%1 is not in the table (evicted, or hidden by the filter)").arg(index), 5000);
    });

    auto *body = new QWidget;
    auto *bl = new QVBoxLayout(body);
    bl->setContentsMargins(8, 8, 8, 8);
    bl->addWidget(m_vsplit);

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(m_drawer);
    layout->addWidget(body, 1);
    setCentralWidget(central);

    m_captureLabel = new QLabel;
    statusBar()->addWidget(m_captureLabel);
    statusBar()->addPermanentWidget(new QLabel(
        m_dict.isLoaded() ? QStringLiteral("Definitions: %1 (%2 files)").arg(MavlinkDict::defaultDir()).arg(m_dict.files().size())
                          : colored(QStringLiteral("MAVLink XML not loaded: %1. Units and enum names unavailable.")
                                        .arg(dictError.toHtmlEscaped()), Theme::Bad)));

    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_ip, &QLineEdit::returnPressed, this, &MainWindow::onConnectClicked);
    connect(m_settingsBtn, &QPushButton::toggled, this, &MainWindow::setDrawerOpen);
    connect(m_profileBox, &QComboBox::activated, this, [this](int i) {
        if (i <= 0 || i > m_profiles.size()) return;
        const ConnectionProfile &p = m_profiles[i - 1];
        m_ip->setText(p.ip);
        m_port->setValue(p.port);
        m_timeout->setValue(p.timeoutS);
    });
    connect(m_saveProfile, &QPushButton::clicked, this, &MainWindow::saveProfile);
    connect(m_deleteProfile, &QPushButton::clicked, this, &MainWindow::deleteProfile);
    connect(m_timeout, &QSpinBox::valueChanged, this, [this](int v) { m_link->setTimeoutMs(v * 1000); });
    connect(m_link, &PayloadLink::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_link, &PayloadLink::framesCaptured, this, &MainWindow::onFrames);
    connect(m_video->player(), &VideoPlayer::stateChanged, this, &MainWindow::refreshOperator);
    auto *tick = new QTimer(this);
    connect(tick, &QTimer::timeout, this, [this] { refreshStats(true); });
    tick->start(1000);
    auto *hud = new QTimer(this);
    connect(hud, &QTimer::timeout, this, &MainWindow::refreshOperator);
    hud->start(250);

    // Restore the layout (each restore ignores invalid data)
    resize(1440, 900);
    restoreGeometry(s.value(QStringLiteral("ui/geometry")).toByteArray());
    m_hsplit->restoreState(s.value(QStringLiteral("ui/hsplit")).toByteArray());
    m_vsplit->restoreState(s.value(QStringLiteral("ui/vsplit")).toByteArray());
    const int view = qBound(0, s.value(QStringLiteral("ui/view")).toInt(), 1);
    m_nav->button(view)->setChecked(true);
    m_views->setCurrentIndex(view);
    m_inspector->setCollapsed(s.value(QStringLiteral("ui/monitorCollapsed"), false).toBool());
    m_settingsBtn->setChecked(true);  // start with the connection fields visible

    m_link->setTimeoutMs(m_timeout->value() * 1000);
    onStateChanged();
    refreshStats();
    refreshOperator();
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
    s.setValue(QStringLiteral("conn/profile"), m_profileBox->currentIndex() > 0 ? m_profileBox->currentText() : QString());
    s.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    s.setValue(QStringLiteral("ui/hsplit"), m_hsplit->saveState());
    s.setValue(QStringLiteral("ui/view"), m_views->currentIndex());
    s.setValue(QStringLiteral("ui/monitorCollapsed"), m_inspector->isCollapsed());
    if (!m_inspector->isCollapsed()) s.setValue(QStringLiteral("ui/vsplit"), m_vsplit->saveState());  // keeps the expanded height
}

void MainWindow::setDrawerOpen(bool open)
{
    m_drawer->setVisible(open);
    if (m_settingsBtn->isChecked() != open) m_settingsBtn->setChecked(open);
}

void MainWindow::reloadProfiles(const QString &select)
{
    m_profileBox->clear();
    m_profileBox->addItem(QStringLiteral("(none)"));
    for (const ConnectionProfile &p : m_profiles) m_profileBox->addItem(p.name);
    const int i = select.isEmpty() ? 0 : m_profileBox->findText(select, Qt::MatchFixedString);
    m_profileBox->setCurrentIndex(i > 0 ? i : 0);
}

void MainWindow::saveProfile()
{
    bool ok = false;
    const QString current = m_profileBox->currentIndex() > 0 ? m_profileBox->currentText() : QString();
    const QString name = QInputDialog::getText(this, QStringLiteral("Save profile"), QStringLiteral("Profile name"),
                                               QLineEdit::Normal, current, &ok);
    if (!ok) return;
    if (const QString err = upsertProfile(m_profiles, {name, m_ip->text(), m_port->value(), m_timeout->value()}); !err.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Profile not saved"), err);
        return;
    }
    QSettings s;
    saveProfiles(s, m_profiles);
    reloadProfiles(name.trimmed());
}

void MainWindow::deleteProfile()
{
    const int i = m_profileBox->currentIndex();
    if (i <= 0) return;
    if (QMessageBox::question(this, QStringLiteral("Delete profile"),
                              QStringLiteral("Delete the profile \"%1\"?").arg(m_profileBox->currentText())) != QMessageBox::Yes)
        return;
    m_profiles.removeAt(i - 1);
    QSettings s;
    saveProfiles(s, m_profiles);
    reloadProfiles();
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
    if (m_link->state() == LinkState::Connecting) {
        saveSettings();
        m_hideDrawerOnHeartbeat = true;  // give the space back to the video once the payload answers
    }
}

void MainWindow::onStateChanged()
{
    const LinkState st = m_link->state();
    const bool idle = st == LinkState::Disconnected || st == LinkState::Failed;
    m_ip->setEnabled(idle);
    m_port->setEnabled(idle);
    m_profileBox->setEnabled(idle);
    m_saveProfile->setEnabled(idle);
    m_deleteProfile->setEnabled(idle);
    m_connectBtn->setEnabled(idle || isOpen(st));
    m_connectBtn->setText(isOpen(st) ? QStringLiteral("Disconnect") : QStringLiteral("Connect"));
    m_stateBadge->setText(colored(QStringLiteral("&#9679;"), lampColor(linkLamp(st))) +
                          QStringLiteral(" <b>%1</b>").arg(linkStateText(st).toHtmlEscaped()));
    if (st == LinkState::Heartbeat && m_hideDrawerOnHeartbeat) {
        m_hideDrawerOnHeartbeat = false;
        setDrawerOpen(false);
    }
    if (st == LinkState::Failed) {
        m_hideDrawerOnHeartbeat = false;
        setDrawerOpen(true);  // the reason is shown there
    }
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
        ? colored(QStringLiteral("Capture queue overflow: %1 datagrams not shown").arg(m_link->droppedDatagrams()), Theme::Bad)
        : QStringLiteral("Capture queue OK"));
    m_rates->setText(colored(QStringLiteral("RX %1/s  TX %2/s").arg(rxRate, 3).arg(txRate, 3).replace(QLatin1Char(' '), QStringLiteral("&nbsp;")), Theme::Dim));
}

void MainWindow::refreshOperator()
{
    const LinkState st = m_link->state();
    const qint64 nowMs = PayloadLink::monoNs() / 1000000;
    const int fresh = freshComponents(m_link->heartbeatSources(), kPayloadComponents, nowMs, m_link->timeoutMs());
    m_payloadLamp->setText(lampText(QStringLiteral("PAYLOAD"), payloadLamp(isOpen(st), fresh, kPayloadComponents.size()),
                                    isOpen(st) ? QStringLiteral("%1/%2").arg(fresh).arg(kPayloadComponents.size()) : QStringLiteral("—")));
    m_payloadLamp->setToolTip(QStringLiteral("Payload core (compid 26), camera (100) and gimbal (154) with a HEARTBEAT "
                                             "within the timeout"));
    m_linkLamp->setText(lampText(QStringLiteral("MAVLINK"), linkLamp(st), linkShort(st)));
    m_linkLamp->setToolTip(linkStateText(st));

    const VideoPlayer *v = m_video->player();
    Lamp vl = Lamp::Off;
    QString vd = QStringLiteral("off");
    switch (v->state()) {
    case VideoPlayer::State::Stopped: break;
    case VideoPlayer::State::Connecting: vl = Lamp::Warn; vd = QStringLiteral("connecting"); break;
    case VideoPlayer::State::Playing: vl = Lamp::Ok; vd = QStringLiteral("%1 fps").arg(v->fps(), 0, 'f', 0); break;
    case VideoPlayer::State::Error: vl = Lamp::Bad; vd = QStringLiteral("error"); break;
    }
    m_videoLamp->setText(lampText(QStringLiteral("VIDEO"), vl, vd));
    m_videoLamp->setToolTip(v->state() == VideoPlayer::State::Error ? v->error() : QStringLiteral("RTSP video stream"));

    // Warnings: only conditions observed on the link
    const OperatorStatus o = m_link->operatorStatus();
    QStringList warn;
    if (o.attitude && o.attitude->failureFlags) warn << QStringLiteral("GIMBAL FAILURE 0x%1").arg(o.attitude->failureFlags, 0, 16);
    if (m_link->droppedDatagrams()) warn << QStringLiteral("CAPTURE DROPS %1").arg(m_link->droppedDatagrams());
    if (m_stats.sendErrors) warn << QStringLiteral("SEND ERRORS %1").arg(m_stats.sendErrors);
    if (m_stats.badCrc) warn << QStringLiteral("BAD CRC %1").arg(m_stats.badCrc);
    m_warning->setText(warn.isEmpty() ? QString() : QStringLiteral("⚠ ") + warn.join(QStringLiteral("  ·  ")));

    // Video overlay
    const QColor text(Theme::Text), dim(Theme::Dim);
    const Qt::Alignment tl = Qt::AlignTop | Qt::AlignLeft, tr = Qt::AlignTop | Qt::AlignRight,
                        bl = Qt::AlignBottom | Qt::AlignLeft, br = Qt::AlignBottom | Qt::AlignRight;
    std::vector<HudText> hud;
    if (o.recording.value_or(false)) hud.push_back({tl, QStringLiteral("● REC %1").arg(clockText(o.recordingS)), QColor(Theme::Bad)});
    if (!isOpen(st)) hud.push_back({tl, QStringLiteral("MAVLINK OFFLINE"), QColor(Theme::Warn)});
    else if (st != LinkState::Heartbeat) hud.push_back({tl, QStringLiteral("MAVLINK ") + linkShort(st).toUpper(), QColor(Theme::Warn)});
    if (o.eoZoom) hud.push_back({tr, QStringLiteral("ZOOM %1×").arg(*o.eoZoom, 0, 'f', 1), text});
    if (o.attitude) {
        hud.push_back({tr, GimbalFlags::modeName(o.attitude->flags), QColor(Theme::Accent)});
        hud.push_back({bl, QStringLiteral("P %1°  R %2°  Y %3°").arg(o.attitude->pitchDeg, 6, 'f', 1).arg(o.attitude->rollDeg, 6, 'f', 1)
                               .arg(o.attitude->yawDeg, 6, 'f', 1), text});
    } else if (isOpen(st)) {
        hud.push_back({bl, QStringLiteral("GIMBAL: NO ATTITUDE"), dim});
    }
    QRectF box;
    QColor boxColor;
    if (o.trackStatus) {
        const bool tracked = *o.trackStatus == PayloadLink::TrackTracked, lost = *o.trackStatus == PayloadLink::TrackLost;
        boxColor = QColor(tracked ? Theme::Ok : lost ? Theme::Bad : Theme::Dim);
        hud.push_back({br, QStringLiteral("TRK ") + trackingStateText(*o.trackMode, *o.trackStatus), boxColor});
        if (o.trackBox && (tracked || lost))
            box = QRectF(o.trackBox->x() / PayloadLink::kTrackerWidth, o.trackBox->y() / PayloadLink::kTrackerHeight,
                         o.trackBox->width() / PayloadLink::kTrackerWidth, o.trackBox->height() / PayloadLink::kTrackerHeight);
    }
    if (m_camera->trackArmed()) hud.push_back({br, QStringLiteral("CLICK TO TRACK"), QColor(Theme::Accent)});
    m_video->view()->setHud(std::move(hud), box, boxColor);
}
