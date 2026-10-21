// ActionScript 2's flash.display.BitmapData: the player's side of the
// library's class (resources/as2/flash_display.as). As the player's own,
// a method given too little, or called on a disposed bitmap, returns a
// negative number rather than throwing: -1, or -2 for a source that is not
// a bitmap, -3 for one disposed, -4 for a rectangle without its four
// numbers.

#include "avm1_player.h"
#include "movie_runtime.h"
#include "../data/bitmap.h"
#include "avm1/avm1_interpreter.h"
#include "bytecode/vm.h"
#include "runtime/value.h"
#include "display/bitmap_data.h"
#include "display/display_object.h"
#include "script/bindings.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace fla {
namespace play {

namespace {

/// A bitmap's pixels, held by an object kept out of sight on it ($bitmap).
struct Pixels : as3::HostObject
{
    std::shared_ptr<display::BitmapData> data;
};

/// A number as a whole one, as the player cuts it: NaN 0, the rest held to
/// an int's range.
int saturated(double value)
{
    if (std::isnan(value))
        return 0;
    return static_cast<int>(std::clamp(std::trunc(value), double(std::numeric_limits<int>::min()),
        double(std::numeric_limits<int>::max())));
}

std::string attributeOf(const std::vector<std::pair<std::string, std::string>>& attributes, const std::string& key)
{
    for (const auto& attribute : attributes)
        if (attribute.first == key)
            return attribute.second;
    return std::string();
}

} // namespace

as3::Val ActionsPlayer::bitmapNatives()
{
    as3::VM& vm = _vm;
    as3::avm1::Interpreter& in = *_interpreter;
    MovieRuntime& runtime = _runtime;
    const int version = in.version();
    const as3::Val natives = vm.newObject();
    const auto define = [&](const char* name, as3::NativeMethod method) {
        natives->properties[name] = vm.makeFunction(name, std::move(method));
    };

    enum class State
    {
        Valid,
        Disposed,
        NotBitmap
    };
    // A value's pixels, and whether they are there to use.
    const auto pixelsOf = [](const as3::Val& value, std::shared_ptr<display::BitmapData>* out = nullptr) {
        if (value.isObject())
            if (const as3::Val* held = value->properties.lookup("$bitmap"))
                if (held->isObject())
                    if (Pixels* pixels = dynamic_cast<Pixels*>((*held)->host.get()))
                    {
                        if (!pixels->data || pixels->data->disposed())
                            return State::Disposed;
                        if (out)
                            *out = pixels->data;
                        return State::Valid;
                    }
        return State::NotBitmap;
    };
    const auto holding = [&vm](std::shared_ptr<display::BitmapData> data) {
        const as3::Val holder = vm.newObject();
        auto pixels = std::make_shared<Pixels>();
        pixels->data = std::move(data);
        holder->host = pixels;
        return holder;
    };
    const auto number = [&in](as3::Args args, size_t i) { return in.toNumber(args[i]); };
    const auto int32 = [&in](as3::Args args, size_t i) { return as3::toInt32(in.toNumber(args[i])); };
    const auto uint32 = [&in](as3::Args args, size_t i) { return as3::toUint32(in.toNumber(args[i])); };
    const auto member = [&in](const as3::Val& object, const char* name) { return in.getMember(object, name); };
    // A rectangle's four numbers, when it has all four (its own or its
    // prototypes').
    const auto rectangle = [&in, member](const as3::Val& object, int& x, int& y, int& width, int& height) {
        for (const char* name : {"x", "y", "width", "height"})
            if (!in.hasMember(object, name))
                return false;
        x = as3::toInt32(in.toNumber(member(object, "x")));
        y = as3::toInt32(in.toNumber(member(object, "y")));
        width = as3::toInt32(in.toNumber(member(object, "width")));
        height = as3::toInt32(in.toNumber(member(object, "height")));
        return true;
    };
    const auto point = [&in, member](const as3::Val& object) {
        return display::Point(saturated(in.toNumber(member(object, "x"))), saturated(in.toNumber(member(object, "y"))));
    };
    // A value stored on the object itself (not an accessor, not a
    // prototype's).
    const auto own = [](const as3::Val& object, const char* name) -> std::optional<as3::Val> {
        if (object.isObject())
            if (const as3::Val* value = object->properties.lookup(name))
                return *value;
        return std::nullopt;
    };
    // A flash.geom.ColorTransform's numbers.
    const auto colorTransform = [own, &in](const as3::Val& value, display::ColorTransform& out) {
        double numbers[8];
        for (int i = 0; i < 8; ++i)
        {
            const std::string name = "$ct" + std::to_string(i);
            const auto field = own(value, name.c_str());
            if (!field)
                return false;
            numbers[i] = in.toNumber(*field);
        }
        out.redMultiplier = numbers[0];
        out.greenMultiplier = numbers[1];
        out.blueMultiplier = numbers[2];
        out.alphaMultiplier = numbers[3];
        out.redOffset = numbers[4];
        out.greenOffset = numbers[5];
        out.blueOffset = numbers[6];
        out.alphaOffset = numbers[7];
        return true;
    };
    const auto channels = [&in](as3::Args args, size_t i) -> uint32_t {
        if (i >= args.size())
            return 7;
        return (as3::toUint32(in.toNumber(args[i])) & 0xff) & 15;
    };
    const as3::Val minusOne = as3::Val::number(-1);

    // new BitmapData(width, height, transparent, fillColor): false when
    // given too little or a size the player cannot make.
    define("make", [&in, pixelsOf, holding, uint32, version](as3::VM&, const as3::Val& self, as3::Args args) {
        (void)pixelsOf;
        if (args.size() < 2 || !self.isObject())
            return as3::Val::boolean(false);
        const uint32_t width = uint32(args, 0);
        const uint32_t height = uint32(args, 1);
        const bool transparent = args.size() > 2 ? in.toBoolean(args[2]) : true;
        const uint32_t fill = args.size() > 3 ? uint32(args, 3) : 0xffffffffu;
        bool valid = width > 0 && height > 0;
        if (version <= 9)
            valid = valid && width <= 2880 && height <= 2880;
        else
            valid = valid && width < 0x2000 && height < 0x2000 && uint64_t(width) * height < 0x1000000;
        if (!valid)
            return as3::Val::boolean(false);
        auto data = std::make_shared<display::BitmapData>(int(width), int(height), transparent, fill);
        if (data->disposed())
            return as3::Val::boolean(false);
        self->properties["$bitmap"] = holding(std::move(data));
        return as3::Val::boolean(true);
    });
    define("width", [pixelsOf, minusOne](as3::VM&, const as3::Val& self, as3::Args) {
        std::shared_ptr<display::BitmapData> data;
        return pixelsOf(self, &data) == State::Valid ? as3::Val::number(data->width()) : minusOne;
    });
    define("height", [pixelsOf, minusOne](as3::VM&, const as3::Val& self, as3::Args) {
        std::shared_ptr<display::BitmapData> data;
        return pixelsOf(self, &data) == State::Valid ? as3::Val::number(data->height()) : minusOne;
    });
    define("transparent", [pixelsOf, minusOne](as3::VM&, const as3::Val& self, as3::Args) {
        std::shared_ptr<display::BitmapData> data;
        return pixelsOf(self, &data) == State::Valid ? as3::Val::boolean(data->transparent()) : minusOne;
    });
    define("getPixel", [pixelsOf, int32, minusOne](as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data;
        if (args.size() < 2 || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        return as3::Val::number(int32_t(data->getPixel(int32(args, 0), int32(args, 1))));
    });
    define("getPixel32", [pixelsOf, int32, minusOne](as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data;
        if (args.size() < 2 || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        return as3::Val::number(int32_t(data->getPixel32(int32(args, 0), int32(args, 1))));
    });
    define("setPixel", [pixelsOf, int32, uint32, minusOne](as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data;
        if (args.size() < 3 || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        const int x = int32(args, 0), y = int32(args, 1);
        data->setPixel(x, y, uint32(args, 2));
        return as3::Val::number(0);
    });
    define("setPixel32", [pixelsOf, int32, uint32, minusOne](as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data;
        if (args.size() < 3 || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        const int x = int32(args, 0), y = int32(args, 1);
        data->setPixel32(x, y, uint32(args, 2));
        return as3::Val::number(0);
    });
    define("fillRect", [pixelsOf, rectangle, uint32, minusOne](as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data;
        if (args.size() < 2 || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        int x, y, width, height;
        if (!rectangle(args[0], x, y, width, height))
            return minusOne;
        data->fillRect(display::Rect(x, y, width, height), uint32(args, 1));
        return as3::Val::number(0);
    });
    // The source of a method that copies from one: its pixels, or the
    // number it returns for a value that is not one.
    const auto source = [pixelsOf](const as3::Val& value, std::shared_ptr<display::BitmapData>& out) -> as3::Val {
        switch (pixelsOf(value, &out))
        {
        case State::Valid:
            return as3::Val();
        case State::Disposed:
            return as3::Val::number(-3);
        default:
            return as3::Val::number(-2);
        }
    };
    define("copyPixels", [pixelsOf, source, rectangle, point, &in, minusOne](
                             as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data, from;
        if (args.size() < 3 || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        const as3::Val refused = source(args[0], from);
        if (!refused.isUndefined())
            return refused;
        int x, y, width, height;
        if (!rectangle(args[1], x, y, width, height))
            return as3::Val::number(-4);
        const display::Point to = point(args[2]);
        std::optional<bool> mergeAlpha;
        if (args.size() >= 6)
            mergeAlpha = in.toBoolean(args[5]);
        std::shared_ptr<display::BitmapData> alpha;
        // An alpha bitmap counts only with its point given too.
        if (args.size() >= 5 && pixelsOf(args[3], &alpha) == State::Valid)
            data->copyPixels(*from, display::Rect(x, y, width, height), to, alpha.get(), point(args[4]),
                mergeAlpha.value_or(true));
        else
            data->copyPixels(*from, display::Rect(x, y, width, height), to, nullptr, display::Point(),
                mergeAlpha.value_or(false));
        return as3::Val::number(0);
    });
    define("applyFilter", [this, pixelsOf, source, minusOne](as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data, from;
        if (pixelsOf(self, &data) != State::Valid)
            return minusOne;
        const as3::Val refused = source(args[0], from);
        if (!refused.isUndefined())
            return refused;
        const as3::Val area = args[1];
        display::BitmapFilter filter;
        if (!filterOf(args[3], filter))
            return minusOne;
        const auto whole = [this](const as3::Val& object, const char* name) {
            return static_cast<double>(static_cast<uint32_t>(std::max(0, saturated(_interpreter->toNumber(
                _interpreter->getMember(object, name))))));
        };
        const display::Rect rect(whole(area, "x"), whole(area, "y"), whole(area, "width"), whole(area, "height"));
        const display::Point to(saturated(_interpreter->toNumber(_interpreter->getMember(args[2], "x"))),
            saturated(_interpreter->toNumber(_interpreter->getMember(args[2], "y"))));
        data->applyFilter(*from, rect, to, filter);
        return as3::Val::number(0);
    });
    define("scroll", [pixelsOf, int32, minusOne](as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data;
        if (args.size() < 2 || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        const int x = int32(args, 0), y = int32(args, 1);
        data->scroll(x, y);
        return as3::Val::number(0);
    });
    define("threshold", [pixelsOf, source, rectangle, point, &in, uint32, minusOne](
                            as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data, from;
        if (args.size() < 5 || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        const as3::Val refused = source(args[0], from);
        if (!refused.isUndefined())
            return refused;
        int x, y, width, height;
        if (!rectangle(args[1], x, y, width, height))
            return as3::Val::number(-4);
        const display::Point to = point(args[2]);
        std::string operation = in.toString(args[3]);
        if (operation != "<" && operation != "<=" && operation != ">" && operation != ">=" && operation != "==" &&
            operation != "!=")
            operation = "<";
        const uint32_t threshold = uint32(args, 4);
        const uint32_t color = uint32(args, 5);
        const uint32_t mask = args.size() > 6 ? uint32(args, 6) : 0xffffffffu;
        const bool copySource = in.toBoolean(args[7]);
        return as3::Val::number(data->threshold(
            *from, display::Rect(x, y, width, height), to, operation, threshold, color, mask, copySource));
    });
    define("draw", [this, pixelsOf, colorTransform, &in, member, &runtime, minusOne](
                       as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data;
        if (pixelsOf(self, &data) != State::Valid)
            return minusOne;
        display::Matrix matrix;
        if (args[1].isObject())
        {
            const auto at = [&](const char* name) { return in.toNumber(member(args[1], name)); };
            matrix = display::Matrix(at("a"), at("b"), at("c"), at("d"), at("tx"), at("ty"));
        }
        display::ColorTransform color;
        const bool colored = colorTransform(args[2], color);
        display::BlendMode mode = display::BlendMode::Normal;
        if (args[3].isString())
            display::blendModeNamed(args[3].str(), mode);
        else if (args[3].isNumber() && args[3].num >= 1 && args[3].num <= 14)
            mode = static_cast<display::BlendMode>(int(args[3].num) - 1);
        std::optional<display::Rect> clipRect;
        if (args[4].isObject())
        {
            const auto at = [&](const char* name) { return in.toNumber(member(args[4], name)); };
            clipRect = display::Rect(at("x"), at("y"), at("width"), at("height"));
        }
        const bool smoothing = in.toBoolean(args[5]);
        std::shared_ptr<display::BitmapData> from;
        switch (pixelsOf(args[0], &from))
        {
        case State::Valid:
            data->draw(*from, matrix, colored ? &color : nullptr, mode, clipRect ? &*clipRect : nullptr, smoothing);
            return as3::Val::number(0);
        case State::Disposed:
            return as3::Val::number(-3);
        default:
            break;
        }
        Clip* clip = MovieRuntime::clipOfObject(args[0]);
        if (!clip && args[0].isString())
            clip = drawTarget(args[0].str());
        if (!clip || !clip->display)
            return as3::Val::number(-2);
        (void)runtime;
        data->draw(*clip->display, matrix, colored ? &color : nullptr, mode, clipRect ? &*clipRect : nullptr, smoothing);
        return as3::Val::number(0);
    });
    define("pixelDissolve", [pixelsOf, source, rectangle, point, int32, uint32, minusOne](
                                as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data, from;
        if (args.size() < 5 || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        const as3::Val refused = source(args[0], from);
        if (!refused.isUndefined())
            return refused;
        int x, y, width, height;
        if (!rectangle(args[1], x, y, width, height))
            return as3::Val::number(-4);
        const display::Point to = point(args[2]);
        const int seed = int32(args, 3);
        const int count = int32(args, 4);
        const uint32_t fill = args.size() > 5 ? uint32(args, 5) : 0;
        return as3::Val::number(
            data->pixelDissolve(*from, x, y, width, height, int(to.x), int(to.y), seed, count, fill));
    });
    define("floodFill", [pixelsOf, uint32, minusOne](as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data;
        if (args.size() < 3 || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        const uint32_t x = uint32(args, 0), y = uint32(args, 1), color = uint32(args, 2);
        if (x >= uint32_t(data->width()) || y >= uint32_t(data->height()))
            return as3::Val::number(0);
        const uint32_t replace = data->transparent() ? display::premultiplied(color) : (color | 0xff000000);
        if (data->bits()[size_t(y) * data->width() + x] == replace)
            return as3::Val::number(0);
        data->floodFill(int(x), int(y), color);
        return as3::Val::number(1);
    });
    define("getColorBoundsRect", [&vm, &in, pixelsOf, uint32, minusOne](
                                     as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data;
        if (args.size() < 2 || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        const uint32_t mask = uint32(args, 0), color = uint32(args, 1);
        const bool find = args.size() > 2 ? in.toBoolean(args[2]) : true;
        const display::Rect box = data->getColorBoundsRect(mask, color, find);
        return vm.newArray({as3::Val::number(box.x), as3::Val::number(box.y), as3::Val::number(box.width),
            as3::Val::number(box.height)});
    });
    define("perlinNoise", [pixelsOf, number, int32, channels, &in, member, minusOne](
                              as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data;
        if (args.size() < 6 || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        const double baseX = number(args, 0), baseY = number(args, 1);
        const unsigned octaves = unsigned(std::max(0, int32(args, 2)));
        const int seed = int32(args, 3);
        const bool stitch = in.toBoolean(args[4]), fractal = in.toBoolean(args[5]);
        const uint32_t options = channels(args, 6);
        const bool gray = in.toBoolean(args[7]);
        std::vector<display::Point> offsets;
        for (unsigned i = 0; i < octaves && args[8].isObject(); ++i)
        {
            const as3::Val offset = member(args[8], std::to_string(i).c_str());
            if (offset.isObject())
                offsets.emplace_back(in.toNumber(member(offset, "x")), in.toNumber(member(offset, "y")));
            else
                offsets.emplace_back(0, 0);
        }
        data->perlinNoise(baseX, baseY, octaves, seed, stitch, fractal, options, gray, offsets);
        return as3::Val::number(0);
    });
    define("colorTransform", [pixelsOf, rectangle, colorTransform, minusOne](
                                 as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data;
        if (args.size() < 2 || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        int x, y, width, height;
        if (!rectangle(args[0], x, y, width, height))
            return as3::Val::number(-2);
        display::ColorTransform color;
        if (!colorTransform(args[1], color))
            return as3::Val::number(-3);
        // The far edges as whole pixels, a negative one wrapping past
        // the edge.
        const int x0 = std::max(0, x), y0 = std::max(0, y);
        const int64_t x1 = std::min<int64_t>(uint32_t(x + width), data->width());
        const int64_t y1 = std::min<int64_t>(uint32_t(y + height), data->height());
        if (x1 > x0 && y1 > y0)
            data->colorTransform(display::Rect(x0, y0, double(x1 - x0), double(y1 - y0)), color);
        return minusOne;
    });
    define("hitTest", [pixelsOf, own, &in, int32, minusOne](as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data;
        if (args.size() < 3 || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        const auto whole = [&in](const as3::Val& value) { return as3::toInt32(in.toNumber(value)); };
        const auto firstX = own(args[0], "x"), firstY = own(args[0], "y");
        if (!firstX || !firstY)
            return as3::Val::number(-2);
        const int left = whole(*firstX), top = whole(*firstY);
        const int threshold = std::clamp(int32(args, 1), 0, 255);
        const auto alphaAt = [](const display::BitmapData& d, int x, int y) {
            return int(d.bits()[size_t(y) * d.width() + x] >> 24);
        };
        std::shared_ptr<display::BitmapData> other;
        const State otherState = pixelsOf(args[2], &other);
        if (otherState == State::Disposed)
            return as3::Val::number(-3);
        if (otherState == State::Valid)
        {
            int secondX = 0, secondY = 0;
            const auto x = own(args[3], "x"), y = own(args[3], "y");
            if (x && y)
            {
                secondX = whole(*x);
                secondY = whole(*y);
            }
            else if (args.size() > 3)
                return as3::Val::number(-4);
            const int otherThreshold = std::clamp(int32(args, 4), 0, 255);
            // Where the two overlap, both at least as opaque as asked.
            const int dx = secondX - left, dy = secondY - top;
            const int x0 = std::max(0, dx), y0 = std::max(0, dy);
            const int x1 = std::min(data->width(), dx + other->width());
            const int y1 = std::min(data->height(), dy + other->height());
            for (int px = x0; px < x1; ++px)
                for (int py = y0; py < y1; ++py)
                    if (alphaAt(*data, px, py) >= threshold && alphaAt(*other, px - dx, py - dy) >= otherThreshold)
                        return as3::Val::boolean(true);
            return as3::Val::boolean(false);
        }
        const auto x = own(args[2], "x"), y = own(args[2], "y");
        const auto width = own(args[2], "width"), height = own(args[2], "height");
        if (x && y && !width && !height)
        {
            const int px = whole(*x) - left, py = whole(*y) - top;
            if (px < 0 || py < 0 || px >= data->width() || py >= data->height())
                return as3::Val::boolean(false);
            return as3::Val::boolean(alphaAt(*data, px, py) >= std::max(threshold, 1));
        }
        if (x && y && width && height)
        {
            const int px = whole(*x) - left, py = whole(*y) - top;
            const int64_t x0 = std::max(0, px), y0 = std::max(0, py);
            const int64_t x1 = std::min<int64_t>(data->width(), int64_t(px) + whole(*width));
            const int64_t y1 = std::min<int64_t>(data->height(), int64_t(py) + whole(*height));
            for (int64_t ry = y0; ry < y1; ++ry)
                for (int64_t rx = x0; rx < x1; ++rx)
                    if (alphaAt(*data, int(rx), int(ry)) >= threshold)
                        return as3::Val::boolean(true);
            return as3::Val::boolean(false);
        }
        return as3::Val::number(-3);
    });
    define("paletteMap", [pixelsOf, source, rectangle, point, &in, member, minusOne](
                             as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data, from;
        if (args.size() < 4 || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        const as3::Val refused = source(args[0], from);
        if (!refused.isUndefined())
            return refused;
        int x, y, width, height;
        if (!rectangle(args[1], x, y, width, height))
            return as3::Val::number(-4);
        const display::Point to = point(args[2]);
        std::optional<std::vector<uint32_t>> lists[4];
        for (int i = 0; i < 4; ++i)
        {
            if (!args[3 + i].isObject())
                continue;
            lists[i].emplace(256, 0);
            for (int k = 0; k < 256; ++k)
                (*lists[i])[k] = as3::toUint32(in.toNumber(member(args[3 + i], std::to_string(k).c_str())));
        }
        data->paletteMap(*from, display::Rect(x, y, width, height), to, lists[0] ? &*lists[0] : nullptr,
            lists[1] ? &*lists[1] : nullptr, lists[2] ? &*lists[2] : nullptr, lists[3] ? &*lists[3] : nullptr);
        return minusOne;
    });
    define("merge", [pixelsOf, source, rectangle, point, int32, minusOne](
                        as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data, from;
        if (args.size() < 6 || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        const as3::Val refused = source(args[0], from);
        if (!refused.isUndefined())
            return refused;
        int x, y, width, height;
        if (!rectangle(args[1], x, y, width, height))
            return as3::Val::number(-4);
        const display::Point to = point(args[2]);
        // Each multiplier held to 0 to 256.
        const auto multiplier = [&](size_t i) { return std::clamp(int32(args, i), 0, 256); };
        const int red = multiplier(3), green = multiplier(4), blue = multiplier(5);
        const int alpha = args.size() > 6 ? multiplier(6) : 255;
        data->merge(*from, display::Rect(x, y, width, height), to, uint32_t(red), uint32_t(green), uint32_t(blue),
            uint32_t(alpha));
        return minusOne;
    });
    define("noise", [pixelsOf, int32, channels, &in, minusOne](as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data;
        if (args.empty() || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        const int seed = int32(args, 0);
        const int low = std::clamp(int32(args, 1), 0, 255);
        const int high = args.size() > 2 ? std::clamp(int32(args, 2), 0, 255) : 255;
        const uint32_t options = channels(args, 3);
        const bool gray = in.toBoolean(args[4]);
        data->noise(seed, uint32_t(low), uint32_t(std::max(high, low)), options, gray);
        return as3::Val::number(0);
    });
    define("copyChannel", [pixelsOf, source, rectangle, &in, member, int32, minusOne](
                              as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data, from;
        if (args.size() < 5 || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        const as3::Val refused = source(args[0], from);
        if (!refused.isUndefined())
            return refused;
        int x, y, width, height;
        if (!rectangle(args[1], x, y, width, height))
            return as3::Val::number(-4);
        const int sourceChannel = int32(args, 3), destChannel = int32(args, 4);
        const int toX = as3::toInt32(in.toNumber(member(args[2], "x")));
        const int toY = as3::toInt32(in.toNumber(member(args[2], "y")));
        data->copyChannel(*from, display::Rect(x, y, width, height), display::Point(toX, toY),
            uint32_t(sourceChannel), uint32_t(destChannel));
        return minusOne;
    });
    // clone and compare: an object holding the pixels, which the library
    // makes a bitmap of.
    define("clone", [pixelsOf, holding, minusOne](as3::VM&, const as3::Val& self, as3::Args) {
        std::shared_ptr<display::BitmapData> data;
        if (pixelsOf(self, &data) != State::Valid)
            return minusOne;
        return holding(data->clone());
    });
    define("dispose", [pixelsOf, minusOne](as3::VM&, const as3::Val& self, as3::Args) {
        std::shared_ptr<display::BitmapData> data;
        if (pixelsOf(self, &data) != State::Valid)
            return minusOne;
        data->dispose();
        return as3::Val();
    });
    define("generateFilterRect", [pixelsOf, minusOne](as3::VM&, const as3::Val& self, as3::Args) {
        return pixelsOf(self) == State::Valid ? as3::Val() : minusOne;
    });
    define("compare", [pixelsOf, holding, minusOne](as3::VM&, const as3::Val& self, as3::Args args) {
        std::shared_ptr<display::BitmapData> data, other;
        if (args.empty() || pixelsOf(self, &data) != State::Valid)
            return minusOne;
        if (pixelsOf(args[0], &other) != State::Valid)
            return as3::Val::number(-2);
        if (data->width() != other->width())
            return as3::Val::number(-3);
        if (data->height() != other->height())
            return as3::Val::number(-4);
        std::shared_ptr<display::BitmapData> differences;
        data->compare(*other, differences);
        return differences ? holding(differences) : as3::Val::number(0);
    });
    // BitmapData.loadBitmap(name): a library bitmap's pixels by its
    // linkage name.
    define("loadBitmap", [&runtime, &in, holding](as3::VM&, const as3::Val&, as3::Args args) {
        const std::string name = in.toString(args[0]);
        for (const Bitmap* bitmap : runtime._bitmaps)
        {
            if (!bitmap || attributeOf(bitmap->extraAttributes, "linkageIdentifier") != name)
                continue;
            const auto decode = [&runtime](const std::vector<uint8_t>& bytes) -> std::shared_ptr<display::BitmapData> {
                int width = 0, height = 0;
                std::vector<uint32_t> pixels;
                if (script::decodePng(bytes, width, height, pixels))
                {
                    for (uint32_t& pixel : pixels)
                        pixel = display::premultiplied(pixel);
                }
                else if (!runtime.decodeImage || !runtime.decodeImage(bytes, width, height, pixels))
                    return nullptr;
                auto data = std::make_shared<display::BitmapData>(width, height, true, 0);
                if (data->disposed() || pixels.size() < size_t(width) * height)
                    return nullptr;
                std::copy(pixels.begin(), pixels.begin() + size_t(width) * height, data->bits());
                data->changed();
                return data;
            };
            if (std::shared_ptr<display::BitmapData> data = script::libraryBitmap(*bitmap, decode))
                return holding(data);
            return as3::Val();
        }
        return as3::Val();
    });
    return natives;
}

bool ActionsPlayer::filterOf(const as3::Val& value, display::BitmapFilter& out)
{
    (void)value;
    (void)out;
    return false;
}

Clip* ActionsPlayer::drawTarget(const std::string& path)
{
    // A target as BitmapData.draw takes one: slash, dot or colon paths
    // from the root, but none that starts at a dot or an empty name.
    Clip* root = _runtime._root.get();
    if (!root || path.empty())
        return nullptr;
    Clip* at = root;
    size_t i = 0;
    if (path[0] == '/')
        i = 1;
    if (i < path.size() && (path[i] == '/' || path[i] == '.'))
        return nullptr;
    bool first = true;
    while (i < path.size())
    {
        if (path.compare(i, 2, "..") == 0 && (i + 2 == path.size() || path[i + 2] == '/' || path[i + 2] == ':'))
        {
            at = at->parent;
            if (!at)
                return nullptr;
            i += 2;
            if (i < path.size() && path[i] == '/')
                ++i;
            first = false;
            continue;
        }
        const size_t stop = path.find_first_of("/.:", i);
        const std::string part = path.substr(i, stop == std::string::npos ? std::string::npos : stop - i);
        i = stop == std::string::npos ? path.size() : stop + 1;
        if (part.empty())
            continue;
        if (part == "_root" || part == "_level0")
            at = root;
        else if (part == "_parent")
            at = at->parent;
        else if (!(part == "this" && first))
            at = at->named(part);
        if (!at)
            return nullptr;
        first = false;
    }
    return at;
}

} // namespace play
} // namespace fla
