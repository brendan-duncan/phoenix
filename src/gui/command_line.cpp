#include "command_line.h"

#include "main_window.h"
#include "updater.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../parser/fla_doctor.h"
#include "../parser/fla_parser.h"
#include "../phx/container.h"
#include "../phx/phx_document.h"
#include "../writer/document_diff.h"
#include "../edit/shot_breakdown.h"
#include "../writer/fla_writer.h"
#include "../writer/xfl_content.h"
#include "../writer/xfl_folder_writer.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace commands {

namespace {

const char* const kHelp =
    "Phoenix commands, run with no window:\n"
    "\n"
    "  render <file> [--range 1-240] [--out frames/|movie.mp4|anim.gif] [--size WxH]\n"
    "         [--transparent] [--no-camera] [--no-audio] [--per-layer]\n"
    "      Renders frames: a folder gets a numbered PNG sequence.\n"
    "\n"
    "  doctor <file> [--repair <out.fla|out.phx>] [--json]\n"
    "      Checks a document for damage and repairs what it can; a file that\n"
    "      will not open is salvaged part by part. Exits 2 when errors remain.\n"
    "\n"
    "  diff <before> <after> [--out <folder>] [--samples N]\n"
    "      What changed between two versions: library items and scenes, then\n"
    "      frames compared pixel by pixel (images of what differs in --out).\n"
    "      Exits 2 when they differ.\n"
    "\n"
    "  batch <folder> --doctor | --convert fla|phx|xfl | --render | --python <script.py>\n"
    "        [--save] [--recursive] [--out <folder>]\n"
    "      Runs one of those on every .fla and .phx in the folder. --python\n"
    "      runs the script on each document opened out as an XFL folder\n"
    "      (python script.py <xfl folder> <document>); with --save, what the\n"
    "      script changed is written back.\n"
    "\n"
    "  shots <episode> [--by scene|label|camera] [--out <folder>] [--format fla|phx|xfl]\n"
    "      Splits an episode into shot files: one per scene, per frame label, or\n"
    "      per camera cut. Tweens across a cut are baked so each shot plays as it\n"
    "      did; what a shot does not use is left out of its library.\n"
    "\n"
    "  --convert <in> <out>, --export-image, --export-movie and --roundtrip\n"
    "      work as before.\n";

bool endsWith(const std::string& text, const char* suffix)
{
    const size_t n = std::strlen(suffix);
    if (text.size() < n)
        return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(text[text.size() - n + i])) != suffix[i])
            return false;
    return true;
}

/// Writes \a document to \a out in the format its extension names.
bool save(const fla::Document& document, const std::string& out, std::string& error)
{
    if (endsWith(out, ".phx"))
        return phx::save(document, out, phx::SaveOptions{"Phoenix " + Updater::currentVersion().toStdString(), {}}, error);
    for (const std::string& lost : fla::unsupportedContent(document))
        std::fprintf(stderr, "Not written: %s\n", lost.c_str());
    if (endsWith(out, ".fla"))
    {
        fla::FLAWriter writer;
        if (!writer.write(document, out))
        {
            error = writer.errorString();
            return false;
        }
        return true;
    }
    fla::XFLFolderWriter writer;
    if (!writer.write(document, out))
    {
        error = writer.errorString();
        return false;
    }
    return true;
}

QStringList documentsIn(const QString& folder, bool recursive)
{
    QStringList found;
    QDirIterator it(folder, {"*.fla", "*.phx"}, QDir::Files,
        recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext())
        found << it.next();
    found.sort(Qt::CaseInsensitive);
    return found;
}

/// The value after \a name in \a options, or empty.
QString option(const QStringList& options, const QString& name)
{
    const int at = options.indexOf(name);
    return at >= 0 && at + 1 < options.size() ? options.at(at + 1) : QString();
}

