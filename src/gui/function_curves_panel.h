#pragma once

#include "../edit/function_curves.h"

#include <QWidget>

#include <functional>
#include <vector>

class QComboBox;
class QLabel;

namespace fla {
class CommandStack;
class Layer;
class Timeline;
}

struct FunctionCurvesLinks
{
    std::function<fla::CommandStack*()> commands;
    /// The layer picked in the timeline, and the timeline it is on.
    std::function<fla::Layer*(fla::Timeline*&)> currentLayer;
    std::function<fla::curves::Context()> context;
    std::function<int()> currentFrame;
    std::function<void(int)> setCurrentFrame;
    std::function<void()> changed;
};

class FunctionGraph;

/// The Function Curves panel: one property of the current layer as a
/// curve over its frames, through its tweens, with a dot on each keyframe.
/// Drag a dot up or down, or double-click it to type a value; click
/// anywhere else to go to that frame.
class FunctionCurvesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit FunctionCurvesPanel(FunctionCurvesLinks links, QWidget* parent = nullptr);

    /// Reads the current layer and its curve again.
    void refresh();

private:
    friend class FunctionGraph;

    FunctionCurvesLinks _links;
    QComboBox* _property;
    QLabel* _layerName;
    FunctionGraph* _graph;
    const fla::Layer* _layer = nullptr;
};
