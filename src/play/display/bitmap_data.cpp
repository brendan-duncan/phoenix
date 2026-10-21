#include "bitmap_data.h"

#include "backend.h"
#include "bitmap_filter.h"
#include "display_object.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace fla {
namespace display {

namespace {

/// For each alpha, 65536 times what undoes it: the factors that give back
/// the player's own unmultiplied colors, found by trying every one.
const uint32_t unmultiplyFactors[256] = {
    0, 16678912, 8339456, 5559638, 4169728, 3335783, 2779819, 2386603, 2086230, 1855488,
    1667892, 1518251, 1391151, 1285234, 1193302, 1111928, 1043895, 981113, 927744, 879275,
    834621, 795535, 759126, 726358, 695839, 668183, 642538, 618737, 596651, 576171,
    555964, 538706, 522104, 506319, 490557, 477321, 464038, 451353, 439544, 428244,
    417582, 407500, 397768, 388535, 379630, 371117, 363179, 355235, 348050, 340965,
    334052, 327038, 321269, 315077, 309159, 303586, 298189, 293092, 287981, 283080,
    278251, 273892, 269268, 265179, 261087, 256971, 253160, 249322, 245508, 242164,
    238575, 235245, 231859, 228848, 225785, 222712, 219616, 216827, 213985, 211432,
    208835, 206075, 203750, 201196, 198895, 196223, 194301, 191987, 189686, 187636,
    185559, 183426, 181453, 179444, 177638, 175855, 174054, 171948, 170489, 168695,
    166889, 165365, 163519, 162045, 160508, 158970, 157429, 156150, 154610, 153081,
    151803, 150511, 148986, 147709, 146420, 145116, 143868, 142586, 141545, 140277,
    139194, 137957, 136954, 135676, 134652, 133621, 132604, 131577, 130552, 129527,
    128508, 127476, 126451, 125432, 124670, 123645, 122818, 121847, 121082, 120060,
    119288, 118263, 117502, 116720, 115967, 115195, 114424, 113655, 112893, 112125,
    111356, 110563, 109811, 109048, 108287, 107766, 107004, 106236, 105724, 104953,
    104434, 103676, 102904, 102375, 101879, 101119, 100604, 99834, 99321, 98813,
    98112, 97533, 97019, 96509, 95994, 95486, 94713, 94185, 93689, 93179,
    92667, 92149, 91643, 91129, 90621, 90068, 89597, 89342, 88829, 88318,
    87804, 87294, 87034, 86523, 85994, 85499, 85245, 84732, 84222, 83956,
    83450, 82937, 82685, 82173, 81840, 81405, 80889, 80638, 80127, 79862,
    79354, 79103, 78590, 78332, 78077, 77565, 77308, 76795, 76541, 76284,
    75766, 75518, 75262, 74748, 74493, 74238, 73691, 73470, 73214, 72959,
    72447, 72189, 71935, 71671, 71166, 70911, 70651, 70399, 70140, 69886,
    69615, 69116, 68861, 68603, 68350, 68093, 67839, 67576, 67326, 67070,
    66813, 66556, 66302, 66046, 65791, 65408,
};

AlphaTables makeTables()
{
    AlphaTables tables;
    for (int a = 0; a < 256; ++a)
        for (int c = 0; c < 256; ++c)
        {
            tables.premultiply[a][c] = static_cast<uint8_t>((c * a + 127) / 255);
            tables.unpremultiply[a][c] =
                static_cast<uint8_t>(std::min<uint32_t>(255, (c * unmultiplyFactors[a] + 0x8000) >> 16));
        }
    return tables;
}

/// The Park-Miller generator Flash Player's noise is made from.
struct Random
{
    /// A seed of 0 or less starts from 1 more than its size.
    explicit Random(int seed)
        : state(seed <= 0 ? static_cast<uint32_t>(1 - static_cast<int64_t>(seed)) : static_cast<uint32_t>(seed))
    {
    }
    uint32_t next()
    {
        state = static_cast<uint32_t>((static_cast<uint64_t>(state) * 16807u) % 2147483647u);
        return state;
    }
    uint32_t state;
};

uint32_t channelOf(uint32_t pixel, uint32_t channel)
{
    switch (channel)
    {
    case 1: return (pixel >> 16) & 0xff;
    case 2: return (pixel >> 8) & 0xff;
    case 4: return pixel & 0xff;
    case 8: return pixel >> 24;
    default: return 0;
    }
}

uint32_t withChannel(uint32_t pixel, uint32_t channel, uint32_t value)
{
    switch (channel)
    {
    case 1: return (pixel & 0xff00ffff) | (value << 16);
    case 2: return (pixel & 0xffff00ff) | (value << 8);
    case 4: return (pixel & 0xffffff00) | value;
    case 8: return (pixel & 0x00ffffff) | (value << 24);
    default: return pixel;
    }
}

/// Smooth noise for perlinNoise: a hash of the lattice, faded between.
/// Perlin noise as SVG's feTurbulence reference makes it, which is the
/// player's perlinNoise: gradients and a lattice shuffled by a Park-Miller
/// generator from the seed.
class Turbulence
{
public:
    explicit Turbulence(int64_t seed)
    {
        constexpr int64_t m = 2147483647;
        if (seed <= 0)
            seed = -(seed % (m - 1)) + 1;
        if (seed > m - 1)
            seed = m - 1;
        const auto random = [](int64_t s) {
            int64_t result = 16807 * (s % 127773) - 2836 * (s / 127773);
            if (result <= 0)
                result += m;
            return result;
        };
        for (int k = 0; k < 4; ++k)
            for (int i = 0; i < size; ++i)
            {
                _lattice[i] = i;
                for (int j = 0; j < 2; ++j)
                {
                    seed = random(seed);
                    _gradient[k][i][j] = static_cast<double>((seed % (size + size)) - size) / size;
                }
                const double s = std::sqrt(_gradient[k][i][0] * _gradient[k][i][0] + _gradient[k][i][1] * _gradient[k][i][1]);
                _gradient[k][i][0] /= s;
                _gradient[k][i][1] /= s;
            }
        for (int i = size - 1; i > 0; --i)
        {
            const int k = _lattice[i];
            seed = random(seed);
            const int j = static_cast<int>(seed % size);
            _lattice[i] = _lattice[j];
            _lattice[j] = k;
        }
        for (int i = 0; i < size + 2; ++i)
        {
            _lattice[size + i] = _lattice[i];
            for (int k = 0; k < 4; ++k)
                for (int j = 0; j < 2; ++j)
                    _gradient[k][size + i][j] = _gradient[k][i][j];
        }
    }

