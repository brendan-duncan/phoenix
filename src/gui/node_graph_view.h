#pragma once

#include "../edit/node_graph.h"

#include <QHash>
#include <QImage>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QWidget>

#include <functional>
#include <optional>

class QLineEdit;
class QVariantAnimation;

/// The node graph drawn and worked on: nodes with their category's color,
/// a picture of what each gives, and their inputs and output; links as
/// curves; backdrops behind; a minimap in the corner. The graph it shows is
/// a copy: each change it makes is handed out (edited) for the window to
/// make a step to undo, and set() puts back what the document holds.
///
/// Mouse: drag a node to move it (it lines up with others as it nears
/// them), drag from an output to an input (or from an input, or an input's
/// link to pick it up) to link them, drop a link on empty space to add a
/// node there linked to it, drop a node onto a link to put it in the link,
/// drag on empty space to select, the middle or right button (or Space) to
/// pan, the wheel to zoom. Keys: Tab adds a node, Delete deletes, D
/// bypasses, H hides pictures, V shows a node on the stage, F frames the
/// selection, L lines nodes up, B puts a backdrop behind the selection,
/// Ctrl+C, Ctrl+V and Ctrl+D copy, paste and duplicate.
class NodeGraphView : public QWidget
{
    Q_OBJECT

public:
    explicit NodeGraphView(QWidget* parent = nullptr);
    ~NodeGraphView() override;

    /// Shows \a graph (null for none), keeping the selection and view.
    void set(const fla::nodegraph::Graph* graph);
    const fla::nodegraph::Graph& graph() const { return _graph; }
    bool hasGraph() const { return _hasGraph; }

    /// The pictures of the nodes, by id, and what is wrong with any.
    void setPictures(const QMap<int, QImage>& pictures, const QMap<int, QString>& problems);
    /// The stage's shape, for the pictures' frames.
    void setStageAspect(double aspect);
    /// The node shown on the stage (0 for none).
    void setViewed(int node);
    int viewed() const { return _viewed; }

    QList<int> selectedNodes() const;
    /// The one node selected, or 0.
    int currentNode() const;
    void selectNode(int id);

    /// Frames the selection, or everything when nothing is selected.
    void frame(bool animate = true);
    void frameAll(bool animate = true);
    /// Lines up the selected nodes (all, when none is selected) in columns
    /// by how they link.
    void tidy();
    /// Opens the menu of nodes to add at the cursor (or the middle).
    void addNodeMenu();
    void setShowPictures(bool show);
    bool showPictures() const { return _showPictures; }
    void setShowMinimap(bool show);
    bool showMinimap() const { return _showMinimap; }

    /// The view's place and zoom, kept per timeline by the panel.
    struct View
    {
        QPointF pan;
        double zoom = 1.0;
    };
    View view() const { return {_pan, _zoom}; }
    void setView(const View& view);

    /// Copies the selected nodes, as text on the clipboard.
    void copy();
    void paste();
    void duplicate();
    void deleteSelection();
    void toggleBypass();
    void toggleCollapsed();
    void backdropSelection();

signals:
    /// The graph changed here: \a name is the step to undo; \a merge folds
    /// it into the step before when that had the same name (a drag).
    void edited(const fla::nodegraph::Graph& graph, const QString& name, bool merge);
    void selectionChanged();
    /// Show \a node on the stage (0 for the output).
    void viewRequested(int node);
    /// A Layer node double-clicked: its layer picked in the timeline.
    void layerRequested(const QString& layer);
    /// The empty view's button: a graph for this timeline.
    void createRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;

private:
    /// What is under a point.
    struct Hit
    {
        enum Kind
        {
            None,
            Node,
            Header,
            Output,
            Input,
            Link,
            Backdrop,
            BackdropTitle,
            BackdropCorner,
            Eye,
            Minimap
        };
        Kind kind = None;
        int node = 0;
        int input = -1;
        int link = -1;
    };
    Hit hitAt(const QPointF& screen) const;

    // Geometry, in graph units.
    QRectF nodeRect(const fla::nodegraph::Node& node) const;
    double pictureHeight() const;
    QRectF pictureRect(const fla::nodegraph::Node& node) const;
    QPointF outputPin(const fla::nodegraph::Node& node) const;
    QPointF inputPin(const fla::nodegraph::Node& node, int input) const;
    QRectF eyeRect(const fla::nodegraph::Node& node) const;
    QRectF graphBounds(bool selectedOnly) const;
    QPainterPath linkPath(const QPointF& from, const QPointF& to) const;
    QPointF toGraph(const QPointF& screen) const;
    QPointF toScreen(const QPointF& graph) const;
    QRectF minimapRect() const;
    QRectF minimapWorld() const;
    bool minimapShown() const;

    // Drawing.
    void drawGrid(QPainter& painter);
    void drawBackdrops(QPainter& painter);
    void drawLinks(QPainter& painter);
    void drawNode(QPainter& painter, const fla::nodegraph::Node& node);
    void drawMinimap(QPainter& painter);
    void drawEmpty(QPainter& painter);

    // Changes.
    void commit(const QString& name, bool merge = false);
    void addNode(const std::string& type, const QPointF& at);
    /// The node menu for a link dragged from \a node: adding one links it.
    void openSearch(const QPoint& screen, int fromNode, int toNode, int toInput);
    void connectTo(int from, int to, int input);
    void deleteLink(int index);
    void insertIntoLink(int node, int link);
    void startRename(int node);
    void animateTo(const QPointF& pan, double zoom);
    void scrollToMinimap(const QPointF& screen);

    fla::nodegraph::Graph _graph;
    bool _hasGraph = false;
    QMap<int, QImage> _pictures;
    QMap<int, QString> _problems;
    double _aspect = 16.0 / 9.0;
    int _viewed = 0;
    bool _showPictures = true;
    bool _showMinimap = true;

    QSet<int> _selected;
    int _selectedLink = -1;
    Hit _hover;
    QPointF _pan{40.0, 40.0};
    double _zoom = 1.0;
    QVariantAnimation* _animation = nullptr;

    // What the mouse is doing.
    enum class Drag
    {
        None,
        Pan,
        Nodes,
        Box,
        Link,
        Backdrop,
        BackdropSize,
        Minimap
    };
    Drag _drag = Drag::None;
    QPointF _pressScreen;
    QPointF _lastScreen;
    QPointF _pressGraph;
    bool _moved = false;
    /// Where each dragged node (and backdrop) started.
    QHash<int, QPointF> _startPositions;
    /// A link being drawn: from a node's output, or (backwards) from an
    /// input; where its loose end is.
    int _linkFrom = 0;
    int _linkTo = 0;
    int _linkInput = -1;
    QPointF _linkEnd;
    QRectF _box;
    /// Lines a dragged node has lined up with, to draw.
    std::optional<double> _guideX;
    std::optional<double> _guideY;
    /// A link a dragged node would go into.
    int _insertLink = -1;
    bool _spaceHeld = false;
    QLineEdit* _rename = nullptr;
    QWidget* _createButton = nullptr;
};
