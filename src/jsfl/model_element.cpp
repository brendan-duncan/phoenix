// Element and its kinds: Instance, SymbolInstance, BitmapInstance, Text and
// Shape. Transforms read and written as JSFL's scale, rotation and skew,
// color effects and filters.

#include "extensions.h"
#include "model.h"

#include "../data/bitmap_instance.h"
#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/static_text.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../edit/element_clone.h"
#include "../edit/element_commands.h"
#include "../edit/property_command.h"
#include "../edit/selection.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <cstdint>
#include <functional>
#include <memory>

namespace jsfl {

/// Moves the transformation point of e to stage, leaving the element in place.
void setPivot(Element& e, const fla::Point& stage);

namespace {

constexpr double kPi = 3.14159265358979323846;

double degrees(double radians)
{
    return radians * 180.0 / kPi;
}

double radians(double degrees)
{
    return degrees * kPi / 180.0;
}

/// A matrix as JSFL describes it: scale on each axis and the turn of
/// each axis (equal for a plain rotation).
struct Parts
{
    double scaleX;
    double scaleY;
    double skewX;
    double skewY;
};

Parts partsOf(const fla::Transform& m)
{
    return {std::hypot(m.m11, m.m12), std::hypot(m.m21, m.m22), degrees(std::atan2(-m.m21, m.m22)),
        degrees(std::atan2(m.m12, m.m11))};
}

/// \a m with new parts, keeping the transformation point \a pivot (in the
/// element's own space) where it is on the stage.
fla::Transform withParts(const fla::Transform& m, const Parts& parts, const fla::Point& pivot)
{
    fla::Transform result(parts.scaleX * std::cos(radians(parts.skewY)), parts.scaleX * std::sin(radians(parts.skewY)),
        -parts.scaleY * std::sin(radians(parts.skewX)), parts.scaleY * std::cos(radians(parts.skewX)), 0.0, 0.0);
    const double px = m.m11 * pivot.x + m.m21 * pivot.y + m.tx;
    const double py = m.m12 * pivot.x + m.m22 * pivot.y + m.ty;
    result.tx = px - (result.m11 * pivot.x + result.m21 * pivot.y);
    result.ty = py - (result.m12 * pivot.x + result.m22 * pivot.y);
    return result;
}

double normalized(double angle)
{
    double result = std::remainder(angle, 360.0);
    if (result <= -180.0)
        result += 360.0;
    return result;
}

fla::SymbolInstance* instanceOf(Element& e)
{
    return dynamic_cast<fla::SymbolInstance*>(&e.get());
}

fla::SymbolInstance& needInstance(Element& e)
{
    fla::SymbolInstance* instance = instanceOf(e);
    if (!instance)
        throw Error("That is for symbol instances.");
    return *instance;
}

fla::StaticText& needText(Element& e)
{
    auto* text = dynamic_cast<fla::StaticText*>(&e.get());
    if (!text)
        throw Error("That is for text fields.");
    return *text;
}

void setColor(Element& e, const std::function<void(fla::ColorTransform&)>& change)
{
    fla::SymbolInstance& instance = needInstance(e);
    fla::ColorTransform after = instance.colorTransform;
    change(after);
    e.push(fla::CommandPtr(new fla::SetPropertyCommand<fla::ColorTransform>(instance.colorTransform, instance.colorTransform,
               after, "Color Effect")),
        false);
}

std::string colorModeOf(const fla::ColorTransform& color)
{
    if (color.isIdentity())
        return "none";
    const bool plain = color.redMultiplier == 1.0 && color.greenMultiplier == 1.0 && color.blueMultiplier == 1.0 &&
        color.redOffset == 0 && color.greenOffset == 0 && color.blueOffset == 0 && color.alphaOffset == 0;
    if (color.tintMultiplier != 0.0 && plain && color.brightness == 0.0 && color.alphaMultiplier == 1.0)
        return "tint";
    if (color.brightness != 0.0 && plain && color.tintMultiplier == 0.0 && color.alphaMultiplier == 1.0)
        return "brightness";
    if (plain && color.tintMultiplier == 0.0 && color.brightness == 0.0)
        return "alpha";
    return "advanced";
}

const char* loopName(fla::LoopType loop)
{
    switch (loop)
    {
    case fla::LoopType::SingleFrame: return "single frame";
    case fla::LoopType::Loop: return "loop";
    case fla::LoopType::PingPong: return "loop";
    case fla::LoopType::LoopReverse: return "loop reverse";
    case fla::LoopType::PlayOnceReverse: return "play once reverse";
    default: return "play once";
    }
}

fla::LoopType loopNamed(const std::string& name)
{
    if (name == "single frame")
        return fla::LoopType::SingleFrame;
    if (name == "loop")
        return fla::LoopType::Loop;
    if (name == "play once")
        return fla::LoopType::PlayOnce;
    if (name == "loop reverse")
        return fla::LoopType::LoopReverse;
    if (name == "play once reverse")
        return fla::LoopType::PlayOnceReverse;
    throw Error("Looping is loop, play once, single frame, loop reverse or play once reverse, not " + name + ".");
}

const char* symbolTypeName(fla::SymbolType type)
{
    switch (type)
    {
    case fla::SymbolType::MovieClip: return "movie clip";
    case fla::SymbolType::Button: return "button";
    default: return "graphic";
    }
}

fla::SymbolType symbolTypeNamed(const std::string& name)
{
    if (name == "movie clip")
        return fla::SymbolType::MovieClip;
    if (name == "button")
        return fla::SymbolType::Button;
    if (name == "graphic")
        return fla::SymbolType::Graphic;
    throw Error("A symbol is a movie clip, button or graphic, not " + name + ".");
}

// Filters, as the plain objects scripts read and write.

const char* filterName(fla::Filter::Type type)
{
    switch (type)
    {
    case fla::Filter::Type::Blur: return "blurFilter";
    case fla::Filter::Type::DropShadow: return "dropShadowFilter";
    case fla::Filter::Type::Glow: return "glowFilter";
    case fla::Filter::Type::Bevel: return "bevelFilter";
    case fla::Filter::Type::GradientGlow: return "gradientGlowFilter";
    case fla::Filter::Type::GradientBevel: return "gradientBevelFilter";
    case fla::Filter::Type::AdjustColor: return "adjustColorFilter";
    case fla::Filter::Type::ColorMatrix: return "colorMatrixFilter";
    case fla::Filter::Type::Convolution: return "convolutionFilter";
    }
    return "blurFilter";
}

Value filterValue(const fla::Filter& filter)
{
    Value value = Value::map();
    value.set("name", filterName(filter.type));
    value.set("enabled", filter.enabled);
    switch (filter.type)
    {
    case fla::Filter::Type::AdjustColor:
        value.set("brightness", filter.brightness);
        value.set("contrast", filter.contrast);
        value.set("saturation", filter.saturation);
        value.set("hue", filter.hue);
        return value;
    case fla::Filter::Type::ColorMatrix:
    case fla::Filter::Type::Convolution:
    {
        Value matrix = Value::array();
        for (double each : filter.matrix)
            matrix.items.emplace_back(each);
        value.set("matrix", matrix);
        if (filter.type == fla::Filter::Type::Convolution)
        {
            value.set("matrixX", filter.matrixX);
            value.set("matrixY", filter.matrixY);
            value.set("divisor", filter.divisor);
            value.set("bias", filter.bias);
            value.set("preserveAlpha", filter.preserveAlpha);
            value.set("clamp", filter.clamp);
            value.set("color", colorText(filter.color));
        }
        return value;
    }
    default:
        break;
    }
    value.set("blurX", filter.blurX);
    value.set("blurY", filter.blurY);
    value.set("quality", filter.quality >= 3 ? "high" : filter.quality == 2 ? "medium" : "low");
    if (filter.type == fla::Filter::Type::Blur)
        return value;
    value.set("strength", std::round(filter.strength * 100.0));
    value.set("knockout", filter.knockout);
    if (filter.type == fla::Filter::Type::DropShadow || filter.type == fla::Filter::Type::Glow)
    {
        value.set("color", colorText(filter.color));
        value.set("inner", filter.inner);
    }
    if (filter.type == fla::Filter::Type::DropShadow || filter.type == fla::Filter::Type::Bevel ||
        filter.type == fla::Filter::Type::GradientBevel || filter.type == fla::Filter::Type::GradientGlow)
    {
        value.set("angle", filter.angle);
        value.set("distance", filter.distance);
    }
    if (filter.type == fla::Filter::Type::DropShadow)
        value.set("hideObject", filter.hideObject);
    if (filter.type == fla::Filter::Type::Bevel)
    {
        value.set("highlightColor", colorText(filter.highlightColor));
        value.set("shadowColor", colorText(filter.shadowColor));
    }
    if (filter.type == fla::Filter::Type::Bevel || filter.type == fla::Filter::Type::GradientBevel ||
        filter.type == fla::Filter::Type::GradientGlow)
        value.set("type", filter.bevelType.empty() ? std::string("inner") : filter.bevelType);
    if (filter.type == fla::Filter::Type::GradientBevel || filter.type == fla::Filter::Type::GradientGlow)
    {
        Value colors = Value::array();
        Value positions = Value::array();
        for (const fla::GradientEntry& entry : filter.entries)
        {
            colors.items.emplace_back(colorText(entry.color));
            positions.items.emplace_back(std::round(entry.ratio * 255.0));
        }
        value.set("colorArray", colors);
        value.set("posArray", positions);
    }
    return value;
}

fla::Filter filterFrom(const Value& value, const fla::Filter* base)
{
    fla::Filter filter;
    const Value* name = value.field("name");
    if (!name)
        throw Error("A filter needs a name: blurFilter, dropShadowFilter, glowFilter...");
    bool known = false;
    for (fla::Filter::Type type : {fla::Filter::Type::Blur, fla::Filter::Type::DropShadow, fla::Filter::Type::Glow,
             fla::Filter::Type::Bevel, fla::Filter::Type::GradientGlow, fla::Filter::Type::GradientBevel,
             fla::Filter::Type::AdjustColor, fla::Filter::Type::ColorMatrix, fla::Filter::Type::Convolution})
    {
        if (name->toString() == filterName(type))
        {
            if (base && base->type == type)
                filter = *base;
            filter.type = type;
            known = true;
        }
    }
    if (!known)
        throw Error("There is no filter called " + name->toString() + ".");
    const auto number = [&value](const char* key, double& out) {
        if (const Value* found = value.field(key))
            out = found->toNumber();
    };
    const auto flag = [&value](const char* key, bool& out) {
        if (const Value* found = value.field(key))
            out = found->toBool();
    };
    const auto color = [&value](const char* key, uint8_t out[4]) {
        if (const Value* found = value.field(key))
        {
            if (!parseColor(*found, out))
                throw Error(std::string("The filter's ") + key + " is not a color.");
        }
    };
    flag("enabled", filter.enabled);
    number("blurX", filter.blurX);
    number("blurY", filter.blurY);
    if (const Value* quality = value.field("quality"))
    {
        const std::string q = quality->toString();
        filter.quality = q == "high" ? 3 : q == "medium" ? 2 : q == "low" ? 1 : std::clamp(quality->toInt(), 1, 3);
    }
    if (const Value* strength = value.field("strength"))
        filter.strength = strength->toNumber() / 100.0;
    number("angle", filter.angle);
    number("distance", filter.distance);
    number("brightness", filter.brightness);
    number("contrast", filter.contrast);
    number("saturation", filter.saturation);
    number("hue", filter.hue);
    flag("inner", filter.inner);
    flag("knockout", filter.knockout);
    flag("hideObject", filter.hideObject);
    color("color", filter.color);
    color("highlightColor", filter.highlightColor);
    color("shadowColor", filter.shadowColor);
    if (const Value* type = value.field("type"))
        filter.bevelType = type->toString() == "inner" ? std::string() : type->toString();
    if (const Value* matrix = value.field("matrix"))
    {
        filter.matrix.clear();
        for (const Value& each : matrix->items)
            filter.matrix.push_back(each.toNumber());
    }
    if (filter.type == fla::Filter::Type::ColorMatrix)
    {
        if (filter.matrix.empty())
            filter.matrix = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
        filter.matrix.resize(20, 0.0);
    }
    else if (filter.type == fla::Filter::Type::Convolution)
    {
        if (const Value* across = value.field("matrixX"))
            filter.matrixX = std::clamp(across->toInt(), 0, 15);
        if (const Value* down = value.field("matrixY"))
            filter.matrixY = std::clamp(down->toInt(), 0, 15);
        filter.matrix.resize(static_cast<size_t>(filter.matrixX * filter.matrixY), 0.0);
        number("divisor", filter.divisor);
        number("bias", filter.bias);
        flag("preserveAlpha", filter.preserveAlpha);
        flag("clamp", filter.clamp);
    }
    const Value* colors = value.field("colorArray");
    const Value* positions = value.field("posArray");
    if (colors && positions)
    {
        filter.entries.clear();
        for (size_t i = 0; i < colors->items.size() && i < positions->items.size(); ++i)
        {
            fla::GradientEntry entry{};
            entry.ratio = std::clamp(positions->items[i].toNumber() / 255.0, 0.0, 1.0);
            if (!parseColor(colors->items[i], entry.color))
                throw Error("A gradient filter color is not a color.");
            filter.entries.push_back(entry);
        }
    }
    return filter;
}

// Text, a character at a time: JavaScript counts characters, the runs
// hold UTF-8.

std::u32string decode(const std::string& text)
{
    std::u32string out;
    for (size_t i = 0; i < text.size();)
    {
        const auto c = static_cast<unsigned char>(text[i]);
        const int length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        char32_t code = length == 1 ? c : length == 2 ? (c & 0x1F) : length == 3 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k < length && i + static_cast<size_t>(k) < text.size(); ++k)
            code = (code << 6) | (static_cast<unsigned char>(text[i + static_cast<size_t>(k)]) & 0x3F);
        out += code;
        i += static_cast<size_t>(length);
    }
    return out;
}

std::string encode(const std::u32string& text)
{
    std::string out;
    for (char32_t c : text)
    {
        if (c < 0x80)
            out += static_cast<char>(c);
        else if (c < 0x800)
        {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
        else if (c < 0x10000)
        {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

std::u32string textOf(const fla::StaticText& text)
{
    std::u32string all;
    for (const fla::TextRun& run : text.runs)
        all += decode(run.text);
    return all;
}

/// The characters from \a start to \a end (not included) of the text.
void rangeOf(const Args& args, size_t at, size_t length, size_t& start, size_t& end)
{
    start = given(args, at) ? static_cast<size_t>(std::clamp(arg(args, at).toInt(), 0, static_cast<int>(length))) : 0;
    end = given(args, at + 1) ? static_cast<size_t>(std::clamp(arg(args, at + 1).toInt(), 0, static_cast<int>(length))) : length;
    if (end < start)
        std::swap(start, end);
}

/// Replaces characters \a start to \a end with \a replacement, which takes
/// the look of the run it goes into.
void replaceText(Element& e, fla::StaticText& text, size_t start, size_t end, const std::u32string& replacement)
{
    std::vector<std::pair<char32_t, size_t>> characters;
    for (size_t r = 0; r < text.runs.size(); ++r)
    {
        for (char32_t c : decode(text.runs[r].text))
            characters.emplace_back(c, r);
    }
    size_t look = 0;
    if (!characters.empty())
        look = characters[std::min(start, characters.size() - 1)].second;
    characters.erase(characters.begin() + static_cast<std::ptrdiff_t>(std::min(start, characters.size())),
        characters.begin() + static_cast<std::ptrdiff_t>(std::min(end, characters.size())));
    std::vector<std::pair<char32_t, size_t>> inserted;
    for (char32_t c : replacement)
        inserted.emplace_back(c == U'\n' ? U'\r' : c, look);
    characters.insert(characters.begin() + static_cast<std::ptrdiff_t>(std::min(start, characters.size())), inserted.begin(),
        inserted.end());

    fla::StaticText after(text.parent);
    fla::setTextContents(after, text);
    after.runs.clear();
    for (size_t i = 0; i < characters.size();)
    {
        const size_t r = characters[i].second;
        std::u32string chunk;
        while (i < characters.size() && characters[i].second == r)
            chunk += characters[i++].first;
        fla::TextRun run = r < text.runs.size() ? text.runs[r] : fla::TextRun();
        run.text = encode(chunk);
        after.runs.push_back(run);
    }
    if (after.runs.empty())
    {
        fla::TextRun run = text.runs.empty() ? fla::TextRun() : text.runs.front();
        run.text.clear();
        after.runs.push_back(run);
    }
    e.push(fla::CommandPtr(new fla::SetTextContentsCommand(&text, text, after, "Text")), false);
}

/// The family of a face kept as "Family-Bold" or "Family-Italic".
std::string familyOf(const std::string& face, bool& bold, bool& italic)
{
    const size_t dash = face.rfind('-');
    std::string style = dash == std::string::npos ? std::string() : face.substr(dash + 1);
    bold = style.find("Bold") != std::string::npos;
    italic = style.find("Italic") != std::string::npos;
    return bold || italic ? face.substr(0, dash) : face;
}

const char* alignmentName(fla::TextRun::Alignment alignment)
{
    switch (alignment)
    {
    case fla::TextRun::Alignment::Center: return "center";
    case fla::TextRun::Alignment::Right: return "right";
    case fla::TextRun::Alignment::Justify: return "justify";
    default: return "left";
    }
}

Value textAttr(const fla::TextRun& run, const std::string& name)
{
    bool bold = false;
    bool italic = false;
    const std::string family = familyOf(run.face, bold, italic);
    if (name == "face")
        return Value(family);
    if (name == "size")
        return Value(run.size);
    if (name == "fillColor")
        return Value(colorText(run.fillColor));
    if (name == "bold")
        return Value(bold);
    if (name == "italic")
        return Value(italic);
    if (name == "alignment")
        return Value(alignmentName(run.alignment));
    if (name == "letterSpacing")
        return Value(run.letterSpacing);
    if (name == "lineSpacing")
        return Value(run.lineHeight);
    if (name == "autoKern")
        return Value(run.autoKern);
    return Value();
}

void setTextAttr(fla::TextRun& run, const std::string& name, const Value& value)
{
    bool bold = false;
    bool italic = false;
    const std::string family = familyOf(run.face, bold, italic);
    const auto face = [](const std::string& f, bool b, bool i) {
        return b || i ? f + "-" + (b ? "Bold" : "") + (i ? "Italic" : "") : f;
    };
    if (name == "face")
        run.face = face(value.toString(), bold, italic);
    else if (name == "size")
        run.size = std::max(1.0, value.toNumber());
    else if (name == "fillColor")
    {
        if (!parseColor(value, run.fillColor))
            throw Error("That is not a color: " + value.toString());
    }
    else if (name == "bold")
        run.face = face(family, value.toBool(), italic);
    else if (name == "italic")
        run.face = face(family, bold, value.toBool());
    else if (name == "alignment")
    {
        const std::string a = value.toString();
        run.alignment = a == "center" ? fla::TextRun::Alignment::Center
            : a == "right"            ? fla::TextRun::Alignment::Right
            : a == "justify"          ? fla::TextRun::Alignment::Justify
                                      : fla::TextRun::Alignment::Left;
    }
    else if (name == "letterSpacing")
        run.letterSpacing = value.toNumber();
    else if (name == "lineSpacing")
        run.lineHeight = value.toNumber();
    else if (name == "autoKern")
        run.autoKern = value.toBool();
    else
        throw Error("Text has no attribute " + name + " to set.");
}

void describeElement(ClassDef<Element>& def)
{
    def.get("elementType", [](Element& e) {
        switch (e.get().elementType())
        {
        case fla::Element::Type::SymbolInstance:
        case fla::Element::Type::BitmapInstance:
            return Value("instance");
        case fla::Element::Type::StaticText:
            return Value("text");
        case fla::Element::Type::Raw:
            return Value("unknown");
        default:
            return Value("shape");
        }
    });
    // A shape's x and y are the middle of its box, as its matrix says
    // nothing of where its lines are; anything else's are its matrix's.
    const auto isShape = [](Element& e) { return e.get().elementType() == fla::Element::Type::Shape; };
    def.prop("x",
        [isShape](Element& e) {
            if (isShape(e))
            {
                const Rect box = e.session.host().bounds(e.document, &e.get());
                return Value((box.left + box.right) / 2);
            }
            return Value(e.get().transform.tx);
        },
        [isShape](Element& e, const Value& v) {
            fla::Transform after = e.get().transform;
            if (isShape(e))
            {
                const Rect box = e.session.host().bounds(e.document, &e.get());
                after = compose(fla::Transform::fromTranslate(v.toNumber() - (box.left + box.right) / 2, 0.0), after);
            }
            else
                after.tx = v.toNumber();
            setTransform(e.session, e.document, e.element, after, "Move");
        });
    def.prop("y",
        [isShape](Element& e) {
            if (isShape(e))
            {
                const Rect box = e.session.host().bounds(e.document, &e.get());
                return Value((box.top + box.bottom) / 2);
            }
            return Value(e.get().transform.ty);
        },
        [isShape](Element& e, const Value& v) {
            fla::Transform after = e.get().transform;
            if (isShape(e))
            {
                const Rect box = e.session.host().bounds(e.document, &e.get());
                after = compose(fla::Transform::fromTranslate(0.0, v.toNumber() - (box.top + box.bottom) / 2), after);
            }
            else
                after.ty = v.toNumber();
            setTransform(e.session, e.document, e.element, after, "Move");
        });
    def.prop("left", [](Element& e) { return Value(e.session.host().bounds(e.document, &e.get()).left); },
        [](Element& e, const Value& v) {
            const Rect box = e.session.host().bounds(e.document, &e.get());
            setTransform(e.session, e.document, e.element,
                compose(fla::Transform::fromTranslate(v.toNumber() - box.left, 0.0), e.element->transform), "Move");
        });
    def.prop("top", [](Element& e) { return Value(e.session.host().bounds(e.document, &e.get()).top); },
        [](Element& e, const Value& v) {
            const Rect box = e.session.host().bounds(e.document, &e.get());
            setTransform(e.session, e.document, e.element,
                compose(fla::Transform::fromTranslate(0.0, v.toNumber() - box.top), e.element->transform), "Move");
        });
    // Size changes scale the element about its top left corner.
    def.prop("width", [](Element& e) { return Value(e.session.host().bounds(e.document, &e.get()).width()); },
        [](Element& e, const Value& v) {
            const Rect box = e.session.host().bounds(e.document, &e.get());
            if (!box.valid() || box.width() <= 0.0)
                return;
            const double factor = std::max(0.0001, v.toNumber()) / box.width();
            const fla::Transform about = compose(fla::Transform::fromTranslate(box.left, box.top),
                compose(fla::Transform::fromScale(factor, 1.0), fla::Transform::fromTranslate(-box.left, -box.top)));
            setTransform(e.session, e.document, e.element, compose(about, e.element->transform), "Scale");
        });
    def.prop("height", [](Element& e) { return Value(e.session.host().bounds(e.document, &e.get()).height()); },
        [](Element& e, const Value& v) {
            const Rect box = e.session.host().bounds(e.document, &e.get());
            if (!box.valid() || box.height() <= 0.0)
                return;
            const double factor = std::max(0.0001, v.toNumber()) / box.height();
            const fla::Transform about = compose(fla::Transform::fromTranslate(box.left, box.top),
                compose(fla::Transform::fromScale(1.0, factor), fla::Transform::fromTranslate(-box.left, -box.top)));
            setTransform(e.session, e.document, e.element, compose(about, e.element->transform), "Scale");
        });
    const auto part = [](const char* name, double Parts::*field, bool angle) {
        return std::make_pair(
            [field, angle](Element& e) {
                const double value = partsOf(e.get().transform).*field;
                return Value(angle ? normalized(value) : value);
            },
            [field, name](Element& e, const Value& v) {
                fla::Element& element = e.get();
                Parts parts = partsOf(element.transform);
                parts.*field = v.toNumber();
                setTransform(e.session, e.document, e.element, withParts(element.transform, parts, element.transformationPoint), name);
            });
    };
    const auto scaleX = part("Scale", &Parts::scaleX, false);
    def.prop("scaleX", scaleX.first, scaleX.second);
    const auto scaleY = part("Scale", &Parts::scaleY, false);
    def.prop("scaleY", scaleY.first, scaleY.second);
    const auto skewX = part("Skew", &Parts::skewX, true);
    def.prop("skewX", skewX.first, skewX.second);
    const auto skewY = part("Skew", &Parts::skewY, true);
    def.prop("skewY", skewY.first, skewY.second);
    def.prop("rotation", [](Element& e) { return Value(normalized(partsOf(e.get().transform).skewY)); },
        [](Element& e, const Value& v) {
            fla::Element& element = e.get();
            Parts parts = partsOf(element.transform);
            const double turn = v.toNumber() - parts.skewY;
            parts.skewX += turn;
            parts.skewY += turn;
            setTransform(e.session, e.document, e.element, withParts(element.transform, parts, element.transformationPoint), "Rotate");
        });
    def.prop("matrix",
        [](Element& e) {
            const fla::Transform& m = e.get().transform;
            Value value = Value::map();
            value.set("a", m.m11);
            value.set("b", m.m12);
            value.set("c", m.m21);
            value.set("d", m.m22);
            value.set("tx", m.tx);
            value.set("ty", m.ty);
            return value;
        },
        [](Element& e, const Value& v) {
            fla::Transform m = e.get().transform;
            const auto read = [&v](const char* key, double& out) {
                if (const Value* found = v.field(key))
                    out = found->toNumber();
            };
            read("a", m.m11);
            read("b", m.m12);
            read("c", m.m21);
            read("d", m.m22);
            read("tx", m.tx);
            read("ty", m.ty);
            setTransform(e.session, e.document, e.element, m, "Transform");
        });
    def.prop("transformX",
        [](Element& e) {
            const fla::Element& element = e.get();
            return Value(element.transformPoint(element.transformationPoint).x);
        },
        [](Element& e, const Value& v) {
            fla::Element& element = e.get();
            fla::Point stage = element.transformPoint(element.transformationPoint);
            stage.x = v.toNumber();
            setPivot(e, stage);
        });
    def.prop("transformY",
        [](Element& e) {
            const fla::Element& element = e.get();
            return Value(element.transformPoint(element.transformationPoint).y);
        },
        [](Element& e, const Value& v) {
            fla::Element& element = e.get();
            fla::Point stage = element.transformPoint(element.transformationPoint);
            stage.y = v.toNumber();
            setPivot(e, stage);
        });
    def.method("getTransformationPoint", 0, [](Element& e, const Args&) {
        const fla::Point& point = e.get().transformationPoint;
        Value value = Value::map();
        value.set("x", point.x);
        value.set("y", point.y);
        return value;
    });
    def.method("setTransformationPoint", 1, [](Element& e, const Args& args) {
        fla::Element& element = e.get();
        fla::Point point = element.transformationPoint;
        if (const Value* x = arg(args, 0).field("x"))
            point.x = x->toNumber();
        if (const Value* y = arg(args, 0).field("y"))
            point.y = y->toNumber();
        e.push(fla::CommandPtr(new fla::SetPropertyCommand<fla::Point>(element.transformationPoint, element.transformationPoint,
                   point, "Transformation Point")),
            false);
        return Value();
    });
    def.prop("name",
        [](Element& e) {
            fla::Element& element = e.get();
            if (auto* instance = dynamic_cast<fla::SymbolInstance*>(&element))
                return Value(instance->name);
            if (auto* text = dynamic_cast<fla::StaticText*>(&element))
                return Value(text->instanceName);
            return Value("");
        },
        [](Element& e, const Value& v) {
            fla::Element& element = e.get();
            const std::string name = v.isNullish() ? std::string() : v.toString();
            if (auto* instance = dynamic_cast<fla::SymbolInstance*>(&element))
                e.push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(instance->name, instance->name, name, "Instance Name")), false);
            else if (auto* text = dynamic_cast<fla::StaticText*>(&element))
                e.push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(text->instanceName, text->instanceName, name, "Instance Name")), false);
            else if (!name.empty())
                throw Error("Only symbol instances and text fields have names.");
        });
    def.get("depth", [](Element& e) {
        auto* frame = dynamic_cast<fla::Frame*>(e.get().parent);
        if (!frame)
            return Value(0);
        const auto at = std::find(frame->elements.begin(), frame->elements.end(), e.element);
        return Value(static_cast<int>(at - frame->elements.begin()));
    });
    def.get("layer", [](Element& e) {
        const fla::DOMElement* up = e.get().parent;
        while (up && !dynamic_cast<const fla::Layer*>(up))
            up = up->parent;
        return up ? Value(wrapLayer(e.session, e.document, const_cast<fla::Layer*>(static_cast<const fla::Layer*>(up)))) : Value();
    });
    def.prop("selected", [](Element& e) { return Value(e.session.host().selection(e.document).contains(&e.get())); },
        [](Element& e, const Value& v) {
            fla::Selection& selection = e.session.host().selection(e.document);
            if (v.toBool())
                selection.add(&e.get());
            else
                selection.remove(&e.get());
        });
    def.prop("locked", [](Element& e) { return Value(e.get().isLocked); },
        [](Element& e, const Value& v) {
            fla::Element& element = e.get();
            e.push(fla::CommandPtr(new fla::SetPropertyCommand<bool>(element.isLocked, element.isLocked, v.toBool(), "Lock")), false);
        });
    describeElementData(def);
}

void describeInstance(ClassDef<Element>& def)
{
    def.get("instanceType", [](Element& e) {
        return Value(e.get().elementType() == fla::Element::Type::BitmapInstance ? "bitmap" : "symbol");
    });
    def.get("libraryItem", [](Element& e) {
        fla::Element& element = e.get();
        if (auto* instance = dynamic_cast<fla::SymbolInstance*>(&element))
            return Value(findItem(e.session, e.document, instance->libraryItemName));
        if (auto* bitmap = dynamic_cast<fla::BitmapInstance*>(&element))
            return Value(findItem(e.session, e.document, bitmap->libraryItemName));
        return Value::null();
    });
}

void describeSymbolInstance(ClassDef<Element>& def)
{
    def.prop("symbolType", [](Element& e) { return Value(symbolTypeName(needInstance(e).symbolType)); },
        [](Element& e, const Value& v) {
            fla::SymbolInstance& instance = needInstance(e);
            e.push(fla::CommandPtr(new fla::SetPropertyCommand<fla::SymbolType>(instance.symbolType, instance.symbolType,
                       symbolTypeNamed(v.toString()), "Behavior")),
                false);
        });
    def.prop("firstFrame", [](Element& e) { return Value(needInstance(e).firstFrame); },
        [](Element& e, const Value& v) {
            fla::SymbolInstance& instance = needInstance(e);
            e.push(fla::CommandPtr(new fla::SetPropertyCommand<int>(instance.firstFrame, instance.firstFrame, std::max(0, v.toInt()),
                       "First Frame")),
                false);
        });
    def.prop("lastFrame", [](Element& e) { return Value(needInstance(e).lastFrame); },
        [](Element& e, const Value& v) {
            fla::SymbolInstance& instance = needInstance(e);
            e.push(fla::CommandPtr(new fla::SetPropertyCommand<int>(instance.lastFrame, instance.lastFrame, std::max(-1, v.toInt()),
                       "Last Frame")),
                false);
        });
    def.prop("loop", [](Element& e) { return Value(loopName(needInstance(e).loopType)); },
        [](Element& e, const Value& v) {
            fla::SymbolInstance& instance = needInstance(e);
            e.push(fla::CommandPtr(new fla::SetPropertyCommand<fla::LoopType>(instance.loopType, instance.loopType,
                       loopNamed(v.toString()), "Looping")),
                false);
        });
    def.prop("blendMode",
        [](Element& e) {
            const std::string& mode = needInstance(e).blendMode;
            return Value(mode.empty() ? std::string("normal") : mode);
        },
        [](Element& e, const Value& v) {
            fla::SymbolInstance& instance = needInstance(e);
            const std::string mode = v.toString() == "normal" ? std::string() : v.toString();
            e.push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(instance.blendMode, instance.blendMode, mode, "Blend")), false);
        });
    def.prop("visible", [](Element& e) { return Value(needInstance(e).visible); },
        [](Element& e, const Value& v) {
            fla::SymbolInstance& instance = needInstance(e);
            e.push(fla::CommandPtr(new fla::SetPropertyCommand<bool>(instance.visible, instance.visible, v.toBool(), "Visible")), false);
        });
    def.prop("filters",
        [](Element& e) {
            Value list = Value::array();
            for (const fla::Filter& filter : needInstance(e).filters)
                list.items.push_back(filterValue(filter));
            return list;
        },
        [](Element& e, const Value& v) {
            fla::SymbolInstance& instance = needInstance(e);
            std::vector<fla::Filter> after;
            for (size_t i = 0; i < v.items.size(); ++i)
                after.push_back(filterFrom(v.items[i], i < instance.filters.size() ? &instance.filters[i] : nullptr));
            e.push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<fla::Filter>>(instance.filters, instance.filters, after,
                       "Filters")),
                false);
        });
    // The color effect.
    def.prop("colorMode", [](Element& e) { return Value(colorModeOf(needInstance(e).colorTransform)); },
        [](Element& e, const Value& v) {
            const std::string mode = v.toString();
            if (mode != "none" && mode != "brightness" && mode != "tint" && mode != "alpha" && mode != "advanced")
                throw Error("A color mode is none, brightness, tint, alpha or advanced, not " + mode + ".");
            setColor(e, [&mode](fla::ColorTransform& color) {
                const fla::ColorTransform was = color;
                if (mode == "advanced")
                    return;
                color = fla::ColorTransform();
                if (mode == "brightness")
                    color.brightness = was.brightness;
                else if (mode == "tint")
                {
                    color.tintMultiplier = was.tintMultiplier;
                    std::copy(was.tintColor, was.tintColor + 4, color.tintColor);
                }
                else if (mode == "alpha")
                    color.alphaMultiplier = was.alphaMultiplier;
            });
        });
    const auto multiplier = [&def](const char* name, double fla::ColorTransform::*field) {
        def.prop(name, [field](Element& e) { return Value(needInstance(e).colorTransform.*field * 100.0); },
            [field](Element& e, const Value& v) {
                setColor(e, [field, &v](fla::ColorTransform& color) { color.*field = std::clamp(v.toNumber(), -100.0, 100.0) / 100.0; });
            });
    };
    multiplier("colorAlphaPercent", &fla::ColorTransform::alphaMultiplier);
    multiplier("colorRedPercent", &fla::ColorTransform::redMultiplier);
    multiplier("colorGreenPercent", &fla::ColorTransform::greenMultiplier);
    multiplier("colorBluePercent", &fla::ColorTransform::blueMultiplier);
    const auto offset = [&def](const char* name, int fla::ColorTransform::*field) {
        def.prop(name, [field](Element& e) { return Value(needInstance(e).colorTransform.*field); },
            [field](Element& e, const Value& v) {
                setColor(e, [field, &v](fla::ColorTransform& color) { color.*field = std::clamp(v.toInt(), -255, 255); });
            });
    };
    offset("colorAlphaAmount", &fla::ColorTransform::alphaOffset);
    offset("colorRedAmount", &fla::ColorTransform::redOffset);
    offset("colorGreenAmount", &fla::ColorTransform::greenOffset);
    offset("colorBlueAmount", &fla::ColorTransform::blueOffset);
    def.prop("brightness", [](Element& e) { return Value(needInstance(e).colorTransform.brightness * 100.0); },
        [](Element& e, const Value& v) {
            setColor(e, [&v](fla::ColorTransform& color) { color.brightness = std::clamp(v.toNumber(), -100.0, 100.0) / 100.0; });
        });
    def.prop("tintPercent", [](Element& e) { return Value(needInstance(e).colorTransform.tintMultiplier * 100.0); },
        [](Element& e, const Value& v) {
            setColor(e, [&v](fla::ColorTransform& color) { color.tintMultiplier = std::clamp(v.toNumber(), -100.0, 100.0) / 100.0; });
        });
    def.prop("tintColor", [](Element& e) { return Value(colorText(needInstance(e).colorTransform.tintColor).substr(0, 7)); },
        [](Element& e, const Value& v) {
            uint8_t color[4];
            if (!parseColor(v, color))
                throw Error("That is not a color: " + v.toString());
            setColor(e, [&color](fla::ColorTransform& effect) { std::copy(color, color + 3, effect.tintColor); });
        });
}

