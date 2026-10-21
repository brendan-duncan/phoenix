#pragma once

#include "../edit/state_graph.h"

#include <QWidget>

#include <functional>

/// The States panel's graph: each state a box, each transition an arrow
/// with what sets it off written on it, the starting state marked, and
/// "Any state" as a box of its own for the transitions from any state.
/// Click a box or an arrow to pick it; drag a box to move it; drag from a
/// box's dot to another box to make a transition.
class StatesView : public QWidget
{
    Q_OBJECT

public:
    explicit StatesView(QWidget* parent = nullptr);

    /// What is picked: a state, a transition, or nothing (-1).
    enum class Pick
    {
        None,
        State,
        Transition,
    };

    void setGraph(const fla::states::Graph& graph, const std::vector<QString>& ranges);
    /// Shows only the states of layer \a layer (0 for the timeline's own)
    /// and the transitions to them.
    void setLayer(int layer);
    void setPicked(Pick kind, int index);
    Pick pickedKind() const { return _kind; }
    int pickedIndex() const { return _index; }

    QSize sizeHint() const override { return QSize(420, 320); }

signals:
    void picked(StatesView::Pick kind, int index);
    /// A box was dragged to (x, y).
    void moved(int state, double x, double y);
    /// A drag from \a from's dot ended on \a to (-1 from Any state).
    void connectRequested(int from, int to);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    /// Whether state \a state is in the layer shown (Any state always is).
    bool shown(int state) const;
    QRectF box(int state) const;
    QRectF anyBox() const;
    QPointF handle(int state) const;
    int stateAt(const QPointF& at) const;
    int transitionAt(const QPointF& at) const;
    /// The ends of transition \a index's arrow and where its words go.
    bool arrow(int index, QPointF& from, QPointF& to, QPointF& middle) const;

    fla::states::Graph _graph;
    std::vector<QString> _ranges;
    int _layer = 0;
    Pick _kind = Pick::None;
    int _index = -1;

    int _dragging = -1;
    bool _connecting = false;
    QPointF _grab;
    QPointF _pointer;
    QPointF _dragOffset;
    bool _dragMoved = false;
    QPointF _origin;
};
