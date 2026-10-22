#include "main_window.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Gremsy"));
    QApplication::setApplicationName(QStringLiteral("MavlinkDebugApp"));
    MainWindow w;
    w.show();
    return app.exec();
}
