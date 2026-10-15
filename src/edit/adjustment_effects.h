#pragma once

#include "behaviors.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace fla::compositing::effects {

/// Another layer of the timeline drawn as a picture the size of the one
/// being changed (premultiplied ARGB), by the layer's name; null when there
/// is none of that name. For Displacement Map.
using LayerPictures = std::function<const std::vector<uint32_t>*(const std::string& layer)>;

/// The effects beyond blur, color and tint: blurs (directional, radial,
/// lens), Glow, generators (gradient ramp, four-color gradient, fill,
/// outline, fractal noise), distortions (bulge, ripple, bend, displacement
/// map), color (levels, curves, hue and saturation, tritone, vignette,
/// grain, chromatic aberration) and transitions (linear and radial wipes,
/// venetian blinds, block dissolve). Free of Qt.
const std::vector<behaviors::Definition>& definitions();
bool handles(const std::string& type);

/// \a applied on \a pixels (premultiplied ARGB, \a before a copy of them as
/// they were), on \a frame, \a seconds into the movie, \a pixelsPerUnit
/// pixels to a stage pixel.
void apply(const behaviors::Applied& applied, uint32_t* pixels, const std::vector<uint32_t>& before, int width, int height,
    int frame, double seconds, double pixelsPerUnit, const LayerPictures* layers);

/// The layers \a list's effects draw from (Displacement Map's map), by name.
std::vector<std::string> layersNeeded(const std::vector<behaviors::Applied>& list, int frame);

} // namespace fla::compositing::effects
