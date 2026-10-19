#pragma once

#include <cstddef>
#include <cstdint>

namespace fla {

/// How the stage is shown in a view: where it sits, how far it is zoomed,
/// turned and mirrored. These are the viewer's choices, never the
/// document's: nothing here changes the art or what is exported. Free of Qt.
namespace stageview {

/// An affine mapping: x' = a x + c y + tx, y' = b x + d y + ty.
struct Affine
{
    double a = 1.0;
    double b = 0.0;
    double c = 0.0;
    double d = 1.0;
    double tx = 0.0;
    double ty = 0.0;

    void map(double x, double y, double& outX, double& outY) const;
    /// The mapping back; false (and the identity) when there is none.
    bool inverted(Affine& inverse) const;
};

struct Placement
{
    /// The view's size in pixels; the stage is centered in it before the
    /// pan moves it.
    double viewWidth = 0.0;
    double viewHeight = 0.0;
    double stageWidth = 0.0;
    double stageHeight = 0.0;
    double zoom = 1.0;
    double panX = 0.0;
    double panY = 0.0;
    /// Degrees clockwise on screen, about a pivot in the stage's
    /// coordinates.
    double rotation = 0.0;
    double pivotX = 0.0;
    double pivotY = 0.0;
    /// Flipped left to right about the stage's middle. The turn still goes
    /// clockwise on screen, about the same point of the art.
    bool mirrored = false;
};

/// From the stage's coordinates to the view's pixels.
Affine stageToView(const Placement& placement);

/// The brightness a color is seen at, 0 to 255 (Rec. 601 luma), which is
/// what a value check shows.
uint8_t luminance(uint8_t red, uint8_t green, uint8_t blue);

/// Pixels in 0xAARRGGBB words (premultiplied or not) turned to their gray,
/// alpha kept.
void grayscale(uint32_t* pixels, size_t count);

} // namespace stageview

} // namespace fla