    /// The noise of \a channel at (\a x, \a y) on the page, \a octaves
    /// of it summed (or their sizes, for turbulence), tiling over \a width
    /// by \a height when stitched.
    double at(int channel, double x, double y, double baseX, double baseY, unsigned octaves, bool fractal, bool stitch,
        double width, double height, const std::vector<Point>& offsets) const
    {
        Stitch info;
        bool stitching = false;
        if (stitch)
        {
            // Frequencies that make whole tiles, so the edges meet.
            if (baseX != 0.0)
            {
                const double low = std::floor(width * baseX) / width;
                const double high = std::ceil(width * baseX) / width;
                baseX = baseX / low < high / baseX ? low : high;
            }
            if (baseY != 0.0)
            {
                // As the player has it: the low frequency from the width's.
                const double low = std::floor(height * baseX) / height;
                const double high = std::ceil(height * baseY) / height;
                baseY = baseY / low < high / baseY ? low : high;
            }
            info.width = wholePart(width * baseX + 0.5);
            info.height = wholePart(height * baseY + 0.5);
            info.wrapX = perlinN + info.width;
            info.wrapY = perlinN + info.height;
            stitching = true;
        }
        double sum = 0.0, ratio = 1.0;
        for (unsigned octave = 0; octave < octaves; ++octave)
        {
            const Point offset = octave < offsets.size() ? offsets[octave] : Point();
            const double n = noise(channel, (x + offset.x) * baseX * ratio, (y + offset.y) * baseY * ratio,
                stitching ? &info : nullptr);
            sum += (fractal ? n : std::fabs(n)) / ratio;
            ratio *= 2.0;
            if (stitching)
            {
                info.width *= 2;
                info.wrapX = 2 * info.wrapX - perlinN;
                info.height *= 2;
                info.wrapY = 2 * info.wrapY - perlinN;
            }
        }
        return sum;
    }

private:
    static constexpr int size = 0x100;
    static constexpr int mask = 0xff;
    static constexpr int perlinN = 0x1000;

    struct Stitch
    {
        int width = 0, height = 0, wrapX = 0, wrapY = 0;
    };

    /// A number cut to a whole one toward 0, held to an int's range.
    static int wholePart(double value)
    {
        if (std::isnan(value))
            return 0;
        return static_cast<int>(std::clamp(value, -2147483648.0, 2147483647.0));
    }

    double noise(int channel, double vx, double vy, const Stitch* stitch) const
    {
        double t = vx + perlinN;
        int bx0 = wholePart(t), bx1 = bx0 + 1;
        const double rx0 = t - wholePart(t), rx1 = rx0 - 1.0;
        t = vy + perlinN;
        int by0 = wholePart(t), by1 = by0 + 1;
        const double ry0 = t - wholePart(t), ry1 = ry0 - 1.0;
        if (stitch)
        {
            if (bx0 >= stitch->wrapX)
                bx0 -= stitch->width;
            if (bx1 >= stitch->wrapX)
                bx1 -= stitch->width;
            if (by0 >= stitch->wrapY)
                by0 -= stitch->height;
            if (by1 >= stitch->wrapY)
                by1 -= stitch->height;
        }
        bx0 &= mask;
        bx1 &= mask;
        by0 &= mask;
        by1 &= mask;
        const int i = _lattice[bx0], j = _lattice[bx1];
        const int b00 = _lattice[i + by0], b10 = _lattice[j + by0];
        const int b01 = _lattice[i + by1], b11 = _lattice[j + by1];
        const double sx = rx0 * rx0 * (3.0 - 2.0 * rx0);
        const double sy = ry0 * ry0 * (3.0 - 2.0 * ry0);
        const auto lerp = [](double f, double a, double b) { return a + f * (b - a); };
        const auto& g = _gradient[channel];
        const double a = lerp(sx, rx0 * g[b00][0] + ry0 * g[b00][1], rx1 * g[b10][0] + ry0 * g[b10][1]);
        const double b = lerp(sx, rx0 * g[b01][0] + ry1 * g[b01][1], rx1 * g[b11][0] + ry1 * g[b11][1]);
        return lerp(sy, a, b);
    }

