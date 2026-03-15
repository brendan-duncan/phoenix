#pragma once

#include <string>

namespace fla {

class Element;

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

enum class TweenType
{
    None,
    Motion,
    Shape
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
        Element,
        Resource,
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

    bool isVisible() const
    {
        if (!visible)
            return false;
        if (parent)
            return parent->isVisible();
        return true;
    }

    bool isElementType() const
    {
        DOMType type = domType();
        return type == DOMType::BitmapInstance || type == DOMType::Shape || type == DOMType::Group ||
            type == DOMType::SymbolInstance || type == DOMType::OvalPrimitive ||
            type == DOMType::RectanglePrimitive || type == DOMType::StaticText;
    }

    const Element* findParentElement() const;

    bool isFillStyleType() const
    {
        DOMType type = domType();
        return type == DOMType::SolidColor || type == DOMType::LinearGradient || type == DOMType::RadialGradient;
    }

    bool isStrokeStyleType() const
    {
        DOMType type = domType();
        return type == DOMType::SolidStroke || type == DOMType::DashedStroke || type == DOMType::RaggedStroke ||
            type == DOMType::StippleStroke || type == DOMType::DottedStroke;
    }

    bool isSwatchType() const
    {
        DOMType type = domType();
        return type == DOMType::SolidSwatch || type == DOMType::LinearGradientSwatch || type == DOMType::RadialGradientSwatch;
    }

    bool isResource() const
    {
        DOMType type = domType();
        return type == DOMType::Bitmap || type == DOMType::Symbol;
    }

    template<typename T>
    const T* findAncestorOfType() const
    {
        const DOMElement* current = parent;
        while (current)
        {
            if (current->domType() == T::staticDomType())
            {
                return static_cast<const T*>(current);
            }
            current = current->parent;
        }
        return nullptr;
    }

    template<typename T>
    T* findAncestorOfType()
    {
        DOMElement* current = parent;
        while (current)
        {
            if (current->domType() == T::staticDomType())
            {
                return static_cast<T*>(current);
            }
            current = current->parent;
        }
        return nullptr;
    }
};

} // namespace fla
