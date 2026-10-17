// The fill and stroke scripts set: on the toolbar, for the shapes drawn
// next, and on the selected shapes, as the Properties panel sets them.
// document.setFillColor, setStrokeColor, setStrokeSize, setStroke,
// setStrokeStyle, setCustomFill and setCustomStroke change both; their
// getters read either.

#include "extensions.h"
#include "model.h"

#include "../data/fill_style.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "../edit/drawing_style.h"
#include "../edit/element_clone.h"
#include "../edit/element_commands.h"

#include <algorithm>
#include <functional>
#include <memory>

namespace jsfl {

namespace {

fla::DrawingStyle& toolbar(Document& d)
{
    d.data();
    if (fla::DrawingStyle* style = d.session.host().drawingStyle())
        return *style;
    static fla::DrawingStyle fallback;
    return fallback;
}

/// The kind of stroke a script names ("solid", "dashed", "dotted",
/// "ragged", "stipple", "hatched"); false for anything else, "noStroke"
/// among them.
bool strokeKind(const std::string& name, fla::StrokeStyle::Style& kind)
{
    static const std::pair<const char*, fla::StrokeStyle::Style> kinds[] = {
        {"solid", fla::StrokeStyle::Style::Solid}, {"dashed", fla::StrokeStyle::Style::Dashed},
        {"dotted", fla::StrokeStyle::Style::Dotted}, {"ragged", fla::StrokeStyle::Style::Ragged},
        {"stipple", fla::StrokeStyle::Style::Stipple}, {"hatched", fla::StrokeStyle::Style::Hatched}};
    for (const auto& [text, value] : kinds)
    {
        if (name == text)
        {
            kind = value;
            return true;
        }
    }
    return false;
}

/// The selected shapes and drawing objects: each changed by \a change on a
/// copy, which says whether it changed anything, and put back as part of
/// the script's step. How many changed.
int restyleSelection(Document& d, const char* name, const std::function<bool(fla::Shape&)>& change)
{
    int changed = 0;
    for (fla::Element* element : selectedElements(d.session, d.document))
    {
        auto* shape = dynamic_cast<fla::Shape*>(element);
        if (!shape)
            continue;
        std::unique_ptr<fla::Shape> after(static_cast<fla::Shape*>(fla::cloneElement(*shape, nullptr)));
        if (!change(*after))
            continue;
        d.push(fla::CommandPtr(new fla::SetShapeContentsCommand(shape, *after, name)), false);
        ++changed;
    }
    return changed;
}

/// \a slot's stroke replaced by \a restyled, where the edges find it too.
void replaceStroke(fla::Shape& shape, fla::StrokeStyle*& slot, fla::StrokeStyle* restyled)
{
    for (auto& entry : shape.strokesMap)
    {
        if (entry.second == slot)
            entry.second = restyled;
    }
    delete slot;
    slot = restyled;
}

void replaceFill(fla::Shape& shape, fla::FillStyle*& slot, fla::FillStyle* replacement)
{
    for (auto& entry : shape.fillsMap)
    {
        if (entry.second == slot)
            entry.second = replacement;
    }
    delete slot;
    slot = replacement;
}

bool strokeColor(fla::Shape& shape, const uint8_t color[4])
{
    for (fla::StrokeStyle* stroke : shape.strokes)
    {
        if (!stroke)
            continue;
        if (stroke->fill && stroke->fill->type() == fla::FillStyle::Type::SolidColor)
        {
            std::copy(color, color + 4, static_cast<fla::SolidColor*>(stroke->fill)->color);
            continue;
        }
        auto* solid = new fla::SolidColor(stroke);
        std::copy(color, color + 4, solid->color);
        delete stroke->fill;
        stroke->fill = solid;
    }
    return !shape.strokes.empty();
}

bool strokeWeight(fla::Shape& shape, double weight)
{
    for (fla::StrokeStyle* stroke : shape.strokes)
    {
        if (!stroke)
            continue;
        stroke->weight = weight;
        // A hairline is a width of its own; a size given is not one.
        if (stroke->isHairline() && weight > 0.1)
            stroke->solidStyle.clear();
    }
    return !shape.strokes.empty();
}

bool strokeLook(fla::Shape& shape, const std::function<void(fla::StrokeLook&)>& change)
{
    for (fla::StrokeStyle*& stroke : shape.strokes)
    {
        if (!stroke)
            continue;
        fla::StrokeLook look = fla::StrokeLook::of(*stroke);
        change(look);
        replaceStroke(shape, stroke, look.applyTo(*stroke, &shape));
    }
    return !shape.strokes.empty();
}

bool fillColor(fla::Shape& shape, const uint8_t color[4])
{
    for (fla::FillStyle*& fill : shape.fills)
    {
        if (!fill)
            continue;
        if (fill->type() == fla::FillStyle::Type::SolidColor)
        {
            std::copy(color, color + 4, static_cast<fla::SolidColor*>(fill)->color);
            continue;
        }
        auto* solid = new fla::SolidColor(&shape);
        std::copy(color, color + 4, solid->color);
        replaceFill(shape, fill, solid);
    }
    return !shape.fills.empty();
}

/// The first selected shape, for the getters' "selection".
const fla::Shape* firstSelectedShape(Document& d)
{
    for (fla::Element* element : selectedElements(d.session, d.document))
    {
        if (const auto* shape = dynamic_cast<const fla::Shape*>(element))
            return shape;
    }
    return nullptr;
}

/// A stroke object's look over \a look: kind, caps, joins, scaling,
/// hinting, dashes and the kinds' own attributes.
void lookFrom(const Value& stroke, fla::StrokeLook& look)
{
    if (const Value* style = stroke.field("style"))
    {
        fla::StrokeStyle::Style kind;
        if (strokeKind(style->toString(), kind) && kind != look.kind)
        {
            look.kind = kind;
            look.styleAttributes.clear();
        }
    }
    if (const Value* caps = stroke.field("capType"))
        look.caps = caps->toString() == "round" ? std::string() : caps->toString();
    if (const Value* joins = stroke.field("joinType"))
        look.joints = joins->toString() == "round" ? std::string() : joins->toString();
    if (const Value* scale = stroke.field("scaleType"))
        look.scaleMode = scale->toString() == "normal" ? std::string() : scale->toString();
    if (const Value* miter = stroke.field("miterLimit"))
        look.miterLimit = miter->toNumber();
    if (const Value* hinting = stroke.field("strokeHinting"))
        look.hinting = hinting->toBool();
    if (const Value* dash = stroke.field("dash1"))
        look.dash1 = dash->toNumber();
    if (const Value* gap = stroke.field("dash2"))
        look.dash2 = gap->toNumber();
    if (const Value* space = stroke.field("dotSpace"))
        look.dotSpace = space->toNumber();
    // The other kinds' settings by the names the file uses (curve,
    // waveHeight, pattern, dotSize, density, ...).
    for (const char* key : {"pattern", "waveHeight", "waveLength", "dotSize", "variation", "density", "hatchThickness",
             "space", "jiggle", "rotate", "curve", "length"})
    {
        if (const Value* value = stroke.field(key))
            look.styleAttributes[key] = value->toString();
    }
}

} // namespace

void describeDocumentStyle(ClassDef<Document>& def)
{
    // The fill and stroke of the toolbar, or with "selection" of the
    // first selected shape.
    def.method("getCustomFill", 1, [](Document& d, const Args& args) {
        if (arg(args, 0).toString() == "selection")
        {
            const fla::Shape* shape = firstSelectedShape(d);
            return fillToValue(shape && !shape->fills.empty() ? shape->fills.front() : nullptr);
        }
        const fla::DrawingStyle& style = toolbar(d);
        Value fill = Value::map();
        fill.set("style", style.hasFill ? "solid" : "noFill");
        fill.set("color", colorText(style.fillColor));
        return fill;
    });
    def.method("getCustomStroke", 1, [](Document& d, const Args& args) {
        if (arg(args, 0).toString() == "selection")
        {
            const fla::Shape* shape = firstSelectedShape(d);
            return strokeToValue(shape && !shape->strokes.empty() ? shape->strokes.front() : nullptr);
        }
        const fla::DrawingStyle& style = toolbar(d);
        if (!style.hasStroke)
        {
            Value none = Value::map();
            none.set("style", "noStroke");
            return none;
        }
        std::unique_ptr<fla::StrokeStyle> stroke(style.createStroke(nullptr));
        return strokeToValue(stroke.get());
    });

    def.method("setFillColor", 1, [](Document& d, const Args& args) {
        fla::DrawingStyle& style = toolbar(d);
        style.hasFill = !arg(args, 0).isNullish();
        if (!style.hasFill)
            return Value();
        if (!parseColor(arg(args, 0), style.fillColor))
            throw Error("That is not a color: " + arg(args, 0).toString());
        uint8_t color[4];
        std::copy(style.fillColor, style.fillColor + 4, color);
        restyleSelection(d, "Fill Color", [&color](fla::Shape& shape) { return fillColor(shape, color); });
        return Value();
    });
    def.method("setStrokeColor", 1, [](Document& d, const Args& args) {
        fla::DrawingStyle& style = toolbar(d);
        style.hasStroke = !arg(args, 0).isNullish();
        if (!style.hasStroke)
            return Value();
        if (!parseColor(arg(args, 0), style.strokeColor))
            throw Error("That is not a color: " + arg(args, 0).toString());
        uint8_t color[4];
        std::copy(style.strokeColor, style.strokeColor + 4, color);
        restyleSelection(d, "Stroke Color", [&color](fla::Shape& shape) { return strokeColor(shape, color); });
        return Value();
    });
    def.method("setStrokeSize", 1, [](Document& d, const Args& args) {
        const double weight = std::clamp(arg(args, 0).toNumber(), 0.1, 200.0);
        toolbar(d).strokeWeight = weight;
        restyleSelection(d, "Stroke Size", [weight](fla::Shape& shape) { return strokeWeight(shape, weight); });
        return Value();
    });
    def.method("setStrokeStyle", 1, [](Document& d, const Args& args) {
        const std::string name = arg(args, 0).toString();
        fla::DrawingStyle& style = toolbar(d);
        if (name == "noStroke")
        {
            style.hasStroke = false;
            return Value();
        }
        fla::StrokeStyle::Style kind;
        if (!strokeKind(name, kind))
            throw Error("A stroke style is solid, dashed, dotted, ragged, stipple, hatched or noStroke, not " + name + ".");
        style.hasStroke = true;
        if (style.strokeLook.kind != kind)
        {
            style.strokeLook.kind = kind;
            style.strokeLook.styleAttributes.clear();
        }
        restyleSelection(d, "Stroke Style", [kind](fla::Shape& shape) {
            return strokeLook(shape, [kind](fla::StrokeLook& look) {
                if (look.kind != kind)
                    look.styleAttributes.clear();
                look.kind = kind;
            });
        });
        return Value();
    });
    // setStroke(color, size, style): all three at once.
    def.method("setStroke", 3, [](Document& d, const Args& args) {
        fla::DrawingStyle& style = toolbar(d);
        const std::string name = given(args, 2) ? arg(args, 2).toString() : std::string("solid");
        if (name == "noStroke")
        {
            style.hasStroke = false;
            return Value();
        }
        fla::StrokeStyle::Style kind;
        if (!strokeKind(name, kind))
            throw Error("A stroke style is solid, dashed, dotted, ragged, stipple, hatched or noStroke, not " + name + ".");
        uint8_t color[4];
        std::copy(style.strokeColor, style.strokeColor + 4, color);
        if (given(args, 0) && !parseColor(arg(args, 0), color))
            throw Error("That is not a color: " + arg(args, 0).toString());
        const double weight = given(args, 1) ? std::clamp(arg(args, 1).toNumber(), 0.1, 200.0) : style.strokeWeight;
        style.hasStroke = true;
        std::copy(color, color + 4, style.strokeColor);
        style.strokeWeight = weight;
        if (style.strokeLook.kind != kind)
        {
            style.strokeLook.kind = kind;
            style.strokeLook.styleAttributes.clear();
        }
        restyleSelection(d, "Stroke", [&](fla::Shape& shape) {
            strokeLook(shape, [kind](fla::StrokeLook& look) {
                if (look.kind != kind)
                    look.styleAttributes.clear();
                look.kind = kind;
            });
            strokeWeight(shape, weight);
            return strokeColor(shape, color);
        });
        return Value();
    });
    def.method("setCustomFill", 1, [](Document& d, const Args& args) {
        fla::DrawingStyle& style = toolbar(d);
        const Value& fill = arg(args, 0);
        if (const Value* kind = fill.field("style"))
            style.hasFill = kind->toString() != "noFill";
        if (const Value* color = fill.field("color"))
            parseColor(*color, style.fillColor);
        if (!style.hasFill || !fill.field("color"))
            return Value();
        uint8_t color[4];
        std::copy(style.fillColor, style.fillColor + 4, color);
        restyleSelection(d, "Fill", [&color](fla::Shape& shape) { return fillColor(shape, color); });
        return Value();
    });
    def.method("setCustomStroke", 1, [](Document& d, const Args& args) {
        fla::DrawingStyle& style = toolbar(d);
        const Value& stroke = arg(args, 0);
        if (const Value* kind = stroke.field("style"))
            style.hasStroke = kind->toString() != "noStroke";
        if (const Value* color = stroke.field("color"))
            parseColor(*color, style.strokeColor);
        if (const Value* thickness = stroke.field("thickness"))
            style.strokeWeight = std::clamp(thickness->toNumber(), 0.1, 200.0);
        lookFrom(stroke, style.strokeLook);
        if (!style.hasStroke)
            return Value();
        uint8_t color[4];
        std::copy(style.strokeColor, style.strokeColor + 4, color);
        const bool hasColor = stroke.field("color") != nullptr;
        const Value* thickness = stroke.field("thickness");
        const double weight = style.strokeWeight;
        restyleSelection(d, "Stroke", [&](fla::Shape& shape) {
            strokeLook(shape, [&stroke](fla::StrokeLook& look) { lookFrom(stroke, look); });
            if (thickness)
                strokeWeight(shape, weight);
            if (hasColor)
                strokeColor(shape, color);
            return !shape.strokes.empty();
        });
        return Value();
    });
}

} // namespace jsfl
