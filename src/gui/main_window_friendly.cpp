// MainWindow's friendlier ways in, kept apart from the rest of it: the
// pop-up palette, the recent colors, the Simple workspace, first-use hints
// and the starter templates.

#include "main_window.h"

#include "color_panel.h"
#include "color_picker_widgets.h"
#include "hint_bubble.h"
#include "popup_palette.h"
#include "simple_workspace.h"
#include "start_dialog.h"
#include "starter_templates.h"
#include "toolbox.h"

#include <QCursor>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLayout>
#include <QMessageBox>
#include <QStatusBar>

#include <algorithm>
#include <set>

namespace {

/// The tools that draw: a right-click with one of them can open the
/// pop-up palette.
const std::set<QString>& drawingTools()
{
    static const std::set<QString> tools = {"Pen", "Line", "Rectangle", "Oval", "Rectangle Primitive",
        "Oval Primitive", "PolyStar", "Pencil", "Brush", "Nib Brush", "Ink Brush", "Paint Brush", "Paint Bucket",
        "Ink Bottle", "Eraser"};
    return tools;
}

/// The edits that put down color, by name, and which they use.
const std::set<std::string> kFillEdits = {"Brush", "Paint Bucket", "Apply Fill"};
const std::set<std::string> kStrokeEdits = {"Pencil", "Line", "Ink Bottle", "Stroke Color", "Paint Brush"};
const std::set<std::string> kBothEdits = {"Rectangle", "Oval", "PolyStar", "Pen", "Rectangle Primitive",
    "Oval Primitive"};

bool hintsWanted()
{
    return QSettings().value("hints/enabled", true).toBool();
}

} // namespace

void MainWindow::setupFriendly()
{
    // The pop-up palette, on the key left of 1.
    QAction* paletteAction = new QAction("&Pop-up Palette", this);
    paletteAction->setObjectName("popupPaletteAction");
    paletteAction->setShortcut(QKeySequence(Qt::Key_QuoteLeft));
    paletteAction->setStatusTip("Favorite tools, recent colors and a color wheel at the cursor");
    connect(paletteAction, &QAction::triggered, this, [this]() { showPopupPalette(QCursor::pos()); });
    addAction(paletteAction);
    for (QAction* menu : menuBar()->actions())
        if (menu->text() == "&Window" && menu->menu())
        {
            QAction* after = menu->menu()->actions().value(1);
            menu->menu()->insertAction(after, paletteAction);
        }

    // The wheel and the recent colors in the Color panel pick for the
    // selection too, as a swatch does.
    if (_colorPanel)
        connect(_colorPanel, &ColorPanel::colorChosen, this, [this](bool stroke) {
            if (stroke)
                refreshStyleSwatches();
            if (!_textTool || !_textTool->isEditing())
                recolorSelection(stroke);
            else if (!stroke)
                _textTool->applyColor(_drawingStyle.fillColor);
        });

    // The colors drawn with, and the hints, follow the edits.
    connect(this, &MainWindow::editStateChanged, this, &MainWindow::noteFriendlyEdit);
    if (_phoenixView)
    {
        _hintBubble = new HintBubble(_phoenixView);
        connect(_hintBubble, &HintBubble::hintsTurnedOff, this, [this]() {
            QSettings().setValue("hints/enabled", false);
            statusBar()->showMessage("Hints are off. Help > Show Hints turns them back on.", 5000);
        });
    }

    // Help: the hints on or off, and all of them again.
    for (QAction* menu : menuBar()->actions())
        if (menu->text() == "&Help" && menu->menu())
        {
            QAction* after = nullptr;
            const QList<QAction*> items = menu->menu()->actions();
            for (int i = 0; i < items.size(); ++i)
                if (items[i]->text() == "&Tutorials...")
                    after = items.value(i + 1);
            QAction* show = new QAction("Show &Hints", this);
            show->setObjectName("showHintsAction");
            show->setCheckable(true);
            show->setStatusTip("A short tip on what to try next, the first time you draw, add a keyframe, "
                               "make a symbol or make a tween");
            connect(menu->menu(), &QMenu::aboutToShow, show, [show]() { show->setChecked(hintsWanted()); });
            connect(show, &QAction::toggled, this, [](bool on) { QSettings().setValue("hints/enabled", on); });
            QAction* reset = new QAction("&Reset Hints", this);
            reset->setObjectName("resetHintsAction");
            reset->setStatusTip("Show every first-use hint again");
            connect(reset, &QAction::triggered, this, [this]() {
                QSettings settings;
                settings.remove("hints/shown");
                settings.setValue("hints/enabled", true);
                statusBar()->showMessage("Every hint will show again.", 4000);
            });
            menu->menu()->insertAction(after, show);
            menu->menu()->insertAction(after, reset);
        }
}