    int _lattice[size + size + 2] = {};
    double _gradient[4][size + size + 2][2] = {};
};

} // namespace

const AlphaTables& alphaTables()
{
    static const AlphaTables tables = makeTables();
    return tables;
}

bool BitmapData::validSize(int width, int height)
{
    return width > 0 && height > 0 && width <= 8191 && height <= 8191 &&
        static_cast<int64_t>(width) * height <= 16777215;
}

BitmapData::BitmapData(int width, int height, bool transparent, uint32_t fillColor)
    : _transparent(transparent)
{
    if (!validSize(width, height))
        return;
    _width = width;
    _height = height;
    const uint32_t pixel = transparent ? premultiplied(fillColor) : (fillColor | 0xff000000);
    _pixels.assign(static_cast<size_t>(width) * height, pixel);
}

void BitmapData::markDisplayChanged()
{
    display::markDisplayChanged();
}

void BitmapData::changed()
{
    touch();
}

bool BitmapData::clip(const Rect& rect, int& x0, int& y0, int& x1, int& y1) const
{
    // Flash Player rounds a rectangle's edges to whole pixels.
    x0 = std::max(0, static_cast<int>(std::floor(rect.x + 0.0)));
    y0 = std::max(0, static_cast<int>(std::floor(rect.y + 0.0)));
    x1 = std::min(_width, static_cast<int>(std::floor(rect.x + rect.width)));
    y1 = std::min(_height, static_cast<int>(std::floor(rect.y + rect.height)));
    return !disposed() && x1 > x0 && y1 > y0;
}

void BitmapData::fillRect(const Rect& rect, uint32_t argb)
{
    int x0, y0, x1, y1;
    if (!clip(rect, x0, y0, x1, y1))
        return;
    const uint32_t pixel = _transparent ? premultiplied(argb) : (argb | 0xff000000);
    for (int y = y0; y < y1; ++y)
        std::fill(_pixels.begin() + static_cast<size_t>(y) * _width + x0,
            _pixels.begin() + static_cast<size_t>(y) * _width + x1, pixel);
    touch();
}

namespace {

/// \a source over \a backdrop, both premultiplied, in the 256ths the player
/// blends copied pixels with.
uint32_t blendOver(uint32_t backdrop, uint32_t source)
{
    const uint32_t keep = 256 - (source >> 24);
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8)
        out |= ((((source >> shift) & 0xff) + ((((backdrop >> shift) & 0xff) * keep) >> 8)) & 0xff) << shift;
    return out;
}

} // namespace

void BitmapData::copyPixels(const BitmapData& source, const Rect& sourceRect, const Point& destPoint,
    const BitmapData* alphaBitmap, const Point& alphaPoint, bool mergeAlpha)
{
    if (disposed() || source.disposed())
        return;
    const int64_t sx = static_cast<int64_t>(std::floor(sourceRect.x));
    const int64_t sy = static_cast<int64_t>(std::floor(sourceRect.y));
    const int64_t width = static_cast<int64_t>(std::floor(sourceRect.x + sourceRect.width)) - sx;
    const int64_t height = static_cast<int64_t>(std::floor(sourceRect.y + sourceRect.height)) - sy;
    const int64_t dx = static_cast<int64_t>(std::floor(destPoint.x));
    const int64_t dy = static_cast<int64_t>(std::floor(destPoint.y));
    if (width <= 0 || height <= 0)
        return;
    const int64_t ax = static_cast<int64_t>(std::floor(alphaPoint.x));
    const int64_t ay = static_cast<int64_t>(std::floor(alphaPoint.y));
    if (alphaBitmap && alphaBitmap->disposed())
        alphaBitmap = nullptr;
    if (alphaBitmap && !(alphaBitmap == &source && ax == sx && ay == sy))
    {
        // Through another bitmap's alpha: each pixel unmultiplied, given
        // the alpha the two make, and put over what is there (or in its
        // place, in a transparent bitmap without mergeAlpha).
        bool any = false;
        for (int64_t y = 0; y < height; ++y)
        {
            const int64_t ty = dy + y, fy = sy + y, ay0 = ay + y;
            if (ty < 0 || ty >= _height || fy < 0 || fy >= source._height || ay0 < 0 || ay0 >= alphaBitmap->_height)
                continue;
            for (int64_t x = 0; x < width; ++x)
            {
                const int64_t tx = dx + x, fx = sx + x, ax0 = ax + x;
                if (tx < 0 || tx >= _width || fx < 0 || fx >= source._width || ax0 < 0 || ax0 >= alphaBitmap->_width)
                    continue;
                const uint32_t from = source._pixels[static_cast<size_t>(fy) * source._width + fx];
                const uint32_t sourceAlpha = from >> 24;
                uint32_t alpha = 255;
                if (alphaBitmap->_transparent)
                {
                    const uint32_t a = alphaBitmap->_pixels[static_cast<size_t>(ay0) * alphaBitmap->_width + ax0] >> 24;
                    alpha = !source._transparent ? a : a == 255 ? sourceAlpha : (a * sourceAlpha) >> 8;
                }
                else if (source._transparent)
                    alpha = sourceAlpha;
                uint32_t pixel = alpha << 24;
                for (int shift = 0; shift < 24; shift += 8)
                {
                    const double unmultiplied = ((from >> shift) & 0xff) / (sourceAlpha / 255.0);
                    const uint32_t c = std::isnan(unmultiplied)
                        ? 0
                        : static_cast<uint32_t>(std::clamp(std::round(unmultiplied), 0.0, 255.0));
                    pixel |= ((c * alpha + 127) / 255) << shift;
                }
                uint32_t& out = _pixels[static_cast<size_t>(ty) * _width + tx];
                out = mergeAlpha || !_transparent ? blendOver(out, pixel) : pixel;
                any = true;
            }
        }
        if (any)
            touch();
        return;
    }
    // The part of the rectangle both bitmaps hold.
    const int64_t left = std::max<int64_t>({0, -dx, -sx});
    const int64_t top = std::max<int64_t>({0, -dy, -sy});
    const int64_t right = std::min<int64_t>({width, _width - dx, source._width - sx});
    const int64_t bottom = std::min<int64_t>({height, _height - dy, source._height - sy});
    if (right <= left || bottom <= top)
        return;
    const int fromX = static_cast<int>(sx + left), fromY = static_cast<int>(sy + top);
    const int toX = static_cast<int>(dx + left), toY = static_cast<int>(dy + top);
    const int w = static_cast<int>(right - left), h = static_cast<int>(bottom - top);
    // An opaque source has nothing to blend; a transparent one onto an
    // opaque bitmap always blends.
    const bool blend = source._transparent && (mergeAlpha || !_transparent);
    if (&source == this)
    {
        if (!blend && fromX == toX && fromY == toY)
            return;
        // Copied in place, backward when moving right and down: moving one
        // way across and the other down, the player reads pixels it has
        // already written.
        const bool reverse = toX >= fromX && toY >= fromY;
        for (int i = 0; i < h; ++i)
        {
            const int y = reverse ? h - 1 - i : i;
            uint32_t* to = &_pixels[static_cast<size_t>(toY + y) * _width + toX];
            const uint32_t* from = &_pixels[static_cast<size_t>(fromY + y) * _width + fromX];
            if (!blend)
            {
                std::memmove(to, from, static_cast<size_t>(w) * sizeof(uint32_t));
                continue;
            }
            for (int k = 0; k < w; ++k)
            {
                const int x = reverse ? w - 1 - k : k;
                to[x] = blendOver(to[x], from[x]);
            }
        }
        touch();
        return;
    }
    for (int y = 0; y < h; ++y)
    {
        uint32_t* to = &_pixels[static_cast<size_t>(toY + y) * _width + toX];
        const uint32_t* from = &source._pixels[static_cast<size_t>(fromY + y) * source._width + fromX];
        for (int x = 0; x < w; ++x)
        {
            if (blend)
                to[x] = _transparent ? blendOver(to[x], from[x]) : (blendOver(to[x], from[x]) | 0xff000000);
            else if (_transparent || !source._transparent)
                to[x] = from[x];
            else
                to[x] = unpremultiplied(from[x]) | 0xff000000;
        }
    }
    touch();
}

