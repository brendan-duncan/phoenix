// flash.display: the display list, its drawing, Stage, Loader and the
// constants classes.

#include "native.h"

#include "../../data/frame.h"
#include "../../data/layer.h"
#include "../../data/symbol.h"
#include "../../data/timeline.h"
#include "../display/stage.h"

#include <algorithm>
#include <climits>

namespace fla {
namespace play {
namespace script {

namespace {

display::DisplayObject& object(VM& vm, const Val& self)
{
    return *displayHost(vm, self).object();
}

display::InteractiveObject& interactive(VM& vm, const Val& self)
{
    display::DisplayObject& o = object(vm, self);
    if (!o.isInteractive())
        vm.throwError("TypeError", "Error #1034: Type Coercion failed: InteractiveObject expected.", 1034);
    return static_cast<display::InteractiveObject&>(o);
}

display::DisplayObjectContainer& container(VM& vm, const Val& self)
{
    display::DisplayObject& o = object(vm, self);
    if (!o.isContainer())
        vm.throwError("TypeError", "Error #1034: Type Coercion failed: DisplayObjectContainer expected.", 1034);
    return static_cast<display::DisplayObjectContainer&>(o);
}

display::Sprite& sprite(VM& vm, const Val& self)
{
    display::DisplayObject& o = object(vm, self);
    if (o.kind() != display::ObjectKind::Sprite && o.kind() != display::ObjectKind::MovieClip)
        vm.throwError("TypeError", "Error #1034: Type Coercion failed: Sprite expected.", 1034);
    return static_cast<display::Sprite&>(o);
}

display::Stage& stageOf(VM& vm, const Val& self)
{
    display::DisplayObject& o = object(vm, self);
    if (o.kind() != display::ObjectKind::Stage)
        vm.throwError("TypeError", "Error #1034: Type Coercion failed: Stage expected.", 1034);
    return static_cast<display::Stage&>(o);
}

display::SimpleButton& button(VM& vm, const Val& self)
{
    display::DisplayObject& o = object(vm, self);
    if (o.kind() != display::ObjectKind::SimpleButton)
        vm.throwError("TypeError", "Error #1034: Type Coercion failed: SimpleButton expected.", 1034);
    return static_cast<display::SimpleButton&>(o);
}

display::Bitmap& bitmap(VM& vm, const Val& self)
{
    display::DisplayObject& o = object(vm, self);
    if (o.kind() != display::ObjectKind::Bitmap)
        vm.throwError("TypeError", "Error #1034: Type Coercion failed: Bitmap expected.", 1034);
    return static_cast<display::Bitmap&>(o);
}

display::Graphics& graphics(VM& vm, const Val& self)
{
    display::Graphics* drawn = hostOf<GraphicsHost>(vm, self, "Graphics").get();
    if (!drawn)
    {
        // The object it drew for is gone: drawing goes nowhere.
        static display::Graphics nowhere;
        nowhere.clear();
        return nowhere;
    }
    return *drawn;
}

/// The frame labels of a timeline, in frame order: name and frame (from 1).
std::vector<std::pair<std::string, int>> labelsOf(const Timeline* timeline)
{
    std::vector<std::pair<std::string, int>> labels;
    for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
        for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            if (frame && !frame->name.empty() && frame->labelType != "comment" && frame->labelType != "anchor")
                labels.emplace_back(frame->name, frame->index + 1);
    std::stable_sort(labels.begin(), labels.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    return labels;
}

/// The label on \a frame (from 0) of a timeline, or the last before it
/// with \a before.
std::string labelAt(const Timeline* timeline, int frame, bool before)
{
    std::string label;
    int at = -1;
    for (const auto& [name, index] : labelsOf(timeline))
    {
        if (index - 1 == frame || (before && index - 1 < frame && index - 1 >= at))
        {
            label = name;
            at = index - 1;
        }
    }
    return label;
}

uint32_t colorArg(const Args& args, size_t i, uint32_t otherwise = 0)
{
    return given(args, i) ? args[i].toUint32() : otherwise;
}

display::Gradient gradientOf(Bindings& b, VM& vm, const Args& args, size_t first)
{
    display::Gradient gradient;
    gradient.radial = stringArg(vm, args, first) == "radial";
    std::vector<uint32_t> colors;
    for (const double c : b.numbers(args[first + 1]))
        colors.push_back(static_cast<uint32_t>(as3::toUint32(c)));
    gradient.stops = display::Gradient::stopsOf(colors, b.numbers(args[first + 2]), b.numbers(args[first + 3]));
    if (args[first + 4].isObject())
        gradient.matrix = b.matrixOf(args[first + 4]);
    const std::string spread = stringArg(vm, args, first + 5, "pad");
    gradient.spread = spread == "reflect" ? display::Gradient::Spread::Reflect
        : spread == "repeat"              ? display::Gradient::Spread::Repeat
                                          : display::Gradient::Spread::Pad;
    gradient.linearRGB = stringArg(vm, args, first + 6) == "linearRGB";
    gradient.focalPointRatio = numberArg(vm, args, first + 7);
    return gradient;
}

std::vector<int> integers(VM& vm, const Val& array)
{
    std::vector<int> out;
    if (array.isObject())
        for (const Val& element : array->elements)
            out.push_back(static_cast<int>(vm.toNumber(element)));
    return out;
}

} // namespace

Val Bindings::graphicsOf(const Val& self)
{
    DisplayHost& host = displayHost(vm, self);
    if (!host.graphics.isUndefined())
        return host.graphics;
    display::DisplayObject* o = host.object();
    display::Graphics* drawn = nullptr;
    if (o->kind() == display::ObjectKind::Shape)
        drawn = &static_cast<display::Shape*>(o)->graphics;
    else if (o->kind() == display::ObjectKind::Sprite || o->kind() == display::ObjectKind::MovieClip)
        drawn = &static_cast<display::Sprite*>(o)->graphics;
    if (!drawn)
        return Val::null();
    auto graphicsHost = std::make_shared<GraphicsHost>();
    graphicsHost->owner = host.shared();
    graphicsHost->graphics = drawn;
    host.graphics = instance("flash.display.Graphics", graphicsHost);
    return host.graphics;
}

void Bindings::defineDisplay()
{
    vm.defineNativeClass("flash.display.IBitmapDrawable").interface().define();

    // DisplayObject.
    const auto property = [](double (display::DisplayObject::*get)() const, void (display::DisplayObject::*set)(double)) {
        return std::make_pair(NATIVE(get) { return number((object(vm, self).*get)()); },
            NATIVE(set) {
                const double value = vm.toNumber(args[0]);
                if (!std::isnan(value))
                    (object(vm, self).*set)(value);
                return Val();
            });
    };
    const auto [getX, setX] = property(&display::DisplayObject::x, &display::DisplayObject::setX);
    const auto [getY, setY] = property(&display::DisplayObject::y, &display::DisplayObject::setY);
    const auto [getScaleX, setScaleX] = property(&display::DisplayObject::scaleX, &display::DisplayObject::setScaleX);
    const auto [getScaleY, setScaleY] = property(&display::DisplayObject::scaleY, &display::DisplayObject::setScaleY);
    const auto [getRotation, setRotation] =
        property(&display::DisplayObject::rotation, &display::DisplayObject::setRotation);
    const auto [getAlpha, setAlpha] = property(&display::DisplayObject::alpha, &display::DisplayObject::setAlpha);
    const auto stub = NATIVE() { return number(0); };
    const auto ignore = NATIVE() { return Val(); };
    vm.defineNativeClass("flash.display.DisplayObject")
        .extends("flash.events.EventDispatcher")
        .implements("flash.display.IBitmapDrawable")
        .constructor(NATIVE() {
            if (!self.host<DisplayHost>())
                vm.throwError("ArgumentError", "Error #2012: DisplayObject class cannot be instantiated.", 2012);
            return Val();
        })
        .property("x", getX, setX)
        .property("y", getY, setY)
        .property("scaleX", getScaleX, setScaleX)
        .property("scaleY", getScaleY, setScaleY)
        .property("rotation", getRotation, setRotation)
        .property("alpha", getAlpha, setAlpha)
        .property("width", NATIVE() { return number(object(vm, self).width()); }, NATIVE() {
            const double value = vm.toNumber(args[0]);
            if (!std::isnan(value))
                object(vm, self).setWidth(value);
            return Val();
        })
        .property("height", NATIVE() { return number(object(vm, self).height()); }, NATIVE() {
            const double value = vm.toNumber(args[0]);
            if (!std::isnan(value))
                object(vm, self).setHeight(value);
            return Val();
        })
        .property("z", stub, ignore)
        .property("scaleZ", NATIVE() { return number(1); }, ignore)
        .property("rotationX", stub, ignore)
        .property("rotationY", stub, ignore)
        .property("rotationZ", stub, ignore)
        .property("visible", NATIVE() { return boolean(object(vm, self).visible()); }, NATIVE() {
            object(vm, self).setVisible(args[0].toBoolean());
            return Val();
        })
        .property("name", NATIVE() { return text(object(vm, self).name()); }, NATIVE() {
            object(vm, self).setName(args[0].isNullish() ? std::string() : vm.toString(args[0]));
            return Val();
        })
        .getter("parent", NATIVE(this) { return wrap(object(vm, self).parent()); })
        .getter("stage", NATIVE(this) { return wrap(object(vm, self).stage()); })
        .getter("root", NATIVE(this) {
            display::DisplayObject& o = object(vm, self);
            if (display::Stage* stage = o.stage())
                return wrap(stage->rootObject());
            // Off the stage: the main timeline is still its own root.
            return o.clip && o.clip == runtime._root.get() ? self : Val::null();
        })
        .getter("loaderInfo", NATIVE(this) {
            display::DisplayObject& o = object(vm, self);
            return o.stage() || o.clip ? rootLoaderInfo() : Val::null();
        })
        .property("mask", NATIVE(this) { return wrap(object(vm, self).mask()); }, NATIVE(this) {
            object(vm, self).setMask(sharedDisplayOf(args[0]));
            return Val();
        })
        .property("filters", NATIVE(this) {
            std::vector<Val> out;
            for (const display::BitmapFilter& filter : object(vm, self).filters())
                out.push_back(newFilter(filter));
            return vm.newArray(std::move(out));
        }, NATIVE(this) {
            std::vector<display::BitmapFilter> filters;
            if (args[0].isObject())
                for (const Val& element : args[0]->elements)
                {
                    display::BitmapFilter filter;
                    if (!filterOf(element, filter))
                        vm.throwError("ArgumentError", "Error #2005: Parameter 0 is of the incorrect type. Should be type Filter.",
                            2005);
                    filters.push_back(filter);
                }
            object(vm, self).setFilters(std::move(filters));
            return Val();
        })
        .property("blendMode", NATIVE() { return text(display::blendModeName(object(vm, self).blendMode())); },
            NATIVE() {
                display::BlendMode mode = display::BlendMode::Normal;
                if (!display::blendModeNamed(vm.toString(args[0]), mode))
                    vm.throwError("ArgumentError", "Error #2008: Parameter blendMode must be one of the accepted values.", 2008);
                object(vm, self).setBlendMode(mode);
                return Val();
            })
        .property("cacheAsBitmap", NATIVE() { return boolean(object(vm, self).cacheAsBitmap()); }, NATIVE() {
            object(vm, self).setCacheAsBitmap(args[0].toBoolean());
            return Val();
        })
        .property("cacheAsBitmapMatrix", NATIVE() { return Val::null(); }, ignore)
        .property("opaqueBackground", NATIVE() {
            const std::optional<uint32_t> color = object(vm, self).opaqueBackground();
            return color ? number(*color) : Val::null();
        }, NATIVE() {
            object(vm, self).setOpaqueBackground(
                args[0].isNullish() ? std::nullopt : std::optional<uint32_t>(args[0].toUint32() & 0xffffff));
            return Val();
        })
        .property("scrollRect", NATIVE(this) {
            const std::optional<display::Rect> rect = object(vm, self).scrollRect();
            return rect ? newRectangle(*rect) : Val::null();
        }, NATIVE(this) {
            object(vm, self).setScrollRect(
                args[0].isNullish() ? std::nullopt : std::optional<display::Rect>(rectOf(args[0])));
            return Val();
        })
        .property("scale9Grid", NATIVE(this) {
            const std::optional<display::Rect> rect = object(vm, self).scale9Grid();
            return rect ? newRectangle(*rect) : Val::null();
        }, NATIVE(this) {
            object(vm, self).setScale9Grid(
                args[0].isNullish() ? std::nullopt : std::optional<display::Rect>(rectOf(args[0])));
            return Val();
        })
        .property("transform", NATIVE(this) {
            auto host = std::make_shared<TransformHost>();
            host->object = displayHost(vm, self).shared();
            return instance("flash.geom.Transform", host);
        }, NATIVE(this) {
            const TransformHost* from = args[0].host<TransformHost>();
            if (!from || !from->object)
                vm.throwError("TypeError", "Error #2007: Parameter transform must be non-null.", 2007);
            display::DisplayObject& o = object(vm, self);
            o.setMatrix(from->object->matrix());
            o.setColorTransform(from->object->colorTransform());
            return Val();
        })
        .getter("mouseX", NATIVE() { return number(object(vm, self).mouseX()); })
        .getter("mouseY", NATIVE() { return number(object(vm, self).mouseY()); })
        .property("accessibilityProperties", NATIVE() { return Val::null(); }, ignore)
        .method("getBounds", NATIVE(this) {
            return newRectangle(object(vm, self).getBounds(displayOf(args[0])));
        }, 1)
        .method("getRect", NATIVE(this) { return newRectangle(object(vm, self).getRect(displayOf(args[0]))); }, 1)
        .method("localToGlobal", NATIVE(this) {
            const display::Point p = object(vm, self).localToGlobal(pointOf(args[0]));
            return newPoint(p.x, p.y);
        }, 1)
        .method("globalToLocal", NATIVE(this) {
            const display::Point p = object(vm, self).globalToLocal(pointOf(args[0]));
            return newPoint(p.x, p.y);
        }, 1)
        .method("hitTestPoint", NATIVE() {
            return boolean(object(vm, self).hitTestPoint(numberArg(vm, args, 0), numberArg(vm, args, 1), booleanArg(args, 2)));
        }, 2)
        .method("hitTestObject", NATIVE(this) {
            display::DisplayObject* other = displayOf(args[0]);
            return boolean(other && object(vm, self).hitTestObject(*other));
        }, 1)
        .method("toString", NATIVE() {
            const as3::ClassInfo* cls = self->cls.get();
            return text("[object " + (cls ? cls->shortName : std::string("DisplayObject")) + "]");
        })
        .define();

    // InteractiveObject.
    vm.defineNativeClass("flash.display.InteractiveObject")
        .extends("flash.display.DisplayObject")
        .property("mouseEnabled", NATIVE() { return boolean(interactive(vm, self).mouseEnabled); }, NATIVE() {
            interactive(vm, self).mouseEnabled = args[0].toBoolean();
            return Val();
        })
        .property("doubleClickEnabled", NATIVE() { return boolean(interactive(vm, self).doubleClickEnabled); },
            NATIVE() {
                interactive(vm, self).doubleClickEnabled = args[0].toBoolean();
                return Val();
            })
        .property("tabEnabled", NATIVE() { return boolean(interactive(vm, self).tabEnabled); }, NATIVE() {
            interactive(vm, self).tabEnabled = args[0].toBoolean();
            return Val();
        })
        .property("tabIndex", NATIVE() { return number(interactive(vm, self).tabIndex); }, NATIVE() {
            interactive(vm, self).tabIndex = args[0].toInt32();
            return Val();
        })
        .property("focusRect", NATIVE() {
            const std::optional<bool> rect = interactive(vm, self).focusRect;
            return rect ? boolean(*rect) : Val::null();
        }, NATIVE() {
            interactive(vm, self).focusRect = args[0].isNullish() ? std::nullopt : std::optional<bool>(args[0].toBoolean());
            return Val();
        })
        .property("contextMenu", NATIVE() { return Val::null(); }, ignore)
        .property("needsSoftKeyboard", NATIVE() { return boolean(false); }, ignore)
        .method("requestSoftKeyboard", NATIVE() { return boolean(false); })
        .define();

    // DisplayObjectContainer.
    vm.defineNativeClass("flash.display.DisplayObjectContainer")
        .extends("flash.display.InteractiveObject")
        .getter("numChildren", NATIVE() { return number(container(vm, self).numChildren()); })
        .property("mouseChildren", NATIVE() { return boolean(container(vm, self).mouseChildren); }, NATIVE() {
            container(vm, self).mouseChildren = args[0].toBoolean();
            return Val();
        })
        .property("tabChildren", NATIVE() { return boolean(container(vm, self).tabChildren); }, NATIVE() {
            container(vm, self).tabChildren = args[0].toBoolean();
            return Val();
        })
        .method("addChild", NATIVE(this) {
            throwDisplayError(vm, container(vm, self).addChild(sharedDisplayOf(args[0])));
            return args[0];
        }, 1)
        .method("addChildAt", NATIVE(this) {
            throwDisplayError(vm, container(vm, self).addChildAt(sharedDisplayOf(args[0]), intArg(args, 1)));
            return args[0];
        }, 2)
        .method("removeChild", NATIVE(this) {
            display::DisplayObject* child = displayOf(args[0]);
            if (!child)
                vm.throwError("TypeError", "Error #2007: Parameter child must be non-null.", 2007);
            std::shared_ptr<display::DisplayObject> keep = child->shared();
            throwDisplayError(vm, container(vm, self).removeChild(child));
            return args[0];
        }, 1)
        .method("removeChildAt", NATIVE(this) {
            std::shared_ptr<display::DisplayObject> removed;
            throwDisplayError(vm, container(vm, self).removeChildAt(intArg(args, 0), &removed));
            return wrap(removed.get());
        }, 1)
        .method("removeChildren", NATIVE() {
            throwDisplayError(vm, container(vm, self).removeChildren(intArg(args, 0), intArg(args, 1, INT_MAX)));
            return Val();
        })
        .method("getChildAt", NATIVE(this) {
            std::shared_ptr<display::DisplayObject> child = container(vm, self).getChildAt(intArg(args, 0));
            if (!child)
                throwDisplayError(vm, display::DisplayError::IndexOutOfRange);
            return wrap(child.get());
        }, 1)
        .method("getChildIndex", NATIVE(this) {
            const int index = container(vm, self).getChildIndex(displayOf(args[0]));
            if (index < 0)
                throwDisplayError(vm, display::DisplayError::NotAChild);
            return number(index);
        }, 1)
        .method("setChildIndex", NATIVE(this) {
            throwDisplayError(vm, container(vm, self).setChildIndex(displayOf(args[0]), intArg(args, 1)));
            return Val();
        }, 2)
        .method("swapChildren", NATIVE(this) {
            throwDisplayError(vm, container(vm, self).swapChildren(displayOf(args[0]), displayOf(args[1])));
            return Val();
        }, 2)
        .method("swapChildrenAt", NATIVE() {
            throwDisplayError(vm, container(vm, self).swapChildrenAt(intArg(args, 0), intArg(args, 1)));
            return Val();
        }, 2)
        .method("contains", NATIVE(this) {
            display::DisplayObject* child = displayOf(args[0]);
            return boolean(child && container(vm, self).contains(child));
        }, 1)
        .method("getChildByName", NATIVE(this) {
            return wrap(container(vm, self).getChildByName(stringArg(vm, args, 0)).get());
        }, 1)
        .method("getObjectsUnderPoint", NATIVE(this) {
            std::vector<Val> out;
            for (const auto& under : container(vm, self).getObjectsUnderPoint(pointOf(args[0])))
                out.push_back(wrap(under.get()));
            return vm.newArray(std::move(out));
        }, 1)
        .method("areInaccessibleObjectsUnderPoint", NATIVE() { return boolean(false); }, 1)
        .method("stopAllMovieClips", NATIVE() {
            std::function<void(display::DisplayObject&)> stop = [&stop](display::DisplayObject& at) {
                if (at.clip)
                    at.clip->playing = false;
                if (at.isContainer())
                    for (const auto& child : static_cast<display::DisplayObjectContainer&>(at).children())
                        if (child)
                            stop(*child);
            };
            stop(object(vm, self));
            return Val();
        })
        .define();

    // Sprite.
    vm.defineNativeClass("flash.display.Sprite")
        .extends("flash.display.DisplayObjectContainer")
        .constructor(NATIVE(this) {
            if (self.host<DisplayHost>())
                return Val();
            if (const Symbol* symbol = symbolOf(self->cls.get()))
                attachSymbol(self, *symbol);
            else
                attach(self, std::make_shared<display::Sprite>());
            return Val();
        })
        .getter("graphics", NATIVE(this) { return graphicsOf(self); })
        .property("buttonMode", NATIVE() { return boolean(sprite(vm, self).buttonMode); }, NATIVE() {
            sprite(vm, self).buttonMode = args[0].toBoolean();
            return Val();
        })
        .property("useHandCursor", NATIVE() { return boolean(sprite(vm, self).useHandCursor); }, NATIVE() {
            sprite(vm, self).useHandCursor = args[0].toBoolean();
            return Val();
        })
        .property("hitArea", NATIVE(this) { return wrap(sprite(vm, self).hitArea().get()); }, NATIVE(this) {
            std::shared_ptr<display::DisplayObject> area = sharedDisplayOf(args[0]);
            if (area && area->kind() != display::ObjectKind::Sprite && area->kind() != display::ObjectKind::MovieClip)
                area = nullptr;
            sprite(vm, self).setHitArea(std::static_pointer_cast<display::Sprite>(area));
            return Val();
        })
        .property("soundTransform", NATIVE(this) { return vm.construct(vm.findDefinition("flash.media.SoundTransform")); },
            ignore)
        .getter("dropTarget", NATIVE(this) {
            (void)self;
            return wrap(runtime._stage->dropTarget());
        })
        .method("startDrag", NATIVE(this) {
            display::Sprite& s = sprite(vm, self);
            const display::Rect bounds = args[1].isObject() ? rectOf(args[1]) : display::Rect();
            runtime._stage->startDrag(s, booleanArg(args, 0), args[1].isObject() ? &bounds : nullptr);
            return Val();
        })
        .method("stopDrag", NATIVE(this) {
            (void)self;
            runtime._stage->stopDrag();
            return Val();
        })
        .method("startTouchDrag", ignore)
        .method("stopTouchDrag", ignore)
        .define();

    // MovieClip.
    const auto clipOf = [](VM& vm, const Val& self) { return displayHost(vm, self).clip(); };
    const auto gotoFrame = [this, clipOf](bool play) {
        return NATIVE(this, clipOf, play) {
            Clip* clip = clipOf(vm, self);
            if (!clip)
                return Val();
            // gotoAndPlay(frame, scene): on the main timeline, another scene.
            if (given(args, 1) && !args[1].isNull() && clip == runtime._root.get())
            {
                const std::string sceneName = vm.toString(args[1]);
                const int scene = runtime.sceneNamed(sceneName);
                if (scene < 0)
                    vm.throwError("ArgumentError", "Error #2108: Scene " + sceneName + " was not found.", 2108);
                const int frame = runtime.frameIn(runtime._scenes[static_cast<size_t>(scene)], args[0]);
                if (scene != runtime._scene)
                {
                    runtime.goToScene(scene, std::max(0, frame), play);
                    return Val();
                }
            }
            const int frame = runtime.frameOf(*clip, args[0]);
            if (frame >= 0)
            {
                goToNow(*clip, frame, play);
                return Val();
            }
            // A label in another scene of the main timeline.
            if (clip == runtime._root.get() && args[0].isString())
                for (size_t scene = 0; scene < runtime._scenes.size(); ++scene)
                {
                    const int there = runtime.frameIn(runtime._scenes[scene], args[0]);
                    if (there >= 0)
                    {
                        runtime.goToScene(static_cast<int>(scene), there, play);
                        return Val();
                    }
                }
            if (args[0].isString())
                vm.throwError("ArgumentError", "Error #2109: Frame label " + args[0].str() + " not found in scene " +
                        (clip->timeline ? clip->timeline->name : std::string()) + ".",
                    2109);
            return Val();
        };
    };
    vm.defineNativeClass("flash.display.MovieClip")
        .extends("flash.display.Sprite")
        .dynamic()
        .constructor(NATIVE(this) {
            if (self.host<DisplayHost>())
                return Val();
            if (const Symbol* symbol = symbolOf(self->cls.get()))
                attachSymbol(self, *symbol);
            else
                attach(self, std::make_shared<display::MovieClip>());
            return Val();
        })
        .getter("currentFrame", NATIVE(clipOf) {
            const Clip* clip = clipOf(vm, self);
            return number(clip ? clip->frame + 1 : 1);
        })
        .getter("totalFrames", NATIVE(clipOf) {
            const Clip* clip = clipOf(vm, self);
            return number(clip ? clip->length : 1);
        })
        .getter("framesLoaded", NATIVE(clipOf) {
            const Clip* clip = clipOf(vm, self);
            return number(clip ? clip->length : 1);
        })
        .getter("isPlaying", NATIVE(clipOf) {
            const Clip* clip = clipOf(vm, self);
            return boolean(clip && clip->playing && clip->length > 1);
        })
        .getter("currentLabel", NATIVE(clipOf) {
            const Clip* clip = clipOf(vm, self);
            const std::string label = clip ? labelAt(clip->timeline, clip->frame, true) : std::string();
            return label.empty() ? Val::null() : text(label);
        })
        .getter("currentFrameLabel", NATIVE(clipOf) {
            const Clip* clip = clipOf(vm, self);
            const std::string label = clip ? labelAt(clip->timeline, clip->frame, false) : std::string();
            return label.empty() ? Val::null() : text(label);
        })
        .getter("currentLabels", NATIVE(this, clipOf) {
            const Clip* clip = clipOf(vm, self);
            std::vector<Val> out;
            for (const auto& [name, frame] : labelsOf(clip ? clip->timeline : nullptr))
                out.push_back(vm.construct(vm.findDefinition("flash.display.FrameLabel"), {text(name), number(frame)}));
            return vm.newArray(std::move(out));
        })
        .getter("currentScene", NATIVE(this, clipOf) {
            const Clip* clip = clipOf(vm, self);
            const Val scene = vm.findDefinition("flash.display.Scene");
            const bool isRoot = clip == runtime._root.get();
            const Timeline* timeline = clip ? clip->timeline : nullptr;
            std::vector<Val> labels;
            for (const auto& [name, frame] : labelsOf(timeline))
                labels.push_back(vm.construct(vm.findDefinition("flash.display.FrameLabel"), {text(name), number(frame)}));
            return vm.construct(scene, {text(isRoot && timeline ? timeline->name : std::string("Scene 1")),
                                           vm.newArray(std::move(labels)), number(clip ? clip->length : 1)});
        })
        .getter("scenes", NATIVE(this, clipOf) {
            const Clip* clip = clipOf(vm, self);
            const Val scene = vm.findDefinition("flash.display.Scene");
            std::vector<const Timeline*> timelines;
            if (clip == runtime._root.get())
                timelines = runtime._scenes;
            else
                timelines.push_back(clip ? clip->timeline : nullptr);
            std::vector<Val> out;
            for (const Timeline* timeline : timelines)
            {
                std::vector<Val> labels;
                for (const auto& [name, frame] : labelsOf(timeline))
                    labels.push_back(vm.construct(vm.findDefinition("flash.display.FrameLabel"), {text(name), number(frame)}));
                int length = 1;
                for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
                    for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                        if (frame)
                            length = std::max(length, frame->index + std::max(1, frame->duration));
                out.push_back(vm.construct(scene, {text(clip == runtime._root.get() && timeline ? timeline->name
                                                                                               : std::string("Scene 1")),
                                                      vm.newArray(std::move(labels)), number(length)}));
            }
            return vm.newArray(std::move(out));
        })
        .property("enabled", NATIVE() {
            display::DisplayObject& o = object(vm, self);
            return boolean(o.kind() != display::ObjectKind::MovieClip || static_cast<display::MovieClip&>(o).enabled);
        }, NATIVE() {
            display::DisplayObject& o = object(vm, self);
            if (o.kind() == display::ObjectKind::MovieClip)
                static_cast<display::MovieClip&>(o).enabled = args[0].toBoolean();
            return Val();
        })
        .property("trackAsMenu", NATIVE() {
            display::DisplayObject& o = object(vm, self);
            return boolean(o.kind() == display::ObjectKind::MovieClip && static_cast<display::MovieClip&>(o).trackAsMenu);
        }, NATIVE() {
            display::DisplayObject& o = object(vm, self);
            if (o.kind() == display::ObjectKind::MovieClip)
                static_cast<display::MovieClip&>(o).trackAsMenu = args[0].toBoolean();
            return Val();
        })
        .method("play", NATIVE(clipOf) {
            if (Clip* clip = clipOf(vm, self))
                clip->playing = true;
            return Val();
        })
        .method("stop", NATIVE(clipOf) {
            if (Clip* clip = clipOf(vm, self))
                clip->playing = false;
            return Val();
        })
        .method("nextFrame", NATIVE(this, clipOf) {
            if (Clip* clip = clipOf(vm, self))
                goToNow(*clip, std::min(clip->frame + 1, clip->length - 1), false);
            return Val();
        })
        .method("prevFrame", NATIVE(this, clipOf) {
            if (Clip* clip = clipOf(vm, self))
                goToNow(*clip, std::max(clip->frame - 1, 0), false);
            return Val();
        })
        .method("nextScene", NATIVE(this, clipOf) {
            if (clipOf(vm, self) == runtime._root.get() && runtime._scene + 1 < runtime.sceneCount())
                runtime.goToScene(runtime._scene + 1, 0, true);
            return Val();
        })
        .method("prevScene", NATIVE(this, clipOf) {
            if (clipOf(vm, self) == runtime._root.get() && runtime._scene > 0)
                runtime.goToScene(runtime._scene - 1, 0, true);
            return Val();
        })
        .method("gotoAndStop", gotoFrame(false), 1)
        .method("gotoAndPlay", gotoFrame(true), 1)
        .method("addFrameScript", NATIVE(clipOf) {
            Clip* clip = clipOf(vm, self);
            for (size_t i = 0; clip && i + 1 < args.size(); i += 2)
            {
                const int frame = static_cast<int>(vm.toNumber(args[i]));
                if (args[i + 1].isFunction())
                    clip->frameScripts[frame] = args[i + 1];
                else
                    clip->frameScripts.erase(frame);
            }
            return Val();
        })
        .define();

    // FrameLabel and Scene.
    vm.defineNativeClass("flash.display.FrameLabel")
        .extends("flash.events.EventDispatcher")
        .field("$name", text(""))
        .field("$frame", number(0))
        .constructor(NATIVE() {
            vm.setProperty(self, "$name", text(stringArg(vm, args, 0)));
            vm.setProperty(self, "$frame", number(intArg(args, 1)));
            return Val();
        })
        .getter("name", NATIVE() { return vm.getProperty(self, "$name"); })
        .getter("frame", NATIVE() { return vm.getProperty(self, "$frame"); })
        .define();
    vm.defineNativeClass("flash.display.Scene")
        .field("$name", text(""))
        .field("$labels", Val::null())
        .field("$numFrames", number(0))
        .constructor(NATIVE() {
            vm.setProperty(self, "$name", text(stringArg(vm, args, 0)));
            vm.setProperty(self, "$labels", args[1].isObject() ? args[1] : vm.newArray());
            vm.setProperty(self, "$numFrames", number(intArg(args, 2)));
            return Val();
        })
        .getter("name", NATIVE() { return vm.getProperty(self, "$name"); })
        .getter("labels", NATIVE() { return vm.getProperty(self, "$labels"); })
        .getter("numFrames", NATIVE() { return vm.getProperty(self, "$numFrames"); })
        .define();

    // Shape.
    vm.defineNativeClass("flash.display.Shape")
        .extends("flash.display.DisplayObject")
        .constructor(NATIVE(this) {
            if (!self.host<DisplayHost>())
                attach(self, std::make_shared<display::Shape>());
            return Val();
        })
        .getter("graphics", NATIVE(this) { return graphicsOf(self); })
        .define();

    // SimpleButton.
    const auto stateGet = [this](std::shared_ptr<display::DisplayObject> display::SimpleButton::*member) {
        return NATIVE(this, member) { return wrap((button(vm, self).*member).get()); };
    };
    const auto stateSet = [this](std::shared_ptr<display::DisplayObject> display::SimpleButton::*member) {
        return NATIVE(this, member)
        {
            display::SimpleButton& b = button(vm, self);
            b.*member = sharedDisplayOf(args[0]);
            b.invalidate();
            return Val();
        };
    };
    vm.defineNativeClass("flash.display.SimpleButton")
        .extends("flash.display.InteractiveObject")
        .constructor(NATIVE(this) {
            if (self.host<DisplayHost>())
                return Val();
            if (const Symbol* symbol = symbolOf(self->cls.get()))
            {
                attachSymbol(self, *symbol);
                return Val();
            }
            auto made = std::make_shared<display::SimpleButton>();
            made->upState = sharedDisplayOf(args[0]);
            made->overState = sharedDisplayOf(args[1]);
            made->downState = sharedDisplayOf(args[2]);
            made->hitTestState = sharedDisplayOf(args[3]);
            attach(self, made);
            return Val();
        })
        .property("upState", stateGet(&display::SimpleButton::upState), stateSet(&display::SimpleButton::upState))
        .property("overState", stateGet(&display::SimpleButton::overState), stateSet(&display::SimpleButton::overState))
        .property("downState", stateGet(&display::SimpleButton::downState), stateSet(&display::SimpleButton::downState))
        .property("hitTestState", stateGet(&display::SimpleButton::hitTestState),
            stateSet(&display::SimpleButton::hitTestState))
        .property("enabled", NATIVE() { return boolean(button(vm, self).enabled); }, NATIVE() {
            button(vm, self).enabled = args[0].toBoolean();
            return Val();
        })
        .property("useHandCursor", NATIVE() { return boolean(button(vm, self).useHandCursor); }, NATIVE() {
            button(vm, self).useHandCursor = args[0].toBoolean();
            return Val();
        })
        .property("trackAsMenu", NATIVE() { return boolean(button(vm, self).trackAsMenu); }, NATIVE() {
            button(vm, self).trackAsMenu = args[0].toBoolean();
            return Val();
        })
        .property("soundTransform", NATIVE(this) { return vm.construct(vm.findDefinition("flash.media.SoundTransform")); },
            ignore)
        .define();

    // Bitmap.
    vm.defineNativeClass("flash.display.Bitmap")
        .extends("flash.display.DisplayObject")
        .constructor(NATIVE(this) {
            if (self.host<DisplayHost>())
                return Val();
            const std::string snapping = stringArg(vm, args, 1, "auto");
            attach(self, std::make_shared<display::Bitmap>(bitmapDataOf(args[0]),
                             snapping == "never"    ? display::PixelSnapping::Never
                                 : snapping == "always" ? display::PixelSnapping::Always
                                                        : display::PixelSnapping::Auto,
                             booleanArg(args, 2)));
            return Val();
        })
        .property("bitmapData", NATIVE(this) {
            std::shared_ptr<display::BitmapData> data = bitmap(vm, self).bitmapData();
            return data ? newBitmapData(data) : Val::null();
        }, NATIVE(this) {
            bitmap(vm, self).setBitmapData(bitmapDataOf(args[0]));
            return Val();
        })
        .property("pixelSnapping", NATIVE() {
            switch (bitmap(vm, self).pixelSnapping)
            {
            case display::PixelSnapping::Never:
                return text("never");
            case display::PixelSnapping::Always:
                return text("always");
            default:
                return text("auto");
            }
        }, NATIVE() {
            const std::string value = vm.toString(args[0]);
            bitmap(vm, self).pixelSnapping = value == "never" ? display::PixelSnapping::Never
                : value == "always"                           ? display::PixelSnapping::Always
                                                              : display::PixelSnapping::Auto;
            return Val();
        })
        .property("smoothing", NATIVE() { return boolean(bitmap(vm, self).smoothing); }, NATIVE() {
            display::Bitmap& b = bitmap(vm, self);
            b.smoothing = args[0].toBoolean();
            b.invalidate();
            return Val();
        })
        .define();

    // Stage.
    vm.defineNativeClass("flash.display.Stage")
        .extends("flash.display.DisplayObjectContainer")
        .constructor(NATIVE() {
            if (!self.host<DisplayHost>())
                vm.throwError("ArgumentError", "Error #2012: Stage class cannot be instantiated.", 2012);
            return Val();
        })
        .property("stageWidth", NATIVE() { return number(stageOf(vm, self).stageWidth()); }, ignore)
        .property("stageHeight", NATIVE() { return number(stageOf(vm, self).stageHeight()); }, ignore)
        .getter("fullScreenWidth", NATIVE() { return number(stageOf(vm, self).stageWidth()); })
        .getter("fullScreenHeight", NATIVE() { return number(stageOf(vm, self).stageHeight()); })
        .property("fullScreenSourceRect", NATIVE() { return Val::null(); }, ignore)
        .getter("allowsFullScreen", NATIVE() { return boolean(false); })
        .getter("allowsFullScreenInteractive", NATIVE() { return boolean(false); })
        .getter("contentsScaleFactor", NATIVE() { return number(1); })
        .property("frameRate", NATIVE() { return number(stageOf(vm, self).frameRate()); }, NATIVE(this) {
            const double rate = std::clamp(vm.toNumber(args[0]), 0.01, 1000.0);
            if (std::isnan(rate))
                return Val();
            stageOf(vm, self).setFrameRate(rate);
            runtime._frameRate = rate;
            if (runtime.frameRateChanged)
                runtime.frameRateChanged(rate);
            return Val();
        })
        .property("focus", NATIVE(this) { return wrap(stageOf(vm, self).focus()); }, NATIVE(this) {
            display::DisplayObject* target = displayOf(args[0]);
            stageOf(vm, self).setFocus(
                target && target->isInteractive() ? static_cast<display::InteractiveObject*>(target) : nullptr);
            return Val();
        })
        .property("scaleMode", NATIVE() { return text(stageOf(vm, self).scaleMode); }, NATIVE() {
            stageOf(vm, self).scaleMode = vm.toString(args[0]);
            return Val();
        })
        .property("align", NATIVE() { return text(stageOf(vm, self).align); }, NATIVE() {
            stageOf(vm, self).align = vm.toString(args[0]);
            return Val();
        })
        .property("quality", NATIVE() {
            std::string quality = stageOf(vm, self).quality;
            std::transform(quality.begin(), quality.end(), quality.begin(), ::toupper);
            return text(quality);
        }, NATIVE() {
            std::string quality = vm.toString(args[0]);
            std::transform(quality.begin(), quality.end(), quality.begin(), ::tolower);
            stageOf(vm, self).quality = quality;
            return Val();
        })
        .property("displayState", NATIVE() { return text(stageOf(vm, self).displayState); }, NATIVE() {
            stageOf(vm, self).displayState = vm.toString(args[0]);
            return Val();
        })
        .property("color", NATIVE() { return number(stageOf(vm, self).color); }, NATIVE() {
            display::Stage& stage = stageOf(vm, self);
            stage.color = args[0].toUint32() & 0xffffff;
            stage.invalidate();
            return Val();
        })
        .property("showDefaultContextMenu", NATIVE() { return boolean(stageOf(vm, self).showDefaultContextMenu); },
            NATIVE() {
                stageOf(vm, self).showDefaultContextMenu = args[0].toBoolean();
                return Val();
            })
        .property("stageFocusRect", NATIVE() { return boolean(stageOf(vm, self).stageFocusRect); }, NATIVE() {
            stageOf(vm, self).stageFocusRect = args[0].toBoolean();
            return Val();
        })
        .property("mouseChildren", NATIVE() { return boolean(stageOf(vm, self).mouseChildren); }, NATIVE() {
            stageOf(vm, self).mouseChildren = args[0].toBoolean();
            return Val();
        })
        .getter("nativeWindow", NATIVE() { return Val::null(); })
        .getter("softKeyboardRect", NATIVE(this) { return newRectangle(display::Rect()); })
        .getter("orientation", NATIVE() { return text("default"); })
        .property("colorCorrection", NATIVE() { return text("default"); }, ignore)
        .getter("colorCorrectionSupport", NATIVE() { return text("unsupported"); })
        .getter("wmodeGPU", NATIVE() { return boolean(false); })
        .getter("stage3Ds", NATIVE() { return vm.newArray(); })
        .method("invalidate", NATIVE() {
            stageOf(vm, self).invalidate();
            return Val();
        })
        .method("isFocusInaccessible", NATIVE() { return boolean(false); })
        .define();

    // Graphics.
    const auto draw = [](void (display::Graphics::*op)(double, double)) {
        return NATIVE(op)
        {
            (graphics(vm, self).*op)(numberArg(vm, args, 0), numberArg(vm, args, 1));
            return Val();
        };
    };
    vm.defineNativeClass("flash.display.Graphics")
        .final()
        .constructor(NATIVE() {
            if (!self.host<GraphicsHost>())
                vm.throwError("ArgumentError", "Error #2012: Graphics class cannot be instantiated.", 2012);
            return Val();
        })
        .method("clear", NATIVE() {
            graphics(vm, self).clear();
            return Val();
        })
        .method("beginFill", NATIVE() {
            graphics(vm, self).beginFill(colorArg(args, 0) & 0xffffff, numberArg(vm, args, 1, 1.0));
            return Val();
        }, 1)
        .method("beginGradientFill", NATIVE(this) {
            graphics(vm, self).beginGradientFill(gradientOf(*this, vm, args, 0));
            return Val();
        }, 4)
        .method("beginBitmapFill", NATIVE(this) {
            graphics(vm, self).beginBitmapFill(bitmapDataOf(args[0]),
                args[1].isObject() ? matrixOf(args[1]) : display::Matrix(), booleanArg(args, 2, true), booleanArg(args, 3));
            return Val();
        }, 1)
        .method("endFill", NATIVE() {
            graphics(vm, self).endFill();
            return Val();
        })
        .method("lineStyle", NATIVE() {
            const double thickness = given(args, 0) && !args[0].isNull() ? vm.toNumber(args[0]) : std::nan("");
            const std::string caps = stringArg(vm, args, 5, "round");
            const std::string joints = stringArg(vm, args, 6, "round");
            graphics(vm, self).lineStyle(thickness, colorArg(args, 1) & 0xffffff, numberArg(vm, args, 2, 1.0),
                booleanArg(args, 3), display::LineStyle::scaleModeNamed(stringArg(vm, args, 4, "normal")),
                display::LineStyle::capsNamed(caps), display::LineStyle::jointsNamed(joints), numberArg(vm, args, 7, 3.0));
            return Val();
        })
        .method("lineGradientStyle", NATIVE(this) {
            graphics(vm, self).lineGradientStyle(gradientOf(*this, vm, args, 0));
            return Val();
        }, 4)
        .method("lineBitmapStyle", NATIVE(this) {
            graphics(vm, self).lineBitmapStyle(bitmapDataOf(args[0]),
                args[1].isObject() ? matrixOf(args[1]) : display::Matrix(), booleanArg(args, 2, true), booleanArg(args, 3));
            return Val();
        }, 1)
        .method("moveTo", draw(&display::Graphics::moveTo), 2)
        .method("lineTo", draw(&display::Graphics::lineTo), 2)
        .method("curveTo", NATIVE() {
            graphics(vm, self).curveTo(
                numberArg(vm, args, 0), numberArg(vm, args, 1), numberArg(vm, args, 2), numberArg(vm, args, 3));
            return Val();
        }, 4)
        .method("cubicCurveTo", NATIVE() {
            graphics(vm, self).cubicCurveTo(numberArg(vm, args, 0), numberArg(vm, args, 1), numberArg(vm, args, 2),
                numberArg(vm, args, 3), numberArg(vm, args, 4), numberArg(vm, args, 5));
            return Val();
        }, 6)
        .method("drawRect", NATIVE() {
            graphics(vm, self).drawRect(
                numberArg(vm, args, 0), numberArg(vm, args, 1), numberArg(vm, args, 2), numberArg(vm, args, 3));
            return Val();
        }, 4)
        .method("drawRoundRect", NATIVE() {
            graphics(vm, self).drawRoundRect(numberArg(vm, args, 0), numberArg(vm, args, 1), numberArg(vm, args, 2),
                numberArg(vm, args, 3), numberArg(vm, args, 4), numberArg(vm, args, 5, std::nan("")));
            return Val();
        }, 5)
        .method("drawRoundRectComplex", NATIVE() {
            // Each corner its own radius.
            display::Graphics& g = graphics(vm, self);
            const double x = numberArg(vm, args, 0);
            const double y = numberArg(vm, args, 1);
            const double w = numberArg(vm, args, 2);
            const double h = numberArg(vm, args, 3);
            const double most = std::min(w, h) / 2.0;
            const double tl = std::clamp(numberArg(vm, args, 4), 0.0, most);
            const double tr = std::clamp(numberArg(vm, args, 5), 0.0, most);
            const double bl = std::clamp(numberArg(vm, args, 6), 0.0, most);
            const double br = std::clamp(numberArg(vm, args, 7), 0.0, most);
            g.moveTo(x + tl, y);
            g.lineTo(x + w - tr, y);
            g.curveTo(x + w, y, x + w, y + tr);
            g.lineTo(x + w, y + h - br);
            g.curveTo(x + w, y + h, x + w - br, y + h);
            g.lineTo(x + bl, y + h);
            g.curveTo(x, y + h, x, y + h - bl);
            g.lineTo(x, y + tl);
            g.curveTo(x, y, x + tl, y);
            return Val();
        }, 8)
        .method("drawCircle", NATIVE() {
            graphics(vm, self).drawCircle(numberArg(vm, args, 0), numberArg(vm, args, 1), numberArg(vm, args, 2));
            return Val();
        }, 3)
        .method("drawEllipse", NATIVE() {
            graphics(vm, self).drawEllipse(
                numberArg(vm, args, 0), numberArg(vm, args, 1), numberArg(vm, args, 2), numberArg(vm, args, 3));
            return Val();
        }, 4)
        .method("drawPath", NATIVE(this) {
            graphics(vm, self).drawPath(integers(vm, args[0]), numbers(args[1]), stringArg(vm, args, 2) == "nonZero");
            return Val();
        }, 2)
        .method("drawTriangles", NATIVE(this) {
            const std::string culling = stringArg(vm, args, 3, "none");
            graphics(vm, self).drawTriangles(
                numbers(args[0]), integers(vm, args[1]), culling == "positive" ? 1 : culling == "negative" ? 2 : 0);
            return Val();
        }, 1)
        .method("copyFrom", NATIVE() {
            display::Graphics& from = graphics(vm, args[0]);
            graphics(vm, self).copyFrom(from);
            return Val();
        }, 1)
        .method("drawGraphicsData", ignore, 1)
        .method("readGraphicsData", NATIVE() { return vm.newArray(); })
        .define();

    // Loader and LoaderInfo.
    vm.defineNativeClass("flash.display.LoaderInfo")
        .extends("flash.events.EventDispatcher")
        .constructor(NATIVE() {
            if (!self.host<LoaderInfoHost>())
                vm.throwError("ArgumentError", "Error #2012: LoaderInfo class cannot be instantiated.", 2012);
            return Val();
        })
        .getter("url", NATIVE() { return text(hostOf<LoaderInfoHost>(vm, self, "LoaderInfo").url); })
        .getter("loaderURL", NATIVE(this) { return text(hostOf<LoaderInfoHost>(vm, rootLoaderInfo(), "LoaderInfo").url); })
        .getter("contentType", NATIVE() {
            const std::string& type = hostOf<LoaderInfoHost>(vm, self, "LoaderInfo").contentType;
            return type.empty() ? Val::null() : text(type);
        })
        .getter("bytesLoaded", NATIVE() { return number(hostOf<LoaderInfoHost>(vm, self, "LoaderInfo").bytesLoaded); })
        .getter("bytesTotal", NATIVE() { return number(hostOf<LoaderInfoHost>(vm, self, "LoaderInfo").bytesTotal); })
        .getter("width", NATIVE() { return number(hostOf<LoaderInfoHost>(vm, self, "LoaderInfo").width); })
        .getter("height", NATIVE() { return number(hostOf<LoaderInfoHost>(vm, self, "LoaderInfo").height); })
        .getter("content", NATIVE() {
            const Val& content = hostOf<LoaderInfoHost>(vm, self, "LoaderInfo").content;
            return content.isUndefined() ? Val::null() : content;
        })
        .getter("loader", NATIVE() {
            const Val& loader = hostOf<LoaderInfoHost>(vm, self, "LoaderInfo").loader;
            return loader.isUndefined() ? Val::null() : loader;
        })
        .getter("parameters", NATIVE() {
            LoaderInfoHost& host = hostOf<LoaderInfoHost>(vm, self, "LoaderInfo");
            if (host.parameters.isUndefined())
                host.parameters = vm.newObject();
            return host.parameters;
        })
        .getter("frameRate", NATIVE(this) { return number(runtime._frameRate); })
        .getter("actionScriptVersion", NATIVE() { return number(3); })
        .getter("swfVersion", NATIVE() { return number(10); })
        .getter("sameDomain", NATIVE() { return boolean(true); })
        .getter("childAllowsParent", NATIVE() { return boolean(true); })
        .getter("parentAllowsChild", NATIVE() { return boolean(true); })
        .getter("isURLInaccessible", NATIVE() { return boolean(false); })
        .getter("bytes", NATIVE() { return Val::null(); })
        .getter("applicationDomain", NATIVE() {
            return vm.getProperty(vm.findDefinition("flash.system.ApplicationDomain"), "currentDomain");
        })
        .getter("sharedEvents", NATIVE() { return vm.construct(vm.findDefinition("flash.events.EventDispatcher")); })
        .getter("uncaughtErrorEvents", NATIVE() {
            return vm.construct(vm.findDefinition("flash.events.EventDispatcher"));
        })
        .define();

    // A Loader loads an image file (PNG, JPEG, GIF) into a Bitmap it holds.
    const auto loaderOf = [](VM& vm, const Val& self) -> LoaderHost& {
        LoaderHost* host = self.host<LoaderHost>();
        if (!host)
            vm.throwError("TypeError", "Error #1034: Type Coercion failed: Loader expected.", 1034);
        return *host;
    };
    const auto unload = [this](LoaderHost& host) {
        ++host.generation;
        auto* holder = static_cast<display::DisplayObjectContainer*>(host.object());
        holder->removeChildren();
        host.content = Val::null();
        if (LoaderInfoHost* info = host.info.host<LoaderInfoHost>())
        {
            info->content = Val::null();
            info->complete = false;
            info->bytesLoaded = info->bytesTotal = 0;
            info->width = info->height = 0;
        }
    };
    // What a load gives: the image shown, or the error told.
    const auto loaded = [this, unload](const Val& self, int generation, const std::string& url,
                            const MovieRuntime::Response& response) {
        LoaderHost* host = self.host<LoaderHost>();
        if (!host || host->generation != generation)
            return;
        const Val info = host->info;
        LoaderInfoHost* infoHost = info.host<LoaderInfoHost>();
        std::shared_ptr<display::BitmapData> pixels = response.ok ? decodeImage(response.bytes) : nullptr;
        if (!pixels)
        {
            const std::string message = response.ok ? "Error #2124: Loaded file is an unknown type."
                                                    : "Error #2035: URL Not Found. URL: " + url;
            Val event = makeEvent("flash.events.IOErrorEvent", "ioError");
            if (EventHost* eventHost = event.host<EventHost>())
            {
                eventHost->text = message;
                eventHost->errorID = response.ok ? 2124 : 2035;
            }
            // Flash Player stops on an error no one listens for.
            if (!infoHost || !infoHost->dispatcher->hasEventListener("ioError"))
                runtime.tell("Error #2044: Unhandled IOErrorEvent:. text=" + message, nullptr, 0);
            else
                dispatchScriptEvent(*infoHost->dispatcher, event);
            return;
        }
        auto shown = std::make_shared<display::Bitmap>(pixels);
        static_cast<display::DisplayObjectContainer*>(host->object())->addChild(shown);
        host->content = wrap(shown.get());
        if (infoHost)
        {
            infoHost->content = host->content;
            infoHost->url = url;
            infoHost->bytesLoaded = infoHost->bytesTotal = static_cast<double>(response.bytes.size());
            infoHost->width = pixels->width();
            infoHost->height = pixels->height();
            infoHost->complete = true;
            const std::string& bytes = std::string(response.bytes.begin(), response.bytes.begin() +
                    std::min<size_t>(4, response.bytes.size()));
            infoHost->contentType = bytes.compare(0, 4, "\x89PNG") == 0 ? "image/png"
                : bytes.compare(0, 3, "GIF") == 0                     ? "image/gif"
                                                                      : "image/jpeg";
            send(info, "flash.events.Event", "open");
            Val progress = makeEvent("flash.events.ProgressEvent", "progress");
            progress.host<EventHost>()->bytesLoaded = infoHost->bytesLoaded;
            progress.host<EventHost>()->bytesTotal = infoHost->bytesTotal;
            dispatchScriptEvent(*infoHost->dispatcher, progress);
            send(info, "flash.events.Event", "init");
            Val status = makeEvent("flash.events.HTTPStatusEvent", "httpStatus");
            status.host<EventHost>()->status = response.status;
            dispatchScriptEvent(*infoHost->dispatcher, status);
            send(info, "flash.events.Event", "complete");
        }
        (void)unload;
    };
    vm.defineNativeClass("flash.display.Loader")
        .extends("flash.display.DisplayObjectContainer")
        .constructor(NATIVE(this) {
            if (self.host<DisplayHost>())
                return Val();
            auto holder = std::make_shared<display::Sprite>();
            auto host = std::make_shared<LoaderHost>(*this, holder, nullptr);
            self->host = host;
            holder->script = self.ref;
            keep(self, holder);
            host->info = makeLoaderInfo(false);
            hostOf<LoaderInfoHost>(vm, host->info, "LoaderInfo").loader = self;
            return Val();
        })
        .getter("content", NATIVE(loaderOf) {
            const Val& content = loaderOf(vm, self).content;
            return content.isUndefined() ? Val::null() : content;
        })
        .getter("contentLoaderInfo", NATIVE(loaderOf) { return loaderOf(vm, self).info; })
        .method("load", NATIVE(this, loaderOf, unload, loaded) {
            LoaderHost& host = loaderOf(vm, self);
            unload(host);
            MovieRuntime::Request request;
            request.url = args[0].isString() ? args[0].str() : vm.toString(vm.getProperty(args[0], "url"));
            const int generation = host.generation;
            const Val target = self;
            const std::string url = request.url;
            load(request, [loaded, target, generation, url](const MovieRuntime::Response& response) {
                loaded(target, generation, url, response);
            });
            return Val();
        }, 1)
        .method("loadBytes", NATIVE(this, loaderOf, unload, loaded) {
            LoaderHost& host = loaderOf(vm, self);
            unload(host);
            const ByteArrayHost* bytes = byteArrayOf(args[0]);
            MovieRuntime::Response response;
            response.ok = bytes != nullptr;
            if (bytes)
                response.bytes = bytes->bytes;
            const int generation = host.generation;
            const Val target = self;
            post([loaded, target, generation, response]() { loaded(target, generation, std::string(), response); });
            return Val();
        }, 1)
        .method("unload", NATIVE(loaderOf, unload) {
            unload(loaderOf(vm, self));
            return Val();
        })
        .method("unloadAndStop", NATIVE(loaderOf, unload) {
            unload(loaderOf(vm, self));
            return Val();
        })
        .method("close", NATIVE(loaderOf) {
            ++loaderOf(vm, self).generation;
            return Val();
        })
        .define();

    // The constants classes.
    const auto constants = [this](const char* name, std::initializer_list<std::pair<const char*, const char*>> values) {
        NativeClassBuilder builder = vm.defineNativeClass(name);
        for (const auto& [constant, value] : values)
            builder.constant(constant, text(value));
        builder.final().define();
    };
    constants("flash.display.BlendMode",
        {{"ADD", "add"}, {"ALPHA", "alpha"}, {"DARKEN", "darken"}, {"DIFFERENCE", "difference"}, {"ERASE", "erase"},
            {"HARDLIGHT", "hardlight"}, {"INVERT", "invert"}, {"LAYER", "layer"}, {"LIGHTEN", "lighten"},
            {"MULTIPLY", "multiply"}, {"NORMAL", "normal"}, {"OVERLAY", "overlay"}, {"SCREEN", "screen"},
            {"SHADER", "shader"}, {"SUBTRACT", "subtract"}});
    constants("flash.display.CapsStyle", {{"NONE", "none"}, {"ROUND", "round"}, {"SQUARE", "square"}});
    constants("flash.display.JointStyle", {{"BEVEL", "bevel"}, {"MITER", "miter"}, {"ROUND", "round"}});
    constants("flash.display.LineScaleMode",
        {{"HORIZONTAL", "horizontal"}, {"NONE", "none"}, {"NORMAL", "normal"}, {"VERTICAL", "vertical"}});
    constants("flash.display.GradientType", {{"LINEAR", "linear"}, {"RADIAL", "radial"}});
    constants("flash.display.SpreadMethod", {{"PAD", "pad"}, {"REFLECT", "reflect"}, {"REPEAT", "repeat"}});
    constants("flash.display.InterpolationMethod", {{"LINEAR_RGB", "linearRGB"}, {"RGB", "rgb"}});
    constants("flash.display.PixelSnapping", {{"ALWAYS", "always"}, {"AUTO", "auto"}, {"NEVER", "never"}});
    constants("flash.display.StageScaleMode",
        {{"EXACT_FIT", "exactFit"}, {"NO_BORDER", "noBorder"}, {"NO_SCALE", "noScale"}, {"SHOW_ALL", "showAll"}});
    constants("flash.display.StageAlign",
        {{"BOTTOM", "B"}, {"BOTTOM_LEFT", "BL"}, {"BOTTOM_RIGHT", "BR"}, {"LEFT", "L"}, {"RIGHT", "R"}, {"TOP", "T"},
            {"TOP_LEFT", "TL"}, {"TOP_RIGHT", "TR"}});
    constants("flash.display.StageQuality",
        {{"BEST", "best"}, {"HIGH", "high"}, {"LOW", "low"}, {"MEDIUM", "medium"}, {"HIGH_8X8", "8x8"},
            {"HIGH_8X8_LINEAR", "8x8linear"}, {"HIGH_16X16", "16x16"}, {"HIGH_16X16_LINEAR", "16x16linear"}});
    constants("flash.display.StageDisplayState",
        {{"FULL_SCREEN", "fullScreen"}, {"FULL_SCREEN_INTERACTIVE", "fullScreenInteractive"}, {"NORMAL", "normal"}});
    constants("flash.display.GraphicsPathWinding", {{"EVEN_ODD", "evenOdd"}, {"NON_ZERO", "nonZero"}});
    constants("flash.display.TriangleCulling", {{"NEGATIVE", "negative"}, {"NONE", "none"}, {"POSITIVE", "positive"}});
    vm.defineNativeClass("flash.display.GraphicsPathCommand")
        .constant("NO_OP", number(0))
        .constant("MOVE_TO", number(1))
        .constant("LINE_TO", number(2))
        .constant("CURVE_TO", number(3))
        .constant("WIDE_MOVE_TO", number(4))
        .constant("WIDE_LINE_TO", number(5))
        .constant("CUBIC_CURVE_TO", number(6))
        .final()
        .define();
}

Val Bindings::makeLoaderInfo(bool root)
{
    auto host = std::make_shared<LoaderInfoHost>();
    host->dispatcher = std::make_shared<display::EventDispatcher>();
    if (root)
    {
        host->url = runtime.url;
        host->contentType = "application/x-shockwave-flash";
        host->width = runtime._stage->stageWidth();
        host->height = runtime._stage->stageHeight();
        host->bytesLoaded = host->bytesTotal = 1;
        host->complete = true;
        if (runtime._root)
            host->content = Val(runtime._root->object);
    }
    Val made = instance("flash.display.LoaderInfo", host);
    host->dispatcher->script = made.ref;
    return made;
}

} // namespace script
} // namespace play
} // namespace fla
