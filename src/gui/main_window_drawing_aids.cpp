#include "main_window.h"

#include "drawing_aids_view.h"
#include "theme.h"
#include "tool_hint_label.h"

#include "../data/document.h"

#include <QActionGroup>
#include <QMenu>
#include <QStatusBar>

namespace aids = fla::aids;

QAction* MainWindow::symmetryAction()
{
    if (_symmetryAction)
        return _symmetryAction;
    _symmetryAction = new QAction("Symmetry &Drawing", this);
    _symmetryAction->setObjectName("viewSymmetry");
    _symmetryAction->setCheckable(true);
    theme::themedIcon(_symmetryAction, []() { return ToolIcons::icon(ToolIcons::Tool::Symmetry); });
    _symmetryAction->setToolTip("Symmetry: copy each stroke across the symmetry axis");
    _symmetryAction->setStatusTip("Brush, Pencil, Paint Brush, Ink Brush and Eraser strokes are copied across "
                                  "the symmetry axis as you draw. Move the axis with the Drawing Guides tool.");
    connect(_symmetryAction, &QAction::toggled, this, [this](bool on) {
        if (!_phoenixView)
            return;
        aids::Symmetry symmetry = drawaids::symmetry(*_phoenixView);
        if (symmetry.on == on)
            return;
        symmetry.on = on;
        drawaids::setSymmetry(*_phoenixView, symmetry);
        refreshDrawingAids();
    });
    addAction(_symmetryAction);
    return _symmetryAction;
}

void MainWindow::refreshDrawingAids()
{
    if (!_phoenixView)
        return;
    if (_symmetryAction)
    {
        QSignalBlocker block(_symmetryAction);
        _symmetryAction->setChecked(drawaids::symmetry(*_phoenixView).on);
    }
    if (_propertiesPanel)
        _propertiesPanel->refreshToolOptions();
    drawaids::refresh(*_phoenixView);
}

