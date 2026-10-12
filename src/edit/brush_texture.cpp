#include "brush_texture.h"

#include "../data/dom_element.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace fla::texture {

namespace {

constexpr int kSize = 256;
constexpr double kPi = 3.14159265358979323846;

const char* const kName = "phoenixTexture";
const char* const kScale = "phoenixTextureScale";
const char* const kStrength = "phoenixTextureStrength";
const char* const kOffset = "phoenixTextureOffset";
const char* const kRotation = "phoenixTextureRotation";
const char* const kFixed = "phoenixTextureFixed";

/// A hash of lattice coordinates to [0, 1), the same on every run.
double hash(int x, int y, int seed)
{
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u +
                 static_cast<uint32_t>(seed) * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFF) / static_cast<double>(0x1000000);
}

double smooth(double t)
{
    return t * t * (3.0 - 2.0 * t);
}

/// Value noise on a lattice of \a cells cells across the tile, wrapping so
/// the tile repeats without a seam.
double valueNoise(double x, double y, int cells, int seed)
{
    const double fx = x / kSize * cells;
    const double fy = y / kSize * cells;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const double tx = smooth(fx - x0);
    const double ty = smooth(fy - y0);
    const auto at = [cells, seed](int ix, int iy) {
        return hash(((ix % cells) + cells) % cells, ((iy % cells) + cells) % cells, seed);
    };
    const double a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * tx;
    const double b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * tx;
    return a + (b - a) * ty;
}

/// Value noise stretched along one axis: \a cellsX cells across and
/// \a cellsY down, each wrapping, so streaks repeat without a seam.
double streakNoise(double x, double y, int cellsX, int cellsY, int seed)
{
    const double fx = x / kSize * cellsX;
    const double fy = y / kSize * cellsY;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const double tx = smooth(fx - x0);
    const double ty = smooth(fy - y0);
    const auto at = [cellsX, cellsY, seed](int ix, int iy) {
        return hash(((ix % cellsX) + cellsX) % cellsX, ((iy % cellsY) + cellsY) % cellsY, seed);
    };
    const double a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * tx;
    const double b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * tx;
    return a + (b - a) * ty;
}

/// Octaves of value noise, each twice as fine and half as strong.
double fractal(double x, double y, int cells, int octaves, int seed)
{
    double sum = 0.0;
    double weight = 0.5;
    double total = 0.0;
    for (int octave = 0; octave < octaves; ++octave)
    {
        sum += valueNoise(x, y, cells << octave, seed + octave * 31) * weight;
        total += weight;
        weight *= 0.5;
    }
    return sum / total;
}

/// Distance to the nearest of a few points scattered in each cell, wrapping.
double cellular(double x, double y, int cells, int seed)
{
    const double cell = static_cast<double>(kSize) / cells;
    const int cx = static_cast<int>(x / cell);
    const int cy = static_cast<int>(y / cell);
    double nearest = 1.0e9;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int ix = cx + dx;
            const int iy = cy + dy;
            const int wx = ((ix % cells) + cells) % cells;
            const int wy = ((iy % cells) + cells) % cells;
            const double px = (ix + hash(wx, wy, seed)) * cell;
            const double py = (iy + hash(wx, wy, seed + 7)) * cell;
            nearest = std::min(nearest, std::hypot(px - x, py - y));
        }
    }
    return std::min(1.0, nearest / cell);
}

/// Round dots scattered one to a cell, wrapping: 1 inside a dot, 0 outside,
/// with a soft edge a pixel wide. Each cell has a dot with chance
/// \a chance, its radius between \a minRadius and \a maxRadius pixels.
double dots(double x, double y, int cells, int seed, double minRadius, double maxRadius, double chance)
{
    const double cell = static_cast<double>(kSize) / cells;
    const int cx = static_cast<int>(x / cell);
    const int cy = static_cast<int>(y / cell);
    double coverage = 0.0;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int ix = cx + dx;
            const int iy = cy + dy;
            const int wx = ((ix % cells) + cells) % cells;
            const int wy = ((iy % cells) + cells) % cells;
            if (hash(wx, wy, seed + 3) >= chance)
                continue;
            const double px = (ix + hash(wx, wy, seed)) * cell;
            const double py = (iy + hash(wx, wy, seed + 7)) * cell;
            const double radius = minRadius + (maxRadius - minRadius) * hash(wx, wy, seed + 13);
            const double inside = radius + 0.5 - std::hypot(px - x, py - y);
            coverage = std::max(coverage, std::clamp(inside, 0.0, 1.0));
        }
    }
    return coverage;
}

