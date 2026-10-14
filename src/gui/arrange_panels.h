#pragma once

#include <QWidget>

#include <functional>
#include <vector>

class PhoenixView;
class QCheckBox;
class QLabel;
class HotDoubleSpinBox;
class QRadioButton;

namespace fla {
class CommandStack;
class Element;
class Selection;
}

/// What the Align, Transform and Info panels work on.
struct ArrangeLinks
{
    PhoenixView* view = nullptr;
    fla::Selection* selection = nullptr;
    std::function<fla::CommandStack*()> commands;
    /// The selected elements, bottom first.
    std::function<std::vector<fla::Element*>()> elements;
    /// The stage, in the space the selection is in.
    std::function<QRectF()> stage;
    /// Something was moved: the stage and panels catch up.
    std::function<void()> changed;
    /// Duplicate the selection (for Duplicate and Transform).
    std::function<void()> duplicate;
};

/// Animate's Align panel (Ctrl+K): line the selection up by an edge or its
/// middle, spread it out evenly, give it one width or height, to each other
/// or to the stage.
class AlignPanel : public QWidget
{
    Q_OBJECT

public:
    AlignPanel(ArrangeLinks links, QWidget* parent = nullptr);

private:
    enum class Edge
    {
        Left,
        HCenter,
        Right,
        Top,
        VCenter,
        Bottom
    };
    void align(Edge edge);
    void distribute(Edge edge);
    void matchSize(bool width, bool height);
    void space(bool horizontal);

    ArrangeLinks _links;
    QCheckBox* _toStage = nullptr;
};

/// Animate's Transform panel (Ctrl+T): the selection's scale, rotation and
/// skew as numbers, set about its transformation point.
class TransformPanel : public QWidget
{
    Q_OBJECT

public:
    TransformPanel(ArrangeLinks links, QWidget* parent = nullptr);

    /// Shows the selection's values.
    void refresh();

private:
    void apply(bool duplicate);

    ArrangeLinks _links;
    HotDoubleSpinBox* _scaleX = nullptr;
    HotDoubleSpinBox* _scaleY = nullptr;
    QCheckBox* _constrain = nullptr;
    QRadioButton* _rotateChoice = nullptr;
    QRadioButton* _skewChoice = nullptr;
    HotDoubleSpinBox* _rotation = nullptr;
    HotDoubleSpinBox* _skewX = nullptr;
    HotDoubleSpinBox* _skewY = nullptr;
    bool _updating = false;
};

/// Animate's Info panel (Ctrl+I): the selection's size and place, and where
/// the cursor is over the stage with the color under it.
class InfoPanel : public QWidget
{
    Q_OBJECT

public:
    InfoPanel(ArrangeLinks links, QWidget* parent = nullptr);

    void refresh();
    void cursorMoved(const QPointF& documentPos, const QPoint& widgetPos);

private:
    ArrangeLinks _links;
    QLabel* _size = nullptr;
    QLabel* _place = nullptr;
    QLabel* _cursor = nullptr;
    QLabel* _color = nullptr;
    QLabel* _swatch = nullptr;
};
