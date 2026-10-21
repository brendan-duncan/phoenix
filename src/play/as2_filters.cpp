// ActionScript 2's flash.filters: the player's side of the library's
// classes (resources/as2/flash_filters.as). A filter object keeps its kind
// ($filter) and its values ($v) as the player stores them; these turn them
// into the filters a display object draws with, and a display object's
// filters back into values, cut to the fixed-point numbers an SWF movie
// keeps them in.

#include "avm1_player.h"
#include "movie_runtime.h"
#include "avm1/avm1_interpreter.h"
#include "bytecode/vm.h"
#include "runtime/value.h"
#include "display/backend.h"
#include "display/bitmap_filter.h"
#include "display/display_object.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace fla {
namespace play {

namespace {

constexpr double kPi = 3.14159265358979323846;

/// A number from the timeline (an SWF's 16.16 fixed-point one, written
/// out in decimals) back at the nearest step.
double fixed16(double value)
{
    if (std::isnan(value))
        return 0.0;
    return std::clamp(std::round(value * 65536.0), -2147483648.0, 2147483647.0) / 65536.0;
}

/// A strength from the timeline (an SWF's 8.8 fixed-point one) in 256ths,
/// none below 0.
double strength256(double value)
{
    if (std::isnan(value))
        return 0.0;
    return std::max(0.0, std::clamp(std::round(value * 256.0), -32768.0, 32767.0));
}

double f32(double value)
{
    return static_cast<double>(static_cast<float>(value));
}

double byteOf(double alpha)
{
    return std::isnan(alpha) ? 0.0 : std::clamp(std::round(alpha * 255.0), 0.0, 255.0);
}

const char* kindOf(display::BitmapFilter::Type type)
{
    using Type = display::BitmapFilter::Type;
    switch (type)
    {
    case Type::Blur:
        return "blur";
    case Type::DropShadow:
        return "dropShadow";
    case Type::Glow:
        return "glow";
    case Type::Bevel:
        return "bevel";
    case Type::GradientGlow:
        return "gradientGlow";
    case Type::GradientBevel:
        return "gradientBevel";
    case Type::ColorMatrix:
        return "colorMatrix";
    case Type::Convolution:
        return "convolution";
    }
    return "blur";
}

/// A copy of a filter's values: arrays copied, anything else as it is.
as3::Val copyOf(as3::VM& vm, const as3::Val& value)
{
    if (!value.isObject() || value->type != as3::ValueType::Array)
        return value;
    std::vector<as3::Val> elements;
    for (const as3::Val& element : value->elements)
        elements.push_back(copyOf(vm, element));
    return vm.newArray(std::move(elements));
}

} // namespace

struct ActionsPlayer::ScriptFilters
{
    struct Entry
    {
        std::weak_ptr<display::DisplayObject> object;
        /// Each [kind, values...].
        std::vector<as3::Val> filters;
    };
    std::map<const display::DisplayObject*, Entry> entries;
};

bool ActionsPlayer::filterOf(const as3::Val& value, display::BitmapFilter& out)
{
    if (!value.isObject())
        return false;
    const as3::Val* kindValue = value->properties.lookup("$filter");
    const as3::Val* valuesValue = value->properties.lookup("$v");
    if (!kindValue || !valuesValue || !kindValue->isString() || !valuesValue->isObject())
        return false;
    const std::string kind = kindValue->str();
    const auto& v = (*valuesValue)->elements;
    as3::avm1::Interpreter& in = *_interpreter;
    const auto at = [&](size_t i) { return i < v.size() ? in.toNumber(v[i]) : 0.0; };
    const auto flag = [&](size_t i) { return i < v.size() && in.toBoolean(v[i]); };
    const auto text = [&](size_t i) { return i < v.size() ? in.toString(v[i]) : std::string(); };
    const auto list = [&](size_t i) {
        std::vector<double> numbers;
        if (i < v.size() && v[i].isObject())
            for (const as3::Val& element : v[i]->elements)
                numbers.push_back(in.toNumber(element));
        return numbers;
    };
    const auto color = [&](size_t i) { return as3::toUint32(at(i)) & 0xffffff; };
    const auto degrees = [&](size_t i) { return at(i) * 180.0 / kPi; };
    const auto quality = [&](size_t i) { return static_cast<int>(at(i)); };
    using Filter = display::BitmapFilter;
    if (kind == "blur")
        out = Filter::blur(at(0), at(1), quality(2));
    else if (kind == "dropShadow")
        out = Filter::dropShadow(at(0), degrees(1), color(2), at(3) / 255.0, at(7), at(8), at(9) / 256.0, quality(4),
            flag(5), flag(6), flag(10));
    else if (kind == "glow")
        out = Filter::glow(color(0), at(1) / 255.0, at(5), at(6), at(7) / 256.0, quality(2), flag(3), flag(4));
    else if (kind == "bevel")
        out = Filter::bevel(at(0), degrees(1), color(2), at(3) / 255.0, color(4), at(5) / 255.0, at(9), at(10),
            at(7) / 256.0, quality(6), text(11), flag(8));
    else if (kind == "gradientBevel" || kind == "gradientGlow")
    {
        out = Filter::bevel(at(0), degrees(1), 0xffffff, 1.0, 0, 1.0, at(5), at(6), at(7) / 256.0, quality(8), text(9),
            flag(10));
        out.type = kind == "gradientBevel" ? Filter::Type::GradientBevel : Filter::Type::GradientGlow;
        const std::vector<double> colors = list(2), alphas = list(3), ratios = list(4);
        const size_t count = std::min<size_t>(static_cast<size_t>(std::max(0.0, at(11))), colors.size());
        for (size_t i = 0; i < count && i < alphas.size() && i < ratios.size(); ++i)
        {
            display::Gradient::Stop stop;
            stop.color = as3::toUint32(colors[i]) & 0xffffff;
            stop.alpha = alphas[i] / 255.0;
            stop.ratio = static_cast<int>(ratios[i]);
            out.stops.push_back(stop);
        }
    }
    else if (kind == "colorMatrix")
        out = Filter::colorMatrix(list(0));
    else if (kind == "convolution")
        out = Filter::convolution(quality(0), quality(1), list(2), at(3), at(4), flag(5), flag(6), color(7),
            at(8) / 255.0);
    else
        // A displacement map: none drawn yet.
        return false;
    return true;
}

as3::Val ActionsPlayer::filterNatives()
{
    as3::VM& vm = _vm;
    const as3::Val natives = vm.newObject();
    const auto define = [&](const char* name, as3::NativeMethod method) {
        natives->properties[name] = vm.makeFunction(name, std::move(method));
    };
    define("f32", [this](as3::VM&, const as3::Val&, as3::Args args) {
        return as3::Val::number(f32(_interpreter->toNumber(args[0])));
    });
    // A clip's filters, each [kind, values...] as the library keeps them.
    if (!_scriptFilters)
        _scriptFilters = std::make_shared<ScriptFilters>();
    std::shared_ptr<ScriptFilters> kept = _scriptFilters;
    define("filters", [&vm, kept](as3::VM&, const as3::Val&, as3::Args args) -> as3::Val {
        Clip* clip = args.empty() ? nullptr : MovieRuntime::clipOfObject(args[0]);
        if (!clip || !clip->display)
            return vm.newArray({});
        // What a script gave it, as it gave them (a displacement map too,
        // though none is drawn).
        const auto found = kept->entries.find(clip->display.get());
        if (found != kept->entries.end() && !found->second.object.expired() && clip->display->filtersScripted())
        {
            std::vector<as3::Val> out;
            for (const as3::Val& filter : found->second.filters)
                out.push_back(copyOf(vm, filter));
            return vm.newArray(std::move(out));
        }
        using Type = display::BitmapFilter::Type;
        const auto n = [](double value) { return as3::Val::number(value); };
        const auto b = [](bool value) { return as3::Val::boolean(value); };
        const auto radians = [](double degrees) { return fixed16(degrees * kPi / 180.0); };
        std::vector<as3::Val> out;
        for (const display::BitmapFilter& f : clip->display->filters())
        {
            std::vector<as3::Val> values = {as3::Val::string(kindOf(f.type))};
            const double quality = f.quality & 31;
            switch (f.type)
            {
            case Type::Blur:
                values.insert(values.end(), {n(fixed16(f.blurX)), n(fixed16(f.blurY)), n(quality)});
                break;
            case Type::DropShadow:
                values.insert(values.end(),
                    {n(fixed16(f.distance)), n(radians(f.angle)), n(f.color & 0xffffff), n(byteOf(f.alpha)), n(quality),
                        b(f.inner), b(f.knockout), n(fixed16(f.blurX)), n(fixed16(f.blurY)), n(strength256(f.strength)),
                        b(f.hideObject)});
                break;
            case Type::Glow:
                values.insert(values.end(),
                    {n(f.color & 0xffffff), n(byteOf(f.alpha)), n(quality), b(f.inner), b(f.knockout),
                        n(fixed16(f.blurX)), n(fixed16(f.blurY)), n(strength256(f.strength))});
                break;
            case Type::Bevel:
                values.insert(values.end(),
                    {n(fixed16(f.distance)), n(radians(f.angle)), n(f.highlightColor & 0xffffff),
                        n(byteOf(f.highlightAlpha)), n(f.shadowColor & 0xffffff), n(byteOf(f.shadowAlpha)), n(quality),
                        n(strength256(f.strength)), b(f.knockout), n(fixed16(f.blurX)), n(fixed16(f.blurY)),
                        as3::Val::string(f.bevelType)});
                break;
            case Type::GradientGlow:
            case Type::GradientBevel:
            {
                std::vector<as3::Val> colors(16, n(0)), alphas(16, n(0)), ratios(16, n(0));
                const size_t count = std::min<size_t>(16, f.stops.size());
                for (size_t i = 0; i < count; ++i)
                {
                    colors[i] = n(f.stops[i].color & 0xffffff);
                    alphas[i] = n(byteOf(f.stops[i].alpha));
                    ratios[i] = n(std::clamp(f.stops[i].ratio, 0, 255));
                }
                values.insert(values.end(),
                    {n(fixed16(f.distance)), n(radians(f.angle)), vm.newArray(std::move(colors)),
                        vm.newArray(std::move(alphas)), vm.newArray(std::move(ratios)), n(fixed16(f.blurX)),
                        n(fixed16(f.blurY)), n(strength256(f.strength)), n(quality), as3::Val::string(f.bevelType),
                        b(f.knockout), n(double(count))});
                break;
            }
            case Type::ColorMatrix:
            {
                std::vector<as3::Val> matrix;
                for (size_t i = 0; i < 20; ++i)
                    matrix.push_back(n(i < f.matrix.size() ? f32(f.matrix[i]) : (i % 6 == 0 ? 1.0 : 0.0)));
                values.push_back(vm.newArray(std::move(matrix)));
                break;
            }
            case Type::Convolution:
            {
                std::vector<as3::Val> matrix;
                for (double value : f.matrix)
                    matrix.push_back(n(f32(value)));
                values.insert(values.end(),
                    {n(std::clamp(f.matrixX, 0, 255)), n(std::clamp(f.matrixY, 0, 255)), vm.newArray(std::move(matrix)),
                        n(f32(f.divisor)), n(f32(f.bias)), b(f.preserveAlpha), b(f.clamp), n(f.color & 0xffffff),
                        n(byteOf(f.alpha))});
                break;
            }
            }
            out.push_back(vm.newArray(std::move(values)));
        }
        return vm.newArray(std::move(out));
    });
    // Set from a list: each element that is a filter, a copy; anything else
    // left out.
    define("setFilters", [this, &vm, kept](as3::VM&, const as3::Val&, as3::Args args) -> as3::Val {
        Clip* clip = args.empty() ? nullptr : MovieRuntime::clipOfObject(args[0]);
        if (!clip || !clip->display)
            return as3::Val();
        std::vector<display::BitmapFilter> filters;
        ScriptFilters::Entry entry;
        entry.object = clip->display;
        if (args.size() > 1 && args[1].isObject())
        {
            const double length = _interpreter->toNumber(_interpreter->getMember(args[1], "length"));
            for (int i = 0; i < static_cast<int>(std::isnan(length) ? 0 : std::min(length, 1e6)); ++i)
            {
                const as3::Val element = _interpreter->getMember(args[1], std::to_string(i));
                display::BitmapFilter filter;
                if (filterOf(element, filter))
                    filters.push_back(std::move(filter));
                const as3::Val* kind = element.isObject() ? element->properties.lookup("$filter") : nullptr;
                const as3::Val* values = element.isObject() ? element->properties.lookup("$v") : nullptr;
                if (kind && values && kind->isString() && values->isObject())
                {
                    std::vector<as3::Val> copy = {*kind};
                    for (const as3::Val& value : (*values)->elements)
                        copy.push_back(copyOf(vm, value));
                    entry.filters.push_back(vm.newArray(std::move(copy)));
                }
            }
        }
        clip->display->setFilters(std::move(filters));
        // Those of display objects gone, let go of.
        for (auto at = kept->entries.begin(); at != kept->entries.end();)
            at = at->second.object.expired() ? kept->entries.erase(at) : std::next(at);
        kept->entries[clip->display.get()] = std::move(entry);
        display::markDisplayChanged();
        return as3::Val();
    });
    return natives;
}

} // namespace play
} // namespace fla
