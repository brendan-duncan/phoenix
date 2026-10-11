#include "gui/main_window.h"
#include "gui/updater.h"
#include <QApplication>
#include <QStringList>

int main(int argc, char *argv[])
{
    // Before anything else: the installer and the updater run Phoenix with
    // arguments of their own, which this answers and exits on.
    Updater::handleStartup();

    QApplication a(argc, argv);
    QApplication::setApplicationVersion(Updater::currentVersion());

    // A file named on the command line, or dropped on the executable, which
    // Windows passes the same way, opens instead of the most recent one.
    const QStringList arguments = a.arguments();

    // Phoenix --export-movie <document> <output.png|output.mp4>: writes
    // every frame and exits, without showing a window.
    if (arguments.size() == 4 && arguments.at(1) == "--export-movie")
    {
        MainWindow window(QString(), nullptr, false);
        return window.exportFromCommandLine(arguments.at(2), arguments.at(3));
    }

    const QString initialFile = arguments.size() > 1 ? arguments.at(1) : QString();

    MainWindow window(initialFile);
    window.show();

    return a.exec();
}
