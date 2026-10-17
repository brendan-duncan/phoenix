#include "scripts.h"

#include "../../data/action_script.h"
#include "../../data/bitmap.h"
#include "../../data/frame.h"
#include "../../data/layer.h"
#include "../../data/symbol.h"
#include "../../data/timeline.h"
#include "../display/backend.h"
#include "../display/bitmap_data.h"
#include "../display/display_object.h"
#include "../display/events.h"
#include "../display/stage.h"
#include "../display/text_field.h"
#include "../movie_runtime.h"
#include "../script/bindings.h"

#include <quickjs.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <sstream>
#include <vector>

namespace fla::play::canvas {

namespace {

using Clock = std::chrono::steady_clock;

/// How long the scripts may run at a time before they are stopped (a loop
/// that never ends, say).
constexpr auto kTimeLimit = std::chrono::seconds(5);

/// What the stage's number is to the scripts, and its parent's.
constexpr int kStage = -1;
constexpr int kNone = -2;

std::string text(JSContext* ctx, JSValueConst value)
{
    size_t length = 0;
    const char* chars = JS_ToCStringLen(ctx, &length, value);
    if (!chars)
    {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return std::string();
    }
    std::string out(chars, length);
    JS_FreeCString(ctx, chars);
    return out;
}

double number(JSContext* ctx, JSValueConst value, double otherwise = 0.0)
{
    double out = otherwise;
    if (JS_ToFloat64(ctx, &out, value) < 0)
    {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return otherwise;
    }
    return out;
}

int integer(JSContext* ctx, JSValueConst value)
{
    const double n = number(ctx, value, -1.0);
    return std::isfinite(n) ? static_cast<int>(n) : -1;
}

JSValueConst arg(int argc, JSValueConst* argv, int i)
{
    return i < argc ? argv[i] : JS_UNDEFINED;
}

/// The display event types CreateJS's mouse and key events are, on an
/// object or the stage; empty for one the scripts make themselves (tick,
/// pressmove, their own).
const char* displayType(const std::string& type, bool stage)
{
    static const std::pair<const char*, const char*> types[] = {{"click", display::events::kClick},
        {"dblclick", display::events::kDoubleClick}, {"mousedown", display::events::kMouseDown},
        {"mouseup", display::events::kMouseUp}, {"mouseover", display::events::kMouseOver},
        {"mouseout", display::events::kMouseOut}, {"rollover", display::events::kRollOver},
        {"rollout", display::events::kRollOut}};
    for (const auto& [js, shown] : types)
        if (type == js)
            return shown;
    if (stage && type == "keydown")
        return display::events::kKeyDown;
    if (stage && type == "keyup")
        return display::events::kKeyUp;
    return nullptr;
}

std::string attributeOf(const std::vector<std::pair<std::string, std::string>>& attributes, const std::string& key)
{
    for (const auto& [name, value] : attributes)
        if (name == key)
            return value;
    return std::string();
}

/// The keyframe of \a layer showing at \a index, or null.
const Frame* keyframeAt(const Layer& layer, int index)
{
    for (const Frame* frame : layer.frames)
        if (frame && frame->index <= index && index < frame->index + std::max(1, frame->duration))
            return frame;
    return nullptr;
}

/// The labels of a timeline, by frame (from 0).
std::vector<std::pair<std::string, int>> labelsOf(const Timeline* timeline)
{
    std::vector<std::pair<std::string, int>> labels;
    for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
        for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            if (frame && !frame->name.empty() && frame->labelType != "comment" && frame->labelType != "anchor")
                labels.emplace_back(frame->name, frame->index);
    std::stable_sort(labels.begin(), labels.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    return labels;
}

/// Where an error was, from its stack: the first frame in a script of the
/// movie (not the CreateJS classes), as "file:line:column".
void locate(const std::string& stack, const std::map<std::string, const DOMElement*>& sources, std::string& file,
    int& line)
{
    std::istringstream lines(stack);
    std::string entry;
    while (std::getline(lines, entry))
    {
        const size_t at = entry.find("at ");
        if (at == std::string::npos)
            continue;
        std::string place = entry.substr(at + 3);
        const size_t open = place.find('(');
        if (open != std::string::npos && !place.empty() && place.back() == ')')
            place = place.substr(open + 1, place.size() - open - 2);
        const size_t column = place.rfind(':');
        if (column == std::string::npos || column == 0)
            continue;
        const size_t lineAt = place.rfind(':', column - 1);
        if (lineAt == std::string::npos)
            continue;
        const std::string name = place.substr(0, lineAt);
        if (!sources.count(name))
            continue;
        file = name;
        line = std::atoi(place.c_str() + lineAt + 1);
        return;
    }
}

} // namespace

/// The native functions the CreateJS classes are made over, as __phoenix's
/// methods: a clip's properties and frames, its children, listening for
/// the display list's events, the stage, sounds and the output.
struct Native
{
    static Scripts& self(JSContext* ctx) { return *static_cast<Scripts*>(JS_GetContextOpaque(ctx)); }
    static Clip* clip(JSContext* ctx, JSValueConst id) { return self(ctx).clipOf(integer(ctx, id)); }

    /// An array of numbers, to the scripts and from them.
    static JSValue numbers(JSContext* ctx, std::initializer_list<double> values)
    {
        JSValue out = JS_NewArray(ctx);
        uint32_t i = 0;
        for (const double value : values)
            JS_SetPropertyUint32(ctx, out, i++, JS_NewFloat64(ctx, value));
        return out;
    }
    static std::vector<double> numberList(JSContext* ctx, JSValueConst array)
    {
        std::vector<double> out;
        if (!JS_IsArray(array))
            return out;
        int64_t length = 0;
        if (JS_GetLength(ctx, array, &length) < 0)
        {
            JS_FreeValue(ctx, JS_GetException(ctx));
            return out;
        }
        out.reserve(static_cast<size_t>(std::max<int64_t>(0, length)));
        for (int64_t i = 0; i < length; ++i)
        {
            JSValue value = JS_GetPropertyInt64(ctx, array, i);
            out.push_back(number(ctx, value, 0.0));
            JS_FreeValue(ctx, value);
        }
        return out;
    }

    /// The children of \a container that scripts know (clips, and what the
    /// scripts made), bottom first: not the timeline's other art.
    static std::vector<display::DisplayObject*> scriptChildren(Scripts& s, display::DisplayObject& container)
    {
        std::vector<display::DisplayObject*> out;
        if (!container.isContainer())
            return out;
        for (const auto& object : static_cast<display::DisplayObjectContainer&>(container).children())
            if (object && s.idOfDisplay(object.get()) >= 0)
                out.push_back(object.get());
        return out;
    }