int doctor(const QStringList& args)
{
    if (args.isEmpty())
    {
        std::fprintf(stderr, "doctor: which file?\n");
        return 1;
    }
    const std::string path = args.at(0).toStdString();
    const QString repairTo = option(args, "--repair");
    const bool json = args.contains("--json");

    fla::doctor::Report report;
    if (!QFileInfo(args.at(0)).isDir())
        fla::doctor::checkContainer(path, report);
    std::string error;
    std::unique_ptr<fla::FLADocument> document = load(path, error);
    report.readable = document != nullptr;
    QTemporaryDir salvaged;
    if (!document && !QFileInfo(args.at(0)).isDir())
    {
        // What can be read, part by part, into a folder, and read from there.
        std::vector<std::string> recovered;
        std::vector<std::string> notes;
        std::string salvageError;
        if (salvaged.isValid() &&
            fla::doctor::salvage(path, salvaged.path().toStdString(), recovered, notes, salvageError))
        {
            for (const std::string& note : notes)
            {
                fla::doctor::Issue issue;
                issue.severity = fla::doctor::Issue::Severity::Error;
                issue.where = path;
                issue.what = note;
                issue.repairable = issue.repaired = true;
                report.issues.push_back(issue);
            }
            document = load(salvaged.path().toStdString(), error);
            report.salvaged = document != nullptr;
            fla::doctor::Issue issue;
            issue.severity = fla::doctor::Issue::Severity::Info;
            issue.where = path;
            issue.what = "salvaged " + std::to_string(recovered.size()) + " parts from the damaged archive";
            report.issues.push_back(issue);
        }
    }
    // What salvage got past is repaired when the salvage is written out.
    if (report.salvaged && !repairTo.isEmpty())
        for (fla::doctor::Issue& issue : report.issues)
            if (issue.what.compare(0, 23, "not a readable archive:") == 0 || issue.what.compare(0, 12, "damaged XML:") == 0)
                issue.repaired = true;
    if (!document)
    {
        fla::doctor::Issue issue;
        issue.severity = fla::doctor::Issue::Severity::Error;
        issue.where = path;
        issue.what = "could not be read: " + error;
        report.issues.push_back(issue);
    }
    else
    {
        fla::doctor::checkDocument(*document->document, report, !repairTo.isEmpty());
    }

    if (document && !repairTo.isEmpty())
    {
        std::string saveError;
        if (!save(*document->document, repairTo.toStdString(), saveError))
        {
            std::fprintf(stderr, "Could not write %s: %s\n", qPrintable(repairTo), saveError.c_str());
            return 1;
        }
    }
    const std::string text = json ? report.json() + "\n" : report.text();
    std::fwrite(text.data(), 1, text.size(), stdout);
    if (!document)
        return 1;
    // Errors left: those not repaired.
    const bool left = std::any_of(report.issues.begin(), report.issues.end(), [](const fla::doctor::Issue& issue) {
        return issue.severity == fla::doctor::Issue::Severity::Error && !issue.repaired;
    });
    return left ? 2 : 0;
}

int diff(const QStringList& args)
{
    if (args.size() < 2)
    {
        std::fprintf(stderr, "diff: two files to compare?\n");
        return 1;
    }
    const QString before = args.at(0);
    const QString after = args.at(1);
    const QString out = option(args, "--out");
    const int samples = std::max(1, option(args, "--samples").isEmpty() ? 12 : option(args, "--samples").toInt());

    std::string error;
    std::unique_ptr<fla::FLADocument> a = load(before.toStdString(), error);
    if (!a)
    {
        std::fprintf(stderr, "Could not read %s: %s\n", qPrintable(before), error.c_str());
        return 1;
    }
    std::unique_ptr<fla::FLADocument> b = load(after.toStdString(), error);
    if (!b)
    {
        std::fprintf(stderr, "Could not read %s: %s\n", qPrintable(after), error.c_str());
        return 1;
    }
    bool differs = false;
    const std::vector<fla::DocumentChange> changes = fla::compareDocuments(*a->document, *b->document, &error);
    for (const fla::DocumentChange& change : changes)
    {
        std::printf("%-8s %s", fla::changeName(change.kind), change.item.c_str());
        if (change.linesChanged > 0)
            std::printf(" (%d lines)", change.linesChanged);
        std::printf("\n");
    }
    differs = !changes.empty();

    // The frames, drawn: the same frames of each, at the first's size.
    MainWindow window(QString(), nullptr, false);
    std::vector<int> frames;
    QSize size;
    const std::vector<QImage> first = window.renderSamples(before, frames, size, samples);
    const std::vector<QImage> second = window.renderSamples(after, frames, size, samples);
    if (!out.isEmpty())
        QDir().mkpath(out);
    int framesChanged = 0;
    for (size_t i = 0; i < first.size() && i < second.size(); ++i)
    {
        const QImage& x = first[i];
        const QImage& y = second[i];
        QImage marked(x.size(), QImage::Format_ARGB32);
        qint64 changed = 0;
        for (int row = 0; row < x.height() && row < y.height(); ++row)
        {
            const QRgb* px = reinterpret_cast<const QRgb*>(x.constScanLine(row));
            const QRgb* py = reinterpret_cast<const QRgb*>(y.constScanLine(row));
            QRgb* pm = reinterpret_cast<QRgb*>(marked.scanLine(row));
            for (int col = 0; col < x.width() && col < y.width(); ++col)
            {
                const int d = std::max({std::abs(qRed(px[col]) - qRed(py[col])), std::abs(qGreen(px[col]) - qGreen(py[col])),
                    std::abs(qBlue(px[col]) - qBlue(py[col])), std::abs(qAlpha(px[col]) - qAlpha(py[col]))});
                const int gray = qGray(py[col]) / 2 + 128;
                pm[col] = d > 8 ? qRgb(255, 0, 0) : qRgb(gray, gray, gray);
                changed += d > 8;
            }
        }
        if (changed == 0)
            continue;
        ++framesChanged;
        std::printf("frame    %d: %lld pixels differ\n", frames[i] + 1, static_cast<long long>(changed));
        if (!out.isEmpty())
        {
            // Before, after and what differs, side by side.
            QImage strip(x.width() * 3, x.height(), QImage::Format_ARGB32);
            strip.fill(Qt::white);
            for (int row = 0; row < x.height(); ++row)
            {
                QRgb* line = reinterpret_cast<QRgb*>(strip.scanLine(row));
                std::copy_n(reinterpret_cast<const QRgb*>(x.constScanLine(row)), x.width(), line);
                if (row < y.height())
                    std::copy_n(reinterpret_cast<const QRgb*>(y.constScanLine(row)), std::min(x.width(), y.width()),
                        line + x.width());
                std::copy_n(reinterpret_cast<const QRgb*>(marked.constScanLine(row)), x.width(), line + 2 * x.width());
            }
            strip.save(QDir(out).filePath(QString("frame%1.png").arg(frames[i] + 1, 4, 10, QChar('0'))));
        }
    }
    differs = differs || framesChanged > 0;
    std::printf("%s: %zu parts changed, %d of %zu sampled frames draw differently\n",
        differs ? "different" : "same", changes.size(), framesChanged, frames.size());
    return differs ? 2 : 0;
}

