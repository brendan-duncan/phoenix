#include "phx_document.h"

#include "container.h"
#include "phx_codec.h"
#include "parallel.h"
#include "phx_schema.h"
#include "wire.h"

#include "../data/action_script.h"
#include "../data/bitmap.h"
#include "../data/bitmap_fill.h"
#include "../data/bitmap_instance.h"
#include "../data/document.h"
#include "../data/edge.h"
#include "../data/fla_document.h"
#include "../data/folder.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/linear_gradient.h"
#include "../data/morph_curves.h"
#include "../data/morph_segment.h"
#include "../data/morph_shape.h"
#include "../data/oval_primitive.h"
#include "../data/printer_settings.h"
#include "../data/publish_history.h"
#include "../data/radial_gradient.h"
#include "../data/raw_element.h"
#include "../data/rectangle_primitive.h"
#include "../data/script_list.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/static_text.h"
#include "../data/stroke_style.h"
#include "../data/swatch.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <thread>

namespace phx {

namespace s = schema;

namespace {

/// What reading one part needs and collects: the symbol instances, for
/// linking them to their symbols, and whether anything was damaged.
struct Context
{
    std::vector<fla::SymbolInstance*> instances;
    bool damaged = false;
};

void finish(const Reader& r, Context& context)
{
    if (r.failed())
        context.damaged = true;
}

void readKeyValue(const Reader& field, std::pair<std::string, std::string>& out)
{
    Reader r = field.message();
    while (r.next())
    {
        if (r.field() == s::KeyValue::Key)
            out.first = r.string();
        else if (r.field() == s::KeyValue::Value)
            out.second = r.string();
    }
}

void readXfl(const Reader& field, std::vector<std::pair<std::string, std::string>>& extraAttributes,
    std::vector<std::string>& presentAttributes, std::vector<std::string>* extraChildren = nullptr)
{
    Reader r = field.message();
    while (r.next())
    {
        switch (r.field())
        {
        case s::XflDetail::ExtraAttributes:
        {
            std::pair<std::string, std::string> kv;
            readKeyValue(r, kv);
            extraAttributes.push_back(std::move(kv));
            break;
        }
        case s::XflDetail::PresentAttributes:
            presentAttributes.push_back(r.string());
            break;
        case s::XflDetail::ExtraChildren:
            if (extraChildren)
                extraChildren->push_back(r.string());
            break;
        default:
            break;
        }
    }
}
void readXfl(const Reader& field, fla::DOMElement& element)
{
    readXfl(field, element.extraAttributes, element.presentAttributes);
}

fla::Transform readMatrix(const Reader& field)
{
    fla::Transform t;
    Reader r = field.message();
    while (r.next())
    {
        switch (r.field())
        {
        case s::Matrix::A: t.m11 = r.real(); break;
        case s::Matrix::B: t.m12 = r.real(); break;
        case s::Matrix::C: t.m21 = r.real(); break;
        case s::Matrix::D: t.m22 = r.real(); break;
        case s::Matrix::Tx: t.tx = r.real(); break;
        case s::Matrix::Ty: t.ty = r.real(); break;
        default: break;
        }
    }
    return t;
}

fla::Point readVector(const Reader& field)
{
    fla::Point p;
    Reader r = field.message();
    while (r.next())
    {
        if (r.field() == s::Vector2::X)
            p.x = r.real();
        else if (r.field() == s::Vector2::Y)
            p.y = r.real();
    }
    return p;
}

void readRectangle(const Reader& field, double& left, double& top, double& right, double& bottom)
{
    Reader r = field.message();
    while (r.next())
    {
        switch (r.field())
        {
        case s::Rectangle::Left: left = r.real(); break;
        case s::Rectangle::Top: top = r.real(); break;
        case s::Rectangle::Right: right = r.real(); break;
        case s::Rectangle::Bottom: bottom = r.real(); break;
        default: break;
        }
    }
}

void readRect(const Reader& field, fla::Rect& rect)
{
    readRectangle(field, rect.topLeft.x, rect.topLeft.y, rect.bottomRight.x, rect.bottomRight.y);
}

/// An enum's value when it is one this Phoenix knows (\a count of them);
/// otherwise the field is kept for writing back and \a fallback stands in.
template <typename E>
E readEnum(const Reader& r, int count, E fallback, std::string& unknown)
{
    if (r.uint() < static_cast<uint64_t>(count))
        return static_cast<E>(r.uint());
    r.keep(unknown);
    return fallback;
}

// ---------------------------------------------------------------------------
// Styles

template <typename Entry>
void readStops(const Reader& field, std::vector<Entry>& entries)
{
    Reader r = field.message();
    Entry entry{0.0, {0, 0, 0, 255}};
    while (r.next())
    {
        if (r.field() == s::GradientStop::Ratio)
            entry.ratio = r.real();
        else if (r.field() == s::GradientStop::Color)
            unpackColor(r.fixed32(), entry.color);
    }
    entries.push_back(entry);
}

fla::FillStyle* readFill(const Reader& field, fla::DOMElement* parent, Context& context)
{
    Reader r = field.message();
    // The kind first: it decides which object the other fields go on.
    fla::FillStyle* fill = nullptr;
    {
        Reader scan = field.message();
        while (scan.next() && !fill)
        {
            switch (scan.field())
            {
            case s::Fill::Solid:
            {
                auto* solid = new fla::SolidColor(parent);
                unpackColor(scan.fixed32(), solid->color);
                fill = solid;
                break;
            }
            case s::Fill::LinearGradient:
            {
                auto* linear = new fla::LinearGradient(parent);
                Reader g = scan.message();
                while (g.next())
                {
                    if (g.field() == s::Gradient::Stops)
                        readStops(g, linear->entries);
                }
                fill = linear;
                break;
            }
            case s::Fill::RadialGradient:
            {
                auto* radial = new fla::RadialGradient(parent);
                Reader g = scan.message();
                while (g.next())
                {
                    if (g.field() == s::Gradient::Stops)
                        readStops(g, radial->entries);
                    else if (g.field() == s::Gradient::FocalPointRatio)
                        radial->focalPointRatio = g.real();
                }
                fill = radial;
                break;
            }
            case s::Fill::Bitmap:
            {
                auto* bitmap = new fla::BitmapFill(parent);
                Reader b = scan.message();
                while (b.next())
                {
                    if (b.field() == s::BitmapFill::BitmapPath)
                        bitmap->bitmapPath = b.string();
                    else if (b.field() == s::BitmapFill::Clipped)
                        bitmap->bitmapIsClipped = b.boolean();
                }
                fill = bitmap;
                break;
            }
            default:
                break;
            }
        }
    }
    // A fill of a kind this Phoenix does not know shows as none: transparent.
    if (!fill)
    {
        auto* none = new fla::SolidColor(parent);
        none->color[3] = 0;
        fill = none;
    }
    while (r.next())
    {
        switch (r.field())
        {
        case s::Fill::Matrix: fill->transform = readMatrix(r); break;
        case s::Fill::SpreadMethod: fill->spreadMethod = r.string(); break;
        case s::Fill::InterpolationMethod: fill->interpolationMethod = r.string(); break;
        case s::Fill::Xfl: readXfl(r, *fill); break;
        case s::Fill::Solid:
        case s::Fill::LinearGradient:
        case s::Fill::RadialGradient:
        case s::Fill::Bitmap:
            break;
        default:
            r.keep(fill->phxUnknown);
            break;
        }
    }
    finish(r, context);
    return fill;
}

fla::StrokeStyle* readStroke(const Reader& field, fla::DOMElement* parent, Context& context)
{
    // The kind decides the class.
    int kind = 0;
    {
        Reader scan = field.message();
        while (scan.next())
        {
            if (scan.field() == s::Stroke::Kind)
                kind = scan.int32();
        }
    }
    fla::StrokeStyle* stroke = nullptr;
    switch (kind)
    {
    case 1: stroke = new fla::DashedStroke(parent); break;
    case 2: stroke = new fla::RaggedStroke(parent); break;
    case 3: stroke = new fla::StippleStroke(parent); break;
    case 4: stroke = new fla::DottedStroke(parent); break;
    case 5: stroke = new fla::HatchedStroke(parent); break;
    default: stroke = new fla::SolidStroke(parent); break;
    }
    Reader r = field.message();
    while (r.next())
    {
        switch (r.field())
        {
        case s::Stroke::Kind:
            // A kind this Phoenix does not know draws solid, and is kept.
            if (r.uint() > 5)
                r.keep(stroke->phxUnknown);
            break;
        case s::Stroke::Weight: stroke->weight = r.real(); break;
        case s::Stroke::ScaleMode: stroke->scaleMode = r.string(); break;
        case s::Stroke::SolidStyle: stroke->solidStyle = r.string(); break;
        case s::Stroke::Caps: stroke->caps = r.string(); break;
        case s::Stroke::Joints: stroke->joints = r.string(); break;
        case s::Stroke::MiterLimit: stroke->miterLimit = r.real(); break;
        case s::Stroke::PixelHinting: stroke->pixelHinting = r.boolean(); break;
        case s::Stroke::Dash1: stroke->dash1 = r.real(); break;
        case s::Stroke::Dash2: stroke->dash2 = r.real(); break;
        case s::Stroke::DotSpace: stroke->dotSpace = r.real(); break;
        case s::Stroke::StyleAttributes:
        {
            std::pair<std::string, std::string> kv;
            readKeyValue(r, kv);
            stroke->styleAttributes[kv.first] = kv.second;
            break;
        }
        case s::Stroke::ExtraChildren: stroke->extraChildren.push_back(r.string()); break;
        case s::Stroke::WidthProfile:
        {
            fla::StrokeStyle::WidthMarker marker;
            Reader m = r.message();
            while (m.next())
            {
                switch (m.field())
                {
                case s::WidthMarker::Position: marker.position = m.real(); break;
                case s::WidthMarker::Left: marker.left = m.real(); break;
                case s::WidthMarker::Right: marker.right = m.real(); break;
                case s::WidthMarker::Corner: marker.corner = m.boolean(); break;
                default: break;
                }
            }
            stroke->widthProfile.push_back(marker);
            break;
        }
        case s::Stroke::Fill:
            delete stroke->fill;
            stroke->fill = readFill(r, stroke, context);
            break;
        case s::Stroke::Xfl: readXfl(r, *stroke); break;
        default:
            r.keep(stroke->phxUnknown);
            break;
        }
    }
    finish(r, context);
    return stroke;
}

// ---------------------------------------------------------------------------
// Geometry

fla::Path* readPath(const Reader& field, fla::Edge* edge, Context& context)
{
    auto* path = new fla::Path(edge);
    std::vector<uint64_t> segments;
    std::vector<int64_t> coordinates;
    Reader r = field.message();
    while (r.next())
    {
        switch (r.field())
        {
        case s::Path::StyleIndex: path->styleIndex = static_cast<int>(r.sint()); break;
        case s::Path::LineStyleIndex: path->lineStyleIndex = static_cast<int>(r.sint()); break;
        case s::Path::FillStyleIndex: path->fillStyleIndex = static_cast<int>(r.sint()); break;
        case s::Path::Segments:
            r.varints([&segments](uint64_t v) { segments.push_back(v); });
            break;
        case s::Path::Coordinates:
            r.varints([&coordinates](uint64_t v) { coordinates.push_back(Reader::unzigzag(v)); });
            break;
        default:
            r.keep(path->phxUnknown);
            break;
        }
    }
    finish(r, context);
    size_t at = 0;
    int64_t x = 0;
    int64_t y = 0;
    path->segments.reserve(segments.size());
    for (const uint64_t segment : segments)
    {
        const uint32_t command = static_cast<uint32_t>(segment & 7);
        const size_t count = static_cast<size_t>(segment >> 3);
        if (command > static_cast<uint32_t>(fla::PathSegment::Command::Close) || at + count * 2 > coordinates.size())
        {
            context.damaged = true;
            break;
        }
        std::vector<fla::Point> points;
        points.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            x += coordinates[at++];
            y += coordinates[at++];
            points.emplace_back(fromUnits(x), fromUnits(y));
        }
        path->segments.push_back(new fla::PathSegment(static_cast<fla::PathSegment::Command>(command), points, path));
    }
    return path;
}

fla::Edge* readEdge(const Reader& field, fla::Shape* shape, Context& context)
{
    auto* edge = new fla::Edge(shape);
    Reader r = field.message();
    while (r.next())
    {
        switch (r.field())
        {
        case s::Edge::FillStyle0: edge->fillStyle0 = static_cast<int>(r.sint()); break;
        case s::Edge::FillStyle1: edge->fillStyle1 = static_cast<int>(r.sint()); break;
        case s::Edge::StrokeStyle: edge->strokeStyle = static_cast<int>(r.sint()); break;
        case s::Edge::Paths: edge->paths.push_back(readPath(r, edge, context)); break;
        case s::Edge::Xfl: readXfl(r, *edge); break;
        default: r.keep(edge->phxUnknown); break;
        }
    }
    finish(r, context);
    return edge;
}

void readShape(const Reader& field, fla::Shape* shape, Context& context)
{
    Reader r = field.message();
    while (r.next())
    {
        switch (r.field())
        {
        case s::Shape::Fills:
        case s::Shape::Strokes:
        {
            int index = 0;
            Reader entry = r.message();
            fla::FillStyle* fill = nullptr;
            fla::StrokeStyle* stroke = nullptr;
            while (entry.next())
            {
                if (entry.field() == s::IndexedStyle::Index)
                    index = entry.int32();
                else if (entry.field() == s::IndexedStyle::Style && r.field() == s::Shape::Fills && !fill)
                    fill = readFill(entry, shape, context);
                else if (entry.field() == s::IndexedStyle::Style && r.field() == s::Shape::Strokes && !stroke)
                    stroke = readStroke(entry, shape, context);
            }
            if (fill)
            {
                shape->fills.push_back(fill);
                if (index != 0)
                    shape->fillsMap[index] = fill;
            }
            if (stroke)
            {
                shape->strokes.push_back(stroke);
                if (index != 0)
                    shape->strokesMap[index] = stroke;
            }
            break;
        }
        case s::Shape::Edges: shape->edges.push_back(readEdge(r, shape, context)); break;
        case s::Shape::IsDrawingObject: shape->isDrawingObject = r.boolean(); break;
        case s::Shape::CubicEdges:
        {
            std::pair<size_t, std::string> record;
            Reader c = r.message();
            while (c.next())
            {
                if (c.field() == s::CubicRecord::BeforeEdge)
                    record.first = static_cast<size_t>(c.uint());
                else if (c.field() == s::CubicRecord::Xml)
                    record.second = c.string();
            }
            shape->cubicEdges.push_back(std::move(record));
            break;
        }
        case s::Shape::ReadEdgeCount: shape->readEdgeCount = static_cast<size_t>(r.uint()); break;
        default: r.keep(shape->phxUnknown); break;
        }
    }
    finish(r, context);
}

// ---------------------------------------------------------------------------
// Elements

void readColorTransform(const Reader& field, fla::ColorTransform& c)
{
    Reader r = field.message();
    while (r.next())
    {
        switch (r.field())
        {
        case s::ColorTransform::TintMultiplier: c.tintMultiplier = r.real(); break;
        case s::ColorTransform::TintColor: unpackColor(r.fixed32(), c.tintColor); break;
        case s::ColorTransform::Brightness: c.brightness = r.real(); break;
        case s::ColorTransform::RedMultiplier: c.redMultiplier = r.real(); break;
        case s::ColorTransform::GreenMultiplier: c.greenMultiplier = r.real(); break;
        case s::ColorTransform::BlueMultiplier: c.blueMultiplier = r.real(); break;
        case s::ColorTransform::AlphaMultiplier: c.alphaMultiplier = r.real(); break;
        case s::ColorTransform::RedOffset: c.redOffset = static_cast<int>(r.sint()); break;
        case s::ColorTransform::GreenOffset: c.greenOffset = static_cast<int>(r.sint()); break;
        case s::ColorTransform::BlueOffset: c.blueOffset = static_cast<int>(r.sint()); break;
        case s::ColorTransform::AlphaOffset: c.alphaOffset = static_cast<int>(r.sint()); break;
        default: break;
        }
    }
}

fla::Filter readFilter(const Reader& field, Context& context)
{
    fla::Filter f;
    Reader r = field.message();
    while (r.next())
    {
        switch (r.field())
        {
        case s::Filter::Type: f.type = readEnum(r, 7, fla::Filter::Type::Blur, f.phxUnknown); break;
        case s::Filter::Enabled: f.enabled = r.boolean(); break;
        case s::Filter::BlurX: f.blurX = r.real(); break;
        case s::Filter::BlurY: f.blurY = r.real(); break;
        case s::Filter::Quality: f.quality = r.int32(); break;
        case s::Filter::Angle: f.angle = r.real(); break;
        case s::Filter::Distance: f.distance = r.real(); break;
        case s::Filter::Strength: f.strength = r.real(); break;
        case s::Filter::Color: unpackColor(r.fixed32(), f.color); break;
        case s::Filter::HighlightColor: unpackColor(r.fixed32(), f.highlightColor); break;
        case s::Filter::ShadowColor: unpackColor(r.fixed32(), f.shadowColor); break;
        case s::Filter::Inner: f.inner = r.boolean(); break;
        case s::Filter::Knockout: f.knockout = r.boolean(); break;
        case s::Filter::HideObject: f.hideObject = r.boolean(); break;
        case s::Filter::BevelType: f.bevelType = r.string(); break;
        case s::Filter::Brightness: f.brightness = r.real(); break;
        case s::Filter::Contrast: f.contrast = r.real(); break;
        case s::Filter::Saturation: f.saturation = r.real(); break;
        case s::Filter::Hue: f.hue = r.real(); break;
        case s::Filter::Entries: readStops(r, f.entries); break;
        case s::Filter::Xfl: readXfl(r, f.extraAttributes, f.presentAttributes); break;
        default: r.keep(f.phxUnknown); break;
        }
    }
    finish(r, context);
    return f;
}

fla::Element* readElement(const Reader& field, fla::DOMElement* parent, Context& context);

/// The element of the kind field \a kind holds, made empty, with its kind's
/// fields read into it.
fla::Element* readKind(const Reader& kind, fla::DOMElement* parent, Context& context)
{
    switch (kind.field())
    {
    case s::Element::Shape:
    {
        auto* shape = new fla::Shape(parent);
        readShape(kind, shape, context);
        return shape;
    }
    case s::Element::Group:
    {
        auto* group = new fla::Group(parent);
        Reader r = kind.message();
        while (r.next())
        {
            if (r.field() == s::Group::Members)
            {
                if (fla::Element* member = readElement(r, group, context))
                    group->members.push_back(member);
            }
            else
            {
                r.keep(group->phxUnknown);
            }
        }
        finish(r, context);
        return group;
    }
    case s::Element::SymbolInstance:
    {
        auto* instance = new fla::SymbolInstance(parent);
        Reader r = kind.message();
        while (r.next())
        {
            switch (r.field())
            {
            case s::SymbolInstance::LibraryItemName: instance->libraryItemName = r.string(); break;
            case s::SymbolInstance::Name: instance->name = r.string(); break;
            case s::SymbolInstance::FirstFrame: instance->firstFrame = r.int32(); break;
            case s::SymbolInstance::SymbolType:
                instance->symbolType = readEnum(r, 3, fla::SymbolType::Graphic, instance->phxUnknown);
                break;
            case s::SymbolInstance::LoopType:
                instance->loopType = readEnum(r, 5, fla::LoopType::PlayOnce, instance->phxUnknown);
                break;
            case s::SymbolInstance::ColorTransform: readColorTransform(r, instance->colorTransform); break;
            case s::SymbolInstance::BlendMode: instance->blendMode = r.string(); break;
            case s::SymbolInstance::OtherSymbolType: instance->otherSymbolType = r.string(); break;
            case s::SymbolInstance::KeepColor: instance->keepColor = r.boolean(); break;
            case s::SymbolInstance::ExtraFilters: instance->extraFilters.push_back(r.string()); break;
            case s::SymbolInstance::Filters: instance->filters.push_back(readFilter(r, context)); break;
            default: r.keep(instance->phxUnknown); break;
            }
        }
        finish(r, context);
        context.instances.push_back(instance);
        return instance;
    }
    case s::Element::BitmapInstance:
    {
        auto* bitmap = new fla::BitmapInstance(parent);
        Reader r = kind.message();
        while (r.next())
        {
            if (r.field() == s::BitmapInstance::LibraryItemName)
                bitmap->libraryItemName = r.string();
            else
                r.keep(bitmap->phxUnknown);
        }
        finish(r, context);
        return bitmap;
    }
    case s::Element::Text:
    {
        auto* text = new fla::StaticText(parent);
        Reader r = kind.message();
        while (r.next())
        {
            switch (r.field())
            {
            case s::Text::Left: text->left = r.real(); break;
            case s::Text::Top: text->top = r.real(); break;
            case s::Text::Width: text->width = r.real(); break;
            case s::Text::Height: text->height = r.real(); break;
            case s::Text::AutoExpand: text->autoExpand = r.boolean(); break;
            case s::Text::TextType:
                text->textType = readEnum(r, 3, fla::StaticText::TextType::Static, text->phxUnknown);
                break;
            case s::Text::InstanceName: text->instanceName = r.string(); break;
            case s::Text::LineType: text->lineType = r.string(); break;
            case s::Text::Runs:
            {
                fla::TextRun run;
                Reader t = r.message();
                while (t.next())
                {
                    switch (t.field())
                    {
                    case s::TextRun::Text: run.text = t.string(); break;
                    case s::TextRun::Alignment:
                        run.alignment = readEnum(t, 4, fla::TextRun::Alignment::Left, run.phxUnknown);
                        break;
                    case s::TextRun::AliasText: run.aliasText = t.boolean(); break;
                    case s::TextRun::AutoKern: run.autoKern = t.boolean(); break;
                    case s::TextRun::LetterSpacing: run.letterSpacing = t.real(); break;
                    case s::TextRun::LineHeight: run.lineHeight = t.real(); break;
                    case s::TextRun::Size: run.size = t.real(); break;
                    case s::TextRun::BitmapSize: run.bitmapSize = t.real(); break;
                    case s::TextRun::Face: run.face = t.string(); break;
                    case s::TextRun::FillColor: unpackColor(t.fixed32(), run.fillColor); break;
                    case s::TextRun::Xfl: readXfl(t, run.extraAttributes, run.presentAttributes); break;
                    default: t.keep(run.phxUnknown); break;
                    }
                }
                finish(t, context);
                text->runs.push_back(std::move(run));
                break;
            }
            default: r.keep(text->phxUnknown); break;
            }
        }
        finish(r, context);
        return text;
    }
    case s::Element::Rectangle:
    {
        auto* rect = new fla::RectanglePrimitive(parent);
        Reader r = kind.message();
        while (r.next())
        {
            switch (r.field())
            {
            case s::RectanglePrimitive::Rect: readRect(r, rect->rect); break;
            case s::RectanglePrimitive::Fill:
                delete rect->fillStyle;
                rect->fillStyle = readFill(r, rect, context);
                break;
            case s::RectanglePrimitive::Stroke:
                delete rect->strokeStyle;
                rect->strokeStyle = readStroke(r, rect, context);
                break;
            case s::RectanglePrimitive::TopLeftRadius: rect->topLeftRadius = r.real(); break;
            case s::RectanglePrimitive::TopRightRadius: rect->topRightRadius = r.real(); break;
            case s::RectanglePrimitive::BottomLeftRadius: rect->bottomLeftRadius = r.real(); break;
            case s::RectanglePrimitive::BottomRightRadius: rect->bottomRightRadius = r.real(); break;
            default: r.keep(rect->phxUnknown); break;
            }
        }
        finish(r, context);
        return rect;
    }
    case s::Element::Oval:
    {
        auto* oval = new fla::OvalPrimitive(parent);
        Reader r = kind.message();
        while (r.next())
        {
            switch (r.field())
            {
            case s::OvalPrimitive::Rect: readRect(r, oval->rect); break;
            case s::OvalPrimitive::Fill:
                delete oval->fillStyle;
                oval->fillStyle = readFill(r, oval, context);
                break;
            case s::OvalPrimitive::Stroke:
                delete oval->strokeStyle;
                oval->strokeStyle = readStroke(r, oval, context);
                break;
            case s::OvalPrimitive::StartAngle: oval->startAngle = r.real(); break;
            case s::OvalPrimitive::EndAngle: oval->endAngle = r.real(); break;
            case s::OvalPrimitive::InnerRadius: oval->innerRadius = r.real(); break;
            default: r.keep(oval->phxUnknown); break;
            }
        }
        finish(r, context);
        return oval;
    }
    case s::Element::Raw:
    {
        auto* raw = new fla::RawElement(parent);
        Reader r = kind.message();
        while (r.next())
        {
            if (r.field() == s::RawXml::Xml)
                raw->xml = r.string();
            else
                r.keep(raw->phxUnknown);
        }
        finish(r, context);
        return raw;
    }
    default:
        return nullptr;
    }
}

bool isKindField(uint32_t field)
{
    return field >= s::Element::Shape && field <= 49;
}

fla::Element* readElement(const Reader& field, fla::DOMElement* parent, Context& context)
{
    // The kind makes the object, so it is found first.
    // As in protobuf, the last kind field is the one that counts.
    fla::Element* element = nullptr;
    std::string unknownKind;
    {
        Reader scan = field.message();
        Reader last = scan;
        bool found = false;
        while (scan.next())
        {
            if (isKindField(scan.field()))
            {
                last = scan;
                found = true;
            }
        }
        if (found)
        {
            element = readKind(last, parent, context);
            if (!element)
                last.keep(unknownKind);
        }
    }
    if (!element)
    {
        // A kind from a newer Phoenix: kept as an empty group that writes
        // the kind back, so it survives a save here.
        element = new fla::Group(parent);
    }
    element->phxUnknown += unknownKind;
    Reader r = field.message();
    while (r.next())
    {
        switch (r.field())
        {
        case s::Element::Matrix: element->transform = readMatrix(r); break;
        case s::Element::TransformationPoint: element->transformationPoint = readVector(r); break;
        case s::Element::IsLocked: element->isLocked = r.boolean(); break;
        case s::Element::IsSelected: element->isSelected = r.boolean(); break;
        case s::Element::IsFloating: element->isFloating = r.boolean(); break;
        case s::Element::KeepMatrix: element->keepMatrix = r.boolean(); break;
        case s::Element::KeepTransformationPoint: element->keepTransformationPoint = r.boolean(); break;
        case s::Element::Visible: element->visible = r.boolean(); break;
        case s::Element::ExtraChildren: element->extraChildren.push_back(r.string()); break;
        case s::Element::LocalBounds: readRect(r, element->localBounds); break;
        case s::Element::Xfl: readXfl(r, *element); break;
        default:
            if (!isKindField(r.field()))
                r.keep(element->phxUnknown);
            break;
        }
    }
    finish(r, context);
    return element;
}

// ---------------------------------------------------------------------------
// Timelines

fla::MorphShape* readMorphShape(const Reader& field, fla::Frame* frame, Context& context)
{
    auto* morph = new fla::MorphShape(frame);
    Reader r = field.message();
    while (r.next())
    {
        switch (r.field())
        {
        case s::MorphShape::Segments:
        {
            auto* segment = new fla::MorphSegment(morph);
            Reader m = r.message();
            while (m.next())
            {
                switch (m.field())
                {
                case s::MorphSegment::StartA: segment->startPointA = readVector(m); break;
                case s::MorphSegment::StartB: segment->startPointB = readVector(m); break;
                case s::MorphSegment::StrokeIndex1: segment->strokeIndex1 = m.int32(); break;
                case s::MorphSegment::StrokeIndex2: segment->strokeIndex2 = m.int32(); break;
                case s::MorphSegment::FillIndex1: segment->fillIndex1 = m.int32(); break;
                case s::MorphSegment::FillIndex2: segment->fillIndex2 = m.int32(); break;
                case s::MorphSegment::Curves:
                {
                    auto* curve = new fla::MorphCurves(segment);
                    Reader c = m.message();
                    while (c.next())
                    {
                        switch (c.field())
                        {
                        case s::MorphCurve::ControlA: curve->controlPointA = readVector(c); break;
                        case s::MorphCurve::AnchorA: curve->anchorPointA = readVector(c); break;
                        case s::MorphCurve::ControlB: curve->controlPointB = readVector(c); break;
                        case s::MorphCurve::AnchorB: curve->anchorPointB = readVector(c); break;
                        case s::MorphCurve::IsLine: curve->isLine = c.boolean(); break;
                        case s::MorphCurve::Xfl: readXfl(c, *curve); break;
                        default: c.keep(curve->phxUnknown); break;
                        }
                    }
                    segment->curves.push_back(curve);
                    break;
                }
                case s::MorphSegment::Xfl: readXfl(m, *segment); break;
                default: m.keep(segment->phxUnknown); break;
                }
            }
            morph->segments.push_back(segment);
            break;
        }
        case s::MorphShape::HintsXml: morph->hintsXml = r.string(); break;
        case s::MorphShape::Xfl: readXfl(r, *morph); break;
        default: r.keep(morph->phxUnknown); break;
        }
    }
    finish(r, context);
    return morph;
}

fla::Frame* readFrame(const Reader& field, fla::Layer* layer, Context& context)
{
    auto* frame = new fla::Frame(layer);
    Reader r = field.message();
    while (r.next())
    {
        switch (r.field())
        {
        case s::Frame::Index: frame->index = r.int32(); break;
        case s::Frame::Duration: frame->duration = r.int32(); break;
        case s::Frame::KeyMode: frame->keyMode = r.string(); break;
        case s::Frame::Name: frame->name = r.string(); break;
        case s::Frame::LabelType: frame->labelType = r.string(); break;
        case s::Frame::TweenType: frame->tweenType = readEnum(r, 3, fla::TweenType::None, frame->phxUnknown); break;
        case s::Frame::MotionTweenSnap: frame->motionTWeenSnap = r.boolean(); break;
        case s::Frame::MotionTweenSync: frame->motionTweenSync = r.boolean(); break;
        case s::Frame::Acceleration: frame->acceleration = static_cast<int>(r.sint()); break;
        case s::Frame::HasCustomEase: frame->hasCustomEase = r.boolean(); break;
        case s::Frame::CustomEase:
        {
            std::vector<double> values;
            r.reals([&values](double v) { values.push_back(v); });
            for (size_t i = 0; i + 1 < values.size(); i += 2)
                frame->customEase.emplace_back(values[i], values[i + 1]);
            break;
        }
        case s::Frame::CustomEaseName: frame->customEaseName = r.string(); break;
        case s::Frame::HasEase: frame->hasEase = r.boolean(); break;
        case s::Frame::EaseMethod: frame->easeMethod = r.string(); break;
        case s::Frame::OtherEases: frame->otherEases.push_back(r.string()); break;
        case s::Frame::MotionTweenRotate: frame->motionTweenRotate = r.string(); break;
        case s::Frame::MotionTweenRotateTimes: frame->motionTweenRotateTimes = r.int32(); break;
        case s::Frame::OtherTweenType: frame->otherTweenType = r.string(); break;
        case s::Frame::ExtraChildren: frame->extraChildren.push_back(r.string()); break;
        case s::Frame::ActionScript:
            if (!frame->actionScript)
                frame->actionScript = new fla::ActionScript(frame);
            frame->actionScript->code = r.string();
            break;
        case s::Frame::ActionScriptXfl:
            if (!frame->actionScript)
                frame->actionScript = new fla::ActionScript(frame);
            readXfl(r, *frame->actionScript);
            break;
        case s::Frame::MorphShape:
            delete frame->morphShape;
            frame->morphShape = readMorphShape(r, frame, context);
            break;
        case s::Frame::MorphSignature: frame->morphSignature = r.fixed64(); break;
        case s::Frame::Elements:
            if (fla::Element* element = readElement(r, frame, context))
                frame->elements.push_back(element);
            break;
        case s::Frame::Visible: frame->visible = r.boolean(); break;
        case s::Frame::Xfl: readXfl(r, *frame); break;
        default: r.keep(frame->phxUnknown); break;
        }
    }
    finish(r, context);
    return frame;
}

fla::Layer* readLayer(const Reader& field, fla::Timeline* timeline, Context& context)
{
    auto* layer = new fla::Layer(timeline);
    Reader r = field.message();
    while (r.next())
    {
        switch (r.field())
        {
        case s::Layer::Name: layer->name = r.string(); break;
        case s::Layer::Color: unpackColor(r.fixed32(), layer->color); break;
        case s::Layer::Current: layer->current = r.string(); break;
        case s::Layer::LayerType: layer->layerType = readEnum(r, 5, fla::Layer::Type::Normal, layer->phxUnknown); break;
        case s::Layer::OtherLayerType: layer->otherLayerType = r.string(); break;
        case s::Layer::ParentLayerIndex: layer->parentLayerIndex = static_cast<int>(r.sint()); break;
        case s::Layer::AutoNamed: layer->autoNamed = r.boolean(); break;
        case s::Layer::Selected: layer->selected = r.boolean(); break;
        case s::Layer::Locked: layer->locked = r.boolean(); break;
        case s::Layer::Outline: layer->outline = r.boolean(); break;
        case s::Layer::Visible: layer->visible = r.boolean(); break;
        case s::Layer::Frames: layer->frames.push_back(readFrame(r, layer, context)); break;
        case s::Layer::ExtraChildren: layer->extraChildren.push_back(r.string()); break;
        case s::Layer::Xfl: readXfl(r, *layer); break;
        default: r.keep(layer->phxUnknown); break;
        }
    }
    finish(r, context);
    // The span the layer covers, as the FLA reader sets it.
    if (!layer->frames.empty())
    {
        layer->firstFrame = layer->frames.front()->index;
        int last = 0;
        for (const fla::Frame* frame : layer->frames)
            last = std::max(last, frame->index + frame->duration);
        layer->lastFrame = last;
    }
    return layer;
}

fla::Timeline* readTimeline(const Reader& field, fla::DOMElement* parent, Context& context)
{
    auto* timeline = new fla::Timeline(parent);
    Reader r = field.message();
    while (r.next())
    {
        switch (r.field())
        {
        case s::Timeline::Name: timeline->name = r.string(); break;
        case s::Timeline::LayerDepthEnabled: timeline->layerDepthEnabled = r.boolean(); break;
        case s::Timeline::GuidesX: r.reals([timeline](double v) { timeline->guidesX.push_back(v); }); break;
        case s::Timeline::GuidesY: r.reals([timeline](double v) { timeline->guidesY.push_back(v); }); break;
        case s::Timeline::Layers: timeline->layers.push_back(readLayer(r, timeline, context)); break;
        case s::Timeline::ExtraChildren: timeline->extraChildren.push_back(r.string()); break;
        case s::Timeline::Xfl: readXfl(r, *timeline); break;
        default: r.keep(timeline->phxUnknown); break;
        }
    }
    finish(r, context);
    return timeline;
}

fla::Symbol* readSymbol(const Reader& field, fla::SymbolList* list, Context& context)
{
    auto* symbol = new fla::Symbol(list);
    Reader r = field;
    while (r.next())
    {
        switch (r.field())
        {
        case s::Symbol::Name: symbol->name = r.string(); break;
        case s::Symbol::ItemId: symbol->itemId = r.string(); break;
        case s::Symbol::LastModified: symbol->lastModified = r.string(); break;
        case s::Symbol::Href: symbol->href = r.string(); break;
        case s::Symbol::SymbolType:
            symbol->symbolType = readEnum(r, 3, fla::SymbolType::Graphic, symbol->phxUnknown);
            break;
        case s::Symbol::OtherSymbolType: symbol->otherSymbolType = r.string(); break;
        case s::Symbol::IncludeAttributes:
        {
            std::pair<std::string, std::string> kv;
            readKeyValue(r, kv);
            symbol->includeAttributes.push_back(std::move(kv));
            break;
        }
        case s::Symbol::Scale9: symbol->scale9 = r.boolean(); break;
        case s::Symbol::ScaleGrid:
            readRectangle(r, symbol->scaleGridLeft, symbol->scaleGridTop, symbol->scaleGridRight, symbol->scaleGridBottom);
            break;
        case s::Symbol::Timelines: symbol->timelines.push_back(readTimeline(r, symbol, context)); break;
        case s::Symbol::Xfl: readXfl(r, *symbol); break;
        default: r.keep(symbol->phxUnknown); break;
        }
    }
    finish(r, context);
    return symbol;
}

// ---------------------------------------------------------------------------
// Media

/// The media of a file, read on several threads; each item's bytes are
/// handed to the first object that asks for them and copied for any other.
class MediaStore
{
public:
    void read(const ContainerReader& file, std::string& warning)
    {
        std::vector<const ChunkInfo*> chunks;
        for (const ChunkInfo& chunk : file.chunks())
        {
            if (chunk.type == kMedia)
                chunks.push_back(&chunk);
        }
        struct Item
        {
            uint32_t id = 0;
            std::vector<uint8_t> data;
            std::string error;
        };
        std::vector<Item> items(chunks.size());
        parallelFor(chunks.size(), [&](size_t i) {
            Item& item = items[i];
            std::vector<uint8_t> content;
            if (!file.content(*chunks[i], content, item.error))
                return;
            // A length-prefixed header, then the bytes.
            uint64_t headerSize = 0;
            size_t at = 0;
            for (int shift = 0; at < content.size() && shift < 64; shift += 7)
            {
                const uint8_t byte = content[at++];
                headerSize |= static_cast<uint64_t>(byte & 0x7F) << shift;
                if (!(byte & 0x80))
                    break;
            }
            if (at + headerSize > content.size())
            {
                item.error = "A media part is damaged (header).";
                return;
            }
            Reader header(content.data() + at, static_cast<size_t>(headerSize));
            while (header.next())
            {
                if (header.field() == s::MediaHeader::Id)
                    item.id = static_cast<uint32_t>(header.uint());
            }
            content.erase(content.begin(), content.begin() + static_cast<std::ptrdiff_t>(at + headerSize));
            item.data = std::move(content);
        });
        for (Item& item : items)
        {
            if (!item.error.empty())
                warning += item.error + "\n";
            else
                _items[item.id] = Entry{std::move(item.data)};
        }
    }

