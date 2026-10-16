#pragma once

#include "../edit/node_graph.h"
#include "node_graph_view.h"

#include <QHash>
#include <QImage>
#include <QMap>
#include <QStringList>
#include <QWidget>

#include <functional>

class QFormLayout;
class QLabel;
class QScrollArea;
class QSplitter;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace fla {
class CommandStack;
class Timeline;
}

struct NodeGraphLinks
{
    std::function<fla::CommandStack*()> commands;
    /// The timeline being edited, whose graph the panel shows.
    std::function<fla::Timeline*()> timeline;
    /// What it is called (the scene's or symbol's name).
    std::function<QString()> timelineName;
    /// The names of its layers, for Layer nodes.
    std::function<QStringList()> layerNames;
    /// The pictures of the graph's nodes at \a size, and their problems.
    std::function<QMap<int, QImage>(const fla::nodegraph::Graph& graph, const QSize& size, QMap<int, QString>* problems)>
        pictures;
    /// The stage's width over its height.
    std::function<double()> stageAspect;
    /// Shows node \a node on the stage in place of the output (0 for the
    /// output).
    std::function<void(int node)> view;
    /// After a change: the stage drawn again.
    std::function<void()> changed;
    /// The frame shown, on the timeline being edited: where keys go.
    std::function<int()> currentFrame;
    /// Makes the layer of that name the current one in the timeline.
    std::function<void(const QString& layer)> pickLayer;
};

/// The Node Graph panel: the graph of the timeline being edited, for
/// compositing its layers through nodes (see fla::nodegraph). A bar over
/// it turns the graph on and off and adds, frames and lines up nodes; the
/// graph in the middle (NodeGraphView); the settings of the node selected
/// at the side, each keyable on the current frame. Every change is a step
/// to undo, and a setting dragged is one step.
class NodeGraphPanel : public QWidget
{
    Q_OBJECT

public:
    explicit NodeGraphPanel(NodeGraphLinks links, QWidget* parent = nullptr);

    /// Shows the edited timeline's graph again (after an undo, a new
    /// document, another timeline).
    void refresh();
    /// The pictures drawn again (the frame moved, the art changed).
    void refreshPictures();

private:
    /// Puts \a graph (null to take it away) on the timeline as one step.
    void apply(const fla::nodegraph::Graph* graph, const QString& name, bool merge);
    void fillInspector();
    void setSetting(int node, const std::string& name, const std::string& value, bool merge);
    void toggleKey(int node, const std::string& name);
    void updateBar();

    NodeGraphLinks _links;
    NodeGraphView* _view = nullptr;
    QLabel* _title = nullptr;
    QToolButton* _enabled = nullptr;
    QToolButton* _add = nullptr;
    QToolButton* _frame = nullptr;
    QToolButton* _tidy = nullptr;
    QToolButton* _pictures = nullptr;
    QToolButton* _minimap = nullptr;
    QToolButton* _more = nullptr;
    QSplitter* _split = nullptr;
    QScrollArea* _inspectorScroll = nullptr;
    QWidget* _inspector = nullptr;
    QTimer* _pictureTimer = nullptr;
    fla::Timeline* _shown = nullptr;
    /// The node the inspector shows, kept while its settings change here.
    int _inspected = -1;
    bool _changing = false;
    QHash<const fla::Timeline*, NodeGraphView::View> _views;
};
