// ActionScript 2's MovieClip methods that ask the player: depths, where
// things are, the drawing API and the rest of MovieClip.prototype
// (resources/as2/movie_clip.as), each called on a clip as this.

#include "avm1_player.h"
#include "movie_runtime.h"
#include "../data/symbol.h"
#include "avm1/avm1_interpreter.h"
#include "bytecode/vm.h"
#include "runtime/value.h"
#include "display/backend.h"
#include "display/bitmap_data.h"
#include "display/blend.h"
#include "display/display_object.h"
#include "display/graphics.h"
#include "display/stage.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace fla {
namespace play {

namespace {

/// The deepest depth scripts may use, 2^31 less 16777220 counted from 0,
/// here from -16384.
constexpr int64_t kMostDepth = 2130706428 - 16384;

/// A coordinate as the player keeps it, in twentieths of a pixel.
double twips(double value)
{
    return std::round(value * 20.0) / 20.0;
}

/// A float rounded to a whole number of twips as the player rounds them:
/// halves to even; NaN and infinities 0, too large the least int.
int32_t roundTwips(float value)
{
    if (!std::isfinite(value))
        return 0;
    if (value >= 2147483648.0f)
        return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(std::nearbyint(value));
}

/// A matrix as the player works with one: its scale and skew as floats,
/// its move in whole twips, rounded at each step.
struct TwipMatrix
{
    float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f;
    int32_t tx = 0, ty = 0;

