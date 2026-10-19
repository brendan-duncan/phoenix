#include "main_window.h"

#include "../data/frame.h"
#include "../data/layer.h"
#include "../edit/inbetween.h"

#include <QAction>
#include <QMenu>
#include <QStatusBar>

#include <map>
#include <memory>

namespace {

/// What each layer's inbetween came to; an edit runs again on redo, so
/// each run replaces its layer's entry rather than adding to it.
struct InbetweenRun
{
    std::map<const fla::Layer*, fla::inbetween::Report> reports;
    std::map<const fla::Layer*, int> made;

    fla::inbetween::Report total() const
    {
        fla::inbetween::Report sum;
        for (const auto& entry : reports)
            sum += entry.second;
        return sum;
    }
};

/// The status bar's word on an inbetween: what was made, and what could
/// not be drawn between (a fade from one drawing to the other is not
/// something a vector drawing can hold).
QString inbetweenMessage(const fla::inbetween::Report& report, const QString& made)
{
    QString message = made;
    if (report.held > 0)
        message += QString(" %1 %2 had no match on the other key and %3 held as drawn on the nearer key.")
                       .arg(report.held)
                       .arg(report.held == 1 ? "thing" : "things")
                       .arg(report.held == 1 ? "was" : "were");
    return message;
}

const char* const kNoKeys = "Select two keyframes with drawings, or a frame between them, to draw an inbetween.";

} // namespace

void MainWindow::insertInbetween(double t)
{
    auto run = std::make_shared<InbetweenRun>();
    editFrames("Insert Inbetween", [run, t](fla::Layer& layer, int first, int last, std::vector<fla::Frame*>&) {
        fla::inbetween::Report report;
        run->made[&layer] = fla::inbetween::insertInbetween(layer, first, last, t, report);
        run->reports[&layer] = report;
    });
    const fla::inbetween::Report report = run->total();
    if (report.keyframes == 0)
    {
        statusBar()->showMessage(kNoKeys, 6000);
        return;
    }
    int frame = -1;
    for (const auto& entry : run->made)
        if (entry.second >= 0 && (frame < 0 || entry.second < frame))
            frame = entry.second;
    statusBar()->showMessage(inbetweenMessage(report, QString("Inbetween drawn on frame %1.").arg(frame + 1)), 8000);
}

void MainWindow::fillInbetweens(int spacing)
{
    auto run = std::make_shared<InbetweenRun>();
    const auto chosen = static_cast<fla::inbetween::Spacing>(spacing);
    editFrames("Fill Inbetweens", [run, chosen](fla::Layer& layer, int first, int last, std::vector<fla::Frame*>&) {
        fla::inbetween::Report report;
        run->made[&layer] = fla::inbetween::fillInbetweens(layer, first, last, chosen, report);
        run->reports[&layer] = report;
    });
    const fla::inbetween::Report report = run->total();
    if (report.keyframes == 0)
    {
        statusBar()->showMessage(QString(kNoKeys) + " Fill Inbetweens needs empty frames between them.", 6000);
        return;
    }
    statusBar()->showMessage(inbetweenMessage(report, QString("%1 %2 drawn.")
        .arg(report.keyframes)
        .arg(report.keyframes == 1 ? "inbetween" : "inbetweens")), 8000);
}

void MainWindow::addInbetweenMenus(QMenu* timelineMenu, QMenu* framesMenu)
{
    QMenu* insertMenu = new QMenu("Insert &Inbetween", this);
    insertMenu->setStatusTip("Draw a new keyframe part way between two keyframe drawings");
    const struct
    {
        const char* text;
        const char* tip;
        double t;
    } places[] = {
        {"&Halfway", "A drawing half way between the two keys", 0.5},
        {"One &Third", "A drawing a third of the way from the first key", 1.0 / 3.0},
        {"&Two Thirds", "A drawing two thirds of the way from the first key", 2.0 / 3.0},
        {"Favor &First Key", "A drawing a quarter of the way, close to the first key", 0.25},
        {"Favor &Second Key", "A drawing three quarters of the way, close to the second key", 0.75},
    };
    for (const auto& place : places)
    {
        QAction* action = insertMenu->addAction(place.text);
        action->setStatusTip(place.tip);
        const double t = place.t;
        connect(action, &QAction::triggered, this, [this, t]() { insertInbetween(t); });
    }

    QMenu* fillMenu = new QMenu("Fill I&nbetweens", this);
    fillMenu->setStatusTip("Draw every empty frame between two keyframe drawings");
    const struct
    {
        const char* text;
        const char* tip;
        fla::inbetween::Spacing spacing;
    } spacings[] = {
        {"&Even", "The drawings evenly spaced", fla::inbetween::Spacing::Even},
        {"Ease &In", "The drawings close together at the first key, starting slowly", fla::inbetween::Spacing::EaseIn},
        {"Ease &Out", "The drawings close together at the second key, slowing to a stop", fla::inbetween::Spacing::EaseOut},
    };
    for (const auto& choice : spacings)
    {
        QAction* action = fillMenu->addAction(choice.text);
        action->setStatusTip(choice.tip);
        const int spacing = static_cast<int>(choice.spacing);
        connect(action, &QAction::triggered, this, [this, spacing]() { fillInbetweens(spacing); });
    }

    if (timelineMenu)
    {
        timelineMenu->addSeparator();
        timelineMenu->addMenu(insertMenu);
        timelineMenu->addMenu(fillMenu);
    }
    if (framesMenu)
    {
        framesMenu->addSeparator();
        framesMenu->addMenu(insertMenu);
        framesMenu->addMenu(fillMenu);
    }
}
