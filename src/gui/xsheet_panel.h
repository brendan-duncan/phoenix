#pragma once

#include <QWidget>

#include <functional>
#include <string>
#include <vector>

class QLabel;
class QScrollArea;

namespace fla {
class CommandStack;
class Frame;
class Layer;
class Timeline;
}

/// What the X-sheet works on.
struct XSheetLinks
{
    std::function<fla::CommandStack*()> commands;
    /// The timeline being edited (a symbol's, or the scene's).
    std::function<fla::Timeline*()> timeline;
    std::function<int()> currentFrame;
    std::function<void(int)> setCurrentFrame;
    std::function<void(const fla::Layer*)> setCurrentLayer;
    /// The frames changed: the stage and timeline catch up.
    std::function<void()> changed;
};

/// The exposure grid of the X-sheet: a column a layer, a row a frame.
class XSheetGrid : public QWidget
{
    Q_OBJECT

public:
    explicit XSheetGrid(XSheetLinks links, QWidget* parent = nullptr);

    /// Reads the timeline again.
    void refresh();

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    /// What a cell shows: the drawing exposed when a keyframe starts there,
    /// "x" for a blank one, nothing while the one before holds.
    struct Cell
    {
        QString text;
        bool key = false;
        bool held = false;
        bool tweened = false;
    };
    Cell cellAt(const fla::Layer& layer, int frame) const;
    /// The drawing number of a keyframe: a graphic symbol's frame, or for
    /// drawn art its place among the layer's drawn keyframes.
    QString drawingOf(const fla::Layer& layer, const fla::Frame& key) const;

    /// Exposes drawing \a number (0 for none) on the selected cell.
    void expose(int number);
    void clearCell();

    std::vector<fla::Layer*> columns() const;
    int frameCount() const;

    XSheetLinks _links;
    int _column = 0;
    int _row = 0;
    QString _typed;
};

/// The X-sheet: the timeline turned on its side, the drawings each
/// layer exposes frame by frame, typed in by number.
class XSheetPanel : public QWidget
{
    Q_OBJECT

public:
    explicit XSheetPanel(XSheetLinks links, QWidget* parent = nullptr);

    void refresh();

private:
    XSheetGrid* _grid = nullptr;
    QScrollArea* _scroll = nullptr;
};