    /// Counts how many objects \a document will ask for each media item, so
    /// the last of them can have the bytes moved rather than copied.
    void countUses(const std::string& document)
    {
        Reader r(document);
        while (r.next())
        {
            if (r.field() != s::Document::Bitmaps && r.field() != s::Document::PackageFiles)
                continue;
            const bool bitmap = r.field() == s::Document::Bitmaps;
            Reader item = r.message();
            while (item.next())
            {
                const bool media = bitmap
                    ? (item.field() == s::BitmapItem::DataMedia || item.field() == s::BitmapItem::ImageMedia)
                    : item.field() == s::PackageFile::Media;
                if (media)
                    ++_uses[static_cast<uint32_t>(item.uint())];
            }
        }
    }

    /// The bytes of media \a id, for \a into; nothing for an unknown id.
    void give(uint32_t id, std::vector<uint8_t>& into)
    {
        const auto found = _items.find(id);
        if (found == _items.end())
            return;
        int& uses = _uses[id];
        if (uses > 1)
        {
            into = found->second.data;
            --uses;
            return;
        }
        into = std::move(found->second.data);
        uses = 0;
    }

private:
    struct Entry
    {
        std::vector<uint8_t> data;
    };
    std::map<uint32_t, int> _uses;
    std::map<uint32_t, Entry> _items;
};

} // namespace