    /// A paint from the list Graphics gives: 0 none; 1 a color (RGB,
    /// alpha); 2 and 3 a linear or radial gradient (its stops' count, each
    /// stop's RGB, alpha and ratio from 0 to 1, then x0, y0, x1, y1 or x0,
    /// y0, r0, x1, y1, r1, as the canvas takes them); 4 an image (its
    /// number, whether it repeats, and the matrix from it to the shape).
    static bool paint(Scripts& s, const std::vector<double>& v, size_t& i, display::FillStyle& out)
    {
        const auto take = [&]() { return i < v.size() ? v[i++] : 0.0; };
        const int kind = static_cast<int>(take());
        if (kind == 1)
        {
            out.type = display::FillStyle::Type::Solid;
            out.color = static_cast<uint32_t>(take()) & 0xffffff;
            out.alpha = std::clamp(take(), 0.0, 1.0);
            return true;
        }
        if (kind == 2 || kind == 3)
        {
            display::Gradient gradient;
            gradient.radial = kind == 3;
            const int count = std::clamp(static_cast<int>(take()), 0, 64);
            for (int n = 0; n < count; ++n)
            {
                display::Gradient::Stop stop;
                stop.color = static_cast<uint32_t>(take()) & 0xffffff;
                stop.alpha = std::clamp(take(), 0.0, 1.0);
                stop.ratio = std::clamp(static_cast<int>(std::lround(take() * 255.0)), 0, 255);
                gradient.stops.push_back(stop);
            }
            constexpr double square = 1638.4;
            if (kind == 2)
            {
                const double x0 = take(), y0 = take(), x1 = take(), y1 = take();
                double dx = x1 - x0, dy = y1 - y0;
                if (dx == 0.0 && dy == 0.0)
                    dx = 0.001;
                gradient.matrix = display::Matrix(dx / square, dy / square, -dy / square, dx / square, (x0 + x1) / 2.0,
                    (y0 + y1) / 2.0);
            }
            else
            {
                const double x0 = take(), y0 = take(), r0 = take(), x1 = take(), y1 = take();
                const double r1 = std::max(0.001, take());
                // The inner circle's radius starts the stops further out.
                const double inner = std::clamp(r0 / r1, 0.0, 1.0);
                if (inner > 0.0)
                    for (display::Gradient::Stop& stop : gradient.stops)
                        stop.ratio = static_cast<int>(std::lround(inner * 255.0 + stop.ratio * (1.0 - inner)));
                const double fx = x0 - x1, fy = y0 - y1;
                const double focal = std::hypot(fx, fy);
                const double turn = focal > 0.0 ? std::atan2(fy, fx) : 0.0;
                const double scale = r1 * 2.0 / square;
                gradient.matrix = display::Matrix(scale * std::cos(turn), scale * std::sin(turn),
                    -scale * std::sin(turn), scale * std::cos(turn), x1, y1);
                gradient.focalPointRatio = std::min(0.99, focal / r1);
            }
            out.type = display::FillStyle::Type::Gradient;
            out.gradient = gradient;
            return true;
        }
        if (kind == 4)
        {
            const auto found = s._images.find(static_cast<int>(take()));
            out.repeat = take() != 0.0;
            double m[6];
            for (double& value : m)
                value = take();
            if (found == s._images.end() || !found->second)
                return false;
            out.type = display::FillStyle::Type::Bitmap;
            out.bitmap = found->second;
            out.bitmapMatrix = display::Matrix(m[0], m[1], m[2], m[3], m[4], m[5]);
            out.smooth = true;
            return true;
        }
        return false;
    }

    /// Draws into \a g what CreateJS's Graphics lists, as \a v gives it:
    /// paths, each 1 then its fill and stroke (a paint each, and for a
    /// stroke its width, caps, joints, miter limit and whether it keeps its
    /// width whatever the scale), then 2 move, 3 line, 4 quadratic and 5
    /// cubic curve to (their points), 6 close.
    static void replay(Scripts& s, display::Graphics& g, const std::vector<double>& v)
    {
        g.clear();
        size_t i = 0;
        const auto take = [&]() { return i < v.size() ? v[i++] : 0.0; };
        bool open = false;
        display::Point start;
        const auto close = [&] {
            if (!open)
                return;
            g.endFill();
            g.lineStyle();
            open = false;
        };
        while (i < v.size())
        {
            const int op = static_cast<int>(take());
            if (op == 1)
            {
                close();
                open = true;
                display::FillStyle fill, stroke;
                const bool filled = paint(s, v, i, fill);
                const bool stroked = paint(s, v, i, stroke);
                const double width = take();
                const int caps = static_cast<int>(take());
                const int joints = static_cast<int>(take());
                const double miter = take();
                const bool keepWidth = take() != 0.0;
                if (stroked && width > 0.0)
                {
                    g.lineStyle(width, stroke.color, stroke.type == display::FillStyle::Type::Solid ? stroke.alpha : 1.0,
                        false, keepWidth ? display::LineStyle::ScaleMode::None : display::LineStyle::ScaleMode::Normal,
                        caps == 1   ? display::LineStyle::Caps::Round
                            : caps == 2 ? display::LineStyle::Caps::Square
                                        : display::LineStyle::Caps::None,
                        joints == 1   ? display::LineStyle::Joints::Round
                            : joints == 2 ? display::LineStyle::Joints::Bevel
                                          : display::LineStyle::Joints::Miter,
                        miter);
                    if (stroke.type == display::FillStyle::Type::Gradient)
                        g.lineGradientStyle(stroke.gradient);
                    else if (stroke.type == display::FillStyle::Type::Bitmap)
                        g.lineBitmapStyle(stroke.bitmap, stroke.bitmapMatrix, stroke.repeat, stroke.smooth);
                }
                if (filled)
                {
                    if (fill.type == display::FillStyle::Type::Solid)
                        g.beginFill(fill.color, fill.alpha);
                    else if (fill.type == display::FillStyle::Type::Gradient)
                        g.beginGradientFill(fill.gradient);
                    else
                        g.beginBitmapFill(fill.bitmap, fill.bitmapMatrix, fill.repeat, fill.smooth);
                    // The canvas fills by the nonzero rule.
                    g.drawPath({}, {}, true);
                }
            }
            else if (op == 2)
            {
                start = {take(), take()};
                g.moveTo(start.x, start.y);
            }
            else if (op == 3)
            {
                const double x = take(), y = take();
                g.lineTo(x, y);
            }
            else if (op == 4)
            {
                const double cx = take(), cy = take(), x = take(), y = take();
                g.curveTo(cx, cy, x, y);
            }
            else if (op == 5)
            {
                const double c1x = take(), c1y = take(), c2x = take(), c2y = take(), x = take(), y = take();
                g.cubicCurveTo(c1x, c1y, c2x, c2y, x, y);
            }
            else if (op == 6)
                g.lineTo(start.x, start.y);
            else
                break;
        }
        close();
    }

    static int interrupt(JSRuntime*, void* opaque)
    {
        return Clock::now() > static_cast<Scripts*>(opaque)->_deadline ? 1 : 0;
    }

    static JSValue time(JSContext* ctx, JSValueConst, int, JSValueConst*)
    {
        return JS_NewFloat64(ctx, self(ctx)._time);
    }

    static JSValue frameRate(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        MovieRuntime& runtime = self(ctx)._runtime;
        if (argc > 0 && !JS_IsUndefined(argv[0]))
        {
            const double rate = number(ctx, argv[0], runtime._frameRate);
            if (rate > 0.0 && rate <= 1000.0 && rate != runtime._frameRate)
            {
                runtime._frameRate = rate;
                runtime._stage->setFrameRate(rate);
                if (runtime.frameRateChanged)
                    runtime.frameRateChanged(rate);
            }
        }
        return JS_NewFloat64(ctx, runtime._frameRate);
    }

    static JSValue root(JSContext* ctx, JSValueConst, int, JSValueConst*)
    {
        Scripts& s = self(ctx);
        return JS_NewInt32(ctx, s.idOf(*s._runtime._root));
    }

    static JSValue alive(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        return JS_NewBool(ctx, self(ctx).displayOf(integer(ctx, arg(argc, argv, 0))) != nullptr);
    }