bool MainWindow::popupPaletteOnRightClick() const
{
    if (!QSettings().value("popupPalette/rightClick", false).toBool())
        return false;
    for (const auto& [tool, action] : _toolActionList)
        if (action->isChecked())
            return drawingTools().count(action->text()) > 0;
    return false;
}

void MainWindow::showPopupPalette(const QPoint& globalPos)
{
    if (!_popupPalette)
    {
        _popupPalette = new PopupPalette(this);
        connect(_popupPalette, &PopupPalette::colorPicked, this,
            [this](const QColor& color, bool stroke, bool done) { setStyleColor(color, stroke, done); });
        connect(_popupPalette, &PopupPalette::favoritesChanged, this,
            [](const QStringList& favorites) { QSettings().setValue("popupPalette/favorites", favorites); });
    }
    QList<QAction*> tools;
    for (const auto& [tool, action] : _toolActionList)
        tools << action;
    const QVariant saved = QSettings().value("popupPalette/favorites");
    _popupPalette->setTools(tools, saved.isValid() ? saved.toStringList() : PopupPalette::defaultFavorites());
    const auto colorOf = [](const uint8_t rgba[4]) { return QColor(rgba[0], rgba[1], rgba[2], rgba[3]); };
    _popupPalette->setColors(colorOf(_drawingStyle.fillColor), colorOf(_drawingStyle.strokeColor));
    // The wheel sets the color the tool draws with: a pencil's or a
    // line's stroke, else the fill.
    bool stroke = false;
    for (const auto& [tool, action] : _toolActionList)
        if (action->isChecked())
            stroke = action->text() == "Pencil" || action->text() == "Line" || action->text() == "Ink Bottle" ||
                     action->text() == "Paint Brush" || action->text() == "Pen";
    _popupPalette->setEditsStroke(stroke);
    _popupPalette->popupAt(globalPos);
}

void MainWindow::setStyleColor(const QColor& color, bool stroke, bool done)
{
    uint8_t* target = stroke ? _drawingStyle.strokeColor : _drawingStyle.fillColor;
    target[0] = static_cast<uint8_t>(color.red());
    target[1] = static_cast<uint8_t>(color.green());
    target[2] = static_cast<uint8_t>(color.blue());
    target[3] = static_cast<uint8_t>(color.alpha());
    if (stroke)
    {
        _drawingStyle.hasStroke = true;
    }
    else
    {
        _drawingStyle.hasFill = true;
        _drawingStyle.fillKind = fla::DrawingStyle::FillKind::Solid;
    }
    refreshStyleSwatches();
    if (_colorPanel)
        _colorPanel->refresh();
    if (!done)
        return;
    if (!stroke && _textTool)
        _textTool->applyColor(_drawingStyle.fillColor);
    if (!_textTool || !_textTool->isEditing())
        recolorSelection(stroke);
}

void MainWindow::noteFriendlyEdit()
{
    // Only a new edit counts: not an undo, nor another document's history
    // come into the window with its tab.
    const fla::CommandStack& stack = _editContext.commandStack();
    if (stack.isInMacro() || stack.isExecuting())
        return;
    const fla::Command* top = stack.topCommand();
    const size_t index = stack.index();
    const bool fresh = _flaDocument == _friendlyDocument && top && top != _friendlyTop &&
                       index >= _friendlyIndex && index == stack.count();
    _friendlyDocument = _flaDocument;
    _friendlyTop = top;
    _friendlyIndex = index;
    if (!fresh)
        return;
    const std::string name = top->name();

    // The colors it put down.
    const bool fill = kFillEdits.count(name) || kBothEdits.count(name);
    const bool line = kStrokeEdits.count(name) || kBothEdits.count(name);
    if (fill && _drawingStyle.hasFill && _drawingStyle.fillKind == fla::DrawingStyle::FillKind::Solid)
        RecentColors::instance().add(_drawingStyle.fillColor);
    if (line && _drawingStyle.hasStroke)
        RecentColors::instance().add(_drawingStyle.strokeColor);

    using Event = fla::FirstUseHints::Event;
    if ((fill || line) && name != "Apply Fill" && name != "Stroke Color")
        showFirstUseHint(static_cast<int>(Event::Drew));
    else if (name == "Insert Keyframe" || name == "Insert Blank Keyframe")
        showFirstUseHint(static_cast<int>(Event::Keyframe));
    else if (name == "Convert to Symbol")
        showFirstUseHint(static_cast<int>(Event::Symbol));
    else if (name == "Create Motion Tween")
        showFirstUseHint(static_cast<int>(Event::Tween), "motion");
    else if (name == "Create Classic Tween")
        showFirstUseHint(static_cast<int>(Event::Tween), "classic");
    else if (name == "Create Shape Tween")
        showFirstUseHint(static_cast<int>(Event::Tween), "shape");
}

