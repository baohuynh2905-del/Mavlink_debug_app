#include "main_window.h"
#include "theme.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Gremsy"));
    QApplication::setApplicationName(QStringLiteral("MavlinkDebugApp"));
    applyTheme(app);
    MainWindow w;
    w.show();
    return app.exec();
}
