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
#include "../data/folder.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/linear_gradient.h"
#include "../data/morph_curves.h"
#include "../data/morph_segment.h"
#include "../data/morph_shape.h"
#include "../data/oval_primitive.h"
#include "../data/radial_gradient.h"
#include "../data/raw_element.h"
#include "../data/rectangle_primitive.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/static_text.h"
#include "../data/stroke_style.h"
#include "../data/swatch.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <map>

namespace phx {

namespace s = schema;

namespace {

// ---------------------------------------------------------------------------
// Small pieces

void writeDouble(Writer& w, uint32_t field, double value, double fallback = 0.0)
{
    if (value != fallback)
        w.real(field, value);
}
void writeInt(Writer& w, uint32_t field, int64_t value, int64_t fallback = 0)
{
    if (value != fallback)
        w.int64(field, value);
}
void writeSint(Writer& w, uint32_t field, int64_t value, int64_t fallback = 0)
{
    if (value != fallback)
        w.sint(field, value);
}
void writeBool(Writer& w, uint32_t field, bool value, bool fallback = false)
{
    if (value != fallback)
        w.boolean(field, value);
}
void writeString(Writer& w, uint32_t field, const std::string& value)
{
    if (!value.empty())
        w.string(field, value);
}
void writeColor(Writer& w, uint32_t field, const uint8_t color[4], uint32_t fallback)
{
    const uint32_t packed = packColor(color);
    if (packed != fallback)
        w.fixed32(field, packed);
}
void writeStrings(Writer& w, uint32_t field, const std::vector<std::string>& values)
{
    for (const std::string& value : values)
        w.string(field, value);
}
void writeKeyValues(Writer& w, uint32_t field, const std::vector<std::pair<std::string, std::string>>& values)
{
    for (const auto& entry : values)
    {
        Writer kv;
        writeString(kv, s::KeyValue::Key, entry.first);
        writeString(kv, s::KeyValue::Value, entry.second);
        w.message(field, kv);
    }
}

void writeMatrix(Writer& w, uint32_t field, const fla::Transform& t)
{
    Writer m;
    writeDouble(m, s::Matrix::A, t.m11, 1.0);
    writeDouble(m, s::Matrix::B, t.m12);
    writeDouble(m, s::Matrix::C, t.m21);
    writeDouble(m, s::Matrix::D, t.m22, 1.0);
    writeDouble(m, s::Matrix::Tx, t.tx);
    writeDouble(m, s::Matrix::Ty, t.ty);
    if (!m.empty())
        w.message(field, m);
}

void writeVector(Writer& w, uint32_t field, const fla::Point& p, bool always = false)
{
    Writer v;
    writeDouble(v, s::Vector2::X, p.x);
    writeDouble(v, s::Vector2::Y, p.y);
    if (always || !v.empty())
        w.message(field, v);
}

void writeRectangle(Writer& w, uint32_t field, double left, double top, double right, double bottom, bool always = false)
{
    Writer r;
    writeDouble(r, s::Rectangle::Left, left);
    writeDouble(r, s::Rectangle::Top, top);
    writeDouble(r, s::Rectangle::Right, right);
    writeDouble(r, s::Rectangle::Bottom, bottom);
    if (always || !r.empty())
        w.message(field, r);
}

/// The XFL attributes kept for \a element, and any extra children passed.
void writeXfl(Writer& w, uint32_t field, const std::vector<std::pair<std::string, std::string>>& extraAttributes,
    const std::vector<std::string>& presentAttributes, const std::vector<std::string>* extraChildren = nullptr)
{
    Writer d;
    writeKeyValues(d, s::XflDetail::ExtraAttributes, extraAttributes);
    writeStrings(d, s::XflDetail::PresentAttributes, presentAttributes);
    if (extraChildren)
        writeStrings(d, s::XflDetail::ExtraChildren, *extraChildren);
    if (!d.empty())
        w.message(field, d);
}
void writeXfl(Writer& w, uint32_t field, const fla::DOMElement& element)
{
    writeXfl(w, field, element.extraAttributes, element.presentAttributes);
}

// ---------------------------------------------------------------------------
// Styles

void writeGradientStops(Writer& w, uint32_t field, const std::vector<fla::GradientEntry>& entries)
{
    for (const fla::GradientEntry& entry : entries)
    {
        Writer stop;
        writeDouble(stop, s::GradientStop::Ratio, entry.ratio);
        writeColor(stop, s::GradientStop::Color, entry.color, kOpaqueBlack);
        w.message(field, stop);
    }
}

Writer encodeFill(const fla::FillStyle& fill)
{
    Writer w;
    writeMatrix(w, s::Fill::Matrix, fill.transform);
    writeString(w, s::Fill::SpreadMethod, fill.spreadMethod);
    writeString(w, s::Fill::InterpolationMethod, fill.interpolationMethod);
    writeXfl(w, s::Fill::Xfl, fill);
    switch (fill.type())
    {
    case fla::FillStyle::Type::SolidColor:
        w.fixed32(s::Fill::Solid, packColor(static_cast<const fla::SolidColor&>(fill).color));
        break;
    case fla::FillStyle::Type::LinearGradient:
    {
        Writer g;
        writeGradientStops(g, s::Gradient::Stops, static_cast<const fla::LinearGradient&>(fill).entries);
        w.message(s::Fill::LinearGradient, g);
        break;
    }
    case fla::FillStyle::Type::RadialGradient:
    {
        const auto& radial = static_cast<const fla::RadialGradient&>(fill);
        Writer g;
        for (const fla::RadialEntry& entry : radial.entries)
        {
            Writer stop;
            writeDouble(stop, s::GradientStop::Ratio, entry.ratio);
            writeColor(stop, s::GradientStop::Color, entry.color, kOpaqueBlack);
            g.message(s::Gradient::Stops, stop);
        }
        writeDouble(g, s::Gradient::FocalPointRatio, radial.focalPointRatio);
        w.message(s::Fill::RadialGradient, g);
        break;
    }
    case fla::FillStyle::Type::BitmapFill:
    {
        const auto& bitmap = static_cast<const fla::BitmapFill&>(fill);
        Writer b;
        writeString(b, s::BitmapFill::BitmapPath, bitmap.bitmapPath);
        writeBool(b, s::BitmapFill::Clipped, bitmap.bitmapIsClipped);
        w.message(s::Fill::Bitmap, b);
        break;
    }
    }
    w.rawFields(fill.phxUnknown);
    return w;
}

Writer encodeStroke(const fla::StrokeStyle& stroke)
{
    Writer w;
    writeInt(w, s::Stroke::Kind, static_cast<int>(stroke.style()));
    writeDouble(w, s::Stroke::Weight, stroke.weight, 1.0);
    writeString(w, s::Stroke::ScaleMode, stroke.scaleMode);
    writeString(w, s::Stroke::SolidStyle, stroke.solidStyle);
    writeString(w, s::Stroke::Caps, stroke.caps);
    writeString(w, s::Stroke::Joints, stroke.joints);
    writeDouble(w, s::Stroke::MiterLimit, stroke.miterLimit, 3.0);
    writeBool(w, s::Stroke::PixelHinting, stroke.pixelHinting);
    writeDouble(w, s::Stroke::Dash1, stroke.dash1, 6.0);
    writeDouble(w, s::Stroke::Dash2, stroke.dash2, 6.0);
    writeDouble(w, s::Stroke::DotSpace, stroke.dotSpace, 3.0);
    writeKeyValues(w, s::Stroke::StyleAttributes,
        std::vector<std::pair<std::string, std::string>>(stroke.styleAttributes.begin(), stroke.styleAttributes.end()));
    writeStrings(w, s::Stroke::ExtraChildren, stroke.extraChildren);
    for (const fla::StrokeStyle::WidthMarker& marker : stroke.widthProfile)
    {
        Writer m;
        writeDouble(m, s::WidthMarker::Position, marker.position);
        writeDouble(m, s::WidthMarker::Left, marker.left, 0.5);
        writeDouble(m, s::WidthMarker::Right, marker.right, 0.5);
        writeBool(m, s::WidthMarker::Corner, marker.corner);
        w.message(s::Stroke::WidthProfile, m);
    }
    if (stroke.fill)
        w.message(s::Stroke::Fill, encodeFill(*stroke.fill));
    writeXfl(w, s::Stroke::Xfl, stroke);
    w.rawFields(stroke.phxUnknown);
    return w;
}

// ---------------------------------------------------------------------------
// Geometry

Writer encodePath(const fla::Path& path)
{
    Writer w;
    writeSint(w, s::Path::StyleIndex, path.styleIndex, -1);
    writeSint(w, s::Path::LineStyleIndex, path.lineStyleIndex, -1);
    writeSint(w, s::Path::FillStyleIndex, path.fillStyleIndex, -1);
    Packed segments;
    Packed coordinates;
    int64_t lastX = 0;
    int64_t lastY = 0;
    for (const fla::PathSegment* segment : path.segments)
    {
        if (!segment)
            continue;
        segments.varint((static_cast<uint64_t>(segment->points.size()) << 3) | static_cast<uint32_t>(segment->command));
        for (const fla::Point& point : segment->points)
        {
            const int64_t x = toUnits(point.x);
            const int64_t y = toUnits(point.y);
            coordinates.sint(x - lastX);
            coordinates.sint(y - lastY);
            lastX = x;
            lastY = y;
        }
    }
    segments.writeTo(w, s::Path::Segments);
    coordinates.writeTo(w, s::Path::Coordinates);
    w.rawFields(path.phxUnknown);
    return w;
}

Writer encodeEdge(const fla::Edge& edge)
{
    Writer w;
    writeSint(w, s::Edge::FillStyle0, edge.fillStyle0, -1);
    writeSint(w, s::Edge::FillStyle1, edge.fillStyle1, -1);
    writeSint(w, s::Edge::StrokeStyle, edge.strokeStyle, -1);
    for (const fla::Path* path : edge.paths)
    {
        if (path)
            w.message(s::Edge::Paths, encodePath(*path));
    }
    writeXfl(w, s::Edge::Xfl, edge);
    w.rawFields(edge.phxUnknown);
    return w;
}

Writer encodeShape(const fla::Shape& shape)
{
    Writer w;
    // The styles in their list order, each with the index edges use for it
    // (0 for one that is not in the index map).
    const auto indexOfFill = [&shape](const fla::FillStyle* fill) {
        for (const auto& entry : shape.fillsMap)
        {
            if (entry.second == fill)
                return entry.first;
        }
        return 0;
    };
    const auto indexOfStroke = [&shape](const fla::StrokeStyle* stroke) {
        for (const auto& entry : shape.strokesMap)
        {
            if (entry.second == stroke)
                return entry.first;
        }
        return 0;
    };
    for (const fla::FillStyle* fill : shape.fills)
    {
        if (!fill)
            continue;
        Writer entry;
        writeInt(entry, s::IndexedStyle::Index, indexOfFill(fill));
        entry.message(s::IndexedStyle::Style, encodeFill(*fill));
        w.message(s::Shape::Fills, entry);
    }
    for (const fla::StrokeStyle* stroke : shape.strokes)
    {
        if (!stroke)
            continue;
        Writer entry;
        writeInt(entry, s::IndexedStyle::Index, indexOfStroke(stroke));
        entry.message(s::IndexedStyle::Style, encodeStroke(*stroke));
        w.message(s::Shape::Strokes, entry);
    }
    for (const fla::Edge* edge : shape.edges)
    {
        if (edge)
            w.message(s::Shape::Edges, encodeEdge(*edge));
    }
    writeBool(w, s::Shape::IsDrawingObject, shape.isDrawingObject);
    for (const auto& record : shape.cubicEdges)
    {
        Writer r;
        writeInt(r, s::CubicRecord::BeforeEdge, static_cast<int64_t>(record.first));
        writeString(r, s::CubicRecord::Xml, record.second);
        w.message(s::Shape::CubicEdges, r);
    }
    writeInt(w, s::Shape::ReadEdgeCount, static_cast<int64_t>(shape.readEdgeCount));
    return w;
}

// ---------------------------------------------------------------------------
// Elements

Writer encodeColorTransform(const fla::ColorTransform& c)
{
    Writer w;
    writeDouble(w, s::ColorTransform::TintMultiplier, c.tintMultiplier);
    writeColor(w, s::ColorTransform::TintColor, c.tintColor, kOpaqueBlack);
    writeDouble(w, s::ColorTransform::Brightness, c.brightness);
    writeDouble(w, s::ColorTransform::RedMultiplier, c.redMultiplier, 1.0);
    writeDouble(w, s::ColorTransform::GreenMultiplier, c.greenMultiplier, 1.0);
    writeDouble(w, s::ColorTransform::BlueMultiplier, c.blueMultiplier, 1.0);
    writeDouble(w, s::ColorTransform::AlphaMultiplier, c.alphaMultiplier, 1.0);
    writeSint(w, s::ColorTransform::RedOffset, c.redOffset);
    writeSint(w, s::ColorTransform::GreenOffset, c.greenOffset);
    writeSint(w, s::ColorTransform::BlueOffset, c.blueOffset);
    writeSint(w, s::ColorTransform::AlphaOffset, c.alphaOffset);
    return w;
}

Writer encodeFilter(const fla::Filter& f)
{
    Writer w;
    writeInt(w, s::Filter::Type, static_cast<int>(f.type));
    writeBool(w, s::Filter::Enabled, f.enabled, true);
    writeDouble(w, s::Filter::BlurX, f.blurX, 5.0);
    writeDouble(w, s::Filter::BlurY, f.blurY, 5.0);
    writeInt(w, s::Filter::Quality, f.quality, 1);
    writeDouble(w, s::Filter::Angle, f.angle, 45.0);
    writeDouble(w, s::Filter::Distance, f.distance, 5.0);
    writeDouble(w, s::Filter::Strength, f.strength, 1.0);
    writeColor(w, s::Filter::Color, f.color, kOpaqueBlack);
    writeColor(w, s::Filter::HighlightColor, f.highlightColor, kOpaqueWhite);
    writeColor(w, s::Filter::ShadowColor, f.shadowColor, kOpaqueBlack);
    writeBool(w, s::Filter::Inner, f.inner);
    writeBool(w, s::Filter::Knockout, f.knockout);
    writeBool(w, s::Filter::HideObject, f.hideObject);
    writeString(w, s::Filter::BevelType, f.bevelType);
    writeDouble(w, s::Filter::Brightness, f.brightness);
    writeDouble(w, s::Filter::Contrast, f.contrast);
    writeDouble(w, s::Filter::Saturation, f.saturation);
    writeDouble(w, s::Filter::Hue, f.hue);
    writeGradientStops(w, s::Filter::Entries, f.entries);
    writeXfl(w, s::Filter::Xfl, f.extraAttributes, f.presentAttributes);
    w.rawFields(f.phxUnknown);
    return w;
}

Writer encodeElement(const fla::Element& element);

Writer encodeKind(const fla::Element& element, uint32_t& field)
{
    Writer w;
    switch (element.elementType())
    {
    case fla::Element::Type::Shape:
        field = s::Element::Shape;
        return encodeShape(static_cast<const fla::Shape&>(element));
    case fla::Element::Type::Group:
        field = s::Element::Group;
        for (const fla::Element* member : static_cast<const fla::Group&>(element).members)
        {
            if (member)
                w.message(s::Group::Members, encodeElement(*member));
        }
        return w;
    case fla::Element::Type::SymbolInstance:
    {
        field = s::Element::SymbolInstance;
        const auto& instance = static_cast<const fla::SymbolInstance&>(element);
        writeString(w, s::SymbolInstance::LibraryItemName, instance.libraryItemName);
        writeString(w, s::SymbolInstance::Name, instance.name);
        writeInt(w, s::SymbolInstance::FirstFrame, instance.firstFrame);
        writeInt(w, s::SymbolInstance::LastFrame, instance.lastFrame, -1);
        writeInt(w, s::SymbolInstance::SymbolType, static_cast<int>(instance.symbolType), static_cast<int>(fla::SymbolType::Graphic));
        writeInt(w, s::SymbolInstance::LoopType, static_cast<int>(instance.loopType), static_cast<int>(fla::LoopType::PlayOnce));
        const Writer color = encodeColorTransform(instance.colorTransform);
        if (!color.empty())
            w.message(s::SymbolInstance::ColorTransform, color);
        writeString(w, s::SymbolInstance::BlendMode, instance.blendMode);
        writeString(w, s::SymbolInstance::OtherSymbolType, instance.otherSymbolType);
        writeBool(w, s::SymbolInstance::KeepColor, instance.keepColor);
        writeStrings(w, s::SymbolInstance::ExtraFilters, instance.extraFilters);
        for (const fla::Filter& filter : instance.filters)
            w.message(s::SymbolInstance::Filters, encodeFilter(filter));
        return w;
    }
    case fla::Element::Type::BitmapInstance:
        field = s::Element::BitmapInstance;
        writeString(w, s::BitmapInstance::LibraryItemName, static_cast<const fla::BitmapInstance&>(element).libraryItemName);
        return w;
    case fla::Element::Type::StaticText:
    {
        field = s::Element::Text;
        const auto& text = static_cast<const fla::StaticText&>(element);
        writeDouble(w, s::Text::Left, text.left);
        writeDouble(w, s::Text::Top, text.top);
        writeDouble(w, s::Text::Width, text.width);
        writeDouble(w, s::Text::Height, text.height);
        writeBool(w, s::Text::AutoExpand, text.autoExpand);
        writeInt(w, s::Text::TextType, static_cast<int>(text.textType));
        writeString(w, s::Text::InstanceName, text.instanceName);
        writeString(w, s::Text::LineType, text.lineType);
        for (const fla::TextRun& run : text.runs)
        {
            Writer r;
            writeString(r, s::TextRun::Text, run.text);
            writeInt(r, s::TextRun::Alignment, static_cast<int>(run.alignment));
            writeBool(r, s::TextRun::AliasText, run.aliasText);
            writeBool(r, s::TextRun::AutoKern, run.autoKern, true);
            writeDouble(r, s::TextRun::LetterSpacing, run.letterSpacing);
            writeDouble(r, s::TextRun::LineHeight, run.lineHeight);
            writeDouble(r, s::TextRun::Size, run.size);
            writeDouble(r, s::TextRun::BitmapSize, run.bitmapSize);
            writeString(r, s::TextRun::Face, run.face);
            writeColor(r, s::TextRun::FillColor, run.fillColor, kOpaqueBlack);
            writeXfl(r, s::TextRun::Xfl, run.extraAttributes, run.presentAttributes);
            r.rawFields(run.phxUnknown);
            w.message(s::Text::Runs, r);
        }
        return w;
    }
    case fla::Element::Type::Rectangle:
    {
        field = s::Element::Rectangle;
        const auto& rect = static_cast<const fla::RectanglePrimitive&>(element);
        writeRectangle(w, s::RectanglePrimitive::Rect, rect.rect.topLeft.x, rect.rect.topLeft.y, rect.rect.bottomRight.x,
            rect.rect.bottomRight.y);
        if (rect.fillStyle)
            w.message(s::RectanglePrimitive::Fill, encodeFill(*rect.fillStyle));
        if (rect.strokeStyle)
            w.message(s::RectanglePrimitive::Stroke, encodeStroke(*rect.strokeStyle));
        writeDouble(w, s::RectanglePrimitive::TopLeftRadius, rect.topLeftRadius);
        writeDouble(w, s::RectanglePrimitive::TopRightRadius, rect.topRightRadius);
        writeDouble(w, s::RectanglePrimitive::BottomLeftRadius, rect.bottomLeftRadius);
        writeDouble(w, s::RectanglePrimitive::BottomRightRadius, rect.bottomRightRadius);
        return w;
    }
    case fla::Element::Type::Oval:
    {
        field = s::Element::Oval;
        const auto& oval = static_cast<const fla::OvalPrimitive&>(element);
        writeRectangle(w, s::OvalPrimitive::Rect, oval.rect.topLeft.x, oval.rect.topLeft.y, oval.rect.bottomRight.x,
            oval.rect.bottomRight.y);
        if (oval.fillStyle)
            w.message(s::OvalPrimitive::Fill, encodeFill(*oval.fillStyle));
        if (oval.strokeStyle)
            w.message(s::OvalPrimitive::Stroke, encodeStroke(*oval.strokeStyle));
        writeDouble(w, s::OvalPrimitive::StartAngle, oval.startAngle);
        writeDouble(w, s::OvalPrimitive::EndAngle, oval.endAngle);
        writeDouble(w, s::OvalPrimitive::InnerRadius, oval.innerRadius);
        return w;
    }
    case fla::Element::Type::Raw:
        field = s::Element::Raw;
        writeString(w, s::RawXml::Xml, static_cast<const fla::RawElement&>(element).xml);
        return w;
    }
    field = 0;
    return w;
}

Writer encodeElement(const fla::Element& element)
{
    Writer w;
    writeMatrix(w, s::Element::Matrix, element.transform);
    writeVector(w, s::Element::TransformationPoint, element.transformationPoint);
    writeBool(w, s::Element::IsLocked, element.isLocked);
    writeBool(w, s::Element::IsSelected, element.isSelected);
    writeBool(w, s::Element::IsFloating, element.isFloating);
    writeBool(w, s::Element::KeepMatrix, element.keepMatrix);
    writeBool(w, s::Element::KeepTransformationPoint, element.keepTransformationPoint);
    writeBool(w, s::Element::Visible, element.visible, true);
    writeStrings(w, s::Element::ExtraChildren, element.extraChildren);
    const fla::Rect& b = element.localBounds;
    writeRectangle(w, s::Element::LocalBounds, b.topLeft.x, b.topLeft.y, b.bottomRight.x, b.bottomRight.y);
    writeXfl(w, s::Element::Xfl, element);
    uint32_t field = 0;
    const Writer kind = encodeKind(element, field);
    if (field)
        w.message(field, kind);
    w.rawFields(element.phxUnknown);
    return w;
}

// ---------------------------------------------------------------------------
// Timelines

Writer encodeMorphShape(const fla::MorphShape& morph)
{
    Writer w;
    for (const fla::MorphSegment* segment : morph.segments)
    {
        if (!segment)
            continue;
        Writer sw;
        writeVector(sw, s::MorphSegment::StartA, segment->startPointA, true);
        writeVector(sw, s::MorphSegment::StartB, segment->startPointB, true);
        writeInt(sw, s::MorphSegment::StrokeIndex1, segment->strokeIndex1);
        writeInt(sw, s::MorphSegment::StrokeIndex2, segment->strokeIndex2);
        writeInt(sw, s::MorphSegment::FillIndex1, segment->fillIndex1);
        writeInt(sw, s::MorphSegment::FillIndex2, segment->fillIndex2);
        for (const fla::MorphCurves* curve : segment->curves)
        {
            if (!curve)
                continue;
            Writer cw;
            writeVector(cw, s::MorphCurve::ControlA, curve->controlPointA, true);
            writeVector(cw, s::MorphCurve::AnchorA, curve->anchorPointA, true);
            writeVector(cw, s::MorphCurve::ControlB, curve->controlPointB, true);
            writeVector(cw, s::MorphCurve::AnchorB, curve->anchorPointB, true);
            writeBool(cw, s::MorphCurve::IsLine, curve->isLine);
            writeXfl(cw, s::MorphCurve::Xfl, *curve);
            cw.rawFields(curve->phxUnknown);
            sw.message(s::MorphSegment::Curves, cw);
        }
        writeXfl(sw, s::MorphSegment::Xfl, *segment);
        sw.rawFields(segment->phxUnknown);
        w.message(s::MorphShape::Segments, sw);
    }
    writeString(w, s::MorphShape::HintsXml, morph.hintsXml);
    writeXfl(w, s::MorphShape::Xfl, morph);
    w.rawFields(morph.phxUnknown);
    return w;
}

Writer encodeFrame(const fla::Frame& frame)
{
    Writer w;
    writeInt(w, s::Frame::Index, frame.index);
    writeInt(w, s::Frame::Duration, frame.duration, 1);
    writeString(w, s::Frame::KeyMode, frame.keyMode);
    writeString(w, s::Frame::Name, frame.name);
    writeString(w, s::Frame::LabelType, frame.labelType);
    writeInt(w, s::Frame::TweenType, static_cast<int>(frame.tweenType));
    writeBool(w, s::Frame::MotionTweenSnap, frame.motionTWeenSnap);
    writeBool(w, s::Frame::MotionTweenSync, frame.motionTweenSync);
    writeSint(w, s::Frame::Acceleration, frame.acceleration);
    writeBool(w, s::Frame::HasCustomEase, frame.hasCustomEase);
    Packed ease;
    for (const fla::Point& point : frame.customEase)
    {
        ease.real(point.x);
        ease.real(point.y);
    }
    ease.writeTo(w, s::Frame::CustomEase);
    writeString(w, s::Frame::CustomEaseName, frame.customEaseName);
    writeBool(w, s::Frame::HasEase, frame.hasEase);
    writeString(w, s::Frame::EaseMethod, frame.easeMethod);
    writeStrings(w, s::Frame::OtherEases, frame.otherEases);
    writeString(w, s::Frame::MotionTweenRotate, frame.motionTweenRotate);
    writeInt(w, s::Frame::MotionTweenRotateTimes, frame.motionTweenRotateTimes);
    writeString(w, s::Frame::OtherTweenType, frame.otherTweenType);
    writeStrings(w, s::Frame::ExtraChildren, frame.extraChildren);
    if (frame.actionScript)
    {
        // Present even when empty: the frame had an Actionscript element.
        w.string(s::Frame::ActionScript, frame.actionScript->code);
        writeXfl(w, s::Frame::ActionScriptXfl, *frame.actionScript);
    }
    if (frame.morphShape)
        w.message(s::Frame::MorphShape, encodeMorphShape(*frame.morphShape));
    if (frame.morphSignature)
        w.fixed64(s::Frame::MorphSignature, frame.morphSignature);
    for (const fla::Element* element : frame.elements)
    {
        if (element)
            w.message(s::Frame::Elements, encodeElement(*element));
    }
    writeBool(w, s::Frame::Visible, frame.visible, true);
    writeXfl(w, s::Frame::Xfl, frame);
    w.rawFields(frame.phxUnknown);
    return w;
}

Writer encodeLayer(const fla::Layer& layer)
{
    Writer w;
    writeString(w, s::Layer::Name, layer.name);
    writeColor(w, s::Layer::Color, layer.color, kOpaqueBlack);
    writeString(w, s::Layer::Current, layer.current);
    writeInt(w, s::Layer::LayerType, static_cast<int>(layer.layerType));
    writeString(w, s::Layer::OtherLayerType, layer.otherLayerType);
    writeSint(w, s::Layer::ParentLayerIndex, layer.parentLayerIndex, -1);
    writeBool(w, s::Layer::AutoNamed, layer.autoNamed);
    writeBool(w, s::Layer::Selected, layer.selected);
    writeBool(w, s::Layer::Locked, layer.locked);
    writeBool(w, s::Layer::Outline, layer.outline);
    writeBool(w, s::Layer::Visible, layer.visible, true);
    for (const fla::Frame* frame : layer.frames)
    {
        if (frame)
            w.message(s::Layer::Frames, encodeFrame(*frame));
    }
    writeStrings(w, s::Layer::ExtraChildren, layer.extraChildren);
    writeXfl(w, s::Layer::Xfl, layer);
    w.rawFields(layer.phxUnknown);
    return w;
}

Writer encodeTimeline(const fla::Timeline& timeline)
{
    Writer w;
    writeString(w, s::Timeline::Name, timeline.name);
    writeBool(w, s::Timeline::LayerDepthEnabled, timeline.layerDepthEnabled);
    Packed guidesX;
    for (const double x : timeline.guidesX)
        guidesX.real(x);
    guidesX.writeTo(w, s::Timeline::GuidesX);
    Packed guidesY;
    for (const double y : timeline.guidesY)
        guidesY.real(y);
    guidesY.writeTo(w, s::Timeline::GuidesY);
    for (const fla::Layer* layer : timeline.layers)
    {
        if (layer)
            w.message(s::Timeline::Layers, encodeLayer(*layer));
    }
    writeStrings(w, s::Timeline::ExtraChildren, timeline.extraChildren);
    writeXfl(w, s::Timeline::Xfl, timeline);
    w.rawFields(timeline.phxUnknown);
    return w;
}

Writer encodeSymbol(const fla::Symbol& symbol)
{
    Writer w;
    writeString(w, s::Symbol::Name, symbol.name);
    writeString(w, s::Symbol::ItemId, symbol.itemId);
    writeString(w, s::Symbol::LastModified, symbol.lastModified);
    writeString(w, s::Symbol::Href, symbol.href);
    writeInt(w, s::Symbol::SymbolType, static_cast<int>(symbol.symbolType), static_cast<int>(fla::SymbolType::Graphic));
    writeString(w, s::Symbol::OtherSymbolType, symbol.otherSymbolType);
    writeKeyValues(w, s::Symbol::IncludeAttributes, symbol.includeAttributes);
    writeBool(w, s::Symbol::Scale9, symbol.scale9);
    writeRectangle(w, s::Symbol::ScaleGrid, symbol.scaleGridLeft, symbol.scaleGridTop, symbol.scaleGridRight,
        symbol.scaleGridBottom);
    for (const fla::Timeline* timeline : symbol.timelines)
    {
        if (timeline)
            w.message(s::Symbol::Timelines, encodeTimeline(*timeline));
    }
    writeXfl(w, s::Symbol::Xfl, symbol);
    w.rawFields(symbol.phxUnknown);
    return w;
}

// ---------------------------------------------------------------------------
// Media

/// Whether data is in an encoding deflate cannot shrink much.
bool alreadyCompressed(const std::vector<uint8_t>& d)
{
    const auto starts = [&d](std::initializer_list<uint8_t> magic, size_t at = 0) {
        if (d.size() < at + magic.size())
            return false;
        size_t i = at;
        for (const uint8_t byte : magic)
        {
            if (d[i++] != byte)
                return false;
        }
        return true;
    };
    return starts({0x89, 'P', 'N', 'G'}) || starts({0xFF, 0xD8}) || starts({'G', 'I', 'F'}) ||
        starts({0x03, 0x05}) /* Animate's lossless bitmap .dat, zlib inside */ || starts({'P', 'K'}) ||
        starts({'I', 'D', '3'}) || (d.size() > 1 && d[0] == 0xFF && (d[1] & 0xE0) == 0xE0) /* MPEG audio */ ||
        starts({'O', 'g', 'g', 'S'}) || starts({'f', 'L', 'a', 'C'}) || starts({'f', 't', 'y', 'p'}, 4);
}

std::string formatOf(const std::vector<uint8_t>& d)
{
    if (d.size() >= 4 && d[0] == 0x89 && d[1] == 'P')
        return "png";
    if (d.size() >= 2 && d[0] == 0xFF && d[1] == 0xD8)
        return "jpeg";
    if (d.size() >= 3 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F')
        return "gif";
    if (d.size() >= 2 && d[0] == 0x03 && d[1] == 0x05)
        return "flash-lossless";
    return std::string();
}

/// A media item to write. Its bytes stay in the document until its chunk is
/// prepared, and one already in a Phoenix file, as it was, is copied from
/// there as stored: never decompressed, checked or compressed again.
struct MediaJob
{
    uint32_t id = 0;
    uint32_t kind = 0;
    std::string packagePath;
    uint64_t hash = 0;
    uint64_t size = 0;
    /// Everything in the document holding these bytes, to point at the new
    /// file once it is written.
    std::vector<const fla::LazyBytes*> holders;
    /// Where the bytes lie as stored, when they are copied from there.
    std::shared_ptr<fla::ByteLocation> copyFrom;
};

/// Media met so far, by where they lie or by content, so the same bytes go
/// in once.
class MediaTable
{
public:
    /// The ids media in a Phoenix file have, kept by their copies, which new
    /// media must not take.
    void reserve(uint32_t id) { _reserved.insert(id); }

    /// The media id for \a bytes, new or the one the same bytes already
    /// have; 0 for none.
    uint32_t add(const fla::LazyBytes& bytes, uint32_t kind, const std::string& packagePath = std::string())
    {
        if (bytes.empty())
            return 0;
        const std::shared_ptr<fla::ByteLocation>& location = bytes.location();
        if (location)
        {
            const auto seen = _byLocation.find(location.get());
            if (seen != _byLocation.end())
            {
                _jobs[seen->second].holders.push_back(&bytes);
                return _jobs[seen->second].id;
            }
            // Copied with the id it had, unless another copy already has it.
            if (location->mediaId != 0 && !_used.count(location->mediaId))
            {
                MediaJob job;
                job.id = location->mediaId;
                job.kind = kind;
                job.packagePath = packagePath;
                job.hash = location->hash;
                job.size = location->size;
                job.holders.push_back(&bytes);
                job.copyFrom = location;
                _used.insert(job.id);
                _byLocation.emplace(location.get(), _jobs.size());
                _jobs.push_back(std::move(job));
                return _jobs.back().id;
            }
        }
        // Bytes in memory: by content.
        const std::vector<uint8_t>& data = bytes.get();
        if (data.empty())
            return 0;
        const uint64_t hash = fnv1a64(data.data(), data.size());
        const auto range = _byHash.equal_range(hash);
        for (auto it = range.first; it != range.second; ++it)
        {
            MediaJob& other = _jobs[it->second];
            if (!other.copyFrom && other.holders.front()->get() == data)
            {
                other.holders.push_back(&bytes);
                return other.id;
            }
        }
        MediaJob job;
        while (_used.count(_next) || _reserved.count(_next))
            ++_next;
        job.id = _next++;
        job.kind = kind;
        job.packagePath = packagePath;
        job.hash = hash;
        job.size = data.size();
        job.holders.push_back(&bytes);
        _used.insert(job.id);
        _byHash.emplace(hash, _jobs.size());
        _jobs.push_back(std::move(job));
        return _jobs.back().id;
    }

    const std::vector<MediaJob>& jobs() const { return _jobs; }

private:
    std::vector<MediaJob> _jobs;
    std::multimap<uint64_t, size_t> _byHash;
    std::map<const fla::ByteLocation*, size_t> _byLocation;
    std::set<uint32_t> _used;
    std::set<uint32_t> _reserved;
    uint32_t _next = 1;
};

/// The stored bytes of a chunk where they lie, checked against its
/// checksum; false when the file is gone or changed.
bool readStored(const fla::ByteLocation& location, std::string& out)
{
    std::ifstream file(std::filesystem::u8path(location.path), std::ios::binary);
    if (!file)
        return false;
    out.resize(static_cast<size_t>(location.storedLength));
    file.seekg(static_cast<std::streamoff>(location.offset + 32));
    if (!file.read(out.data(), static_cast<std::streamsize>(out.size())))
        return false;
    return crc32(out.data(), out.size()) == location.crc;
}

/// An MDIA chunk: a length-prefixed MediaHeader, then the bytes. False when
/// media to be copied could no longer be read.
bool prepareMedia(const MediaJob& job, PreparedChunk& chunk)
{
    if (job.copyFrom)
    {
        // As stored: the header inside it already has this id.
        const fla::ByteLocation& from = *job.copyFrom;
        if (readStored(from, chunk.stored))
        {
            chunk.info = ChunkInfo();
            chunk.info.type = kMedia;
            chunk.info.flags = from.flags;
            chunk.info.length = from.length;
            chunk.info.storedLength = from.storedLength;
            chunk.info.crc = from.crc;
            chunk.info.mediaId = job.id;
            return true;
        }
        // Gone from there: the bytes the document has read, if it has.
        if (!job.holders.front()->loaded())
            return false;
    }
    const std::vector<uint8_t>& data = job.holders.front()->get();
    if (data.empty())
        return false;
    Writer header;
    header.uint(s::MediaHeader::Id, job.id);
    if (job.kind)
        header.uint(s::MediaHeader::Kind, job.kind);
    writeString(header, s::MediaHeader::Format, formatOf(data));
    header.fixed64(s::MediaHeader::ContentHash, job.hash);
    writeString(header, s::MediaHeader::PackagePath, job.packagePath);
    Writer content;
    content.varint(header.buffer().size());
    content.rawFields(header.buffer());
    content.buffer().reserve(content.buffer().size() + data.size());
    content.buffer().append(reinterpret_cast<const char*>(data.data()), data.size());
    chunk = prepare(kMedia, std::move(content.buffer()), !alreadyCompressed(data), true, job.id);
    return true;
}

std::string isoNow()
{
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

} // namespace

// ---------------------------------------------------------------------------
// The document

bool save(const fla::Document& document, const std::string& path, const SaveOptions& options, std::string& error)
{
    ContainerWriter out;
    if (!out.open(path, error))
        return false;
    MediaTable media;
    // Media copied from a Phoenix file keep their ids; new ones go round them.
    for (const fla::Resource* resource : document.resources)
    {
        if (resource && resource->resourceType() == fla::Resource::Type::Bitmap)
        {
            for (const fla::LazyBytes* bytes : {&static_cast<const fla::Bitmap*>(resource)->datData,
                     &static_cast<const fla::Bitmap*>(resource)->imageData})
            {
                if (bytes->location())
                    media.reserve(bytes->location()->mediaId);
            }
        }
    }
    for (const auto& file : document.extraFiles)
    {
        if (file.second.location())
            media.reserve(file.second.location()->mediaId);
    }

    Writer w;
    writeInt(w, s::Document::Width, document.width, 550);
    writeInt(w, s::Document::Height, document.height, 400);
    writeDouble(w, s::Document::FrameRate, document.frameRate, 24.0);
    writeColor(w, s::Document::BackgroundColor, document.backgroundColor, kOpaqueWhite);
    writeColor(w, s::Document::GridColor, document.gridColor, kOpaqueBlack);
    writeColor(w, s::Document::GuidesColor, document.guidesColor, 0xFF00FF00);
    writeInt(w, s::Document::GridSpacingX, document.gridSpacingX, 18);
    writeInt(w, s::Document::GridSpacingY, document.gridSpacingY, 18);
    writeInt(w, s::Document::SnapAlignBorderSpacing, document.snapAlignBorderSpacing, 18);
    writeBool(w, s::Document::ObjectsSnapTo, document.objectsSnapTo, true);
    writeBool(w, s::Document::GuidesSnapTo, document.guidesSnapTo, true);
    writeInt(w, s::Document::TimelineHeight, document.timelineHeight, 197);
    writeInt(w, s::Document::TimelineLabelWidth, document.timelineLabelWidth, 256);
    writeDouble(w, s::Document::VanishingPointX, document.vanishingPoint3DX, 400.0);
    writeDouble(w, s::Document::VanishingPointY, document.vanishingPoint3DY, 275.0);
    writeDouble(w, s::Document::ViewAngle3D, document.viewAngle3D);
    writeInt(w, s::Document::NextSceneIdentifier, document.nextSceneIdentifier, 2);
    writeBool(w, s::Document::PlayLoop, document.playOptionsPlayLoop, true);
    writeBool(w, s::Document::PlayPages, document.playOptionsPlayPages, true);
    writeBool(w, s::Document::PlayFrameActions, document.playOptionsPlayFrameActions, true);
    writeString(w, s::Document::CurrentTimeline, document.currentTimeline);
    writeBool(w, s::Document::HasSymbolList, document.symbolList != nullptr);

    for (const fla::Timeline* scene : document.timelines)
    {
        if (scene)
            w.message(s::Document::Scenes, encodeTimeline(*scene));
    }
    for (const fla::Folder* folder : document.folders)
    {
        if (!folder)
            continue;
        Writer f;
        writeString(f, s::LibraryFolder::Name, folder->name);
        writeString(f, s::LibraryFolder::ItemId, folder->itemId);
        writeBool(f, s::LibraryFolder::IsExpanded, folder->isExpanded);
        writeXfl(f, s::LibraryFolder::Xfl, *folder);
        f.rawFields(folder->phxUnknown);
        w.message(s::Document::Folders, f);
    }
    for (const fla::Resource* resource : document.resources)
    {
        if (!resource || resource->resourceType() != fla::Resource::Type::Bitmap)
            continue;
        const auto& bitmap = static_cast<const fla::Bitmap&>(*resource);
        Writer b;
        writeString(b, s::BitmapItem::Name, bitmap.name);
        writeString(b, s::BitmapItem::ItemId, bitmap.itemId);
        writeString(b, s::BitmapItem::SourceExternalFilepath, bitmap.sourceExternalFilepath);
        writeInt(b, s::BitmapItem::SourceLastImported, bitmap.sourceLastImported);
        writeInt(b, s::BitmapItem::ExternalFileCrc32, bitmap.externalFileCRC32);
        writeInt(b, s::BitmapItem::ExternalFileSize, bitmap.externalFileSize);
        writeBool(b, s::BitmapItem::AllowSmoothing, bitmap.allowSmoothing);
        writeInt(b, s::BitmapItem::Quality, bitmap.quality);
        writeString(b, s::BitmapItem::Href, bitmap.href);
        writeString(b, s::BitmapItem::BitmapDataHref, bitmap.bitmapDataHRef);
        writeInt(b, s::BitmapItem::FrameRight, bitmap.frameRight);
        writeInt(b, s::BitmapItem::FrameBottom, bitmap.frameBottom);
        writeBool(b, s::BitmapItem::IsJpeg, bitmap.isJPEG);
        writeInt(b, s::BitmapItem::DataMedia, media.add(bitmap.datData, s::MediaBitmap));
        writeInt(b, s::BitmapItem::ImageMedia, media.add(bitmap.imageData, s::MediaBitmap));
        writeXfl(b, s::BitmapItem::Xfl, bitmap);
        b.rawFields(bitmap.phxUnknown);
        w.message(s::Document::Bitmaps, b);
    }
    for (const fla::Document::SoundInfo& sound : document.sounds)
    {
        Writer so;
        writeString(so, s::SoundItem::Name, sound.name);
        writeString(so, s::SoundItem::Format, sound.format);
        writeString(so, s::SoundItem::DataPath, sound.dataPath);
        w.message(s::Document::Sounds, so);
    }
    if (document.symbolList)
    {
        for (const fla::Symbol* symbol : document.symbolList->symbols)
        {
            if (symbol)
                w.string(s::Document::SymbolOrder, symbol->name);
        }
    }
    for (const fla::SwatchList* list : document.swatchLists)
    {
        if (!list)
            continue;
        Writer l;
        writeString(l, s::SwatchList::Name, list->name);
        Packed colors;
        for (const fla::Swatch* swatch : list->swatches)
        {
            if (swatch && swatch->swatchType() == fla::Swatch::Type::Solid)
                colors.fixed32(packColor(static_cast<const fla::SolidSwatch*>(swatch)->color));
        }
        colors.writeTo(l, s::SwatchList::Colors);
        writeString(l, s::SwatchList::Xml, list->xml);
        w.message(s::Document::SwatchLists, l);
    }
    for (const auto& file : document.extraFiles)
    {
        bool sound = false;
        for (const fla::Document::SoundInfo& info : document.sounds)
            sound = sound || info.dataPath == file.first;
        Writer p;
        writeString(p, s::PackageFile::Path, file.first);
        writeInt(p, s::PackageFile::Media, media.add(file.second, sound ? s::MediaSound : s::MediaPackageFile, file.first));
        w.message(s::Document::PackageFiles, p);
    }
    writeStrings(w, s::Document::Features, document.phxFeatures);
    writeStrings(w, s::Document::RequiredFeatures, document.phxRequiredFeatures);

    Writer x;
    writeString(x, s::XflDocument::FiletypeGuid, document.filetypeGUID);
    writeString(x, s::XflDocument::FileGuid, document.fileGUID);
    writeString(x, s::XflDocument::XflVersion, document.xflVersion);
    writeString(x, s::XflDocument::CreatorInfo, document.creatorInfo);
    writeString(x, s::XflDocument::Platform, document.platform);
    writeString(x, s::XflDocument::VersionInfo, document.versionInfo);
    writeInt(x, s::XflDocument::MajorVersion, document.majorVersion);
    writeInt(x, s::XflDocument::BuildNumber, document.buildNumber);
    if (document.scriptList)
        x.string(s::XflDocument::ScriptListXml, document.scriptList->xml);
    if (document.publishHistory)
        x.string(s::XflDocument::PublishHistoryXml, document.publishHistory->xml);
    if (document.printerSettings)
        x.string(s::XflDocument::PrinterSettingsXml, document.printerSettings->xml);
    for (const fla::Document::RawSection& section : document.extraSections)
    {
        Writer r;
        writeString(r, s::RawSection::Xml, section.xml);
        writeBool(r, s::RawSection::BeforeTimelines, section.beforeTimelines);
        x.message(s::XflDocument::ExtraSections, r);
    }
    writeStrings(x, s::XflDocument::ExtraMedia, document.extraMedia);
    writeXfl(x, s::XflDocument::Detail, document);
    if (!x.empty())
        w.message(s::Document::Xfl, x);
    w.rawFields(document.phxUnknown);

    out.add(kDocument, w.buffer(), true, true);

    // Symbols encoded and every chunk compressed and checksummed on several
    // threads, then written in order.
    std::vector<const fla::Symbol*> symbols;
    if (document.symbolList)
    {
        for (const fla::Symbol* symbol : document.symbolList->symbols)
        {
            if (symbol)
                symbols.push_back(symbol);
        }
    }
    const std::vector<MediaJob>& jobs = media.jobs();
    std::vector<PreparedChunk> prepared(symbols.size() + jobs.size());
    std::vector<char> missing(jobs.size(), 0);
    parallelFor(prepared.size(), [&](size_t i) {
        if (i < symbols.size())
            prepared[i] = prepare(kSymbol, std::move(encodeSymbol(*symbols[i]).buffer()), true, true, 0, symbols[i]->name);
        else if (!prepareMedia(jobs[i - symbols.size()], prepared[i]))
            missing[i - symbols.size()] = 1;
    });
    // Media that could not be read would be lost: the save stops instead.
    for (size_t j = 0; j < jobs.size(); ++j)
    {
        if (missing[j])
        {
            const fla::ByteLocation* from = jobs[j].copyFrom.get();
            error = "Some of the document's media could not be read" +
                (from ? " from " + from->path + " (was it moved or changed?)" : std::string()) +
                ". Nothing was saved.";
            return false;
        }
    }
    std::vector<ChunkInfo> written(jobs.size());
    for (size_t i = 0; i < prepared.size(); ++i)
    {
        out.add(prepared[i]);
        if (i >= symbols.size() && !out.chunks().empty())
            written[i - symbols.size()] = out.chunks().back();
        prepared[i].stored.clear();
        prepared[i].stored.shrink_to_fit();
    }

    for (const fla::Document::UnknownChunk& chunk : document.phxUnknownChunks)
        out.add(chunk.type, chunk.content, true, (chunk.flags & kCritical) != 0);

    if (!options.thumbnailPng.empty())
        out.add(kThumbnail, options.thumbnailPng.data(), options.thumbnailPng.size(), false, false);
    Writer meta;
    writeString(meta, s::Metadata::Application, options.application);
    meta.string(s::Metadata::Modified, isoNow());
    out.add(kMetadata, meta.buffer(), true, false);

    if (!out.finish(error))
        return false;

    // The media now lie in the new file: everything holding them reads them,
    // and the next save copies them, from there.
    if (options.relocateMedia)
    {
        std::string absolute = path;
        std::error_code ignored;
        const std::filesystem::path full = std::filesystem::absolute(std::filesystem::u8path(path), ignored);
        if (!ignored)
            absolute = full.u8string();
        for (size_t j = 0; j < jobs.size(); ++j)
        {
            const MediaJob& job = jobs[j];
            const ChunkInfo& chunk = written[j];
            auto location = job.copyFrom ? job.copyFrom : std::make_shared<fla::ByteLocation>();
            location->load = &loadMedia;
            location->path = absolute;
            location->offset = chunk.offset;
            location->storedLength = chunk.storedLength;
            location->length = chunk.length;
            location->flags = chunk.flags;
            location->crc = chunk.crc;
            location->type = kMedia;
            location->mediaId = job.id;
            location->size = job.size;
            location->hash = job.hash;
            for (const fla::LazyBytes* holder : job.holders)
                holder->relocate(location);
        }
    }
    return true;
}

} // namespace phx
