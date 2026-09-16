#pragma once

#include "phoenix_view.h"
#include "document_view.h"
#include "timeline_view.h"
#include "player.h"
#include "../data/fla_document.h"
#include "selection_tool.h"
#include "../edit/edit_context.h"
#include "../edit/selection.h"

#include <memory>

#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QString>
#include <QSplitter>
#include <QSettings>
#include <QStringList>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
   
    void loadFLAFile(const QString& filePath);

private slots:
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

private:
    void setupUI();

    void setupMenus();

    void setupToolBar();

    /// Refreshes the actions that only make sense with something selected.
    void updateSelectionState();

    /// Refreshes everything that depends on the undo history: the Edit menu
    /// entries and the modified marker in the title bar.
    void updateEditState();

    /// Rebuilds the title from the open file plus the modified marker.
    void updateWindowTitle();

    /// Offers to abandon unsaved changes. Returns false if the user cancels.
    bool confirmDiscardChanges();

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

    QIcon createPhoenixIcon();

    static const int MAX_RECENT_FILES = 10;

    fla::FLADocument* _flaDocument;
    fla::EditContext _editContext;
    fla::Selection _selection;
    std::unique_ptr<SelectionTool> _selectionTool;
    QAction* _selectAllAction = nullptr;
    QAction* _deselectAllAction = nullptr;
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
