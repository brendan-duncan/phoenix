#include "script_window.h"
#include "modal.h"

#include "../play/movie_runtime.h"

#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextStream>

namespace {

const char* const kPathProperty = "scriptPath";
const char* const kFilter =
    "Scripts (*.jsfl *.py *.as *.js);;JSFL (*.jsfl);;Python (*.py);;ActionScript (*.as);;All files (*)";

QString extensionFor(CodeEditor::Language language)
{
    switch (language)
    {
    case CodeEditor::Language::Python:
        return "py";
    case CodeEditor::Language::ActionScript:
        return "as";
    default:
        return "jsfl";
    }
}

} // namespace

ScriptWindow::ScriptWindow(std::function<bool(const QString&)> run, const QString& folder, QWidget* parent)
    : QMainWindow(parent, Qt::Window)
    , _run(std::move(run))
    , _folder(folder)
{
    setObjectName("scriptWindow");
    setWindowTitle("Script Editor");
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(820, 620);

    _tabs = new QTabWidget(this);
    _tabs->setDocumentMode(true);
    _tabs->setTabsClosable(true);
    _tabs->setMovable(true);
    setCentralWidget(_tabs);
    connect(_tabs, &QTabWidget::tabCloseRequested, this, [this](int tab) { closeTab(tab); });
    connect(_tabs, &QTabWidget::currentChanged, this, [this](int tab) {
        updateTitle(tab);
        _status->clear();
    });

    _status = new QLabel(this);
    statusBar()->addWidget(_status, 1);

    QMenu* file = menuBar()->addMenu("&File");
    file->addAction("New &JSFL Script", QKeySequence::New, this,
        [this]() { newScript(CodeEditor::Language::JavaScript); });
    file->addAction("New &Python Script", this, [this]() { newScript(CodeEditor::Language::Python); });
    file->addAction("New &ActionScript File", this, [this]() { newScript(CodeEditor::Language::ActionScript); });
    file->addSeparator();
    file->addAction("&Open...", QKeySequence::Open, this, &ScriptWindow::askOpen);
    file->addAction("&Save", QKeySequence::Save, this, [this]() { save(_tabs->currentIndex(), false); });
    file->addAction("Save &As...", QKeySequence::SaveAs, this, [this]() { save(_tabs->currentIndex(), true); });
    file->addSeparator();
    file->addAction("&Close", QKeySequence::Close, this, [this]() { closeTab(_tabs->currentIndex()); });

    QMenu* edit = menuBar()->addMenu("&Edit");
    edit->addAction("&Undo", QKeySequence::Undo, this, [this]() {
        if (CodeView* view = current())
            view->editor()->undo();
    });
    edit->addAction("&Redo", QKeySequence::Redo, this, [this]() {
        if (CodeView* view = current())
            view->editor()->redo();
    });
    edit->addSeparator();
    // Shown with their keys; the editor itself answers them.
    QAction* find = edit->addAction("&Find...", this, [this]() {
        if (CodeView* view = current())
            view->showFind(false);
    });
    find->setShortcut(QKeySequence::Find);
    find->setShortcutContext(Qt::WidgetShortcut);
    QAction* replace = edit->addAction("R&eplace...", this, [this]() {
        if (CodeView* view = current())
            view->showFind(true);
    });
    replace->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
    replace->setShortcutContext(Qt::WidgetShortcut);
    QAction* goTo = edit->addAction("&Go to Line...", this, [this]() {
        if (CodeView* view = current())
            view->askGoToLine();
    });
    goTo->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    goTo->setShortcutContext(Qt::WidgetShortcut);
    QAction* comment = edit->addAction("&Toggle Comment", this, [this]() {
        if (CodeView* view = current())
            view->editor()->toggleComment();
    });
    comment->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Slash));
    comment->setShortcutContext(Qt::WidgetShortcut);

    QMenu* script = menuBar()->addMenu("&Script");
    script->addAction("&Run", QKeySequence(Qt::CTRL | Qt::Key_Return), this, &ScriptWindow::run)
        ->setStatusTip("Save the script and run it on the open document");
    script->addAction("&Check Syntax", this, &ScriptWindow::check)
        ->setStatusTip("Check an ActionScript file's syntax");
}

CodeView* ScriptWindow::current() const
{
    return qobject_cast<CodeView*>(_tabs->currentWidget());
}

QString ScriptWindow::pathOf(int tab) const
{
    QWidget* page = _tabs->widget(tab);
    return page ? page->property(kPathProperty).toString() : QString();
}

void ScriptWindow::setPath(int tab, const QString& path)
{
    if (QWidget* page = _tabs->widget(tab))
        page->setProperty(kPathProperty, path);
    updateTitle(tab);
}

void ScriptWindow::updateTitle(int tab)
{
    auto* view = qobject_cast<CodeView*>(_tabs->widget(tab));
    if (!view)
    {
        setWindowTitle("Script Editor");
        return;
    }
    const QString path = pathOf(tab);
    QString name = path.isEmpty() ? view->property("untitledName").toString() : QFileInfo(path).fileName();
    if (view->editor()->document()->isModified())
        name += "*";
    _tabs->setTabText(tab, name);
    _tabs->setTabToolTip(tab, path.isEmpty() ? name : QDir::toNativeSeparators(path));
    if (tab == _tabs->currentIndex())
        setWindowTitle(QString("%1 - Script Editor").arg(name));
}

