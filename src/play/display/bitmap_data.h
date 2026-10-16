#pragma once

#include "blend.h"
#include "geom.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fla {
namespace display {

class DisplayObject;
struct BitmapFilter;

/// The tables that premultiply and unpremultiply a channel by an alpha:
/// [alpha][channel]. Rounded as Flash Player rounds them, so a color with
/// alpha comes back from a BitmapData a little off, as it does there.
struct AlphaTables
{
    uint8_t premultiply[256][256];
    uint8_t unpremultiply[256][256];
};
const AlphaTables& alphaTables();

/// Premultiplied ARGB from unpremultiplied, and back.
inline uint32_t premultiplied(uint32_t argb)
{
    const uint32_t a = argb >> 24;
    if (a == 255)
        return argb;
    const auto& t = alphaTables().premultiply[a];
    return (a << 24) | (uint32_t(t[(argb >> 16) & 0xff]) << 16) | (uint32_t(t[(argb >> 8) & 0xff]) << 8) | t[argb & 0xff];
}
inline uint32_t unpremultiplied(uint32_t pixel)
{
    const uint32_t a = pixel >> 24;
    if (a == 255)
        return pixel;
    const auto& t = alphaTables().unpremultiply[a];
    return (a << 24) | (uint32_t(t[(pixel >> 16) & 0xff]) << 16) | (uint32_t(t[(pixel >> 8) & 0xff]) << 8) |
        t[pixel & 0xff];
}

/// flash.display.BitmapData: pixels kept as Flash Player keeps them,
/// premultiplied ARGB (alpha in the top byte, 0xAARRGGBB) a row after
/// another, so getPixel32 after setPixel32 of a partly see-through color
/// gives back what the player would. A BitmapData that is not transparent
/// is opaque throughout. Free of Qt: the same pixels are what a QImage in
/// Format_ARGB32_Premultiplied holds, so whatever draws can wrap them.
class BitmapData : public std::enable_shared_from_this<BitmapData>
{
public:
    /// Flash Player's limits: each side 1 to 8191, at most 16,777,215
    /// pixels. Sizes out of them make an invalid (disposed) one.
    BitmapData(int width, int height, bool transparent = true, uint32_t fillColor = 0xffffffff);
    BitmapData(const BitmapData&) = delete;
    BitmapData& operator=(const BitmapData&) = delete;

    static bool validSize(int width, int height);

    int width() const { return _width; }
    int height() const { return _height; }
    bool transparent() const { return _transparent; }
    Rect rect() const { return Rect(0, 0, _width, _height); }
    /// After dispose (or made at a size out of range): no pixels.
    bool disposed() const { return _pixels.empty(); }

    /// The pixels, premultiplied, \a width to a row. Call changed() after
    /// writing to them directly.
    uint32_t* bits() { return _pixels.data(); }
    const uint32_t* bits() const { return _pixels.data(); }

    /// getPixel: RGB, unpremultiplied; 0 off the edge.
    uint32_t getPixel(int x, int y) const
    {
        if (static_cast<unsigned>(x) >= static_cast<unsigned>(_width) ||
            static_cast<unsigned>(y) >= static_cast<unsigned>(_height))
            return 0;
        return unpremultiplied(_pixels[static_cast<size_t>(y) * _width + x]) & 0xffffff;
    }
    /// getPixel32: ARGB, unpremultiplied; 0 off the edge.
    uint32_t getPixel32(int x, int y) const
    {
        if (static_cast<unsigned>(x) >= static_cast<unsigned>(_width) ||
            static_cast<unsigned>(y) >= static_cast<unsigned>(_height))
            return 0;
        return unpremultiplied(_pixels[static_cast<size_t>(y) * _width + x]);
    }
    /// setPixel: the RGB, keeping the pixel's alpha. Nothing off the edge.
    void setPixel(int x, int y, uint32_t rgb)
    {
        if (static_cast<unsigned>(x) >= static_cast<unsigned>(_width) ||
            static_cast<unsigned>(y) >= static_cast<unsigned>(_height))
            return;
        uint32_t& pixel = _pixels[static_cast<size_t>(y) * _width + x];
        pixel = premultiplied((pixel & 0xff000000) | (rgb & 0xffffff));
        touch();
    }
    /// setPixel32: ARGB, unpremultiplied (alpha ignored when not
    /// transparent). Nothing off the edge.
    void setPixel32(int x, int y, uint32_t argb)
    {
        if (static_cast<unsigned>(x) >= static_cast<unsigned>(_width) ||
            static_cast<unsigned>(y) >= static_cast<unsigned>(_height))
            return;
        _pixels[static_cast<size_t>(y) * _width + x] = _transparent ? premultiplied(argb) : (argb | 0xff000000);
        touch();
    }