void describeText(ClassDef<Element>& def)
{
    def.get("textType", [](Element& e) {
        switch (needText(e).textType)
        {
        case fla::StaticText::TextType::Dynamic: return Value("dynamic");
        case fla::StaticText::TextType::Input: return Value("input");
        default: return Value("static");
        }
    });
    def.get("length", [](Element& e) { return Value(static_cast<int>(textOf(needText(e)).size())); });
    def.method("getTextString", 2, [](Element& e, const Args& args) {
        const std::u32string all = textOf(needText(e));
        size_t start = 0;
        size_t end = 0;
        rangeOf(args, 0, all.size(), start, end);
        return Value(encode(all.substr(start, end - start)));
    });
    def.method("setTextString", 3, [](Element& e, const Args& args) {
        fla::StaticText& text = needText(e);
        const std::u32string all = textOf(text);
        size_t start = 0;
        size_t end = 0;
        rangeOf(args, 1, all.size(), start, end);
        replaceText(e, text, start, end, decode(arg(args, 0).toString()));
        return Value();
    });
    def.get("textRuns", [](Element& e) {
        Value list = Value::array();
        for (const fla::TextRun& run : needText(e).runs)
        {
            Value entry = Value::map();
            entry.set("characters", run.text);
            Value attrs = Value::map();
            for (const char* name : {"face", "size", "fillColor", "bold", "italic", "alignment", "letterSpacing", "lineSpacing"})
                attrs.set(name, textAttr(run, name));
            entry.set("textAttrs", attrs);
            list.items.push_back(entry);
        }
        return list;
    });
    def.method("getTextAttr", 3, [](Element& e, const Args& args) {
        const fla::StaticText& text = needText(e);
        return text.runs.empty() ? Value() : textAttr(text.runs.front(), arg(args, 0).toString());
    });
    def.method("setTextAttr", 4, [](Element& e, const Args& args) {
        // Every run the range touches takes the attribute.
        fla::StaticText& text = needText(e);
        const size_t length = textOf(text).size();
        size_t start = 0;
        size_t end = 0;
        rangeOf(args, 2, length, start, end);
        fla::StaticText after(text.parent);
        fla::setTextContents(after, text);
        size_t at = 0;
        for (fla::TextRun& run : after.runs)
        {
            const size_t runLength = decode(run.text).size();
            if ((at < end && at + runLength > start) || (start == end && at <= start && start <= at + runLength) || length == 0)
                setTextAttr(run, arg(args, 0).toString(), arg(args, 1));
            at += runLength;
        }
        e.push(fla::CommandPtr(new fla::SetTextContentsCommand(&text, text, after, "Text")), false);
        return Value();
    });
}

