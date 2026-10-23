#include "splash.h"

#include "theme.h"

#include <QConicalGradient>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QScreen>

#include <algorithm>
#include <cmath>

// 0..1 progress of t through [from, from + len], eased out
static double phase(qint64 t, int from, int len)
{
    const double x = std::clamp(double(t - from) / len, 0.0, 1.0);
    return 1 - (1 - x) * (1 - x) * (1 - x);
}

static QColor alpha(const char *color, double a)
{
    QColor c(color);
    c.setAlphaF(float(std::clamp(a, 0.0, 1.0)));
    return c;
}

BootSplash::BootSplash(std::vector<BootCheck> checks, QWidget *parent)
    : QWidget(parent, Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), m_checks(std::move(checks))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(780, 440);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Click to skip"));
    m_frame.setInterval(16);
    connect(&m_frame, &QTimer::timeout, this, &BootSplash::tick);
}

void BootSplash::start()
{
    if (const QScreen *s = QGuiApplication::primaryScreen()) move(s->availableGeometry().center() - rect().center());
    m_clock.start();
    m_frame.start();
    show();
}

void BootSplash::beginFade()
{
    if (m_fadeAt >= 0) return;
    m_fadeAt = m_clock.elapsed();
    emit finished();
}

void BootSplash::tick()
{
    const qint64 t = m_clock.elapsed();
    if (m_fadeAt < 0 && t >= kShowMs) beginFade();
    if (m_fadeAt >= 0) {
        const double f = double(t - m_fadeAt) / kFadeMs;
        if (f >= 1) {
            m_frame.stop();
            hide();
            return;
        }
        setWindowOpacity(1 - f);
    }
    update();
}

void BootSplash::mousePressEvent(QMouseEvent *) { beginFade(); }
void BootSplash::keyPressEvent(QKeyEvent *) { beginFade(); }

