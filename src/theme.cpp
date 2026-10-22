#include "theme.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>

// @Name is replaced by the Theme colour of that name, so the stylesheet and the code share one palette.
static const char *kStyle = R"(
#header { background: #0d1524; border-bottom: 1px solid @Border; }
#drawer { background: @Panel; border-bottom: 1px solid @Border; }
QLabel#appTitle { color: @Text; font-size: 12pt; font-weight: 700; }
QLabel#appSubtitle { color: @Dim; }
QLabel#warning { color: @Bad; font-weight: 600; }

QFrame#panel { background: @Panel; border: 1px solid @Border; border-radius: 4px; }
QLabel#panelTitle { color: @Accent; font-weight: 700; padding: 6px 10px; border-bottom: 1px solid @Border; }
QLabel#readout { color: @Text; background: #0c1421; border: 1px solid @Border; border-radius: 3px; padding: 4px; }
QLabel#readoutCaption { color: @Dim; font-size: 8pt; font-weight: 600; }

QGroupBox { border: 1px solid @Border; border-radius: 4px; margin-top: 16px; padding: 6px 4px 4px 4px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: @Dim; font-size: 8pt; font-weight: 700; }

QPushButton { background: @Raised; color: @Text; border: 1px solid #2a3b57; border-radius: 3px; padding: 5px 10px; }
QPushButton:hover { border-color: @Accent; }
QPushButton:pressed, QPushButton:checked { background: #0f3a4a; border-color: @Accent; color: #e8fbff; }
QPushButton:disabled { background: #111a28; color: #45536a; border-color: #1a2537; }
QPushButton[active="true"] { background: #0f3a4a; border-color: @Accent; color: @Accent; font-weight: 700; }
QPushButton[role="primary"] { background: #0d4f60; border-color: @Accent; font-weight: 700; }
QPushButton[role="danger"] { background: #3a1518; border-color: #7a2a2f; color: #ffd9d9; font-weight: 700; }
QPushButton[role="danger"]:hover { border-color: @Bad; }
QPushButton[role="danger"][active="true"] { background: #b3262d; border-color: @Bad; color: #ffffff; }
QPushButton[role="primary"]:disabled, QPushButton[role="danger"]:disabled { background: #111a28; color: #45536a; border-color: #1a2537; }
QPushButton[role="segment"] { border-radius: 0; padding: 4px 12px; color: @Dim; font-weight: 600; }
QPushButton[role="segment"]:checked { color: @Accent; background: #0f2f3d; border-color: @Accent; }
QPushButton[role="pad"] { font-size: 13pt; padding: 4px; min-width: 38px; min-height: 30px; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: #0c1421; color: @Text; border: 1px solid #2a3b57; border-radius: 3px; padding: 3px 6px;
    selection-background-color: #1b5568;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: @Accent; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { color: #45536a; border-color: #1a2537; }
QComboBox QAbstractItemView { background: @Raised; border: 1px solid @Border; selection-background-color: #14445a; }

QTableView, QTreeWidget, QTableWidget, QPlainTextEdit {
    background: #0b1320; alternate-background-color: #0f1827; color: @Text; gridline-color: #182436;
    border: 1px solid @Border; selection-background-color: #14445a; selection-color: #ffffff;
}
QHeaderView::section { background: #111b2b; color: @Dim; border: none; border-right: 1px solid @Border;
    border-bottom: 1px solid @Border; padding: 4px 6px; font-weight: 700; }

QTabWidget::pane { border: 1px solid @Border; top: -1px; background: @Panel; }
QTabBar::tab { background: transparent; color: @Dim; padding: 7px 16px; border: none; border-bottom: 2px solid transparent; font-weight: 600; }
QTabBar::tab:selected { color: @Text; border-bottom-color: @Accent; }
QTabBar::tab:hover { color: @Text; }

QSplitter::handle { background: @Bg; }
QSplitter::handle:hover { background: #164a5a; }
QScrollArea { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle { background: #26364f; border-radius: 4px; min-height: 24px; min-width: 24px; }
QScrollBar::handle:hover { background: #33496a; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QSlider::groove:horizontal { height: 4px; background: #26364f; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #1b7088; border-radius: 2px; }
QSlider::handle:horizontal { background: @Accent; width: 12px; margin: -5px 0; border-radius: 6px; }

QStatusBar { background: #0d1524; color: @Dim; border-top: 1px solid @Border; }
QStatusBar QLabel { color: @Dim; }
QToolTip { background: #142033; color: @Text; border: 1px solid @Border; padding: 4px; }
)";

void applyTheme(QApplication &app)
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));  // the same look on every desktop theme

    QFont f(QStringList{QStringLiteral("Ubuntu Sans"), QStringLiteral("Ubuntu"), QStringLiteral("Noto Sans"),
                        QStringLiteral("DejaVu Sans")});
    f.setPointSize(10);
    QApplication::setFont(f);

    QPalette p;
    const QColor bg(Theme::Bg), text(Theme::Text), dim(Theme::Dim);
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, QColor(0x0b, 0x13, 0x20));
    p.setColor(QPalette::AlternateBase, QColor(0x0f, 0x18, 0x27));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(Theme::Raised));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, QColor(0x14, 0x44, 0x5a));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, QColor(0x14, 0x20, 0x33));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, dim);
    p.setColor(QPalette::Link, QColor(Theme::Accent));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor(0x45, 0x53, 0x6a));
    QApplication::setPalette(p);

    QString style = QString::fromLatin1(kStyle);
    const std::pair<const char *, const char *> tokens[] = {{"@Bg", Theme::Bg}, {"@Panel", Theme::Panel},
        {"@Raised", Theme::Raised}, {"@Border", Theme::Border}, {"@Text", Theme::Text}, {"@Dim", Theme::Dim},
        {"@Accent", Theme::Accent}, {"@Bad", Theme::Bad}};
    for (const auto &[name, color] : tokens) style.replace(QLatin1String(name), QLatin1String(color));
    app.setStyleSheet(style);
}

QFont monoFont(int pointSize)
{
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setFamilies({QStringLiteral("Ubuntu Sans Mono"), QStringLiteral("Ubuntu Mono"), QStringLiteral("DejaVu Sans Mono"), f.family()});
    f.setPointSize(pointSize);
    return f;
}
