#include "cep_panel.h"

#include "output_panel.h"
#include "script_runner.h"
#include "theme.h"
#include "web_view.h"

#include "../jsfl/panel_script.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QMainWindow>
#include <QMenu>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>

namespace {

// cep.fs error codes.
constexpr int kNoError = 0;
constexpr int kErrUnknown = 1;
constexpr int kErrInvalidParams = 2;
constexpr int kErrNotFound = 3;
constexpr int kErrCantRead = 4;
constexpr int kErrUnsupportedEncoding = 5;
constexpr int kErrCantWrite = 6;
constexpr int kErrNotFile = 8;
constexpr int kErrNotDirectory = 9;
constexpr int kErrFileExists = 10;

/// The name Phoenix gives as the host application, so panels made for it
/// find what they look for.
const char* const kHostId = "FLPR";
const char* const kHostVersion = "24.0";

QString json(const QJsonValue& value)
{
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    // A lone value, through an array to have JSON write it.
    const QString wrapped = QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact));
    return wrapped.mid(1, wrapped.size() - 2);
}

QJsonObject color(const QColor& c)
{
    return QJsonObject{{"red", c.red()}, {"green", c.green()}, {"blue", c.blue()}, {"alpha", c.alpha()}};
}

QJsonObject uiColor(const QColor& c)
{
    return QJsonObject{{"antialiasLevel", 0}, {"type", 1}, {"color", color(c)}};
}

QJsonObject fileResult(int error, const QJsonValue& data = QJsonValue())
{
    QJsonObject result{{"err", error}};
    if (!data.isUndefined() && !data.isNull())
        result["data"] = data;
    return result;
}

/// The folder holding CSXS/manifest.xml in an extension's files.
QString extensionRoot(const QString& package)
{
    if (QFileInfo::exists(QDir(package).filePath("CSXS/manifest.xml")))
        return package;
    QDirIterator it(package, {"manifest.xml"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QFileInfo info(it.next());
        if (info.dir().dirName().compare("CSXS", Qt::CaseInsensitive) == 0)
        {
            QDir root = info.dir();
            root.cdUp();
            return root.absolutePath();
        }
    }
    return package;
}

/// A host name for an extension's files in the web view's addresses.
QString hostName(const QString& id)
{
    QString out;
    for (const QChar c : id.toLower())
        out += c.isLetterOrNumber() && c.unicode() < 128 ? c : (c == '.' ? QChar('.') : QChar('-'));
    QStringList labels;
    for (QString label : out.split('.', Qt::SkipEmptyParts))
    {
        while (label.startsWith('-'))
            label.remove(0, 1);
        while (label.endsWith('-'))
            label.chop(1);
        if (!label.isEmpty())
            labels << label.left(63);
    }
    if (labels.isEmpty())
        labels << "extension";
    return labels.join('.') + ".phoenix-extension.example";
}

/// The file filters a cep.fs dialog was given: extensions without dots.
QString filters(const QJsonArray& types)
{
    QStringList patterns;
    for (const QJsonValue& type : types)
    {
        QString t = type.toString().trimmed();
        if (t.startsWith('.'))
            t.remove(0, 1);
        if (!t.isEmpty() && t != "*")
            patterns << "*." + t;
    }
    if (patterns.isEmpty())
        return QString();
    return "Files (" + patterns.join(' ') + ");;All Files (*)";
}

/// A menu from the panel menu XML (<Menu><MenuItem Id Label Enabled
/// Checked>, nested items for submenus, a Label of "---" for a line).
void fillXmlMenu(QMenu* menu, const QString& xml, const std::function<void(const QString&, const QString&)>& chosen)
{
    QXmlStreamReader reader(xml);
    QList<QMenu*> stack{menu};
    // An item with items inside is a submenu: known only when its first
    // child comes, so each item is held until then.
    struct Held
    {
        QString id, label;
        bool enabled = true, checkable = false, checked = false;
        bool opened = false;
        QMenu* sub = nullptr;
    };
    QList<Held> held;
    const auto addHeld = [&](Held& item) {
        QMenu* into = stack.last();
        if (item.label == "---" || item.label == "-")
        {
            into->addSeparator();
            return;
        }
        QAction* action = into->addAction(item.label);
        action->setEnabled(item.enabled);
        action->setCheckable(item.checkable);
        action->setChecked(item.checked);
        const QString id = item.id, label = item.label;
        QObject::connect(action, &QAction::triggered, menu, [chosen, id, label]() { chosen(id, label); });
    };
    while (!reader.atEnd())
    {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("MenuItem"))
        {
            // The parent item has a child: it is a submenu.
            if (!held.isEmpty() && !held.last().opened)
            {
                Held& parent = held.last();
                parent.opened = true;
                parent.sub = stack.last()->addMenu(parent.label);
                parent.sub->setEnabled(parent.enabled);
                stack << parent.sub;
            }
            Held item;
            const QXmlStreamAttributes a = reader.attributes();
            item.id = a.value("Id").toString();
            item.label = a.value("Label").toString();
            item.enabled = a.value("Enabled").toString() != "false";
            item.checkable = a.hasAttribute("Checkable") ? a.value("Checkable").toString() == "true"
                                                         : a.hasAttribute("Checked");
            item.checked = a.value("Checked").toString() == "true";
            held << item;
        }
        else if (reader.isEndElement() && reader.name() == QLatin1String("MenuItem") && !held.isEmpty())
        {
            Held item = held.takeLast();
            if (item.opened)
                stack.removeLast();
            else
                addHeld(item);
        }
    }
}

