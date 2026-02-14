#pragma once

#include "phoenix_view.h"
#include "document_view.h"
#include "timeline_view.h"
#include "player.h"
#include "../data/fla_document.h"

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
    
    void openRecentFile();
    
    void quit();
    
    void viewDocument();
    
    void onVisibilityChanged();

private:
    void setupUI();

    void setupMenus();

    void updateRecentFilesMenu();

    void addToRecentFiles(const QString& filePath);

    void loadSettings();

    void saveSettings();

    QIcon createPhoenixIcon();

    static const int MAX_RECENT_FILES = 10;

    fla::FLADocument* _flaDocument;
    Player* _player;
    QSplitter* _mainSplitter;
    QSplitter* _viewSplitter;
    DocumentView* _documentView;
    PhoenixView* _phoenixView;
    TimelineView* _timelineView;
    QMenu* _recentFilesMenu;

    QStringList _recentFiles;
    QString _lastDirectory;
};
