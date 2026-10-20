#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>
#include <vector>

class QDockWidget;
class QMainWindow;
class QMenu;
class ScriptRunner;

namespace fla {
class FLADocument;
}
namespace jsfl {
class PanelScript;
}

/// The SWF panels: each movie in the Configuration folder's WindowSWF
/// played in a dock of its own, listed under Window > Extensions. A panel's
/// buttons reach Phoenix through MMExecute, whose JSFL runs in a context
/// the panel keeps, as an extension's HTML panel's evalScript does.
class SwfPanels : public QObject
{
    Q_OBJECT

public:
    SwfPanels(QMainWindow& window, ScriptRunner& scripts);
    ~SwfPanels() override;

    /// Where the panels are: Configuration/WindowSWF.
    static QString folder();

    /// Makes docks for the movies in the folder now, and lets go of those
    /// taken out since. A panel's movie is read when it is first shown.
    void refresh();
    /// A line for each panel, added to \a menu; false when there are none.
    bool fillMenu(QMenu* menu);
    /// Shows the panel of the movie at \a path; false when there is none.
    bool open(const QString& path);

private:
    struct Panel;
    /// Reads \a panel's movie and puts its player in the dock.
    void start(Panel& panel);

    QMainWindow& _window;
    ScriptRunner& _scripts;
    std::vector<std::shared_ptr<Panel>> _panels;
};
