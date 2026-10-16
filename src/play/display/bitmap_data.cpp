#include "bitmap_data.h"

#include "backend.h"
#include "bitmap_filter.h"
#include "display_object.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fla {
namespace display {

namespace {

AlphaTables makeTables()
{
    AlphaTables tables;
    for (int a = 0; a < 256; ++a)
        for (int c = 0; c < 256; ++c)
        {
            tables.premultiply[a][c] = static_cast<uint8_t>(std::lround(c * a / 255.0));
            tables.unpremultiply[a][c] =
                a == 0 ? 0 : static_cast<uint8_t>(std::min(255L, std::lround(c * 255.0 / a)));
        }
    return tables;
}

/// The Park-Miller generator Flash Player's noise is made from.
struct Random
{
    explicit Random(int seed)
        : state(static_cast<uint32_t>(seed) % 2147483647u)
    {
        if (state == 0)
            state = 1;
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
double lattice(int x, int y, int seed, int channel)
{
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u +
        static_cast<uint32_t>(seed) * 2246822519u + static_cast<uint32_t>(channel) * 3266489917u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xffff) / 65535.0;
}

double smoothNoise(double x, double y, int seed, int channel, int wrapX, int wrapY)
{
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const double fx = x - x0;
    const double fy = y - y0;
    const auto at = [&](int ix, int iy) {
        if (wrapX > 0)
            ix = ((ix % wrapX) + wrapX) % wrapX;
        if (wrapY > 0)
            iy = ((iy % wrapY) + wrapY) % wrapY;
        return lattice(ix, iy, seed, channel);
    };
    const double sx = fx * fx * (3.0 - 2.0 * fx);
    const double sy = fy * fy * (3.0 - 2.0 * fy);
    const double top = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * sx;
    const double bottom = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * sx;
    return top + (bottom - top) * sy;
}

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

void BitmapData::copyPixels(const BitmapData& source, const Rect& sourceRect, const Point& destPoint,
    const BitmapData* alphaBitmap, const Point& alphaPoint, bool mergeAlpha)
{
    if (disposed() || source.disposed())
        return;
    int sx0, sy0, sx1, sy1;
    if (!source.clip(sourceRect, sx0, sy0, sx1, sy1))
        return;
    // Where the source's corner lands, moved by what was cut off it.
    int dx = static_cast<int>(std::floor(destPoint.x)) + (sx0 - static_cast<int>(std::floor(sourceRect.x)));
    int dy = static_cast<int>(std::floor(destPoint.y)) + (sy0 - static_cast<int>(std::floor(sourceRect.y)));
    const int ax = static_cast<int>(std::floor(alphaPoint.x)) + (sx0 - static_cast<int>(std::floor(sourceRect.x)));
    const int ay = static_cast<int>(std::floor(alphaPoint.y)) + (sy0 - static_cast<int>(std::floor(sourceRect.y)));
    // A copy onto itself reads what was there before.
    std::vector<uint32_t> copied;
    const uint32_t* from = source._pixels.data();
    if (&source == this)
    {
        copied = _pixels;
        from = copied.data();
    }
    const bool blend = mergeAlpha && source._transparent;
    for (int y = sy0; y < sy1; ++y)
    {
        const int ty = dy + (y - sy0);
        if (ty < 0 || ty >= _height)
            continue;
        for (int x = sx0; x < sx1; ++x)
        {
            const int tx = dx + (x - sx0);
            if (tx < 0 || tx >= _width)
                continue;
            uint32_t pixel = from[static_cast<size_t>(y) * source._width + x];
            if (alphaBitmap)
            {
                const uint32_t a = alphaBitmap->getPixel32(ax + (x - sx0), ay + (y - sy0)) >> 24;
                if (a != 255)
                {
                    uint32_t scaled = 0;
                    for (int shift = 0; shift < 32; shift += 8)
                        scaled |= ((((pixel >> shift) & 0xff) * a + 127) / 255) << shift;
                    pixel = scaled;
                }
            }
            uint32_t& out = _pixels[static_cast<size_t>(ty) * _width + tx];
            if (blend || (alphaBitmap && mergeAlpha))
                out = blendPixel(BlendMode::Normal, pixel, out);
            else
                out = pixel;
            if (!_transparent)
                out = unpremultiplied(out) | 0xff000000;
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
    const uint32_t wanted = thresholdValue & mask;
    const std::vector<uint32_t> from = source._pixels;
    uint32_t count = 0;
    const uint32_t setTo = _transparent ? premultiplied(color) : (color | 0xff000000);
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
            const uint32_t value = unpremultiplied(raw) & mask;
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
                pixel = _transparent ? raw : (unpremultiplied(raw) | 0xff000000);
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
        filter.type == BitmapFilter::Type::Blur)
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
    const double fx0 = baseX != 0.0 ? 1.0 / baseX : 0.0;
    const double fy0 = baseY != 0.0 ? 1.0 / baseY : 0.0;
    const int channels[4] = {1, 2, 4, 8};
    for (int y = 0; y < _height; ++y)
        for (int x = 0; x < _width; ++x)
        {
            double values[4] = {0, 0, 0, 0};
            for (int c = 0; c < 4; ++c)
            {
                if (!(channelOptions & channels[c]) && !(grayScale && c < 3))
                    continue;
                if (grayScale && c > 0 && c < 3)
                {
                    values[c] = values[0];
                    continue;
                }
                double sum = 0.0, amplitude = 1.0, total = 0.0, fx = fx0, fy = fy0;
                for (unsigned octave = 0; octave < std::max(1u, numOctaves); ++octave)
                {
                    const Point offset = octave < offsets.size() ? offsets[octave] : Point();
                    const int wrapX = stitch && fx > 0.0 ? std::max(1, static_cast<int>(std::lround(_width * fx))) : 0;
                    const int wrapY = stitch && fy > 0.0 ? std::max(1, static_cast<int>(std::lround(_height * fy))) : 0;
                    double n = smoothNoise((x + offset.x) * fx, (y + offset.y) * fy, randomSeed + static_cast<int>(octave),
                        c, wrapX, wrapY);
                    if (!fractalNoise)
                        n = std::fabs(n * 2.0 - 1.0);
                    sum += n * amplitude;
                    total += amplitude;
                    amplitude *= 0.5;
                    fx *= 2.0;
                    fy *= 2.0;
                }
                values[c] = total > 0.0 ? sum / total : 0.0;
            }
            const auto byte = [](double v) { return static_cast<uint32_t>(std::max(0.0, std::min(255.0, v * 255.0))); };
            const uint32_t a = (channelOptions & 8) && _transparent ? byte(values[3]) : 255;
            const uint32_t argb = (a << 24) | (byte(values[0]) << 16) | (byte(values[1]) << 8) | byte(values[2]);
            _pixels[static_cast<size_t>(y) * _width + x] = _transparent ? premultiplied(argb) : (argb | 0xff000000);
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
    int l = _width, t = _height, r = -1, b = -1;
    for (int y = 0; y < _height; ++y)
        for (int x = 0; x < _width; ++x)
        {
            const bool same = (unpremultiplied(_pixels[static_cast<size_t>(y) * _width + x]) & mask) == (color & mask);
            if (same != findColor)
                continue;
            l = std::min(l, x);
            r = std::max(r, x);
            t = std::min(t, y);
            b = std::max(b, y);
        }
    if (r < 0)
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