std::shared_ptr<BitmapData> BitmapData::clone() const
{
    auto copy = std::make_shared<BitmapData>(std::max(1, _width), std::max(1, _height), _transparent, 0);
    copy->_pixels = _pixels;
    copy->_width = _width;
    copy->_height = _height;
    return copy;
}

void BitmapData::dispose()
{
    _pixels.clear();
    _pixels.shrink_to_fit();
    _width = 0;
    _height = 0;
    touch();
}

void BitmapData::lock()
{
    ++_locks;
}

void BitmapData::unlock()
{
    if (_locks > 0 && --_locks == 0 && _changedWhileLocked)
    {
        _changedWhileLocked = false;
        ++_revision;
        markDisplayChanged();
    }
}

void BitmapData::colorTransform(const Rect& rect, const ColorTransform& transform)
{
    int x0, y0, x1, y1;
    if (!clip(rect, x0, y0, x1, y1))
        return;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            uint32_t& pixel = _pixels[static_cast<size_t>(y) * _width + x];
            const uint32_t out = transform.apply(unpremultiplied(pixel));
            pixel = _transparent ? premultiplied(out) : (out | 0xff000000);
        }
    touch();
}

void BitmapData::copyChannel(const BitmapData& source, const Rect& sourceRect, const Point& destPoint,
    uint32_t sourceChannel, uint32_t destChannel)
{
    if (disposed() || source.disposed())
        return;
    int sx0, sy0, sx1, sy1;
    if (!source.clip(sourceRect, sx0, sy0, sx1, sy1))
        return;
    const int dx = static_cast<int>(std::floor(destPoint.x)) - sx0 + (sx0 - static_cast<int>(std::floor(sourceRect.x)));
    const int dy = static_cast<int>(std::floor(destPoint.y)) - sy0 + (sy0 - static_cast<int>(std::floor(sourceRect.y)));
    const std::vector<uint32_t> from = source._pixels;
    for (int y = sy0; y < sy1; ++y)
    {
        const int ty = y + dy;
        if (ty < 0 || ty >= _height)
            continue;
        for (int x = sx0; x < sx1; ++x)
        {
            const int tx = x + dx;
            if (tx < 0 || tx >= _width)
                continue;
            const uint32_t value = channelOf(unpremultiplied(from[static_cast<size_t>(y) * source._width + x]), sourceChannel);
            if (destChannel == 8 && !_transparent)
                continue;
            uint32_t& pixel = _pixels[static_cast<size_t>(ty) * _width + tx];
            const uint32_t changed = withChannel(unpremultiplied(pixel), destChannel, value);
            pixel = _transparent ? premultiplied(changed) : (changed | 0xff000000);
        }
    }
    touch();
}

void BitmapData::floodFill(int x, int y, uint32_t argb)
{
    if (static_cast<unsigned>(x) >= static_cast<unsigned>(_width) ||
        static_cast<unsigned>(y) >= static_cast<unsigned>(_height))
        return;
    const uint32_t fill = _transparent ? premultiplied(argb) : (argb | 0xff000000);
    const uint32_t old = _pixels[static_cast<size_t>(y) * _width + x];
    if (old == fill)
        return;
    // Scanlines: each run of the old color filled whole, the rows above and
    // below it looked at for more.
    std::vector<std::pair<int, int>> stack{{x, y}};
    while (!stack.empty())
    {
        const auto [sx, sy] = stack.back();
        stack.pop_back();
        uint32_t* line = &_pixels[static_cast<size_t>(sy) * _width];
        if (line[sx] != old)
            continue;
        int left = sx;
        while (left > 0 && line[left - 1] == old)
            --left;
        int right = sx;
        while (right + 1 < _width && line[right + 1] == old)
            ++right;
        for (int i = left; i <= right; ++i)
            line[i] = fill;
        for (const int ny : {sy - 1, sy + 1})
        {
            if (ny < 0 || ny >= _height)
                continue;
            const uint32_t* next = &_pixels[static_cast<size_t>(ny) * _width];
            bool inRun = false;
            for (int i = left; i <= right; ++i)
            {
                if (next[i] == old)
                {
                    if (!inRun)
                        stack.push_back({i, ny});
                    inRun = true;
                }
                else
                    inRun = false;
            }
        }
    }
    touch();
}

