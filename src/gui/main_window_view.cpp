// The view's own settings and helpers, kept apart from the rest of the main
// window: Mirror View, Grayscale, the reference board, Canvas Only and the
// touch gestures on the stage.

#include "main_window.h"

#include "canvas_only.h"
#include "stage_controls.h"
#include "stage_gestures.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QStatusBar>
#include <QTimer>

#include <cmath>

namespace refs = fla::references;

namespace {

/// Mirror View's icon: a shape and its outline mirrored across a dashed
/// line.
QIcon mirrorIcon(const QColor& color)
{
    QPixmap pixmap(32, 32);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath left;
    left.moveTo(6.5, 3.0);
    left.lineTo(6.5, 13.0);
    left.lineTo(1.5, 13.0);
    left.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPath(left);
    QPainterPath right;
    right.moveTo(9.5, 3.0);
    right.lineTo(9.5, 13.0);
    right.lineTo(14.5, 13.0);
    right.closeSubpath();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(color, 1.0));
    painter.drawPath(right);
    QPen dashed(color, 1.0, Qt::CustomDashLine);
    dashed.setDashPattern({1.5, 1.5});
    painter.setPen(dashed);
    painter.drawLine(QPointF(8.0, 1.0), QPointF(8.0, 15.0));
    return QIcon(pixmap);
}

/// Grayscale's icon: a circle half filled, light and dark.
QIcon grayscaleIcon(const QColor& color)
{
    QPixmap pixmap(32, 32);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF circle(2.5, 2.5, 11.0, 11.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPie(circle, 90 * 16, 180 * 16);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(color, 1.0));
    painter.drawEllipse(circle);
    return QIcon(pixmap);
}

} // namespace

void MainWindow::createViewExtras(QMenu* viewMenu, QAction* after, StageControls* controls, int layoutVersion)
{
    // They go in the View menu after \a after, with Reset Rotation's kind.
    const QList<QAction*> existing = viewMenu->actions();
    QAction* before = existing.value(static_cast<int>(existing.indexOf(after)) + 1);
    const auto put = [viewMenu, before](QAction* action) { viewMenu->insertAction(before, action); };

    // Mirror View: Shift+M, as M is the Bone tool.
    QAction* mirror = new QAction("&Mirror View", this);
    mirror->setObjectName("viewMirror");
    mirror->setCheckable(true);
    mirror->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_M));
    mirror->setToolTip("Mirror View: flip the view left to right to check a drawing (Shift+M)");
    mirror->setStatusTip("Flip the view of the stage left to right, to check a drawing with fresh eyes; the art is not changed");
    connect(mirror, &QAction::toggled, _phoenixView, &PhoenixView::setMirrored);
    connect(_phoenixView, &PhoenixView::mirroredChanged, mirror, &QAction::setChecked);
    put(mirror);
    if (controls)
        controls->addToggle(mirror, "stageMirror", mirrorIcon);

    QAction* grayscale = new QAction("&Grayscale", this);
    grayscale->setObjectName("viewGrayscale");
    grayscale->setCheckable(true);
    grayscale->setStatusTip("Show the stage in shades of gray to check its light and dark values; the colors are not changed");
    grayscale->setToolTip("Grayscale: show the stage in shades of gray to check its values");
    connect(grayscale, &QAction::toggled, _phoenixView, &PhoenixView::setGrayscale);
    put(grayscale);
    if (controls)
        controls->addToggle(grayscale, "stageGrayscale", grayscaleIcon);

    _canvasOnly = new CanvasOnly(this, {_breadcrumbRow}, layoutVersion);
    put(_canvasOnly->action());

    // The reference board: pictures to draw from, around the stage.
    _referenceBoard = std::make_unique<ReferenceBoardView>(
        _phoenixView, [this]() { return _flaDocument ? _flaDocument->document : nullptr; },
        [this](fla::CommandPtr command) { _editContext.commandStack().push(std::move(command)); });
    _phoenixView->setReferenceBoard(_referenceBoard.get());
    _referenceTool = std::make_unique<ReferenceTool>(_referenceBoard.get(), [this]() {
        chooseTool(_toolBeforeReferences ? _toolBeforeReferences : _selectionTool.get());
    });

    QMenu* references = new QMenu("&Reference Images", viewMenu);
    references->menuAction()->setObjectName("viewReferenceImages");
    viewMenu->insertMenu(before, references);
    QAction* add = references->addAction("&Add Reference Image...");
    add->setObjectName("viewAddReferenceImage");
    add->setStatusTip("Pin a picture beside the stage to draw from; it is never exported");
    connect(add, &QAction::triggered, this, &MainWindow::addReferenceImages);
    QAction* arrange = references->addAction("A&rrange Reference Images");
    arrange->setObjectName("viewArrangeReferenceImages");
    arrange->setCheckable(true);
    arrange->setStatusTip("Move, size and turn the reference images; right-click one for its opacity, lock and order");
    connect(arrange, &QAction::triggered, this, [this](bool on) {
        Tool* active = _phoenixView->activeTool();
        if (on && active != _referenceTool.get())
        {
            _toolBeforeReferences = active;
            _phoenixView->setActiveTool(_referenceTool.get());
        }
        else if (!on && active == _referenceTool.get())
        {
            chooseTool(_toolBeforeReferences ? _toolBeforeReferences : _selectionTool.get());
        }
    });
    connect(_phoenixView, &PhoenixView::activeToolChanged, arrange, [this, arrange](Tool* tool) {
        const QSignalBlocker quiet(arrange);
        arrange->setChecked(tool == _referenceTool.get());
    });
    QAction* hide = references->addAction("&Hide Reference Images");
    hide->setObjectName("viewHideReferenceImages");
    hide->setCheckable(true);
    hide->setStatusTip("Hide every reference image, or show them again");
    connect(hide, &QAction::triggered, this, [this](bool on) {
        refs::Board board = _referenceBoard->board();
        board.hidden = on;
        _referenceBoard->change(board, on ? "Hide Reference Images" : "Show Reference Images");
    });
    references->addSeparator();
    QAction* removeAll = references->addAction("Remove All Reference Images");
    removeAll->setObjectName("viewRemoveReferenceImages");
    connect(removeAll, &QAction::triggered, this, [this]() {
        refs::Board board = _referenceBoard->board();
        board.images.clear();
        board.hidden = false;
        _referenceBoard->select(-1);
        _referenceBoard->change(board, "Remove Reference Images");
    });
    connect(references, &QMenu::aboutToShow, this, [this, add, arrange, hide, removeAll]() {
        const bool open = _flaDocument && _flaDocument->document;
        const refs::Board board = _referenceBoard->board();
        add->setEnabled(open);
        arrange->setEnabled(open && !board.images.empty());
        hide->setEnabled(open && !board.images.empty());
        hide->setChecked(board.hidden);
        removeAll->setEnabled(open && !board.images.empty());
    });

    // Fingers on the stage. What the first finger began as a mouse press is
    // taken back once two fingers show it was a gesture; a two-finger tap
    // then undoes one step more.
    new StageGestures(
        _phoenixView, [this]() { _touchTop = _editContext.commandStack().topCommand(); },
        [this](bool tap) {
            QTimer::singleShot(0, this, [this, tap]() {
                fla::CommandStack& stack = _editContext.commandStack();
                for (int guard = 0; guard < 8 && stack.canUndo() && stack.topCommand() != _touchTop; ++guard)
                    _undoAction->trigger();
                if (tap)
                    _undoAction->trigger();
                _touchTop = stack.topCommand();
            });
        });
}