    static JSValue get(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Scripts& s = self(ctx);
        const int id = integer(ctx, arg(argc, argv, 0));
        Clip* c = s.clipOf(id);
        display::DisplayObject* object = s.displayOf(id);
        const std::string name = text(ctx, arg(argc, argv, 1));
        if (!object)
            return JS_UNDEFINED;
        display::DisplayObject& d = *object;
        // What any display object has.
        if (name == "x")
            return JS_NewFloat64(ctx, d.x());
        if (name == "y")
            return JS_NewFloat64(ctx, d.y());
        if (name == "rotation")
            return JS_NewFloat64(ctx, d.rotation());
        if (name == "scaleX")
            return JS_NewFloat64(ctx, d.scaleX());
        if (name == "scaleY")
            return JS_NewFloat64(ctx, d.scaleY());
        if (name == "alpha")
            return JS_NewFloat64(ctx, d.alpha());
        if (name == "visible")
            return JS_NewBool(ctx, d.visible());
        if (name == "mouseX")
            return JS_NewFloat64(ctx, d.mouseX());
        if (name == "mouseY")
            return JS_NewFloat64(ctx, d.mouseY());
        if (name == "mouseEnabled")
            return JS_NewBool(ctx, !d.isInteractive() || static_cast<display::InteractiveObject&>(d).mouseEnabled);
        if (name == "mouseChildren")
            return JS_NewBool(ctx, !d.isContainer() || static_cast<display::DisplayObjectContainer&>(d).mouseChildren);
        if (name == "numChildren")
            return JS_NewInt32(ctx, d.isContainer() ? static_cast<int>(scriptChildren(s, d).size()) : 0);
        if (name == "cursor")
        {
            const bool sprite = d.kind() == display::ObjectKind::MovieClip || d.kind() == display::ObjectKind::Sprite;
            return sprite && static_cast<display::Sprite&>(d).buttonMode ? JS_NewString(ctx, "pointer") : JS_NULL;
        }
        if (!c)
        {
            if (name == "name")
                return d.name().empty() ? JS_NULL : JS_NewString(ctx, d.name().c_str());
            return JS_UNDEFINED;
        }
        if (name == "currentFrame")
            return JS_NewInt32(ctx, c->frame);
        if (name == "totalFrames")
            return JS_NewInt32(ctx, c->length);
        if (name == "paused")
            return JS_NewBool(ctx, !c->playing);
        if (name == "name")
        {
            // The main timeline has none, as exportRoot has none.
            const bool root = c == s._runtime._root.get();
            return c->name.empty() || root ? JS_NULL : JS_NewString(ctx, c->name.c_str());
        }
        if (name == "currentLabel")
        {
            std::string label;
            for (const auto& [named, frame] : labelsOf(c->timeline))
                if (frame <= c->frame)
                    label = named;
            return label.empty() ? JS_NULL : JS_NewString(ctx, label.c_str());
        }
        if (name == "labels")
        {
            JSValue out = JS_NewArray(ctx);
            uint32_t i = 0;
            for (const auto& [named, frame] : labelsOf(c->timeline))
            {
                JSValue label = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, label, "label", JS_NewString(ctx, named.c_str()));
                JS_SetPropertyStr(ctx, label, "position", JS_NewInt32(ctx, frame));
                JS_SetPropertyUint32(ctx, out, i++, label);
            }
            return out;
        }
        return JS_UNDEFINED;
    }

    static JSValue set(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Scripts& s = self(ctx);
        const int id = integer(ctx, arg(argc, argv, 0));
        Clip* c = s.clipOf(id);
        display::DisplayObject* object = s.displayOf(id);
        const std::string name = text(ctx, arg(argc, argv, 1));
        JSValueConst value = arg(argc, argv, 2);
        if (!object || id == kStage)
            return JS_FALSE;
        display::DisplayObject& d = *object;
        const auto finite = [&](void (display::DisplayObject::*setter)(double)) {
            const double n = number(ctx, value, NAN);
            if (std::isfinite(n))
                (d.*setter)(n);
        };
        if (name == "x")
            finite(&display::DisplayObject::setX);
        else if (name == "y")
            finite(&display::DisplayObject::setY);
        else if (name == "rotation")
            finite(&display::DisplayObject::setRotation);
        else if (name == "scaleX")
            finite(&display::DisplayObject::setScaleX);
        else if (name == "scaleY")
            finite(&display::DisplayObject::setScaleY);
        else if (name == "alpha")
            finite(&display::DisplayObject::setAlpha);
        else if (name == "visible")
            d.setVisible(JS_ToBool(ctx, value) > 0);
        else if (name == "paused" && c)
            c->playing = JS_ToBool(ctx, value) <= 0;
        else if (name == "name")
        {
            const std::string named = JS_IsNull(value) || JS_IsUndefined(value) ? std::string() : text(ctx, value);
            if (c)
                c->name = named;
            d.setName(named);
        }
        else if (name == "mouseEnabled" && d.isInteractive())
            static_cast<display::InteractiveObject&>(d).mouseEnabled = JS_ToBool(ctx, value) > 0;
        else if (name == "mouseChildren" && d.isContainer())
            static_cast<display::DisplayObjectContainer&>(d).mouseChildren = JS_ToBool(ctx, value) > 0;
        else if (name == "cursor" &&
            (d.kind() == display::ObjectKind::MovieClip || d.kind() == display::ObjectKind::Sprite))
            static_cast<display::Sprite&>(d).buttonMode = JS_IsString(value) && text(ctx, value) == "pointer";
        else
            return JS_FALSE;
        if (self(ctx)._runtime.changed)
            self(ctx)._runtime.changed();
        return JS_TRUE;
    }

    /// go(id, frame or label, play): false when there is no such frame.
    static JSValue go(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Clip* c = clip(ctx, arg(argc, argv, 0));
        JSValueConst target = arg(argc, argv, 1);
        const bool play = JS_ToBool(ctx, arg(argc, argv, 2)) > 0;
        if (!c)
            return JS_FALSE;
        int frame = -1;
        if (JS_IsNumber(target))
            frame = integer(ctx, target);
        else if (JS_IsString(target))
        {
            const std::string label = text(ctx, target);
            for (const auto& [named, at] : labelsOf(c->timeline))
                if (named == label)
                {
                    frame = at;
                    break;
                }
        }
        if (frame < 0)
            return JS_FALSE;
        frame = std::min(frame, std::max(0, c->length - 1));
        // As CreateJS moves: what the frame places is there at once, its
        // script runs once the script running is done.
        MovieRuntime& runtime = self(ctx)._runtime;
        const int was = c->frame;
        runtime.goTo(*c, frame, play);
        if (c->frame != was)
        {
            std::vector<Clip*> entered;
            runtime.syncChildren(*c, entered);
            for (Clip* made : entered)
                if (!made->gone)
                    runtime._madeToEnter.push_back(made);
        }
        if (runtime.changed)
            runtime.changed();
        return JS_TRUE;
    }

    static JSValue play(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        if (Clip* c = clip(ctx, arg(argc, argv, 0)))
            c->playing = JS_ToBool(ctx, arg(argc, argv, 1)) > 0;
        return JS_UNDEFINED;
    }

    /// child(id, name): the clip of that instance name in it; -3 for a text
    /// field; kNone for neither.
    static JSValue child(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Clip* c = clip(ctx, arg(argc, argv, 0));
        const std::string name = text(ctx, arg(argc, argv, 1));
        if (!c || name.empty())
            return JS_NewInt32(ctx, kNone);
        // As CreateJS makes them all at once: one whose frame is still to
        // come, or one a frame took off for now, is there all the same.
        if (Clip* named = self(ctx)._runtime.canvasInstance(*c, name); named && !named->gone)
            return JS_NewInt32(ctx, self(ctx).idOf(*named));
        if (self(ctx)._runtime.textField(*c, name))
            return JS_NewInt32(ctx, -3);
        return JS_NewInt32(ctx, kNone);
    }

    /// text(id, name[, value]): a text field's text, set when given.
    static JSValue fieldText(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Clip* c = clip(ctx, arg(argc, argv, 0));
        const std::string name = text(ctx, arg(argc, argv, 1));
        const StaticText* field = c ? self(ctx)._runtime.textField(*c, name) : nullptr;
        if (!field)
            return JS_UNDEFINED;
        if (argc > 2)
            self(ctx)._runtime.setFieldText(*c, *field, text(ctx, argv[2]));
        return JS_NewString(ctx, self(ctx)._runtime.fieldText(*c, *field).c_str());
    }

    /// parent(id): its parent's number, kStage for one on the stage itself
    /// (the main timeline), kNone for one off the display list.
    static JSValue parent(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Scripts& s = self(ctx);
        const int id = integer(ctx, arg(argc, argv, 0));
        display::DisplayObject* d = id == kStage ? nullptr : s.displayOf(id);
        if (!d || !d->parent())
            return JS_NewInt32(ctx, kNone);
        return JS_NewInt32(ctx, s.idOfDisplay(d->parent()));
    }

    /// The display objects in it scripts know, bottom first.
    static JSValue children(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Scripts& s = self(ctx);
        display::DisplayObject* d = s.displayOf(integer(ctx, arg(argc, argv, 0)));
        JSValue out = JS_NewArray(ctx);
        if (!d || !d->isContainer())
            return out;
        uint32_t i = 0;
        for (display::DisplayObject* object : scriptChildren(s, *d))
            JS_SetPropertyUint32(ctx, out, i++, JS_NewInt32(ctx, s.idOfDisplay(object)));
        return out;
    }