uint32_t BitmapData::threshold(const BitmapData& source, const Rect& sourceRect, const Point& destPoint,
    const std::string& operation, uint32_t thresholdValue, uint32_t color, uint32_t mask, bool copySource)
{
    if (disposed() || source.disposed())
        return 0;
    int sx0, sy0, sx1, sy1;
    if (!source.clip(sourceRect, sx0, sy0, sx1, sy1))
        return 0;
    const int dx = static_cast<int>(std::floor(destPoint.x)) - static_cast<int>(std::floor(sourceRect.x));
    const int dy = static_cast<int>(std::floor(destPoint.y)) - static_cast<int>(std::floor(sourceRect.y));
    // Compared as the pixels are kept, premultiplied; and as the player
    // does, set (or copied) with their alpha even in an opaque bitmap.
    const uint32_t wanted = thresholdValue & mask;
    const std::vector<uint32_t> from = source._pixels;
    uint32_t count = 0;
    const uint32_t setTo = premultiplied(color);
    for (int y = sy0; y < sy1; ++y)
    {
        const int ty = y + dy;
        if (ty < 0 || ty >= _height)
            continue;
        for (int x = sx0; x < sx1; ++x)
        {
            const int tx = x + dx;
            if (tx < 0 || tx >= _width)
                continue;
            const uint32_t raw = from[static_cast<size_t>(y) * source._width + x];
            const uint32_t value = raw & mask;
            bool hit = false;
            if (operation == "<")
                hit = value < wanted;
            else if (operation == "<=")
                hit = value <= wanted;
            else if (operation == ">")
                hit = value > wanted;
            else if (operation == ">=")
                hit = value >= wanted;
            else if (operation == "==")
                hit = value == wanted;
            else if (operation == "!=")
                hit = value != wanted;
            uint32_t& pixel = _pixels[static_cast<size_t>(ty) * _width + tx];
            if (hit)
            {
                pixel = setTo;
                ++count;
            }
            else if (copySource)
                pixel = raw;
        }
    }
    touch();
    return count;
}

void BitmapData::applyFilter(const BitmapData& source, const Rect& sourceRect, const Point& destPoint,
    const BitmapFilter& filter)
{
    if (disposed() || source.disposed())
        return;
    if (filter.type == BitmapFilter::Type::ColorMatrix || filter.type == BitmapFilter::Type::Convolution ||
        filter.type == BitmapFilter::Type::Blur || filter.type == BitmapFilter::Type::DisplacementMap)
    {
        // Worked here: the source's rectangle copied out, filtered and put
        // back at the destination.
        int sx0, sy0, sx1, sy1;
        if (!source.clip(sourceRect, sx0, sy0, sx1, sy1))
            return;
        const int w = sx1 - sx0;
        const int h = sy1 - sy0;
        std::vector<uint32_t> work(static_cast<size_t>(w) * h);
        for (int y = 0; y < h; ++y)
            std::memcpy(&work[static_cast<size_t>(y) * w], &source._pixels[static_cast<size_t>(sy0 + y) * source._width + sx0],
                sizeof(uint32_t) * w);
        if (filter.type == BitmapFilter::Type::ColorMatrix)
            applyColorMatrix(work.data(), w, h, w, filter.matrix);
        else if (filter.type == BitmapFilter::Type::Convolution)
            applyConvolution(work.data(), w, h, w, filter);
        else if (filter.type == BitmapFilter::Type::DisplacementMap)
            applyDisplacementMap(work.data(), w, h, w, filter);
        else
            applyBoxBlur(work.data(), w, h, w, filter.blurX, filter.blurY, std::max(1, filter.quality));
        const int dx = static_cast<int>(std::floor(destPoint.x)) + (sx0 - static_cast<int>(std::floor(sourceRect.x)));
        const int dy = static_cast<int>(std::floor(destPoint.y)) + (sy0 - static_cast<int>(std::floor(sourceRect.y)));
        for (int y = 0; y < h; ++y)
        {
            const int ty = dy + y;
            if (ty < 0 || ty >= _height)
                continue;
            for (int x = 0; x < w; ++x)
            {
                const int tx = dx + x;
                if (tx < 0 || tx >= _width)
                    continue;
                const uint32_t pixel = work[static_cast<size_t>(y) * w + x];
                _pixels[static_cast<size_t>(ty) * _width + tx] = _transparent ? pixel : (unpremultiplied(pixel) | 0xff000000);
            }
        }
        touch();
        return;
    }
    if (Backend* renderer = backend())
        if (renderer->applyFilter(*this, source, sourceRect, destPoint, filter))
            touch();
}

void BitmapData::merge(const BitmapData& source, const Rect& sourceRect, const Point& destPoint, uint32_t redMultiplier,
    uint32_t greenMultiplier, uint32_t blueMultiplier, uint32_t alphaMultiplier)
{
    if (disposed() || source.disposed())
        return;
    int sx0, sy0, sx1, sy1;
    if (!source.clip(sourceRect, sx0, sy0, sx1, sy1))
        return;
    const int dx = static_cast<int>(std::floor(destPoint.x)) - static_cast<int>(std::floor(sourceRect.x));
    const int dy = static_cast<int>(std::floor(destPoint.y)) - static_cast<int>(std::floor(sourceRect.y));
    const uint32_t multipliers[4] = {blueMultiplier, greenMultiplier, redMultiplier, alphaMultiplier};
    const std::vector<uint32_t> from = source._pixels;
    for (int y = sy0; y < sy1; ++y)
    {
        const int ty = y + dy;
        if (ty < 0 || ty >= _height)
            continue;
        for (int x = sx0; x < sx1; ++x)
        {
            const int tx = x + dx;
            if (tx < 0 || tx >= _width)
                continue;
            const uint32_t src = unpremultiplied(from[static_cast<size_t>(y) * source._width + x]);
            uint32_t& pixel = _pixels[static_cast<size_t>(ty) * _width + tx];
            const uint32_t dst = unpremultiplied(pixel);
            uint32_t out = 0;
            for (int c = 0; c < 4; ++c)
            {
                const int shift = c * 8;
                const int s = (src >> shift) & 0xff;
                const int d = (dst >> shift) & 0xff;
                const int m = static_cast<int>(std::min<uint32_t>(256, multipliers[c]));
                out |= static_cast<uint32_t>(std::max(0, std::min(255, (s * m + d * (256 - m)) / 256))) << shift;
            }
            pixel = _transparent ? premultiplied(out) : (out | 0xff000000);
        }
    }
    touch();
}

