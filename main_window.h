#pragma once

#include "phoenix_view.h"
#include "document_view.h"
#include "data/fla_document.h"
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QString>
#include <QSplitter>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
   
    void loadFLAFile(const QString& filePath);

private slots:
    void openFile();

    void quit();

    void viewDocument();

    void onVisibilityChanged();

private:
    void setupUI();

    void setupMenus();

    FLADocument* _flaDocument;

    QSplitter* _splitter;
    DocumentView* _documentView;
    PhoenixView* _phoenixView;
};
