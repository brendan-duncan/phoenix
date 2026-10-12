#pragma once

#include <QIcon>

/// Icons for the toolbar, drawn rather than loaded.
///
/// Nine small glyphs are not worth a dependency on an image set or a resource
/// file to keep in step with the build, and drawing them means they scale to
/// whatever size the toolbar asks for.
class ToolIcons
{
public:
    enum class Tool
    {
        Selection,
        Subselection,
        FreeTransform,
        GradientTransform,
        Rectangle,
        Oval,
        Line,
        PolyStar,
        Pen,
        AddAnchor,
        DeleteAnchor,
        ConvertAnchor,
        Pencil,
        Hand,
        Zoom,
        RectanglePrimitive,
        OvalPrimitive,
        PaintBucket,
        InkBottle,
        Eyedropper,
        /// The Nib Brush.
        Brush,
        FluidBrush,
        InkBrush,
        Eraser,
        Lasso,
        PolygonLasso,
        MagicWand,
        Rotation,
        TimeScrub,
        Camera,
        Text,
        Bone,
        /// Not a tool: the toggle that keeps a drawing its own object.
        ObjectDrawing
    };

    /// The icon for a tool, at the sizes a toolbar is likely to ask for.
    static QIcon icon(Tool tool);
};
