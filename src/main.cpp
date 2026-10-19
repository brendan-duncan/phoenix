#include "gui/crash_dump.h"
#include "gui/app_identity.h"
#include "gui/command_line.h"
#include "gui/web_page.h"
#include "gui/mcp_server.h"
#include "gui/script_runner.h"
#include "gui/main_window.h"
#include "gui/theme.h"
#include "gui/updater.h"
#include "phx/container.h"
#include "phx/phx_document.h"
#include "data/fla_document.h"
#include "parser/fla_parser.h"
#include "parser/swf_import.h"
#include "writer/fla_writer.h"
#include "writer/xfl_content.h"
#include "writer/xfl_folder_writer.h"
#include <QApplication>
#include <QImage>
#include <QProxyStyle>
#include <QStyleOptionMenuItem>
#include <QTimer>
#include <QSettings>
#include <cmath>
#include <algorithm>
#include <QFileInfo>
#include <QDir>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileOpenEvent>
#include <QStringList>

#include <cstdio>
#include <cstring>
#include <clocale>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>

namespace {

/// Hands the files the system asks Phoenix to open to \a open.
class FileOpenHandler : public QObject
{
public:
    explicit FileOpenHandler(std::function<void(const QString&)> open)
        : _open(std::move(open))
    {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::FileOpen)
        {
            openWhenIdle(static_cast<QFileOpenEvent*>(event)->file());
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    /// A file can arrive inside a modal dialog or an export's progress
    /// loop; replacing the document there would pull it out from under
    /// them. It waits until nothing modal is up.
    void openWhenIdle(const QString& file)
    {
        if (QApplication::activeModalWidget() || QApplication::activePopupWidget())
        {
            QTimer::singleShot(500, this, [this, file]() { openWhenIdle(file); });
            return;
        }
        _open(file);
    }

    std::function<void(const QString&)> _open;
};

/// The platform's style, with room between a menu item's name and its
/// shortcut: the Windows style leaves almost none, and a long name runs
/// into its keys.
class MenuSpacingStyle : public QProxyStyle
{
public:
    QSize sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& size,
                           const QWidget* widget) const override
    {
        QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
        if (type == CT_MenuItem)
        {
            const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
            if (item && item->text.contains('\t'))
                result.rwidth() += 24;
        }
        return result;
    }
};

} // namespace

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

/// Phoenix is a console program, so the commands that print (render,
/// doctor, --export-movie) print where they were typed. Opened as the
/// editor from Explorer or a shortcut, though, the console was made for it
/// alone and is no use: it is let go. Typed in a terminal, the terminal's
/// stays.
void releaseOwnConsole()
{
    DWORD processes[2];
    if (GetConsoleWindow() && GetConsoleProcessList(processes, 2) <= 1)
        FreeConsole();
}
#else
void releaseOwnConsole() {}
#endif