void BitmapData::noise(int randomSeed, uint32_t low, uint32_t high, uint32_t channelOptions, bool grayScale)
{
    if (disposed())
        return;
    low = std::min<uint32_t>(low, 255);
    high = std::min<uint32_t>(high, 255);
    if (high < low)
        std::swap(low, high);
    const uint32_t range = high - low + 1;
    Random random(randomSeed);
    for (uint32_t& pixel : _pixels)
    {
        uint32_t a = 255, r = 0, g = 0, b = 0;
        if (grayScale)
        {
            r = g = b = low + random.next() % range;
        }
        else
        {
            if (channelOptions & 1)
                r = low + random.next() % range;
            if (channelOptions & 2)
                g = low + random.next() % range;
            if (channelOptions & 4)
                b = low + random.next() % range;
        }
        if ((channelOptions & 8) && _transparent)
            a = low + random.next() % range;
        const uint32_t argb = (a << 24) | (r << 16) | (g << 8) | b;
        pixel = _transparent ? premultiplied(argb) : (argb | 0xff000000);
    }
    touch();
}

void BitmapData::perlinNoise(double baseX, double baseY, unsigned numOctaves, int randomSeed, bool stitch,
    bool fractalNoise, uint32_t channelOptions, bool grayScale, const std::vector<Point>& offsets)
{
    if (disposed())
        return;
    const auto turbulence = std::make_unique<Turbulence>(randomSeed);
    const double fx = baseX != 0.0 ? 1.0 / baseX : 0.0;
    const double fy = baseY != 0.0 ? 1.0 / baseY : 0.0;
    const double width = _width, height = _height;
    for (int y = 0; y < _height; ++y)
        for (int x = 0; x < _width; ++x)
        {
            const auto noise = [&](int channel) {
                return turbulence->at(channel, x, y, fx, fy, numOctaves, fractalNoise, stitch, width, height, offsets);
            };
            double values[4] = {-1.0, -1.0, -1.0, 1.0};
            if (grayScale)
            {
                values[0] = values[1] = values[2] = noise(0);
                values[3] = (channelOptions & 8) ? noise(1) : 1.0;
            }
            else
            {
                // The channels asked for take the noise of the next one in
                // turn, the others skipped over.
                int next = 0;
                for (int c = 0; c < 4; ++c)
                    if (channelOptions & (1u << c))
                        values[c] = noise(next++);
            }
            uint32_t bytes[4];
            for (int c = 0; c < 4; ++c)
            {
                // As the player turns noise into a byte: rounded half up
                // before halving, for fractal noise; cut, held to 0 to 255.
                const double v = fractalNoise ? ((values[c] * 255.0 + 255.0) + 0.5) / 2.0 : values[c] * 255.0 + 0.5;
                bytes[c] = std::isnan(v) ? 0 : static_cast<uint32_t>(std::clamp(v, 0.0, 255.0));
            }
            if (!_transparent)
                bytes[3] = 255;
            // Kept as made, not premultiplied, as the player keeps them.
            _pixels[static_cast<size_t>(y) * _width + x] = (bytes[3] << 24) | (bytes[0] << 16) | (bytes[1] << 8) | bytes[2];
        }
    touch();
}

void BitmapData::paletteMap(const BitmapData& source, const Rect& sourceRect, const Point& destPoint,
    const std::vector<uint32_t>* red, const std::vector<uint32_t>* green, const std::vector<uint32_t>* blue,
    const std::vector<uint32_t>* alpha)
{
    if (disposed() || source.disposed())
        return;
    int sx0, sy0, sx1, sy1;
    if (!source.clip(sourceRect, sx0, sy0, sx1, sy1))
        return;
    const int dx = static_cast<int>(std::floor(destPoint.x)) - static_cast<int>(std::floor(sourceRect.x));
    const int dy = static_cast<int>(std::floor(destPoint.y)) - static_cast<int>(std::floor(sourceRect.y));
    const std::vector<uint32_t> from = source._pixels;
    const auto look = [](const std::vector<uint32_t>* table, uint32_t value, uint32_t keep) {
        return table && value < table->size() ? (*table)[value] : keep;
    };
    for (int y = sy0; y < sy1; ++y)
    {
        const int ty = y + dy;
        if (ty < 0 || ty >= _height)
            continue;
        for (int x = sx0; x < sx1; ++x)
        {
            const int tx = x + dx;
            if (tx < 0 || tx >= _width)
                continue;
            const uint32_t src = unpremultiplied(from[static_cast<size_t>(y) * source._width + x]);
            const uint32_t r = (src >> 16) & 0xff, g = (src >> 8) & 0xff, b = src & 0xff, a = src >> 24;
            const uint32_t out = look(red, r, r << 16) + look(green, g, g << 8) + look(blue, b, b) + look(alpha, a, a << 24);
            _pixels[static_cast<size_t>(ty) * _width + tx] = _transparent ? premultiplied(out) : (out | 0xff000000);
        }
    }
    touch();
}