/// 1 on a line one pixel or so wide repeating every \a period pixels along
/// \a t, 0 between, soft edged. Periods that divide the tile keep it
/// seamless.
double lines(double t, double period, double width)
{
    const double phase = t - period * std::floor(t / period);
    const double distance = std::min(phase, period - phase);
    return std::clamp(width * 0.5 + 0.5 - distance, 0.0, 1.0);
}

uint8_t level(double value)
{
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
}

Tile make(const std::string& name)
{
    Tile tile;
    tile.size = kSize;
    tile.coverage.resize(static_cast<size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y)
    {
        for (int x = 0; x < kSize; ++x)
        {
            double v = 1.0;
            if (name == "Paper")
            {
                // Fine tooth: mostly covered, light pits.
                const double n = fractal(x, y, 32, 3, 11);
                v = 0.55 + n * 0.9;
            }
            else if (name == "Chalk")
            {
                // Coarse tooth that the chalk skips over in patches.
                const double n = fractal(x, y, 32, 4, 23);
                v = (n - 0.32) * 3.0;
            }
            else if (name == "Rough Paper")
            {
                // A heavy, toothy paper: broad hollows the paint skips.
                const double n = fractal(x, y, 16, 4, 101);
                v = (n - 0.3) * 2.4;
            }
            else if (name == "Watercolor Paper")
            {
                // Cold-pressed: rounded bumps, paint pooling between them.
                const double d = cellular(x, y, 24, 107);
                const double n = fractal(x, y, 32, 2, 109);
                v = 0.25 + d * 1.0 + (n - 0.5) * 0.5;
            }
            else if (name == "Newsprint")
            {
                // Cheap paper: short fibers lying every way, faint and even.
                const double across = streakNoise(x, y, 16, 128, 113);
                const double down = streakNoise(x, y, 128, 16, 127);
                v = 0.55 + (across - 0.5) * 0.9 + (down - 0.5) * 0.9;
            }
            else if (name == "Pencil")
            {
                // Graphite catching only the top of a fine tooth.
                const double n = fractal(x, y, 64, 3, 131);
                v = (n - 0.3) * 2.6;
            }
            else if (name == "Charcoal")
            {
                // Streaks along the stroke, the stick dragging.
                const double streak = streakNoise(x, y, 16, 256, 37);
                const double n = fractal(x, y, 32, 2, 41);
                v = (streak * 0.7 + n * 0.3 - 0.3) * 2.6;
            }
            else if (name == "Pastel")
            {
                // Soft chalk: a powdery grain that fills in as it builds.
                const double n = fractal(x, y, 16, 5, 137);
                v = (n - 0.22) * 2.0;
            }
            else if (name == "Crayon")
            {
                // Wax skating over the tooth, with streaks of bare paper.
                const double tooth = fractal(x, y, 32, 3, 139);
                const double streak = streakNoise(x, y, 8, 128, 149);
                v = (tooth * 0.6 + streak * 0.4 - 0.4) * 4.0;
            }
            else if (name == "Dry Brush")
            {
                // A nearly dry brush: bristle tracks with gaps between them.
                const double bristles = streakNoise(x, y, 4, 128, 151);
                const double n = fractal(x, y, 32, 2, 157);
                v = (bristles * 0.8 + n * 0.2 - 0.38) * 3.2;
            }
            else if (name == "Watercolor")
            {
                // Uneven washes, the pigment gathering at their edges.
                const double wash = fractal(x, y, 8, 4, 163);
                const double edge = std::abs(fractal(x, y, 4, 3, 167) - 0.5);
                v = 0.45 + (wash - 0.5) * 1.2 + 0.35 * std::max(0.0, 1.0 - edge * 14.0);
            }
            else if (name == "Splatter")
            {
                // Drops flicked from a loaded brush, large and small.
                const double big = dots(x, y, 8, 173, 2.0, 9.0, 0.6);
                const double small = dots(x, y, 32, 179, 0.6, 2.2, 0.7);
                v = std::max(big, small);
            }
            else if (name == "Stipple")
            {
                // Dense dots, as from the point of a pen.
                v = dots(x, y, 48, 181, 0.8, 1.8, 0.95);
            }
            else if (name == "Linen")
            {
                // A finer, uneven weave: thick and thin threads.
                const double warp = 0.5 + 0.5 * std::sin(x * 2.0 * kPi / 4.0);
                const double weft = 0.5 + 0.5 * std::sin(y * 2.0 * kPi / 4.0);
                const double slubX = streakNoise(x, y, 64, 4, 191);
                const double slubY = streakNoise(x, y, 4, 64, 193);
                v = 0.2 + 0.5 * std::max(warp * (0.6 + slubX * 0.8), weft * (0.6 + slubY * 0.8));
            }
            else if (name == "Crosshatch")
            {
                // Two sets of diagonal lines, slightly wavering.
                const double wobble = (fractal(x, y, 16, 2, 197) - 0.5) * 2.0;
                const double one = lines(x + y + wobble, 16.0, 2.2);
                const double other = lines(x - y + wobble, 16.0, 2.2);
                v = std::max(one, other);
            }
            else if (name == "Halftone")
            {
                // A printer's screen of even dots.
                const double fx = x - 16.0 * std::floor(x / 16.0) - 8.0;
                const double fy = y - 16.0 * std::floor(y / 16.0) - 8.0;
                v = std::clamp(5.5 + 0.5 - std::hypot(fx, fy), 0.0, 1.0);
            }
            else if (name == "Canvas")
            {
                // A weave: threads over and under, with some unevenness.
                const double warp = 0.5 + 0.5 * std::sin(x * 2.0 * kPi / 8.0);
                const double weft = 0.5 + 0.5 * std::sin(y * 2.0 * kPi / 8.0);
                const double n = fractal(x, y, 32, 2, 53);
                v = 0.45 + 0.35 * std::max(warp, weft) + 0.4 * (n - 0.5);
            }
            else if (name == "Sponge")
            {
                // Holes of the sponge, where no paint lands.
                const double d = cellular(x, y, 12, 61);
                v = (d - 0.22) * 2.8;
            }
            else if (name == "Grain")
            {
                // Film grain: fine and even.
                const double n = valueNoise(x, y, 128, 71);
                v = 0.5 + (n - 0.5) * 1.6;
            }
            else
            {
                return Tile();
            }
            tile.coverage[static_cast<size_t>(y) * kSize + x] = level(v);
        }
    }
    return tile;
}

