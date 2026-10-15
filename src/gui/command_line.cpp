#include "command_line.h"

#include "main_window.h"
#include "script_runner.h"
#include "shortcuts_dialog.h"
#include "updater.h"
#include "phoenix_view.h"
#include "player.h"
#include "../jsfl/behavior_frames.h"

#include "../data/action_script.h"
#include "../data/document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/timeline.h"
#include "../data/fla_document.h"
#include "../parser/fla_doctor.h"
#include "../parser/fla_parser.h"
#include "../phx/container.h"
#include "../phx/phx_document.h"
#include "../writer/document_diff.h"
#include "../edit/shot_breakdown.h"
#include "../edit/linked_assets.h"
#include "../data/symbol.h"
#include "../data/symbol_list.h"
#include "../writer/fla_writer.h"
#include "../edit/phoenix_features.h"
#include "tutorial_lesson.h"
#include <QApplication>
#include "tutorial_runner.h"
#include "../writer/xfl_content.h"
#include "../writer/xfl_folder_writer.h"
#include "../play/movie_runtime.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <numeric>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif
#include <cstdlib>

namespace commands {

namespace {

const char* const kHelp =
    "Phoenix commands, run with no window:\n"
    "\n"
    "  render <file> [--range 1-240] [--out frames/|movie.mp4|anim.gif|frame.svg|movie.swf]\n"
    "         [--size WxH] [--transparent] [--no-camera] [--no-audio] [--per-layer]\n"
    "         [--layers A,B] [--passes] [--exr] [--prores|--prores4444] [--everything]\n"
    "         [--scene NAME|--all-scenes|--each-scene] [--colors N] [--dither] [--link-images]\n"
    "         [--safe-zones]\n"
    "      Renders frames: a folder gets a numbered PNG sequence, an .svg name an\n"
    "      SVG sequence. --passes adds line, color and matte sequences; --exr\n"
    "      writes one multi-layer EXR a frame holding the picture and the passes.\n"
    "      --prores writes ProRes 422 HQ (.mov), --prores4444 keeps alpha.\n"
    "      --everything draws the pasteboard too. --colors and --dither are for GIFs.\n"
    "      --link-images writes an SVG's images once beside it, not inside it.\n"
    "\n"
    "  shortcuts [--defaults]\n"
    "      Lists every command and its key: the set in use, or Phoenix's defaults.\n"
    "\n"
    "  lessons [--check]\n"
    "      Lists the interactive tutorials. --check finds every control their\n"
    "      steps point at in the default workspace; exits 2 when one is missing.\n"
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
    "  batch --jsfl <script.jsfl> <files or folders> [--save] [--out <folder>]\n"
    "        [--recursive] [--timeout SECONDS]\n"
    "      Runs a JSFL script on each document (every .fla and .phx in a\n"
    "      folder), opened alone, as run-script does. --save writes what the\n"
    "      script changed back; --out writes a copy into a folder instead.\n"
    "\n"
    "  shots <episode> [--by scene|label|camera] [--out <folder>] [--format fla|phx|xfl]\n"
    "      Splits an episode into shot files: one per scene, per frame label, or\n"
    "      per camera cut. Tweens across a cut are baked so each shot plays as it\n"
    "      did; what a shot does not use is left out of its library.\n"
    "\n"
    "  links <file> [--update] [--save]\n"
    "      Lists the symbols linked to a master file; --update brings them up to\n"
    "      date from their masters (what they use comes too), --save writes the\n"
    "      file back. batch <folder> --update-links --save does every shot.\n"
    "\n"
    "  run-script <script.jsfl|script.py> [document ...] [--timeout SECONDS]\n"
    "      Opens the documents and runs a JSFL or Python script on them with no\n"
    "      window (Python scripts use your own Python 3.8 or later; set\n"
    "      PHOENIX_PYTHON to pick a python or virtual environment).\n"
    "      What it traces goes to standard output, its alerts too; confirm\n"
    "      answers yes, prompt its default, and an open-file dialog the first\n"
    "      document. Exits 1 when the script fails. The script stops after\n"
    "      --timeout seconds (600 unless given; 0 for no limit).\n"
    "\n"
    "  play <file> [--frames N] [--tree] [--click-all] [--image <out.png>]\n"
    "      Plays the movie as Control > Test Movie does, scripts and all, for N\n"
    "      frames (120 unless given), printing what its scripts trace and the\n"
    "      errors they stop on. --tree lists the clips playing then, with\n"
    "      their scripts. --click-all then clicks each button it finds,\n"
    "      one after another, playing a few frames after each. --image draws\n"
    "      the frame it is then on, as Test Movie shows it. Exits 1 when a\n"
    "      script stopped on an error.\n"
    "\n"
    "  benchmark <file> [--frames N] [--json <out.json>] [--against <animate.json>]\n"
    "      Times opening, drawing (first with nothing cached, then cached),\n"
    "      saving as FLA, PHX and XFL, and reopening the PHX, with peak memory.\n"
    "      --against compares with the times docs/benchmark.jsfl recorded.\n"
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
    // Kept, but only Phoenix reads them back.
    for (const fla::features::Use& use : fla::features::used(document))
        std::fprintf(stderr, "Phoenix feature kept in the file (other FLA editors show %s): %s\n",
            use.feature->elsewhere, use.feature->title);
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

int updateLinks(const QString& file, bool update, bool saveBack)
{
    std::string error;
    std::unique_ptr<fla::FLADocument> document = load(file.toStdString(), error);
    if (!document)
    {
        std::fprintf(stderr, "Could not read %s: %s\n", qPrintable(file), error.c_str());
        return 1;
    }
    fla::Document& target = *document->document;
    std::map<std::string, std::unique_ptr<fla::FLADocument>> masters;
    int failed = 0;
    int updated = 0;
    for (const std::string& name : fla::links::linkedSymbols(target))
    {
        fla::links::Link link;
        fla::links::linkOf(*target.symbolList->symbolMap[name], link);
        const std::string masterPath = fla::links::resolve(link.master, file.toStdString());
        std::printf("%s <- %s : %s\n", name.c_str(), masterPath.c_str(), link.item.c_str());
        if (!update)
            continue;
        std::unique_ptr<fla::FLADocument>& master = masters[masterPath];
        if (!master)
            master = load(masterPath, error);
        if (!master)
        {
            std::fprintf(stderr, "  could not read the master: %s\n", error.c_str());
            ++failed;
            continue;
        }
        const std::vector<std::string> changed = fla::links::update(target, name, *master->document, link.item, error);
        if (changed.empty())
        {
            std::fprintf(stderr, "  %s\n", error.c_str());
            ++failed;
            continue;
        }
        ++updated;
        std::printf("  updated %zu library items\n", changed.size());
    }
    if (update && saveBack && updated > 0 && !save(target, file.toStdString(), error))
    {
        std::fprintf(stderr, "Could not write %s: %s\n", qPrintable(file), error.c_str());
        return 1;
    }
    return failed ? 2 : 0;
}

/// batch --jsfl script.jsfl <files or folders>: the script run on each
/// document, every .fla and .phx in a folder.
int batchScript(const QStringList& args)
{
    const QString script = option(args, "--jsfl");
    if (!ScriptRunner::isScript(script) || !QFileInfo(script).isFile())
    {
        std::fprintf(stderr, "batch: --jsfl needs a .jsfl or .py script\n");
        return 1;
    }
    QStringList files;
    for (int i = 0; i < args.size(); ++i)
    {
        const QString& each = args.at(i);
        if (each == "--jsfl" || each == "--out" || each == "--timeout")
        {
            ++i;
            continue;
        }
        if (each.startsWith("--"))
            continue;
        if (QFileInfo(each).isDir())
            files << documentsIn(each, args.contains("--recursive"));
        else if (QFileInfo(each).isFile())
            files << each;
        else
        {
            std::fprintf(stderr, "batch: there is no %s\n", qPrintable(each));
            return 1;
        }
    }
    if (files.isEmpty())
    {
        std::fprintf(stderr, "batch: which documents?\n");
        return 1;
    }
    const QString out = option(args, "--out");
    if (!out.isEmpty())
        QDir().mkpath(out);
    const QString timeout = option(args, "--timeout");
    MainWindow window(QString(), nullptr, false);
    const int failed = window.scripts()->runBatch(script, files, args.contains("--save"), out,
        timeout.isEmpty() ? 600.0 : timeout.toDouble());
    std::printf("%lld documents, %d with problems\n", static_cast<long long>(files.size()), failed);
    return failed ? 2 : 0;
}

int batch(const QStringList& args)
{
    if (args.contains("--jsfl"))
        return batchScript(args);
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
        else if (args.contains("--update-links"))
        {
            result = updateLinks(file, true, saveBack);
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

double mean(const std::vector<double>& values)
{
    return values.empty() ? 0.0 : std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

double percentile(std::vector<double> values, double p)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, static_cast<size_t>(p * (values.size() - 1) + 0.5))];
}

double peakMemoryMb()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return counters.PeakWorkingSetSize / (1024.0 * 1024.0);
#endif
    return 0.0;
}

int benchmark(const QStringList& args)
{
    if (args.isEmpty())
    {
        std::fprintf(stderr, "benchmark: which file?\n");
        return 1;
    }
    const QString file = args.at(0);
    const int maxFrames = option(args, "--frames").isEmpty() ? 240 : std::max(1, option(args, "--frames").toInt());
    QJsonObject result;
    result["file"] = QFileInfo(file).fileName();
    result["bytes"] = static_cast<double>(QFileInfo(file).size());

    // Reading alone, as a command does.
    QElapsedTimer timer;
    timer.start();
    std::string error;
    std::unique_ptr<fla::FLADocument> document = load(file.toStdString(), error);
    result["parse_ms"] = timer.nsecsElapsed() / 1.0e6;
    if (!document)
    {
        std::fprintf(stderr, "Could not read %s: %s\n", qPrintable(file), error.c_str());
        return 1;
    }

    // Opening as the editor does, and drawing.
    double openMs = 0.0;
    std::vector<double> cold;
    std::vector<double> warm;
    QSize size;
    {
        MainWindow window(QString(), nullptr, false);
        if (!window.benchmarkRender(file, maxFrames, openMs, cold, warm, size))
        {
            std::fprintf(stderr, "Could not open %s\n", qPrintable(file));
            return 1;
        }
    }
    result["open_ms"] = openMs;
    result["frames"] = static_cast<int>(cold.size());
    result["width"] = size.width();
    result["height"] = size.height();
    result["render_cold_ms_per_frame"] = mean(cold);
    result["render_warm_ms_per_frame"] = mean(warm);
    result["render_warm_p95_ms"] = percentile(warm, 0.95);
    result["render_warm_fps"] = mean(warm) > 0.0 ? 1000.0 / mean(warm) : 0.0;

    // Saving each way, and reopening the PHX.
    QTemporaryDir scratch;
    for (const char* format : {"fla", "phx", "xfl"})
    {
        const std::string target = QDir(scratch.path()).filePath(QString("benchmark.") + format).toStdString();
        timer.restart();
        const bool saved = save(*document->document, target, error);
        result[QString("save_%1_ms").arg(format)] = saved ? timer.nsecsElapsed() / 1.0e6 : -1.0;
        if (saved && std::strcmp(format, "phx") == 0)
        {
            timer.restart();
            std::unique_ptr<fla::FLADocument> again = load(target, error);
            result["open_phx_ms"] = again ? timer.nsecsElapsed() / 1.0e6 : -1.0;
        }
    }
    result["peak_memory_mb"] = peakMemoryMb();

    const auto number = [&result](const char* key) { return result.value(key).toDouble(); };
    std::printf("%s: %d frames at %dx%d\n", qPrintable(result["file"].toString()), result["frames"].toInt(), size.width(),
        size.height());
    std::printf("  parse            %9.1f ms\n", number("parse_ms"));
    std::printf("  open             %9.1f ms\n", number("open_ms"));
    std::printf("  draw, uncached   %9.2f ms a frame\n", number("render_cold_ms_per_frame"));
    std::printf("  draw, cached     %9.2f ms a frame (95%% under %.2f ms, %.0f fps)\n", number("render_warm_ms_per_frame"),
        number("render_warm_p95_ms"), number("render_warm_fps"));
    std::printf("  save FLA         %9.1f ms\n", number("save_fla_ms"));
    std::printf("  save PHX         %9.1f ms\n", number("save_phx_ms"));
    std::printf("  save XFL         %9.1f ms\n", number("save_xfl_ms"));
    std::printf("  reopen PHX       %9.1f ms\n", number("open_phx_ms"));
    std::printf("  peak memory      %9.1f MB\n", number("peak_memory_mb"));

    // Side by side with the times benchmark.jsfl recorded for the same file.
    const QString against = option(args, "--against");
    if (!against.isEmpty())
    {
        QFile in(against);
        const QJsonObject animate = in.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(in.readAll()).object() : QJsonObject();
        if (animate.isEmpty())
            std::fprintf(stderr, "Could not read %s\n", qPrintable(against));
        else
        {
            std::printf("\n  against JSFL      %10s %10s %8s\n", "JSFL", "Phoenix", "faster");
            const struct
            {
                const char* label;
                const char* theirs;
                const char* ours;
            } rows[] = {
                {"open", "open_ms", "open_ms"},
                {"draw a frame", "render_ms_per_frame", "render_cold_ms_per_frame"},
                {"save FLA", "save_fla_ms", "save_fla_ms"},
            };
            for (const auto& row : rows)
            {
                const double theirs = animate.value(row.theirs).toDouble(-1.0);
                const double ours = number(row.ours);
                if (theirs <= 0.0 || ours <= 0.0)
                    continue;
                std::printf("  %-16s %10.1f %10.1f %7.1fx\n", row.label, theirs, ours, theirs / ours);
            }
        }
    }
    const QString json = option(args, "--json");
    if (!json.isEmpty())
    {
        QFile out(json);
        if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(result).toJson()) < 0)
        {
            std::fprintf(stderr, "Could not write %s\n", qPrintable(json));
            return 1;
        }
    }
    return 0;
}

