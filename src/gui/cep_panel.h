#pragma once

#include "../parser/extension_package.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QWidget>

#include <memory>

class QDockWidget;
class QMainWindow;
class QMenu;
class QNetworkAccessManager;
class QNetworkReply;
class QToolButton;
class ScriptRunner;
class HtmlView;
class CepPanels;

namespace jsfl {
class PanelScript;
}

namespace cep {
class NativeHost;
}

namespace json {
class Value;
}

/// An HTML extension panel: the page its manifest names, shown in a web
/// view, with the host side of CSInterface behind it. evalScript runs JSFL
/// in a context of the panel's own, loaded with its ScriptPath file when
/// the first call comes; the file system, dialogs, menus and events are
/// answered here. Files, processes and, for a panel whose manifest asks for
/// Node.js, Node's modules go to a cep::NativeHost; web requests for Node's
/// http and https are made here.
class CepPanel : public QWidget
{
    Q_OBJECT

public:
    CepPanel(CepPanels& panels, const extensions::Installed& extension, const extensions::Package::Panel& panel,
        QWidget* parent = nullptr);
    ~CepPanel() override;

    const QString& id() const { return _id; }
    QString menuText() const;
    /// The folder holding the extension's CSXS folder.
    const QString& root() const { return _root; }
    const extensions::Package::Panel& panel() const { return _panel; }
    const extensions::Installed& extension() const { return _extension; }

    /// Loads the page, the first time the panel shows.
    void start();
    /// Hands \a event (type, scope, appId, extensionId, data) to the page's
    /// listeners for its type.
    void deliver(const QJsonObject& event);

private:
    /// A synchronous call from the page: {"name", "args"} in, {"value"} or
    /// {"error"} out, as JSON.
    QString answer(const QString& request);
    QJsonValue call(const QString& name, const QJsonArray& args);
    QJsonValue fileCall(const QString& name, const QJsonArray& args);
    void evalScript(int callback, const QString& script);
    void setFlyoutMenu(const QString& xml);
    void showFlyoutMenu();
    void showContextMenu(const QString& kind, const QString& menu);
    QJsonObject hostEnvironment() const;
    QString resourceBundle() const;
    QDockWidget* dock() const;
    /// Hands the page what processes wrote and how they ended.
    void flushNativeEvents();
    /// A web request for Node's http and https ("net.request",
    /// "net.abort"), its reply posted to the page as it comes.
    json::Value networkCall(const std::string& name, const json::Value& args);
    void postToPage(const json::Value& event);

    CepPanels& _panels;
    extensions::Installed _extension;
    extensions::Package::Panel _panel;
    QString _id;
    QString _root;
    HtmlView* _view = nullptr;
    QToolButton* _flyoutButton = nullptr;
    QString _flyoutXml;
    std::unique_ptr<jsfl::PanelScript> _script;
    std::unique_ptr<cep::NativeHost> _native;
    QNetworkAccessManager* _network = nullptr;
    QHash<int, QNetworkReply*> _requests;
    int _nextRequest = 1 << 20;
    QSet<QString> _listening;
    bool _started = false;
};

/// The HTML panels of the installed extensions: a dock for each, made
/// before the window restores its layout so a panel left open opens again,
/// the Window > Extensions menu, and the events passed between panels and
/// from Phoenix to them.
class CepPanels : public QObject
{
    Q_OBJECT

public:
    CepPanels(QMainWindow& window, ScriptRunner& scripts);
    ~CepPanels() override;

    /// Makes docks for the panels installed now, and lets go of those of
    /// extensions removed since.
    void refresh();
    /// The Window > Extensions menu: a line for each panel, added to
    /// \a menu; false when there are none.
    bool fillMenu(QMenu* menu);
    /// Opens the panel of id \a id; false when there is none.
    bool open(const QString& id);
    /// Hands \a event to every panel.
    void broadcast(const QJsonObject& event);

    ScriptRunner& scripts() { return _scripts; }
    QMainWindow& window() { return _window; }
    const QList<QPointer<CepPanel>>& panels() const { return _panels; }

private:
    QMainWindow& _window;
    ScriptRunner& _scripts;
    QList<QPointer<CepPanel>> _panels;
    QList<QPointer<QDockWidget>> _docks;
};
