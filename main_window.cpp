#include "main_window.h"
#include "phoenix_view.h"
#include "parse/fla_parser.h"

#include <QApplication>
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
#include <QSettings>
#include <QDir>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , _phoenixView(nullptr)
    , _documentView(nullptr)
    , _flaDocument(nullptr)
    , _recentFilesMenu(nullptr)
{
    setWindowTitle("Phoenix - FLA Viewer");

    QCoreApplication::setOrganizationName("Phoenix");
    QCoreApplication::setApplicationName("FLAViewer");

    loadSettings();

    setupUI();
    setupMenus();

    resize(1024, 768);

    // Auto-open last file if available
    if (!_recentFiles.isEmpty())
    {
        loadFLAFile(_recentFiles.first());
    }
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
    _documentView->setDocument(_flaDocument);

    if (_flaDocument)
    {
        setWindowTitle(QString("Phoenix - %1").arg(QFileInfo(filePath).baseName()));
        addToRecentFiles(filePath);
    }
    else
    {
        QMessageBox::warning(this, "Failed to Load FLA",
                           QString("Failed to load FLA file:\n%1\n\nError: %2")
                           .arg(filePath)
                           .arg(parser.errorString()));
    }
}

void MainWindow::setupUI()
{
    // Create the main layout
    QVBoxLayout* mainLayout = new QVBoxLayout();
    QWidget* centralWidget = new QWidget(this);
    centralWidget->setLayout(mainLayout);
    setCentralWidget(centralWidget);

    // Create the splitter
    _splitter = new QSplitter(Qt::Horizontal, this);
    mainLayout->addWidget(_splitter);

    // Create the document view (left panel)
    _documentView = new DocumentView(this);
    _documentView->setMinimumWidth(150);
    //_documentView->setMaximumWidth(400);

    // Create the phoenix view (right panel)
    _phoenixView = new PhoenixView(this);

    // Add widgets to splitter
    _splitter->addWidget(_documentView);
    _splitter->addWidget(_phoenixView);

    // Set splitter properties
    _splitter->setCollapsible(0, false);  // Don't collapse document view
    _splitter->setCollapsible(1, false);  // Don't collapse phoenix view
    _splitter->setStretchFactor(0, 0);    // Document view doesn't stretch
    _splitter->setStretchFactor(1, 1);    // Phoenix view stretches

    connect(_documentView, &DocumentView::visibilityChanged,
            this, &MainWindow::onVisibilityChanged);
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

    // Add Open Recent submenu
    _recentFilesMenu = fileMenu->addMenu("Open &Recent");
    updateRecentFilesMenu();

    fileMenu->addSeparator();

    QAction* exitAction = new QAction("&Exit", this);
    exitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
    connect(exitAction, &QAction::triggered, this, &MainWindow::quit);
    fileMenu->addAction(exitAction);

    // View menu
    QMenu* viewMenu = menuBar->addMenu("&View");

    QAction* viewDocumentAction = new QAction("View Document...", this);
    viewDocumentAction->setStatusTip("View the current document source");
    connect(viewDocumentAction, &QAction::triggered, this, &MainWindow::viewDocument);
    viewMenu->addAction(viewDocumentAction);

    viewMenu->addSeparator();

    QAction* showBoundsAction = new QAction("Show Bounding Boxes", this);
    showBoundsAction->setCheckable(true);
    showBoundsAction->setChecked(false);
    showBoundsAction->setShortcut(QKeySequence(Qt::Key_B));
    showBoundsAction->setStatusTip("Show element bounding boxes used for culling (Green=rendered, Red=culled, Blue=visible area)");
    connect(showBoundsAction, &QAction::toggled, _phoenixView, &PhoenixView::setShowBounds);
    viewMenu->addAction(showBoundsAction);
}

void MainWindow::openFile()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        tr("Open FLA File"), _lastDirectory, tr("FLA Files (*.fla;DOMDocument.xml);;All Files (*)"));

    if (!fileName.isEmpty())
    {
        _lastDirectory = QFileInfo(fileName).absolutePath();
        saveSettings();
        loadFLAFile(fileName);
    }
}

void MainWindow::quit()
{
    qApp->exit();
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

void MainWindow::onVisibilityChanged()
{
    // Trigger a repaint of the phoenix view when visibility changes
    if (_phoenixView)
    {
        _phoenixView->update();
    }
}

void MainWindow::openRecentFile()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (action)
    {
        QString filePath = action->data().toString();
        loadFLAFile(filePath);
    }
}

void MainWindow::updateRecentFilesMenu()
{
    _recentFilesMenu->clear();

    if (_recentFiles.isEmpty())
    {
        QAction* noRecentAction = _recentFilesMenu->addAction("No recent files");
        noRecentAction->setEnabled(false);
        return;
    }

    for (int i = 0; i < _recentFiles.size() && i < MAX_RECENT_FILES; ++i)
    {
        QString filePath = _recentFiles[i];
        QAction* action = _recentFilesMenu->addAction(QString("&%1 %2").arg(i + 1).arg(QFileInfo(filePath).fileName()));
        action->setData(filePath);
        connect(action, &QAction::triggered, this, &MainWindow::openRecentFile);
    }
}

void MainWindow::addToRecentFiles(const QString& filePath)
{
    if (_recentFiles.length() > 0 && _recentFiles[0] == filePath)
        return;

    _recentFiles.removeAll(filePath);
    _recentFiles.prepend(filePath);

    while (_recentFiles.size() > MAX_RECENT_FILES)
    {
        _recentFiles.removeLast();
    }

    updateRecentFilesMenu();
    saveSettings();
}

void MainWindow::loadSettings()
{
    QSettings settings;
    _recentFiles = settings.value("recentFiles").toStringList();
    _lastDirectory = settings.value("lastDirectory").toString();
}

void MainWindow::saveSettings()
{
    QSettings settings;
    settings.setValue("recentFiles", _recentFiles);
    settings.setValue("lastDirectory", _lastDirectory);
}