    static JSValue listen(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        self(ctx).listen(integer(ctx, arg(argc, argv, 0)), text(ctx, arg(argc, argv, 1)),
            JS_ToBool(ctx, arg(argc, argv, 2)) > 0);
        return JS_UNDEFINED;
    }

    /// make(name): a new clip of the library symbol \a name links to (or
    /// is named, as lib names them), off the stage; -1 for none.
    static JSValue make(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Scripts& s = self(ctx);
        const std::string name = text(ctx, arg(argc, argv, 0));
        const Symbol* symbol = s._runtime.linked(name);
        for (const Symbol* each : s._runtime._library)
        {
            if (symbol || !each)
                break;
            // lib's name for a symbol: its name without its folders, and
            // with only what a JavaScript name can have.
            std::string base = each->name.substr(each->name.find_last_of('/') + 1);
            base.erase(std::remove_if(base.begin(), base.end(),
                           [](char ch) {
                               return !(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '$');
                           }),
                base.end());
            if (base == name)
                symbol = each;
        }
        if (!symbol || symbol->symbolType == SymbolType::Graphic)
            return JS_NewInt32(ctx, kNone);
        Clip* made = s._runtime.makeClip(nullptr, *symbol, std::string());
        return JS_NewInt32(ctx, s.idOf(*made));
    }

    /// add(parent, child, index): the child put in the parent's display
    /// list, on top, or at index among the children scripts know.
    static JSValue add(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Scripts& s = self(ctx);
        display::DisplayObject* to = s.displayOf(integer(ctx, arg(argc, argv, 0)));
        const std::shared_ptr<display::DisplayObject> child = s.sharedOf(integer(ctx, arg(argc, argv, 1)));
        if (!to || !to->isContainer() || !child || child.get() == to || child->kind() == display::ObjectKind::Stage)
            return JS_FALSE;
        if (child->isContainer() && static_cast<display::DisplayObjectContainer&>(*child).contains(to))
            return JS_FALSE;
        auto& container = static_cast<display::DisplayObjectContainer&>(*to);
        const int index = argc > 2 && !JS_IsUndefined(argv[2]) ? integer(ctx, argv[2]) : -1;
        std::vector<display::DisplayObject*> known = scriptChildren(s, *to);
        known.erase(std::remove(known.begin(), known.end(), child.get()), known.end());
        display::DisplayObject* under = index >= 0 && index < static_cast<int>(known.size()) ? known[index] : nullptr;
        if (child->parent() == to)
        {
            // Moved within it: no removed and added.
            const int at = container.getChildIndex(child.get());
            int place = under ? container.getChildIndex(under) : container.numChildren() - 1;
            if (under && at < place)
                --place;
            if (place != at)
                container.setChildIndex(child.get(), place);
        }
        else if (under)
            container.addChildAt(child, container.getChildIndex(under));
        else
            container.addChild(child);
        if (child->clip)
            s._runtime.placeMadeClips();
        if (s._runtime.changed)
            s._runtime.changed();
        return JS_TRUE;
    }

    static JSValue remove(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Scripts& s = self(ctx);
        display::DisplayObject* from = s.displayOf(integer(ctx, arg(argc, argv, 0)));
        display::DisplayObject* child = s.displayOf(integer(ctx, arg(argc, argv, 1)));
        if (!from || !child || child->parent() != from)
            return JS_FALSE;
        const bool clip = child->clip != nullptr;
        static_cast<display::DisplayObjectContainer&>(*from).removeChild(child);
        if (clip)
            s._runtime.placeMadeClips();
        if (s._runtime.changed)
            s._runtime.changed();
        return JS_TRUE;
    }

    /// local(id, x, y, toGlobal): a point from its space to the stage's, or
    /// back.
    static JSValue local(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        display::DisplayObject* d = self(ctx).displayOf(integer(ctx, arg(argc, argv, 0)));
        display::Point point{number(ctx, arg(argc, argv, 1)), number(ctx, arg(argc, argv, 2))};
        if (d)
            point = JS_ToBool(ctx, arg(argc, argv, 3)) > 0 ? d->localToGlobal(point) : d->globalToLocal(point);
        return numbers(ctx, {point.x, point.y});
    }

    /// bounds(id): its box in its own space, [x, y, width, height]; null
    /// for one with nothing in it.
    static JSValue bounds(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        display::DisplayObject* d = self(ctx).displayOf(integer(ctx, arg(argc, argv, 0)));
        if (!d)
            return JS_NULL;
        const display::Rect box = d->getBounds(d);
        if (box.width <= 0.0 && box.height <= 0.0)
            return JS_NULL;
        return numbers(ctx, {box.x, box.y, box.width, box.height});
    }

    /// hit(id, x, y): whether its art is at the point, in its own space.
    static JSValue hit(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        display::DisplayObject* d = self(ctx).displayOf(integer(ctx, arg(argc, argv, 0)));
        return JS_NewBool(ctx, d && d->hitsLocal(number(ctx, arg(argc, argv, 1)), number(ctx, arg(argc, argv, 2))));
    }

    /// create(kind): a new display object off the stage, "text" (a text
    /// field) or anything else (a sprite, its Graphics what it draws); its
    /// number.
    static JSValue create(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Scripts& s = self(ctx);
        std::shared_ptr<display::DisplayObject> made;
        if (text(ctx, arg(argc, argv, 0)) == "text")
        {
            auto field = std::make_shared<display::TextField>();
            field->selectable = false;
            field->setMultiline(true);
            made = field;
        }
        else
            made = std::make_shared<display::Sprite>();
        const int id = s._nextId++;
        s._objects[id] = made;
        s._objectIds[made.get()] = id;
        return JS_NewInt32(ctx, id);
    }

    /// release(id): the scripts have let go of what they made.
    static JSValue release(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Scripts& s = self(ctx);
        const int id = integer(ctx, arg(argc, argv, 0));
        const auto found = s._objects.find(id);
        if (found == s._objects.end())
            return JS_UNDEFINED;
        for (auto it = s._listening.begin(); it != s._listening.end();)
        {
            if (it->first.first != id)
            {
                ++it;
                continue;
            }
            if (const auto object = it->second.object.lock())
                object->removeEventListener(it->second.id);
            it = s._listening.erase(it);
        }
        s._masks.erase(id);
        s._objectIds.erase(found->second.get());
        s._objects.erase(found);
        return JS_UNDEFINED;
    }

    /// matrix(id, a, b, c, d, tx, ty): its whole transform, as CreateJS
    /// makes it of x, y, scale, rotation, skew and registration point.
    static JSValue matrix(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Scripts& s = self(ctx);
        const int id = integer(ctx, arg(argc, argv, 0));
        display::DisplayObject* d = id == kStage ? nullptr : s.displayOf(id);
        double m[6];
        for (int i = 0; i < 6; ++i)
        {
            m[i] = number(ctx, arg(argc, argv, i + 1), i == 0 || i == 3 ? 1.0 : 0.0);
            if (!std::isfinite(m[i]))
                return JS_FALSE;
        }
        if (!d)
            return JS_FALSE;
        d->setMatrix(display::Matrix(m[0], m[1], m[2], m[3], m[4], m[5]));
        if (s._runtime.changed)
            s._runtime.changed();
        return JS_TRUE;
    }

    /// graphics(id, drawing): what its Graphics draws, made again from the
    /// list Graphics gives (see replay).
    static JSValue graphics(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Scripts& s = self(ctx);
        display::DisplayObject* d = s.displayOf(integer(ctx, arg(argc, argv, 0)));
        display::Graphics* g = nullptr;
        if (d && (d->kind() == display::ObjectKind::Sprite || d->kind() == display::ObjectKind::MovieClip))
            g = &static_cast<display::Sprite*>(d)->graphics;
        else if (d && d->kind() == display::ObjectKind::Shape)
            g = &static_cast<display::Shape*>(d)->graphics;
        if (!g)
            return JS_FALSE;
        replay(s, *g, numberList(ctx, arg(argc, argv, 1)));
        if (s._runtime.changed)
            s._runtime.changed();
        return JS_TRUE;
    }

    /// image(source): the number of the image a library bitmap's linkage
    /// or name gives, or a file's path from the movie's folder; -1 for
    /// none.
    static JSValue image(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        return JS_NewInt32(ctx, self(ctx).imageNamed(text(ctx, arg(argc, argv, 0))));
    }

