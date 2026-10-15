#pragma once

#include "behaviors.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fla {

class Layer;

/// Compositing on layers, Phoenix's own: a track matte (a layer shown
/// through the layer above it, by that layer's alpha or brightness) and
/// adjustment layers (effects on everything below them: blur, color, tint).
/// Kept in the layers' phoenixMatte, phoenixAdjustmentLayer and
/// phoenixAdjustments attributes, which other apps that open the FLA
/// ignore: they show the layers plainly. Free of Qt.
namespace compositing {

enum class Matte
{
    None,
    Alpha,
    AlphaInverted,
    Luma,
    LumaInverted
};

extern const char* const kMatte;
extern const char* const kAdjustmentLayer;
extern const char* const kAdjustments;

/// The matte \a layer is shown through, and its attributes set to \a matte.
Matte matteOf(const Layer& layer);
std::vector<std::pair<std::string, std::string>> withMatte(const Layer& layer, Matte matte);
const char* matteName(Matte matte);

/// Whether \a layer is an adjustment layer, its effects, and its
/// attributes made one (or not).
bool isAdjustment(const Layer& layer);
std::vector<behaviors::Applied> effectsOf(const Layer& layer);
std::vector<std::pair<std::string, std::string>> withAdjustment(const Layer& layer, bool on);

/// The effects an adjustment layer can have, with their settings.
const std::vector<behaviors::Definition>& definitions();
const behaviors::Definition* find(const std::string& id);
behaviors::Applied defaults(const std::string& id, int seed);

/// \a list on an image (premultiplied ARGB, a row after another) on frame
/// \a frame of a movie at \a frameRate; \a pixelsPerUnit is how many of its
/// pixels a stage pixel covers, for the reach of a blur or a displacement.
/// \a coverage, when given (one byte a pixel), limits the effects to where
/// it is set, fading at its edges.
void apply(uint32_t* pixels, int width, int height, const std::vector<behaviors::Applied>& list, int frame,
    double pixelsPerUnit, const uint8_t* coverage = nullptr, double frameRate = 24.0);

/// The matte's strength at each pixel of \a matte (premultiplied ARGB),
/// from 0 to 255, for \a kind.
std::vector<uint8_t> matteStrength(const uint32_t* matte, int width, int height, Matte kind);

} // namespace compositing

} // namespace fla
