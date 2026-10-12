#pragma once

#include <QWidget>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace fla {
class CommandStack;
class Layer;
class Timeline;
}

/// What the Layer Depth panel works on.
struct LayerDepthLinks
{
    std::function<fla::CommandStack*()> commands;
    /// The scene the camera looks at.
    std::function<fla::Timeline*()> scene;
    /// The stage's width, which the camera's view spans at the stage plane.
    std::function<double()> stageWidth;
    /// A depth changed: the stage catches up.
    std::function<void()> changed;
};

/// Animate's Layer Depth panel, as Harmony's top view of a multiplane: the
/// camera at the left looking right, the stage's plane, and each layer as a
/// bar at its depth, dragged nearer or farther. Near layers move more when
/// the camera pans.
class LayerDepthPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LayerDepthPanel(LayerDepthLinks links, QWidget* parent = nullptr);

    void refresh() { update(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    std::vector<fla::Layer*> layers() const;
    /// Where depth \a d is across the panel, and back.
    double xOf(double depth) const;
    double depthAt(double x) const;
    int rowTop(int index) const;

    LayerDepthLinks _links;
    fla::Layer* _dragging = nullptr;
    std::vector<std::pair<std::string, std::string>> _before;
};
