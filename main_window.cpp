#include "main_window.h"
#include "phoenix_view.h"
#include "parse/fla_parser.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDebug>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QTextEdit>
#include <QDialog>
#include <QVBoxLayout>
#include <QKeySequence>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , _phoenixView(new PhoenixView(this))
    , _flaDocument(nullptr)
{
    // Set the PhoenixView as central widget
    setCentralWidget(_phoenixView);

    // Set window properties
    setWindowTitle("Phoenix - FLA Viewer");
    resize(800, 600);

    // Setup menus
    setupMenus();

    // Try to load a default FLA file
    //loadFLAFile("d:\\fla\\test_4_symbol.fla");
    loadFLAFile("d:\\fla\\MensWear_04.fla");
    //loadFLAFile("d:\\fla\\test_1.fla");
}

void MainWindow::loadFLAFile(const QString& filePath)
{
    if (_flaDocument)
    {
        delete _flaDocument;
        _flaDocument = nullptr;
    }

    FLAParser parser;
    _flaDocument = parser.parse(filePath.toStdString());
    _phoenixView->setDocument(_flaDocument);

    if (_flaDocument)
    {
        setWindowTitle(QString("Phoenix - %1").arg(QFileInfo(filePath).baseName()));
    }
    else
    {
        QMessageBox::warning(this, "Failed to Load FLA",
                           QString("Failed to load FLA file:\n%1\n\nError: %2")
                           .arg(filePath)
                           .arg(parser.errorString()));
    }
}

void MainWindow::setupMenus()
{
    // Create menu bar
    QMenuBar* menuBar = this->menuBar();
    
    // File menu
    QMenu* fileMenu = menuBar->addMenu("&File");
    
    QAction* openAction = new QAction("&Open...", this);
    openAction->setShortcut(QKeySequence::Open);
    openAction->setStatusTip("Open an FLA file");
    connect(openAction, &QAction::triggered, this, &MainWindow::openFile);
    fileMenu->addAction(openAction);
    
    // View menu
    QMenu* viewMenu = menuBar->addMenu("&View");
    
    QAction* viewDocumentAction = new QAction("View Document...", this);
    viewDocumentAction->setStatusTip("View the current document source");
    connect(viewDocumentAction, &QAction::triggered, this, &MainWindow::viewDocument);
    viewMenu->addAction(viewDocumentAction);
}

void MainWindow::openFile()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        tr("Open FLA File"), "", tr("FLA Files (*.fla);;All Files (*)"));
    
    if (!fileName.isEmpty())
    {
        loadFLAFile(fileName);
    }
}

void MainWindow::viewDocument()
{
    if (!_flaDocument || !_flaDocument->document)
    {
        QMessageBox::information(this, "No Document", "No FLA document is currently loaded.");
        return;
    }
    
    QDialog* dialog = new QDialog(this);
    dialog->setWindowTitle("Document Source");
    dialog->resize(600, 400);
    
    QTextEdit* textEdit = new QTextEdit(dialog);
    textEdit->setReadOnly(true);
    
    // For now, just show basic document info
    QString docText = QString::fromStdString(_flaDocument->document->source);

    textEdit->setPlainText(docText);
    
    QVBoxLayout* layout = new QVBoxLayout(dialog);
    layout->addWidget(textEdit);
    dialog->setLayout(layout);
    
    dialog->exec();
}