int main(int argc, char *argv[])
{
    // A crash leaves a dump to find out from what (Windows).
    CrashDump::install();

    // Before anything else: the installer and the updater run Phoenix with
    // arguments of their own, which this answers and exits on.
    Updater::handleStartup();

    // Phoenix --phx-dump file.phx: the file's parts as text, no window.
    if (argc == 3 && std::strcmp(argv[1], "--phx-dump") == 0)
    {
        const std::string text = phx::dump(argv[2]);
        std::fwrite(text.data(), 1, text.size(), stdout);
        return 0;
    }

    // Phoenix --convert in out: between .phx, .fla and XFL folders, by the
    // output's extension, no window.
    if (argc == 4 && std::strcmp(argv[1], "--convert") == 0)
    {
        std::string error;
        if (!commands::convert(argv[2], argv[3], error))
        {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        return 0;
    }

    // The pipeline commands (render, doctor, diff, batch) run with no
    // window; on a render node with no display, Qt draws offscreen.
    const bool command = argc > 1 && commands::isCommand(QString::fromLocal8Bit(argv[1]));
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    if (command && qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") &&
        qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    // capture-window draws the whole window for the guide with settings of
    // its own, so the panels sit in their default places and the user's
    // own layout and recent files are left alone.
    QString captureSettings;
    if (argc > 1 && std::strcmp(argv[1], "capture-window") == 0 && qEnvironmentVariableIsEmpty("PHOENIX_SETTINGS_DIR"))
    {
        captureSettings =
            QDir(QDir::tempPath()).filePath("phoenix-capture-" + QString::number(QCoreApplication::applicationPid()));
        qputenv("PHOENIX_SETTINGS_DIR", captureSettings.toLocal8Bit());
    }

#ifdef Q_OS_WIN
    // Exporting headless (QT_QPA_PLATFORM=offscreen) reads fonts from a
    // folder rather than from Windows, and Qt's own folder has none: text
    // would come out as empty boxes. Point it at the system's.
    if (argc > 1 && (command || std::strcmp(argv[1], "--export-movie") == 0 || std::strcmp(argv[1], "--export-image") == 0 ||
                     std::strcmp(argv[1], "--roundtrip") == 0 || std::strcmp(argv[1], "--convert") == 0) &&
        qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR"))
    {
        const QByteArray windows = qgetenv("WINDIR");
        if (!windows.isEmpty())
            qputenv("QT_QPA_FONTDIR", windows + "\\Fonts");
    }
#endif

    // PHOENIX_SETTINGS_DIR keeps the settings in INI files in that folder
    // rather than the user's own: for tests, demos and shared machines.
    const QString settingsDir = qEnvironmentVariable("PHOENIX_SETTINGS_DIR");
    if (!settingsDir.isEmpty())
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir);
    }

    // Phoenix's name, which places its settings; settings kept under the
    // old name are brought over on the first start.
    app_identity::apply();
    app_identity::adoptOldSettings();

    // Phoenix --mcp with a Phoenix already running that takes AI assistants:
    // this process only passes messages between the two, with no window.
    if (argc == 2 && std::strcmp(argv[1], "--mcp") == 0 && McpServer::relayToRunning())
        return 0;
    // Phoenix --mcp --port N: started by a Phoenix for its own session (Open
    // Claude Code); only that Phoenix will do, never a new window.
    if (argc == 4 && std::strcmp(argv[1], "--mcp") == 0 && std::strcmp(argv[2], "--port") == 0)
    {
        const int port = std::atoi(argv[3]);
        if (port > 0 && port < 65536 && McpServer::relayToRunning(port))
            return 0;
        std::fprintf(stderr, "The Phoenix that started this assistant is no longer running.\n");
        return 1;
    }

    // The theme's UI scale, which Qt takes only before it starts.
    {
        const double scale = QSettings().value("theme/uiScale", 1.0).toDouble();
        if (scale >= 0.75 && scale <= 2.5 && scale != 1.0 && qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR"))
            qputenv("QT_SCALE_FACTOR", QByteArray::number(scale));
    }

    QApplication a(argc, argv);
    // Qt takes the system's locale on Linux and macOS, and with it a comma
    // for the decimal point in printf and strtod: files would be written
    // with "0,5" and read back wrong. Numbers in files are always "C".
    std::setlocale(LC_NUMERIC, "C");
    // SWF import decodes PNG, GIF and JPEG-with-alpha bitmaps with Qt.
    SwfImport::setImageDecoder([](const std::vector<uint8_t>& bytes, SwfImport::Image& image) {
        QImage decoded;
        if (!decoded.loadFromData(bytes.data(), static_cast<int>(bytes.size())))
            return false;
        decoded = decoded.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        image.width = decoded.width();
        image.height = decoded.height();
        image.pixels.resize(static_cast<size_t>(image.width) * static_cast<size_t>(image.height));
        for (int y = 0; y < image.height; ++y)
            std::memcpy(image.pixels.data() + static_cast<size_t>(y) * image.width, decoded.constScanLine(y),
                static_cast<size_t>(image.width) * 4);
        return true;
    });
    QApplication::setStyle(new MenuSpacingStyle);
    QApplication::setApplicationVersion(Updater::currentVersion());

    // A file named on the command line, or dropped on the executable, which
    // Windows passes the same way, opens instead of the most recent one.
    const QStringList arguments = a.arguments();

    if (command)
    {
        const int result = commands::run(arguments);
        // The settings capture-window kept apart go with it.
        if (!captureSettings.isEmpty())
            QDir(captureSettings).removeRecursively();
        return result;
    }

    // Phoenix --export-movie <document> <output.png|output.mp4>: writes
    // every frame and exits, without showing a window.
    // Phoenix --export-image <document> <out.png|jpg|gif> [options]: one
    // frame, without a window.
    // Phoenix --view <file>: the file in a player window, no editor: a
    // quick viewer for documents, SWF movies, artwork and images.
    if (arguments.size() >= 3 && (arguments.at(1) == "--view" || arguments.at(1) == "-view"))
    {
        releaseOwnConsole();
        MainWindow window(QString(), nullptr, false);
        return window.viewFromCommandLine(arguments.at(2));
    }

    if (arguments.size() >= 4 && arguments.at(1) == "--export-image")
    {
        MainWindow window(QString(), nullptr, false);
        return window.exportImageFromCommandLine(arguments.at(2), arguments.at(3), arguments.mid(4));
    }

    // Phoenix --roundtrip <document> <folder> [samples]: saves the document
    // as .fla and as .phx in the folder, opens each again and compares
    // sample frames with the original's, pixel by pixel. One line of JSON on
    // stdout; images of what differs in the folder. Exits 2 when a copy
    // draws differently.
    if (arguments.size() >= 4 && arguments.at(1) == "--roundtrip")
    {
        const QString source = arguments.at(2);
        const QDir folder(arguments.at(3));
        folder.mkpath(".");
        const int samples = arguments.size() > 4 ? std::max(1, arguments.at(4).toInt()) : 5;
        const QString base = QFileInfo(source).completeBaseName();
        MainWindow window(QString(), nullptr, false);
        std::vector<int> frames;
        QSize size;
        const std::vector<QImage> original = window.renderSamples(source, frames, size, samples);
        QJsonObject report;
        report["file"] = source;
        QJsonArray frameList;
        for (int frame : frames)
            frameList.append(frame);
        report["frames"] = frameList;
        bool differs = false;
        if (original.empty())
        {
            report["error"] = "could not open";
            differs = true;
        }
        for (const char* extension : {"fla", "phx"})
        {
            if (original.empty())
                break;
            QJsonObject result;
            const QString copy = folder.filePath(base + "_rt." + extension);
            std::string error;
            if (!commands::convert(source.toStdString(), copy.toStdString(), error))
            {
                result["error"] = QString::fromStdString(error);
                report[extension] = result;
                differs = true;
                continue;
            }
            std::vector<int> same = frames;
            QSize sameSize = size;
            const std::vector<QImage> again = window.renderSamples(copy, same, sameSize, samples);
            if (again.size() != original.size())
            {
                result["error"] = "could not reopen";
                report[extension] = result;
                differs = true;
                continue;
            }
            // Pixels apart by more than antialiasing noise.
            int worst = 0;
            qint64 changed = 0;
            QJsonArray changedFrames;
            for (size_t i = 0; i < original.size(); ++i)
            {
                const QImage& a = original[i];
                const QImage& b = again[i];
                QImage diff(a.size(), QImage::Format_ARGB32);
                diff.fill(Qt::white);
                qint64 here = 0;
                for (int y = 0; y < a.height() && y < b.height(); ++y)
                {
                    const QRgb* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
                    const QRgb* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
                    QRgb* pd = reinterpret_cast<QRgb*>(diff.scanLine(y));
                    for (int x = 0; x < a.width() && x < b.width(); ++x)
                    {
                        const int d = std::max({std::abs(qRed(pa[x]) - qRed(pb[x])), std::abs(qGreen(pa[x]) - qGreen(pb[x])),
                            std::abs(qBlue(pa[x]) - qBlue(pb[x])), std::abs(qAlpha(pa[x]) - qAlpha(pb[x]))});
                        worst = std::max(worst, d);
                        if (d > 8)
                        {
                            ++here;
                            pd[x] = qRgb(255, 0, 0);
                        }
                        else
                        {
                            pd[x] = qRgb(qGray(pa[x]) / 2 + 128, qGray(pa[x]) / 2 + 128, qGray(pa[x]) / 2 + 128);
                        }
                    }
                }
                if (here > 0)
                {
                    changedFrames.append(frames[i]);
                    diff.save(folder.filePath(QString("%1_%2_frame%3_diff.png").arg(base, extension).arg(frames[i] + 1)));
                    a.save(folder.filePath(QString("%1_frame%2_original.png").arg(base).arg(frames[i] + 1)));
                    b.save(folder.filePath(QString("%1_%2_frame%3_copy.png").arg(base, extension).arg(frames[i] + 1)));
                }
                changed += here;
            }
            result["maxDifference"] = worst;
            result["pixelsChanged"] = changed;
            result["framesChanged"] = changedFrames;
            report[extension] = result;
            differs = differs || changed > 0;
        }
        std::printf("%s\n", QJsonDocument(report).toJson(QJsonDocument::Compact).constData());
        std::fflush(stdout);
        return differs ? 2 : 0;
    }

    // With --per-layer, a PNG sequence for each layer.
    if (arguments.size() == 5 && arguments.at(1) == "--export-movie" && arguments.at(4) == "--per-layer")
    {
        MainWindow window(QString(), nullptr, false);
        return window.exportFromCommandLine(arguments.at(2), arguments.at(3), true);
    }

    // Phoenix --convert <in> <out> --for-other-apps: written as Export FLA
    // for Other Apps writes it, everything Phoenix's own baked.
    if (arguments.size() == 5 && arguments.at(1) == "--convert" && arguments.at(4) == "--for-other-apps")
    {
        MainWindow window(QString(), nullptr, false);
        return window.exportForOtherAppsFromCommandLine(arguments.at(2), arguments.at(3));
    }

    if (arguments.size() == 4 && arguments.at(1) == "--export-movie")
    {
        MainWindow window(QString(), nullptr, false);
        return window.exportFromCommandLine(arguments.at(2), arguments.at(3));
    }

    // Phoenix --mcp: an AI assistant (an MCP client) started Phoenix to work
    // with it over standard input and output.
    bool mcp = false;
    QStringList launch = arguments;
    if (launch.size() > 1 && launch.at(1) == "--mcp")
    {
        mcp = true;
        launch.removeAt(1);
        // The assistant works with the window; no start screen in its way.
        MainWindow::skipStartScreen();
    }

    // Phoenix --tutorial <lesson>: starts with that lesson running.
    QString tutorial;
    const int tutorialAt = static_cast<int>(launch.indexOf("--tutorial"));
    if (tutorialAt > 0 && tutorialAt + 1 < launch.size())
    {
        tutorial = launch.at(tutorialAt + 1);
        launch.remove(tutorialAt, 2);
        MainWindow::skipStartScreen();
    }
    QString initialFile = launch.size() > 1 ? launch.at(1) : QString();

    // Phoenix script.jsfl (or script.py): a Phoenix already running runs
    // it; else this one does, once its window is up.
    QString script;
    if (ScriptRunner::isScript(initialFile))
    {
        if (ScriptRunner::handToRunning(initialFile))
            return 0;
        script = QFileInfo(initialFile).absoluteFilePath();
        initialFile.clear();
        MainWindow::skipStartScreen();
    }
    // Phoenix extension.zxp: installed once the window is up.
    QString package;
    if (initialFile.endsWith(".zxp", Qt::CaseInsensitive))
    {
        package = QFileInfo(initialFile).absoluteFilePath();
        initialFile.clear();
        MainWindow::skipStartScreen();
    }

    // The theme last chosen, before any window is made.
#ifdef PHOENIX_WEB
    web_page::install();
#endif
    app_identity::apply();
    theme::applySaved();
    releaseOwnConsole();
    MainWindow window(initialFile);
    window.show();
    if (!script.isEmpty())
        QTimer::singleShot(300, &window, [&window, script]() { window.scripts()->runFile(script); });
    if (!package.isEmpty())
        QTimer::singleShot(300, &window, [&window, package]() { ScriptRunner::installExtension(package, &window); });
    if (!tutorial.isEmpty())
        QTimer::singleShot(300, &window, [&window, tutorial]() { window.startTutorial(tutorial); });
    if (mcp)
        window.serveMcpOnStdio();

    // On macOS a file opened from Finder arrives as an event, not as an
    // argument.
    FileOpenHandler fileOpen([&window](const QString& path) { window.openDocumentPath(path); });
    a.installEventFilter(&fileOpen);

    return a.exec();
}
