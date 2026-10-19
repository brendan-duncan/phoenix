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
        Bind,
        AssetWarp,
        Rotate3D,
        Translate3D,
        Width,
        PaintBrush,
        DrawingGuides,
        /// A JSFL tool without an icon of its own.
        Script,
        /// Not a tool: the toggle that keeps a drawing its own object.
        ObjectDrawing,
        /// Not a tool: the toggle that copies strokes across the symmetry axis.
        Symmetry
    };

    /// The icon for a tool, at the sizes a toolbar is likely to ask for.
    static QIcon icon(Tool tool);
};
