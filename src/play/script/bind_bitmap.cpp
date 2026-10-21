// flash.display.BitmapData and BitmapDataChannel.

#include "native.h"

#include "../../data/bitmap.h"

#include <algorithm>

namespace fla {
namespace play {
namespace script {

namespace {

display::BitmapData& pixels(VM& vm, const Val& self)
{
    BitmapDataHost& host = hostOf<BitmapDataHost>(vm, self, "BitmapData");
    if (!host.data || host.data->disposed())
        vm.throwError("ArgumentError", "Error #2015: Invalid BitmapData.", 2015);
    return *host.data;
}

/// An argument that must be a BitmapData.
display::BitmapData& sourceOf(Bindings& b, VM& vm, const Val& value)
{
    std::shared_ptr<display::BitmapData> data = b.bitmapDataOf(value);
    if (!data)
        vm.throwError("TypeError", "Error #2007: Parameter sourceBitmapData must be non-null.", 2007);
    if (data->disposed())
        vm.throwError("ArgumentError", "Error #2015: Invalid BitmapData.", 2015);
    return *data;
}

/// A palette: 256 entries, or none for null.
std::optional<std::vector<uint32_t>> palette(VM& vm, const Val& value)
{
    if (!value.isObject())
        return std::nullopt;
    std::vector<uint32_t> out;
    for (const Val& element : value->elements)
        out.push_back(as3::toUint32(vm.toNumber(element)));
    out.resize(256, 0);
    return out;
}

/// A new Vector.<type>.
Val vectorOf(VM& vm, const std::shared_ptr<as3::ClassInfo>& type, std::vector<Val> elements)
{
    const Val cls = vm.applyType(vm.classValue(vm.builtins().vector.get()), {vm.classValue(type.get())});
    Val made = vm.construct(cls);
    made->elements = std::move(elements);
    return made;
}

} // namespace

Val Bindings::newBitmapData(std::shared_ptr<display::BitmapData> data)
{
    auto host = std::make_shared<BitmapDataHost>();
    host->data = std::move(data);
    return instance("flash.display.BitmapData", host);
}

std::shared_ptr<display::BitmapData> Bindings::bitmapDataOf(const Val& value) const
{
    if (const BitmapDataHost* host = value.host<BitmapDataHost>())
        return host->data;
    // A Bitmap given where its pixels are wanted.
    if (const DisplayHost* host = value.host<DisplayHost>())
        if (host->object() && host->object()->kind() == display::ObjectKind::Bitmap)
            return static_cast<display::Bitmap*>(host->object())->bitmapData();
    return nullptr;
}

void Bindings::defineBitmaps()
{
    vm.defineNativeClass("flash.display.BitmapDataChannel")
        .constant("RED", number(1))
        .constant("GREEN", number(2))
        .constant("BLUE", number(4))
        .constant("ALPHA", number(8))
        .final()
        .define();

    const auto decode = [this](const std::vector<uint8_t>& bytes) { return decodeImage(bytes); };
    vm.defineNativeClass("flash.display.BitmapData")
        .implements("flash.display.IBitmapDrawable")
        .constructor(NATIVE(this, decode) {
            if (self.host<BitmapDataHost>())
                return Val();
            auto host = std::make_shared<BitmapDataHost>();
            // A library bitmap's class: its pixels.
            if (const Bitmap* linked = bitmapOf(self->cls.get()))
                host->data = libraryBitmap(*linked, decode);
            if (!host->data)
            {
                host->data = std::make_shared<display::BitmapData>(
                    intArg(args, 0), intArg(args, 1), booleanArg(args, 2, true), uintArg(args, 3, 0xffffffff));
                if (host->data->disposed())
                    vm.throwError("ArgumentError", "Error #2015: Invalid BitmapData.", 2015);
            }
            self->host = host;
            return Val();
        })
        .getter("width", NATIVE() { return number(pixels(vm, self).width()); })
        .getter("height", NATIVE() { return number(pixels(vm, self).height()); })
        .getter("transparent", NATIVE() { return boolean(pixels(vm, self).transparent()); })
        .getter("rect", NATIVE(this) { return newRectangle(pixels(vm, self).rect()); })
        .method("getPixel", NATIVE() {
            return number(pixels(vm, self).getPixel(intArg(args, 0), intArg(args, 1)));
        }, 2)
        .method("getPixel32", NATIVE() {
            return number(pixels(vm, self).getPixel32(intArg(args, 0), intArg(args, 1)));
        }, 2)
        .method("setPixel", NATIVE() {
            pixels(vm, self).setPixel(intArg(args, 0), intArg(args, 1), uintArg(args, 2));
            return Val();
        }, 3)
        .method("setPixel32", NATIVE() {
            pixels(vm, self).setPixel32(intArg(args, 0), intArg(args, 1), uintArg(args, 2));
            return Val();
        }, 3)
        .method("fillRect", NATIVE(this) {
            pixels(vm, self).fillRect(rectOf(args[0]), uintArg(args, 1));
            return Val();
        }, 2)
        .method("copyPixels", NATIVE(this) {
            display::BitmapData& target = pixels(vm, self);
            const display::BitmapData& source = sourceOf(*this, vm, args[0]);
            std::shared_ptr<display::BitmapData> alpha = bitmapDataOf(args[3]);
            target.copyPixels(source, rectOf(args[1]), pointOf(args[2]), alpha.get(),
                args[4].isObject() ? pointOf(args[4]) : display::Point(), booleanArg(args, 5));
            return Val();
        }, 3)
        .method("copyPixelsToByteArray", NATIVE(this) {
            const std::vector<uint8_t> bytes = pixels(vm, self).getPixels(rectOf(args[0]));
            ByteArrayHost* out = byteArrayOf(args[1]);
            if (!out)
                vm.throwError("TypeError", "Error #2007: Parameter data must be non-null.", 2007);
            if (out->position + bytes.size() > out->bytes.size())
                out->bytes.resize(out->position + bytes.size());
            std::copy(bytes.begin(), bytes.end(), out->bytes.begin() + static_cast<std::ptrdiff_t>(out->position));
            out->position += bytes.size();
            return Val();
        }, 2)
        .method("clone", NATIVE(this) { return newBitmapData(pixels(vm, self).clone()); })
        .method("dispose", NATIVE() {
            BitmapDataHost& host = hostOf<BitmapDataHost>(vm, self, "BitmapData");
            if (host.data)
                host.data->dispose();
            return Val();
        })
        .method("lock", NATIVE() {
            pixels(vm, self).lock();
            return Val();
        })
        .method("unlock", NATIVE() {
            pixels(vm, self).unlock();
            return Val();
        })
        .method("colorTransform", NATIVE(this) {
            pixels(vm, self).colorTransform(rectOf(args[0]), colorTransformOf(args[1]));
            return Val();
        }, 2)
        .method("copyChannel", NATIVE(this) {
            pixels(vm, self).copyChannel(sourceOf(*this, vm, args[0]), rectOf(args[1]), pointOf(args[2]),
                uintArg(args, 3), uintArg(args, 4));
            return Val();
        }, 5)
        .method("floodFill", NATIVE() {
            pixels(vm, self).floodFill(intArg(args, 0), intArg(args, 1), uintArg(args, 2));
            return Val();
        }, 3)
        .method("threshold", NATIVE(this) {
            const uint32_t count = pixels(vm, self).threshold(sourceOf(*this, vm, args[0]), rectOf(args[1]),
                pointOf(args[2]), stringArg(vm, args, 3), uintArg(args, 4), uintArg(args, 5),
                uintArg(args, 6, 0xffffffff), booleanArg(args, 7));
            return number(count);
        }, 5)
        .method("applyFilter", NATIVE(this) {
            display::BitmapFilter filter;
            if (!filterOf(args[3], filter))
                vm.throwError("TypeError", "Error #2007: Parameter filter must be non-null.", 2007);
            pixels(vm, self).applyFilter(sourceOf(*this, vm, args[0]), rectOf(args[1]), pointOf(args[2]), filter);
            return Val();
        }, 4)
        .method("generateFilterRect", NATIVE(this) {
            display::BitmapFilter filter;
            if (!filterOf(args[1], filter))
                vm.throwError("TypeError", "Error #2007: Parameter filter must be non-null.", 2007);
            (void)pixels(vm, self);
            return newRectangle(display::filteredRect(rectOf(args[0]), {filter}));
        }, 2)
        .method("merge", NATIVE(this) {
            pixels(vm, self).merge(sourceOf(*this, vm, args[0]), rectOf(args[1]), pointOf(args[2]), uintArg(args, 3),
                uintArg(args, 4), uintArg(args, 5), uintArg(args, 6));
            return Val();
        }, 7)
        .method("noise", NATIVE() {
            pixels(vm, self).noise(
                intArg(args, 0), uintArg(args, 1, 0), uintArg(args, 2, 255), uintArg(args, 3, 7), booleanArg(args, 4));
            return Val();
        }, 1)
        .method("perlinNoise", NATIVE(this) {
            std::vector<display::Point> offsets;
            if (args[8].isObject())
                for (const Val& offset : args[8]->elements)
                    offsets.push_back(pointOf(offset));
            pixels(vm, self).perlinNoise(numberArg(vm, args, 0), numberArg(vm, args, 1),
                static_cast<unsigned>(std::max(0, intArg(args, 2))), intArg(args, 3), booleanArg(args, 4),
                booleanArg(args, 5), uintArg(args, 6, 7), booleanArg(args, 7), offsets);
            return Val();
        }, 6)
        .method("paletteMap", NATIVE(this) {
            const auto red = palette(vm, args[3]);
            const auto green = palette(vm, args[4]);
            const auto blue = palette(vm, args[5]);
            const auto alpha = palette(vm, args[6]);
            pixels(vm, self).paletteMap(sourceOf(*this, vm, args[0]), rectOf(args[1]), pointOf(args[2]),
                red ? &*red : nullptr, green ? &*green : nullptr, blue ? &*blue : nullptr, alpha ? &*alpha : nullptr);
            return Val();
        }, 3)
        .method("pixelDissolve", NATIVE(this) {
            const display::BitmapData& source = sourceOf(*this, vm, args[0]);
            const display::Rect r = rectOf(args[1]);
            const display::Point p = pointOf(args[2]);
            return number(pixels(vm, self).pixelDissolve(source, static_cast<int>(r.x), static_cast<int>(r.y),
                static_cast<int>(r.width), static_cast<int>(r.height), static_cast<int>(p.x), static_cast<int>(p.y),
                intArg(args, 3), intArg(args, 4), uintArg(args, 5)));
        }, 3)
        .method("scroll", NATIVE() {
            pixels(vm, self).scroll(intArg(args, 0), intArg(args, 1));
            return Val();
        }, 2)
        .method("getPixels", NATIVE(this) { return newByteArray(pixels(vm, self).getPixels(rectOf(args[0]))); }, 1)
        .method("setPixels", NATIVE(this) {
            ByteArrayHost* bytes = byteArrayOf(args[1]);
            if (!bytes)
                vm.throwError("TypeError", "Error #2007: Parameter inputByteArray must be non-null.", 2007);
            size_t position = bytes->position;
            const bool whole = pixels(vm, self).setPixels(rectOf(args[0]), bytes->bytes.data(), bytes->bytes.size(), position);
            bytes->position = position;
            if (!whole)
                vm.throwError("flash.errors.EOFError", "Error #2030: End of file was encountered.", 2030);
            return Val();
        }, 2)
        .method("getVector", NATIVE(this) {
            std::vector<Val> out;
            for (const uint32_t pixel : pixels(vm, self).getVector(rectOf(args[0])))
                out.push_back(number(pixel));
            return vectorOf(vm, vm.builtins().uintClass, std::move(out));
        }, 1)
        .method("setVector", NATIVE(this) {
            std::vector<uint32_t> values;
            if (args[1].isObject())
                for (const Val& element : args[1]->elements)
                    values.push_back(as3::toUint32(vm.toNumber(element)));
            pixels(vm, self).setVector(rectOf(args[0]), values);
            return Val();
        }, 2)
        .method("getColorBoundsRect", NATIVE(this) {
            return newRectangle(pixels(vm, self).getColorBoundsRect(uintArg(args, 0), uintArg(args, 1), booleanArg(args, 2, true)));
        }, 2)
        .method("compare", NATIVE(this) {
            display::BitmapData& self_ = pixels(vm, self);
            std::shared_ptr<display::BitmapData> other = bitmapDataOf(args[0]);
            if (!other)
                vm.throwError("TypeError", "Error #2007: Parameter otherBitmapData must be non-null.", 2007);
            std::shared_ptr<display::BitmapData> differences;
            const int result = self_.compare(*other, differences);
            return differences ? newBitmapData(differences) : number(result);
        }, 1)
        .method("histogram", NATIVE(this) {
            display::BitmapData& data = pixels(vm, self);
            const display::Rect area = args[0].isObject() ? rectOf(args[0]) : data.rect();
            std::vector<double> counts[4];
            for (auto& count : counts)
                count.assign(256, 0.0);
            const int x0 = std::max(0, static_cast<int>(area.x));
            const int y0 = std::max(0, static_cast<int>(area.y));
            const int x1 = std::min(data.width(), static_cast<int>(area.right()));
            const int y1 = std::min(data.height(), static_cast<int>(area.bottom()));
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                {
                    const uint32_t pixel = data.getPixel32(x, y);
                    counts[0][(pixel >> 16) & 0xff] += 1;
                    counts[1][(pixel >> 8) & 0xff] += 1;
                    counts[2][pixel & 0xff] += 1;
                    counts[3][pixel >> 24] += 1;
                }
            std::vector<Val> channels;
            for (const auto& count : counts)
            {
                std::vector<Val> values;
                for (const double value : count)
                    values.push_back(number(value));
                channels.push_back(vectorOf(vm, vm.builtins().number, std::move(values)));
            }
            return vectorOf(vm, vm.builtins().object, std::move(channels));
        })
        .method("hitTest", NATIVE(this) {
            display::BitmapData& data = pixels(vm, self);
            const display::Point first = pointOf(args[0]);
            const uint32_t threshold = uintArg(args, 1);
            const auto solid = [&](const display::BitmapData& d, int x, int y, uint32_t limit) {
                return x >= 0 && y >= 0 && x < d.width() && y < d.height() && (d.getPixel32(x, y) >> 24) >= limit;
            };
            if (std::shared_ptr<display::BitmapData> other = bitmapDataOf(args[2]))
            {
                // Two bitmaps: where they overlap, both solid.
                const display::Point second = args[3].isObject() ? pointOf(args[3]) : display::Point();
                const uint32_t otherThreshold = uintArg(args, 4, 1);
                const int dx = static_cast<int>(second.x - first.x);
                const int dy = static_cast<int>(second.y - first.y);
                for (int y = 0; y < data.height(); ++y)
                    for (int x = 0; x < data.width(); ++x)
                        if (solid(data, x, y, threshold) && solid(*other, x - dx, y - dy, otherThreshold))
                            return boolean(true);
                return boolean(false);
            }
            if (args[2].host<RectangleHost>())
            {
                const display::Rect r = rectOf(args[2]);
                for (int y = static_cast<int>(r.y); y < static_cast<int>(r.bottom()); ++y)
                    for (int x = static_cast<int>(r.x); x < static_cast<int>(r.right()); ++x)
                        if (solid(data, x - static_cast<int>(first.x), y - static_cast<int>(first.y), threshold))
                            return boolean(true);
                return boolean(false);
            }
            const display::Point p = pointOf(args[2]);
            return boolean(solid(data, static_cast<int>(p.x - first.x), static_cast<int>(p.y - first.y), threshold));
        }, 3)
        .method("draw", NATIVE(this) {
            display::BitmapData& target = pixels(vm, self);
            const display::Matrix m = args[1].isObject() ? matrixOf(args[1]) : display::Matrix();
            const display::ColorTransform ct = colorTransformOf(args[2]);
            const display::ColorTransform* color = args[2].isObject() ? &ct : nullptr;
            display::BlendMode mode = display::BlendMode::Normal;
            if (args[3].isString())
                display::blendModeNamed(args[3].str(), mode);
            const display::Rect clip = rectOf(args[4]);
            const display::Rect* clipRect = args[4].isObject() ? &clip : nullptr;
            const bool smoothing = booleanArg(args, 5);
            if (display::DisplayObject* source = displayOf(args[0]))
                target.draw(*source, m, color, mode, clipRect, smoothing);
            else if (std::shared_ptr<display::BitmapData> source = bitmapDataOf(args[0]))
                target.draw(*source, m, color, mode, clipRect, smoothing);
            else
                vm.throwError("TypeError", "Error #2007: Parameter source must be non-null.", 2007);
            return Val();
        }, 1)
        .define();
    // drawWithQuality: draw, the quality aside.
    auto cls = classNamed("flash.display.BitmapData");
    if (const as3::Trait* draw = cls->findTrait("draw"))
    {
        as3::Trait copy = *draw;
        cls->traits["drawWithQuality"] = copy;
    }
}

} // namespace script
} // namespace play
} // namespace fla
