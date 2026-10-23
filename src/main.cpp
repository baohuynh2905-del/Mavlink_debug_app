#include "main_window.h"
#include "theme.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Gremsy"));
    QApplication::setApplicationName(QStringLiteral("MavlinkDebugApp"));
    applyTheme(app);
    MainWindow w;  // built first, so the splash reports what actually loaded
    BootSplash splash(w.bootChecks());
    QObject::connect(&splash, &BootSplash::finished, &w, &QWidget::show);
    splash.start();
    return app.exec();
}