/// A menu from the JSON form: {"menu": [{"id", "label", "enabled",
/// "checkable", "checked", "menu": [...]}]}.
void fillJsonMenu(QMenu* menu, const QJsonArray& items, const std::function<void(const QString&, const QString&)>& chosen)
{
    for (const QJsonValue& value : items)
    {
        const QJsonObject item = value.toObject();
        const QString label = item.value("label").toString();
        if (label == "---" || label == "-")
        {
            menu->addSeparator();
            continue;
        }
        if (item.value("menu").isArray())
        {
            QMenu* sub = menu->addMenu(label);
            sub->setEnabled(item.value("enabled").toBool(true));
            fillJsonMenu(sub, item.value("menu").toArray(), chosen);
            continue;
        }
        QAction* action = menu->addAction(label);
        action->setEnabled(item.value("enabled").toBool(true));
        action->setCheckable(item.value("checkable").toBool(false));
        action->setChecked(item.value("checked").toBool(false));
        const QString id = item.value("id").toString();
        QObject::connect(action, &QAction::triggered, menu, [chosen, id, label]() { chosen(id, label); });
    }
}

/// The CSInterface shim each page starts with.
QString startScript()
{
    QFile file(":/cep/adobe_cep.js");
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

} // namespace

// ---------------------------------------------------------------------------
// CepPanel

