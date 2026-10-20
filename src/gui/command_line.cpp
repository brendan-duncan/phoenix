#include "command_line.h"
#include "display_check.h"
#include "display_painter.h"
#include "movie_hooks.h"

#include "main_window.h"
#include "script_runner.h"
#include "shortcuts_dialog.h"
#include "updater.h"
#include "phoenix_view.h"
#include "player.h"
#include "publish_dialog.h"
#include "brush_library_dialog.h"
#include "brush_library_store.h"
#include "document_brushes_dialog.h"
#include "stroke_style_dialog.h"
#include "../edit/paint_brushes.h"
#include "theme.h"
#include "toolbox.h"
#include "../jsfl/behavior_frames.h"
#include "../jsfl/native_extensions.h"

#include "../data/action_script.h"
#include "../data/bitmap.h"
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
#include "capture_steps.h"
#include "tutorial_lesson.h"
#include <QAbstractButton>
#include <QApplication>
#include "tutorial_runner.h"
#include "../writer/xfl_content.h"
#include "../writer/xfl_folder_writer.h"
#include "../play/movie_runtime.h"
#include "../play/display/stage.h"

#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QMenuBar>
#include <QMouseEvent>
#include <QTimer>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QToolButton>
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
    "  render <file> [--range 1-240] [--out frames/|movie.mp4|anim.gif|frame.svg|movie.swf|page.html]\n"
    "         [--size WxH] [--transparent] [--no-camera] [--no-audio] [--per-layer]\n"
    "         [--layers A,B] [--passes] [--exr] [--prores|--prores4444] [--everything]\n"
    "         [--scene NAME|--all-scenes|--each-scene] [--colors N] [--dither] [--link-images]\n"
    "         [--safe-zones] [--out anim.webp|anim.apng|movie.webm] [--quality 1-100]\n"
    "         [--fps N] [--fit]\n"
    "      Renders frames: a folder gets a numbered PNG sequence, an .svg name an\n"
    "      SVG sequence. --passes adds line, color and matte sequences; --exr\n"
    "      writes one multi-layer EXR a frame holding the picture and the passes.\n"
    "      --prores writes ProRes 422 HQ (.mov), --prores4444 keeps alpha.\n"
    "      --everything draws the pasteboard too. --colors and --dither are for GIFs.\n"
    "      .webp, .apng and .webm write an animated WebP, an animated PNG and a\n"
    "      WebM (VP9) video, as File > Share does; --quality sets their quality.\n"
    "      --fps writes at another frame rate, the movie lasting as long; --fit\n"
    "      keeps the stage's shape inside --size, with bars round it.\n"
    "      --link-images writes an SVG's images once beside it, not inside it.\n"
    "      An .html name publishes HTML5: a page that plays the movie in a browser,\n"
    "      its runtime, content, pictures and sounds beside it.\n"
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
    "  batch <folder> --doctor | --convert fla|phx|xfl | --render\n"
    "        [--save] [--recursive] [--out <folder>]\n"
    "      Runs one of those on every .fla and .phx in the folder.\n"
    "\n"
    "  batch --jsfl <script.jsfl> | --python <script.py> <files or folders>\n"
    "        [--save] [--out <folder>] [--recursive] [--timeout SECONDS]\n"
    "      Runs a JSFL or Python script on each document (every .fla and .phx\n"
    "      in a folder), opened alone, as run-script does. --save writes what\n"
    "      the script changed back; --out writes a copy into a folder instead.\n"
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
    "  capture-dialog <dialog> <file> --out <png> [--theme <name>]\n"
    "      Draws a dialog as it opens for the file, with no window, for the\n"
    "      guide's pictures; in the Dark theme unless another is named. The\n"
    "      dialogs: publish-settings; stroke-style [--brush <name>] [--kind\n"
    "      <solid|hairline|dashed|dotted|ragged|stipple|hatched|paint-brush>]\n"
    "      (Custom Stroke Style, for a stroke drawn with the file's brush);\n"
    "      brush-library [--category <name>] (the file's brushes added to a\n"
    "      library kept only for the picture); document-brushes; toolbox\n"
    "      [--columns N] [--height H] [--orientation horizontal] [--width W]\n"
    "      [--flyout <tool>] (the Tools panel, taking more columns or rows\n"
    "      where it is too short or narrow, a slot's flyout beside it).\n"
    "\n"
    "  capture-window <file|-> --out <png> [--script <jsfl>] [--size WxH]\n"
    "                 [--show <panel>] [--panel <name>] [--menu <path>]\n"
    "                 [--action <path> [--press <button>]] [--crop x,y,w,h]\n"
    "                 [--dock-width N] [--trigger <path>] [--drag x1,y1,x2,y2[,shift|alt]]\n"
    "                 [--steps <file>]\n"
    "                 [--theme <name>]\n"
    "      Draws the window with the file open, unseen, with default\n"
    "      settings, after the script has set it up (selected something,\n"
    "      gone to a frame), for the guide's pictures. --show opens a panel\n"
    "      by its object name; --panel draws one panel or widget rather than\n"
    "      the window, --menu one menu (\"Modify>Bitmap\"), --action the dialog\n"
    "      a menu item opens (--press clicks one of its buttons first, by its\n"
    "      text). --trigger runs a menu item (\"View>Onion Skin\") and --drag\n"
    "      drags on the stage, in its own pixels, before the picture is\n"
    "      taken. --steps then plays clicks, drags, keys, menu commands and\n"
    "      dialog answers from a file, as a lesson's steps (see capture_steps.h),\n"
    "      drawing pictures along the way. Nothing is saved.\n"
    "\n"
    "  run-script <script.jsfl|script.py> [document ...] [--timeout SECONDS]\n"
    "             [--no-native-extensions]\n"
    "      Opens the documents and runs a JSFL or Python script on them with no\n"
    "      window (Python scripts use your own Python 3.8 or later; set\n"
    "      PHOENIX_PYTHON to pick a python or virtual environment).\n"
    "      What it traces goes to standard output, its alerts too; confirm\n"
    "      answers yes, prompt its default, and an open-file dialog the first\n"
    "      document. Exits 1 when the script fails. The script stops after\n"
    "      --timeout seconds (600 unless given; 0 for no limit). Native\n"
    "      extension libraries in External Libraries load as in the window;\n"
    "      --no-native-extensions (or PHOENIX_NATIVE_EXTENSIONS=off) leaves\n"
    "      them out, here and in batch.\n"
    "\n"
    "  play <file> [--frames N] [--tree] [--click-all] [--image <out.png>]\n"
    "       [--click F:X,Y] [--drag F:X,Y:X,Y] [--move F:X,Y] [--key F:CODE]\n"
    "      Plays the movie as Control > Test Movie does, scripts and all (an\n"
    "      HTML5 Canvas document's JavaScript too), for N frames (120 unless\n"
    "      given), printing what its scripts trace or log and the errors they\n"
    "      stop on. --tree lists the clips playing then, with\n"
    "      their scripts. --click-all then clicks each button it finds,\n"
    "      one after another, playing a few frames after each. --image draws\n"
    "      the frame it is then on, as Test Movie shows it. --click, --drag,\n"
    "      --move and --key (each may be given more than once) send the\n"
    "      pointer or a key (its code, as Keyboard numbers them) to the stage\n"
    "      before frame F plays: a click at X,Y, a press at one point, moves\n"
    "      and a release at the other, or a move. Exits 1 when a script\n"
    "      stopped on an error.\n"
    "\n"
    "  display-check <folder> [--fla <file> [--frames N]]\n"
    "      Draws display lists built in code (shapes, gradients, bitmaps,\n"
    "      blend modes, filters, masks, text, a paint program's canvas) as\n"
    "      Test Movie draws them, to PNGs in the folder; with --fla, the\n"
    "      movie after N frames with display objects put among its own.\n"
    "\n"
    "  benchmark <file> [--frames N] [--json <out.json>] [--against <animate.json>]\n"
    "      Times opening, drawing (first with nothing cached, then cached),\n"
    "      saving as FLA, PHX and XFL, and reopening the PHX, with peak memory.\n"
    "      --against compares with the times website/benchmark.jsfl recorded.\n"
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