void describeShape(ClassDef<Element>& def)
{
    def.get("isGroup", [](Element& e) { return Value(e.get().elementType() == fla::Element::Type::Group); });
    def.get("isDrawingObject", [](Element& e) {
        auto* shape = dynamic_cast<fla::Shape*>(&e.get());
        return Value(shape && shape->isDrawingObject);
    });
    def.get("isRectangleObject", [](Element& e) { return Value(e.get().elementType() == fla::Element::Type::Rectangle); });
    def.get("isOvalObject", [](Element& e) { return Value(e.get().elementType() == fla::Element::Type::Oval); });
    def.get("members", [](Element& e) {
        Value list = Value::array();
        if (auto* group = dynamic_cast<fla::Group*>(&e.get()))
        {
            for (fla::Element* member : group->members)
            {
                if (member)
                    list.items.emplace_back(wrapElement(e.session, e.document, member));
            }
        }
        return list;
    });
    describeShapeGeometry(def);
}

struct ElementClasses
{
    ClassInfo element;
    ClassInfo instance;
    ClassInfo symbolInstance;
    ClassInfo bitmapInstance;
    ClassInfo text;
    ClassInfo shape;
};

const ElementClasses& elementClasses()
{
    // Built in place: each class points at its parent.
    static ElementClasses c;
    static const bool built = []() {
        ClassDef<Element> element(c.element, "Element");
        describeElement(element);
        ClassDef<Element> instance(c.instance, "Instance", &c.element);
        describeInstance(instance);
        ClassDef<Element> symbolInstance(c.symbolInstance, "SymbolInstance", &c.instance);
        describeSymbolInstance(symbolInstance);
        ClassDef<Element> bitmapInstance(c.bitmapInstance, "BitmapInstance", &c.instance);
        (void)bitmapInstance;
        ClassDef<Element> text(c.text, "Text", &c.element);
        describeText(text);
        ClassDef<Element> shape(c.shape, "Shape", &c.element);
        describeShape(shape);
        return true;
    }();
    (void)built;
    return c;
}

} // namespace

