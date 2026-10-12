#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fla {

class DOMElement;

/// Textured brushes, as Harmony's: a grain the paint takes on, as chalk
/// takes the tooth of the paper. A texture is a tile of coverage, 255 where
/// the paint lies fully and 0 where it misses, repeated over the stage. A
/// shape painted with one keeps the texture's name, scale and strength as
/// Phoenix attributes, which Animate ignores: there the shape shows plain.
/// Free of Qt.
namespace texture {

/// A square tile of coverage, row by row.
struct Tile
{
    int size = 0;
    std::vector<uint8_t> coverage;
};

/// What a shape is textured with. Scale and strength are fractions: a scale
/// of 1 shows the tile at its own size; a strength of 1 lets the grain take
/// the paint away fully where it is lightest, 0 shows no grain.
struct Spec
{
    std::string name;
    double scale = 1.0;
    double strength = 1.0;
    /// Where the grain's tile starts, in pixels, and its turn in degrees,
    /// so two strokes need not show the same grain.
    double offsetX = 0.0;
    double offsetY = 0.0;
    double rotation = 0.0;
    /// The grain keeps its size in the rendered frame rather than scaling
    /// with the art, as a camera zooms or a symbol is scaled.
    bool fixed = false;
    bool valid() const { return !name.empty(); }
};

/// The textures Phoenix makes itself, by family: papers (Paper, Rough
/// Paper, Watercolor Paper, Newsprint), dry media (Pencil, Chalk, Charcoal,
/// Pastel, Crayon), paint (Dry Brush, Watercolor, Sponge, Splatter,
/// Stipple) and patterns (Canvas, Linen, Crosshatch, Halftone, Grain).
const std::vector<std::string>& builtIn();

/// A built-in texture's tile, made the same way every time; empty for a
/// name that is not one. Tiles repeat without seams.
Tile builtInTile(const std::string& name);

/// The prefix that names a library bitmap as a texture: "bitmap:<name>".
constexpr const char* kBitmapPrefix = "bitmap:";

/// A shape's texture, read from its attributes; invalid when it has none.
Spec of(const DOMElement& element);

/// Sets (or, for an invalid spec, removes) the texture attributes.
void set(std::vector<std::pair<std::string, std::string>>& attributes, const Spec& spec);

} // namespace texture

} // namespace fla
