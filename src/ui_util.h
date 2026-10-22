#pragma once
// Small helpers shared by the control and telemetry pages.

#include "theme.h"

#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QString>

// Rich text in one of the Theme colours. text must already be HTML-escaped.
inline QString colored(const QString &text, const char *color)
{
    return QStringLiteral("<span style='color:%1'>%2</span>").arg(QLatin1String(color), text);
}

// Rich text for a telemetry value: "12.3 (0.4 s ago)", dimmed "(stale ...)" past the timeout,
// or "not received" when there is no value at all.
inline QString agedValue(const QString &value, qint64 sampleNs, qint64 nowNs, int timeoutMs)
{
    if (sampleNs < 0) return colored(QStringLiteral("not received"), Theme::Dim);
    const double age = double(nowNs - sampleNs) / 1e9;
    const QString v = value.toHtmlEscaped();
    if (age * 1000 > timeoutMs) return colored(QStringLiteral("%1 (stale, %2 s ago)").arg(v).arg(age, 0, 'f', 1), Theme::Dim);
    return v + QLatin1Char(' ') + colored(QStringLiteral("(%1 s ago)").arg(age, 0, 'f', 1), Theme::Dim);
}

inline QString clockText(double seconds)  // m:ss
{
    const int s = int(seconds);
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

inline QLabel *valueLabel()
{
    auto *l = new QLabel(colored(QStringLiteral("not received"), Theme::Dim));
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

inline QPushButton *button(const QString &text, const QString &tip = {}, const char *role = nullptr)
{
    auto *b = new QPushButton(text);
    if (!tip.isEmpty()) b->setToolTip(tip);
    if (role) b->setProperty("role", QLatin1String(role));  // styled by theme.cpp
    return b;
}

// Toggles a stylesheet-visible property (e.g. "active") and re-applies the style.
inline void setStyleFlag(QWidget *w, const char *name, bool on)
{
    if (w->property(name).toBool() == on) return;
    w->setProperty(name, on);
    w->style()->unpolish(w);
    w->style()->polish(w);
}