/// Plays a movie with no window, as Test Movie does, for checking a game's
/// scripts: what they trace and the errors they stop on.
int play(const QStringList& args)
{
    if (args.isEmpty())
    {
        std::fprintf(stderr, "play: which file?\n");
        return 1;
    }
    std::string error;
    std::unique_ptr<fla::FLADocument> document = load(args.at(0).toStdString(), error);
    if (!document || !document->document || !document->document->currentScene())
    {
        std::fprintf(stderr, "%s\n", error.empty() ? "Could not open it." : error.c_str());
        return 1;
    }
    const int frames = option(args, "--frames").isEmpty() ? 120 : std::max(0, option(args, "--frames").toInt());
    const std::vector<const fla::Timeline*> scenes(document->document->timelines.begin(), document->document->timelines.end());
    fla::play::MovieRuntime movie(scenes, document->document->frameRate > 0.0 ? document->document->frameRate : 24.0);
    int errors = 0;
    movie.trace = [](const std::string& text) { std::printf("%s\n", text.c_str()); };
    movie.error = [&errors](const std::string& text) {
        ++errors;
        std::printf("ERROR %s\n", text.c_str());
    };
    movie.getURL = [](const std::string& url, const std::string& window) {
        std::printf("getURL %s %s\n", url.c_str(), window.c_str());
    };
    movie.playSound = [](const std::string& linkage, double, int loops, double) {
        std::printf("sound %s x%d\n", linkage.c_str(), loops);
        return -1;
    };
    // Clips scripts make from the library, by linkage.
    std::vector<const fla::Symbol*> symbols;
    if (document->document->symbolList)
        symbols.assign(document->document->symbolList->symbols.begin(), document->document->symbolList->symbols.end());
    movie.setLibrary(symbols, {}, {});
    // ActionScript 3 class files beside the document.
    std::string documentClass;
    const auto classes = fla::play::classFiles(*document->document,
        QFileInfo(args.at(0)).absolutePath().toStdString(), documentClass);
    movie.setClasses(classes, documentClass);
    movie.start();
    for (int i = 0; i < frames; ++i)
        movie.step();
    std::printf("after %d frames: scene %d of %d, frame %d, %s\n", frames, movie.scene() + 1, movie.sceneCount(),
        movie.root().frame + 1, movie.root().playing ? "playing" : "stopped");
    if (args.contains("--tree"))
    {
        // Every clip there is now, with what scripts it has.
        std::function<void(const fla::play::Clip&, int)> show = [&](const fla::play::Clip& clip, int depth) {
            int frameScripts = 0;
            if (clip.timeline)
            {
                for (const fla::Layer* layer : clip.timeline->layers)
                {
                    for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>{})
                        frameScripts += frame->actionScript && !frame->actionScript->code.empty() ? 1 : 0;
                }
            }
            const bool own = clip.placed && !fla::play::instanceScript(*clip.placed).empty();
            std::printf("%*s%s%s%s frame %d/%d %s%s%s\n", depth * 2, "", clip.key.empty() ? "root" : clip.key.c_str(),
                clip.name.empty() ? "" : (" \"" + clip.name + "\"").c_str(), clip.button ? " [button]" : "",
                clip.frame + 1, clip.length, clip.playing ? "playing" : "stopped",
                frameScripts ? (", " + std::to_string(frameScripts) + " frame scripts").c_str() : "",
                own ? ", own script" : "");
            for (const auto& child : clip.children)
                show(*child, depth + 1);
        };
        show(movie.root(), 0);
    }
    if (args.contains("--click-all"))
    {
        // The buttons there are now, by the keys that lead to them; clicking
        // may make or drop others, so each is looked for again before it is
        // clicked.
        std::vector<std::vector<std::string>> paths;
        std::vector<std::string> path;
        std::function<void(const fla::play::Clip&)> collect = [&](const fla::play::Clip& clip) {
            for (const auto& child : clip.children)
            {
                path.push_back(child->key);
                if (child->button)
                    paths.push_back(path);
                collect(*child);
                path.pop_back();
            }
        };
        collect(movie.root());
        std::printf("%d buttons\n", static_cast<int>(paths.size()));
        for (const std::vector<std::string>& keys : paths)
        {
            fla::play::Clip* clip = &movie.root();
            std::string shown;
            for (const std::string& key : keys)
            {
                clip = clip ? clip->child(key) : nullptr;
                shown += "/" + key;
            }
            if (!clip)
            {
                std::printf("click %s: gone\n", shown.c_str());
                continue;
            }
            std::printf("click %s\n", shown.c_str());
            movie.pointerOver(clip);
            movie.pointerDown();
            movie.pointerUp();
            movie.pointerOver(nullptr);
            for (int i = 0; i < 4; ++i)
                movie.step();
        }
        std::printf("after clicking: root on frame %d, %s\n", movie.root().frame + 1,
            movie.root().playing ? "playing" : "stopped");
    }
    // --image: the frame the movie is on, drawn as Test Movie draws it, for
    // comparing with what exports draw.
    const QString imagePath = option(args, "--image");
    if (!imagePath.isEmpty())
    {
        Player player;
        PhoenixView view(&player);
        view.setDocument(document.get());
        view.setShowPasteboard(false);
        view.setClipOutsideStage(true);
        view.setMovie(&movie);
        // Behaviors that follow layer parents (Jiggle) see them through
        // this view, as they do through the window's.
        fla::behaviors::setRigging([&view](const fla::Layer& layer, int frame, double matrix[6]) {
            const fla::Frame* key = nullptr;
            for (const fla::Frame* each : layer.frames)
                if (each && each->index <= frame && frame < each->index + std::max(1, each->duration))
                    key = each;
            if (!key)
                return false;
            const QTransform placed = view.parentingAt(&layer, key, frame);
            const double values[6] = {placed.m11(), placed.m12(), placed.m21(), placed.m22(), placed.dx(), placed.dy()};
            std::copy(values, values + 6, matrix);
            return true;
        });
        player.setCurrentFrame(movie.root().frame);
        const QSize size(std::max(1, static_cast<int>(std::lround(document->document->width))),
            std::max(1, static_cast<int>(std::lround(document->document->height))));
        const QImage image = view.renderFrame(movie.root().frame, size, false);
        view.setMovie(nullptr);
        fla::behaviors::setRigging(nullptr);
        if (image.isNull() || !image.save(imagePath))
        {
            std::fprintf(stderr, "play: %s could not be written\n", qPrintable(imagePath));
            return 1;
        }
        std::printf("frame %d drawn to %s\n", movie.root().frame + 1, qPrintable(imagePath));
    }
    std::printf("%d errors\n", errors);
    return errors > 0 ? 1 : 0;
}

} // namespace

