#pragma once
// Small helpers shared by the control and telemetry pages.

#include <QLabel>
#include <QPushButton>
#include <QString>

// Rich text for a telemetry value: "12.3 (0.4 s ago)", greyed "(stale ...)" past the timeout,
// or "not received" when there is no value at all.
inline QString agedValue(const QString &value, qint64 sampleNs, qint64 nowNs, int timeoutMs)
{
    if (sampleNs < 0) return QStringLiteral("<span style='color:#888'>not received</span>");
    const double age = double(nowNs - sampleNs) / 1e9;
    const QString v = value.toHtmlEscaped();
    if (age * 1000 > timeoutMs)
        return QStringLiteral("<span style='color:#888'>%1 (stale, %2 s ago)</span>").arg(v).arg(age, 0, 'f', 1);
    return QStringLiteral("%1 <span style='color:#888'>(%2 s ago)</span>").arg(v).arg(age, 0, 'f', 1);
}

inline QLabel *valueLabel()
{
    auto *l = new QLabel(QStringLiteral("<span style='color:#888'>not received</span>"));
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

inline QPushButton *button(const QString &text, const QString &tip = {})
{
    auto *b = new QPushButton(text);
    if (!tip.isEmpty()) b->setToolTip(tip);
    return b;
}