    static JSValue imageSize(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        const auto& images = self(ctx)._images;
        const auto found = images.find(integer(ctx, arg(argc, argv, 0)));
        if (found == images.end() || !found->second)
            return JS_NULL;
        return numbers(ctx, {double(found->second->width()), double(found->second->height())});
    }

    static JSValue releaseImage(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        self(ctx)._images.erase(integer(ctx, arg(argc, argv, 0)));
        return JS_UNDEFINED;
    }

    /// snapshot(id, x, y, width, height, scale): an image of it as drawn
    /// now (its cache), the box (\a x, \a y, \a width, \a height) of its
    /// own space at \a scale; -1 when it cannot be drawn.
    static JSValue snapshot(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Scripts& s = self(ctx);
        display::DisplayObject* d = s.displayOf(integer(ctx, arg(argc, argv, 0)));
        const double x = number(ctx, arg(argc, argv, 1)), y = number(ctx, arg(argc, argv, 2));
        const double scale = std::max(0.01, number(ctx, arg(argc, argv, 5), 1.0));
        const int width = static_cast<int>(std::ceil(number(ctx, arg(argc, argv, 3)) * scale));
        const int height = static_cast<int>(std::ceil(number(ctx, arg(argc, argv, 4)) * scale));
        if (!d || !display::BitmapData::validSize(width, height))
            return JS_NewInt32(ctx, -1);
        auto pixels = std::make_shared<display::BitmapData>(width, height, true, 0);
        if (display::Backend* backend = display::backend())
            backend->draw(*pixels, *d, display::Matrix(scale, 0, 0, scale, -x * scale, -y * scale), nullptr,
                display::BlendMode::Normal, nullptr, true);
        const int id = s._nextImage++;
        s._images[id] = pixels;
        return JS_NewInt32(ctx, id);
    }

    /// textStyle(id, text, font, size, bold, italic, color, align,
    /// baseline, lineWidth, lineHeight, maxWidth): a text field set out as
    /// CreateJS's Text draws, its origin where the alignment and baseline
    /// put it; [measured width, measured height, line height].
    static JSValue textStyle(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        display::DisplayObject* d = self(ctx).displayOf(integer(ctx, arg(argc, argv, 0)));
        if (!d || d->kind() != display::ObjectKind::TextField)
            return JS_NULL;
        auto& field = static_cast<display::TextField&>(*d);
        const std::string words = text(ctx, arg(argc, argv, 1));
        display::TextFormat format;
        format.font = text(ctx, arg(argc, argv, 2));
        const double size = std::max(1.0, number(ctx, arg(argc, argv, 3), 10.0));
        format.size = size;
        format.bold = JS_ToBool(ctx, arg(argc, argv, 4)) > 0;
        format.italic = JS_ToBool(ctx, arg(argc, argv, 5)) > 0;
        format.color = static_cast<uint32_t>(number(ctx, arg(argc, argv, 6))) & 0xffffff;
        const int align = integer(ctx, arg(argc, argv, 7));
        const int baseline = integer(ctx, arg(argc, argv, 8));
        const double lineWidth = number(ctx, arg(argc, argv, 9));
        const double lineHeight = number(ctx, arg(argc, argv, 10));
        const double maxWidth = number(ctx, arg(argc, argv, 11));
        format.align = align == 1 ? "center" : align == 2 ? "right" : "left";
        format.leading = 0.0;
        field.setWordWrap(lineWidth > 0.0);
        field.setBox(display::Rect(0, 0, lineWidth > 0.0 ? lineWidth + 4.0 : 100000.0, 100000.0));
        const auto apply = [&] {
            field.setDefaultTextFormat(format);
            if (field.text() != words)
                field.setText(words);
            field.setTextFormat(format);
        };
        apply();
        // A line's height: what lineHeight says (baseline to baseline), the
        // leading making up the difference.
        const auto first = [&field] {
            const std::vector<display::TextLine>& lines = field.lines();
            return lines.empty() ? display::TextLine() : lines.front();
        };
        display::TextLine line = first();
        if (lineHeight > 0.0 && line.height > 0.0)
        {
            format.leading = lineHeight - (line.ascent + line.descent);
            apply();
        }
        double width = field.textWidth();
        // Squeezed into maxWidth, as the canvas squeezes it.
        if (maxWidth > 0.0 && width > maxWidth)
        {
            format.size = std::max(1.0, size * maxWidth / width);
            apply();
            width = field.textWidth();
        }
        line = first();
        const double height = field.textHeight();
        const double ascent = line.height > 0.0 ? line.ascent : size * 0.8;
        const double descent = line.height > 0.0 ? line.descent : size * 0.2;
        const double boxWidth = (lineWidth > 0.0 ? lineWidth : width) + 4.0;
        const double left = align == 1 ? -boxWidth / 2.0 : align == 2 ? -boxWidth + 2.0 : -2.0;
        // From the first line's top: top, hanging, middle, alphabetic,
        // ideographic, bottom.
        const double drop = baseline == 2 ? (ascent + descent) / 2.0
            : baseline == 3 || baseline == 4 ? ascent
            : baseline == 5 ? ascent + descent
                            : 0.0;
        field.setBox(display::Rect(left, -2.0 - drop, boxWidth, height + 4.0));
        const double shownLine = lineHeight > 0.0 ? lineHeight : (line.height > 0.0 ? line.height : size * 1.2);
        return numbers(ctx, {width, std::max(1, field.numLines()) * shownLine, shownLine});
    }

    /// effects(id, cached, filters, shadow, composite): the filters CreateJS
    /// draws a cached object with (each a type and its values: 1 blur x,
    /// y, quality; 2 a color matrix's twenty), its shadow ([color, alpha,
    /// x, y, blur], or empty) and its compositeOperation; false when that
    /// names no blend mode Phoenix has.
    static JSValue effects(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Scripts& s = self(ctx);
        const int id = integer(ctx, arg(argc, argv, 0));
        display::DisplayObject* d = id == kStage ? nullptr : s.displayOf(id);
        if (!d)
            return JS_TRUE;
        const bool cached = JS_ToBool(ctx, arg(argc, argv, 1)) > 0;
        std::vector<display::BitmapFilter> filters;
        const std::vector<double> listed = numberList(ctx, arg(argc, argv, 2));
        for (size_t i = 0; i < listed.size();)
        {
            const int type = static_cast<int>(listed[i++]);
            if (type == 1 && i + 3 <= listed.size())
            {
                // CreateJS's blur reaches its amount each way.
                filters.push_back(display::BitmapFilter::blur(listed[i] * 2.0, listed[i + 1] * 2.0,
                    std::clamp(static_cast<int>(listed[i + 2]), 1, 3)));
                i += 3;
            }
            else if (type == 2 && i + 20 <= listed.size())
            {
                filters.push_back(display::BitmapFilter::colorMatrix(
                    std::vector<double>(listed.begin() + static_cast<std::ptrdiff_t>(i),
                        listed.begin() + static_cast<std::ptrdiff_t>(i + 20))));
                i += 20;
            }
            else
                break;
        }
        if (!cached)
            filters.clear();
        const std::vector<double> shadow = numberList(ctx, arg(argc, argv, 3));
        if (shadow.size() >= 5)
        {
            const double distance = std::hypot(shadow[2], shadow[3]);
            const double angle = std::atan2(shadow[3], shadow[2]) * 180.0 / display::kPi;
            filters.push_back(display::BitmapFilter::dropShadow(distance, angle,
                static_cast<uint32_t>(shadow[0]) & 0xffffff, shadow[1], shadow[4], shadow[4], 1.0, 2));
        }
        d->setFilters(std::move(filters));
        d->setCacheAsBitmap(cached);
        display::BlendMode mode = display::BlendMode::Normal;
        const bool known = display::blendModeNamed(text(ctx, arg(argc, argv, 4)), mode);
        d->setBlendMode(mode);
        if (s._runtime.changed)
            s._runtime.changed();
        return JS_NewBool(ctx, known);
    }