bool isCommand(const QString& name)
{
    return name == "play" || name == "benchmark" || name == "render" || name == "run-script" || name == "doctor" || name == "diff" || name == "batch" || name == "shots" || name == "links" || name == "shortcuts" || name == "lessons" || name == "help" ||
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
        if (document && !parser.warningString().empty())
            std::fprintf(stderr, "%s", parser.warningString().c_str());
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
    if (name == "lessons")
    {
        QStringList unreadable;
        const std::vector<TutorialLesson> lessons = tutorials::allLessons(&unreadable);
        for (const TutorialLesson& lesson : lessons)
            std::printf("%s\t%s\t%d steps\n", lesson.id.toUtf8().constData(), lesson.title.toUtf8().constData(),
                static_cast<int>(lesson.steps.size()));
        for (const QString& problem : unreadable)
            std::fprintf(stderr, "%s\n", problem.toUtf8().constData());
        if (!args.contains("--check"))
            return unreadable.isEmpty() ? 0 : 2;
        MainWindow window(QString(), nullptr, false);
        // Shown (offscreen, for a check), so its panels are laid out as a
        // reader sees them.
        window.resize(1600, 1000);
        window.show();
        window.newLessonDocument(1280, 720, 24.0);
        QApplication::processEvents();
        const QStringList problems = tutorials::checkLessons(window);
        for (const QString& problem : problems)
            std::fprintf(stderr, "%s\n", problem.toUtf8().constData());
        std::printf("%d lessons, %d problems\n", static_cast<int>(lessons.size()), static_cast<int>(problems.size()));
        return problems.isEmpty() ? 0 : 2;
    }
    if (name == "shortcuts")
    {
        // Every command and its key: the set in use, or Phoenix's defaults.
        MainWindow window(QString(), nullptr, false);
        const std::map<QString, QKeySequence> current = Shortcuts::currentSet();
        for (const ShortcutEntry& entry : window.shortcutEntries())
        {
            const auto chosen = current.find(entry.id);
            const QKeySequence key =
                args.contains("--defaults") || chosen == current.end() ? entry.defaultKey : chosen->second;
            std::printf("%s\t%s\n", entry.id.toUtf8().constData(), key.toString(QKeySequence::PortableText).toUtf8().constData());
        }
        return 0;
    }
    if (name == "run-script")
    {
        if (args.isEmpty() || !ScriptRunner::isScript(args.at(0)))
        {
            std::fprintf(stderr, "run-script: which .jsfl or .py script?\n");
            return 1;
        }
        QStringList documents;
        for (int i = 1; i < args.size(); ++i)
        {
            if (args.at(i) == "--timeout")
                ++i;
            else
                documents << args.at(i);
        }
        const QString timeout = option(args, "--timeout");
        MainWindow window(QString(), nullptr, false);
        return window.scripts()->runHeadless(args.at(0), documents, timeout.isEmpty() ? 600.0 : timeout.toDouble());
    }
    if (name == "doctor")
        return doctor(args);
    if (name == "benchmark")
        return benchmark(args);
    if (name == "play")
        return play(args);
    if (name == "diff")
        return diff(args);
    if (name == "batch")
        return batch(args);
    if (name == "shots")
        return splitShots(args);
    if (name == "links")
    {
        if (args.isEmpty())
        {
            std::fprintf(stderr, "links: which file?\n");
            return 1;
        }
        return updateLinks(args.at(0), args.contains("--update"), args.contains("--save"));
    }
    std::fputs(kHelp, stdout);
    return 0;
}

} // namespace commands
