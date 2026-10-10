#pragma once

#include "phoenix_view.h"
#include "document_view.h"
#include "timeline_view.h"
#include "player.h"
#include "../data/fla_document.h"
#include "../data/static_text.h"
#include "free_transform_tool.h"
#include "brush_tools.h"
#include "lasso_tool.h"
#include "navigation_tools.h"
#include "paint_tools.h"
#include "pen_tool.h"
#include "pencil_tool.h"
#include "primitive_tool.h"
#include "subselection_tool.h"
#include "text_tool.h"
#include "selection_tool.h"
#include "properties_panel.h"
#include "tool_icons.h"
#include "../edit/edit_context.h"
#include "../edit/selection.h"
#include "../edit/drawing_style.h"
#include "../edit/snapping.h"

#include <memory>
#include <vector>

#include <QMainWindow>
#include <QToolButton>

#include "../edit/editable_path.h"

#include <functional>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QString>
#include <QSplitter>
#include <QSettings>
#include <QStringList>

class QDoubleSpinBox;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /// Opens \a initialFile when given, else the most recent file.
    explicit MainWindow(const QString& initialFile = QString(), QWidget* parent = nullptr);
   
    void loadFLAFile(const QString& filePath);

private slots:
    void newFile();

    void openFile();

    void saveFile();

    void saveFileAs();

    void exportSvg();

    void openRecentFile();
    
    void quit();
    
    void viewDocument();
    
    void onVisibilityChanged();

    void onHighQualityAntiAliasingToggled(bool checked);

    void undo();

    void redo();

    void selectAll();

    void deselectAll();

    void cut();

    void copy();

    void paste();

    void pasteInPlace();

    void duplicateSelection();

    void groupSelection();

    void ungroupSelection();

    void breakApartSelection();

    void convertToSymbol();

    /// Modify > Shape: each rewrites every selected shape's paths as one
    /// undo step per shape.
    void smoothSelection();
    void straightenSelection();
    void optimizeSelection();
    void convertLinesToFillsSelection();
    void expandFillSelection();
    void softenFillEdgesSelection();

    /// The timeline being edited: the symbol entered, else the document's.
    fla::Timeline* editedTimeline() const;

    /// The frames a timeline command works on: the frames selected in the
    /// grid, else the current layer at the playhead.
    FrameSelection framesToEdit() const;

    /// Runs \a edit on each layer of the frames to edit as one undo step
    /// named \a name, and redraws what it changed.
    void editFrames(const char* name, const std::function<void(fla::Layer&, int first, int last,
        std::vector<fla::Frame*>& removed)>& edit);
    void insertFrame();
    void insertKeyframe(bool blank);
    void removeFrames();
    void clearKeyframe();
    void setClassicTween(bool on);
    void moveKeyframe(const fla::Layer* layer, int from, int to);
    void showFramesMenu(const QPoint& globalPos);
    void addLayer(bool folder);
    void deleteCurrentLayer();
    void renameLayer(const fla::Timeline* timeline, int layerIndex, const QString& name);

    /// Replaces each selected shape's contents with \a change applied to a
    /// copy, one undo step named \a name. A change that returns false
    /// leaves that shape alone.
    void reshapeSelectedShapes(const char* name, const std::function<bool(fla::Shape&, fla::Shape&)>& change);

    /// Applies \a tidy to every path of every selected shape.
    void tidySelectedShapes(const char* name, const std::function<void(fla::EditablePath&)>& tidy);

    /// Makes every selected text field static, dynamic or input.
    void setSelectedTextType(fla::StaticText::TextType type);

    /// Arrange: moves the selection in the z-order by \a step, or to the
    /// front or back when \a toEnd is set.
    void arrangeSelection(int step, bool toEnd);