    /// mask(id, shape, image, x, y): what shows of it, the shape numbered
    /// \a shape (placed in its parent's space), or the alpha of image
    /// \a image at (\a x, \a y) in its own (AlphaMaskFilter); neither, -1,
    /// for none.
    static JSValue mask(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Scripts& s = self(ctx);
        const int id = integer(ctx, arg(argc, argv, 0));
        display::DisplayObject* d = id == kStage ? nullptr : s.displayOf(id);
        if (!d)
            return JS_FALSE;
        const int shape = integer(ctx, arg(argc, argv, 1));
        const int image = integer(ctx, arg(argc, argv, 2));
        const auto found = s._images.find(image);
        if (shape < 0 && (found == s._images.end() || !found->second))
        {
            s._masks.erase(id);
            d->setMask(nullptr);
            return JS_TRUE;
        }
        Scripts::Masking& masking = s._masks[id];
        masking.shape = shape;
        masking.image = shape < 0 ? image : -1;
        masking.x = number(ctx, arg(argc, argv, 3));
        masking.y = number(ctx, arg(argc, argv, 4));
        masking.copied = 0;
        if (shape >= 0)
            masking.proxy = std::make_shared<display::Shape>();
        else
        {
            masking.proxy = std::make_shared<display::Bitmap>(found->second, display::PixelSnapping::Never, true);
            // Its alpha by degrees, as AlphaMaskFilter has it.
            masking.proxy->setCacheAsBitmap(true);
        }
        d->setMask(masking.proxy);
        s.syncMasks();
        return JS_TRUE;
    }

    /// stage(name): the stage's size and the pointer on it.
    static JSValue stage(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        const display::Stage& s = *self(ctx)._runtime._stage;
        const std::string name = text(ctx, arg(argc, argv, 0));
        if (name == "mouseX")
            return JS_NewFloat64(ctx, s.pointerX());
        if (name == "mouseY")
            return JS_NewFloat64(ctx, s.pointerY());
        if (name == "width")
            return JS_NewFloat64(ctx, s.stageWidth());
        if (name == "height")
            return JS_NewFloat64(ctx, s.stageHeight());
        if (name == "color")
            return JS_NewFloat64(ctx, s.color);
        return JS_UNDEFINED;
    }

    /// print(text): to the output, as trace.
    static JSValue print(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        MovieRuntime& runtime = self(ctx)._runtime;
        if (runtime.trace)
            runtime.trace(text(ctx, arg(argc, argv, 0)));
        return JS_UNDEFINED;
    }

    /// sound(linkage, loops, volume, offset): a library sound played; its
    /// channel or -1.
    static JSValue sound(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        MovieRuntime& runtime = self(ctx)._runtime;
        if (!runtime.playSound)
            return JS_NewInt32(ctx, -1);
        const int loops = std::max(1, integer(ctx, arg(argc, argv, 1)));
        const double volume = argc > 2 && !JS_IsUndefined(argv[2]) ? number(ctx, argv[2], 1.0) : 1.0;
        const double offset = argc > 3 && !JS_IsUndefined(argv[3]) ? number(ctx, argv[3], 0.0) / 1000.0 : 0.0;
        return JS_NewInt32(ctx, runtime.playSound(text(ctx, arg(argc, argv, 0)), offset, loops, volume));
    }

    static JSValue stopSound(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        MovieRuntime& runtime = self(ctx)._runtime;
        const int channel = argc > 0 ? integer(ctx, argv[0]) : -1;
        if (channel >= 0 && runtime.stopSound)
            runtime.stopSound(channel);
        else if (channel < 0 && runtime.stopAllSounds)
            runtime.stopAllSounds();
        return JS_UNDEFINED;
    }

    /// open(url, window): what window.open and location ask of the player.
    static JSValue open(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        MovieRuntime& runtime = self(ctx)._runtime;
        if (runtime.getURL)
            runtime.getURL(text(ctx, arg(argc, argv, 0)), argc > 1 ? text(ctx, argv[1]) : std::string("_blank"));
        return JS_UNDEFINED;
    }

