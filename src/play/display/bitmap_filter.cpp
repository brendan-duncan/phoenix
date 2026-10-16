#include "bitmap_filter.h"

#include <algorithm>
#include <cmath>

namespace fla {
namespace display {

namespace {

double limit(double value, double low, double high)
{
    return std::max(low, std::min(high, value));
}

double blurOf(double amount)
{
    return std::isnan(amount) ? 0.0 : limit(amount, 0.0, 255.0);
}

int qualityOf(int quality)
{
    return std::max(0, std::min(15, quality));
}

} // namespace

BitmapFilter BitmapFilter::blur(double blurX, double blurY, int quality)
{
    BitmapFilter filter;
    filter.type = Type::Blur;
    filter.blurX = blurOf(blurX);
    filter.blurY = blurOf(blurY);
    filter.quality = qualityOf(quality);
    return filter;
}

BitmapFilter BitmapFilter::dropShadow(double distance, double angle, uint32_t color, double alpha, double blurX,
    double blurY, double strength, int quality, bool inner, bool knockout, bool hideObject)
{
    BitmapFilter filter = blur(blurX, blurY, quality);
    filter.type = Type::DropShadow;
    filter.distance = distance;
    filter.angle = angle;
    filter.color = color & 0xffffff;
    filter.alpha = limit(alpha, 0.0, 1.0);
    filter.strength = limit(strength, 0.0, 255.0);
    filter.inner = inner;
    filter.knockout = knockout;
    filter.hideObject = hideObject;
    return filter;
}

BitmapFilter BitmapFilter::glow(uint32_t color, double alpha, double blurX, double blurY, double strength, int quality,
    bool inner, bool knockout)
{
    BitmapFilter filter = blur(blurX, blurY, quality);
    filter.type = Type::Glow;
    filter.color = color & 0xffffff;
    filter.alpha = limit(alpha, 0.0, 1.0);
    filter.strength = limit(strength, 0.0, 255.0);
    filter.inner = inner;
    filter.knockout = knockout;
    return filter;
}

BitmapFilter BitmapFilter::bevel(double distance, double angle, uint32_t highlightColor, double highlightAlpha,
    uint32_t shadowColor, double shadowAlpha, double blurX, double blurY, double strength, int quality,
    const std::string& type, bool knockout)
{
    BitmapFilter filter = blur(blurX, blurY, quality);
    filter.type = Type::Bevel;
    filter.distance = distance;
    filter.angle = angle;
    filter.highlightColor = highlightColor & 0xffffff;
    filter.highlightAlpha = limit(highlightAlpha, 0.0, 1.0);
    filter.shadowColor = shadowColor & 0xffffff;
    filter.shadowAlpha = limit(shadowAlpha, 0.0, 1.0);
    filter.strength = limit(strength, 0.0, 255.0);
    filter.bevelType = type;
    filter.knockout = knockout;
    return filter;
}

BitmapFilter BitmapFilter::colorMatrix(const std::vector<double>& matrix)
{
    BitmapFilter filter;
    filter.type = Type::ColorMatrix;
    if (matrix.size() >= 20)
        filter.matrix.assign(matrix.begin(), matrix.begin() + 20);
    else
        filter.matrix = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
    for (double& value : filter.matrix)
        if (std::isnan(value))
            value = 0.0;
    return filter;
}

BitmapFilter BitmapFilter::convolution(int matrixX, int matrixY, const std::vector<double>& matrix, double divisor,
    double bias, bool preserveAlpha, bool clamp, uint32_t color, double alpha)
{
    BitmapFilter filter;
    filter.type = Type::Convolution;
    filter.matrixX = std::max(0, std::min(15, matrixX));
    filter.matrixY = std::max(0, std::min(15, matrixY));
    filter.matrix = matrix;
    filter.matrix.resize(static_cast<size_t>(filter.matrixX * filter.matrixY), 0.0);
    filter.divisor = divisor;
    filter.bias = bias;
    filter.preserveAlpha = preserveAlpha;
    filter.clamp = clamp;
    filter.color = color & 0xffffff;
    filter.alpha = std::max(0.0, std::min(1.0, alpha));
    filter.blurX = filter.blurY = 0.0;
    return filter;
}

void BitmapFilter::reach(double& left, double& top, double& right, double& bottom) const
{
    left = top = right = bottom = 0.0;
    if (type == Type::ColorMatrix)
        return;
    if (type == Type::Convolution)
    {
        left = right = matrixX / 2 + 1;
        top = bottom = matrixY / 2 + 1;
        return;
    }
    const int passes = std::max(1, quality);
    const double bx = std::ceil(blurX * passes / 2.0) + 1.0;
    const double by = std::ceil(blurY * passes / 2.0) + 1.0;
    left = right = bx;
    top = bottom = by;
    if (type == Type::DropShadow || type == Type::Bevel || type == Type::GradientBevel ||
        (type == Type::GradientGlow && distance != 0.0))
    {
        const double radians = angle * kPi / 180.0;
        const double dx = std::cos(radians) * distance;
        const double dy = std::sin(radians) * distance;
        // A bevel throws light one way and shade the other.
        const bool both = type != Type::DropShadow;
        right += std::max(0.0, dx) + (both ? std::max(0.0, -dx) : 0.0);
        left += std::max(0.0, -dx) + (both ? std::max(0.0, dx) : 0.0);
        bottom += std::max(0.0, dy) + (both ? std::max(0.0, -dy) : 0.0);
        top += std::max(0.0, -dy) + (both ? std::max(0.0, dy) : 0.0);
    }
}

Rect filteredRect(const Rect& rect, const std::vector<BitmapFilter>& filters)
{
    Rect grown = rect;
    for (const BitmapFilter& filter : filters)
    {
        double l, t, r, b;
        filter.reach(l, t, r, b);
        grown = Rect(grown.x - l, grown.y - t, grown.width + l + r, grown.height + t + b);
    }
    return grown;
}

void applyColorMatrix(uint32_t* pixels, int width, int height, int stride, const std::vector<double>& m)
{
    if (m.size() < 20)
        return;
    for (int y = 0; y < height; ++y)
    {
        uint32_t* line = pixels + static_cast<size_t>(y) * stride;
        for (int x = 0; x < width; ++x)
        {
            const uint32_t p = line[x];
            const double a = p >> 24;
            double r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
            // Worked on unpremultiplied color, as the player does.
            if (a > 0.0)
            {
                r = std::min(255.0, r * 255.0 / a);
                g = std::min(255.0, g * 255.0 / a);
                b = std::min(255.0, b * 255.0 / a);
            }
            const double nr = limit(m[0] * r + m[1] * g + m[2] * b + m[3] * a + m[4], 0.0, 255.0);
            const double ng = limit(m[5] * r + m[6] * g + m[7] * b + m[8] * a + m[9], 0.0, 255.0);
            const double nb = limit(m[10] * r + m[11] * g + m[12] * b + m[13] * a + m[14], 0.0, 255.0);
            const double na = limit(m[15] * r + m[16] * g + m[17] * b + m[18] * a + m[19], 0.0, 255.0);
            const double k = na / 255.0;
            const uint32_t ia = static_cast<uint32_t>(std::lround(na));
            line[x] = (ia << 24) | (static_cast<uint32_t>(std::lround(nr * k)) << 16) |
                (static_cast<uint32_t>(std::lround(ng * k)) << 8) | static_cast<uint32_t>(std::lround(nb * k));
        }
    }
}

void applyConvolution(uint32_t* pixels, int width, int height, int stride, const BitmapFilter& filter)
{
    const int mx = filter.matrixX;
    const int my = filter.matrixY;
    if (mx <= 0 || my <= 0 || static_cast<int>(filter.matrix.size()) < mx * my || width <= 0 || height <= 0)
        return;
    const double divisor = filter.divisor == 0.0 ? 1.0 : filter.divisor;
    // Unpremultiplied copy to read from.
    std::vector<double> source(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const uint32_t p = pixels[static_cast<size_t>(y) * stride + x];
            const double a = p >> 24;
            double* s = &source[(static_cast<size_t>(y) * width + x) * 4];
            s[3] = a;
            s[0] = a > 0.0 ? ((p >> 16) & 0xff) * 255.0 / a : 0.0;
            s[1] = a > 0.0 ? ((p >> 8) & 0xff) * 255.0 / a : 0.0;
            s[2] = a > 0.0 ? (p & 0xff) * 255.0 / a : 0.0;
        }
    const double edge[4] = {
        static_cast<double>((filter.color >> 16) & 0xff), static_cast<double>((filter.color >> 8) & 0xff),
        static_cast<double>(filter.color & 0xff), filter.alpha * 255.0};
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            double sum[4] = {0, 0, 0, 0};
            for (int j = 0; j < my; ++j)
                for (int i = 0; i < mx; ++i)
                {
                    int sx = x + i - mx / 2;
                    int sy = y + j - my / 2;
                    const double* s = nullptr;
                    if (sx < 0 || sy < 0 || sx >= width || sy >= height)
                    {
                        if (filter.clamp)
                        {
                            sx = std::max(0, std::min(width - 1, sx));
                            sy = std::max(0, std::min(height - 1, sy));
                            s = &source[(static_cast<size_t>(sy) * width + sx) * 4];
                        }
                        else
                            s = edge;
                    }
                    else
                        s = &source[(static_cast<size_t>(sy) * width + sx) * 4];
                    const double k = filter.matrix[static_cast<size_t>(j) * mx + i];
                    for (int c = 0; c < 4; ++c)
                        sum[c] += s[c] * k;
                }
            const double* self = &source[(static_cast<size_t>(y) * width + x) * 4];
            double out[4];
            for (int c = 0; c < 4; ++c)
                out[c] = limit(sum[c] / divisor + filter.bias, 0.0, 255.0);
            if (filter.preserveAlpha)
                out[3] = self[3];
            const double k = out[3] / 255.0;
            pixels[static_cast<size_t>(y) * stride + x] = (static_cast<uint32_t>(std::lround(out[3])) << 24) |
                (static_cast<uint32_t>(std::lround(out[0] * k)) << 16) |
                (static_cast<uint32_t>(std::lround(out[1] * k)) << 8) | static_cast<uint32_t>(std::lround(out[2] * k));
        }
}

namespace {

/// One box blur pass along a line of \a count pixels \a step apart.
void boxLine(uint32_t* line, int count, int step, int radius, std::vector<uint32_t>& scratch)
{
    if (radius <= 0 || count <= 0)
        return;
    scratch.resize(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
        scratch[static_cast<size_t>(i)] = line[static_cast<size_t>(i) * step];
    const int window = radius * 2 + 1;
    uint32_t sum[4] = {0, 0, 0, 0};
    const auto add = [&](int i, int sign) {
        if (i < 0 || i >= count)
            return;
        const uint32_t p = scratch[static_cast<size_t>(i)];
        if (sign > 0)
        {
            sum[0] += p >> 24;
            sum[1] += (p >> 16) & 0xff;
            sum[2] += (p >> 8) & 0xff;
            sum[3] += p & 0xff;
        }
        else
        {
            sum[0] -= p >> 24;
            sum[1] -= (p >> 16) & 0xff;
            sum[2] -= (p >> 8) & 0xff;
            sum[3] -= p & 0xff;
        }
    };
    for (int i = -radius; i <= radius; ++i)
        add(i, 1);
    for (int i = 0; i < count; ++i)
    {
        line[static_cast<size_t>(i) * step] = ((sum[0] / window) << 24) | ((sum[1] / window) << 16) |
            ((sum[2] / window) << 8) | (sum[3] / window);
        add(i - radius, -1);
        add(i + radius + 1, 1);
    }
}

} // namespace

void applyBoxBlur(uint32_t* pixels, int width, int height, int stride, double blurX, double blurY, int passes)
{
    const int rx = static_cast<int>(std::floor(blurOf(blurX) / 2.0));
    const int ry = static_cast<int>(std::floor(blurOf(blurY) / 2.0));
    std::vector<uint32_t> scratch;
    for (int pass = 0; pass < passes; ++pass)
    {
        if (rx > 0)
            for (int y = 0; y < height; ++y)
                boxLine(pixels + static_cast<size_t>(y) * stride, width, 1, rx, scratch);
        if (ry > 0)
            for (int x = 0; x < width; ++x)
                boxLine(pixels + x, height, stride, ry, scratch);
    }
}

} // namespace display
} // namespace fla
