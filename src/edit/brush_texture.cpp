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
            else if (name == "Charcoal")
            {
                // Streaks along the stroke, the stick dragging.
                const double streak = valueNoise(x * 0.25, y * 4.0, 64, 37);
                const double n = fractal(x, y, 32, 2, 41);
                v = (streak * 0.7 + n * 0.3 - 0.3) * 2.6;
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
    static const std::vector<std::string> names = {"Paper", "Chalk", "Charcoal", "Canvas", "Sponge", "Grain"};
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
    }
    return spec;
}

void set(std::vector<std::pair<std::string, std::string>>& attributes, const Spec& spec)
{
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                         [](const std::pair<std::string, std::string>& a) {
                             return a.first == kName || a.first == kScale || a.first == kStrength;
                         }),
        attributes.end());
    if (!spec.valid())
        return;
    attributes.emplace_back(kName, spec.name);
    attributes.emplace_back(kScale, number(spec.scale));
    attributes.emplace_back(kStrength, number(spec.strength));
}

} // namespace fla::texture