// ---------------------------------------------------------------------------
// The document

fla::FLADocument* load(const std::string& path, std::string& error, std::string& warning, bool& readOnly)
{
    readOnly = false;
    // PHOENIX_PHX_PROFILE=1 prints how long each part of a read takes.
    static const bool profile = std::getenv("PHOENIX_PHX_PROFILE") != nullptr;
    auto mark = std::chrono::steady_clock::now();
    const auto phase = [&mark](const char* name) {
        if (!profile)
            return;
        const auto now = std::chrono::steady_clock::now();
        std::fprintf(stderr, "  phx %-10s %7.1f ms\n", name, std::chrono::duration<double, std::milli>(now - mark).count());
        mark = now;
    };
    ContainerReader file;
    if (!file.open(path, error))
        return nullptr;
    phase("file");
    if (file.recovered())
        warning += "The file's index was damaged and has been rebuilt.\n";

    const ChunkInfo* documentChunk = nullptr;
    std::vector<const ChunkInfo*> symbolChunks;
    for (const ChunkInfo& chunk : file.chunks())
    {
        if (chunk.type == kDocument && !documentChunk)
            documentChunk = &chunk;
        else if (chunk.type == kSymbol)
            symbolChunks.push_back(&chunk);
    }
    std::string documentContent;
    if (!documentChunk || !file.content(*documentChunk, documentContent, error))
    {
        if (error.empty())
            error = "This Phoenix document has no document part.";
        return nullptr;
    }

    MediaStore media;
    media.read(file, warning);
    phase("media");
    media.countUses(documentContent);

    auto* flaDocument = new fla::FLADocument(nullptr);
    flaDocument->filepath = path;
    auto* document = new fla::Document(flaDocument);
    flaDocument->document = document;
    Context context;
    std::vector<std::string> symbolOrder;
    bool hasSymbolList = false;

    Reader r(documentContent);
    while (r.next())
    {
        switch (r.field())
        {
        case s::Document::Width: document->width = r.int32(); break;
        case s::Document::Height: document->height = r.int32(); break;
        case s::Document::FrameRate: document->frameRate = r.real(); break;
        case s::Document::BackgroundColor: unpackColor(r.fixed32(), document->backgroundColor); break;
        case s::Document::GridColor: unpackColor(r.fixed32(), document->gridColor); break;
        case s::Document::GuidesColor: unpackColor(r.fixed32(), document->guidesColor); break;
        case s::Document::GridSpacingX: document->gridSpacingX = r.int32(); break;
        case s::Document::GridSpacingY: document->gridSpacingY = r.int32(); break;
        case s::Document::SnapAlignBorderSpacing: document->snapAlignBorderSpacing = r.int32(); break;
        case s::Document::ObjectsSnapTo: document->objectsSnapTo = r.boolean(); break;
        case s::Document::GuidesSnapTo: document->guidesSnapTo = r.boolean(); break;
        case s::Document::TimelineHeight: document->timelineHeight = r.int32(); break;
        case s::Document::TimelineLabelWidth: document->timelineLabelWidth = r.int32(); break;
        case s::Document::VanishingPointX: document->vanishingPoint3DX = r.real(); break;
        case s::Document::VanishingPointY: document->vanishingPoint3DY = r.real(); break;
        case s::Document::ViewAngle3D: document->viewAngle3D = r.real(); break;
        case s::Document::NextSceneIdentifier: document->nextSceneIdentifier = r.int32(); break;
        case s::Document::PlayLoop: document->playOptionsPlayLoop = r.boolean(); break;
        case s::Document::PlayPages: document->playOptionsPlayPages = r.boolean(); break;
        case s::Document::PlayFrameActions: document->playOptionsPlayFrameActions = r.boolean(); break;
        case s::Document::CurrentTimeline: document->currentTimeline = r.string(); break;
        case s::Document::HasSymbolList: hasSymbolList = r.boolean(); break;
        case s::Document::Scenes: document->timelines.push_back(readTimeline(r, document, context)); break;
        case s::Document::Folders:
        {
            auto* folder = new fla::Folder(document);
            Reader f = r.message();
            while (f.next())
            {
                switch (f.field())
                {
                case s::LibraryFolder::Name: folder->name = f.string(); break;
                case s::LibraryFolder::ItemId: folder->itemId = f.string(); break;
                case s::LibraryFolder::IsExpanded: folder->isExpanded = f.boolean(); break;
                case s::LibraryFolder::Xfl: readXfl(f, *folder); break;
                default: f.keep(folder->phxUnknown); break;
                }
            }
            document->folders.push_back(folder);
            break;
        }
        case s::Document::Bitmaps:
        {
            auto* bitmap = new fla::Bitmap(document);
            Reader b = r.message();
            while (b.next())
            {
                switch (b.field())
                {
                case s::BitmapItem::Name: bitmap->name = b.string(); break;
                case s::BitmapItem::ItemId: bitmap->itemId = b.string(); break;
                case s::BitmapItem::SourceExternalFilepath: bitmap->sourceExternalFilepath = b.string(); break;
                case s::BitmapItem::SourceLastImported: bitmap->sourceLastImported = static_cast<uint32_t>(b.uint()); break;
                case s::BitmapItem::ExternalFileCrc32: bitmap->externalFileCRC32 = static_cast<uint32_t>(b.uint()); break;
                case s::BitmapItem::ExternalFileSize: bitmap->externalFileSize = static_cast<uint32_t>(b.uint()); break;
                case s::BitmapItem::AllowSmoothing: bitmap->allowSmoothing = b.boolean(); break;
                case s::BitmapItem::Quality: bitmap->quality = b.int32(); break;
                case s::BitmapItem::Href: bitmap->href = b.string(); break;
                case s::BitmapItem::BitmapDataHref: bitmap->bitmapDataHRef = b.string(); break;
                case s::BitmapItem::FrameRight: bitmap->frameRight = b.int32(); break;
                case s::BitmapItem::FrameBottom: bitmap->frameBottom = b.int32(); break;
                case s::BitmapItem::IsJpeg: bitmap->isJPEG = b.boolean(); break;
                case s::BitmapItem::DataMedia: media.give(static_cast<uint32_t>(b.uint()), bitmap->datData); break;
                case s::BitmapItem::ImageMedia: media.give(static_cast<uint32_t>(b.uint()), bitmap->imageData); break;
                case s::BitmapItem::Xfl: readXfl(b, *bitmap); break;
                default: b.keep(bitmap->phxUnknown); break;
                }
            }
            document->resources.push_back(bitmap);
            break;
        }
        case s::Document::Sounds:
        {
            fla::Document::SoundInfo sound;
            Reader so = r.message();
            while (so.next())
            {
                switch (so.field())
                {
                case s::SoundItem::Name: sound.name = so.string(); break;
                case s::SoundItem::Format: sound.format = so.string(); break;
                case s::SoundItem::DataPath: sound.dataPath = so.string(); break;
                default: break;
                }
            }
            document->sounds.push_back(std::move(sound));
            break;
        }
        case s::Document::SymbolOrder: symbolOrder.push_back(r.string()); break;
        case s::Document::SwatchLists:
        {
            auto* list = new fla::SwatchList(document);
            Reader l = r.message();
            while (l.next())
            {
                switch (l.field())
                {
                case s::SwatchList::Name: list->name = l.string(); break;
                case s::SwatchList::Colors:
                    l.fixed32s([list](uint32_t color) {
                        auto* swatch = new fla::SolidSwatch(list);
                        unpackColor(color, swatch->color);
                        list->swatches.push_back(swatch);
                    });
                    break;
                case s::SwatchList::Xml: list->xml = l.string(); break;
                default: l.keep(list->phxUnknown); break;
                }
            }
            document->swatchLists.push_back(list);
            break;
        }
        case s::Document::PackageFiles:
        {
            std::pair<std::string, std::vector<uint8_t>> file;
            Reader p = r.message();
            while (p.next())
            {
                if (p.field() == s::PackageFile::Path)
                    file.first = p.string();
                else if (p.field() == s::PackageFile::Media)
                    media.give(static_cast<uint32_t>(p.uint()), file.second);
            }
            document->extraFiles.push_back(std::move(file));
            break;
        }
        case s::Document::Features: document->phxFeatures.push_back(r.string()); break;
        case s::Document::RequiredFeatures: document->phxRequiredFeatures.push_back(r.string()); break;
        case s::Document::Xfl:
        {
            Reader x = r.message();
            while (x.next())
            {
                switch (x.field())
                {
                case s::XflDocument::FiletypeGuid: document->filetypeGUID = x.string(); break;
                case s::XflDocument::FileGuid: document->fileGUID = x.string(); break;
                case s::XflDocument::XflVersion: document->xflVersion = x.string(); break;
                case s::XflDocument::CreatorInfo: document->creatorInfo = x.string(); break;
                case s::XflDocument::Platform: document->platform = x.string(); break;
                case s::XflDocument::VersionInfo: document->versionInfo = x.string(); break;
                case s::XflDocument::MajorVersion: document->majorVersion = x.int32(); break;
                case s::XflDocument::BuildNumber: document->buildNumber = x.int32(); break;
                case s::XflDocument::ScriptListXml:
                    document->scriptList = new fla::ScriptList(document);
                    document->scriptList->xml = x.string();
                    break;
                case s::XflDocument::PublishHistoryXml:
                    document->publishHistory = new fla::PublishHistory(document);
                    document->publishHistory->xml = x.string();
                    break;
                case s::XflDocument::PrinterSettingsXml:
                    document->printerSettings = new fla::PrinterSettings(document);
                    document->printerSettings->xml = x.string();
                    break;
                case s::XflDocument::ExtraSections:
                {
                    fla::Document::RawSection section;
                    Reader e = x.message();
                    while (e.next())
                    {
                        if (e.field() == s::RawSection::Xml)
                            section.xml = e.string();
                        else if (e.field() == s::RawSection::BeforeTimelines)
                            section.beforeTimelines = e.boolean();
                    }
                    document->extraSections.push_back(std::move(section));
                    break;
                }
                case s::XflDocument::ExtraMedia: document->extraMedia.push_back(x.string()); break;
                case s::XflDocument::Detail: readXfl(x, *document); break;
                case s::XflDocument::FromBinary: flaDocument->fromBinary = x.boolean(); break;
                default: break;
                }
            }
            break;
        }
        default:
            r.keep(document->phxUnknown);
            break;
        }
    }
    if (r.failed())
        warning += "Part of the document's settings is damaged.\n";

    phase("document");
    // Symbols, several at once: each chunk is read on its own.
    if (hasSymbolList || !symbolChunks.empty())
    {
        auto* list = new fla::SymbolList(document);
        document->symbolList = list;
        struct Result
        {
            fla::Symbol* symbol = nullptr;
            Context context;
            std::string error;
        };
        std::vector<Result> results(symbolChunks.size());
        const auto readRange = [&](size_t first, size_t last) {
            for (size_t i = first; i < last; ++i)
            {
                std::string content;
                if (!file.content(*symbolChunks[i], content, results[i].error))
                    continue;
                results[i].symbol = readSymbol(Reader(content), list, results[i].context);
            }
        };
        const size_t threads = std::max<size_t>(1, std::min<size_t>(std::thread::hardware_concurrency(), 8));
        if (symbolChunks.size() < 8 || threads == 1)
        {
            readRange(0, symbolChunks.size());
        }
        else
        {
            std::vector<std::future<void>> work;
            const size_t per = (symbolChunks.size() + threads - 1) / threads;
            for (size_t first = 0; first < symbolChunks.size(); first += per)
                work.push_back(std::async(std::launch::async, readRange, first, std::min(first + per, symbolChunks.size())));
            for (std::future<void>& done : work)
                done.get();
        }

        // In the library's order, then any the order does not name.
        std::map<std::string, fla::Symbol*> byName;
        for (Result& result : results)
        {
            if (!result.error.empty())
                warning += result.error + "\n";
            if (!result.symbol)
                continue;
            if (result.context.damaged)
                warning += "The symbol " + result.symbol->name + " is damaged; what could be read of it is shown.\n";
            byName[result.symbol->name] = result.symbol;
            context.instances.insert(context.instances.end(), result.context.instances.begin(),
                result.context.instances.end());
        }
        std::set<const fla::Symbol*> placed;
        for (const std::string& name : symbolOrder)
        {
            const auto found = byName.find(name);
            if (found == byName.end() || placed.count(found->second))
                continue;
            list->symbols.push_back(found->second);
            placed.insert(found->second);
        }
        for (Result& result : results)
        {
            if (result.symbol && !placed.count(result.symbol))
                list->symbols.push_back(result.symbol);
        }
        for (fla::Symbol* symbol : list->symbols)
            list->symbolMap[symbol->name] = symbol;
    }

    phase("symbols");
    // Instances find their symbols, as the FLA reader links them.
    document->symbolInstances = context.instances;
    if (document->symbolList)
    {
        for (fla::SymbolInstance* instance : document->symbolInstances)
        {
            const auto found = document->symbolList->symbolMap.find(instance->libraryItemName);
            if (found == document->symbolList->symbolMap.end())
                continue;
            instance->symbol = found->second;
            document->symbolInstancesMap[found->second].push_back(instance);
        }
    }
    if (context.damaged)
        warning += "Part of the document is damaged; what could be read of it is shown.\n";

    // Chunks this Phoenix does not know are kept; a critical one means it
    // cannot edit the document safely.
    static const std::set<uint32_t> known = {kIndex, kDocument, kSymbol, kMedia, kThumbnail, kMetadata};
    for (const ChunkInfo& chunk : file.chunks())
    {
        if (known.count(chunk.type))
            continue;
        fla::Document::UnknownChunk unknown;
        unknown.type = chunk.type;
        unknown.flags = chunk.flags;
        std::string chunkError;
        if (!file.content(chunk, unknown.content, chunkError))
        {
            warning += chunkError + "\n";
            continue;
        }
        if (chunk.flags & kCritical)
        {
            readOnly = true;
            warning += "This document has a part (" + typeName(chunk.type) +
                ") from a newer Phoenix. It is open read-only; save a copy to keep your changes.\n";
        }
        document->phxUnknownChunks.push_back(std::move(unknown));
    }
    // Required features this Phoenix does not have: the same.
    static const std::set<std::string> supported = {"variable-width-strokes"};
    for (const std::string& feature : document->phxRequiredFeatures)
    {
        if (!supported.count(feature))
        {
            readOnly = true;
            warning += "This document uses " + feature + ", which needs a newer Phoenix to edit safely. It is open read-only.\n";
        }
    }
    if (file.minorVersion() > kMinorVersion && warning.empty())
    {
        // Nothing to warn about: newer additions are kept as they are.
    }
    return flaDocument;
}

} // namespace phx