int splitShots(const QStringList& args)
{
    if (args.isEmpty())
    {
        std::fprintf(stderr, "shots: which episode?\n");
        return 1;
    }
    const QString episode = args.at(0);
    const QString by = option(args, "--by").isEmpty() ? QString("label") : option(args, "--by");
    const fla::shots::CutBy cut = by == "scene" ? fla::shots::CutBy::Scene
        : by == "camera"                        ? fla::shots::CutBy::Camera
                                                : fla::shots::CutBy::Label;
    const QString format = option(args, "--format").isEmpty() ? QString("fla") : option(args, "--format").toLower();
    const QString out = option(args, "--out").isEmpty()
        ? QFileInfo(episode).absoluteDir().filePath(QFileInfo(episode).completeBaseName() + "_shots")
        : option(args, "--out");
    QDir().mkpath(out);

    std::string error;
    std::unique_ptr<fla::FLADocument> document = load(episode.toStdString(), error);
    if (!document)
    {
        std::fprintf(stderr, "Could not read %s: %s\n", qPrintable(episode), error.c_str());
        return 1;
    }
    const std::vector<fla::shots::Shot> shots = fla::shots::find(*document->document, cut);
    document.reset();
    int failed = 0;
    for (const fla::shots::Shot& shot : shots)
    {
        // Each shot from a fresh copy of the episode.
        std::unique_ptr<fla::FLADocument> copy = load(episode.toStdString(), error);
        if (!copy)
            return 1;
        fla::shots::keepOnly(*copy->document, shot);
        const QString name = QString::fromStdString(fla::shots::fileName(shot.name));
        const QString target = QDir(out).filePath(format == "xfl" ? name : name + "." + format);
        if (!save(*copy->document, target.toStdString(), error))
        {
            std::fprintf(stderr, "Could not write %s: %s\n", qPrintable(target), error.c_str());
            ++failed;
            continue;
        }
        std::printf("%s: frames %d-%d -> %s\n", shot.name.c_str(), shot.first + 1, shot.last + 1, qPrintable(target));
    }
    std::printf("%zu shots\n", shots.size());
    return failed ? 1 : 0;
}

