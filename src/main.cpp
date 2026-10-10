#include "gui/main_window.h"
#include <QApplication>
#include <QStringList>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // A file named on the command line, or dropped on the executable, which
    // Windows passes the same way, opens instead of the most recent one.
    const QStringList arguments = a.arguments();
    const QString initialFile = arguments.size() > 1 ? arguments.at(1) : QString();

    MainWindow window(initialFile);
    window.show();

    return a.exec();
}