void MainWindow::addReferenceImages()
{
    if (!_flaDocument || !_flaDocument->document || !_referenceBoard)
        return;
    const QStringList paths = QFileDialog::getOpenFileNames(this, "Add Reference Image", _lastDirectory,
        "Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.tif *.tiff);;All files (*)");
    if (paths.isEmpty())
        return;
    _lastDirectory = QFileInfo(paths.front()).absolutePath();
    const size_t before = _referenceBoard->board().images.size();
    const QStringList failed = _referenceBoard->addFiles(paths, nullptr);
    if (!failed.isEmpty())
        QMessageBox::warning(this, "Add Reference Image", "These could not be read as pictures:\n" + failed.join("\n"));
    // Out of sight beside the stage, the view draws back to show it too.
    const refs::Board board = _referenceBoard->board();
    if (board.images.size() > before)
    {
        const fla::Document* document = _flaDocument->document;
        QRectF wanted(0, 0, document->width, document->height);
        for (size_t i = before; i < board.images.size(); ++i)
        {
            const QSizeF size = _referenceBoard->imageSize(static_cast<int>(i));
            wanted |= _referenceBoard->imageToStage(board.images[i]).mapRect(QRectF(QPointF(0, 0), size));
        }
        const QRectF shown(_phoenixView->widgetToStage(QPointF(0, 0)),
            _phoenixView->widgetToStage(QPointF(_phoenixView->width(), _phoenixView->height())));
        if (!shown.normalized().contains(wanted))
            _phoenixView->zoomToRect(wanted.adjusted(-20, -20, 20, 20));
        _phoenixView->update();
        statusBar()->showMessage("Reference images are never exported. View > Reference Images > Arrange Reference "
                                 "Images moves and sizes them",
            8000);
    }
}

bool MainWindow::showReferenceMenu(const QPoint& globalPos)
{
    if (!_referenceTool || _phoenixView->activeTool() != _referenceTool.get())
        return false;
    const QPointF local = _phoenixView->mapFromGlobal(globalPos);
    const int index = _referenceBoard->imageAt(_phoenixView->widgetToStage(local));
    QMenu menu(this);
    if (index >= 0)
    {
        _referenceBoard->select(index);
        _referenceBoard->fillMenu(&menu, index);
        menu.addSeparator();
    }
    menu.addAction("Add Reference Image...", this, &MainWindow::addReferenceImages);
    menu.addAction("Done Arranging", this, [this]() {
        chooseTool(_toolBeforeReferences ? _toolBeforeReferences : _selectionTool.get());
    });
    menu.exec(globalPos);
    return true;
}

bool MainWindow::dropReferenceImages(const QStringList& paths, const QPoint& windowPos,
    Qt::KeyboardModifiers modifiers)
{
    // Alt held as the files are let go.
    if (!_referenceBoard || !_phoenixView ||
        !((modifiers | QGuiApplication::queryKeyboardModifiers()) & Qt::AltModifier))
        return false;
    const QPoint local = _phoenixView->mapFrom(this, windowPos);
    if (!_phoenixView->rect().contains(local))
        return false;
    QStringList images;
    for (const QString& path : paths)
        if (ReferenceBoardView::isImageFile(path))
            images << path;
    if (images.isEmpty())
        return false;
    const QPointF at = _phoenixView->widgetToStage(QPointF(local));
    const QStringList failed = _referenceBoard->addFiles(images, &at);
    if (!failed.isEmpty())
        QMessageBox::warning(this, "Add Reference Image", "These could not be read as pictures:\n" + failed.join("\n"));
    return true;
}