int batch(const QStringList& args)
{
    if (args.isEmpty() || !QFileInfo(args.at(0)).isDir())
    {
        std::fprintf(stderr, "batch: which folder?\n");
        return 1;
    }
    const QStringList files = documentsIn(args.at(0), args.contains("--recursive"));
    const QString out = option(args, "--out");
    const bool saveBack = args.contains("--save");
    if (!out.isEmpty())
        QDir().mkpath(out);
    const auto outputFor = [&out](const QString& file, const QString& extension) {
        const QFileInfo info(file);
        const QDir folder = out.isEmpty() ? info.absoluteDir() : QDir(out);
        return folder.filePath(info.completeBaseName() + extension);
    };

    int failed = 0;
    std::unique_ptr<MainWindow> window;
    for (const QString& file : files)
    {
        std::printf("== %s\n", qPrintable(file));
        std::fflush(stdout);
        int result = 0;
        if (args.contains("--doctor"))
        {
            QStringList doctorArgs{file};
            if (saveBack || !out.isEmpty())
                doctorArgs << "--repair" << (saveBack ? file : outputFor(file, "." + QFileInfo(file).suffix()));
            result = doctor(doctorArgs);
        }
        else if (args.contains("--convert"))
        {
            const QString format = option(args, "--convert").toLower();
            const QString target = outputFor(file, format == "xfl" ? QString() : "." + format);
            std::string error;
            if (target == file || !convert(file.toStdString(), target.toStdString(), error))
            {
                std::fprintf(stderr, "%s\n", error.empty() ? "would overwrite itself" : error.c_str());
                result = 1;
            }
        }
        else if (args.contains("--render"))
        {
            if (!window)
                window = std::make_unique<MainWindow>(QString(), nullptr, false);
            const QString folder = out.isEmpty() ? QFileInfo(file).absoluteDir().filePath(QFileInfo(file).completeBaseName() + "_frames")
                                                 : QDir(out).filePath(QFileInfo(file).completeBaseName());
            result = window->renderCommand(file, {"--out", folder});
        }
        else if (args.contains("--python"))
        {
            // The document opened out as an XFL folder for the script, and
            // read back from it.
            QTemporaryDir temporary;
            const QString folder = QDir(temporary.path()).filePath(QFileInfo(file).completeBaseName());
            std::string error;
            if (!convert(file.toStdString(), folder.toStdString(), error))
            {
                std::fprintf(stderr, "%s\n", error.c_str());
                result = 1;
            }
            else
            {
                const QString python = qEnvironmentVariableIsSet("PHOENIX_PYTHON") ? qEnvironmentVariable("PHOENIX_PYTHON")
                                                                                   : QString("python");
                QProcess process;
                process.setProcessChannelMode(QProcess::ForwardedChannels);
                process.start(python, {option(args, "--python"), folder, file});
                if (!process.waitForStarted())
                {
                    std::fprintf(stderr, "Could not start %s\n", qPrintable(python));
                    return 1;
                }
                process.waitForFinished(-1);
                result = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : 1;
                if (result == 0 && (saveBack || !out.isEmpty()))
                {
                    const QString target = saveBack ? file : outputFor(file, "." + QFileInfo(file).suffix());
                    if (!convert(folder.toStdString(), target.toStdString(), error))
                    {
                        std::fprintf(stderr, "%s\n", error.c_str());
                        result = 1;
                    }
                }
            }
        }
        else
        {
            std::fprintf(stderr, "batch: --doctor, --convert, --render or --python?\n");
            return 1;
        }
        failed += result != 0;
    }
    std::printf("%lld documents, %d with problems\n", static_cast<long long>(files.size()), failed);
    return failed ? 2 : 0;
}

} // namespace

bool isCommand(const QString& name)
{
    return name == "render" || name == "doctor" || name == "diff" || name == "batch" || name == "shots" || name == "help" ||
           name == "--help";
}

std::unique_ptr<fla::FLADocument> load(const std::string& path, std::string& error)
{
    std::unique_ptr<fla::FLADocument> document;
    if (phx::isPhx(path))
    {
        std::string warning;
        bool readOnly = false;
        document.reset(phx::load(path, error, warning, readOnly));
        if (!warning.empty())
            std::fprintf(stderr, "%s", warning.c_str());
    }
    else
    {
        FLAParser parser;
        document.reset(parser.parse(path));
        error = parser.errorString();
    }
    if (document && !document->document)
        document.reset();
    if (!document && error.empty())
        error = "Could not read " + path;
    return document;
}

bool convert(const std::string& in, const std::string& out, std::string& error)
{
    std::unique_ptr<fla::FLADocument> document = load(in, error);
    if (!document)
        return false;
    return save(*document->document, out, error);
}

int run(const QStringList& arguments)
{
    const QString name = arguments.value(1);
    const QStringList args = arguments.mid(2);
    if (name == "render")
    {
        if (args.isEmpty())
        {
            std::fprintf(stderr, "render: which file?\n");
            return 1;
        }
        MainWindow window(QString(), nullptr, false);
        return window.renderCommand(args.at(0), args.mid(1));
    }
    if (name == "doctor")
        return doctor(args);
    if (name == "diff")
        return diff(args);
    if (name == "batch")
        return batch(args);
    if (name == "shots")
        return splitShots(args);
    std::fputs(kHelp, stdout);
    return 0;
}

} // namespace commands