    static TwipMatrix of(const display::Matrix& m)
    {
        return {float(m.a), float(m.b), float(m.c), float(m.d), int32_t(std::lround(m.tx * 20.0)),
            int32_t(std::lround(m.ty * 20.0))};
    }
    /// This after \a r.
    TwipMatrix operator*(const TwipMatrix& r) const
    {
        const float rx = float(r.tx), ry = float(r.ty);
        return {a * r.a + c * r.b, b * r.a + d * r.b, a * r.c + c * r.d, b * r.c + d * r.d,
            int32_t(uint32_t(roundTwips(a * rx + c * ry)) + uint32_t(tx)),
            int32_t(uint32_t(roundTwips(b * rx + d * ry)) + uint32_t(ty))};
    }
    void map(int32_t x, int32_t y, int32_t& outX, int32_t& outY) const
    {
        const float fx = float(x), fy = float(y);
        outX = int32_t(uint32_t(roundTwips(a * fx + c * fy)) + uint32_t(tx));
        outY = int32_t(uint32_t(roundTwips(b * fx + d * fy)) + uint32_t(ty));
    }
    /// The inverse; none when it has none.
    std::optional<TwipMatrix> inverted() const
    {
        const float det = a * d - b * c;
        if (!(std::abs(det) > std::numeric_limits<float>::epsilon()))
            return std::nullopt;
        const float x = float(tx), y = float(ty);
        return TwipMatrix{d / det, b / -det, c / -det, a / det, roundTwips((d * x - c * y) / -det),
            roundTwips((b * x - a * y) / det)};
    }
};

/// From \a object's space to the stage's, as the player multiplies it out.
TwipMatrix toStage(const display::DisplayObject& object)
{
    TwipMatrix matrix = TwipMatrix::of(object.matrix());
    for (const display::DisplayObject* up = object.parent(); up && up->kind() != display::ObjectKind::Stage;
         up = up->parent())
        matrix = TwipMatrix::of(up->matrix()) * matrix;
    return matrix;
}

/// Pixels as twips, as the player takes a script's number: cut toward 0.
int32_t twipsOf(double pixels)
{
    const double t = pixels * 20.0;
    if (std::isnan(t))
        return 0;
    return static_cast<int32_t>(std::clamp(t, -2147483648.0, 2147483647.0));
}

/// What a box with nothing in it reads as: 0x7ffffff twips each way, or
/// 0x8000000 where SWF 8 has it.
constexpr double kNoBounds = 134217727.0 / 20.0;
constexpr double kNoBounds8 = 134217728.0 / 20.0;

} // namespace

as3::Val ActionsPlayer::clipNatives()
{
    MovieRuntime& runtime = _runtime;
    as3::VM& vm = _vm;
    as3::avm1::Interpreter& interpreter = *_interpreter;
    const as3::Val natives = vm.newObject();
    const auto define = [&](const char* name, as3::NativeMethod method) {
        natives->properties[name] = vm.makeFunction(name, std::move(method));
    };
    const auto clipOf = [](const as3::Val& value) { return MovieRuntime::clipOfObject(value); };
    const auto given = [](as3::Args args, size_t i) { return i < args.size(); };
    const auto number = [&interpreter](as3::Args args, size_t i) {
        return i < args.size() ? interpreter.toNumber(args[i]) : std::numeric_limits<double>::quiet_NaN();
    };
    const auto whole = [number](as3::Args args, size_t i) { return as3::toInt32(number(args, i)); };
    const auto container = [](Clip& clip) -> display::DisplayObjectContainer* {
        return clip.display && clip.display->isContainer()
            ? static_cast<display::DisplayObjectContainer*>(clip.display.get())
            : nullptr;
    };
    const auto sharedOf = [](display::DisplayObjectContainer& parent, const display::DisplayObject* child) {
        for (const auto& each : parent.children())
            if (each.get() == child)
                return each;
        return std::shared_ptr<display::DisplayObject>();
    };
    // A clip a script names: the clip, or a path to one from \a from as
    // the player reads them, slashes, dots and colons alike ("../hat",
    // "_root.hat", "_level0:hat"); along the path a clip's children come
    // before its variables.
    const auto target = [&runtime, &interpreter](Clip& from, const as3::Val& value) -> Clip* {
        if (Clip* clip = MovieRuntime::clipOfObject(value))
            return clip;
        const std::string path = interpreter.toString(value);
        if (path.empty())
            return nullptr;
        const bool anyCase = interpreter.version() < 7;
        const auto same = [anyCase](const std::string& a, const std::string& b) {
            return a.size() == b.size() &&
                (anyCase ? std::equal(a.begin(), a.end(), b.begin(),
                               [](char x, char y) { return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y)); })
                         : a == b);
        };
        as3::Val object = runtime.objectFor(from);
        bool first = true;
        bool slashes = false;
        size_t at = 0;
        if (path[0] == '/')
        {
            object = runtime.objectFor(*runtime._root);
            first = false;
            slashes = true;
            at = 1;
        }
        while (at < path.size())
        {
            while (at < path.size() && path[at] == ':')
                ++at;
            if (at >= path.size())
                break;
            as3::Val next;
            const std::string prefix = path.substr(at, 3);
            if (prefix == ".." || prefix == "../" || prefix == "..:")
            {
                slashes = slashes || prefix == "../";
                at += 3;
                Clip* clip = MovieRuntime::clipOfObject(object);
                if (!clip || !clip->parent)
                    return nullptr;
                next = runtime.objectFor(*clip->parent);
            }
            else
            {
                size_t end = at;
                while (end < path.size() && path[end] != ':' && !(path[end] == '.' && !slashes) && path[end] != '/')
                    ++end;
                if (end < path.size() && path[end] == '/')
                    slashes = true;
                const std::string name = path.substr(at, end - at);
                at = end + 1;
                Clip* clip = MovieRuntime::clipOfObject(object);
                Clip* child = nullptr;
                if (clip)
                    for (const auto& each : clip->children)
                        if (each && !each->gone && !each->name.empty() && same(each->name, name))
                        {
                            child = each.get();
                            break;
                        }
                if (first && name == "_root")
                    next = runtime.objectFor(*runtime._root);
                else if (first && name == "_level0")
                    next = runtime.objectFor(*runtime._root);
                else if (child)
                    next = runtime.objectFor(*child);
                else
                    next = interpreter.getMember(object, name);
            }
            first = false;
            if (!next.isObject())
                return nullptr;
            object = next;
        }
        return MovieRuntime::clipOfObject(object);
    };
    const auto changed = [&runtime]() {
        display::markDisplayChanged();
        if (runtime.changed)
            runtime.changed();
    };

    // Depths.
    define("getDepth", [clipOf](as3::VM&, const as3::Val& self, as3::Args) -> as3::Val {
        Clip* clip = clipOf(self);
        if (!clip || !clip->display)
            return as3::Val();
        // A level's depth is its number less 16384.
        if (!clip->parent)
            return as3::Val::number(-16384);
        return as3::Val::number(clip->display->depthNumber().value_or(0));
    });
    define("swapDepths", [clipOf, container, sharedOf, target, changed](
                             as3::VM&, const as3::Val& self, as3::Args args) -> as3::Val {
        Clip* clip = clipOf(self);
        if (!clip || clip->gone || !clip->parent || !clip->display)
            return as3::Val();
        display::DisplayObjectContainer* parent = container(*clip->parent);
        if (!parent)
            return as3::Val();
        std::optional<int> depth;
        if (!args.empty() && args[0].isNumber())
            depth = as3::toInt32(args[0].num);
        else if (Clip* other = args.empty() ? nullptr : target(*clip, args[0]))
        {
            if (other->parent == clip->parent && !other->gone && other->display)
                depth = other->display->depthNumber();
        }
        const std::optional<int> now = clip->display->depthNumber();
        if (!depth || !now || *depth < -16384 || *depth > kMostDepth || *depth == *now)
            return as3::Val();
        // What is there goes where it was.
        if (display::DisplayObject* there = parent->childAtDepth(*depth))
            parent->placeAtDepth(sharedOf(*parent, there), *now);
        parent->placeAtDepth(clip->display, *depth);
        changed();
        return as3::Val();
    });
    define("getNextHighestDepth", [clipOf, container](as3::VM&, const as3::Val& self, as3::Args) -> as3::Val {
        Clip* clip = clipOf(self);
        display::DisplayObjectContainer* holder = clip ? container(*clip) : nullptr;
        if (!holder)
            return as3::Val();
        int64_t next = 0;
        for (const auto& child : holder->children())
            if (const std::optional<int> depth = child ? child->depthNumber() : std::nullopt)
                next = std::max<int64_t>(next, int64_t(*depth) + 1);
        return as3::Val::number(static_cast<double>(next));
    });
    define("getInstanceAtDepth", [&runtime, clipOf, container, whole](
                                     as3::VM&, const as3::Val& self, as3::Args args) -> as3::Val {
        Clip* clip = clipOf(self);
        display::DisplayObjectContainer* holder = clip ? container(*clip) : nullptr;
        if (!holder || args.empty())
            return as3::Val();
        display::DisplayObject* there = holder->childAtDepth(whole(args, 0));
        if (!there)
            return as3::Val();
        // What scripts have no object for (a shape) gives the clip itself.
        if (there->clip)
            return runtime.objectFor(*there->clip);
        if (there->kind() == display::ObjectKind::TextField && !there->name().empty())
            if (const as3::ValuePtr field = runtime.textFieldObject(*clip, there->name()))
                return field;
        return self;
    });

    // Clips made by scripts, in place of what is at their depth.
    const auto initialize = [&interpreter, &runtime](const as3::Val& init) {
        return [&interpreter, &runtime, init](Clip& made) {
            if (!init.isObject())
                return;
            const as3::Val object = runtime.objectFor(made);
            for (const std::string& name : interpreter.forInNames(init))
                interpreter.setMember(object, name, interpreter.getMember(init, name));
        };
    };
    define("attachMovie", [&runtime, &interpreter, clipOf, whole, initialize](
                              as3::VM&, const as3::Val& self, as3::Args args) -> as3::Val {
        Clip* clip = clipOf(self);
        if (!clip || clip->gone || args.size() < 3)
            return as3::Val();
        const int depth = whole(args, 2);
        if (depth < -16384 || depth > kMostDepth)
            return as3::Val();
        const Symbol* symbol = runtime.linked(interpreter.toString(args[0]));
        if (!symbol)
            return as3::Val();
        Clip* made = runtime.makeClip(clip, *symbol, interpreter.toString(args[1]), depth,
            initialize(args.size() > 3 ? args[3] : as3::Val()));
        return runtime.objectFor(*made);
    });
    define("createEmptyMovieClip", [&runtime, &interpreter, clipOf, whole](
                                       as3::VM&, const as3::Val& self, as3::Args args) -> as3::Val {
        Clip* clip = clipOf(self);
        if (!clip || clip->gone || args.size() < 2)
            return as3::Val();
        const int depth = whole(args, 1);
        if (depth < -16384 || depth > kMostDepth)
            return as3::Val();
        return runtime.objectFor(*runtime.makeClip(clip, emptySymbol(), interpreter.toString(args[0]), depth));
    });
    define("duplicateMovieClip", [this, &runtime, &interpreter, clipOf, whole, initialize](
                                     as3::VM&, const as3::Val& self, as3::Args args) -> as3::Val {
        Clip* clip = clipOf(self);
        if (!clip || args.empty())
            return as3::Val();
        const int depth = args.size() > 1 ? whole(args, 1) : 0;
        Clip* made = runtime.duplicateClip(*clip, interpreter.toString(args[0]), depth,
            initialize(args.size() > 2 ? args[2] : as3::Val()));
        // Before SWF 6, it gives nothing back.
        if (!made || interpreter.version() < 6)
            return as3::Val();
        return runtime.objectFor(*made);
    });
    define("removeMovieClip", [this, clipOf](as3::VM&, const as3::Val& self, as3::Args) -> as3::Val {
        if (Clip* clip = clipOf(self))
            removeIfScripted(*clip);
        return as3::Val();
    });

    // Where things are: a point's x and y, own numbers only, from the clip's
    // space to the stage's and back; and its box in another clip's space.
    const auto convert = [&interpreter, clipOf](const as3::Val& self, as3::Args args, bool outward) {
        Clip* clip = clipOf(self);
        if (!clip || !clip->display || args.empty() || !args[0].isObject())
            return;
        const as3::Val& point = args[0];
        const auto x = point->properties.find("x");
        const auto y = point->properties.find("y");
        if (x == point->properties.end() || y == point->properties.end() || !as3::Val(x->second).isNumber() ||
            !as3::Val(y->second).isNumber())
            return;
        const TwipMatrix local = toStage(*clip->display);
        const TwipMatrix matrix = outward ? local : local.inverted().value_or(TwipMatrix());
        int32_t mx = 0, my = 0;
        matrix.map(twipsOf(as3::Val(x->second).num), twipsOf(as3::Val(y->second).num), mx, my);
        interpreter.setMember(point, "x", as3::Val::number(mx / 20.0));
        interpreter.setMember(point, "y", as3::Val::number(my / 20.0));
    };
    define("localToGlobal", [convert](as3::VM&, const as3::Val& self, as3::Args args) {
        convert(self, args, true);
        return as3::Val();
    });
    define("globalToLocal", [convert](as3::VM&, const as3::Val& self, as3::Args args) {
        convert(self, args, false);
        return as3::Val();
    });
    define("getBounds", [&vm, &interpreter, clipOf, target](as3::VM&, const as3::Val& self, as3::Args args) -> as3::Val {
        Clip* clip = clipOf(self);
        if (!clip || !clip->display)
            return as3::Val();
        Clip* space = args.empty() ? clip : target(*clip, args[0]);
        if (!space || !space->display)
            return as3::Val();
        const display::Rect own = clip->display->getBounds(clip->display.get());
        double box[4];
        if (own.isEmpty() && own.width == 0.0 && own.height == 0.0 && own.x == 0.0 && own.y == 0.0)
        {
            const double none = space != clip && interpreter.version() >= 8 ? kNoBounds8 : kNoBounds;
            std::fill(box, box + 4, none);
        }
        else
        {
            // In twips, each corner through the matrix, the box around them.
            int32_t left = int32_t(std::lround(own.x * 20.0)), top = int32_t(std::lround(own.y * 20.0));
            int32_t right = int32_t(std::lround((own.x + own.width) * 20.0));
            int32_t bottom = int32_t(std::lround((own.y + own.height) * 20.0));
            if (space != clip)
            {
                const TwipMatrix matrix =
                    toStage(*space->display).inverted().value_or(TwipMatrix()) * toStage(*clip->display);
                int32_t xs[4], ys[4];
                matrix.map(left, top, xs[0], ys[0]);
                matrix.map(left, bottom, xs[1], ys[1]);
                matrix.map(right, top, xs[2], ys[2]);
                matrix.map(right, bottom, xs[3], ys[3]);
                left = *std::min_element(xs, xs + 4);
                right = *std::max_element(xs, xs + 4);
                top = *std::min_element(ys, ys + 4);
                bottom = *std::max_element(ys, ys + 4);
            }
            box[0] = left / 20.0;
            box[1] = right / 20.0;
            box[2] = top / 20.0;
            box[3] = bottom / 20.0;
        }
        const as3::Val out = vm.newObject();
        interpreter.setMember(out, "xMin", as3::Val::number(twips(box[0])));
        interpreter.setMember(out, "xMax", as3::Val::number(twips(box[1])));
        interpreter.setMember(out, "yMin", as3::Val::number(twips(box[2])));
        interpreter.setMember(out, "yMax", as3::Val::number(twips(box[3])));
        return out;
    });
    define("getSWFVersion", [&interpreter, clipOf](as3::VM&, const as3::Val& self, as3::Args) -> as3::Val {
        return clipOf(self) ? as3::Val::number(interpreter.version()) : as3::Val();
    });
    define("setMask", [&runtime, clipOf, target, changed](
                          as3::VM&, const as3::Val& self, as3::Args args) -> as3::Val {
        Clip* clip = clipOf(self);
        if (!clip || !clip->display || args.empty())
            return as3::Val();
        if (args[0].isNullish())
        {
            clip->display->setMask(nullptr);
            changed();
            return as3::Val::boolean(true);
        }
        // A path from the timeline whose script this is.
        Clip* mask = target(runtime._current ? *runtime._current : *runtime._root, args[0]);
        if (!mask || !mask->display)
            return as3::Val::boolean(false);
        clip->display->setMask(mask->display);
        changed();
        return as3::Val::boolean(true);
    });

    // The drawing API: alphas in percent, colors as numbers.
    const auto graphicsOf = [clipOf](const as3::Val& self) -> display::Graphics* {
        Clip* clip = clipOf(self);
        auto* sprite = clip ? dynamic_cast<display::Sprite*>(clip->display.get()) : nullptr;
        return sprite ? &sprite->graphics : nullptr;
    };
    const auto percent = [number, given](as3::Args args, size_t i) {
        return given(args, i) ? std::clamp(number(args, i), 0.0, 100.0) / 100.0 : 1.0;
    };
    const auto color = [&interpreter](const as3::Val& value) { return as3::toUint32(interpreter.toNumber(value)); };
    define("beginFill", [graphicsOf, percent, color, changed](as3::VM&, const as3::Val& self, as3::Args args) {
        if (display::Graphics* graphics = graphicsOf(self))
        {
            if (args.empty())
                graphics->endFill();
            else
                graphics->beginFill(color(args[0]) & 0xffffff, percent(args, 1));
            changed();
        }
        return as3::Val();
    });
    define("lineStyle", [&interpreter, graphicsOf, number, percent, color, changed](
                            as3::VM&, const as3::Val& self, as3::Args args) {
        display::Graphics* graphics = graphicsOf(self);
        if (!graphics)
            return as3::Val();
        if (args.empty())
            graphics->lineStyle();
        else
        {
            const double thickness = std::clamp(number(args, 0), 0.0, 255.0);
            const auto text = [&](size_t i) { return i < args.size() ? interpreter.toString(args[i]) : std::string(); };
            display::LineStyle::ScaleMode scaleMode = display::LineStyle::ScaleMode::Normal;
            if (text(4) == "none")
                scaleMode = display::LineStyle::ScaleMode::None;
            else if (text(4) == "vertical")
                scaleMode = display::LineStyle::ScaleMode::Vertical;
            else if (text(4) == "horizontal")
                scaleMode = display::LineStyle::ScaleMode::Horizontal;
            display::LineStyle::Caps caps = display::LineStyle::Caps::Round;
            if (text(5) == "square")
                caps = display::LineStyle::Caps::Square;
            else if (text(5) == "none")
                caps = display::LineStyle::Caps::None;
            display::LineStyle::Joints joints = display::LineStyle::Joints::Round;
            double miter = 3.0;
            if (text(6) == "miter")
            {
                joints = display::LineStyle::Joints::Miter;
                if (args.size() > 7)
                    miter = std::clamp(number(args, 7), 0.0, 255.0);
            }
            else if (text(6) == "bevel")
                joints = display::LineStyle::Joints::Bevel;
            graphics->lineStyle(std::isnan(thickness) ? 0.0 : thickness,
                args.size() > 1 ? color(args[1]) & 0xffffff : 0, args.size() > 1 ? percent(args, 2) : 1.0,
                args.size() > 3 && interpreter.toBoolean(args[3]), scaleMode, caps, joints, miter);
        }
        changed();
        return as3::Val();
    });
    define("moveTo", [graphicsOf, number, changed](as3::VM&, const as3::Val& self, as3::Args args) {
        if (display::Graphics* graphics = graphicsOf(self); graphics && args.size() >= 2)
        {
            graphics->moveTo(number(args, 0), number(args, 1));
            changed();
        }
        return as3::Val();
    });
    define("lineTo", [graphicsOf, number, changed](as3::VM&, const as3::Val& self, as3::Args args) {
        if (display::Graphics* graphics = graphicsOf(self); graphics && args.size() >= 2)
        {
            graphics->lineTo(number(args, 0), number(args, 1));
            changed();
        }
        return as3::Val();
    });
    define("curveTo", [graphicsOf, number, changed](as3::VM&, const as3::Val& self, as3::Args args) {
        if (display::Graphics* graphics = graphicsOf(self); graphics && args.size() >= 4)
        {
            graphics->curveTo(number(args, 0), number(args, 1), number(args, 2), number(args, 3));
            changed();
        }
        return as3::Val();
    });
    define("endFill", [graphicsOf, changed](as3::VM&, const as3::Val& self, as3::Args) {
        if (display::Graphics* graphics = graphicsOf(self))
        {
            graphics->endFill();
            changed();
        }
        return as3::Val();
    });
    define("clear", [graphicsOf, changed](as3::VM&, const as3::Val& self, as3::Args) {
        if (display::Graphics* graphics = graphicsOf(self))
        {
            graphics->clear();
            changed();
        }
        return as3::Val();
    });
    // beginGradientFill and lineGradientStyle: the three arrays as long as
    // each other, the ratios from 0 to 255; the matrix a box
    // ({matrixType: "box", x, y, w, h, r}), a flash.geom.Matrix, or the
    // 3 by 3 one of Flash MX, which maps the gradient's square to a box of
    // its a by e at (g, h).
    const auto gradientOf = [&interpreter, number, color](as3::Args args, display::Gradient& out) {
        if (args.size() < 5)
            return false;
        const std::string type = interpreter.toString(args[0]);
        if (type != "linear" && type != "radial")
            return false;
        out.radial = type == "radial";
        const auto length = [&](const as3::Val& list) {
            return list.isObject() ? as3::toInt32(interpreter.toNumber(interpreter.getMember(list, "length"))) : 0;
        };
        const int count = length(args[1]);
        if (count != length(args[2]) || count != length(args[3]))
            return false;
        std::vector<uint32_t> colors;
        std::vector<double> alphas;
        std::vector<double> ratios;
        for (int i = 0; i < count; ++i)
        {
            const std::string index = std::to_string(i);
            colors.push_back(color(interpreter.getMember(args[1], index)) & 0xffffff);
            alphas.push_back(std::clamp(interpreter.toNumber(interpreter.getMember(args[2], index)), 0.0, 100.0) / 100.0);
            const double ratio = interpreter.toNumber(interpreter.getMember(args[3], index));
            if (!(ratio > -1.0 && ratio < 256.0))
                return false;
            ratios.push_back(std::trunc(ratio));
        }
        out.stops = display::Gradient::stopsOf(colors, alphas, ratios);
        const as3::Val& m = args[4];
        const auto field = [&](const char* name) { return interpreter.toNumber(interpreter.getMember(m, name)); };
        if (m.isObject() && interpreter.toString(interpreter.getMember(m, "matrixType")) == "box")
            out.matrix.createGradientBox(field("w"), field("h"), field("r"), field("x"), field("y"));
        else if (m.isObject() && interpreter.hasMember(m, "tx"))
            out.matrix = display::Matrix(field("a"), field("b"), field("c"), field("d"), field("tx"), field("ty"));
        else if (m.isObject())
            out.matrix = display::Matrix(field("a") / 1638.4, field("b") / 1638.4, field("d") / 1638.4,
                field("e") / 1638.4, field("g"), field("h"));
        const std::string spread = args.size() > 5 ? interpreter.toString(args[5]) : std::string();
        out.spread = spread == "reflect" ? display::Gradient::Spread::Reflect
            : spread == "repeat"         ? display::Gradient::Spread::Repeat
                                         : display::Gradient::Spread::Pad;
        out.linearRGB = args.size() > 6 && interpreter.toString(args[6]) == "linearRGB";
        out.focalPointRatio = args.size() > 7 ? std::clamp(number(args, 7), -1.0, 1.0) : 0.0;
        return true;
    };
    define("beginGradientFill", [&interpreter, graphicsOf, gradientOf, changed](
                                    as3::VM&, const as3::Val& self, as3::Args args) {
        display::Graphics* graphics = graphicsOf(self);
        if (!graphics)
            return as3::Val();
        if (args.empty() || args[0].isUndefined())
        {
            graphics->endFill();
            changed();
            return as3::Val();
        }
        // Too much given: nothing done (before SWF 8, more than five).
        if (args.size() > 8 || (args.size() > 5 && interpreter.version() < 8))
            return as3::Val();
        display::Gradient gradient;
        if (gradientOf(args, gradient))
        {
            graphics->beginGradientFill(gradient);
            changed();
        }
        return as3::Val();
    });
    define("lineGradientStyle", [graphicsOf, gradientOf, changed](as3::VM&, const as3::Val& self, as3::Args args) {
        display::Graphics* graphics = graphicsOf(self);
        display::Gradient gradient;
        if (graphics && args.size() <= 8 && gradientOf(args, gradient))
        {
            graphics->lineGradientStyle(gradient);
            changed();
        }
        return as3::Val();
    });
    // beginBitmapFill(bitmap's holder, matrix, repeat, smooth).
    define("beginBitmapFill", [&interpreter, graphicsOf, changed](as3::VM&, const as3::Val& self, as3::Args args) {
        display::Graphics* graphics = graphicsOf(self);
        if (!graphics)
            return as3::Val();
        std::shared_ptr<display::BitmapData> bitmap = args.empty() ? nullptr : bitmapOfHolder(args[0]);
        if (!bitmap)
        {
            graphics->endFill();
            changed();
            return as3::Val();
        }
        display::Matrix matrix;
        if (args.size() > 1 && args[1].isObject())
        {
            const auto field = [&](const char* name) {
                return interpreter.toNumber(interpreter.getMember(args[1], name));
            };
            matrix = display::Matrix(field("a"), field("b"), field("c"), field("d"), field("tx"), field("ty"));
        }
        graphics->beginBitmapFill(bitmap, matrix, args.size() < 3 || interpreter.toBoolean(args[2]),
            args.size() > 3 && interpreter.toBoolean(args[3]));
        changed();
        return as3::Val();
    });
    // attachBitmap(bitmap's holder, depth, pixelSnapping, smoothing).
    define("attachBitmap", [&runtime, &interpreter, clipOf, container, whole, changed](
                               as3::VM&, const as3::Val& self, as3::Args args) {
        Clip* clip = clipOf(self);
        display::DisplayObjectContainer* holder = clip ? container(*clip) : nullptr;
        std::shared_ptr<display::BitmapData> bitmap = args.empty() ? nullptr : bitmapOfHolder(args[0]);
        if (!holder || !bitmap || args.size() < 2)
            return as3::Val();
        const int depth = whole(args, 1);
        runtime.clearDepth(*clip, depth);
        auto shown = std::make_shared<display::Bitmap>(bitmap, display::PixelSnapping::Auto,
            args.size() > 3 && interpreter.toBoolean(args[3]));
        holder->placeAtDepth(shown, depth);
        changed();
        return as3::Val();
    });

    // Its looks: the blend mode by name (or number, 1 to 14),
    // cacheAsBitmap, opaqueBackground and scrollRect.
    define("blendMode", [clipOf](as3::VM&, const as3::Val& self, as3::Args) -> as3::Val {
        Clip* clip = clipOf(self);
        return clip && clip->display ? as3::Val::string(display::blendModeName(clip->display->blendMode())) : as3::Val();
    });
    define("setBlendMode", [&interpreter, clipOf, changed](as3::VM&, const as3::Val& self, as3::Args args) {
        Clip* clip = clipOf(self);
        if (!clip || !clip->display || args.empty())
            return as3::Val();
        static const char* const byNumber[] = {"normal", "normal", "layer", "multiply", "screen", "lighten", "darken",
            "difference", "add", "subtract", "invert", "alpha", "erase", "overlay", "hardlight"};
        std::string name;
        if (args[0].isNumber())
        {
            const double n = args[0].num;
            if (n >= 1 && n <= 14 && n == std::trunc(n))
                name = byNumber[static_cast<int>(n)];
        }
        else if (args[0].isString())
            name = interpreter.toString(args[0]);
        display::BlendMode mode = display::BlendMode::Normal;
        if (!name.empty() && display::blendModeNamed(name, mode))
        {
            clip->display->setBlendMode(mode);
            changed();
        }
        return as3::Val();
    });
    define("cacheAsBitmap", [clipOf](as3::VM&, const as3::Val& self, as3::Args) -> as3::Val {
        Clip* clip = clipOf(self);
        if (!clip || !clip->display)
            return as3::Val();
        // What it is, not what was asked: filters cache it too.
        return as3::Val::boolean(clip->display->cacheAsBitmap() || !clip->display->filters().empty());
    });
    define("setCacheAsBitmap", [&interpreter, clipOf](as3::VM&, const as3::Val& self, as3::Args args) {
        if (Clip* clip = clipOf(self); clip && clip->display && !args.empty())
            clip->display->setCacheAsBitmap(interpreter.toBoolean(args[0]));
        return as3::Val();
    });
    define("opaqueBackground", [clipOf](as3::VM&, const as3::Val& self, as3::Args) -> as3::Val {
        Clip* clip = clipOf(self);
        if (!clip || !clip->display || !clip->display->opaqueBackground())
            return as3::Val();
        return as3::Val::number(*clip->display->opaqueBackground() & 0xffffff);
    });
    define("setOpaqueBackground", [&interpreter, clipOf, changed](as3::VM&, const as3::Val& self, as3::Args args) {
        Clip* clip = clipOf(self);
        if (!clip || !clip->display)
            return as3::Val();
        if (args.empty() || args[0].isNullish())
            clip->display->setOpaqueBackground(std::nullopt);
        else
            clip->display->setOpaqueBackground(as3::toUint32(interpreter.toNumber(args[0])) & 0xffffff);
        changed();
        return as3::Val();
    });
    // scrollRect as [x, y, width, height], or undefined.
    define("scrollRect", [&vm, clipOf](as3::VM&, const as3::Val& self, as3::Args) -> as3::Val {
        Clip* clip = clipOf(self);
        if (!clip || !clip->display || !clip->display->scrollRect())
            return as3::Val();
        const display::Rect r = *clip->display->scrollRect();
        return vm.newArray({as3::Val::number(r.x), as3::Val::number(r.y), as3::Val::number(r.width),
            as3::Val::number(r.height)});
    });
    define("setScrollRect", [&interpreter, clipOf, changed](as3::VM&, const as3::Val& self, as3::Args args) {
        Clip* clip = clipOf(self);
        if (!clip || !clip->display)
            return as3::Val();
        if (args.empty() || !args[0].isObject())
            clip->display->setScrollRect(std::nullopt);
        else
        {
            // Its own x, y, width and height, whole; without all four, as
            // it was.
            const as3::Val& rect = args[0];
            double values[4];
            const char* names[] = {"x", "y", "width", "height"};
            for (int i = 0; i < 4; ++i)
            {
                const auto found = rect->properties.find(names[i]);
                if (found == rect->properties.end())
                    return as3::Val();
                values[i] = as3::toInt32(interpreter.toNumber(as3::Val(found->second)));
            }
            clip->display->setScrollRect(display::Rect(values[0], values[1], values[2], values[3]));
        }
        changed();
        return as3::Val();
    });
    return natives;
}

void ActionsPlayer::removeIfScripted(Clip& clip)
{
    // Only what is at a depth scripts use, 0 and up, comes off.
    const std::optional<int> depth = clip.display ? clip.display->depthNumber() : std::nullopt;
    if (clip.parent && depth && *depth >= 0 && *depth < 2130706416 - 16384)
        _runtime.removeClip(clip);
}

} // namespace play
} // namespace fla