/// batch --jsfl script.jsfl <files or folders> (or --python script.py): the
/// script run on each document, every .fla and .phx in a folder, in this
/// process.
int batchScript(const QStringList& args, const QString& flag)
{
    const QString script = option(args, flag);
    if (!ScriptRunner::isScript(script) || !QFileInfo(script).isFile())
    {
        std::fprintf(stderr, "batch: %s needs a .jsfl or .py script\n", qPrintable(flag));
        return 1;
    }
    QStringList files;
    for (int i = 0; i < args.size(); ++i)
    {
        const QString& each = args.at(i);
        if (each == flag || each == "--out" || each == "--timeout")
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
        return batchScript(args, "--jsfl");
    if (args.contains("--python"))
        return batchScript(args, "--python");
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
    // Clips scripts make from the library, by linkage, and bitmaps.
    std::vector<const fla::Symbol*> symbols;
    if (document->document->symbolList)
        symbols.assign(document->document->symbolList->symbols.begin(), document->document->symbolList->symbols.end());
    movie.setLibrary(symbols, {}, {});
    std::vector<const fla::Bitmap*> bitmaps;
    for (const fla::Resource* resource : document->document->resources)
        if (resource && resource->resourceType() == fla::Resource::Type::Bitmap)
            bitmaps.push_back(static_cast<const fla::Bitmap*>(resource));
    movie.setLibraryBitmaps(bitmaps);
    // Its classes, images, the web and SharedObjects, as Test Movie has them.
    QObject network;
    connectMovie(movie, *document, QFileInfo(args.at(0)).absoluteFilePath(), &network,
        [](const QString& text) { std::printf("%s\n", text.toUtf8().constData()); });
    // What the timelines place is measured and drawn (BitmapData.draw) by a
    // view of the document, with no window as with one.
    Player player;
    PhoenixView view(&player);
    view.setDocument(document.get());
    view.setShowPasteboard(false);
    view.setClipOutsideStage(true);
    view.setMovie(&movie);

    // The pointer and keys sent before a frame: --click F:X,Y and the rest.
    struct Input
    {
        int frame = 0;
        QString kind;
        double x = 0.0, y = 0.0, toX = 0.0, toY = 0.0;
        int key = 0;
    };
    std::vector<Input> inputs;
    for (int i = 1; i + 1 < args.size(); ++i)
    {
        const QString& kind = args.at(i);
        if (kind != "--click" && kind != "--drag" && kind != "--move" && kind != "--key")
            continue;
        const QStringList parts = args.at(i + 1).split(':');
        Input input;
        input.kind = kind.mid(2);
        input.frame = parts.value(0).toInt();
        const QStringList at = parts.value(1).split(',');
        if (kind == "--key")
            input.key = parts.value(1).toInt();
        else
        {
            input.x = at.value(0).toDouble();
            input.y = at.value(1).toDouble();
            const QStringList to = parts.value(2).split(',');
            input.toX = to.value(0).toDouble();
            input.toY = to.value(1).toDouble();
        }
        inputs.push_back(input);
    }
    // Buttons are found under the pointer as the window finds them: by the
    // art last drawn, so the frame is drawn before each look.
    const QSize stageSize(std::max(1, static_cast<int>(std::lround(document->document->width))),
        std::max(1, static_cast<int>(std::lround(document->document->height))));
    const auto buttonAt = [&](double x, double y) {
        player.setCurrentFrame(movie.root().frame);
        view.renderFrame(movie.root().frame, stageSize, false);
        return view.movieButtonAtStage(QPointF(x, y));
    };
    const auto send = [&](const Input& input) {
        fla::display::Stage& stage = movie.stage();
        if (input.kind == "key")
        {
            const int character = input.key >= 32 && input.key < 127 ? input.key : 0;
            movie.keyDown(input.key, character);
            movie.keyUp(input.key, character);
            return;
        }
        movie.pointerAt(input.x, input.y);
        movie.pointerOver(buttonAt(input.x, input.y));
        stage.pointerMove(input.x, input.y);
        if (input.kind == "click" || input.kind == "drag")
        {
            movie.pointerDown();
            stage.pointerDown(input.x, input.y);
            movie.afterEvents();
            if (input.kind == "drag")
                for (int step = 1; step <= 8; ++step)
                {
                    const double x = input.x + (input.toX - input.x) * step / 8.0;
                    const double y = input.y + (input.toY - input.y) * step / 8.0;
                    movie.pointerAt(x, y);
                    movie.pointerOver(buttonAt(x, y));
                    stage.pointerMove(x, y);
                    movie.afterEvents();
                }
            const double x = input.kind == "drag" ? input.toX : input.x;
            const double y = input.kind == "drag" ? input.toY : input.y;
            movie.pointerOver(buttonAt(x, y));
            movie.pointerUp();
            stage.pointerUp(x, y);
        }
        movie.afterEvents();
    };

    movie.start();
    for (int i = 0; i < frames; ++i)
    {
        for (const Input& input : inputs)
            if (input.frame == i)
                send(input);
        movie.step();
        // What the web has sent comes in between frames.
        QCoreApplication::processEvents();
    }
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
    if (args.contains("--display-list"))
    {
        // The display list as scripts left it: each object's kind, name,
        // and box on the stage.
        static const char* kinds[] = {"Shape", "Sprite", "MovieClip", "SimpleButton", "Bitmap", "TextField", "Stage",
            "TimelineArt"};
        std::function<void(const fla::display::DisplayObject&, int)> show = [&](const fla::display::DisplayObject& object,
                                                                               int depth) {
            const fla::display::Rect box = object.getBounds(&movie.stage());
            std::printf("%*s%s \"%s\" %.0f,%.0f %.0fx%.0f%s\n", depth * 2, "", kinds[static_cast<int>(object.kind())],
                object.name().c_str(), box.x, box.y, box.width, box.height, object.visible() ? "" : " hidden");
            if (object.isContainer())
                for (const auto& child : static_cast<const fla::display::DisplayObjectContainer&>(object).children())
                    if (child)
                        show(*child, depth + 1);
        };
        show(movie.stage(), 0);
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
            // ActionScript 3's and JavaScript's listeners hear it from the stage,
            // at the middle of the button.
            if ((movie.actionScript3() || movie.javaScript()) && clip->display && clip->display->stage())
            {
                const fla::display::Rect box = clip->display->getBounds(&movie.stage());
                const double x = box.x + box.width / 2.0;
                const double y = box.y + box.height / 2.0;
                movie.stage().pointerMove(x, y);
                movie.stage().pointerDown(x, y);
                movie.stage().pointerUp(x, y);
                movie.afterEvents();
            }
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

/// capture-dialog toolbox: the Tools panel as Phoenix builds it, in the
/// default layout, --columns wide (down a side) or along the top
/// (--orientation horizontal), --height tall or --width wide; too short or
/// narrow, it takes more columns or rows. --flyout <tool> opens that
/// tool's slot beside it.
int captureToolbox(const QStringList& args, const QString& out)
{
    const QString theme = option(args, "--theme");
    MainWindow window(QString(), nullptr, false);
    theme::preview(theme::load(theme.isEmpty() ? QString("Dark") : theme));
    Toolbox* toolbox = window.toolbox();
    // Quiet, so the layout saved for the user's own panel stays as it is,
    // and out of its dock, which would size it to itself; it goes back
    // after.
    const QSignalBlocker quiet(toolbox);
    QWidget* const dock = toolbox->parentWidget();
    toolbox->setParent(nullptr);
    const std::unique_ptr<QObject, std::function<void(QObject*)>> back(toolbox,
        [dock](QObject* object) { static_cast<QWidget*>(object)->setParent(dock); });
    Toolbox::Layout layout;
    layout.columns = std::clamp(option(args, "--columns").toInt(), 1, 8);
    toolbox->setLayout(layout);
    const bool horizontal = option(args, "--orientation") == "horizontal";
    toolbox->setOrientation(horizontal ? Qt::Horizontal : Qt::Vertical);
    const QSize hint = toolbox->sizeHint();
    QSize size;
    int across = 1;
    if (horizontal)
    {
        const int width = option(args, "--width").toInt() > 0 ? option(args, "--width").toInt() : hint.width();
        across = toolbox->acrossFor(width);
        size = QSize(width, toolbox->widthFor(across));
    }
    else
    {
        const int height = option(args, "--height").toInt() > 0 ? option(args, "--height").toInt() : hint.height();
        across = toolbox->acrossFor(height);
        size = QSize(toolbox->widthFor(across), height);
    }
    // Never shown, so it is told of its size itself, to lay out for it.
    const QSize before = toolbox->size();
    toolbox->resize(size);
    QResizeEvent resized(size, before);
    QApplication::sendEvent(toolbox, &resized);
    QPixmap picture = toolbox->grab();
    const QString flyoutTool = option(args, "--flyout");
    if (!flyoutTool.isEmpty())
    {
        // The slot's flyout beside its button, as a long press opens it.
        QToolButton* button = toolbox->buttonFor(flyoutTool);
        QMenu* flyout = button ? button->menu() : nullptr;
        if (!flyout)
        {
            std::fprintf(stderr, "capture-dialog: %s has no flyout\n", qPrintable(flyoutTool));
            return 1;
        }
        flyout->ensurePolished();
        flyout->adjustSize();
        const QPixmap menu = flyout->grab();
        const QPoint at(button->geometry().right() + 8, button->geometry().top());
        const double ratio = picture.devicePixelRatio();
        const QSize whole(std::max(size.width(), at.x() + menu.deviceIndependentSize().toSize().width() + 8), size.height());
        QPixmap composed(whole * ratio);
        composed.setDevicePixelRatio(ratio);
        composed.fill(theme::color("window"));
        QPainter painter(&composed);
        painter.drawPixmap(0, 0, picture);
        painter.drawPixmap(at, menu);
        // Its edge, which the window around a shown menu would draw.
        painter.setPen(theme::color("swatchBorder"));
        painter.drawRect(QRect(at, menu.deviceIndependentSize().toSize()).adjusted(0, 0, -1, -1));
        painter.end();
        picture = composed;
    }
    if (!picture.save(out))
    {
        std::fprintf(stderr, "Could not write %s\n", qPrintable(out));
        return 1;
    }
    std::printf("Drew the Tools panel, %d %s, %dx%d, to %s\n", across,
        horizontal ? (across == 1 ? "row" : "rows") : (across == 1 ? "column" : "columns"), size.width(),
        size.height(), qPrintable(out));
    return 0;
}

/// capture-dialog <dialog> <file> --out <png> [--theme <name>]: a dialog
/// drawn as it opens for \a file, with no window, for the guide's pictures
/// (Dark unless another theme is named): publish-settings, stroke-style,
/// brush-library, document-brushes or toolbox.
int captureDialog(const QStringList& args)
{
    const QStringList dialogs{"publish-settings", "stroke-style", "brush-library", "document-brushes", "toolbox"};
    if (args.size() < 2 || !dialogs.contains(args.at(0)))
    {
        std::fprintf(stderr, "capture-dialog: %s <file> --out <png> [--theme <name>]\n",
            qPrintable(dialogs.join('|')));
        return 1;
    }
    const int outAt = args.indexOf("--out");
    const int themeAt = args.indexOf("--theme");
    const QString out = outAt >= 0 && outAt + 1 < args.size() ? args.at(outAt + 1) : QString();
    if (out.isEmpty())
    {
        std::fprintf(stderr, "capture-dialog: where to? (--out <png>)\n");
        return 1;
    }
    std::string error;
    const std::unique_ptr<fla::FLADocument> file = load(args.at(1).toStdString(), error);
    if (!file)
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    theme::preview(theme::load(themeAt >= 0 && themeAt + 1 < args.size() ? args.at(themeAt + 1) : QString("Dark")));
    const auto written = [&](QWidget& dialog, const char* name) {
        if (!dialog.grab().save(out))
        {
            std::fprintf(stderr, "Could not write %s\n", qPrintable(out));
            return 1;
        }
        std::printf("Drew %s for %s to %s\n", name, qPrintable(args.at(1)), qPrintable(out));
        return 0;
    };
    fla::Document& document = *file->document;
    const auto brushes = fla::paintBrushesOf(document);
    if (args.at(0) == "stroke-style")
    {
        // A stroke drawn with the file's brush (the one named, or its
        // first), or of the kind named.
        fla::StrokeLook look;
        const QString brushName = option(args, "--brush");
        if (brushes)
            for (const fla::PaintBrushDefinition& definition : brushes->definitions)
                if (brushName.isEmpty() || QString::fromStdString(definition.name) == brushName)
                {
                    look.kind = fla::StrokeStyle::Style::PaintBrush;
                    look.paintBrush = definition.index;
                    look.paintBrushGuid = definition.guid();
                    break;
                }
        // Else one of Phoenix's own, from the brush library.
        if (look.kind != fla::StrokeStyle::Style::PaintBrush && !brushName.isEmpty())
            for (const fla::BrushLibraryEntry& entry : fla::phoenixBrushes())
                if (QString::fromStdString(entry.definition.name) == brushName)
                {
                    look.kind = fla::StrokeStyle::Style::PaintBrush;
                    look.paintBrushGuid = entry.definition.guid();
                }
        const QString kind = option(args, "--kind");
        const std::map<QString, fla::StrokeStyle::Style> kinds{{"solid", fla::StrokeStyle::Style::Solid},
            {"hairline", fla::StrokeStyle::Style::Solid}, {"dashed", fla::StrokeStyle::Style::Dashed},
            {"dotted", fla::StrokeStyle::Style::Dotted}, {"ragged", fla::StrokeStyle::Style::Ragged},
            {"stipple", fla::StrokeStyle::Style::Stipple}, {"hatched", fla::StrokeStyle::Style::Hatched},
            {"paint-brush", fla::StrokeStyle::Style::PaintBrush}};
        if (kinds.count(kind))
        {
            look.kind = kinds.at(kind);
            look.hairline = kind == "hairline";
        }
        StrokeStyleDialog dialog(look, 20.0, QColor(30, 30, 30), &document, nullptr);
        dialog.adjustSize();
        return written(dialog, "Custom Stroke Style");
    }
    if (args.at(0) == "brush-library")
    {
        // Phoenix's own brushes and the file's, as Import Brushes from
        // Document adds them, in a library kept only for the picture.
        BrushLibraryStore store{QString()};
        if (brushes)
            store.add(brushes->definitions);
        BrushLibraryDialog::Hooks hooks;
        hooks.document = [&document]() -> const fla::Document* { return &document; };
        BrushLibraryDialog dialog(store, hooks, nullptr);
        const QString category = option(args, "--category");
        if (!category.isEmpty())
            dialog.chooseCategory(category);
        return written(dialog, "Brush Library");
    }
    if (args.at(0) == "toolbox")
        return captureToolbox(args, out);
    if (args.at(0) == "document-brushes")
    {
        DocumentBrushesDialog dialog(document, DocumentBrushesDialog::Hooks(), nullptr);
        return written(dialog, "Document Brushes");
    }
    std::string xml;
    for (const auto& [path, bytes] : file->document->extraFiles)
        if (path == "PublishSettings.xml")
            xml.assign(bytes.get().begin(), bytes.get().end());
    const QFileInfo info(args.at(1));
    PublishSettingsDialog dialog(xml, info.completeBaseName(), file->document->width, file->document->height,
        info.absolutePath(), true, nullptr);
    dialog.adjustSize();
    if (!dialog.grab().save(out))
    {
        std::fprintf(stderr, "Could not write %s\n", qPrintable(out));
        return 1;
    }
    std::printf("Drew Publish Settings for %s to %s\n", qPrintable(args.at(1)), qPrintable(out));
    return 0;
}

/// Lets queued work (layout, repaints, timers a panel starts) run for
/// \a ms milliseconds.
void settle(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        // Widgets let go of with deleteLater, which an event loop of its own
        // would delete: left alone they would still draw where they were.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

/// The menu bar's action at \a path ("Modify>Bitmap>Trace Bitmap..."),
/// matched without "&" or a trailing "...".
QAction* menuAction(QMainWindow& window, const QString& path)
{
    const auto plain = [](QString text) {
        text.remove('&');
        if (text.endsWith("..."))
            text.chop(3);
        else if (text.endsWith(QChar(0x2026)))
            text.chop(1);
        return text.trimmed();
    };
    QList<QAction*> actions = window.menuBar()->actions();
    QAction* found = nullptr;
    for (const QString& part : path.split('>'))
    {
        found = nullptr;
        for (QAction* action : actions)
            if (!action->isSeparator() && plain(action->text()).compare(plain(part), Qt::CaseInsensitive) == 0)
            {
                found = action;
                break;
            }
        if (!found)
            return nullptr;
        // A menu fills some of its items as it opens.
        if (QMenu* menu = found->menu())
        {
            emit menu->aboutToShow();
            actions = menu->actions();
        }
        else
            actions.clear();
    }
    return found;
}

/// Keeps every window that opens (a dialog, a menu) off the screen while
/// it is drawn, as the main window is.
class Unseen : public QObject
{
public:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (event->type() == QEvent::Polish || event->type() == QEvent::PolishRequest)
            if (QWidget* widget = qobject_cast<QWidget*>(object); widget && widget->isWindow())
                widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};

/// capture-window <file> --out <png> [--script <jsfl>] [--size WxH]
/// [--show <dock>]... [--panel <name>] [--menu <path>] [--action <path>]
/// [--crop x,y,w,h] [--theme <name>]: the main window with the file open,
/// never on the screen, after a script has set it up (selected something,
/// moved to a frame), for the guide's pictures. --panel draws one widget
/// (a dock or any widget by object name), --menu one menu as it drops
/// down, --action the dialog a menu item opens (closed again after;
/// --press clicks one of its buttons first).
int captureWindow(const QStringList& args)
{
    const QString out = option(args, "--out");
    if (args.isEmpty() || out.isEmpty())
    {
        std::fprintf(stderr, "capture-window: <file> --out <png> [--script <jsfl>] [--size WxH] [--show <dock>] "
                             "[--panel <name>] [--menu <path>] [--action <path>] [--crop x,y,w,h]\n");
        return 1;
    }
    const QString theme = option(args, "--theme");
    theme::preview(theme::load(theme.isEmpty() ? QString("Dark") : theme));
    Unseen unseen;
    qApp->installEventFilter(&unseen);
    MainWindow window(QString(), nullptr, false);
    QSize size(1800, 1100);
    const QStringList wh = option(args, "--size").split('x');
    if (wh.size() == 2 && wh.at(0).toInt() > 0 && wh.at(1).toInt() > 0)
        size = QSize(wh.at(0).toInt(), wh.at(1).toInt());
    window.resize(size);
    // Laid out and drawn as if shown, but never on the screen.
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    settle(200);
    if (args.at(0) != "-")
        window.openDocumentPath(QFileInfo(args.at(0)).absoluteFilePath());
    settle(500);
    for (int i = 0; i + 1 < args.size(); ++i)
        if (args.at(i) == "--show")
        {
            QDockWidget* dock = window.findChild<QDockWidget*>(args.at(i + 1));
            if (!dock)
            {
                std::fprintf(stderr, "capture-window: no panel %s\n", qPrintable(args.at(i + 1)));
                return 1;
            }
            dock->show();
            dock->raise();
        }
    // The panel to be drawn comes to the front of its tabs, as --show does.
    // (An empty name would match any panel.)
    QDockWidget* drawn = option(args, "--panel").isEmpty() ? nullptr : window.findChild<QDockWidget*>(option(args, "--panel"));
    if (drawn)
    {
        drawn->show();
        drawn->raise();
    }
    // The right-hand panels as wide as the guide's other pictures (--dock-width),
    // so the Properties panel's rows fit.
    const int dockWidth = option(args, "--dock-width").toInt() > 0 ? option(args, "--dock-width").toInt() : 391;
    QList<QDockWidget*> side;
    for (QDockWidget* dock : window.findChildren<QDockWidget*>())
    {
        const Qt::DockWidgetArea area = window.dockWidgetArea(dock);
        if (area == Qt::RightDockWidgetArea &&
            !dock->isHidden())
            side.append(dock);
    }
    window.resizeDocks(side, QList<int>(side.size(), dockWidth), Qt::Horizontal);
    settle(200);
    const QString script = option(args, "--script");
    if (!script.isEmpty() && !window.scripts()->runFile(QFileInfo(script).absoluteFilePath()))
    {
        std::fprintf(stderr, "capture-window: %s failed\n", qPrintable(script));
        return 1;
    }
    window.refreshAfterScript();
    settle(500);
    // Menu items turned on or run (--trigger), then drags on the stage
    // (--drag x1,y1,x2,y2 in the stage's own widget, with ,shift or ,alt
    // held), in the order given.
    for (int i = 0; i + 1 < args.size(); ++i)
        if (args.at(i) == "--trigger")
        {
            QAction* action = menuAction(window, args.at(i + 1));
            if (!action)
            {
                std::fprintf(stderr, "capture-window: no menu item %s\n", qPrintable(args.at(i + 1)));
                return 1;
            }
            action->trigger();
            settle(200);
        }
        else if (args.at(i) == "--drag")
        {
            const QStringList parts = args.at(i + 1).split(',');
            PhoenixView* stage = window.findChild<PhoenixView*>();
            if (parts.size() < 4 || !stage)
            {
                std::fprintf(stderr, "capture-window: --drag x1,y1,x2,y2[,shift|alt]\n");
                return 1;
            }
            const Qt::KeyboardModifiers held = parts.value(4) == "shift" ? Qt::ShiftModifier
                : parts.value(4) == "alt"                                ? Qt::AltModifier
                                                                         : Qt::NoModifier;
            const QPointF from(parts.at(0).toDouble(), parts.at(1).toDouble());
            const QPointF to(parts.at(2).toDouble(), parts.at(3).toDouble());
            const auto send = [&](QEvent::Type type, const QPointF& at, Qt::MouseButtons buttons) {
                QMouseEvent event(type, at, stage->mapToGlobal(at), Qt::LeftButton, buttons, held);
                QApplication::sendEvent(stage, &event);
            };
            send(QEvent::MouseButtonPress, from, Qt::LeftButton);
            for (int step = 1; step <= 8; ++step)
                send(QEvent::MouseMove, from + (to - from) * (step / 8.0), Qt::LeftButton);
            send(QEvent::MouseButtonRelease, to, Qt::NoButton);
            settle(200);
        }
    const QString steps = option(args, "--steps");
    if (!steps.isEmpty())
    {
        QString error;
        if (!runCaptureSteps(window, steps, error))
        {
            std::fprintf(stderr, "capture-window: %s\n", qPrintable(error));
            return 1;
        }
        settle(300);
    }

    QPixmap picture;
    const QString panel = option(args, "--panel");
    const QString menuPath = option(args, "--menu");
    const QString actionPath = option(args, "--action");
    if (!actionPath.isEmpty())
    {
        QAction* action = menuAction(window, actionPath);
        if (!action)
        {
            std::fprintf(stderr, "capture-window: no menu item %s\n", qPrintable(actionPath));
            return 1;
        }
        // The dialog it opens is drawn once it is up (after --press clicks
        // one of its buttons, by its text), then closed.
        const QString press = option(args, "--press");
        QTimer::singleShot(600, [&picture, press]() {
            QWidget* dialog = QApplication::activeModalWidget();
            if (!dialog)
                for (QWidget* top : QApplication::topLevelWidgets())
                    if (top->isVisible() && qobject_cast<QDialog*>(top))
                        dialog = top;
            if (!dialog)
                return;
            if (!press.isEmpty())
                for (QAbstractButton* button : dialog->findChildren<QAbstractButton*>())
                    if (button->text().remove('&') == press)
                    {
                        button->click();
                        settle(300);
                        break;
                    }
            picture = dialog->grab();
            if (QDialog* asDialog = qobject_cast<QDialog*>(dialog))
                asDialog->reject();
            else
                dialog->close();
        });
        action->trigger();
        settle(800);
        if (picture.isNull())
        {
            std::fprintf(stderr, "capture-window: %s opened no dialog\n", qPrintable(actionPath));
            return 1;
        }
    }
    else if (!menuPath.isEmpty())
    {
        QAction* action = menuAction(window, menuPath);
        QMenu* menu = action ? action->menu() : nullptr;
        if (!menu)
        {
            std::fprintf(stderr, "capture-window: no menu %s\n", qPrintable(menuPath));
            return 1;
        }
        menu->ensurePolished();
        menu->adjustSize();
        picture = menu->grab();
    }
    else if (!panel.isEmpty())
    {
        QWidget* widget = window.findChild<QWidget*>(panel);
        if (!widget)
        {
            std::fprintf(stderr, "capture-window: no panel %s\n", qPrintable(panel));
            return 1;
        }
        picture = widget->grab();
    }
    else
        picture = window.grab();
    const QStringList crop = option(args, "--crop").split(',');
    if (crop.size() == 4)
    {
        const double ratio = picture.devicePixelRatio();
        const QRect area(QPoint(crop.at(0).toInt(), crop.at(1).toInt()) * ratio,
            QSize(crop.at(2).toInt(), crop.at(3).toInt()) * ratio);
        if (area.isValid())
            picture = picture.copy(area);
    }
    if (!picture.save(out))
    {
        std::fprintf(stderr, "Could not write %s\n", qPrintable(out));
        return 1;
    }
    std::printf("Drew %dx%d to %s\n", picture.width(), picture.height(), qPrintable(out));
    // Nothing is saved: the document stays as it was on disk.
    return 0;
}

} // namespace

bool isCommand(const QString& name)
{
    return name == "capture-dialog" || name == "capture-window" || name == "play" || name == "display-check" || name == "benchmark" || name == "render" || name == "gpu-bakeoff" || name == "run-script" || name == "doctor" || name == "diff" || name == "batch" || name == "shots" || name == "links" || name == "shortcuts" || name == "lessons" || name == "help" ||
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
    QStringList args = arguments.mid(2);
    // Scripts run without the native libraries in External Libraries.
    if (args.removeAll("--no-native-extensions") > 0)
        jsfl::native::setEnabled(false);
#ifdef PHOENIX_GPU
    if (name == "gpu-bakeoff")
    {
        if (args.isEmpty())
        {
            std::fprintf(stderr, "gpu-bakeoff: which file?\n");
            return 1;
        }
        MainWindow window(QString(), nullptr, false);
        return window.gpuBakeoffCommand(args.at(0), args.mid(1));
    }
#endif
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
    if (name == "display-check")
        return displayCheck(args);
    if (name == "diff")
        return diff(args);
    if (name == "batch")
        return batch(args);
    if (name == "shots")
        return splitShots(args);
    if (name == "capture-window")
        return captureWindow(args);
    if (name == "capture-dialog")
        return captureDialog(args);
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