    /// Calls the prelude's global function \a name with \a args (freed
    /// after); false when it threw, which is told as in \a where. Whether
    /// it returned true goes in \a returned.
    static bool call(Scripts& s, const char* name, std::initializer_list<JSValue> args, const std::string& where,
        bool* returned = nullptr)
    {
        std::vector<JSValue> values(args);
        s.begin();
        JSValue global = JS_GetGlobalObject(s._ctx);
        JSValue function = JS_GetPropertyStr(s._ctx, global, name);
        JSValue result = JS_Call(s._ctx, function, JS_UNDEFINED, static_cast<int>(values.size()), values.data());
        const bool ok = !JS_IsException(result);
        if (!ok)
            s.report(where);
        else if (returned)
            *returned = JS_ToBool(s._ctx, result) > 0;
        JS_FreeValue(s._ctx, result);
        JS_FreeValue(s._ctx, function);
        JS_FreeValue(s._ctx, global);
        for (JSValue& value : values)
            JS_FreeValue(s._ctx, value);
        s.end();
        return ok;
    }
};

Scripts::Scripts(MovieRuntime& runtime)
    : _runtime(runtime)
{
    _js = JS_NewRuntime();
    JS_SetMemoryLimit(_js, size_t(256) * 1024 * 1024);
    JS_SetMaxStackSize(_js, size_t(1) * 1024 * 1024);
    JS_SetInterruptHandler(_js, &Native::interrupt, this);
    _ctx = JS_NewContext(_js);
    JS_SetContextOpaque(_ctx, this);
    JSValue global = JS_GetGlobalObject(_ctx);
    JSValue native = JS_NewObject(_ctx);
    const struct
    {
        const char* name;
        JSCFunction* function;
        int length;
    } functions[] = {{"time", &Native::time, 0}, {"frameRate", &Native::frameRate, 1}, {"root", &Native::root, 0},
        {"alive", &Native::alive, 1}, {"get", &Native::get, 2}, {"set", &Native::set, 3}, {"go", &Native::go, 3},
        {"play", &Native::play, 2}, {"child", &Native::child, 2}, {"text", &Native::fieldText, 3},
        {"parent", &Native::parent, 1}, {"children", &Native::children, 1}, {"listen", &Native::listen, 3},
        {"make", &Native::make, 1}, {"add", &Native::add, 3}, {"remove", &Native::remove, 2},
        {"local", &Native::local, 4}, {"bounds", &Native::bounds, 1}, {"hit", &Native::hit, 3},
        {"stage", &Native::stage, 1}, {"print", &Native::print, 1}, {"sound", &Native::sound, 4},
        {"stopSound", &Native::stopSound, 1}, {"open", &Native::open, 2}, {"create", &Native::create, 1},
        {"release", &Native::release, 1}, {"matrix", &Native::matrix, 7}, {"graphics", &Native::graphics, 2},
        {"image", &Native::image, 1}, {"imageSize", &Native::imageSize, 1},
        {"releaseImage", &Native::releaseImage, 1}, {"snapshot", &Native::snapshot, 6},
        {"textStyle", &Native::textStyle, 12}, {"effects", &Native::effects, 5}, {"mask", &Native::mask, 5}};
    for (const auto& f : functions)
        JS_SetPropertyStr(_ctx, native, f.name, JS_NewCFunction(_ctx, f.function, f.name, f.length));
    JS_SetPropertyStr(_ctx, global, "__phoenix", native);
    JS_SetPropertyStr(_ctx, global, "__phoenixFrames", JS_NewArray(_ctx));
    JS_FreeValue(_ctx, global);
    begin();
    JSValue ran = JS_Eval(_ctx, kPrelude, std::strlen(kPrelude), "<createjs>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ran))
        report("CreateJS");
    JS_FreeValue(_ctx, ran);
    end();

    // The pointer on the stage, for stagemousedown and the rest: as it
    // passes down to what it is on (capture), or at the stage itself.
    const std::shared_ptr<bool> alive = _alive;
    for (const char* type : {display::events::kMouseDown, display::events::kMouseMove, display::events::kMouseUp})
        for (const bool capture : {true, false})
            _runtime._stage->addEventListener(
                type,
                [this, alive, capture, type](display::Event& event) {
                    const bool atStage = event.target == _runtime._stage.get();
                    if (*alive && capture != atStage)
                        stagePointer(type, event);
                },
                capture);
}

Scripts::~Scripts()
{
    *_alive = false;
    for (const auto& [key, listening] : _listening)
        if (const auto object = listening.object.lock())
            object->removeEventListener(listening.id);
    JS_FreeContext(_ctx);
    JS_FreeRuntime(_js);
}

void Scripts::begin()
{
    if (_depth++ == 0)
        _deadline = Clock::now() + kTimeLimit;
}

int Scripts::idOf(Clip& clip)
{
    const auto found = _ids.find(clip.life.get());
    if (found != _ids.end())
        return found->second;
    const int id = _nextId++;
    _ids[clip.life.get()] = id;
    _clips[id] = clip.life;
    return id;
}

Clip* Scripts::clipOf(int id) const
{
    const auto found = _clips.find(id);
    if (found == _clips.end() || !*found->second || (*found->second)->gone)
        return nullptr;
    return *found->second;
}

display::DisplayObject* Scripts::displayOf(int id) const
{
    return sharedOf(id).get();
}

std::shared_ptr<display::DisplayObject> Scripts::sharedOf(int id) const
{
    if (id == kStage)
        return _runtime._stage;
    const auto made = _objects.find(id);
    if (made != _objects.end())
        return made->second;
    Clip* clip = clipOf(id);
    return clip ? clip->display : nullptr;
}

int Scripts::idOfDisplay(const display::DisplayObject* object)
{
    if (!object)
        return kNone;
    if (object == _runtime._stage.get())
        return kStage;
    if (object->clip && !object->clip->gone && object->clip->display.get() == object)
        return idOf(*object->clip);
    const auto made = _objectIds.find(object);
    return made == _objectIds.end() ? kNone : made->second;
}

int Scripts::imageNamed(const std::string& source)
{
    if (source.empty())
        return -1;
    const auto known = _imageNames.find(source);
    if (known != _imageNames.end())
        return known->second;
    const auto decode = [this](const std::vector<uint8_t>& bytes) -> std::shared_ptr<display::BitmapData> {
        int width = 0, height = 0;
        std::vector<uint32_t> pixels;
        if (script::decodePng(bytes, width, height, pixels))
        {
            for (uint32_t& pixel : pixels)
                pixel = display::premultiplied(pixel);
        }
        else if (!_runtime.decodeImage || !_runtime.decodeImage(bytes, width, height, pixels))
            return nullptr;
        auto data = std::make_shared<display::BitmapData>(width, height, true, 0);
        if (data->disposed() || pixels.size() < static_cast<size_t>(width) * height)
            return nullptr;
        std::copy(pixels.begin(), pixels.begin() + static_cast<std::ptrdiff_t>(width) * height, data->bits());
        data->changed();
        return data;
    };
    // A library bitmap: by its linkage, its name, or the name lib and
    // images give it (no folders, no extension, only what a JavaScript name
    // can have).
    std::shared_ptr<display::BitmapData> pixels;
    for (const Bitmap* bitmap : _runtime._bitmaps)
    {
        if (!bitmap)
            continue;
        std::string base = bitmap->name.substr(bitmap->name.find_last_of('/') + 1);
        const std::string whole = base;
        if (const size_t dot = base.find_last_of('.'); dot != std::string::npos && dot > 0)
            base.erase(dot);
        base.erase(std::remove_if(base.begin(), base.end(),
                       [](char ch) { return !(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '$'); }),
            base.end());
        if (attributeOf(bitmap->extraAttributes, "linkageClassName") == source ||
            attributeOf(bitmap->extraAttributes, "linkageIdentifier") == source || bitmap->name == source ||
            whole == source || base == source)
        {
            pixels = script::libraryBitmap(*bitmap, decode);
            break;
        }
    }
    // A file, from the movie's folder.
    if (!pixels && source.find("://") == std::string::npos)
    {
        std::filesystem::path path = std::filesystem::u8path(source);
        if (path.is_relative() && !_runtime.folder.empty())
            path = std::filesystem::u8path(_runtime.folder) / path;
        std::ifstream file(path, std::ios::binary);
        if (file)
        {
            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            pixels = decode(bytes);
        }
    }
    if (!pixels)
        return -1;
    const int id = _nextImage++;
    _images[id] = pixels;
    _imageNames[source] = id;
    return id;
}

void Scripts::sync()
{
    Native::call(*this, "__phoenixSync", {}, "drawing");
    // Promises' reactions and the finalizers of what the scripts let go of.
    JSContext* at = nullptr;
    for (int i = 0; i < 1000; ++i)
    {
        const int ran = JS_ExecutePendingJob(_js, &at);
        if (ran == 0)
            break;
        if (ran < 0)
        {
            begin();
            report("a promise");
            end();
        }
    }
    syncMasks();
}

void Scripts::syncMasks()
{
    for (auto it = _masks.begin(); it != _masks.end();)
    {
        display::DisplayObject* object = displayOf(it->first);
        Masking& masking = it->second;
        if (!object || !masking.proxy || object->mask() != masking.proxy.get())
        {
            it = _masks.erase(it);
            continue;
        }
        if (masking.shape >= 0)
        {
            // The shape where CreateJS draws a mask: in the masked object's
            // parent's space.
            display::DisplayObject* shape = displayOf(masking.shape);
            const display::Graphics* drawn = shape && (shape->kind() == display::ObjectKind::Sprite ||
                                                          shape->kind() == display::ObjectKind::MovieClip)
                ? &static_cast<display::Sprite*>(shape)->graphics
                : nullptr;
            auto& proxy = static_cast<display::Shape&>(*masking.proxy);
            if (drawn && drawn->revision() != masking.copied)
            {
                proxy.graphics.copyFrom(*drawn);
                masking.copied = drawn->revision();
            }
            else if (!drawn && masking.copied != 0)
            {
                proxy.graphics.clear();
                masking.copied = 0;
            }
            const display::Matrix toStage = object->parent() ? object->parent()->concatenatedMatrix() : display::Matrix();
            const display::Matrix placed = (shape ? shape->matrix() : display::Matrix()).then(toStage);
            if (placed != proxy.matrix())
                proxy.setMatrix(placed);
        }
        else
        {
            // The image at the cache's corner, in the object's own space.
            const display::Matrix placed =
                display::Matrix(1, 0, 0, 1, masking.x, masking.y).then(object->concatenatedMatrix());
            if (placed != masking.proxy->matrix())
                masking.proxy->setMatrix(placed);
        }
        ++it;
    }
}

void Scripts::listen(int id, const std::string& type, bool on)
{
    const auto key = std::make_pair(id, type);
    const auto found = _listening.find(key);
    if (!on)
    {
        if (found == _listening.end())
            return;
        if (const auto object = found->second.object.lock())
            object->removeEventListener(found->second.id);
        _listening.erase(found);
        return;
    }
    const char* shown = displayType(type, id == kStage);
    display::DisplayObject* object = displayOf(id);
    if (!shown || !object || found != _listening.end())
        return;
    if (type == "dblclick" && object->isInteractive())
        static_cast<display::InteractiveObject*>(object)->doubleClickEnabled = true;
    const std::shared_ptr<bool> alive = _alive;
    Listening listening;
    listening.object = object->shared();
    listening.id = object->addEventListener(shown, [this, alive, id, type](display::Event& event) {
        if (*alive)
            deliver(id, type, event);
    });
    _listening[key] = listening;
}

namespace {

/// The object an event is at (a clip or one the scripts made, or what
/// holds it), by its number; the stage's for one at the stage.
int targetOf(const display::Event& event, const std::function<int(const display::DisplayObject*)>& idOf)
{
    auto* object = dynamic_cast<display::DisplayObject*>(event.target);
    for (; object; object = object->parent())
        if (const int id = idOf(object); id != kNone)
            return id;
    return kStage;
}

} // namespace

void Scripts::deliver(int id, const std::string& type, display::Event& event)
{
    if (!event.script)
        event.script = std::make_shared<uint64_t>(++_eventSerial);
    const double serial = static_cast<double>(*static_cast<uint64_t*>(event.script.get()));
    const int target = targetOf(event, [this](const display::DisplayObject* object) { return idOfDisplay(object); });
    // A mouse event's place on the stage, or a key's code, character and
    // modifiers.
    double a = 0.0, b = 0.0;
    int modifiers = 0;
    if (event.eventClass() == display::Event::Class::Mouse)
    {
        const auto& mouse = static_cast<const display::MouseEvent&>(event);
        a = mouse.stageX;
        b = mouse.stageY;
    }
    else if (event.eventClass() == display::Event::Class::Keyboard)
    {
        const auto& key = static_cast<const display::KeyboardEvent&>(event);
        a = key.keyCode;
        b = key.charCode;
        modifiers = (key.shiftKey ? 1 : 0) | (key.ctrlKey ? 2 : 0) | (key.altKey ? 4 : 0);
    }
    Clip* clip = clipOf(id);
    const std::string where = (clip ? (clip->name.empty() ? clip->key : clip->name) : std::string("stage")) + ", " + type;
    ++_runtime._scriptRuns;
    bool stopped = false;
    Native::call(*this, "__phoenixEvent",
        {JS_NewInt32(_ctx, id), JS_NewString(_ctx, type.c_str()), JS_NewInt32(_ctx, target),
            JS_NewFloat64(_ctx, serial), JS_NewInt32(_ctx, static_cast<int>(event.eventPhase)), JS_NewFloat64(_ctx, a),
            JS_NewFloat64(_ctx, b), JS_NewInt32(_ctx, modifiers)},
        where, &stopped);
    if (stopped)
        event.stopPropagation();
    sync();
}

void Scripts::stagePointer(const std::string& type, display::Event& event)
{
    if (event.eventClass() != display::Event::Class::Mouse)
        return;
    const auto& mouse = static_cast<const display::MouseEvent&>(event);
    const int target = targetOf(event, [this](const display::DisplayObject* object) { return idOfDisplay(object); });
    Native::call(*this, "__phoenixStage",
        {JS_NewString(_ctx, type.c_str()), JS_NewFloat64(_ctx, mouse.stageX), JS_NewFloat64(_ctx, mouse.stageY),
            JS_NewInt32(_ctx, target)},
        "stage, " + type);
    sync();
}

void Scripts::afterEvents()
{
    Native::call(*this, "__phoenixReleased", {}, "stage, pressup");
    sync();
}

void Scripts::runFrame(Clip& clip, const Frame& frame, const std::string& where)
{
    if (!frame.actionScript || frame.actionScript->code.empty())
        return;
    auto found = _compiled.find(&frame);
    if (found == _compiled.end())
    {
        // As a function of its own, as a published movie has it: its var
        // declarations its own, this the clip. On the first line, so the
        // lines errors give are the script's.
        _sources.emplace(where, &frame);
        const std::string code = "(function () {" + frame.actionScript->code + "\n})";
        int index = -1;
        begin();
        JSValue function = JS_Eval(_ctx, code.c_str(), code.size(), where.c_str(), JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(function))
            report(where);
        else
        {
            JSValue global = JS_GetGlobalObject(_ctx);
            JSValue frames = JS_GetPropertyStr(_ctx, global, "__phoenixFrames");
            index = static_cast<int>(_compiled.size());
            JS_SetPropertyUint32(_ctx, frames, static_cast<uint32_t>(index), JS_DupValue(_ctx, function));
            JS_FreeValue(_ctx, frames);
            JS_FreeValue(_ctx, global);
        }
        JS_FreeValue(_ctx, function);
        end();
        found = _compiled.emplace(&frame, index).first;
    }
    if (found->second < 0)
        return;
    ++_runtime._scriptRuns;
    Native::call(*this, "__phoenixRun", {JS_NewInt32(_ctx, found->second), JS_NewInt32(_ctx, idOf(clip))}, where);
    sync();
}

void Scripts::tick()
{
    const double delta = 1000.0 / std::max(1.0, _runtime._frameRate);
    _time += delta;
    // The clips gone since, let go.
    std::vector<int> gone;
    for (const auto& [id, life] : _clips)
        if (!*life || (*life)->gone)
            gone.push_back(id);
    for (const int id : gone)
    {
        _ids.erase(_clips[id].get());
        _clips.erase(id);
        for (auto it = _listening.begin(); it != _listening.end();)
            it = it->first.first == id ? _listening.erase(it) : std::next(it);
        Native::call(*this, "__phoenixForget", {JS_NewInt32(_ctx, id)}, "tick");
    }
    ++_runtime._scriptRuns;
    Native::call(*this, "__phoenixTick", {JS_NewFloat64(_ctx, delta), JS_NewFloat64(_ctx, _time)}, "tick");
    sync();
}

void Scripts::report(const std::string& where)
{
    JSValue exception = JS_GetException(_ctx);
    std::string message = text(_ctx, exception);
    std::string stack;
    if (JS_IsError(exception))
    {
        JSValue value = JS_GetPropertyStr(_ctx, exception, "stack");
        if (JS_IsString(value))
            stack = text(_ctx, value);
        JS_FreeValue(_ctx, value);
    }
    JS_FreeValue(_ctx, exception);
    const bool timedOut = Clock::now() > _deadline;
    if (timedOut)
        message = "the script ran for longer than " + std::to_string(kTimeLimit.count()) +
            " seconds and was stopped (a loop that does not end?)";
    // The script the error is in: the frame script its stack runs through,
    // else the one that was run. Where a script was stopped is not known.
    std::string file;
    int line = 0;
    locate(stack, _sources, file, line);
    if (timedOut)
        line = 0;
    if (file.empty() && message.rfind("SyntaxError", 0) == 0 && _sources.count(where))
    {
        // A script that does not compile: the line is at its end.
        const size_t at = message.find(where + ":");
        if (at != std::string::npos)
            line = std::atoi(message.c_str() + at + where.size() + 1);
        file = where;
    }
    std::string told = file.empty() ? where : file;
    if (line > 0)
        told += ", line " + std::to_string(line);
    told += ": " + message;
    if (!file.empty() && file != where)
        told += " (in " + where + ")";
    const auto source = _sources.find(file.empty() ? where : file);
    // The line itself, as QuickJS's messages do not always name what was
    // wrong ("not a function").
    const auto* frame = source == _sources.end() ? nullptr : dynamic_cast<const Frame*>(source->second);
    if (frame && frame->actionScript && line > 0)
    {
        std::istringstream lines(frame->actionScript->code);
        std::string shown;
        for (int i = 0; i < line; ++i)
            if (!std::getline(lines, shown))
            {
                shown.clear();
                break;
            }
        const size_t first = shown.find_first_not_of(" \t\r");
        if (first != std::string::npos)
        {
            shown = shown.substr(first);
            while (!shown.empty() && (shown.back() == '\r' || shown.back() == ' '))
                shown.pop_back();
            told += "\n    " + shown;
        }
    }
    _runtime.tell(told, source == _sources.end() ? nullptr : source->second, line);
}

bool checkSyntax(const std::string& source, int& line, std::string& message)
{
    line = 0;
    message.clear();
    JSRuntime* runtime = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(runtime);
    const std::string code = "(function () {" + source + "\n})";
    JSValue compiled =
        JS_Eval(ctx, code.c_str(), code.size(), "<script>", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    const bool ok = !JS_IsException(compiled);
    if (!ok)
    {
        JSValue exception = JS_GetException(ctx);
        message = text(ctx, exception);
        std::string stack;
        if (JS_IsError(exception))
        {
            JSValue value = JS_GetPropertyStr(ctx, exception, "stack");
            if (JS_IsString(value))
                stack = text(ctx, value);
            JS_FreeValue(ctx, value);
        }
        const size_t at = stack.find("<script>:");
        if (at != std::string::npos)
            line = std::atoi(stack.c_str() + at + 9);
        JS_FreeValue(ctx, exception);
        // The line past the end the wrapper adds is the script's last.
        const int lines = static_cast<int>(std::count(source.begin(), source.end(), '\n')) + 1;
        line = std::min(line, lines);
    }
    JS_FreeValue(ctx, compiled);
    JS_FreeContext(ctx);
    JS_FreeRuntime(runtime);
    return ok;
}

} // namespace fla::play::canvas