std::string number(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.4g", value);
    return buffer;
}

} // namespace

const std::vector<std::string>& builtIn()
{
    // By family: papers, dry media, paint, then patterns. Names are what
    // documents store, so a name once shipped never changes.
    static const std::vector<std::string> names = {"Paper", "Rough Paper", "Watercolor Paper", "Newsprint",
        "Pencil", "Chalk", "Charcoal", "Pastel", "Crayon", "Dry Brush", "Watercolor", "Sponge", "Splatter",
        "Stipple", "Canvas", "Linen", "Crosshatch", "Halftone", "Grain"};
    return names;
}

Tile builtInTile(const std::string& name)
{
    // Made once each; they never change.
    static std::map<std::string, Tile> made;
    const auto found = made.find(name);
    if (found != made.end())
        return found->second;
    Tile tile = make(name);
    if (tile.size > 0)
        made[name] = tile;
    return tile;
}

Spec of(const DOMElement& element)
{
    Spec spec;
    for (const auto& attribute : element.extraAttributes)
    {
        if (attribute.first == kName)
            spec.name = attribute.second;
        else if (attribute.first == kScale)
            spec.scale = std::max(0.01, std::atof(attribute.second.c_str()));
        else if (attribute.first == kStrength)
            spec.strength = std::clamp(std::atof(attribute.second.c_str()), 0.0, 1.0);
        else if (attribute.first == kOffset)
        {
            // "x,y"
            const size_t comma = attribute.second.find(',');
            spec.offsetX = std::atof(attribute.second.c_str());
            spec.offsetY = comma == std::string::npos ? 0.0 : std::atof(attribute.second.c_str() + comma + 1);
        }
        else if (attribute.first == kRotation)
            spec.rotation = std::atof(attribute.second.c_str());
        else if (attribute.first == kFixed)
            spec.fixed = attribute.second == "true";
    }
    return spec;
}

void set(std::vector<std::pair<std::string, std::string>>& attributes, const Spec& spec)
{
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                         [](const std::pair<std::string, std::string>& a) {
                             return a.first == kName || a.first == kScale || a.first == kStrength ||
                                 a.first == kOffset || a.first == kRotation || a.first == kFixed;
                         }),
        attributes.end());
    if (!spec.valid())
        return;
    attributes.emplace_back(kName, spec.name);
    attributes.emplace_back(kScale, number(spec.scale));
    attributes.emplace_back(kStrength, number(spec.strength));
    if (spec.offsetX != 0.0 || spec.offsetY != 0.0)
        attributes.emplace_back(kOffset, number(spec.offsetX) + "," + number(spec.offsetY));
    if (spec.rotation != 0.0)
        attributes.emplace_back(kRotation, number(spec.rotation));
    if (spec.fixed)
        attributes.emplace_back(kFixed, "true");
}

} // namespace fla::texture