private:
    void setupUI();

    void setupMenus();

    void setupToolBar();

    void setupPropertiesPanel();

    /// Refreshes the actions that only make sense with something selected.
    void updateSelectionState();

    /// Refreshes everything that depends on the undo history: the Edit menu
    /// entries and the modified marker in the title bar.
    void updateEditState();

    /// Rebuilds the title from the open file plus the modified marker.
    void updateWindowTitle();

    /// Offers to abandon unsaved changes. Returns false if the user cancels.
    bool confirmDiscardChanges();

    /// Lets go of the open document and deletes it, leaving nothing open.
    ///
    /// The selection and the undo history both hold pointers into the document,
    /// so both are detached first.
    void releaseDocument();

    /// Takes over  document and shows it, replacing whatever was open.
    ///
    /// Takes ownership. A null document leaves the window with nothing open,
    /// which is what a failed load wants.
    void adoptDocument(fla::FLADocument* document, const QString& filePath,
        const QString& displayName);

    /// Writes the document to  filePath, as a .fla when the name ends in
    /// .fla and as an uncompressed XFL folder otherwise. Reports failures and
    /// updates the saved state.
    bool saveToPath(const QString& filePath);

    /// Warns when saving would drop content the writer cannot represent yet.
    /// Returns false if the user would rather not go ahead.
    bool confirmLossySave();

    void updateRecentFilesMenu();

    void addToRecentFiles(const QString& filePath);

    void loadSettings();

    void saveSettings();

    void closeEvent(QCloseEvent* event) override;

    /// A .fla or .xfl dropped on the window is opened.
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    QIcon createPhoenixIcon();

    static const int MAX_RECENT_FILES = 10;

    fla::FLADocument* _flaDocument;
    fla::EditContext _editContext;
    fla::Selection _selection;
    fla::Snapper _snapper;
    std::unique_ptr<SelectionTool> _selectionTool;
    std::unique_ptr<FreeTransformTool> _freeTransformTool;
    std::unique_ptr<PrimitiveTool> _rectangleTool;
    std::unique_ptr<PrimitiveTool> _ovalTool;
    std::unique_ptr<PrimitiveTool> _lineTool;
    std::unique_ptr<PrimitiveTool> _polyStarTool;
    std::unique_ptr<PrimitiveTool> _rectanglePrimitiveTool;
    std::unique_ptr<PrimitiveTool> _ovalPrimitiveTool;
    std::unique_ptr<HandTool> _handTool;
    std::unique_ptr<ZoomTool> _zoomTool;
    std::unique_ptr<LassoTool> _lassoTool;
    std::unique_ptr<BrushTool> _brushTool;
    std::unique_ptr<EraserTool> _eraserTool;
    std::unique_ptr<PaintBucketTool> _paintBucketTool;
    std::unique_ptr<InkBottleTool> _inkBottleTool;
    std::unique_ptr<EyedropperTool> _eyedropperTool;
    QAction* _paintBucketAction = nullptr;
    QAction* _inkBottleAction = nullptr;

    /// The edit-in-place breadcrumb above the stage: the scene, then each
    /// symbol entered. Clicking a level steps back out to it.
    QWidget* _breadcrumb = nullptr;

    void rebuildBreadcrumb();

    /// Redraws the fill and stroke swatches after the style changed from
    /// somewhere other than the swatch buttons, such as the eyedropper.
    void refreshStyleSwatches();

    /// The toolbox's own stroke and fill swatches, under the tools as
    /// Animate places them.
    QToolButton* _toolboxStrokeSwatch = nullptr;
    QToolButton* _toolboxFillSwatch = nullptr;

    /// Opens the color dialog for the stroke or the fill and applies the
    /// choice everywhere the color shows.
    void chooseStyleColor(bool stroke);
    std::unique_ptr<PenTool> _penTool;
    std::unique_ptr<PencilTool> _pencilTool;
    std::unique_ptr<TextTool> _textTool;
    std::unique_ptr<SubselectionTool> _subselectionTool;
    fla::DrawingStyle _drawingStyle;
    PropertiesPanel* _propertiesPanel = nullptr;

    /// Which toolbox icon stands for \a tool, for the Properties panel's
    /// Tool tab.
    ToolIcons::Tool iconForTool(const Tool* tool) const;

    /// True while one of the tree and the stage is being brought in line
    /// with the other, so the change does not bounce back.
    bool _syncingSelection = false;
    QAction* _selectAllAction = nullptr;
    QAction* _deselectAllAction = nullptr;
    QAction* _cutAction = nullptr;
    QAction* _copyAction = nullptr;
    QAction* _pasteAction = nullptr;
    QAction* _pasteInPlaceAction = nullptr;
    QAction* _duplicateAction = nullptr;
    QAction* _groupAction = nullptr;
    QAction* _smoothAction = nullptr;
    QAction* _straightenAction = nullptr;
    QAction* _optimizeAction = nullptr;
    QAction* _linesToFillsAction = nullptr;
    QAction* _expandFillAction = nullptr;
    QAction* _softenFillAction = nullptr;
    /// Timeline commands on a right-click in the frames.
    QMenu* _framesMenu = nullptr;
    QAction* _ungroupAction = nullptr;
    QAction* _breakApartAction = nullptr;
    QAction* _convertToSymbolAction = nullptr;
    QAction* _arrangeActions[4] = {nullptr, nullptr, nullptr, nullptr};

    /// Copies of the shapes last cut or copied. Only shapes can be copied so
    /// far, since nothing else can be cloned yet.
    std::vector<std::unique_ptr<fla::Shape>> _clipboard;

    /// The shapes currently selected, in selection order.
    std::vector<fla::Shape*> selectedShapes() const;

    /// Adds copies of \a sources to the current frame as one undo step and
    /// selects them. With \a centerInView the copies are moved so that they
    /// sit in the middle of the view, which is where Animate pastes.
    void placeCopies(const std::vector<const fla::Shape*>& sources, const char* name,
        bool centerInView);
    QAction* _undoAction = nullptr;
    QAction* _redoAction = nullptr;
    /// Display name of the open file, without the modified marker.
    QString _documentName;
    /// Where the document was last saved or loaded from, for File > Save.
    QString _documentPath;
    Player* _player;
    QSplitter* _mainSplitter;
    QSplitter* _viewSplitter;
    DocumentView* _documentView;
    PhoenixView* _phoenixView;
    TimelineView* _timelineView;
    QMenu* _recentFilesMenu;

    QStringList _recentFiles;
    QString _lastDirectory;
    bool _highQualityAntiAliasing = true;
};