void BootSplash::paintEvent(QPaintEvent *)
{
    const qint64 t = m_clock.isValid() ? m_clock.elapsed() : kShowMs;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    // Panel: background, grid, vignette, frame with accent corner brackets
    QPainterPath frame;
    frame.addRoundedRect(r, 10, 10);
    p.fillPath(frame, QColor(Theme::Bg));
    p.save();
    p.setClipPath(frame);
    p.setPen(QPen(alpha(Theme::Accent, 0.05), 1));
    for (int x = 0; x < width(); x += 26) p.drawLine(x, 0, x, height());
    for (int y = 0; y < height(); y += 26) p.drawLine(0, y, width(), y);
    QRadialGradient vignette(r.center(), r.width() * 0.62);
    vignette.setColorAt(0.55, Qt::transparent);
    vignette.setColorAt(1, QColor(0, 0, 0, 150));
    p.fillRect(r, vignette);
    const double scan = std::fmod(double(t) / 1800, 1.0) * height();  // slow scan line
    QLinearGradient sl(0, scan - 40, 0, scan);
    sl.setColorAt(0, Qt::transparent);
    sl.setColorAt(1, alpha(Theme::Accent, 0.07));
    p.fillRect(QRectF(0, scan - 40, width(), 40), sl);
    p.restore();
    p.setPen(QPen(QColor(Theme::Border), 1));
    p.drawPath(frame);
    p.setPen(QPen(QColor(Theme::Accent), 2, Qt::SolidLine, Qt::SquareCap));
    const double c = 18, m = 8;
    for (const QPointF &k : {QPointF(m, m), QPointF(width() - m, m), QPointF(m, height() - m), QPointF(width() - m, height() - m)}) {
        const double sx = k.x() < width() / 2 ? 1 : -1, sy = k.y() < height() / 2 ? 1 : -1;
        p.drawLine(k, k + QPointF(sx * c, 0));
        p.drawLine(k, k + QPointF(0, sy * c));
    }

    // Reticle: ticks drawn in a sweep, counter-rotating arcs, radar wedge, crosshair
    const QPointF o(200, 205);
    const double in = phase(t, 0, 700), R = 128 * (0.85 + 0.15 * in);
    p.save();
    p.translate(o);
    p.setOpacity(in);
    for (int i = 0; i < 72; ++i) {
        if (i * 5 > 360 * phase(t, 0, 900)) break;
        const bool major = i % 6 == 0;
        p.setPen(QPen(major ? alpha(Theme::Accent, 0.9) : alpha(Theme::Dim, 0.6), major ? 2 : 1));
        const double a = qDegreesToRadians(i * 5.0 - 90), r0 = R - (major ? 12 : 6);
        p.drawLine(QPointF(std::cos(a) * r0, std::sin(a) * r0), QPointF(std::cos(a) * R, std::sin(a) * R));
    }
    p.setPen(QPen(QColor(Theme::Border), 1));
    p.drawEllipse(QPointF(), R + 8, R + 8);
    const double spin = double(t) * 0.12;
    p.setPen(QPen(QColor(Theme::Accent), 3, Qt::SolidLine, Qt::RoundCap));
    const QRectF a1(-R * 0.78, -R * 0.78, R * 1.56, R * 1.56), a2(-R * 0.62, -R * 0.62, R * 1.24, R * 1.24);
    for (int k = 0; k < 2; ++k) p.drawArc(a1, int((spin + k * 180) * 16), 70 * 16);
    p.setPen(QPen(alpha(Theme::Accent, 0.55), 2, Qt::SolidLine, Qt::RoundCap));
    for (int k = 0; k < 3; ++k) p.drawArc(a2, int((-spin * 1.6 + k * 120) * 16), 40 * 16);
    QConicalGradient sweep(QPointF(), -std::fmod(double(t) * 0.22, 360.0));
    sweep.setColorAt(0, alpha(Theme::Accent, 0.38));
    sweep.setColorAt(0.18, Qt::transparent);
    sweep.setColorAt(1, Qt::transparent);
    p.setPen(Qt::NoPen);
    p.setBrush(sweep);
    p.drawEllipse(QPointF(), R * 0.58, R * 0.58);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(alpha(Theme::Text, 0.85), 1.5));
    for (const QPointF d : {QPointF(1, 0), QPointF(-1, 0), QPointF(0, 1), QPointF(0, -1)}) p.drawLine(d * 14, d * (R * 0.5));
    const double pulse = 0.5 + 0.5 * std::sin(double(t) / 160);
    p.setPen(QPen(QColor(Theme::Accent), 2));
    p.drawEllipse(QPointF(), 7 + 3 * pulse, 7 + 3 * pulse);
    p.restore();

    // Title, revealed left to right behind a bright scan bar
    const double x0 = 380, reveal = phase(t, 250, 800);
    QFont f = font();
    f.setPointSize(9);
    f.setBold(true);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 6);
    p.setFont(f);
    p.setPen(alpha(Theme::Accent, phase(t, 150, 500)));
    p.drawText(QPointF(x0, 112), QStringLiteral("GREMSY"));
    f.setPointSize(25);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 2);
    p.setFont(f);
    const QRectF title(x0, 122, width() - x0 - 30, 84);
    p.save();
    p.setClipRect(QRectF(x0, title.top(), title.width() * reveal, title.height()));
    p.setPen(QColor(Theme::Text));
    p.drawText(title, Qt::AlignLeft | Qt::AlignTop, QStringLiteral("PAYLOAD\nDEBUG STATION"));
    p.restore();
    if (reveal > 0 && reveal < 1) p.fillRect(QRectF(x0 + title.width() * reveal - 1, title.top(), 2, title.height()), QColor(Theme::Accent));
    f = font();
    f.setPointSize(9);
    p.setFont(f);
    p.setPen(alpha(Theme::Dim, phase(t, 900, 400)));
    p.drawText(QPointF(x0, 226), QStringLiteral("MAVLink · gimbal · camera · video · tracking"));

    // Startup checks, typed in one after another
    QFont mono = monoFont(9);
    p.setFont(mono);
    const QFontMetricsF fm(mono);
    const double lineH = fm.height() + 6, right = width() - 34;
    for (size_t i = 0; i < m_checks.size(); ++i) {
        const int at = 1000 + int(i) * 260;
        if (t < at) break;
        const BootCheck &b = m_checks[i];
        const double y = 262 + double(i) * lineH;
        const QString label = QStringLiteral("▸ ") + b.label;
        const int shown = int(label.size() * phase(t, at, 160));
        p.setPen(QColor(Theme::Text));
        p.drawText(QPointF(x0, y), label.left(shown));
        if (t < at + 160) continue;
        const QString value = b.value + (b.ok ? QStringLiteral("  OK") : QStringLiteral("  !"));
        const double vx = right - fm.horizontalAdvance(value);
        p.setPen(alpha(Theme::Border, 1));
        for (double d = x0 + fm.horizontalAdvance(label) + 8; d < vx - 8; d += 6) p.drawPoint(QPointF(d, y - 3));
        p.setPen(QColor(b.ok ? Theme::Text : Theme::Warn));
        p.drawText(QPointF(vx, y), b.value);
        p.setPen(QColor(b.ok ? Theme::Ok : Theme::Warn));
        p.drawText(QPointF(right - fm.horizontalAdvance(value.mid(b.value.size())), y), value.mid(b.value.size()));
    }

    // Progress line with a glowing head
    const double prog = phase(t, 0, kShowMs - 200), py = height() - 34, px0 = 34, pw = width() - 68;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(Theme::Border));
    p.drawRect(QRectF(px0, py, pw, 2));
    p.setBrush(QColor(Theme::Accent));
    p.drawRect(QRectF(px0, py, pw * prog, 2));
    QRadialGradient glow(QPointF(px0 + pw * prog, py + 1), 14);
    glow.setColorAt(0, alpha(Theme::Accent, 0.7));
    glow.setColorAt(1, Qt::transparent);
    p.setBrush(glow);
    p.drawEllipse(QPointF(px0 + pw * prog, py + 1), 14, 14);
    p.setFont(mono);
    p.setPen(QColor(Theme::Dim));
    p.drawText(QPointF(px0, py - 8), prog < 1 ? QStringLiteral("INITIALISING") : QStringLiteral("READY"));
    const QString pct = QStringLiteral("%1%").arg(int(prog * 100));
    p.drawText(QPointF(px0 + pw - fm.horizontalAdvance(pct), py - 8), pct);
}