void ScriptWindow::newScript(CodeEditor::Language language)
{
    auto* view = new CodeView(_tabs);
    view->editor()->setLanguage(language);
    view->setProperty("untitledName", QString("Untitled-%1.%2").arg(++_untitled).arg(extensionFor(language)));
    const int tab = _tabs->addTab(view, QString());
    connect(view->editor()->document(), &QTextDocument::modificationChanged, this, [this, view]() {
        updateTitle(_tabs->indexOf(view));
    });
    _tabs->setCurrentIndex(tab);
    updateTitle(tab);
    view->editor()->setFocus();
}

bool ScriptWindow::openFile(const QString& path)
{
    const QString wanted = QFileInfo(path).absoluteFilePath();
    for (int tab = 0; tab < _tabs->count(); ++tab)
    {
        if (QFileInfo(pathOf(tab)).absoluteFilePath() == wanted)
        {
            _tabs->setCurrentIndex(tab);
            return true;
        }
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        modal::warning(this, "Open Script", QString("%1 could not be read.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    const QString text = QString::fromUtf8(file.readAll());
    newScript(CodeEditor::languageFor(path));
    --_untitled;
    const int tab = _tabs->currentIndex();
    CodeEditor* editor = current()->editor();
    editor->setPlainText(text);
    editor->document()->setModified(false);
    setPath(tab, wanted);
    _folder = QFileInfo(wanted).absolutePath();
    return true;
}

void ScriptWindow::askOpen()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, "Open Script", _folder, kFilter);
    for (const QString& path : paths)
        openFile(path);
}

bool ScriptWindow::save(int tab, bool askForName)
{
    auto* view = qobject_cast<CodeView*>(_tabs->widget(tab));
    if (!view)
        return false;
    QString path = pathOf(tab);
    if (path.isEmpty() || askForName)
    {
        const QString suggested =
            path.isEmpty() ? QDir(_folder).filePath(view->property("untitledName").toString()) : path;
        path = QFileDialog::getSaveFileName(this, "Save Script", suggested, kFilter);
        if (path.isEmpty())
            return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        modal::warning(this, "Save Script", QString("%1 could not be written.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    file.write(view->editor()->toPlainText().toUtf8());
    if (!file.commit())
    {
        modal::warning(this, "Save Script", QString("%1 could not be written.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    view->editor()->document()->setModified(false);
    const CodeEditor::Language language = CodeEditor::languageFor(path);
    if (language != CodeEditor::Language::Plain)
        view->editor()->setLanguage(language);
    setPath(tab, QFileInfo(path).absoluteFilePath());
    _folder = QFileInfo(path).absolutePath();
    return true;
}

bool ScriptWindow::closeTab(int tab)
{
    auto* view = qobject_cast<CodeView*>(_tabs->widget(tab));
    if (!view)
        return true;
    if (view->editor()->document()->isModified())
    {
        _tabs->setCurrentIndex(tab);
        const auto answer = QMessageBox::question(this, "Script Editor",
            QString("Save the changes to %1?").arg(_tabs->tabText(tab).remove('*')),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !save(tab, false)))
            return false;
    }
    _tabs->removeTab(tab);
    view->deleteLater();
    return true;
}

void ScriptWindow::run()
{
    CodeView* view = current();
    if (!view)
        return;
    const int tab = _tabs->currentIndex();
    const CodeEditor::Language language = view->editor()->language();
    if (language != CodeEditor::Language::JavaScript && language != CodeEditor::Language::Python)
    {
        _status->setText("Only JSFL and Python scripts run here; ActionScript runs in Control > Test Movie.");
        return;
    }
    QString path = pathOf(tab);
    if (path.isEmpty())
    {
        // An untitled script runs from a file of its own that is not kept.
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        path = QDir(folder).filePath(view->property("untitledName").toString());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            _status->setText("The script could not be written to run.");
            return;
        }
        file.write(view->editor()->toPlainText().toUtf8());
    }
    else if (view->editor()->document()->isModified() && !save(tab, false))
        return;
    _status->setText(QString("Running %1...").arg(QFileInfo(path).fileName()));
    const bool ran = _run && _run(path);
    _status->setText(ran ? QString("Ran %1; anything it traced is in the Output panel.").arg(QFileInfo(path).fileName())
                         : QString("%1 stopped on an error; see the Output panel.").arg(QFileInfo(path).fileName()));
}

void ScriptWindow::check()
{
    CodeView* view = current();
    if (!view)
        return;
    if (view->editor()->language() != CodeEditor::Language::ActionScript)
    {
        _status->setText("Check Syntax is for ActionScript; a JSFL or Python script reports its errors when run.");
        return;
    }
    int line = 0;
    std::string message;
    if (fla::play::checkSyntax(view->editor()->toPlainText().toStdString(), line, message))
    {
        view->editor()->setErrorLine(0);
        _status->setText("No syntax errors");
        return;
    }
    view->editor()->setErrorLine(line, QString::fromStdString(message));
    if (line > 0)
        view->editor()->goToLine(line);
    _status->setText(line > 0 ? QString("Line %1: %2").arg(line).arg(QString::fromStdString(message))
                              : QString::fromStdString(message));
}

void ScriptWindow::closeEvent(QCloseEvent* event)
{
    for (int tab = _tabs->count() - 1; tab >= 0; --tab)
    {
        if (!closeTab(tab))
        {
            event->ignore();
            return;
        }
    }
    event->accept();
}
