#include "gui/main_window.h"
#include "gui/theme.h"
#include "gui/updater.h"
#include "phx/container.h"
#include "phx/phx_document.h"
#include "data/fla_document.h"
#include "parser/fla_parser.h"
#include "writer/fla_writer.h"
#include "writer/xfl_content.h"
#include "writer/xfl_folder_writer.h"
#include <QApplication>
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
#include <cctype>
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
            _open(static_cast<QFileOpenEvent*>(event)->file());
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void(const QString&)> _open;
};

/// Reads \a in (.phx, .fla or an XFL folder) and writes it to \a out in the
/// format its extension names: .phx, .fla, or anything else an XFL folder.
bool convertDocument(const std::string& in, const std::string& out, std::string& error)
{
    std::unique_ptr<fla::FLADocument> document;
    if (phx::isPhx(in))
    {
        std::string warning;
        bool readOnly = false;
        document.reset(phx::load(in, error, warning, readOnly));
        if (!warning.empty())
            std::fprintf(stderr, "%s", warning.c_str());
    }
    else
    {
        FLAParser parser;
        document.reset(parser.parse(in));
        error = parser.errorString();
    }
    if (!document || !document->document)
    {
        if (error.empty())
            error = "Could not read " + in;
        return false;
    }
    const auto endsWith = [&out](const char* suffix) {
        const size_t n = std::strlen(suffix);
        if (out.size() < n)
            return false;
        for (size_t i = 0; i < n; ++i)
        {
            if (std::tolower(static_cast<unsigned char>(out[out.size() - n + i])) != suffix[i])
                return false;
        }
        return true;
    };
    if (endsWith(".phx"))
        return phx::save(*document->document, out, phx::SaveOptions{"Phoenix " + Updater::currentVersion().toStdString(), {}}, error);
    for (const std::string& lost : fla::unsupportedContent(*document->document))
        std::fprintf(stderr, "Not written: %s\n", lost.c_str());
    if (endsWith(".fla"))
    {
        fla::FLAWriter writer;
        if (!writer.write(*document->document, out))
        {
            error = writer.errorString();
            return false;
        }
        return true;
    }
    fla::XFLFolderWriter writer;
    if (!writer.write(*document->document, out))
    {
        error = writer.errorString();
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
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
        if (!convertDocument(argv[2], argv[3], error))
        {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        return 0;
    }

#ifdef Q_OS_WIN
    // Exporting headless (QT_QPA_PLATFORM=offscreen) reads fonts from a
    // folder rather than from Windows, and Qt's own folder has none: text
    // would come out as empty boxes. Point it at the system's.
    if (argc > 1 && (std::strcmp(argv[1], "--export-movie") == 0 || std::strcmp(argv[1], "--export-image") == 0 ||
                     std::strcmp(argv[1], "--roundtrip") == 0) &&
        qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR"))
    {
        const QByteArray windows = qgetenv("WINDIR");
        if (!windows.isEmpty())
            qputenv("QT_QPA_FONTDIR", windows + "\\Fonts");
    }
#endif

    // The theme's UI scale, which Qt takes only before it starts.
    {
        const double scale = QSettings("Phoenix", "FLAViewer").value("theme/uiScale", 1.0).toDouble();
        if (scale >= 0.75 && scale <= 2.5 && scale != 1.0 && qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR"))
            qputenv("QT_SCALE_FACTOR", QByteArray::number(scale));
    }

    QApplication a(argc, argv);
    QApplication::setApplicationVersion(Updater::currentVersion());

    // A file named on the command line, or dropped on the executable, which
    // Windows passes the same way, opens instead of the most recent one.
    const QStringList arguments = a.arguments();

    // Phoenix --export-movie <document> <output.png|output.mp4>: writes
    // every frame and exits, without showing a window.
    // Phoenix --export-image <document> <out.png|jpg|gif> [options]: one
    // frame, without a window.
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
            if (!convertDocument(source.toStdString(), copy.toStdString(), error))
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

    if (arguments.size() == 4 && arguments.at(1) == "--export-movie")
    {
        MainWindow window(QString(), nullptr, false);
        return window.exportFromCommandLine(arguments.at(2), arguments.at(3));
    }

    const QString initialFile = arguments.size() > 1 ? arguments.at(1) : QString();

    // The theme last chosen, before any window is made.
    QCoreApplication::setOrganizationName("Phoenix");
    QCoreApplication::setApplicationName("FLAViewer");
    theme::applySaved();
    MainWindow window(initialFile);
    window.show();

    // On macOS a file opened from Finder arrives as an event, not as an
    // argument.
    FileOpenHandler fileOpen([&window](const QString& path) { window.openDocumentPath(path); });
    a.installEventFilter(&fileOpen);

    return a.exec();
}
