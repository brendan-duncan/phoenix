#pragma once

#include <string>

namespace fla {

enum class SymbolType
{
    MovieClip,
    Button,
    Graphic
};

enum class LoopType
{
    None,
    SingleFrame,
    Loop,
    PlayOnce,
    PingPong
};

class DOMElement
{
public:
    enum class DOMType
    {
        ActionScript,
        BitmapInstance,
        Bitmap,
        Document,
        Edge,
        Path,
        PathSegment,
        FLADocument,
        Folder,
        Frame,
        Group,
        Layer,
        LinearGradient,
        OvalPrimitive,
        PrinterSettings,
        PublishHistory,
        RadialGradient,
        RectanglePrimitive,
        ScriptList,
        Shape,
        SolidColor,
        StaticText,
        SolidStroke,
        DashedStroke,
        RaggedStroke,
        StippleStroke,
        DottedStroke,
        SwatchList,
        SolidSwatch,
        LinearGradientSwatch,
        RadialGradientSwatch,
        SymbolInstance,
        Symbol,
        SymbolList,
        Timeline,
    };

    bool visible = true;
    DOMElement* parent = nullptr;

    DOMElement(DOMElement* parent)
        : parent(parent)
    {}

    virtual ~DOMElement() = default;

    virtual std::string domTypeName() const = 0;

    virtual DOMType domType() const = 0;
};

} // namespace fla