CepPanel::CepPanel(CepPanels& panels, const extensions::Installed& extension, const extensions::Package::Panel& panel,
    QWidget* parent)
    : QWidget(parent)
    , _panels(panels)
    , _extension(extension)
    , _panel(panel)
    , _id(QString::fromStdString(panel.id))
{
    _root = extensionRoot(QDir(QString::fromStdString(extension.folder)).filePath("package"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    // The panel's own menu, when it sets one, behind a button at the top.
    _flyoutButton = new QToolButton(this);
    _flyoutButton->setText(QString::fromUtf8("\xe2\x89\xa1"));
    _flyoutButton->setToolTip("Panel menu");
    _flyoutButton->setAutoRaise(true);
    _flyoutButton->hide();
    connect(_flyoutButton, &QToolButton::clicked, this, &CepPanel::showFlyoutMenu);
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(0, 0, 2, 0);
    bar->addStretch(1);
    bar->addWidget(_flyoutButton);
    layout->addLayout(bar);

    _view = new WebView(this);
    _view->setProfileFolder(QDir(ScriptRunner::settingsFolder()).filePath("WebView"));
    _view->setStartScript(startScript());
    _view->onCall = [this](const QString& request) { return answer(request); };
    layout->addWidget(_view, 1);

    if (_panel.width > 0 && _panel.height > 0)
        resize(_panel.width, _panel.height);
    if (_panel.minWidth > 0 || _panel.minHeight > 0)
        setMinimumSize(_panel.minWidth, _panel.minHeight);
    if (_panel.maxWidth > 0 && _panel.maxHeight > 0)
        setMaximumSize(_panel.maxWidth, _panel.maxHeight);
}

CepPanel::~CepPanel() = default;

QString CepPanel::menuText() const
{
    return QString::fromStdString(_panel.menu);
}

QDockWidget* CepPanel::dock() const
{
    return qobject_cast<QDockWidget*>(parentWidget());
}

void CepPanel::start()
{
    if (_started)
        return;
    _started = true;
    const QString page = QDir(_root).relativeFilePath(
        QDir(QString::fromStdString(_extension.folder)).filePath("package/" + QString::fromStdString(_panel.mainPath)));
    _view->load(_root, page, hostName(QString::fromStdString(_extension.package.id)));
}

void CepPanel::deliver(const QJsonObject& event)
{
    if (!_started || !_listening.contains(event.value("type").toString()))
        return;
    _view->runScript("window.__phoenixCep && window.__phoenixCep.dispatch(" + json(event) + ");");
}

QString CepPanel::answer(const QString& request)
{
    const QJsonObject message = QJsonDocument::fromJson(request.toUtf8()).object();
    QJsonObject reply;
    reply["value"] = call(message.value("name").toString(), message.value("args").toArray());
    return json(reply);
}

QJsonValue CepPanel::call(const QString& name, const QJsonArray& args)
{
    const auto arg = [&args](int i) { return i < args.size() ? args.at(i) : QJsonValue(); };
    if (name.startsWith("fs."))
        return fileCall(name.mid(3), args);
    if (name == "getHostEnvironment")
        return json(hostEnvironment());
    if (name == "getHostCapabilities")
        return json(QJsonObject{{"EXTENDED_PANEL_MENU", true}, {"EXTENDED_PANEL_ICONS", false},
            {"DELEGATE_APE_ENGINE", false}, {"SUPPORT_HTML_EXTENSIONS", true}, {"DISABLE_FLASH_EXTENSIONS", true}});
    if (name == "getCurrentApiVersion")
        return json(QJsonObject{{"major", 11}, {"minor", 0}, {"micro", 0}});
    if (name == "getExtensionId")
        return _id;
    if (name == "getNetworkPreferences")
        return json(QJsonObject{{"proxyEnabled", false}, {"proxyType", 0}, {"proxyAddress", ""}, {"proxyPort", 0},
            {"exceptionList", ""}});
    if (name == "initResourceBundle")
        return resourceBundle();
    if (name == "getSystemPath")
    {
        const QString type = arg(0).toString();
        QString path;
        if (type == "userData")
        {
#ifdef Q_OS_WIN
            path = qEnvironmentVariable("APPDATA");
#else
            path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#endif
        }
        else if (type == "commonFiles")
        {
#ifdef Q_OS_WIN
            path = qEnvironmentVariable("CommonProgramFiles");
#else
            path = "/Library/Application Support";
#endif
        }
        else if (type == "myDocuments")
            path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        else if (type == "application")
            path = QCoreApplication::applicationDirPath();
        else if (type == "extension")
            path = _root;
        else if (type == "hostApplication")
            path = QCoreApplication::applicationFilePath();
        // As a file URL, which CSInterface decodes and takes the scheme off.
        return path.isEmpty() ? QString() : QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
    }
    if (name == "getExtensions")
    {
        QStringList wanted;
        const QJsonDocument ids = QJsonDocument::fromJson(arg(0).toString().toUtf8());
        for (const QJsonValue& id : ids.array())
            wanted << id.toString();
        QJsonArray list;
        for (const QPointer<CepPanel>& other : _panels.panels())
        {
            if (!other || (!wanted.isEmpty() && !wanted.contains(other->id())))
                continue;
            const extensions::Package::Panel& p = other->panel();
            list.append(QJsonObject{{"id", other->id()}, {"name", other->menuText()},
                {"mainPath", QDir(QString::fromStdString(other->extension().folder))
                                 .filePath("package/" + QString::fromStdString(p.mainPath))},
                {"basePath", other->root()}, {"windowType", QString::fromStdString(p.type)}, {"width", p.width},
                {"height", p.height}, {"minWidth", p.minWidth}, {"minHeight", p.minHeight}, {"maxWidth", p.maxWidth},
                {"maxHeight", p.maxHeight}, {"defaultExtensionDataXml", ""}, {"specialExtensionDataXml", ""},
                {"requiredRuntimeList", QJsonArray()}, {"isAutoVisible", true}, {"isPluginExtension", false}});
        }
        return json(list);
    }
    if (name == "evalScript")
    {
        evalScript(arg(0).toInt(), arg(1).toString());
        return QJsonValue();
    }
    if (name == "addEventListener")
    {
        _listening.insert(arg(0).toString());
        return QJsonValue();
    }
    if (name == "dispatchEvent")
    {
        QJsonObject event = arg(0).toObject();
        if (event.value("extensionId").toString().isEmpty())
            event["extensionId"] = _id;
        if (event.value("appId").toString().isEmpty())
            event["appId"] = kHostId;
        // Delivered once this call has returned, so a listener in this same
        // page is not called inside its own dispatchEvent.
        QPointer<CepPanels> panels(&_panels);
        QTimer::singleShot(0, &_panels, [panels, event]() {
            if (panels)
                panels->broadcast(event);
        });
        return QJsonValue();
    }
    if (name == "setWindowTitle")
    {
        if (QDockWidget* d = dock())
            d->setWindowTitle(arg(0).toString());
        return QJsonValue();
    }
    if (name == "isWindowVisible")
        return dock() ? dock()->isVisible() : isVisible();
    if (name == "getWindowTitle")
        return dock() ? dock()->windowTitle() : menuText();
    if (name == "closeExtension")
    {
        if (QDockWidget* d = dock())
            QTimer::singleShot(0, d, [d]() { d->close(); });
        return QJsonValue();
    }
    if (name == "requestOpenExtension")
    {
        const QString id = arg(0).toString();
        QPointer<CepPanels> panels(&_panels);
        QTimer::singleShot(0, &_panels, [panels, id]() {
            if (panels)
                panels->open(id);
        });
        return QJsonValue();
    }
    if (name == "resizeContent")
    {
        const int width = arg(0).toInt(), height = arg(1).toInt();
        if (QDockWidget* d = dock(); d && width > 0 && height > 0)
        {
            if (d->isFloating())
                d->resize(d->size() - size() + QSize(width, height));
            else
                resize(width, height);
        }
        return QJsonValue();
    }
    if (name == "setPanelFlyoutMenu")
    {
        setFlyoutMenu(arg(0).toString());
        return QJsonValue();
    }
    if (name == "updatePanelMenuItem")
    {
        // {"menuItemLabel", "enabled", "checked"}: changed in the menu's XML.
        const QJsonObject item = QJsonDocument::fromJson(arg(0).toString().toUtf8()).object();
        const QString label = item.value("menuItemLabel").toString();
        if (!label.isEmpty() && !_flyoutXml.isEmpty())
        {
            QString xml;
            QXmlStreamReader reader(_flyoutXml);
            QXmlStreamWriter writer(&xml);
            while (!reader.atEnd())
            {
                reader.readNext();
                if (reader.isStartElement() && reader.name() == QLatin1String("MenuItem") &&
                    reader.attributes().value("Label") == label)
                {
                    writer.writeStartElement("MenuItem");
                    for (const QXmlStreamAttribute& a : reader.attributes())
                    {
                        if (a.name() != QLatin1String("Enabled") && a.name() != QLatin1String("Checked"))
                            writer.writeAttribute(a.name().toString(), a.value().toString());
                    }
                    writer.writeAttribute("Enabled", item.value("enabled").toBool(true) ? "true" : "false");
                    writer.writeAttribute("Checked", item.value("checked").toBool(false) ? "true" : "false");
                }
                else if (!reader.isStartDocument() && !reader.isEndDocument())
                    writer.writeCurrentToken(reader);
            }
            _flyoutXml = xml;
        }
        return QJsonValue();
    }
    if (name == "showContextMenu")
    {
        const QString kind = arg(0).toString(), menu = arg(1).toString();
        QPointer<CepPanel> self(this);
        QTimer::singleShot(0, this, [self, kind, menu]() {
            if (self)
                self->showContextMenu(kind, menu);
        });
        return QJsonValue();
    }
    if (name == "openURLInDefaultBrowser")
    {
        const QUrl url(arg(0).toString());
        if (url.scheme() == "http" || url.scheme() == "https" || url.scheme() == "mailto")
        {
            QDesktopServices::openUrl(url);
            return fileResult(kNoError);
        }
        return fileResult(kErrInvalidParams);
    }
    return QJsonValue();
}

QJsonValue CepPanel::fileCall(const QString& name, const QJsonArray& args)
{
    const auto arg = [&args](int i) { return i < args.size() ? args.at(i) : QJsonValue(); };
    const auto localPath = [](const QString& path) {
        // Panels hand over plain paths, and now and then file: URLs.
        if (path.startsWith("file:", Qt::CaseInsensitive))
            return QUrl(path).toLocalFile();
        return path;
    };
    if (name == "showOpenDialog")
    {
        const bool multiple = arg(0).toBool(), directory = arg(1).toBool();
        const QString title = arg(2).toString(), start = localPath(arg(3).toString());
        QStringList chosen;
        if (directory)
        {
            const QString folder = QFileDialog::getExistingDirectory(this, title, start);
            if (!folder.isEmpty())
                chosen << folder;
        }
        else if (multiple)
            chosen = QFileDialog::getOpenFileNames(this, title, start, filters(arg(4).toArray()));
        else
        {
            const QString file = QFileDialog::getOpenFileName(this, title, start, filters(arg(4).toArray()));
            if (!file.isEmpty())
                chosen << file;
        }
        return fileResult(kNoError, QJsonArray::fromStringList(chosen));
    }
    if (name == "showSaveDialog")
    {
        QString start = localPath(arg(1).toString());
        if (!arg(3).toString().isEmpty())
            start = QDir(start).filePath(arg(3).toString());
        const QString file = QFileDialog::getSaveFileName(this, arg(0).toString(), start, filters(arg(2).toArray()));
        return fileResult(kNoError, file);
    }

    const QString path = localPath(arg(0).toString());
    if (path.isEmpty())
        return fileResult(kErrInvalidParams);
    const QFileInfo info(path);
    if (name == "readdir")
    {
        if (!info.exists())
            return fileResult(kErrNotFound);
        if (!info.isDir())
            return fileResult(kErrNotDirectory);
        return fileResult(kNoError,
            QJsonArray::fromStringList(QDir(path).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)));
    }
    if (name == "makedir")
        return fileResult(QDir().mkpath(path) ? kNoError : kErrCantWrite);
    if (name == "rename")
    {
        const QString to = localPath(arg(1).toString());
        if (!info.exists())
            return fileResult(kErrNotFound);
        if (QFileInfo::exists(to))
            return fileResult(kErrFileExists);
        return fileResult(QDir().rename(path, to) ? kNoError : kErrUnknown);
    }
    if (name == "stat")
    {
        if (!info.exists())
            return fileResult(kErrNotFound);
        return fileResult(kNoError, QJsonObject{{"file", info.isFile()}, {"directory", info.isDir()},
                                        {"mtime", double(info.lastModified().toMSecsSinceEpoch())}});
    }
    if (name == "readFile")
    {
        const QString encoding = arg(1).toString();
        if (encoding != "UTF-8" && encoding != "Base64")
            return fileResult(kErrUnsupportedEncoding);
        if (!info.exists())
            return fileResult(kErrNotFound);
        if (info.isDir())
            return fileResult(kErrNotFile);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return fileResult(kErrCantRead);
        const QByteArray bytes = file.readAll();
        return fileResult(kNoError, encoding == "Base64" ? QString::fromLatin1(bytes.toBase64()) : QString::fromUtf8(bytes));
    }
    if (name == "writeFile")
    {
        const QString encoding = arg(2).toString();
        if (encoding != "UTF-8" && encoding != "Base64")
            return fileResult(kErrUnsupportedEncoding);
        if (info.isDir())
            return fileResult(kErrNotFile);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return fileResult(kErrCantWrite);
        const QString data = arg(1).toString();
        const QByteArray bytes = encoding == "Base64" ? QByteArray::fromBase64(data.toLatin1()) : data.toUtf8();
        return fileResult(file.write(bytes) == bytes.size() ? kNoError : kErrCantWrite);
    }
    if (name == "chmod")
        return fileResult(info.exists() ? kNoError : kErrNotFound);
    if (name == "deleteFile")
    {
        if (!info.exists())
            return fileResult(kErrNotFound);
        if (info.isDir())
            return fileResult(kErrNotFile);
        return fileResult(QFile::remove(path) ? kNoError : kErrCantWrite);
    }
    return fileResult(kErrUnknown);
}

void CepPanel::evalScript(int callback, const QString& script)
{
    // Run once the page's call has returned; a script the window is busy
    // with (a command, another panel's call) finishes first.
    QPointer<CepPanel> self(this);
    QTimer::singleShot(0, this, [self, callback, script]() {
        if (!self)
            return;
        if (self->_panels.scripts().running() || QApplication::activeModalWidget())
        {
            QTimer::singleShot(50, self, [self, callback, script]() {
                if (self)
                    self->evalScript(callback, script);
            });
            return;
        }
        if (!self->_script)
        {
            self->_script = self->_panels.scripts().panelScript(self->menuText());
            std::string error;
            const std::string path = self->_panel.scriptPath.empty()
                                         ? std::string()
                                         : QDir(QString::fromStdString(self->_extension.folder))
                                               .filePath("package/" + QString::fromStdString(self->_panel.scriptPath))
                                               .toStdString();
            if (!self->_script->load(path, error) && !error.empty())
                self->_panels.scripts().output()->appendError(QString::fromStdString(error));
        }
        const jsfl::PanelScript::Result result = self->_panels.scripts().evaluate(*self->_script, script, self->menuText());
        if (!self || callback <= 0)
            return;
        const QString value = result.ok ? QString::fromStdString(result.value) : QString("EvalScript error.");
        self->_view->runScript("window.__phoenixCep && window.__phoenixCep.resolve(" + QString::number(callback) + "," +
                               json(value) + ");");
    });
}

void CepPanel::setFlyoutMenu(const QString& xml)
{
    _flyoutXml = xml;
    _flyoutButton->setVisible(!xml.trimmed().isEmpty());
}

void CepPanel::showFlyoutMenu()
{
    QMenu menu(this);
    QPointer<CepPanel> self(this);
    fillXmlMenu(&menu, _flyoutXml, [self](const QString& id, const QString& label) {
        if (!self)
            return;
        QJsonObject event{{"type", "com.adobe.csxs.events.flyoutMenuClicked"}, {"scope", "APPLICATION"},
            {"appId", kHostId}, {"extensionId", self->_id}, {"data", QJsonObject{{"menuId", id}, {"menuName", label}}}};
        self->deliver(event);
    });
    menu.exec(_flyoutButton->mapToGlobal(QPoint(0, _flyoutButton->height())));
}

void CepPanel::showContextMenu(const QString& kind, const QString& spec)
{
    QMenu menu(this);
    QPointer<CepPanel> self(this);
    const auto chosen = [self](const QString& id, const QString&) {
        if (self)
            self->_view->runScript("window.__phoenixCep && window.__phoenixCep.contextMenuClicked(" + json(id) + ");");
    };
    if (kind == "json")
        fillJsonMenu(&menu, QJsonDocument::fromJson(spec.toUtf8()).object().value("menu").toArray(), chosen);
    else
        fillXmlMenu(&menu, spec, chosen);
    if (!menu.isEmpty())
        menu.exec(QCursor::pos());
}

QJsonObject CepPanel::hostEnvironment() const
{
    const QColor panel = theme::color("window");
    const QColor bar = panel.darker(115);
    const QColor highlight = theme::color("accent");
    const QFont font = QApplication::font();
    QJsonObject skin{{"baseFontFamily", font.family()},
        {"baseFontSize", font.pointSizeF() > 0 ? font.pointSizeF() : 10.0},
        {"appBarBackgroundColor", uiColor(bar)}, {"panelBackgroundColor", uiColor(panel)},
        {"appBarBackgroundColorSRGB", uiColor(bar)}, {"panelBackgroundColorSRGB", uiColor(panel)},
        {"systemHighlightColor", color(highlight)}};
    const QString locale = QLocale::system().name();
    return QJsonObject{{"appName", kHostId}, {"appVersion", kHostVersion}, {"appLocale", locale},
        {"appUILocale", locale}, {"appId", kHostId}, {"isAppOnline", true}, {"appSkinInfo", skin}};
}

QString CepPanel::resourceBundle() const
{
    // locale/messages.properties, then the person's language over it.
    QJsonObject strings;
    const auto read = [&strings](const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return;
        QTextStream in(&file);
        in.setEncoding(QStringConverter::Utf8);
        while (!in.atEnd())
        {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith('#') || line.startsWith('!'))
                continue;
            const qsizetype equals = line.indexOf('=');
            if (equals <= 0)
                continue;
            strings[line.left(equals).trimmed()] = line.mid(equals + 1).trimmed();
        }
    };
    const QDir locale(QDir(_root).filePath("locale"));
    read(locale.filePath("messages.properties"));
    const QString name = QLocale::system().name();
    read(locale.filePath(name.left(2) + "/messages.properties"));
    read(locale.filePath(name + "/messages.properties"));
    return json(strings);
}

