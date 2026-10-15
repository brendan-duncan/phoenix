#pragma once

#include "../edit/motion_object.h"

#include <QWidget>

#include <functional>
#include <string>
#include <vector>

class PhoenixView;
class QComboBox;
class HotDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class HotSpinBox;

namespace fla {
class CommandStack;
class Frame;
}

/// What the Motion Editor works on.
struct MotionEditorLinks
{
    std::function<fla::CommandStack*()> commands;
    /// The object motion tween span being edited, or null.
    std::function<fla::Frame*()> span;
    /// The playhead, as a frame of the timeline.
    std::function<int()> currentFrame;
    std::function<double()> frameRate;
    /// The motion was changed: the stage catches up.
    std::function<void()> changed;
};

/// The curve of one property of a motion tween, over the frames of its span:
/// keyframes to drag (in time by whole frames, and in value), their Bezier
/// handles, a double-click to add one, Delete to take one away.
class MotionCurveView : public QWidget
{
    Q_OBJECT

public:
    explicit MotionCurveView(QWidget* parent = nullptr);

    /// Shows \a keys over a span of \a frames frames, \a timePerFrame
    /// TimeScale units each, with the playhead at \a current.
    void setCurve(const std::vector<fla::motion::PropertyKey>& keys, int frames, double timePerFrame, int current);

    /// New keyframes on the same span.
    void setKeys(const std::vector<fla::motion::PropertyKey>& keys);

    const std::vector<fla::motion::PropertyKey>& keys() const { return _keys; }
    int selectedKey() const { return _selected; }
    void selectKey(int index);

signals:
    /// While dragging (\a done false) and when let go (\a done true).
    void keysEdited(bool done);
    void selectionChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QRectF plot() const;
    QPointF toScreen(double time, double value) const;
    double timeAt(double x) const;
    double valueAt(double y) const;
    void fitRange();

    std::vector<fla::motion::PropertyKey> _keys;
    int _frames = 1;
    double _timePerFrame = 1.0;
    int _current = 0;
    double _low = 0.0;
    double _high = 1.0;
    int _selected = -1;
    /// What is being dragged: 0 none, 1 a keyframe, 2 its next handle, 3 its
    /// previous handle.
    int _dragging = 0;
};

/// The Motion Editor: the property curves of the selected (or current)
/// object motion tween, one at a time, with their keyframes and eases.
class MotionEditorPanel : public QWidget
{
    Q_OBJECT

public:
    MotionEditorPanel(MotionEditorLinks links, QWidget* parent = nullptr);

    /// Reads the tween again, after the selection, playhead or document
    /// changed.
    void refresh();

    /// Abandons a curve drag in progress, putting the keys back as they
    /// were: an undo or redo is about to change what it was editing.
    void cancelDrag();

private:
    void showProperty();
    /// Writes the curve's keys into the tween, live, or as an undo step when
    /// \a done.
    void writeKeys(bool done, const char* name);
    void addKeyAtPlayhead();
    void removeKey();
    void applyEase(int preset);
    void updateKeyFields();
    /// The properties listed: the transform's, the color effect's and the
    /// filters' of the object.
    void buildRows();

    struct Row
    {
        std::string label;
        std::string id;
        QString unit;
        double fallback = 0.0;
    };
    const Row& currentRow() const;
    std::vector<Row> _rows;

    MotionEditorLinks _links;
    fla::Frame* _frame = nullptr;
    /// The frame's kept children when the edit started.
    std::vector<std::string> _before;
    bool _editing = false;

    QLabel* _title = nullptr;
    QListWidget* _properties = nullptr;
    MotionCurveView* _curve = nullptr;
    HotSpinBox* _keyFrame = nullptr;
    HotDoubleSpinBox* _keyValue = nullptr;
    QComboBox* _ease = nullptr;
    HotDoubleSpinBox* _spanEase = nullptr;
    QPushButton* _add = nullptr;
    QPushButton* _remove = nullptr;
    bool _updating = false;
};
