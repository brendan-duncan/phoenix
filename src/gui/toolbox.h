#pragma once

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QStringList>
#include <QWidget>

#include <vector>

class QAction;
class QFrame;
class QToolButton;

/// The Tools panel: the tools in slots, a slot with more than one
/// tool showing the last one used and opening the others on a long press,
/// sections parted by lines, and the color controls after them. The slots
/// flow to fill the panel: down the left or right in as many columns as
/// its width holds, along the top or bottom in as many rows as its height
/// holds. Which tools sit in which slot, in what order, and which are
/// hidden is a layout that can be edited (Edit Toolbar) and saved; a
/// hidden tool still works by its key.
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
        /// How many columns wide the panel was left, down a side.
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
    /// The button that holds tool \a id, shown or in its flyout; null when
    /// the tool is hidden.
    QToolButton* buttonFor(const QString& id) const;

    /// The layout Phoenix starts with; set once all tools are registered.
    void setDefaultLayout(const Layout& layout) { _default = layout; }
    const Layout& defaultLayout() const { return _default; }

    /// Arranges the tools as \a layout says. Tools it does not mention
    /// (added in a later Phoenix) go back where the default puts them.
    void setLayout(const Layout& layout);
    const Layout& layout() const { return _layout; }

    /// The color controls and toggles after the tools, in order; a null
    /// entry is a line.
    void setExtras(const QList<QWidget*>& extras);

    /// Horizontal along the top or bottom of the window, vertical down a
    /// side. Floating, it follows the panel's shape.
    void setOrientation(Qt::Orientation orientation);
    void setFloating(bool floating);
    Qt::Orientation orientation() const { return _orientation; }

    /// The width the panel takes for \a columns columns.
    int widthFor(int columns) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    /// The layout changed, or the panel was resized to other columns.
    void layoutChanged();
    /// Edit Toolbar was chosen from the panel's menu.
    void editRequested();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Item
    {
        QWidget* widget = nullptr;
        QFrame* line = nullptr;
    };

    void rebuild();
    /// Puts every item in place for the panel's size; returns the length
    /// along the flow it takes, for \a across items across.
    int place(int across, bool move);
    int cell() const;
    QFrame* makeLine();
    /// \a layout with unknown ids dropped and missing ones put back.
    Layout completed(const Layout& layout) const;

    QMap<QString, QAction*> _tools;
    QStringList _order;
    Layout _default;
    Layout _layout;
    QList<QWidget*> _buttons;
    QList<QWidget*> _extras;
    std::vector<Item> _items;
    Qt::Orientation _orientation = Qt::Vertical;
    bool _floating = false;
};

/// A slim title bar for a dock, a row of grip dots: dragged it moves the
/// panel, double-clicked it floats or docks it. Its dots run along its
/// longer side, so it serves a title standing at a panel's end too.
class DockGrip : public QWidget
{
    Q_OBJECT

public:
    explicit DockGrip(QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(10, 10); }
    QSize minimumSizeHint() const override { return QSize(10, 10); }

protected:
    void paintEvent(QPaintEvent* event) override;
};