// ---------------------------------------------------------------------------
// CepPanels

CepPanels::CepPanels(QMainWindow& window, ScriptRunner& scripts)
    : QObject(&window)
    , _window(window)
    , _scripts(scripts)
{
    connect(theme::notifier(), &theme::Notifier::changed, this, [this]() {
        broadcast(QJsonObject{{"type", "com.adobe.csxs.events.ThemeColorChanged"}, {"scope", "APPLICATION"},
            {"appId", kHostId}, {"extensionId", ""}, {"data", ""}});
    });
    // What JSFL listeners hear, panels hear too.
    connect(&scripts, &ScriptRunner::eventDispatched, this, [this](const QString& type) {
        broadcast(QJsonObject{{"type", "com.adobe.events.flash." + type}, {"scope", "APPLICATION"},
            {"appId", kHostId}, {"extensionId", ""}, {"data", ""}});
    });
}

CepPanels::~CepPanels() = default;

void CepPanels::refresh()
{
    QSet<QString> installed;
    const std::vector<extensions::Installed> list = extensions::list(ScriptRunner::extensionsFolder().toStdString());
    for (const extensions::Installed& extension : list)
    {
        if (extension.package.kind != extensions::Package::Kind::Panels)
            continue;
        for (const extensions::Package::Panel& panel : extension.package.panels)
        {
            const QString id = QString::fromStdString(panel.id);
            installed.insert(id);
            bool known = false;
            for (const QPointer<CepPanel>& p : _panels)
                known = known || (p && p->id() == id);
            if (known)
                continue;
            auto* dock = new QDockWidget(QString::fromStdString(panel.menu), &_window);
            dock->setObjectName("extension:" + id);
            dock->setAllowedAreas(Qt::AllDockWidgetAreas);
            auto* view = new CepPanel(*this, extension, panel, dock);
            dock->setWidget(view);
            _window.addDockWidget(Qt::RightDockWidgetArea, dock);
            if (panel.type != "Panel")
                dock->setFloating(true);
            if (panel.width > 0 && panel.height > 0)
                dock->resize(panel.width, panel.height);
            dock->hide();
            QPointer<CepPanel> started(view);
            connect(dock, &QDockWidget::visibilityChanged, view, [started](bool shown) {
                if (shown && started)
                    started->start();
            });
            _panels << view;
            _docks << dock;
            // A panel that starts with Phoenix runs out of sight until it
            // is opened.
            for (const std::string& event : panel.startOn)
            {
                if (event == "com.adobe.csxs.events.ApplicationActivate" || event == "applicationActivate")
                {
                    QPointer<CepPanel> later(view);
                    QTimer::singleShot(0, this, [later]() {
                        if (later)
                            later->start();
                    });
                }
            }
        }
    }
    // Panels of extensions removed since go.
    for (int i = _panels.size() - 1; i >= 0; --i)
    {
        if (_panels[i] && installed.contains(_panels[i]->id()))
            continue;
        if (_docks[i])
            _docks[i]->deleteLater();
        _panels.removeAt(i);
        _docks.removeAt(i);
    }
}

