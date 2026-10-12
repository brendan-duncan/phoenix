#pragma once

#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QWidget>

#include <vector>

class QAction;
class QGridLayout;
class QVBoxLayout;

/// Animate's Tools panel: the tools in slots down one or two columns, a
/// slot with more than one tool showing the last one used and opening the
/// others on a long press, sections parted by lines, and the color
/// controls under them. Which tools sit in which slot, in what order, and
/// which are hidden is a layout that can be edited (Edit Toolbar) and
/// saved; a hidden tool still works by its key.
class Toolbox : public QWidget
{
    Q_OBJECT

public:
    /// A slot's tools, by id, the first shown; a slot of just "|" is a
    /// line between sections.
    using Slot = QStringList;

    struct Layout
    {
        std::vector<Slot> entries;
        QStringList hidden;
        int columns = 1;

        QJsonObject toJson() const;
        /// \a json read back, or an empty layout (no slots) when it is not
        /// one.
        static Layout fromJson(const QJsonObject& json);
    };

    explicit Toolbox(QWidget* parent = nullptr);

    /// Registers a tool under \a id (its name), with its action.
    void addTool(const QString& id, QAction* action);
    QAction* tool(const QString& id) const { return _tools.value(id); }
    /// Every tool's id, in the order they were registered.
    QStringList toolIds() const { return _order; }

    /// The layout Phoenix starts with; set once all tools are registered.
    void setDefaultLayout(const Layout& layout) { _default = layout; }
    const Layout& defaultLayout() const { return _default; }

    /// Arranges the tools as \a layout says. Tools it does not mention
    /// (added in a later Phoenix) go back where the default puts them.
    void setLayout(const Layout& layout);
    const Layout& layout() const { return _layout; }

    void setColumns(int columns);
    int columns() const { return _layout.columns; }

    /// The color controls and toggles under the tools.
    void setExtras(QWidget* extras);

signals:
    /// The layout or the columns changed.
    void layoutChanged();
    /// Edit Toolbar was chosen from the panel's menu.
    void editRequested();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void rebuild();
    /// \a layout with unknown ids dropped and missing ones put back.
    Layout completed(const Layout& layout) const;

    QMap<QString, QAction*> _tools;
    QStringList _order;
    Layout _default;
    Layout _layout;
    QWidget* _grid = nullptr;
    QVBoxLayout* _outer = nullptr;
    QWidget* _extras = nullptr;
};
