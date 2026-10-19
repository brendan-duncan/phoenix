#include "stage_view.h"

#include <cmath>

namespace fla::stageview {

namespace {

/// \a first, then \a second.
Affine then(const Affine& first, const Affine& second)
{
    Affine out;
    out.a = second.a * first.a + second.c * first.b;
    out.b = second.b * first.a + second.d * first.b;
    out.c = second.a * first.c + second.c * first.d;
    out.d = second.b * first.c + second.d * first.d;
    out.tx = second.a * first.tx + second.c * first.ty + second.tx;
    out.ty = second.b * first.tx + second.d * first.ty + second.ty;
    return out;
}

Affine translation(double x, double y)
{
    Affine out;
    out.tx = x;
    out.ty = y;
    return out;
}

} // namespace

void Affine::map(double x, double y, double& outX, double& outY) const
{
    outX = a * x + c * y + tx;
    outY = b * x + d * y + ty;
}

bool Affine::inverted(Affine& inverse) const
{
    const double determinant = a * d - b * c;
    if (!std::isfinite(determinant) || std::fabs(determinant) < 1.0e-12)
    {
        inverse = Affine();
        return false;
    }
    inverse.a = d / determinant;
    inverse.b = -b / determinant;
    inverse.c = -c / determinant;
    inverse.d = a / determinant;
    inverse.tx = -(inverse.a * tx + inverse.c * ty);
    inverse.ty = -(inverse.b * tx + inverse.d * ty);
    return true;
}

Affine stageToView(const Placement& placement)
{
    Affine transform;
    // Mirrored first, about the stage's middle, so the turn after it still
    // goes clockwise on screen; the pivot is mirrored with the art so the
    // same point of it stays put.
    double pivotX = placement.pivotX;
    if (placement.mirrored)
    {
        transform.a = -1.0;
        transform.tx = placement.stageWidth;
        pivotX = placement.stageWidth - pivotX;
    }
    if (placement.rotation != 0.0)
    {
        // Turned about the pivot, inside the pan, so panning stays in screen
        // directions.
        const double radians = placement.rotation * 3.14159265358979323846 / 180.0;
        Affine turn;
        turn.a = std::cos(radians);
        turn.b = std::sin(radians);
        turn.c = -turn.b;
        turn.d = turn.a;
        transform = then(transform, translation(-pivotX, -placement.pivotY));
        transform = then(transform, turn);
        transform = then(transform, translation(pivotX, placement.pivotY));
    }
    Affine scale;
    scale.a = scale.d = placement.zoom;
    transform = then(transform, scale);
    // The stage sits centered in whatever space the view has, so the
    // centering offset is part of the mapping.
    const double centerX = (placement.viewWidth - placement.stageWidth * placement.zoom) / 2.0;
    const double centerY = (placement.viewHeight - placement.stageHeight * placement.zoom) / 2.0;
    return then(transform, translation(placement.panX + centerX, placement.panY + centerY));
}

uint8_t luminance(uint8_t red, uint8_t green, uint8_t blue)
{
    return static_cast<uint8_t>((299u * red + 587u * green + 114u * blue + 500u) / 1000u);
}

void grayscale(uint32_t* pixels, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        const uint32_t pixel = pixels[i];
        const uint32_t gray = luminance(static_cast<uint8_t>(pixel >> 16), static_cast<uint8_t>(pixel >> 8),
            static_cast<uint8_t>(pixel));
        pixels[i] = (pixel & 0xFF000000u) | (gray << 16) | (gray << 8) | gray;
    }
}

} // namespace fla::stageview