    void fillRect(const Rect& rect, uint32_t argb);
    /// copyPixels: \a source's \a sourceRect to \a destPoint, as they are,
    /// or blended over what is there with \a mergeAlpha; \a alphaBitmap's
    /// alpha (from \a alphaPoint) multiplies the source's.
    void copyPixels(const BitmapData& source, const Rect& sourceRect, const Point& destPoint,
        const BitmapData* alphaBitmap = nullptr, const Point& alphaPoint = Point(), bool mergeAlpha = false);
    std::shared_ptr<BitmapData> clone() const;
    void dispose();
    /// While locked, changes are not shown (Bitmaps showing it keep their
    /// picture) until unlock.
    void lock();
    void unlock();
    bool locked() const { return _locks > 0; }
    void colorTransform(const Rect& rect, const ColorTransform& transform);
    /// copyChannel: BitmapDataChannel values (1 red, 2 green, 4 blue, 8
    /// alpha).
    void copyChannel(const BitmapData& source, const Rect& sourceRect, const Point& destPoint, uint32_t sourceChannel,
        uint32_t destChannel);
    /// floodFill: the pixels of the same color as (\a x, \a y) joined to
    /// it, four ways, made \a argb.
    void floodFill(int x, int y, uint32_t argb);
    /// threshold: the pixels of \a source whose (color & \a mask) compares
    /// with (\a threshold & \a mask) by \a operation ("<", "<=", ">", ">=",
    /// "==", "!=") set to \a color, the others \a source's when
    /// \a copySource; how many were set.
    uint32_t threshold(const BitmapData& source, const Rect& sourceRect, const Point& destPoint,
        const std::string& operation, uint32_t threshold, uint32_t color = 0, uint32_t mask = 0xffffffff,
        bool copySource = false);
    /// applyFilter: \a source's \a sourceRect filtered, put at \a destPoint.
    void applyFilter(const BitmapData& source, const Rect& sourceRect, const Point& destPoint, const BitmapFilter& filter);
    /// merge: each channel blended toward \a source's by its multiplier
    /// (0 to 256).
    void merge(const BitmapData& source, const Rect& sourceRect, const Point& destPoint, uint32_t redMultiplier,
        uint32_t greenMultiplier, uint32_t blueMultiplier, uint32_t alphaMultiplier);
    /// noise: random values from \a low to \a high in the channels
    /// \a channelOptions names (or gray), from \a randomSeed.
    void noise(int randomSeed, uint32_t low = 0, uint32_t high = 255, uint32_t channelOptions = 7, bool grayScale = false);
    /// perlinNoise: fractal noise of \a numOctaves, base frequency
    /// 1 / \a baseX by 1 / \a baseY.
    void perlinNoise(double baseX, double baseY, unsigned numOctaves, int randomSeed, bool stitch, bool fractalNoise,
        uint32_t channelOptions = 7, bool grayScale = false, const std::vector<Point>& offsets = {});
    /// paletteMap: each channel's value looked up in its array (null keeps
    /// it), the results added.
    void paletteMap(const BitmapData& source, const Rect& sourceRect, const Point& destPoint,
        const std::vector<uint32_t>* red, const std::vector<uint32_t>* green, const std::vector<uint32_t>* blue,
        const std::vector<uint32_t>* alpha);
    /// scroll: the pixels moved by (\a dx, \a dy); what they leave keeps
    /// its old pixels.
    void scroll(int dx, int dy);
    /// getPixels: the rectangle's pixels as unpremultiplied ARGB, four
    /// bytes each, big-endian (as a ByteArray holds them).
    std::vector<uint8_t> getPixels(const Rect& rect) const;
    /// setPixels: from \a bytes at \a position (moved on past what is
    /// read); false when they ran out first (EOFError).
    bool setPixels(const Rect& rect, const uint8_t* bytes, size_t length, size_t& position);
    std::vector<uint32_t> getVector(const Rect& rect) const;
    void setVector(const Rect& rect, const std::vector<uint32_t>& pixels);
    /// getColorBoundsRect: the box around the pixels whose (color & mask)
    /// is \a color (or is not, without \a findColor).
    Rect getColorBoundsRect(uint32_t mask, uint32_t color, bool findColor = true) const;
    /// compare: 0 when the same, -3 to -6 when they differ in width,
    /// height or both..., else a BitmapData of the differences.
    int compare(const BitmapData& other, std::shared_ptr<BitmapData>& differences) const;

    /// draw: \a source (a display object with its children and effects)
    /// drawn over the pixels through \a matrix, as whatever renders the
    /// movie renders it. Needs a renderer (see display::Backend).
    void draw(const DisplayObject& source, const Matrix& matrix = Matrix(), const ColorTransform* colorTransform = nullptr,
        BlendMode blendMode = BlendMode::Normal, const Rect* clipRect = nullptr, bool smoothing = false);
    /// draw of another BitmapData, which the core does itself.
    void draw(const BitmapData& source, const Matrix& matrix = Matrix(), const ColorTransform* colorTransform = nullptr,
        BlendMode blendMode = BlendMode::Normal, const Rect* clipRect = nullptr, bool smoothing = false);

    /// Bumped by every change shown (unlocked), for whatever draws it.
    uint64_t revision() const { return _revision; }
    /// The pixels were written to directly: the change is shown.
    void changed();

private:
    void touch()
    {
        if (_locks == 0)
            ++_revision;
        else
            _changedWhileLocked = true;
        markDisplayChanged();
    }
    static void markDisplayChanged();
    /// \a rect cut to the pixels, as whole pixels.
    bool clip(const Rect& rect, int& x0, int& y0, int& x1, int& y1) const;

    int _width = 0;
    int _height = 0;
    bool _transparent = true;
    std::vector<uint32_t> _pixels;
    int _locks = 0;
    bool _changedWhileLocked = false;
    uint64_t _revision = 1;
};

} // namespace display
} // namespace fla
