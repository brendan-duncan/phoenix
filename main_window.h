#pragma once

#include "data/fla_document.h"
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QString>

class PhoenixView;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
   
    void loadFLAFile(const QString& filePath);

private slots:
    void openFile();
    void viewDocument();

private:
    PhoenixView* _phoenixView;
    FLADocument* _flaDocument;
    void setupMenus();
};