int BitmapData::pixelDissolve(const BitmapData& source, int sourceX, int sourceY, int sourceWidth, int sourceHeight,
    int destX, int destY, int randomSeed, int numPixels, uint32_t fillColor)
{
    // With nothing to dissolve, the seed as it was.
    if (disposed() || source.disposed())
        return 0;
    if (sourceWidth <= 0 || sourceHeight <= 0)
        return randomSeed;
    // The part of the source rectangle both bitmaps hold, in each.
    const int64_t left = std::max<int64_t>({0, -int64_t(destX), -int64_t(sourceX)});
    const int64_t top = std::max<int64_t>({0, -int64_t(destY), -int64_t(sourceY)});
    const int64_t right = std::min<int64_t>({sourceWidth, int64_t(_width) - destX, int64_t(source._width) - sourceX});
    const int64_t bottom =
        std::min<int64_t>({sourceHeight, int64_t(_height) - destY, int64_t(source._height) - sourceY});
    if (right <= left || bottom <= top)
        return randomSeed;
    const int writeX = static_cast<int>(destX + left), writeY = static_cast<int>(destY + top);
    const int readX = static_cast<int>(sourceX + left), readY = static_cast<int>(sourceY + top);
    const uint32_t width = static_cast<uint32_t>(right - left);
    const uint32_t length = width * static_cast<uint32_t>(bottom - top);
    const std::vector<uint32_t> from = &source == this ? std::vector<uint32_t>() : source._pixels;
    const uint32_t fill = _transparent ? premultiplied(fillColor) : (fillColor | 0xff000000);
    const auto write = [&](uint32_t x, uint32_t y) {
        _pixels[static_cast<size_t>(writeY + y) * _width + writeX + x] =
            &source == this ? fill : from[static_cast<size_t>(readY + y) * source._width + readX + x];
    };
    // The pixels in an order a one-round Feistel network shuffles, the
    // first always first.
    uint32_t bits = 0;
    for (uint32_t n = std::max(length, 2u) - 1; n > 0; n /= 2)
        ++bits;
    bits += bits % 2;
    const uint32_t half = bits / 2;
    const uint32_t permutation = 1u << bits;
    const auto shuffled = [half](uint32_t index) {
        const uint32_t high = index >> half;
        const uint32_t low = index & ((1u << half) - 1);
        const uint32_t mixed = (low * low + 1) % (1u << half);
        return ((high ^ mixed) << half) | low;
    };
    write(0, 0);
    uint32_t raw = static_cast<uint32_t>(randomSeed % static_cast<int32_t>(permutation));
    const int count = std::min<int64_t>(numPixels, length) < 0 ? 0 : static_cast<int>(std::min<int64_t>(numPixels, length));
    for (int i = 0; i < count; ++i)
    {
        uint32_t index = 0;
        for (uint32_t guard = 0; (index == 0 || index >= length) && length != 1; )
        {
            raw = (raw + 1) % permutation;
            index = shuffled(raw);
            if (++guard > permutation + 2)
                break;
        }
        write(index % width, index / width);
    }
    touch();
    return static_cast<int>(raw);
}

void BitmapData::scroll(int dx, int dy)
{
    if (disposed() || (dx == 0 && dy == 0))
        return;
    const std::vector<uint32_t> from = _pixels;
    for (int y = 0; y < _height; ++y)
    {
        const int sy = y - dy;
        if (sy < 0 || sy >= _height)
            continue;
        for (int x = 0; x < _width; ++x)
        {
            const int sx = x - dx;
            if (sx < 0 || sx >= _width)
                continue;
            _pixels[static_cast<size_t>(y) * _width + x] = from[static_cast<size_t>(sy) * _width + sx];
        }
    }
    touch();
}

std::vector<uint8_t> BitmapData::getPixels(const Rect& rect) const
{
    std::vector<uint8_t> bytes;
    int x0, y0, x1, y1;
    if (!clip(rect, x0, y0, x1, y1))
        return bytes;
    bytes.reserve(static_cast<size_t>(x1 - x0) * (y1 - y0) * 4);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            const uint32_t pixel = unpremultiplied(_pixels[static_cast<size_t>(y) * _width + x]);
            bytes.push_back(static_cast<uint8_t>(pixel >> 24));
            bytes.push_back(static_cast<uint8_t>(pixel >> 16));
            bytes.push_back(static_cast<uint8_t>(pixel >> 8));
            bytes.push_back(static_cast<uint8_t>(pixel));
        }
    return bytes;
}

bool BitmapData::setPixels(const Rect& rect, const uint8_t* bytes, size_t length, size_t& position)
{
    int x0, y0, x1, y1;
    if (!clip(rect, x0, y0, x1, y1))
        return true;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            if (position + 4 > length)
            {
                touch();
                return false;
            }
            const uint32_t argb = (uint32_t(bytes[position]) << 24) | (uint32_t(bytes[position + 1]) << 16) |
                (uint32_t(bytes[position + 2]) << 8) | bytes[position + 3];
            position += 4;
            _pixels[static_cast<size_t>(y) * _width + x] = _transparent ? premultiplied(argb) : (argb | 0xff000000);
        }
    touch();
    return true;
}

std::vector<uint32_t> BitmapData::getVector(const Rect& rect) const
{
    std::vector<uint32_t> pixels;
    int x0, y0, x1, y1;
    if (!clip(rect, x0, y0, x1, y1))
        return pixels;
    pixels.reserve(static_cast<size_t>(x1 - x0) * (y1 - y0));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            pixels.push_back(unpremultiplied(_pixels[static_cast<size_t>(y) * _width + x]));
    return pixels;
}

void BitmapData::setVector(const Rect& rect, const std::vector<uint32_t>& pixels)
{
    int x0, y0, x1, y1;
    if (!clip(rect, x0, y0, x1, y1))
        return;
    size_t at = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1 && at < pixels.size(); ++x)
        {
            const uint32_t argb = pixels[at++];
            _pixels[static_cast<size_t>(y) * _width + x] = _transparent ? premultiplied(argb) : (argb | 0xff000000);
        }
    touch();
}