Value filterToValue(const fla::Filter& filter)
{
    return filterValue(filter);
}

fla::Filter filterFromValue(const Value& value)
{
    return filterFrom(value, nullptr);
}

void setPivot(Element& e, const fla::Point& stage)
{
    fla::Element& element = e.get();
    const fla::Transform& m = element.transform;
    const double det = m.m11 * m.m22 - m.m12 * m.m21;
    if (std::fabs(det) < 1e-12)
        return;
    const double x = stage.x - m.tx;
    const double y = stage.y - m.ty;
    fla::Point local;
    local.x = (m.m22 * x - m.m21 * y) / det;
    local.y = (-m.m12 * x + m.m11 * y) / det;
    e.push(fla::CommandPtr(new fla::SetPropertyCommand<fla::Point>(element.transformationPoint, element.transformationPoint, local,
               "Transformation Point")),
        false);
}

fla::Element& Element::get() const
{
    require(element, "element");
    return *element;
}

namespace {

const ClassInfo& classOf(const fla::Element* element)
{
    const ElementClasses& classes = elementClasses();
    switch (element ? element->elementType() : fla::Element::Type::Raw)
    {
    case fla::Element::Type::SymbolInstance:
        return classes.symbolInstance;
    case fla::Element::Type::BitmapInstance:
        return classes.bitmapInstance;
    case fla::Element::Type::StaticText:
        return classes.text;
    case fla::Element::Type::Shape:
    case fla::Element::Type::Group:
    case fla::Element::Type::Rectangle:
    case fla::Element::Type::Oval:
        return classes.shape;
    default:
        return classes.element;
    }
}

} // namespace

Element::Element(Session& session, fla::FLADocument* document, fla::Element* element)
    : DocumentPart(session, document), element(element), _info(&classOf(element))
{}

std::shared_ptr<Element> wrapElement(Session& session, fla::FLADocument* document, fla::Element* element)
{
    return element ? std::make_shared<Element>(session, document, element) : nullptr;
}

} // namespace jsfl