void CepPanels::fillMenu(QMenu* menu)
{
    menu->clear();
    refresh();
    for (int i = 0; i < _panels.size(); ++i)
    {
        CepPanel* panel = _panels[i];
        QDockWidget* dock = _docks[i];
        if (!panel || !dock)
            continue;
        QAction* action = menu->addAction(panel->menuText());
        action->setCheckable(true);
        action->setChecked(dock->isVisible());
        const QString icon = QDir(QString::fromStdString(panel->extension().folder))
                                 .filePath("package/" + QString::fromStdString(panel->panel().icon));
        if (!panel->panel().icon.empty() && QFileInfo::exists(icon))
            action->setIcon(QIcon(icon));
        const QString id = panel->id();
        connect(action, &QAction::triggered, this, [this, id, dock](bool on) {
            if (!on && dock)
                dock->close();
            else
                open(id);
        });
    }
    if (_panels.isEmpty())
    {
        QAction* none = menu->addAction("No Extension Panels");
        none->setEnabled(false);
        none->setStatusTip("Install extensions with HTML panels through Commands > Manage Extensions");
    }
}

bool CepPanels::open(const QString& id)
{
    refresh();
    for (int i = 0; i < _panels.size(); ++i)
    {
        if (!_panels[i] || _panels[i]->id() != id || !_docks[i])
            continue;
        QDockWidget* dock = _docks[i];
        dock->show();
        dock->raise();
        if (dock->isFloating())
            dock->activateWindow();
        _panels[i]->start();
        return true;
    }
    return false;
}

void CepPanels::broadcast(const QJsonObject& event)
{
    const std::string type = event.value("type").toString().toStdString();
    for (const QPointer<CepPanel>& panel : _panels)
    {
        if (!panel)
            continue;
        // A panel waiting for this event starts; it hears the next one.
        const std::vector<std::string>& startOn = panel->panel().startOn;
        if (std::find(startOn.begin(), startOn.end(), type) != startOn.end())
            panel->start();
        panel->deliver(event);
    }
}