Rect BitmapData::getColorBoundsRect(uint32_t mask, uint32_t color, bool findColor) const
{
    // As the player compares: the pixels as they are kept (premultiplied)
    // and masked, with the color premultiplied too; an opaque bitmap's
    // alpha always matches.
    if (!_transparent)
        mask |= 0xff000000;
    color = _transparent ? premultiplied(color) : (color | 0xff000000);
    int l = _width, t = _height, r = 0, b = 0;
    for (int y = 0; y < _height; ++y)
        for (int x = 0; x < _width; ++x)
        {
            const bool same = (_pixels[static_cast<size_t>(y) * _width + x] & mask) == color;
            if (same != findColor)
                continue;
            l = std::min(l, x);
            r = std::max(r, x);
            t = std::min(t, y);
            b = std::max(b, y);
        }
    // A match at the corner alone counts as none.
    if (r == 0 && b == 0)
        return Rect();
    return Rect(l, t, r - l + 1, b - t + 1);
}

int BitmapData::compare(const BitmapData& other, std::shared_ptr<BitmapData>& differences) const
{
    differences.reset();
    if (_width != other._width)
        return -3;
    if (_height != other._height)
        return -4;
    bool any = false;
    auto out = std::make_shared<BitmapData>(_width, _height, true, 0);
    for (size_t i = 0; i < _pixels.size(); ++i)
    {
        const uint32_t a = unpremultiplied(_pixels[i]);
        const uint32_t b = unpremultiplied(other._pixels[i]);
        if (a == b)
            continue;
        any = true;
        uint32_t diff;
        if ((a & 0xffffff) != (b & 0xffffff))
        {
            const uint32_t r = (((a >> 16) & 0xff) - ((b >> 16) & 0xff)) & 0xff;
            const uint32_t g = (((a >> 8) & 0xff) - ((b >> 8) & 0xff)) & 0xff;
            const uint32_t bl = ((a & 0xff) - (b & 0xff)) & 0xff;
            diff = 0xff000000 | (r << 16) | (g << 8) | bl;
        }
        else
            diff = ((((a >> 24) - (b >> 24)) & 0xff) << 24) | 0xffffff;
        out->_pixels[i] = premultiplied(diff);
    }
    if (!any)
        return 0;
    differences = out;
    return 1;
}

void BitmapData::draw(const DisplayObject& source, const Matrix& matrix, const ColorTransform* colorTransform,
    BlendMode blendMode, const Rect* clipRect, bool smoothing)
{
    if (disposed())
        return;
    if (Backend* renderer = backend())
    {
        renderer->draw(*this, source, matrix, colorTransform, blendMode, clipRect, smoothing);
        touch();
    }
}

void BitmapData::draw(const BitmapData& source, const Matrix& matrix, const ColorTransform* colorTransform,
    BlendMode blendMode, const Rect* clipRect, bool smoothing)
{
    if (disposed() || source.disposed())
        return;
    bool invertible = false;
    const Matrix back = matrix.inverted(&invertible);
    if (!invertible)
        return;
    Rect area = matrix.mapRect(source.rect()).intersected(rect());
    if (clipRect)
        area = area.intersected(*clipRect);
    if (area.isEmpty())
        return;
    const int x0 = std::max(0, static_cast<int>(std::floor(area.left())));
    const int y0 = std::max(0, static_cast<int>(std::floor(area.top())));
    const int x1 = std::min(_width, static_cast<int>(std::ceil(area.right())));
    const int y1 = std::min(_height, static_cast<int>(std::ceil(area.bottom())));
    const std::vector<uint32_t> copied = &source == this ? _pixels : std::vector<uint32_t>();
    const uint32_t* from = &source == this ? copied.data() : source._pixels.data();
    const int sw = source._width;
    const int sh = source._height;
    const auto texel = [&](int x, int y) -> uint32_t {
        if (x < 0 || y < 0 || x >= sw || y >= sh)
            return 0;
        return from[static_cast<size_t>(y) * sw + x];
    };
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            // Sampled at the pixel's center.
            const Point at = back.map(x + 0.5, y + 0.5);
            uint32_t pixel;
            if (smoothing)
            {
                const double fx = at.x - 0.5, fy = at.y - 0.5;
                const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
                const double ux = fx - ix, uy = fy - iy;
                const uint32_t p00 = texel(ix, iy), p10 = texel(ix + 1, iy), p01 = texel(ix, iy + 1),
                               p11 = texel(ix + 1, iy + 1);
                pixel = 0;
                for (int shift = 0; shift < 32; shift += 8)
                {
                    const double v = ((p00 >> shift) & 0xff) * (1 - ux) * (1 - uy) + ((p10 >> shift) & 0xff) * ux * (1 - uy) +
                        ((p01 >> shift) & 0xff) * (1 - ux) * uy + ((p11 >> shift) & 0xff) * ux * uy;
                    pixel |= static_cast<uint32_t>(std::lround(v)) << shift;
                }
            }
            else
            {
                const int ix = static_cast<int>(std::floor(at.x)), iy = static_cast<int>(std::floor(at.y));
                if (ix < 0 || iy < 0 || ix >= sw || iy >= sh)
                    continue;
                pixel = texel(ix, iy);
            }
            if (colorTransform && !colorTransform->isIdentity())
                pixel = premultiplied(colorTransform->apply(unpremultiplied(pixel)));
            uint32_t& out = _pixels[static_cast<size_t>(y) * _width + x];
            out = blendPixel(blendMode, pixel, out);
            if (!_transparent)
                out = unpremultiplied(out) | 0xff000000;
        }
    touch();
}

} // namespace display
} // namespace fla