void MainWindow::createDrawingAids(QMenu* viewMenu)
{
    drawaids::attach(_phoenixView);

    // View > Symmetry.
    QMenu* symmetryMenu = viewMenu->addMenu("S&ymmetry");
    symmetryMenu->addAction(symmetryAction());
    symmetryMenu->addSeparator();
    QActionGroup* modes = new QActionGroup(symmetryMenu);
    const std::pair<const char*, aids::SymmetryMode> modeNames[] = {
        {"Mirror &Left and Right", aids::SymmetryMode::Vertical},
        {"Mirror &Top and Bottom", aids::SymmetryMode::Horizontal},
        {"Mirror &Four Ways", aids::SymmetryMode::Both},
        {"&Radial", aids::SymmetryMode::Radial},
    };
    std::vector<std::pair<QAction*, aids::SymmetryMode>> modeActions;
    for (const auto& entry : modeNames)
    {
        const auto mode = entry.second;
        QAction* action = symmetryMenu->addAction(entry.first);
        action->setCheckable(true);
        modes->addAction(action);
        modeActions.emplace_back(action, mode);
        connect(action, &QAction::triggered, this, [this, mode]() {
            aids::Symmetry symmetry = drawaids::symmetry(*_phoenixView);
            symmetry.mode = mode;
            symmetry.on = true;
            drawaids::setSymmetry(*_phoenixView, symmetry);
            refreshDrawingAids();
        });
    }
    QMenu* copiesMenu = symmetryMenu->addMenu("Radial &Copies");
    QActionGroup* counts = new QActionGroup(copiesMenu);
    std::vector<std::pair<QAction*, int>> countActions;
    for (int count : {2, 3, 4, 5, 6, 8, 10, 12, 16, 24})
    {
        QAction* action = copiesMenu->addAction(QString::number(count));
        action->setCheckable(true);
        counts->addAction(action);
        countActions.emplace_back(action, count);
        connect(action, &QAction::triggered, this, [this, count]() {
            aids::Symmetry symmetry = drawaids::symmetry(*_phoenixView);
            symmetry.copies = count;
            drawaids::setSymmetry(*_phoenixView, symmetry);
            refreshDrawingAids();
        });
    }
    QAction* mirrorAction = symmetryMenu->addAction("&Mirror Radial Copies");
    mirrorAction->setCheckable(true);
    connect(mirrorAction, &QAction::triggered, this, [this](bool on) {
        aids::Symmetry symmetry = drawaids::symmetry(*_phoenixView);
        symmetry.mirror = on;
        drawaids::setSymmetry(*_phoenixView, symmetry);
        refreshDrawingAids();
    });
    symmetryMenu->addSeparator();
    QAction* centerAction = symmetryMenu->addAction("C&enter Axis on Stage");
    centerAction->setStatusTip("Put the symmetry axis back in the middle of the stage, straight");
    connect(centerAction, &QAction::triggered, this, [this]() {
        const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
        if (!document)
            return;
        aids::Symmetry symmetry = drawaids::symmetry(*_phoenixView);
        symmetry.x = document->width / 2.0;
        symmetry.y = document->height / 2.0;
        symmetry.angle = 0.0;
        drawaids::setSymmetry(*_phoenixView, symmetry);
        refreshDrawingAids();
    });
    connect(symmetryMenu, &QMenu::aboutToShow, this, [this, modeActions, countActions, mirrorAction]() {
        const aids::Symmetry symmetry = drawaids::symmetry(*_phoenixView);
        for (const auto& [action, mode] : modeActions)
            action->setChecked(symmetry.mode == mode);
        for (const auto& [action, count] : countActions)
            action->setChecked(symmetry.copies == count);
        mirrorAction->setChecked(symmetry.mirror);
    });

    // View > Drawing Guides.
    QMenu* guidesMenu = viewMenu->addMenu("Dra&wing Guides");
    const auto changeGuides = [this](const std::function<void(aids::DrawingGuides&)>& change) {
        aids::DrawingGuides guides = drawaids::guides(*_phoenixView);
        change(guides);
        drawaids::setGuides(*_phoenixView, guides);
        refreshDrawingAids();
    };
    QMenu* addMenu = guidesMenu->addMenu("&Add");
    for (int kind = 0; kind <= static_cast<int>(aids::GuideKind::Perspective3); ++kind)
    {
        QAction* action = addMenu->addAction(aids::kindName(static_cast<aids::GuideKind>(kind)));
        connect(action, &QAction::triggered, this, [this, kind, changeGuides]() {
            const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
            if (!document)
                return;
            // In the middle of the stage, a third of it across; a
            // perspective's points spread over it.
            const auto guideKind = static_cast<aids::GuideKind>(kind);
            const bool perspective = guideKind == aids::GuideKind::Perspective1 ||
                                     guideKind == aids::GuideKind::Perspective2 ||
                                     guideKind == aids::GuideKind::Perspective3;
            const double size = document->width / (perspective ? 2.0 : 3.0);
            changeGuides([&](aids::DrawingGuides& guides) {
                guides.guides.push_back(aids::makeGuide(
                    guideKind, fla::Point(document->width / 2.0, document->height / 2.0), size));
                guides.hidden = false;
            });
            _drawingStyle.drawingGuideKind = kind;
            chooseTool(_drawingGuidesTool.get());
            drawaids::setSelectedGuide(*_phoenixView, static_cast<int>(drawaids::guides(*_phoenixView).guides.size()) - 1);
        });
    }
    QAction* showAction = guidesMenu->addAction("&Show Drawing Guides");
    showAction->setCheckable(true);
    connect(showAction, &QAction::triggered, this, [changeGuides](bool on) {
        changeGuides([on](aids::DrawingGuides& guides) { guides.hidden = !on; });
    });
    QAction* snapAction = guidesMenu->addAction("S&nap to Drawing Guides");
    snapAction->setCheckable(true);
    snapAction->setStatusTip("Freehand strokes keep to the guide they begin near, or run the way the guides lead");
    connect(snapAction, &QAction::triggered, this, [changeGuides](bool on) {
        changeGuides([on](aids::DrawingGuides& guides) { guides.snap = on; });
    });
    QMenu* magnetismMenu = guidesMenu->addMenu("&Magnetism");
    QActionGroup* strengths = new QActionGroup(magnetismMenu);
    std::vector<std::pair<QAction*, int>> strengthActions;
    for (const auto& [name, value] : {std::pair("Light (25%)", 25), std::pair("Medium (50%)", 50),
             std::pair("Strong (75%)", 75), std::pair("Full (100%)", 100)})
    {
        QAction* action = magnetismMenu->addAction(name);
        action->setCheckable(true);
        strengths->addAction(action);
        strengthActions.emplace_back(action, value);
        const int strength = value;
        connect(action, &QAction::triggered, this, [changeGuides, strength]() {
            changeGuides([strength](aids::DrawingGuides& guides) { guides.magnetism = strength; });
        });
    }
    guidesMenu->addSeparator();
    QAction* removeAction = guidesMenu->addAction("&Remove All Drawing Guides");
    connect(removeAction, &QAction::triggered, this, [this, changeGuides]() {
        changeGuides([](aids::DrawingGuides& guides) { guides.guides.clear(); });
        drawaids::setSelectedGuide(*_phoenixView, -1);
    });
    connect(guidesMenu, &QMenu::aboutToShow, this, [this, showAction, snapAction, strengthActions, removeAction]() {
        const aids::DrawingGuides guides = drawaids::guides(*_phoenixView);
        showAction->setChecked(!guides.hidden);
        snapAction->setChecked(guides.snap);
        for (const auto& [action, value] : strengthActions)
            action->setChecked(guides.magnetism == value);
        removeAction->setEnabled(!guides.guides.empty());
    });

    // The status bar's hint for the tool, and View > Tool Hints.
    _toolHintLabel = new ToolHintLabel(statusBar());
    statusBar()->addWidget(_toolHintLabel, 1);
    _toolHintLabel->setContext([this]() {
        fla::hints::Context context;
        context.faucet = _drawingStyle.faucet;
        return context;
    });
    if (_phoenixView->activeTool())
        _toolHintLabel->setTool(_phoenixView->activeTool()->name());
    connect(_phoenixView, &PhoenixView::activeToolChanged, this, [this](Tool* tool) {
        _toolHintLabel->setTool(tool ? tool->name() : QString());
        // The Drawing Guides tool shows the handles; the others do not.
        refreshDrawingAids();
    });
    QAction* hintsAction = viewMenu->addAction("Tool &Hints");
    hintsAction->setObjectName("viewToolHints");
    hintsAction->setCheckable(true);
    hintsAction->setChecked(ToolHintLabel::enabledSetting());
    hintsAction->setStatusTip("Show in the status bar what the tool does and what Shift, Alt and Ctrl add");
    connect(hintsAction, &QAction::toggled, this, [this](bool on) {
        ToolHintLabel::setEnabledSetting(on);
        _toolHintLabel->setHintsOn(on);
    });
}

void MainWindow::connectDrawingAidsPanel()
{
    connect(_propertiesPanel, &PropertiesPanel::toolSettingChanged, this, [this](const QString& key) {
        if (key == "faucet")
            _toolHintLabel->refresh();
        else if (key == "symmetry" || key == "drawingGuides")
            refreshDrawingAids();
    });
}