void MainWindow::showFirstUseHint(int event, const std::string& tween)
{
    // Not in a window a command line runs, unless asked (for the guide's
    // pictures).
    if (!_hintBubble || (!_interactive && !qEnvironmentVariableIsSet("PHOENIX_SHOW_HINTS")))
        return;
    QSettings settings;
    _hints.setEnabled(hintsWanted());
    _hints.setShownText(settings.value("hints/shown").toString().toStdString());
    fla::FirstUseHints::Context context;
    context.frame = _player ? _player->currentFrame() : 0;
    context.tween = tween;
    const std::optional<fla::FirstUseHints::Hint> hint = _hints.on(static_cast<fla::FirstUseHints::Event>(event), context);
    if (!hint)
        return;
    settings.setValue("hints/shown", QString::fromStdString(_hints.shownText()));
    _hintBubble->showHint(QString::fromStdString(hint->text));
}

void MainWindow::applySimpleWorkspace()
{
    // Only its panels open, Properties in front of its group.
    const std::vector<std::string>& kept = simpleworkspace::panels();
    for (QDockWidget* dock : findChildren<QDockWidget*>())
    {
        if (dock->parentWidget() != this)
            continue;
        const bool keep = std::find(kept.begin(), kept.end(), dock->objectName().toStdString()) != kept.end();
        dock->setVisible(keep);
    }
    if (QDockWidget* color = findChild<QDockWidget*>("colorDock"))
        color->raise();
    if (QDockWidget* properties = findChild<QDockWidget*>("propertiesDock"))
    {
        properties->raise();
        // Wide enough for its rows.
        resizeDocks({properties}, {std::max(properties->width(), 380)}, Qt::Horizontal);
    }

    // Its tools, the rest hidden (each still works by its key).
    Toolbox::Layout tools;
    tools.columns = 1;
    QStringList shown;
    for (const std::vector<std::string>& names : simpleworkspace::toolSlots())
    {
        Toolbox::Slot slot;
        for (const std::string& name : names)
        {
            const QString id = QString::fromStdString(name);
            if (id == "|" || _toolbox->tool(id))
                slot << id;
        }
        if (slot.isEmpty())
            continue;
        tools.entries.push_back(slot);
        shown << slot;
    }
    for (const QString& id : _toolbox->toolIds())
        if (!shown.contains(id))
            tools.hidden << id;
    _toolbox->setLayout(tools);
}

void MainWindow::rememberFullWorkspace()
{
    const QString current = QSettings().value("workspace/current", simpleworkspace::fullName()).toString();
    if (current != simpleworkspace::name())
        QSettings().setValue("workspace/lastFull", current);
}

void MainWindow::prepareStartDialog(StartDialog& dialog)
{
    // The first time, nothing is marked and a word says what Simple is;
    // after a choice, the workspace in use is marked.
    QSettings settings;
    const bool firstTime = !settings.value("workspace/chosen", false).toBool();
    dialog.setWorkspace(firstTime ? QString() : settings.value("workspace/current", simpleworkspace::fullName()).toString(),
        firstTime);
    connect(&dialog, &StartDialog::workspaceChosen, this, [this](const QString& name) {
        QSettings().setValue("workspace/chosen", true);
        useWorkspace(name);
    });
}

void MainWindow::openStarterTemplate(const QString& id)
{
    QString error;
    const QString path = startertemplates::copyToWork(id, error);
    if (path.isEmpty())
    {
        QMessageBox::warning(this, "Starter Templates", error);
        return;
    }
    openLessonDocument(path);
}
